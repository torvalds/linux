//===--- SemaOpenMP.cpp - Semantic Analysis for OpenMP constructs ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements semantic analysis for OpenMP directives and
/// clauses.
///
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaOpenMP.h"

#include "TreeTransform.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/PointerEmbeddedInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Frontend/OpenMP/OMPAssume.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"
#include "llvm/IR/Assumptions.h"
#include <optional>
#include <set>

using namespace clang;
using namespace llvm::omp;

//===----------------------------------------------------------------------===//
// Stack of data-sharing attributes for variables
//===----------------------------------------------------------------------===//

static const Expr *checkMapClauseExpressionBase(
    Sema &SemaRef, Expr *E,
    OMPClauseMappableExprCommon::MappableExprComponentList &CurComponents,
    OpenMPClauseKind CKind, OpenMPDirectiveKind DKind, bool NoDiagnose);

namespace {
/// Default data sharing attributes, which can be applied to directive.
enum DefaultDataSharingAttributes {
  DSA_unspecified = 0,       /// Data sharing attribute not specified.
  DSA_none = 1 << 0,         /// Default data sharing attribute 'none'.
  DSA_shared = 1 << 1,       /// Default data sharing attribute 'shared'.
  DSA_private = 1 << 2,      /// Default data sharing attribute 'private'.
  DSA_firstprivate = 1 << 3, /// Default data sharing attribute 'firstprivate'.
};

/// Stack for tracking declarations used in OpenMP directives and
/// clauses and their data-sharing attributes.
class DSAStackTy {
public:
  struct DSAVarData {
    OpenMPDirectiveKind DKind = OMPD_unknown;
    OpenMPClauseKind CKind = OMPC_unknown;
    unsigned Modifier = 0;
    const Expr *RefExpr = nullptr;
    DeclRefExpr *PrivateCopy = nullptr;
    SourceLocation ImplicitDSALoc;
    bool AppliedToPointee = false;
    DSAVarData() = default;
    DSAVarData(OpenMPDirectiveKind DKind, OpenMPClauseKind CKind,
               const Expr *RefExpr, DeclRefExpr *PrivateCopy,
               SourceLocation ImplicitDSALoc, unsigned Modifier,
               bool AppliedToPointee)
        : DKind(DKind), CKind(CKind), Modifier(Modifier), RefExpr(RefExpr),
          PrivateCopy(PrivateCopy), ImplicitDSALoc(ImplicitDSALoc),
          AppliedToPointee(AppliedToPointee) {}
  };
  using OperatorOffsetTy =
      llvm::SmallVector<std::pair<Expr *, OverloadedOperatorKind>, 4>;
  using DoacrossClauseMapTy = llvm::DenseMap<OMPClause *, OperatorOffsetTy>;
  /// Kind of the declaration used in the uses_allocators clauses.
  enum class UsesAllocatorsDeclKind {
    /// Predefined allocator
    PredefinedAllocator,
    /// User-defined allocator
    UserDefinedAllocator,
    /// The declaration that represent allocator trait
    AllocatorTrait,
  };

private:
  struct DSAInfo {
    OpenMPClauseKind Attributes = OMPC_unknown;
    unsigned Modifier = 0;
    /// Pointer to a reference expression and a flag which shows that the
    /// variable is marked as lastprivate(true) or not (false).
    llvm::PointerIntPair<const Expr *, 1, bool> RefExpr;
    DeclRefExpr *PrivateCopy = nullptr;
    /// true if the attribute is applied to the pointee, not the variable
    /// itself.
    bool AppliedToPointee = false;
  };
  using DeclSAMapTy = llvm::SmallDenseMap<const ValueDecl *, DSAInfo, 8>;
  using UsedRefMapTy = llvm::SmallDenseMap<const ValueDecl *, const Expr *, 8>;
  using LCDeclInfo = std::pair<unsigned, VarDecl *>;
  using LoopControlVariablesMapTy =
      llvm::SmallDenseMap<const ValueDecl *, LCDeclInfo, 8>;
  /// Struct that associates a component with the clause kind where they are
  /// found.
  struct MappedExprComponentTy {
    OMPClauseMappableExprCommon::MappableExprComponentLists Components;
    OpenMPClauseKind Kind = OMPC_unknown;
  };
  using MappedExprComponentsTy =
      llvm::DenseMap<const ValueDecl *, MappedExprComponentTy>;
  using CriticalsWithHintsTy =
      llvm::StringMap<std::pair<const OMPCriticalDirective *, llvm::APSInt>>;
  struct ReductionData {
    using BOKPtrType = llvm::PointerEmbeddedInt<BinaryOperatorKind, 16>;
    SourceRange ReductionRange;
    llvm::PointerUnion<const Expr *, BOKPtrType> ReductionOp;
    ReductionData() = default;
    void set(BinaryOperatorKind BO, SourceRange RR) {
      ReductionRange = RR;
      ReductionOp = BO;
    }
    void set(const Expr *RefExpr, SourceRange RR) {
      ReductionRange = RR;
      ReductionOp = RefExpr;
    }
  };
  using DeclReductionMapTy =
      llvm::SmallDenseMap<const ValueDecl *, ReductionData, 4>;
  struct DefaultmapInfo {
    OpenMPDefaultmapClauseModifier ImplicitBehavior =
        OMPC_DEFAULTMAP_MODIFIER_unknown;
    SourceLocation SLoc;
    DefaultmapInfo() = default;
    DefaultmapInfo(OpenMPDefaultmapClauseModifier M, SourceLocation Loc)
        : ImplicitBehavior(M), SLoc(Loc) {}
  };

  struct SharingMapTy {
    DeclSAMapTy SharingMap;
    DeclReductionMapTy ReductionMap;
    UsedRefMapTy AlignedMap;
    UsedRefMapTy NontemporalMap;
    MappedExprComponentsTy MappedExprComponents;
    LoopControlVariablesMapTy LCVMap;
    DefaultDataSharingAttributes DefaultAttr = DSA_unspecified;
    SourceLocation DefaultAttrLoc;
    DefaultmapInfo DefaultmapMap[OMPC_DEFAULTMAP_unknown + 1];
    OpenMPDirectiveKind Directive = OMPD_unknown;
    /// GenericLoopDirective with bind clause is mapped to other directives,
    /// like for, distribute and simd. Presently, set MappedDirective to
    /// OMPLoop. This may also be used in a similar way for other constructs.
    OpenMPDirectiveKind MappedDirective = OMPD_unknown;
    DeclarationNameInfo DirectiveName;
    Scope *CurScope = nullptr;
    DeclContext *Context = nullptr;
    SourceLocation ConstructLoc;
    /// Set of 'depend' clauses with 'sink|source' dependence kind. Required to
    /// get the data (loop counters etc.) about enclosing loop-based construct.
    /// This data is required during codegen.
    DoacrossClauseMapTy DoacrossDepends;
    /// First argument (Expr *) contains optional argument of the
    /// 'ordered' clause, the second one is true if the regions has 'ordered'
    /// clause, false otherwise.
    std::optional<std::pair<const Expr *, OMPOrderedClause *>> OrderedRegion;
    bool RegionHasOrderConcurrent = false;
    unsigned AssociatedLoops = 1;
    bool HasMutipleLoops = false;
    const Decl *PossiblyLoopCounter = nullptr;
    bool NowaitRegion = false;
    bool UntiedRegion = false;
    bool CancelRegion = false;
    bool LoopStart = false;
    bool BodyComplete = false;
    SourceLocation PrevScanLocation;
    SourceLocation PrevOrderedLocation;
    SourceLocation InnerTeamsRegionLoc;
    /// Reference to the taskgroup task_reduction reference expression.
    Expr *TaskgroupReductionRef = nullptr;
    llvm::DenseSet<QualType> MappedClassesQualTypes;
    SmallVector<Expr *, 4> InnerUsedAllocators;
    llvm::DenseSet<CanonicalDeclPtr<Decl>> ImplicitTaskFirstprivates;
    /// List of globals marked as declare target link in this target region
    /// (isOpenMPTargetExecutionDirective(Directive) == true).
    llvm::SmallVector<DeclRefExpr *, 4> DeclareTargetLinkVarDecls;
    /// List of decls used in inclusive/exclusive clauses of the scan directive.
    llvm::DenseSet<CanonicalDeclPtr<Decl>> UsedInScanDirective;
    llvm::DenseMap<CanonicalDeclPtr<const Decl>, UsesAllocatorsDeclKind>
        UsesAllocatorsDecls;
    /// Data is required on creating capture fields for implicit
    /// default first|private clause.
    struct ImplicitDefaultFDInfoTy {
      /// Field decl.
      const FieldDecl *FD = nullptr;
      /// Nesting stack level
      size_t StackLevel = 0;
      /// Capture variable decl.
      VarDecl *VD = nullptr;
      ImplicitDefaultFDInfoTy(const FieldDecl *FD, size_t StackLevel,
                              VarDecl *VD)
          : FD(FD), StackLevel(StackLevel), VD(VD) {}
    };
    /// List of captured fields
    llvm::SmallVector<ImplicitDefaultFDInfoTy, 8>
        ImplicitDefaultFirstprivateFDs;
    Expr *DeclareMapperVar = nullptr;
    SmallVector<VarDecl *, 16> IteratorVarDecls;
    SharingMapTy(OpenMPDirectiveKind DKind, DeclarationNameInfo Name,
                 Scope *CurScope, SourceLocation Loc)
        : Directive(DKind), DirectiveName(Name), CurScope(CurScope),
          ConstructLoc(Loc) {}
    SharingMapTy() = default;
  };

  using StackTy = SmallVector<SharingMapTy, 4>;

  /// Stack of used declaration and their data-sharing attributes.
  DeclSAMapTy Threadprivates;
  const FunctionScopeInfo *CurrentNonCapturingFunctionScope = nullptr;
  SmallVector<std::pair<StackTy, const FunctionScopeInfo *>, 4> Stack;
  /// true, if check for DSA must be from parent directive, false, if
  /// from current directive.
  OpenMPClauseKind ClauseKindMode = OMPC_unknown;
  Sema &SemaRef;
  bool ForceCapturing = false;
  /// true if all the variables in the target executable directives must be
  /// captured by reference.
  bool ForceCaptureByReferenceInTargetExecutable = false;
  CriticalsWithHintsTy Criticals;
  unsigned IgnoredStackElements = 0;

  /// Iterators over the stack iterate in order from innermost to outermost
  /// directive.
  using const_iterator = StackTy::const_reverse_iterator;
  const_iterator begin() const {
    return Stack.empty() ? const_iterator()
                         : Stack.back().first.rbegin() + IgnoredStackElements;
  }
  const_iterator end() const {
    return Stack.empty() ? const_iterator() : Stack.back().first.rend();
  }
  using iterator = StackTy::reverse_iterator;
  iterator begin() {
    return Stack.empty() ? iterator()
                         : Stack.back().first.rbegin() + IgnoredStackElements;
  }
  iterator end() {
    return Stack.empty() ? iterator() : Stack.back().first.rend();
  }

  // Convenience operations to get at the elements of the stack.

  bool isStackEmpty() const {
    return Stack.empty() ||
           Stack.back().second != CurrentNonCapturingFunctionScope ||
           Stack.back().first.size() <= IgnoredStackElements;
  }
  size_t getStackSize() const {
    return isStackEmpty() ? 0
                          : Stack.back().first.size() - IgnoredStackElements;
  }

  SharingMapTy *getTopOfStackOrNull() {
    size_t Size = getStackSize();
    if (Size == 0)
      return nullptr;
    return &Stack.back().first[Size - 1];
  }
  const SharingMapTy *getTopOfStackOrNull() const {
    return const_cast<DSAStackTy &>(*this).getTopOfStackOrNull();
  }
  SharingMapTy &getTopOfStack() {
    assert(!isStackEmpty() && "no current directive");
    return *getTopOfStackOrNull();
  }
  const SharingMapTy &getTopOfStack() const {
    return const_cast<DSAStackTy &>(*this).getTopOfStack();
  }

  SharingMapTy *getSecondOnStackOrNull() {
    size_t Size = getStackSize();
    if (Size <= 1)
      return nullptr;
    return &Stack.back().first[Size - 2];
  }
  const SharingMapTy *getSecondOnStackOrNull() const {
    return const_cast<DSAStackTy &>(*this).getSecondOnStackOrNull();
  }

  /// Get the stack element at a certain level (previously returned by
  /// \c getNestingLevel).
  ///
  /// Note that nesting levels count from outermost to innermost, and this is
  /// the reverse of our iteration order where new inner levels are pushed at
  /// the front of the stack.
  SharingMapTy &getStackElemAtLevel(unsigned Level) {
    assert(Level < getStackSize() && "no such stack element");
    return Stack.back().first[Level];
  }
  const SharingMapTy &getStackElemAtLevel(unsigned Level) const {
    return const_cast<DSAStackTy &>(*this).getStackElemAtLevel(Level);
  }

  DSAVarData getDSA(const_iterator &Iter, ValueDecl *D) const;

  /// Checks if the variable is a local for OpenMP region.
  bool isOpenMPLocal(VarDecl *D, const_iterator Iter) const;

  /// Vector of previously declared requires directives
  SmallVector<const OMPRequiresDecl *, 2> RequiresDecls;
  /// omp_allocator_handle_t type.
  QualType OMPAllocatorHandleT;
  /// omp_depend_t type.
  QualType OMPDependT;
  /// omp_event_handle_t type.
  QualType OMPEventHandleT;
  /// omp_alloctrait_t type.
  QualType OMPAlloctraitT;
  /// Expression for the predefined allocators.
  Expr *OMPPredefinedAllocators[OMPAllocateDeclAttr::OMPUserDefinedMemAlloc] = {
      nullptr};
  /// Vector of previously encountered target directives
  SmallVector<SourceLocation, 2> TargetLocations;
  SourceLocation AtomicLocation;
  /// Vector of declare variant construct traits.
  SmallVector<llvm::omp::TraitProperty, 8> ConstructTraits;

public:
  explicit DSAStackTy(Sema &S) : SemaRef(S) {}

  /// Sets omp_allocator_handle_t type.
  void setOMPAllocatorHandleT(QualType Ty) { OMPAllocatorHandleT = Ty; }
  /// Gets omp_allocator_handle_t type.
  QualType getOMPAllocatorHandleT() const { return OMPAllocatorHandleT; }
  /// Sets omp_alloctrait_t type.
  void setOMPAlloctraitT(QualType Ty) { OMPAlloctraitT = Ty; }
  /// Gets omp_alloctrait_t type.
  QualType getOMPAlloctraitT() const { return OMPAlloctraitT; }
  /// Sets the given default allocator.
  void setAllocator(OMPAllocateDeclAttr::AllocatorTypeTy AllocatorKind,
                    Expr *Allocator) {
    OMPPredefinedAllocators[AllocatorKind] = Allocator;
  }
  /// Returns the specified default allocator.
  Expr *getAllocator(OMPAllocateDeclAttr::AllocatorTypeTy AllocatorKind) const {
    return OMPPredefinedAllocators[AllocatorKind];
  }
  /// Sets omp_depend_t type.
  void setOMPDependT(QualType Ty) { OMPDependT = Ty; }
  /// Gets omp_depend_t type.
  QualType getOMPDependT() const { return OMPDependT; }

  /// Sets omp_event_handle_t type.
  void setOMPEventHandleT(QualType Ty) { OMPEventHandleT = Ty; }
  /// Gets omp_event_handle_t type.
  QualType getOMPEventHandleT() const { return OMPEventHandleT; }

  bool isClauseParsingMode() const { return ClauseKindMode != OMPC_unknown; }
  OpenMPClauseKind getClauseParsingMode() const {
    assert(isClauseParsingMode() && "Must be in clause parsing mode.");
    return ClauseKindMode;
  }
  void setClauseParsingMode(OpenMPClauseKind K) { ClauseKindMode = K; }

  bool isBodyComplete() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top && Top->BodyComplete;
  }
  void setBodyComplete() { getTopOfStack().BodyComplete = true; }

  bool isForceVarCapturing() const { return ForceCapturing; }
  void setForceVarCapturing(bool V) { ForceCapturing = V; }

  void setForceCaptureByReferenceInTargetExecutable(bool V) {
    ForceCaptureByReferenceInTargetExecutable = V;
  }
  bool isForceCaptureByReferenceInTargetExecutable() const {
    return ForceCaptureByReferenceInTargetExecutable;
  }

  void push(OpenMPDirectiveKind DKind, const DeclarationNameInfo &DirName,
            Scope *CurScope, SourceLocation Loc) {
    assert(!IgnoredStackElements &&
           "cannot change stack while ignoring elements");
    if (Stack.empty() ||
        Stack.back().second != CurrentNonCapturingFunctionScope)
      Stack.emplace_back(StackTy(), CurrentNonCapturingFunctionScope);
    Stack.back().first.emplace_back(DKind, DirName, CurScope, Loc);
    Stack.back().first.back().DefaultAttrLoc = Loc;
  }

  void pop() {
    assert(!IgnoredStackElements &&
           "cannot change stack while ignoring elements");
    assert(!Stack.back().first.empty() &&
           "Data-sharing attributes stack is empty!");
    Stack.back().first.pop_back();
  }

  /// RAII object to temporarily leave the scope of a directive when we want to
  /// logically operate in its parent.
  class ParentDirectiveScope {
    DSAStackTy &Self;
    bool Active;

  public:
    ParentDirectiveScope(DSAStackTy &Self, bool Activate)
        : Self(Self), Active(false) {
      if (Activate)
        enable();
    }
    ~ParentDirectiveScope() { disable(); }
    void disable() {
      if (Active) {
        --Self.IgnoredStackElements;
        Active = false;
      }
    }
    void enable() {
      if (!Active) {
        ++Self.IgnoredStackElements;
        Active = true;
      }
    }
  };

  /// Marks that we're started loop parsing.
  void loopInit() {
    assert(isOpenMPLoopDirective(getCurrentDirective()) &&
           "Expected loop-based directive.");
    getTopOfStack().LoopStart = true;
  }
  /// Start capturing of the variables in the loop context.
  void loopStart() {
    assert(isOpenMPLoopDirective(getCurrentDirective()) &&
           "Expected loop-based directive.");
    getTopOfStack().LoopStart = false;
  }
  /// true, if variables are captured, false otherwise.
  bool isLoopStarted() const {
    assert(isOpenMPLoopDirective(getCurrentDirective()) &&
           "Expected loop-based directive.");
    return !getTopOfStack().LoopStart;
  }
  /// Marks (or clears) declaration as possibly loop counter.
  void resetPossibleLoopCounter(const Decl *D = nullptr) {
    getTopOfStack().PossiblyLoopCounter = D ? D->getCanonicalDecl() : D;
  }
  /// Gets the possible loop counter decl.
  const Decl *getPossiblyLoopCounter() const {
    return getTopOfStack().PossiblyLoopCounter;
  }
  /// Start new OpenMP region stack in new non-capturing function.
  void pushFunction() {
    assert(!IgnoredStackElements &&
           "cannot change stack while ignoring elements");
    const FunctionScopeInfo *CurFnScope = SemaRef.getCurFunction();
    assert(!isa<CapturingScopeInfo>(CurFnScope));
    CurrentNonCapturingFunctionScope = CurFnScope;
  }
  /// Pop region stack for non-capturing function.
  void popFunction(const FunctionScopeInfo *OldFSI) {
    assert(!IgnoredStackElements &&
           "cannot change stack while ignoring elements");
    if (!Stack.empty() && Stack.back().second == OldFSI) {
      assert(Stack.back().first.empty());
      Stack.pop_back();
    }
    CurrentNonCapturingFunctionScope = nullptr;
    for (const FunctionScopeInfo *FSI : llvm::reverse(SemaRef.FunctionScopes)) {
      if (!isa<CapturingScopeInfo>(FSI)) {
        CurrentNonCapturingFunctionScope = FSI;
        break;
      }
    }
  }

  void addCriticalWithHint(const OMPCriticalDirective *D, llvm::APSInt Hint) {
    Criticals.try_emplace(D->getDirectiveName().getAsString(), D, Hint);
  }
  const std::pair<const OMPCriticalDirective *, llvm::APSInt>
  getCriticalWithHint(const DeclarationNameInfo &Name) const {
    auto I = Criticals.find(Name.getAsString());
    if (I != Criticals.end())
      return I->second;
    return std::make_pair(nullptr, llvm::APSInt());
  }
  /// If 'aligned' declaration for given variable \a D was not seen yet,
  /// add it and return NULL; otherwise return previous occurrence's expression
  /// for diagnostics.
  const Expr *addUniqueAligned(const ValueDecl *D, const Expr *NewDE);
  /// If 'nontemporal' declaration for given variable \a D was not seen yet,
  /// add it and return NULL; otherwise return previous occurrence's expression
  /// for diagnostics.
  const Expr *addUniqueNontemporal(const ValueDecl *D, const Expr *NewDE);

  /// Register specified variable as loop control variable.
  void addLoopControlVariable(const ValueDecl *D, VarDecl *Capture);
  /// Check if the specified variable is a loop control variable for
  /// current region.
  /// \return The index of the loop control variable in the list of associated
  /// for-loops (from outer to inner).
  const LCDeclInfo isLoopControlVariable(const ValueDecl *D) const;
  /// Check if the specified variable is a loop control variable for
  /// parent region.
  /// \return The index of the loop control variable in the list of associated
  /// for-loops (from outer to inner).
  const LCDeclInfo isParentLoopControlVariable(const ValueDecl *D) const;
  /// Check if the specified variable is a loop control variable for
  /// current region.
  /// \return The index of the loop control variable in the list of associated
  /// for-loops (from outer to inner).
  const LCDeclInfo isLoopControlVariable(const ValueDecl *D,
                                         unsigned Level) const;
  /// Get the loop control variable for the I-th loop (or nullptr) in
  /// parent directive.
  const ValueDecl *getParentLoopControlVariable(unsigned I) const;

  /// Marks the specified decl \p D as used in scan directive.
  void markDeclAsUsedInScanDirective(ValueDecl *D) {
    if (SharingMapTy *Stack = getSecondOnStackOrNull())
      Stack->UsedInScanDirective.insert(D);
  }

  /// Checks if the specified declaration was used in the inner scan directive.
  bool isUsedInScanDirective(ValueDecl *D) const {
    if (const SharingMapTy *Stack = getTopOfStackOrNull())
      return Stack->UsedInScanDirective.contains(D);
    return false;
  }

  /// Adds explicit data sharing attribute to the specified declaration.
  void addDSA(const ValueDecl *D, const Expr *E, OpenMPClauseKind A,
              DeclRefExpr *PrivateCopy = nullptr, unsigned Modifier = 0,
              bool AppliedToPointee = false);

  /// Adds additional information for the reduction items with the reduction id
  /// represented as an operator.
  void addTaskgroupReductionData(const ValueDecl *D, SourceRange SR,
                                 BinaryOperatorKind BOK);
  /// Adds additional information for the reduction items with the reduction id
  /// represented as reduction identifier.
  void addTaskgroupReductionData(const ValueDecl *D, SourceRange SR,
                                 const Expr *ReductionRef);
  /// Returns the location and reduction operation from the innermost parent
  /// region for the given \p D.
  const DSAVarData
  getTopMostTaskgroupReductionData(const ValueDecl *D, SourceRange &SR,
                                   BinaryOperatorKind &BOK,
                                   Expr *&TaskgroupDescriptor) const;
  /// Returns the location and reduction operation from the innermost parent
  /// region for the given \p D.
  const DSAVarData
  getTopMostTaskgroupReductionData(const ValueDecl *D, SourceRange &SR,
                                   const Expr *&ReductionRef,
                                   Expr *&TaskgroupDescriptor) const;
  /// Return reduction reference expression for the current taskgroup or
  /// parallel/worksharing directives with task reductions.
  Expr *getTaskgroupReductionRef() const {
    assert((getTopOfStack().Directive == OMPD_taskgroup ||
            ((isOpenMPParallelDirective(getTopOfStack().Directive) ||
              isOpenMPWorksharingDirective(getTopOfStack().Directive)) &&
             !isOpenMPSimdDirective(getTopOfStack().Directive))) &&
           "taskgroup reference expression requested for non taskgroup or "
           "parallel/worksharing directive.");
    return getTopOfStack().TaskgroupReductionRef;
  }
  /// Checks if the given \p VD declaration is actually a taskgroup reduction
  /// descriptor variable at the \p Level of OpenMP regions.
  bool isTaskgroupReductionRef(const ValueDecl *VD, unsigned Level) const {
    return getStackElemAtLevel(Level).TaskgroupReductionRef &&
           cast<DeclRefExpr>(getStackElemAtLevel(Level).TaskgroupReductionRef)
                   ->getDecl() == VD;
  }

  /// Returns data sharing attributes from top of the stack for the
  /// specified declaration.
  const DSAVarData getTopDSA(ValueDecl *D, bool FromParent);
  /// Returns data-sharing attributes for the specified declaration.
  const DSAVarData getImplicitDSA(ValueDecl *D, bool FromParent) const;
  /// Returns data-sharing attributes for the specified declaration.
  const DSAVarData getImplicitDSA(ValueDecl *D, unsigned Level) const;
  /// Checks if the specified variables has data-sharing attributes which
  /// match specified \a CPred predicate in any directive which matches \a DPred
  /// predicate.
  const DSAVarData
  hasDSA(ValueDecl *D,
         const llvm::function_ref<bool(OpenMPClauseKind, bool,
                                       DefaultDataSharingAttributes)>
             CPred,
         const llvm::function_ref<bool(OpenMPDirectiveKind)> DPred,
         bool FromParent) const;
  /// Checks if the specified variables has data-sharing attributes which
  /// match specified \a CPred predicate in any innermost directive which
  /// matches \a DPred predicate.
  const DSAVarData
  hasInnermostDSA(ValueDecl *D,
                  const llvm::function_ref<bool(OpenMPClauseKind, bool)> CPred,
                  const llvm::function_ref<bool(OpenMPDirectiveKind)> DPred,
                  bool FromParent) const;
  /// Checks if the specified variables has explicit data-sharing
  /// attributes which match specified \a CPred predicate at the specified
  /// OpenMP region.
  bool
  hasExplicitDSA(const ValueDecl *D,
                 const llvm::function_ref<bool(OpenMPClauseKind, bool)> CPred,
                 unsigned Level, bool NotLastprivate = false) const;

  /// Returns true if the directive at level \Level matches in the
  /// specified \a DPred predicate.
  bool hasExplicitDirective(
      const llvm::function_ref<bool(OpenMPDirectiveKind)> DPred,
      unsigned Level) const;

  /// Finds a directive which matches specified \a DPred predicate.
  bool hasDirective(
      const llvm::function_ref<bool(
          OpenMPDirectiveKind, const DeclarationNameInfo &, SourceLocation)>
          DPred,
      bool FromParent) const;

  /// Returns currently analyzed directive.
  OpenMPDirectiveKind getCurrentDirective() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->Directive : OMPD_unknown;
  }
  OpenMPDirectiveKind getMappedDirective() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->MappedDirective : OMPD_unknown;
  }
  void setCurrentDirective(OpenMPDirectiveKind NewDK) {
    SharingMapTy *Top = getTopOfStackOrNull();
    assert(Top &&
           "Before calling setCurrentDirective Top of Stack not to be NULL.");
    // Store the old into MappedDirective & assign argument NewDK to Directive.
    Top->Directive = NewDK;
  }
  void setMappedDirective(OpenMPDirectiveKind NewDK) {
    SharingMapTy *Top = getTopOfStackOrNull();
    assert(Top &&
           "Before calling setMappedDirective Top of Stack not to be NULL.");
    // Store the old into MappedDirective & assign argument NewDK to Directive.
    Top->MappedDirective = NewDK;
  }
  /// Returns directive kind at specified level.
  OpenMPDirectiveKind getDirective(unsigned Level) const {
    assert(!isStackEmpty() && "No directive at specified level.");
    return getStackElemAtLevel(Level).Directive;
  }
  /// Returns the capture region at the specified level.
  OpenMPDirectiveKind getCaptureRegion(unsigned Level,
                                       unsigned OpenMPCaptureLevel) const {
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, getDirective(Level));
    return CaptureRegions[OpenMPCaptureLevel];
  }
  /// Returns parent directive.
  OpenMPDirectiveKind getParentDirective() const {
    const SharingMapTy *Parent = getSecondOnStackOrNull();
    return Parent ? Parent->Directive : OMPD_unknown;
  }

  /// Add requires decl to internal vector
  void addRequiresDecl(OMPRequiresDecl *RD) { RequiresDecls.push_back(RD); }

  /// Checks if the defined 'requires' directive has specified type of clause.
  template <typename ClauseType> bool hasRequiresDeclWithClause() const {
    return llvm::any_of(RequiresDecls, [](const OMPRequiresDecl *D) {
      return llvm::any_of(D->clauselists(), [](const OMPClause *C) {
        return isa<ClauseType>(C);
      });
    });
  }

  /// Checks for a duplicate clause amongst previously declared requires
  /// directives
  bool hasDuplicateRequiresClause(ArrayRef<OMPClause *> ClauseList) const {
    bool IsDuplicate = false;
    for (OMPClause *CNew : ClauseList) {
      for (const OMPRequiresDecl *D : RequiresDecls) {
        for (const OMPClause *CPrev : D->clauselists()) {
          if (CNew->getClauseKind() == CPrev->getClauseKind()) {
            SemaRef.Diag(CNew->getBeginLoc(),
                         diag::err_omp_requires_clause_redeclaration)
                << getOpenMPClauseName(CNew->getClauseKind());
            SemaRef.Diag(CPrev->getBeginLoc(),
                         diag::note_omp_requires_previous_clause)
                << getOpenMPClauseName(CPrev->getClauseKind());
            IsDuplicate = true;
          }
        }
      }
    }
    return IsDuplicate;
  }

  /// Add location of previously encountered target to internal vector
  void addTargetDirLocation(SourceLocation LocStart) {
    TargetLocations.push_back(LocStart);
  }

  /// Add location for the first encountered atomic directive.
  void addAtomicDirectiveLoc(SourceLocation Loc) {
    if (AtomicLocation.isInvalid())
      AtomicLocation = Loc;
  }

  /// Returns the location of the first encountered atomic directive in the
  /// module.
  SourceLocation getAtomicDirectiveLoc() const { return AtomicLocation; }

  // Return previously encountered target region locations.
  ArrayRef<SourceLocation> getEncounteredTargetLocs() const {
    return TargetLocations;
  }

  /// Set default data sharing attribute to none.
  void setDefaultDSANone(SourceLocation Loc) {
    getTopOfStack().DefaultAttr = DSA_none;
    getTopOfStack().DefaultAttrLoc = Loc;
  }
  /// Set default data sharing attribute to shared.
  void setDefaultDSAShared(SourceLocation Loc) {
    getTopOfStack().DefaultAttr = DSA_shared;
    getTopOfStack().DefaultAttrLoc = Loc;
  }
  /// Set default data sharing attribute to private.
  void setDefaultDSAPrivate(SourceLocation Loc) {
    getTopOfStack().DefaultAttr = DSA_private;
    getTopOfStack().DefaultAttrLoc = Loc;
  }
  /// Set default data sharing attribute to firstprivate.
  void setDefaultDSAFirstPrivate(SourceLocation Loc) {
    getTopOfStack().DefaultAttr = DSA_firstprivate;
    getTopOfStack().DefaultAttrLoc = Loc;
  }
  /// Set default data mapping attribute to Modifier:Kind
  void setDefaultDMAAttr(OpenMPDefaultmapClauseModifier M,
                         OpenMPDefaultmapClauseKind Kind, SourceLocation Loc) {
    DefaultmapInfo &DMI = getTopOfStack().DefaultmapMap[Kind];
    DMI.ImplicitBehavior = M;
    DMI.SLoc = Loc;
  }
  /// Check whether the implicit-behavior has been set in defaultmap
  bool checkDefaultmapCategory(OpenMPDefaultmapClauseKind VariableCategory) {
    if (VariableCategory == OMPC_DEFAULTMAP_unknown)
      return getTopOfStack()
                     .DefaultmapMap[OMPC_DEFAULTMAP_aggregate]
                     .ImplicitBehavior != OMPC_DEFAULTMAP_MODIFIER_unknown ||
             getTopOfStack()
                     .DefaultmapMap[OMPC_DEFAULTMAP_scalar]
                     .ImplicitBehavior != OMPC_DEFAULTMAP_MODIFIER_unknown ||
             getTopOfStack()
                     .DefaultmapMap[OMPC_DEFAULTMAP_pointer]
                     .ImplicitBehavior != OMPC_DEFAULTMAP_MODIFIER_unknown;
    return getTopOfStack().DefaultmapMap[VariableCategory].ImplicitBehavior !=
           OMPC_DEFAULTMAP_MODIFIER_unknown;
  }

  ArrayRef<llvm::omp::TraitProperty> getConstructTraits() {
    return ConstructTraits;
  }
  void handleConstructTrait(ArrayRef<llvm::omp::TraitProperty> Traits,
                            bool ScopeEntry) {
    if (ScopeEntry)
      ConstructTraits.append(Traits.begin(), Traits.end());
    else
      for (llvm::omp::TraitProperty Trait : llvm::reverse(Traits)) {
        llvm::omp::TraitProperty Top = ConstructTraits.pop_back_val();
        assert(Top == Trait && "Something left a trait on the stack!");
        (void)Trait;
        (void)Top;
      }
  }

  DefaultDataSharingAttributes getDefaultDSA(unsigned Level) const {
    return getStackSize() <= Level ? DSA_unspecified
                                   : getStackElemAtLevel(Level).DefaultAttr;
  }
  DefaultDataSharingAttributes getDefaultDSA() const {
    return isStackEmpty() ? DSA_unspecified : getTopOfStack().DefaultAttr;
  }
  SourceLocation getDefaultDSALocation() const {
    return isStackEmpty() ? SourceLocation() : getTopOfStack().DefaultAttrLoc;
  }
  OpenMPDefaultmapClauseModifier
  getDefaultmapModifier(OpenMPDefaultmapClauseKind Kind) const {
    return isStackEmpty()
               ? OMPC_DEFAULTMAP_MODIFIER_unknown
               : getTopOfStack().DefaultmapMap[Kind].ImplicitBehavior;
  }
  OpenMPDefaultmapClauseModifier
  getDefaultmapModifierAtLevel(unsigned Level,
                               OpenMPDefaultmapClauseKind Kind) const {
    return getStackElemAtLevel(Level).DefaultmapMap[Kind].ImplicitBehavior;
  }
  bool isDefaultmapCapturedByRef(unsigned Level,
                                 OpenMPDefaultmapClauseKind Kind) const {
    OpenMPDefaultmapClauseModifier M =
        getDefaultmapModifierAtLevel(Level, Kind);
    if (Kind == OMPC_DEFAULTMAP_scalar || Kind == OMPC_DEFAULTMAP_pointer) {
      return (M == OMPC_DEFAULTMAP_MODIFIER_alloc) ||
             (M == OMPC_DEFAULTMAP_MODIFIER_to) ||
             (M == OMPC_DEFAULTMAP_MODIFIER_from) ||
             (M == OMPC_DEFAULTMAP_MODIFIER_tofrom);
    }
    return true;
  }
  static bool mustBeFirstprivateBase(OpenMPDefaultmapClauseModifier M,
                                     OpenMPDefaultmapClauseKind Kind) {
    switch (Kind) {
    case OMPC_DEFAULTMAP_scalar:
    case OMPC_DEFAULTMAP_pointer:
      return (M == OMPC_DEFAULTMAP_MODIFIER_unknown) ||
             (M == OMPC_DEFAULTMAP_MODIFIER_firstprivate) ||
             (M == OMPC_DEFAULTMAP_MODIFIER_default);
    case OMPC_DEFAULTMAP_aggregate:
      return M == OMPC_DEFAULTMAP_MODIFIER_firstprivate;
    default:
      break;
    }
    llvm_unreachable("Unexpected OpenMPDefaultmapClauseKind enum");
  }
  bool mustBeFirstprivateAtLevel(unsigned Level,
                                 OpenMPDefaultmapClauseKind Kind) const {
    OpenMPDefaultmapClauseModifier M =
        getDefaultmapModifierAtLevel(Level, Kind);
    return mustBeFirstprivateBase(M, Kind);
  }
  bool mustBeFirstprivate(OpenMPDefaultmapClauseKind Kind) const {
    OpenMPDefaultmapClauseModifier M = getDefaultmapModifier(Kind);
    return mustBeFirstprivateBase(M, Kind);
  }

  /// Checks if the specified variable is a threadprivate.
  bool isThreadPrivate(VarDecl *D) {
    const DSAVarData DVar = getTopDSA(D, false);
    return isOpenMPThreadPrivate(DVar.CKind);
  }

  /// Marks current region as ordered (it has an 'ordered' clause).
  void setOrderedRegion(bool IsOrdered, const Expr *Param,
                        OMPOrderedClause *Clause) {
    if (IsOrdered)
      getTopOfStack().OrderedRegion.emplace(Param, Clause);
    else
      getTopOfStack().OrderedRegion.reset();
  }
  /// Returns true, if region is ordered (has associated 'ordered' clause),
  /// false - otherwise.
  bool isOrderedRegion() const {
    if (const SharingMapTy *Top = getTopOfStackOrNull())
      return Top->OrderedRegion.has_value();
    return false;
  }
  /// Returns optional parameter for the ordered region.
  std::pair<const Expr *, OMPOrderedClause *> getOrderedRegionParam() const {
    if (const SharingMapTy *Top = getTopOfStackOrNull())
      if (Top->OrderedRegion)
        return *Top->OrderedRegion;
    return std::make_pair(nullptr, nullptr);
  }
  /// Returns true, if parent region is ordered (has associated
  /// 'ordered' clause), false - otherwise.
  bool isParentOrderedRegion() const {
    if (const SharingMapTy *Parent = getSecondOnStackOrNull())
      return Parent->OrderedRegion.has_value();
    return false;
  }
  /// Returns optional parameter for the ordered region.
  std::pair<const Expr *, OMPOrderedClause *>
  getParentOrderedRegionParam() const {
    if (const SharingMapTy *Parent = getSecondOnStackOrNull())
      if (Parent->OrderedRegion)
        return *Parent->OrderedRegion;
    return std::make_pair(nullptr, nullptr);
  }
  /// Marks current region as having an 'order' clause.
  void setRegionHasOrderConcurrent(bool HasOrderConcurrent) {
    getTopOfStack().RegionHasOrderConcurrent = HasOrderConcurrent;
  }
  /// Returns true, if parent region is order (has associated
  /// 'order' clause), false - otherwise.
  bool isParentOrderConcurrent() const {
    if (const SharingMapTy *Parent = getSecondOnStackOrNull())
      return Parent->RegionHasOrderConcurrent;
    return false;
  }
  /// Marks current region as nowait (it has a 'nowait' clause).
  void setNowaitRegion(bool IsNowait = true) {
    getTopOfStack().NowaitRegion = IsNowait;
  }
  /// Returns true, if parent region is nowait (has associated
  /// 'nowait' clause), false - otherwise.
  bool isParentNowaitRegion() const {
    if (const SharingMapTy *Parent = getSecondOnStackOrNull())
      return Parent->NowaitRegion;
    return false;
  }
  /// Marks current region as untied (it has a 'untied' clause).
  void setUntiedRegion(bool IsUntied = true) {
    getTopOfStack().UntiedRegion = IsUntied;
  }
  /// Return true if current region is untied.
  bool isUntiedRegion() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->UntiedRegion : false;
  }
  /// Marks parent region as cancel region.
  void setParentCancelRegion(bool Cancel = true) {
    if (SharingMapTy *Parent = getSecondOnStackOrNull())
      Parent->CancelRegion |= Cancel;
  }
  /// Return true if current region has inner cancel construct.
  bool isCancelRegion() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->CancelRegion : false;
  }

  /// Mark that parent region already has scan directive.
  void setParentHasScanDirective(SourceLocation Loc) {
    if (SharingMapTy *Parent = getSecondOnStackOrNull())
      Parent->PrevScanLocation = Loc;
  }
  /// Return true if current region has inner cancel construct.
  bool doesParentHasScanDirective() const {
    const SharingMapTy *Top = getSecondOnStackOrNull();
    return Top ? Top->PrevScanLocation.isValid() : false;
  }
  /// Return true if current region has inner cancel construct.
  SourceLocation getParentScanDirectiveLoc() const {
    const SharingMapTy *Top = getSecondOnStackOrNull();
    return Top ? Top->PrevScanLocation : SourceLocation();
  }
  /// Mark that parent region already has ordered directive.
  void setParentHasOrderedDirective(SourceLocation Loc) {
    if (SharingMapTy *Parent = getSecondOnStackOrNull())
      Parent->PrevOrderedLocation = Loc;
  }
  /// Return true if current region has inner ordered construct.
  bool doesParentHasOrderedDirective() const {
    const SharingMapTy *Top = getSecondOnStackOrNull();
    return Top ? Top->PrevOrderedLocation.isValid() : false;
  }
  /// Returns the location of the previously specified ordered directive.
  SourceLocation getParentOrderedDirectiveLoc() const {
    const SharingMapTy *Top = getSecondOnStackOrNull();
    return Top ? Top->PrevOrderedLocation : SourceLocation();
  }

  /// Set collapse value for the region.
  void setAssociatedLoops(unsigned Val) {
    getTopOfStack().AssociatedLoops = Val;
    if (Val > 1)
      getTopOfStack().HasMutipleLoops = true;
  }
  /// Return collapse value for region.
  unsigned getAssociatedLoops() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->AssociatedLoops : 0;
  }
  /// Returns true if the construct is associated with multiple loops.
  bool hasMutipleLoops() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->HasMutipleLoops : false;
  }

  /// Marks current target region as one with closely nested teams
  /// region.
  void setParentTeamsRegionLoc(SourceLocation TeamsRegionLoc) {
    if (SharingMapTy *Parent = getSecondOnStackOrNull())
      Parent->InnerTeamsRegionLoc = TeamsRegionLoc;
  }
  /// Returns true, if current region has closely nested teams region.
  bool hasInnerTeamsRegion() const {
    return getInnerTeamsRegionLoc().isValid();
  }
  /// Returns location of the nested teams region (if any).
  SourceLocation getInnerTeamsRegionLoc() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->InnerTeamsRegionLoc : SourceLocation();
  }

  Scope *getCurScope() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->CurScope : nullptr;
  }
  void setContext(DeclContext *DC) { getTopOfStack().Context = DC; }
  SourceLocation getConstructLoc() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->ConstructLoc : SourceLocation();
  }

  /// Do the check specified in \a Check to all component lists and return true
  /// if any issue is found.
  bool checkMappableExprComponentListsForDecl(
      const ValueDecl *VD, bool CurrentRegionOnly,
      const llvm::function_ref<
          bool(OMPClauseMappableExprCommon::MappableExprComponentListRef,
               OpenMPClauseKind)>
          Check) const {
    if (isStackEmpty())
      return false;
    auto SI = begin();
    auto SE = end();

    if (SI == SE)
      return false;

    if (CurrentRegionOnly)
      SE = std::next(SI);
    else
      std::advance(SI, 1);

    for (; SI != SE; ++SI) {
      auto MI = SI->MappedExprComponents.find(VD);
      if (MI != SI->MappedExprComponents.end())
        for (OMPClauseMappableExprCommon::MappableExprComponentListRef L :
             MI->second.Components)
          if (Check(L, MI->second.Kind))
            return true;
    }
    return false;
  }

  /// Do the check specified in \a Check to all component lists at a given level
  /// and return true if any issue is found.
  bool checkMappableExprComponentListsForDeclAtLevel(
      const ValueDecl *VD, unsigned Level,
      const llvm::function_ref<
          bool(OMPClauseMappableExprCommon::MappableExprComponentListRef,
               OpenMPClauseKind)>
          Check) const {
    if (getStackSize() <= Level)
      return false;

    const SharingMapTy &StackElem = getStackElemAtLevel(Level);
    auto MI = StackElem.MappedExprComponents.find(VD);
    if (MI != StackElem.MappedExprComponents.end())
      for (OMPClauseMappableExprCommon::MappableExprComponentListRef L :
           MI->second.Components)
        if (Check(L, MI->second.Kind))
          return true;
    return false;
  }

  /// Create a new mappable expression component list associated with a given
  /// declaration and initialize it with the provided list of components.
  void addMappableExpressionComponents(
      const ValueDecl *VD,
      OMPClauseMappableExprCommon::MappableExprComponentListRef Components,
      OpenMPClauseKind WhereFoundClauseKind) {
    MappedExprComponentTy &MEC = getTopOfStack().MappedExprComponents[VD];
    // Create new entry and append the new components there.
    MEC.Components.resize(MEC.Components.size() + 1);
    MEC.Components.back().append(Components.begin(), Components.end());
    MEC.Kind = WhereFoundClauseKind;
  }

  unsigned getNestingLevel() const {
    assert(!isStackEmpty());
    return getStackSize() - 1;
  }
  void addDoacrossDependClause(OMPClause *C, const OperatorOffsetTy &OpsOffs) {
    SharingMapTy *Parent = getSecondOnStackOrNull();
    assert(Parent && isOpenMPWorksharingDirective(Parent->Directive));
    Parent->DoacrossDepends.try_emplace(C, OpsOffs);
  }
  llvm::iterator_range<DoacrossClauseMapTy::const_iterator>
  getDoacrossDependClauses() const {
    const SharingMapTy &StackElem = getTopOfStack();
    if (isOpenMPWorksharingDirective(StackElem.Directive)) {
      const DoacrossClauseMapTy &Ref = StackElem.DoacrossDepends;
      return llvm::make_range(Ref.begin(), Ref.end());
    }
    return llvm::make_range(StackElem.DoacrossDepends.end(),
                            StackElem.DoacrossDepends.end());
  }

  // Store types of classes which have been explicitly mapped
  void addMappedClassesQualTypes(QualType QT) {
    SharingMapTy &StackElem = getTopOfStack();
    StackElem.MappedClassesQualTypes.insert(QT);
  }

  // Return set of mapped classes types
  bool isClassPreviouslyMapped(QualType QT) const {
    const SharingMapTy &StackElem = getTopOfStack();
    return StackElem.MappedClassesQualTypes.contains(QT);
  }

  /// Adds global declare target to the parent target region.
  void addToParentTargetRegionLinkGlobals(DeclRefExpr *E) {
    assert(*OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(
               E->getDecl()) == OMPDeclareTargetDeclAttr::MT_Link &&
           "Expected declare target link global.");
    for (auto &Elem : *this) {
      if (isOpenMPTargetExecutionDirective(Elem.Directive)) {
        Elem.DeclareTargetLinkVarDecls.push_back(E);
        return;
      }
    }
  }

  /// Returns the list of globals with declare target link if current directive
  /// is target.
  ArrayRef<DeclRefExpr *> getLinkGlobals() const {
    assert(isOpenMPTargetExecutionDirective(getCurrentDirective()) &&
           "Expected target executable directive.");
    return getTopOfStack().DeclareTargetLinkVarDecls;
  }

  /// Adds list of allocators expressions.
  void addInnerAllocatorExpr(Expr *E) {
    getTopOfStack().InnerUsedAllocators.push_back(E);
  }
  /// Return list of used allocators.
  ArrayRef<Expr *> getInnerAllocators() const {
    return getTopOfStack().InnerUsedAllocators;
  }
  /// Marks the declaration as implicitly firstprivate nin the task-based
  /// regions.
  void addImplicitTaskFirstprivate(unsigned Level, Decl *D) {
    getStackElemAtLevel(Level).ImplicitTaskFirstprivates.insert(D);
  }
  /// Checks if the decl is implicitly firstprivate in the task-based region.
  bool isImplicitTaskFirstprivate(Decl *D) const {
    return getTopOfStack().ImplicitTaskFirstprivates.contains(D);
  }

  /// Marks decl as used in uses_allocators clause as the allocator.
  void addUsesAllocatorsDecl(const Decl *D, UsesAllocatorsDeclKind Kind) {
    getTopOfStack().UsesAllocatorsDecls.try_emplace(D, Kind);
  }
  /// Checks if specified decl is used in uses allocator clause as the
  /// allocator.
  std::optional<UsesAllocatorsDeclKind>
  isUsesAllocatorsDecl(unsigned Level, const Decl *D) const {
    const SharingMapTy &StackElem = getTopOfStack();
    auto I = StackElem.UsesAllocatorsDecls.find(D);
    if (I == StackElem.UsesAllocatorsDecls.end())
      return std::nullopt;
    return I->getSecond();
  }
  std::optional<UsesAllocatorsDeclKind>
  isUsesAllocatorsDecl(const Decl *D) const {
    const SharingMapTy &StackElem = getTopOfStack();
    auto I = StackElem.UsesAllocatorsDecls.find(D);
    if (I == StackElem.UsesAllocatorsDecls.end())
      return std::nullopt;
    return I->getSecond();
  }

  void addDeclareMapperVarRef(Expr *Ref) {
    SharingMapTy &StackElem = getTopOfStack();
    StackElem.DeclareMapperVar = Ref;
  }
  const Expr *getDeclareMapperVarRef() const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    return Top ? Top->DeclareMapperVar : nullptr;
  }

  /// Add a new iterator variable.
  void addIteratorVarDecl(VarDecl *VD) {
    SharingMapTy &StackElem = getTopOfStack();
    StackElem.IteratorVarDecls.push_back(VD->getCanonicalDecl());
  }
  /// Check if variable declaration is an iterator VarDecl.
  bool isIteratorVarDecl(const VarDecl *VD) const {
    const SharingMapTy *Top = getTopOfStackOrNull();
    if (!Top)
      return false;

    return llvm::is_contained(Top->IteratorVarDecls, VD->getCanonicalDecl());
  }
  /// get captured field from ImplicitDefaultFirstprivateFDs
  VarDecl *getImplicitFDCapExprDecl(const FieldDecl *FD) const {
    const_iterator I = begin();
    const_iterator EndI = end();
    size_t StackLevel = getStackSize();
    for (; I != EndI; ++I) {
      if (I->DefaultAttr == DSA_firstprivate || I->DefaultAttr == DSA_private)
        break;
      StackLevel--;
    }
    assert((StackLevel > 0 && I != EndI) || (StackLevel == 0 && I == EndI));
    if (I == EndI)
      return nullptr;
    for (const auto &IFD : I->ImplicitDefaultFirstprivateFDs)
      if (IFD.FD == FD && IFD.StackLevel == StackLevel)
        return IFD.VD;
    return nullptr;
  }
  /// Check if capture decl is field captured in ImplicitDefaultFirstprivateFDs
  bool isImplicitDefaultFirstprivateFD(VarDecl *VD) const {
    const_iterator I = begin();
    const_iterator EndI = end();
    for (; I != EndI; ++I)
      if (I->DefaultAttr == DSA_firstprivate || I->DefaultAttr == DSA_private)
        break;
    if (I == EndI)
      return false;
    for (const auto &IFD : I->ImplicitDefaultFirstprivateFDs)
      if (IFD.VD == VD)
        return true;
    return false;
  }
  /// Store capture FD info in ImplicitDefaultFirstprivateFDs
  void addImplicitDefaultFirstprivateFD(const FieldDecl *FD, VarDecl *VD) {
    iterator I = begin();
    const_iterator EndI = end();
    size_t StackLevel = getStackSize();
    for (; I != EndI; ++I) {
      if (I->DefaultAttr == DSA_private || I->DefaultAttr == DSA_firstprivate) {
        I->ImplicitDefaultFirstprivateFDs.emplace_back(FD, StackLevel, VD);
        break;
      }
      StackLevel--;
    }
    assert((StackLevel > 0 && I != EndI) || (StackLevel == 0 && I == EndI));
  }
};

bool isImplicitTaskingRegion(OpenMPDirectiveKind DKind) {
  return isOpenMPParallelDirective(DKind) || isOpenMPTeamsDirective(DKind);
}

bool isImplicitOrExplicitTaskingRegion(OpenMPDirectiveKind DKind) {
  return isImplicitTaskingRegion(DKind) || isOpenMPTaskingDirective(DKind) ||
         DKind == OMPD_unknown;
}

} // namespace

static const Expr *getExprAsWritten(const Expr *E) {
  if (const auto *FE = dyn_cast<FullExpr>(E))
    E = FE->getSubExpr();

  if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E))
    E = MTE->getSubExpr();

  while (const auto *Binder = dyn_cast<CXXBindTemporaryExpr>(E))
    E = Binder->getSubExpr();

  if (const auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    E = ICE->getSubExprAsWritten();
  return E->IgnoreParens();
}

static Expr *getExprAsWritten(Expr *E) {
  return const_cast<Expr *>(getExprAsWritten(const_cast<const Expr *>(E)));
}

static const ValueDecl *getCanonicalDecl(const ValueDecl *D) {
  if (const auto *CED = dyn_cast<OMPCapturedExprDecl>(D))
    if (const auto *ME = dyn_cast<MemberExpr>(getExprAsWritten(CED->getInit())))
      D = ME->getMemberDecl();
  const auto *VD = dyn_cast<VarDecl>(D);
  const auto *FD = dyn_cast<FieldDecl>(D);
  if (VD != nullptr) {
    VD = VD->getCanonicalDecl();
    D = VD;
  } else {
    assert(FD);
    FD = FD->getCanonicalDecl();
    D = FD;
  }
  return D;
}

static ValueDecl *getCanonicalDecl(ValueDecl *D) {
  return const_cast<ValueDecl *>(
      getCanonicalDecl(const_cast<const ValueDecl *>(D)));
}

DSAStackTy::DSAVarData DSAStackTy::getDSA(const_iterator &Iter,
                                          ValueDecl *D) const {
  D = getCanonicalDecl(D);
  auto *VD = dyn_cast<VarDecl>(D);
  const auto *FD = dyn_cast<FieldDecl>(D);
  DSAVarData DVar;
  if (Iter == end()) {
    // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a region but not in construct]
    //  File-scope or namespace-scope variables referenced in called routines
    //  in the region are shared unless they appear in a threadprivate
    //  directive.
    if (VD && !VD->isFunctionOrMethodVarDecl() && !isa<ParmVarDecl>(VD))
      DVar.CKind = OMPC_shared;

    // OpenMP [2.9.1.2, Data-sharing Attribute Rules for Variables Referenced
    // in a region but not in construct]
    //  Variables with static storage duration that are declared in called
    //  routines in the region are shared.
    if (VD && VD->hasGlobalStorage())
      DVar.CKind = OMPC_shared;

    // Non-static data members are shared by default.
    if (FD)
      DVar.CKind = OMPC_shared;

    return DVar;
  }

  // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
  // in a Construct, C/C++, predetermined, p.1]
  // Variables with automatic storage duration that are declared in a scope
  // inside the construct are private.
  if (VD && isOpenMPLocal(VD, Iter) && VD->isLocalVarDecl() &&
      (VD->getStorageClass() == SC_Auto || VD->getStorageClass() == SC_None)) {
    DVar.CKind = OMPC_private;
    return DVar;
  }

  DVar.DKind = Iter->Directive;
  // Explicitly specified attributes and local variables with predetermined
  // attributes.
  if (Iter->SharingMap.count(D)) {
    const DSAInfo &Data = Iter->SharingMap.lookup(D);
    DVar.RefExpr = Data.RefExpr.getPointer();
    DVar.PrivateCopy = Data.PrivateCopy;
    DVar.CKind = Data.Attributes;
    DVar.ImplicitDSALoc = Iter->DefaultAttrLoc;
    DVar.Modifier = Data.Modifier;
    DVar.AppliedToPointee = Data.AppliedToPointee;
    return DVar;
  }

  // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
  // in a Construct, C/C++, implicitly determined, p.1]
  //  In a parallel or task construct, the data-sharing attributes of these
  //  variables are determined by the default clause, if present.
  switch (Iter->DefaultAttr) {
  case DSA_shared:
    DVar.CKind = OMPC_shared;
    DVar.ImplicitDSALoc = Iter->DefaultAttrLoc;
    return DVar;
  case DSA_none:
    return DVar;
  case DSA_firstprivate:
    if (VD && VD->getStorageDuration() == SD_Static &&
        VD->getDeclContext()->isFileContext()) {
      DVar.CKind = OMPC_unknown;
    } else {
      DVar.CKind = OMPC_firstprivate;
    }
    DVar.ImplicitDSALoc = Iter->DefaultAttrLoc;
    return DVar;
  case DSA_private:
    // each variable with static storage duration that is declared
    // in a namespace or global scope and referenced in the construct,
    // and that does not have a predetermined data-sharing attribute
    if (VD && VD->getStorageDuration() == SD_Static &&
        VD->getDeclContext()->isFileContext()) {
      DVar.CKind = OMPC_unknown;
    } else {
      DVar.CKind = OMPC_private;
    }
    DVar.ImplicitDSALoc = Iter->DefaultAttrLoc;
    return DVar;
  case DSA_unspecified:
    // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a Construct, implicitly determined, p.2]
    //  In a parallel construct, if no default clause is present, these
    //  variables are shared.
    DVar.ImplicitDSALoc = Iter->DefaultAttrLoc;
    if ((isOpenMPParallelDirective(DVar.DKind) &&
         !isOpenMPTaskLoopDirective(DVar.DKind)) ||
        isOpenMPTeamsDirective(DVar.DKind)) {
      DVar.CKind = OMPC_shared;
      return DVar;
    }

    // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a Construct, implicitly determined, p.4]
    //  In a task construct, if no default clause is present, a variable that in
    //  the enclosing context is determined to be shared by all implicit tasks
    //  bound to the current team is shared.
    if (isOpenMPTaskingDirective(DVar.DKind)) {
      DSAVarData DVarTemp;
      const_iterator I = Iter, E = end();
      do {
        ++I;
        // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables
        // Referenced in a Construct, implicitly determined, p.6]
        //  In a task construct, if no default clause is present, a variable
        //  whose data-sharing attribute is not determined by the rules above is
        //  firstprivate.
        DVarTemp = getDSA(I, D);
        if (DVarTemp.CKind != OMPC_shared) {
          DVar.RefExpr = nullptr;
          DVar.CKind = OMPC_firstprivate;
          return DVar;
        }
      } while (I != E && !isImplicitTaskingRegion(I->Directive));
      DVar.CKind =
          (DVarTemp.CKind == OMPC_unknown) ? OMPC_firstprivate : OMPC_shared;
      return DVar;
    }
  }
  // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
  // in a Construct, implicitly determined, p.3]
  //  For constructs other than task, if no default clause is present, these
  //  variables inherit their data-sharing attributes from the enclosing
  //  context.
  return getDSA(++Iter, D);
}

const Expr *DSAStackTy::addUniqueAligned(const ValueDecl *D,
                                         const Expr *NewDE) {
  assert(!isStackEmpty() && "Data sharing attributes stack is empty");
  D = getCanonicalDecl(D);
  SharingMapTy &StackElem = getTopOfStack();
  auto It = StackElem.AlignedMap.find(D);
  if (It == StackElem.AlignedMap.end()) {
    assert(NewDE && "Unexpected nullptr expr to be added into aligned map");
    StackElem.AlignedMap[D] = NewDE;
    return nullptr;
  }
  assert(It->second && "Unexpected nullptr expr in the aligned map");
  return It->second;
}

const Expr *DSAStackTy::addUniqueNontemporal(const ValueDecl *D,
                                             const Expr *NewDE) {
  assert(!isStackEmpty() && "Data sharing attributes stack is empty");
  D = getCanonicalDecl(D);
  SharingMapTy &StackElem = getTopOfStack();
  auto It = StackElem.NontemporalMap.find(D);
  if (It == StackElem.NontemporalMap.end()) {
    assert(NewDE && "Unexpected nullptr expr to be added into aligned map");
    StackElem.NontemporalMap[D] = NewDE;
    return nullptr;
  }
  assert(It->second && "Unexpected nullptr expr in the aligned map");
  return It->second;
}

void DSAStackTy::addLoopControlVariable(const ValueDecl *D, VarDecl *Capture) {
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty");
  D = getCanonicalDecl(D);
  SharingMapTy &StackElem = getTopOfStack();
  StackElem.LCVMap.try_emplace(
      D, LCDeclInfo(StackElem.LCVMap.size() + 1, Capture));
}

const DSAStackTy::LCDeclInfo
DSAStackTy::isLoopControlVariable(const ValueDecl *D) const {
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty");
  D = getCanonicalDecl(D);
  const SharingMapTy &StackElem = getTopOfStack();
  auto It = StackElem.LCVMap.find(D);
  if (It != StackElem.LCVMap.end())
    return It->second;
  return {0, nullptr};
}

const DSAStackTy::LCDeclInfo
DSAStackTy::isLoopControlVariable(const ValueDecl *D, unsigned Level) const {
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty");
  D = getCanonicalDecl(D);
  for (unsigned I = Level + 1; I > 0; --I) {
    const SharingMapTy &StackElem = getStackElemAtLevel(I - 1);
    auto It = StackElem.LCVMap.find(D);
    if (It != StackElem.LCVMap.end())
      return It->second;
  }
  return {0, nullptr};
}

const DSAStackTy::LCDeclInfo
DSAStackTy::isParentLoopControlVariable(const ValueDecl *D) const {
  const SharingMapTy *Parent = getSecondOnStackOrNull();
  assert(Parent && "Data-sharing attributes stack is empty");
  D = getCanonicalDecl(D);
  auto It = Parent->LCVMap.find(D);
  if (It != Parent->LCVMap.end())
    return It->second;
  return {0, nullptr};
}

const ValueDecl *DSAStackTy::getParentLoopControlVariable(unsigned I) const {
  const SharingMapTy *Parent = getSecondOnStackOrNull();
  assert(Parent && "Data-sharing attributes stack is empty");
  if (Parent->LCVMap.size() < I)
    return nullptr;
  for (const auto &Pair : Parent->LCVMap)
    if (Pair.second.first == I)
      return Pair.first;
  return nullptr;
}

void DSAStackTy::addDSA(const ValueDecl *D, const Expr *E, OpenMPClauseKind A,
                        DeclRefExpr *PrivateCopy, unsigned Modifier,
                        bool AppliedToPointee) {
  D = getCanonicalDecl(D);
  if (A == OMPC_threadprivate) {
    DSAInfo &Data = Threadprivates[D];
    Data.Attributes = A;
    Data.RefExpr.setPointer(E);
    Data.PrivateCopy = nullptr;
    Data.Modifier = Modifier;
  } else {
    DSAInfo &Data = getTopOfStack().SharingMap[D];
    assert(Data.Attributes == OMPC_unknown || (A == Data.Attributes) ||
           (A == OMPC_firstprivate && Data.Attributes == OMPC_lastprivate) ||
           (A == OMPC_lastprivate && Data.Attributes == OMPC_firstprivate) ||
           (isLoopControlVariable(D).first && A == OMPC_private));
    Data.Modifier = Modifier;
    if (A == OMPC_lastprivate && Data.Attributes == OMPC_firstprivate) {
      Data.RefExpr.setInt(/*IntVal=*/true);
      return;
    }
    const bool IsLastprivate =
        A == OMPC_lastprivate || Data.Attributes == OMPC_lastprivate;
    Data.Attributes = A;
    Data.RefExpr.setPointerAndInt(E, IsLastprivate);
    Data.PrivateCopy = PrivateCopy;
    Data.AppliedToPointee = AppliedToPointee;
    if (PrivateCopy) {
      DSAInfo &Data = getTopOfStack().SharingMap[PrivateCopy->getDecl()];
      Data.Modifier = Modifier;
      Data.Attributes = A;
      Data.RefExpr.setPointerAndInt(PrivateCopy, IsLastprivate);
      Data.PrivateCopy = nullptr;
      Data.AppliedToPointee = AppliedToPointee;
    }
  }
}

/// Build a variable declaration for OpenMP loop iteration variable.
static VarDecl *buildVarDecl(Sema &SemaRef, SourceLocation Loc, QualType Type,
                             StringRef Name, const AttrVec *Attrs = nullptr,
                             DeclRefExpr *OrigRef = nullptr) {
  DeclContext *DC = SemaRef.CurContext;
  IdentifierInfo *II = &SemaRef.PP.getIdentifierTable().get(Name);
  TypeSourceInfo *TInfo = SemaRef.Context.getTrivialTypeSourceInfo(Type, Loc);
  auto *Decl =
      VarDecl::Create(SemaRef.Context, DC, Loc, Loc, II, Type, TInfo, SC_None);
  if (Attrs) {
    for (specific_attr_iterator<AlignedAttr> I(Attrs->begin()), E(Attrs->end());
         I != E; ++I)
      Decl->addAttr(*I);
  }
  Decl->setImplicit();
  if (OrigRef) {
    Decl->addAttr(
        OMPReferencedVarAttr::CreateImplicit(SemaRef.Context, OrigRef));
  }
  return Decl;
}

static DeclRefExpr *buildDeclRefExpr(Sema &S, VarDecl *D, QualType Ty,
                                     SourceLocation Loc,
                                     bool RefersToCapture = false) {
  D->setReferenced();
  D->markUsed(S.Context);
  return DeclRefExpr::Create(S.getASTContext(), NestedNameSpecifierLoc(),
                             SourceLocation(), D, RefersToCapture, Loc, Ty,
                             VK_LValue);
}

void DSAStackTy::addTaskgroupReductionData(const ValueDecl *D, SourceRange SR,
                                           BinaryOperatorKind BOK) {
  D = getCanonicalDecl(D);
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty");
  assert(
      getTopOfStack().SharingMap[D].Attributes == OMPC_reduction &&
      "Additional reduction info may be specified only for reduction items.");
  ReductionData &ReductionData = getTopOfStack().ReductionMap[D];
  assert(ReductionData.ReductionRange.isInvalid() &&
         (getTopOfStack().Directive == OMPD_taskgroup ||
          ((isOpenMPParallelDirective(getTopOfStack().Directive) ||
            isOpenMPWorksharingDirective(getTopOfStack().Directive)) &&
           !isOpenMPSimdDirective(getTopOfStack().Directive))) &&
         "Additional reduction info may be specified only once for reduction "
         "items.");
  ReductionData.set(BOK, SR);
  Expr *&TaskgroupReductionRef = getTopOfStack().TaskgroupReductionRef;
  if (!TaskgroupReductionRef) {
    VarDecl *VD = buildVarDecl(SemaRef, SR.getBegin(),
                               SemaRef.Context.VoidPtrTy, ".task_red.");
    TaskgroupReductionRef =
        buildDeclRefExpr(SemaRef, VD, SemaRef.Context.VoidPtrTy, SR.getBegin());
  }
}

void DSAStackTy::addTaskgroupReductionData(const ValueDecl *D, SourceRange SR,
                                           const Expr *ReductionRef) {
  D = getCanonicalDecl(D);
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty");
  assert(
      getTopOfStack().SharingMap[D].Attributes == OMPC_reduction &&
      "Additional reduction info may be specified only for reduction items.");
  ReductionData &ReductionData = getTopOfStack().ReductionMap[D];
  assert(ReductionData.ReductionRange.isInvalid() &&
         (getTopOfStack().Directive == OMPD_taskgroup ||
          ((isOpenMPParallelDirective(getTopOfStack().Directive) ||
            isOpenMPWorksharingDirective(getTopOfStack().Directive)) &&
           !isOpenMPSimdDirective(getTopOfStack().Directive))) &&
         "Additional reduction info may be specified only once for reduction "
         "items.");
  ReductionData.set(ReductionRef, SR);
  Expr *&TaskgroupReductionRef = getTopOfStack().TaskgroupReductionRef;
  if (!TaskgroupReductionRef) {
    VarDecl *VD = buildVarDecl(SemaRef, SR.getBegin(),
                               SemaRef.Context.VoidPtrTy, ".task_red.");
    TaskgroupReductionRef =
        buildDeclRefExpr(SemaRef, VD, SemaRef.Context.VoidPtrTy, SR.getBegin());
  }
}

const DSAStackTy::DSAVarData DSAStackTy::getTopMostTaskgroupReductionData(
    const ValueDecl *D, SourceRange &SR, BinaryOperatorKind &BOK,
    Expr *&TaskgroupDescriptor) const {
  D = getCanonicalDecl(D);
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty.");
  for (const_iterator I = begin() + 1, E = end(); I != E; ++I) {
    const DSAInfo &Data = I->SharingMap.lookup(D);
    if (Data.Attributes != OMPC_reduction ||
        Data.Modifier != OMPC_REDUCTION_task)
      continue;
    const ReductionData &ReductionData = I->ReductionMap.lookup(D);
    if (!ReductionData.ReductionOp ||
        ReductionData.ReductionOp.is<const Expr *>())
      return DSAVarData();
    SR = ReductionData.ReductionRange;
    BOK = ReductionData.ReductionOp.get<ReductionData::BOKPtrType>();
    assert(I->TaskgroupReductionRef && "taskgroup reduction reference "
                                       "expression for the descriptor is not "
                                       "set.");
    TaskgroupDescriptor = I->TaskgroupReductionRef;
    return DSAVarData(I->Directive, OMPC_reduction, Data.RefExpr.getPointer(),
                      Data.PrivateCopy, I->DefaultAttrLoc, OMPC_REDUCTION_task,
                      /*AppliedToPointee=*/false);
  }
  return DSAVarData();
}

const DSAStackTy::DSAVarData DSAStackTy::getTopMostTaskgroupReductionData(
    const ValueDecl *D, SourceRange &SR, const Expr *&ReductionRef,
    Expr *&TaskgroupDescriptor) const {
  D = getCanonicalDecl(D);
  assert(!isStackEmpty() && "Data-sharing attributes stack is empty.");
  for (const_iterator I = begin() + 1, E = end(); I != E; ++I) {
    const DSAInfo &Data = I->SharingMap.lookup(D);
    if (Data.Attributes != OMPC_reduction ||
        Data.Modifier != OMPC_REDUCTION_task)
      continue;
    const ReductionData &ReductionData = I->ReductionMap.lookup(D);
    if (!ReductionData.ReductionOp ||
        !ReductionData.ReductionOp.is<const Expr *>())
      return DSAVarData();
    SR = ReductionData.ReductionRange;
    ReductionRef = ReductionData.ReductionOp.get<const Expr *>();
    assert(I->TaskgroupReductionRef && "taskgroup reduction reference "
                                       "expression for the descriptor is not "
                                       "set.");
    TaskgroupDescriptor = I->TaskgroupReductionRef;
    return DSAVarData(I->Directive, OMPC_reduction, Data.RefExpr.getPointer(),
                      Data.PrivateCopy, I->DefaultAttrLoc, OMPC_REDUCTION_task,
                      /*AppliedToPointee=*/false);
  }
  return DSAVarData();
}

bool DSAStackTy::isOpenMPLocal(VarDecl *D, const_iterator I) const {
  D = D->getCanonicalDecl();
  for (const_iterator E = end(); I != E; ++I) {
    if (isImplicitOrExplicitTaskingRegion(I->Directive) ||
        isOpenMPTargetExecutionDirective(I->Directive)) {
      if (I->CurScope) {
        Scope *TopScope = I->CurScope->getParent();
        Scope *CurScope = getCurScope();
        while (CurScope && CurScope != TopScope && !CurScope->isDeclScope(D))
          CurScope = CurScope->getParent();
        return CurScope != TopScope;
      }
      for (DeclContext *DC = D->getDeclContext(); DC; DC = DC->getParent())
        if (I->Context == DC)
          return true;
      return false;
    }
  }
  return false;
}

static bool isConstNotMutableType(Sema &SemaRef, QualType Type,
                                  bool AcceptIfMutable = true,
                                  bool *IsClassType = nullptr) {
  ASTContext &Context = SemaRef.getASTContext();
  Type = Type.getNonReferenceType().getCanonicalType();
  bool IsConstant = Type.isConstant(Context);
  Type = Context.getBaseElementType(Type);
  const CXXRecordDecl *RD = AcceptIfMutable && SemaRef.getLangOpts().CPlusPlus
                                ? Type->getAsCXXRecordDecl()
                                : nullptr;
  if (const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(RD))
    if (const ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate())
      RD = CTD->getTemplatedDecl();
  if (IsClassType)
    *IsClassType = RD;
  return IsConstant && !(SemaRef.getLangOpts().CPlusPlus && RD &&
                         RD->hasDefinition() && RD->hasMutableFields());
}

static bool rejectConstNotMutableType(Sema &SemaRef, const ValueDecl *D,
                                      QualType Type, OpenMPClauseKind CKind,
                                      SourceLocation ELoc,
                                      bool AcceptIfMutable = true,
                                      bool ListItemNotVar = false) {
  ASTContext &Context = SemaRef.getASTContext();
  bool IsClassType;
  if (isConstNotMutableType(SemaRef, Type, AcceptIfMutable, &IsClassType)) {
    unsigned Diag = ListItemNotVar ? diag::err_omp_const_list_item
                    : IsClassType  ? diag::err_omp_const_not_mutable_variable
                                   : diag::err_omp_const_variable;
    SemaRef.Diag(ELoc, Diag) << getOpenMPClauseName(CKind);
    if (!ListItemNotVar && D) {
      const VarDecl *VD = dyn_cast<VarDecl>(D);
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(Context) ==
                               VarDecl::DeclarationOnly;
      SemaRef.Diag(D->getLocation(),
                   IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
    }
    return true;
  }
  return false;
}

const DSAStackTy::DSAVarData DSAStackTy::getTopDSA(ValueDecl *D,
                                                   bool FromParent) {
  D = getCanonicalDecl(D);
  DSAVarData DVar;

  auto *VD = dyn_cast<VarDecl>(D);
  auto TI = Threadprivates.find(D);
  if (TI != Threadprivates.end()) {
    DVar.RefExpr = TI->getSecond().RefExpr.getPointer();
    DVar.CKind = OMPC_threadprivate;
    DVar.Modifier = TI->getSecond().Modifier;
    return DVar;
  }
  if (VD && VD->hasAttr<OMPThreadPrivateDeclAttr>()) {
    DVar.RefExpr = buildDeclRefExpr(
        SemaRef, VD, D->getType().getNonReferenceType(),
        VD->getAttr<OMPThreadPrivateDeclAttr>()->getLocation());
    DVar.CKind = OMPC_threadprivate;
    addDSA(D, DVar.RefExpr, OMPC_threadprivate);
    return DVar;
  }
  // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
  // in a Construct, C/C++, predetermined, p.1]
  //  Variables appearing in threadprivate directives are threadprivate.
  if ((VD && VD->getTLSKind() != VarDecl::TLS_None &&
       !(VD->hasAttr<OMPThreadPrivateDeclAttr>() &&
         SemaRef.getLangOpts().OpenMPUseTLS &&
         SemaRef.getASTContext().getTargetInfo().isTLSSupported())) ||
      (VD && VD->getStorageClass() == SC_Register &&
       VD->hasAttr<AsmLabelAttr>() && !VD->isLocalVarDecl())) {
    DVar.RefExpr = buildDeclRefExpr(
        SemaRef, VD, D->getType().getNonReferenceType(), D->getLocation());
    DVar.CKind = OMPC_threadprivate;
    addDSA(D, DVar.RefExpr, OMPC_threadprivate);
    return DVar;
  }
  if (SemaRef.getLangOpts().OpenMPCUDAMode && VD &&
      VD->isLocalVarDeclOrParm() && !isStackEmpty() &&
      !isLoopControlVariable(D).first) {
    const_iterator IterTarget =
        std::find_if(begin(), end(), [](const SharingMapTy &Data) {
          return isOpenMPTargetExecutionDirective(Data.Directive);
        });
    if (IterTarget != end()) {
      const_iterator ParentIterTarget = IterTarget + 1;
      for (const_iterator Iter = begin(); Iter != ParentIterTarget; ++Iter) {
        if (isOpenMPLocal(VD, Iter)) {
          DVar.RefExpr =
              buildDeclRefExpr(SemaRef, VD, D->getType().getNonReferenceType(),
                               D->getLocation());
          DVar.CKind = OMPC_threadprivate;
          return DVar;
        }
      }
      if (!isClauseParsingMode() || IterTarget != begin()) {
        auto DSAIter = IterTarget->SharingMap.find(D);
        if (DSAIter != IterTarget->SharingMap.end() &&
            isOpenMPPrivate(DSAIter->getSecond().Attributes)) {
          DVar.RefExpr = DSAIter->getSecond().RefExpr.getPointer();
          DVar.CKind = OMPC_threadprivate;
          return DVar;
        }
        const_iterator End = end();
        if (!SemaRef.OpenMP().isOpenMPCapturedByRef(
                D, std::distance(ParentIterTarget, End),
                /*OpenMPCaptureLevel=*/0)) {
          DVar.RefExpr =
              buildDeclRefExpr(SemaRef, VD, D->getType().getNonReferenceType(),
                               IterTarget->ConstructLoc);
          DVar.CKind = OMPC_threadprivate;
          return DVar;
        }
      }
    }
  }

  if (isStackEmpty())
    // Not in OpenMP execution region and top scope was already checked.
    return DVar;

  // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
  // in a Construct, C/C++, predetermined, p.4]
  //  Static data members are shared.
  // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
  // in a Construct, C/C++, predetermined, p.7]
  //  Variables with static storage duration that are declared in a scope
  //  inside the construct are shared.
  if (VD && VD->isStaticDataMember()) {
    // Check for explicitly specified attributes.
    const_iterator I = begin();
    const_iterator EndI = end();
    if (FromParent && I != EndI)
      ++I;
    if (I != EndI) {
      auto It = I->SharingMap.find(D);
      if (It != I->SharingMap.end()) {
        const DSAInfo &Data = It->getSecond();
        DVar.RefExpr = Data.RefExpr.getPointer();
        DVar.PrivateCopy = Data.PrivateCopy;
        DVar.CKind = Data.Attributes;
        DVar.ImplicitDSALoc = I->DefaultAttrLoc;
        DVar.DKind = I->Directive;
        DVar.Modifier = Data.Modifier;
        DVar.AppliedToPointee = Data.AppliedToPointee;
        return DVar;
      }
    }

    DVar.CKind = OMPC_shared;
    return DVar;
  }

  auto &&MatchesAlways = [](OpenMPDirectiveKind) { return true; };
  // The predetermined shared attribute for const-qualified types having no
  // mutable members was removed after OpenMP 3.1.
  if (SemaRef.LangOpts.OpenMP <= 31) {
    // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a Construct, C/C++, predetermined, p.6]
    //  Variables with const qualified type having no mutable member are
    //  shared.
    if (isConstNotMutableType(SemaRef, D->getType())) {
      // Variables with const-qualified type having no mutable member may be
      // listed in a firstprivate clause, even if they are static data members.
      DSAVarData DVarTemp = hasInnermostDSA(
          D,
          [](OpenMPClauseKind C, bool) {
            return C == OMPC_firstprivate || C == OMPC_shared;
          },
          MatchesAlways, FromParent);
      if (DVarTemp.CKind != OMPC_unknown && DVarTemp.RefExpr)
        return DVarTemp;

      DVar.CKind = OMPC_shared;
      return DVar;
    }
  }

  // Explicitly specified attributes and local variables with predetermined
  // attributes.
  const_iterator I = begin();
  const_iterator EndI = end();
  if (FromParent && I != EndI)
    ++I;
  if (I == EndI)
    return DVar;
  auto It = I->SharingMap.find(D);
  if (It != I->SharingMap.end()) {
    const DSAInfo &Data = It->getSecond();
    DVar.RefExpr = Data.RefExpr.getPointer();
    DVar.PrivateCopy = Data.PrivateCopy;
    DVar.CKind = Data.Attributes;
    DVar.ImplicitDSALoc = I->DefaultAttrLoc;
    DVar.DKind = I->Directive;
    DVar.Modifier = Data.Modifier;
    DVar.AppliedToPointee = Data.AppliedToPointee;
  }

  return DVar;
}

const DSAStackTy::DSAVarData DSAStackTy::getImplicitDSA(ValueDecl *D,
                                                        bool FromParent) const {
  if (isStackEmpty()) {
    const_iterator I;
    return getDSA(I, D);
  }
  D = getCanonicalDecl(D);
  const_iterator StartI = begin();
  const_iterator EndI = end();
  if (FromParent && StartI != EndI)
    ++StartI;
  return getDSA(StartI, D);
}

const DSAStackTy::DSAVarData DSAStackTy::getImplicitDSA(ValueDecl *D,
                                                        unsigned Level) const {
  if (getStackSize() <= Level)
    return DSAVarData();
  D = getCanonicalDecl(D);
  const_iterator StartI = std::next(begin(), getStackSize() - 1 - Level);
  return getDSA(StartI, D);
}

const DSAStackTy::DSAVarData
DSAStackTy::hasDSA(ValueDecl *D,
                   const llvm::function_ref<bool(OpenMPClauseKind, bool,
                                                 DefaultDataSharingAttributes)>
                       CPred,
                   const llvm::function_ref<bool(OpenMPDirectiveKind)> DPred,
                   bool FromParent) const {
  if (isStackEmpty())
    return {};
  D = getCanonicalDecl(D);
  const_iterator I = begin();
  const_iterator EndI = end();
  if (FromParent && I != EndI)
    ++I;
  for (; I != EndI; ++I) {
    if (!DPred(I->Directive) &&
        !isImplicitOrExplicitTaskingRegion(I->Directive))
      continue;
    const_iterator NewI = I;
    DSAVarData DVar = getDSA(NewI, D);
    if (I == NewI && CPred(DVar.CKind, DVar.AppliedToPointee, I->DefaultAttr))
      return DVar;
  }
  return {};
}

const DSAStackTy::DSAVarData DSAStackTy::hasInnermostDSA(
    ValueDecl *D, const llvm::function_ref<bool(OpenMPClauseKind, bool)> CPred,
    const llvm::function_ref<bool(OpenMPDirectiveKind)> DPred,
    bool FromParent) const {
  if (isStackEmpty())
    return {};
  D = getCanonicalDecl(D);
  const_iterator StartI = begin();
  const_iterator EndI = end();
  if (FromParent && StartI != EndI)
    ++StartI;
  if (StartI == EndI || !DPred(StartI->Directive))
    return {};
  const_iterator NewI = StartI;
  DSAVarData DVar = getDSA(NewI, D);
  return (NewI == StartI && CPred(DVar.CKind, DVar.AppliedToPointee))
             ? DVar
             : DSAVarData();
}

bool DSAStackTy::hasExplicitDSA(
    const ValueDecl *D,
    const llvm::function_ref<bool(OpenMPClauseKind, bool)> CPred,
    unsigned Level, bool NotLastprivate) const {
  if (getStackSize() <= Level)
    return false;
  D = getCanonicalDecl(D);
  const SharingMapTy &StackElem = getStackElemAtLevel(Level);
  auto I = StackElem.SharingMap.find(D);
  if (I != StackElem.SharingMap.end() && I->getSecond().RefExpr.getPointer() &&
      CPred(I->getSecond().Attributes, I->getSecond().AppliedToPointee) &&
      (!NotLastprivate || !I->getSecond().RefExpr.getInt()))
    return true;
  // Check predetermined rules for the loop control variables.
  auto LI = StackElem.LCVMap.find(D);
  if (LI != StackElem.LCVMap.end())
    return CPred(OMPC_private, /*AppliedToPointee=*/false);
  return false;
}

bool DSAStackTy::hasExplicitDirective(
    const llvm::function_ref<bool(OpenMPDirectiveKind)> DPred,
    unsigned Level) const {
  if (getStackSize() <= Level)
    return false;
  const SharingMapTy &StackElem = getStackElemAtLevel(Level);
  return DPred(StackElem.Directive);
}

bool DSAStackTy::hasDirective(
    const llvm::function_ref<bool(OpenMPDirectiveKind,
                                  const DeclarationNameInfo &, SourceLocation)>
        DPred,
    bool FromParent) const {
  // We look only in the enclosing region.
  size_t Skip = FromParent ? 2 : 1;
  for (const_iterator I = begin() + std::min(Skip, getStackSize()), E = end();
       I != E; ++I) {
    if (DPred(I->Directive, I->DirectiveName, I->ConstructLoc))
      return true;
  }
  return false;
}

void SemaOpenMP::InitDataSharingAttributesStack() {
  VarDataSharingAttributesStack = new DSAStackTy(SemaRef);
}

#define DSAStack static_cast<DSAStackTy *>(VarDataSharingAttributesStack)

void SemaOpenMP::pushOpenMPFunctionRegion() { DSAStack->pushFunction(); }

void SemaOpenMP::popOpenMPFunctionRegion(const FunctionScopeInfo *OldFSI) {
  DSAStack->popFunction(OldFSI);
}

static bool isOpenMPDeviceDelayedContext(Sema &S) {
  assert(S.LangOpts.OpenMP && S.LangOpts.OpenMPIsTargetDevice &&
         "Expected OpenMP device compilation.");
  return !S.OpenMP().isInOpenMPTargetExecutionDirective();
}

namespace {
/// Status of the function emission on the host/device.
enum class FunctionEmissionStatus {
  Emitted,
  Discarded,
  Unknown,
};
} // anonymous namespace

SemaBase::SemaDiagnosticBuilder
SemaOpenMP::diagIfOpenMPDeviceCode(SourceLocation Loc, unsigned DiagID,
                                   const FunctionDecl *FD) {
  assert(getLangOpts().OpenMP && getLangOpts().OpenMPIsTargetDevice &&
         "Expected OpenMP device compilation.");

  SemaDiagnosticBuilder::Kind Kind = SemaDiagnosticBuilder::K_Nop;
  if (FD) {
    Sema::FunctionEmissionStatus FES = SemaRef.getEmissionStatus(FD);
    switch (FES) {
    case Sema::FunctionEmissionStatus::Emitted:
      Kind = SemaDiagnosticBuilder::K_Immediate;
      break;
    case Sema::FunctionEmissionStatus::Unknown:
      // TODO: We should always delay diagnostics here in case a target
      //       region is in a function we do not emit. However, as the
      //       current diagnostics are associated with the function containing
      //       the target region and we do not emit that one, we would miss out
      //       on diagnostics for the target region itself. We need to anchor
      //       the diagnostics with the new generated function *or* ensure we
      //       emit diagnostics associated with the surrounding function.
      Kind = isOpenMPDeviceDelayedContext(SemaRef)
                 ? SemaDiagnosticBuilder::K_Deferred
                 : SemaDiagnosticBuilder::K_Immediate;
      break;
    case Sema::FunctionEmissionStatus::TemplateDiscarded:
    case Sema::FunctionEmissionStatus::OMPDiscarded:
      Kind = SemaDiagnosticBuilder::K_Nop;
      break;
    case Sema::FunctionEmissionStatus::CUDADiscarded:
      llvm_unreachable("CUDADiscarded unexpected in OpenMP device compilation");
      break;
    }
  }

  return SemaDiagnosticBuilder(Kind, Loc, DiagID, FD, SemaRef);
}

SemaBase::SemaDiagnosticBuilder
SemaOpenMP::diagIfOpenMPHostCode(SourceLocation Loc, unsigned DiagID,
                                 const FunctionDecl *FD) {
  assert(getLangOpts().OpenMP && !getLangOpts().OpenMPIsTargetDevice &&
         "Expected OpenMP host compilation.");

  SemaDiagnosticBuilder::Kind Kind = SemaDiagnosticBuilder::K_Nop;
  if (FD) {
    Sema::FunctionEmissionStatus FES = SemaRef.getEmissionStatus(FD);
    switch (FES) {
    case Sema::FunctionEmissionStatus::Emitted:
      Kind = SemaDiagnosticBuilder::K_Immediate;
      break;
    case Sema::FunctionEmissionStatus::Unknown:
      Kind = SemaDiagnosticBuilder::K_Deferred;
      break;
    case Sema::FunctionEmissionStatus::TemplateDiscarded:
    case Sema::FunctionEmissionStatus::OMPDiscarded:
    case Sema::FunctionEmissionStatus::CUDADiscarded:
      Kind = SemaDiagnosticBuilder::K_Nop;
      break;
    }
  }

  return SemaDiagnosticBuilder(Kind, Loc, DiagID, FD, SemaRef);
}

static OpenMPDefaultmapClauseKind
getVariableCategoryFromDecl(const LangOptions &LO, const ValueDecl *VD) {
  if (LO.OpenMP <= 45) {
    if (VD->getType().getNonReferenceType()->isScalarType())
      return OMPC_DEFAULTMAP_scalar;
    return OMPC_DEFAULTMAP_aggregate;
  }
  if (VD->getType().getNonReferenceType()->isAnyPointerType())
    return OMPC_DEFAULTMAP_pointer;
  if (VD->getType().getNonReferenceType()->isScalarType())
    return OMPC_DEFAULTMAP_scalar;
  return OMPC_DEFAULTMAP_aggregate;
}

bool SemaOpenMP::isOpenMPCapturedByRef(const ValueDecl *D, unsigned Level,
                                       unsigned OpenMPCaptureLevel) const {
  assert(getLangOpts().OpenMP && "OpenMP is not allowed");

  ASTContext &Ctx = getASTContext();
  bool IsByRef = true;

  // Find the directive that is associated with the provided scope.
  D = cast<ValueDecl>(D->getCanonicalDecl());
  QualType Ty = D->getType();

  bool IsVariableUsedInMapClause = false;
  if (DSAStack->hasExplicitDirective(isOpenMPTargetExecutionDirective, Level)) {
    // This table summarizes how a given variable should be passed to the device
    // given its type and the clauses where it appears. This table is based on
    // the description in OpenMP 4.5 [2.10.4, target Construct] and
    // OpenMP 4.5 [2.15.5, Data-mapping Attribute Rules and Clauses].
    //
    // =========================================================================
    // | type |  defaultmap   | pvt | first | is_device_ptr |    map   | res.  |
    // |      |(tofrom:scalar)|     |  pvt  |               |has_dv_adr|       |
    // =========================================================================
    // | scl  |               |     |       |       -       |          | bycopy|
    // | scl  |               |  -  |   x   |       -       |     -    | bycopy|
    // | scl  |               |  x  |   -   |       -       |     -    | null  |
    // | scl  |       x       |     |       |       -       |          | byref |
    // | scl  |       x       |  -  |   x   |       -       |     -    | bycopy|
    // | scl  |       x       |  x  |   -   |       -       |     -    | null  |
    // | scl  |               |  -  |   -   |       -       |     x    | byref |
    // | scl  |       x       |  -  |   -   |       -       |     x    | byref |
    //
    // | agg  |      n.a.     |     |       |       -       |          | byref |
    // | agg  |      n.a.     |  -  |   x   |       -       |     -    | byref |
    // | agg  |      n.a.     |  x  |   -   |       -       |     -    | null  |
    // | agg  |      n.a.     |  -  |   -   |       -       |     x    | byref |
    // | agg  |      n.a.     |  -  |   -   |       -       |    x[]   | byref |
    //
    // | ptr  |      n.a.     |     |       |       -       |          | bycopy|
    // | ptr  |      n.a.     |  -  |   x   |       -       |     -    | bycopy|
    // | ptr  |      n.a.     |  x  |   -   |       -       |     -    | null  |
    // | ptr  |      n.a.     |  -  |   -   |       -       |     x    | byref |
    // | ptr  |      n.a.     |  -  |   -   |       -       |    x[]   | bycopy|
    // | ptr  |      n.a.     |  -  |   -   |       x       |          | bycopy|
    // | ptr  |      n.a.     |  -  |   -   |       x       |     x    | bycopy|
    // | ptr  |      n.a.     |  -  |   -   |       x       |    x[]   | bycopy|
    // =========================================================================
    // Legend:
    //  scl - scalar
    //  ptr - pointer
    //  agg - aggregate
    //  x - applies
    //  - - invalid in this combination
    //  [] - mapped with an array section
    //  byref - should be mapped by reference
    //  byval - should be mapped by value
    //  null - initialize a local variable to null on the device
    //
    // Observations:
    //  - All scalar declarations that show up in a map clause have to be passed
    //    by reference, because they may have been mapped in the enclosing data
    //    environment.
    //  - If the scalar value does not fit the size of uintptr, it has to be
    //    passed by reference, regardless the result in the table above.
    //  - For pointers mapped by value that have either an implicit map or an
    //    array section, the runtime library may pass the NULL value to the
    //    device instead of the value passed to it by the compiler.

    if (Ty->isReferenceType())
      Ty = Ty->castAs<ReferenceType>()->getPointeeType();

    // Locate map clauses and see if the variable being captured is referred to
    // in any of those clauses. Here we only care about variables, not fields,
    // because fields are part of aggregates.
    bool IsVariableAssociatedWithSection = false;

    DSAStack->checkMappableExprComponentListsForDeclAtLevel(
        D, Level,
        [&IsVariableUsedInMapClause, &IsVariableAssociatedWithSection,
         D](OMPClauseMappableExprCommon::MappableExprComponentListRef
                MapExprComponents,
            OpenMPClauseKind WhereFoundClauseKind) {
          // Both map and has_device_addr clauses information influences how a
          // variable is captured. E.g. is_device_ptr does not require changing
          // the default behavior.
          if (WhereFoundClauseKind != OMPC_map &&
              WhereFoundClauseKind != OMPC_has_device_addr)
            return false;

          auto EI = MapExprComponents.rbegin();
          auto EE = MapExprComponents.rend();

          assert(EI != EE && "Invalid map expression!");

          if (isa<DeclRefExpr>(EI->getAssociatedExpression()))
            IsVariableUsedInMapClause |= EI->getAssociatedDeclaration() == D;

          ++EI;
          if (EI == EE)
            return false;
          auto Last = std::prev(EE);
          const auto *UO =
              dyn_cast<UnaryOperator>(Last->getAssociatedExpression());
          if ((UO && UO->getOpcode() == UO_Deref) ||
              isa<ArraySubscriptExpr>(Last->getAssociatedExpression()) ||
              isa<ArraySectionExpr>(Last->getAssociatedExpression()) ||
              isa<MemberExpr>(EI->getAssociatedExpression()) ||
              isa<OMPArrayShapingExpr>(Last->getAssociatedExpression())) {
            IsVariableAssociatedWithSection = true;
            // There is nothing more we need to know about this variable.
            return true;
          }

          // Keep looking for more map info.
          return false;
        });

    if (IsVariableUsedInMapClause) {
      // If variable is identified in a map clause it is always captured by
      // reference except if it is a pointer that is dereferenced somehow.
      IsByRef = !(Ty->isPointerType() && IsVariableAssociatedWithSection);
    } else {
      // By default, all the data that has a scalar type is mapped by copy
      // (except for reduction variables).
      // Defaultmap scalar is mutual exclusive to defaultmap pointer
      IsByRef = (DSAStack->isForceCaptureByReferenceInTargetExecutable() &&
                 !Ty->isAnyPointerType()) ||
                !Ty->isScalarType() ||
                DSAStack->isDefaultmapCapturedByRef(
                    Level, getVariableCategoryFromDecl(getLangOpts(), D)) ||
                DSAStack->hasExplicitDSA(
                    D,
                    [](OpenMPClauseKind K, bool AppliedToPointee) {
                      return K == OMPC_reduction && !AppliedToPointee;
                    },
                    Level);
    }
  }

  if (IsByRef && Ty.getNonReferenceType()->isScalarType()) {
    IsByRef =
        ((IsVariableUsedInMapClause &&
          DSAStack->getCaptureRegion(Level, OpenMPCaptureLevel) ==
              OMPD_target) ||
         !(DSAStack->hasExplicitDSA(
               D,
               [](OpenMPClauseKind K, bool AppliedToPointee) -> bool {
                 return K == OMPC_firstprivate ||
                        (K == OMPC_reduction && AppliedToPointee);
               },
               Level, /*NotLastprivate=*/true) ||
           DSAStack->isUsesAllocatorsDecl(Level, D))) &&
        // If the variable is artificial and must be captured by value - try to
        // capture by value.
        !(isa<OMPCapturedExprDecl>(D) && !D->hasAttr<OMPCaptureNoInitAttr>() &&
          !cast<OMPCapturedExprDecl>(D)->getInit()->isGLValue()) &&
        // If the variable is implicitly firstprivate and scalar - capture by
        // copy
        !((DSAStack->getDefaultDSA() == DSA_firstprivate ||
           DSAStack->getDefaultDSA() == DSA_private) &&
          !DSAStack->hasExplicitDSA(
              D, [](OpenMPClauseKind K, bool) { return K != OMPC_unknown; },
              Level) &&
          !DSAStack->isLoopControlVariable(D, Level).first);
  }

  // When passing data by copy, we need to make sure it fits the uintptr size
  // and alignment, because the runtime library only deals with uintptr types.
  // If it does not fit the uintptr size, we need to pass the data by reference
  // instead.
  if (!IsByRef && (Ctx.getTypeSizeInChars(Ty) >
                       Ctx.getTypeSizeInChars(Ctx.getUIntPtrType()) ||
                   Ctx.getAlignOfGlobalVarInChars(Ty, dyn_cast<VarDecl>(D)) >
                       Ctx.getTypeAlignInChars(Ctx.getUIntPtrType()))) {
    IsByRef = true;
  }

  return IsByRef;
}

unsigned SemaOpenMP::getOpenMPNestingLevel() const {
  assert(getLangOpts().OpenMP);
  return DSAStack->getNestingLevel();
}

bool SemaOpenMP::isInOpenMPTaskUntiedContext() const {
  return isOpenMPTaskingDirective(DSAStack->getCurrentDirective()) &&
         DSAStack->isUntiedRegion();
}

bool SemaOpenMP::isInOpenMPTargetExecutionDirective() const {
  return (isOpenMPTargetExecutionDirective(DSAStack->getCurrentDirective()) &&
          !DSAStack->isClauseParsingMode()) ||
         DSAStack->hasDirective(
             [](OpenMPDirectiveKind K, const DeclarationNameInfo &,
                SourceLocation) -> bool {
               return isOpenMPTargetExecutionDirective(K);
             },
             false);
}

bool SemaOpenMP::isOpenMPRebuildMemberExpr(ValueDecl *D) {
  // Only rebuild for Field.
  if (!dyn_cast<FieldDecl>(D))
    return false;
  DSAStackTy::DSAVarData DVarPrivate = DSAStack->hasDSA(
      D,
      [](OpenMPClauseKind C, bool AppliedToPointee,
         DefaultDataSharingAttributes DefaultAttr) {
        return isOpenMPPrivate(C) && !AppliedToPointee &&
               (DefaultAttr == DSA_firstprivate || DefaultAttr == DSA_private);
      },
      [](OpenMPDirectiveKind) { return true; },
      DSAStack->isClauseParsingMode());
  if (DVarPrivate.CKind != OMPC_unknown)
    return true;
  return false;
}

static OMPCapturedExprDecl *buildCaptureDecl(Sema &S, IdentifierInfo *Id,
                                             Expr *CaptureExpr, bool WithInit,
                                             DeclContext *CurContext,
                                             bool AsExpression);

VarDecl *SemaOpenMP::isOpenMPCapturedDecl(ValueDecl *D, bool CheckScopeInfo,
                                          unsigned StopAt) {
  assert(getLangOpts().OpenMP && "OpenMP is not allowed");
  D = getCanonicalDecl(D);

  auto *VD = dyn_cast<VarDecl>(D);
  // Do not capture constexpr variables.
  if (VD && VD->isConstexpr())
    return nullptr;

  // If we want to determine whether the variable should be captured from the
  // perspective of the current capturing scope, and we've already left all the
  // capturing scopes of the top directive on the stack, check from the
  // perspective of its parent directive (if any) instead.
  DSAStackTy::ParentDirectiveScope InParentDirectiveRAII(
      *DSAStack, CheckScopeInfo && DSAStack->isBodyComplete());

  // If we are attempting to capture a global variable in a directive with
  // 'target' we return true so that this global is also mapped to the device.
  //
  if (VD && !VD->hasLocalStorage() &&
      (SemaRef.getCurCapturedRegion() || SemaRef.getCurBlock() ||
       SemaRef.getCurLambda())) {
    if (isInOpenMPTargetExecutionDirective()) {
      DSAStackTy::DSAVarData DVarTop =
          DSAStack->getTopDSA(D, DSAStack->isClauseParsingMode());
      if (DVarTop.CKind != OMPC_unknown && DVarTop.RefExpr)
        return VD;
      // If the declaration is enclosed in a 'declare target' directive,
      // then it should not be captured.
      //
      if (OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD))
        return nullptr;
      CapturedRegionScopeInfo *CSI = nullptr;
      for (FunctionScopeInfo *FSI : llvm::drop_begin(
               llvm::reverse(SemaRef.FunctionScopes),
               CheckScopeInfo ? (SemaRef.FunctionScopes.size() - (StopAt + 1))
                              : 0)) {
        if (!isa<CapturingScopeInfo>(FSI))
          return nullptr;
        if (auto *RSI = dyn_cast<CapturedRegionScopeInfo>(FSI))
          if (RSI->CapRegionKind == CR_OpenMP) {
            CSI = RSI;
            break;
          }
      }
      assert(CSI && "Failed to find CapturedRegionScopeInfo");
      SmallVector<OpenMPDirectiveKind, 4> Regions;
      getOpenMPCaptureRegions(Regions,
                              DSAStack->getDirective(CSI->OpenMPLevel));
      if (Regions[CSI->OpenMPCaptureLevel] != OMPD_task)
        return VD;
    }
    if (isInOpenMPDeclareTargetContext()) {
      // Try to mark variable as declare target if it is used in capturing
      // regions.
      if (getLangOpts().OpenMP <= 45 &&
          !OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD))
        checkDeclIsAllowedInOpenMPTarget(nullptr, VD);
      return nullptr;
    }
  }

  if (CheckScopeInfo) {
    bool OpenMPFound = false;
    for (unsigned I = StopAt + 1; I > 0; --I) {
      FunctionScopeInfo *FSI = SemaRef.FunctionScopes[I - 1];
      if (!isa<CapturingScopeInfo>(FSI))
        return nullptr;
      if (auto *RSI = dyn_cast<CapturedRegionScopeInfo>(FSI))
        if (RSI->CapRegionKind == CR_OpenMP) {
          OpenMPFound = true;
          break;
        }
    }
    if (!OpenMPFound)
      return nullptr;
  }

  if (DSAStack->getCurrentDirective() != OMPD_unknown &&
      (!DSAStack->isClauseParsingMode() ||
       DSAStack->getParentDirective() != OMPD_unknown)) {
    auto &&Info = DSAStack->isLoopControlVariable(D);
    if (Info.first ||
        (VD && VD->hasLocalStorage() &&
         isImplicitOrExplicitTaskingRegion(DSAStack->getCurrentDirective())) ||
        (VD && DSAStack->isForceVarCapturing()))
      return VD ? VD : Info.second;
    DSAStackTy::DSAVarData DVarTop =
        DSAStack->getTopDSA(D, DSAStack->isClauseParsingMode());
    if (DVarTop.CKind != OMPC_unknown && isOpenMPPrivate(DVarTop.CKind) &&
        (!VD || VD->hasLocalStorage() || !DVarTop.AppliedToPointee))
      return VD ? VD : cast<VarDecl>(DVarTop.PrivateCopy->getDecl());
    // Threadprivate variables must not be captured.
    if (isOpenMPThreadPrivate(DVarTop.CKind))
      return nullptr;
    // The variable is not private or it is the variable in the directive with
    // default(none) clause and not used in any clause.
    DSAStackTy::DSAVarData DVarPrivate = DSAStack->hasDSA(
        D,
        [](OpenMPClauseKind C, bool AppliedToPointee, bool) {
          return isOpenMPPrivate(C) && !AppliedToPointee;
        },
        [](OpenMPDirectiveKind) { return true; },
        DSAStack->isClauseParsingMode());
    // Global shared must not be captured.
    if (VD && !VD->hasLocalStorage() && DVarPrivate.CKind == OMPC_unknown &&
        ((DSAStack->getDefaultDSA() != DSA_none &&
          DSAStack->getDefaultDSA() != DSA_private &&
          DSAStack->getDefaultDSA() != DSA_firstprivate) ||
         DVarTop.CKind == OMPC_shared))
      return nullptr;
    auto *FD = dyn_cast<FieldDecl>(D);
    if (DVarPrivate.CKind != OMPC_unknown && !VD && FD &&
        !DVarPrivate.PrivateCopy) {
      DSAStackTy::DSAVarData DVarPrivate = DSAStack->hasDSA(
          D,
          [](OpenMPClauseKind C, bool AppliedToPointee,
             DefaultDataSharingAttributes DefaultAttr) {
            return isOpenMPPrivate(C) && !AppliedToPointee &&
                   (DefaultAttr == DSA_firstprivate ||
                    DefaultAttr == DSA_private);
          },
          [](OpenMPDirectiveKind) { return true; },
          DSAStack->isClauseParsingMode());
      if (DVarPrivate.CKind == OMPC_unknown)
        return nullptr;

      VarDecl *VD = DSAStack->getImplicitFDCapExprDecl(FD);
      if (VD)
        return VD;
      if (SemaRef.getCurrentThisType().isNull())
        return nullptr;
      Expr *ThisExpr = SemaRef.BuildCXXThisExpr(SourceLocation(),
                                                SemaRef.getCurrentThisType(),
                                                /*IsImplicit=*/true);
      const CXXScopeSpec CS = CXXScopeSpec();
      Expr *ME = SemaRef.BuildMemberExpr(
          ThisExpr, /*IsArrow=*/true, SourceLocation(),
          NestedNameSpecifierLoc(), SourceLocation(), FD,
          DeclAccessPair::make(FD, FD->getAccess()),
          /*HadMultipleCandidates=*/false, DeclarationNameInfo(), FD->getType(),
          VK_LValue, OK_Ordinary);
      OMPCapturedExprDecl *CD = buildCaptureDecl(
          SemaRef, FD->getIdentifier(), ME, DVarPrivate.CKind != OMPC_private,
          SemaRef.CurContext->getParent(), /*AsExpression=*/false);
      DeclRefExpr *VDPrivateRefExpr = buildDeclRefExpr(
          SemaRef, CD, CD->getType().getNonReferenceType(), SourceLocation());
      VD = cast<VarDecl>(VDPrivateRefExpr->getDecl());
      DSAStack->addImplicitDefaultFirstprivateFD(FD, VD);
      return VD;
    }
    if (DVarPrivate.CKind != OMPC_unknown ||
        (VD && (DSAStack->getDefaultDSA() == DSA_none ||
                DSAStack->getDefaultDSA() == DSA_private ||
                DSAStack->getDefaultDSA() == DSA_firstprivate)))
      return VD ? VD : cast<VarDecl>(DVarPrivate.PrivateCopy->getDecl());
  }
  return nullptr;
}

void SemaOpenMP::adjustOpenMPTargetScopeIndex(unsigned &FunctionScopesIndex,
                                              unsigned Level) const {
  FunctionScopesIndex -= getOpenMPCaptureLevels(DSAStack->getDirective(Level));
}

void SemaOpenMP::startOpenMPLoop() {
  assert(getLangOpts().OpenMP && "OpenMP must be enabled.");
  if (isOpenMPLoopDirective(DSAStack->getCurrentDirective()))
    DSAStack->loopInit();
}

void SemaOpenMP::startOpenMPCXXRangeFor() {
  assert(getLangOpts().OpenMP && "OpenMP must be enabled.");
  if (isOpenMPLoopDirective(DSAStack->getCurrentDirective())) {
    DSAStack->resetPossibleLoopCounter();
    DSAStack->loopStart();
  }
}

OpenMPClauseKind SemaOpenMP::isOpenMPPrivateDecl(ValueDecl *D, unsigned Level,
                                                 unsigned CapLevel) const {
  assert(getLangOpts().OpenMP && "OpenMP is not allowed");
  if (DSAStack->getCurrentDirective() != OMPD_unknown &&
      (!DSAStack->isClauseParsingMode() ||
       DSAStack->getParentDirective() != OMPD_unknown)) {
    DSAStackTy::DSAVarData DVarPrivate = DSAStack->hasDSA(
        D,
        [](OpenMPClauseKind C, bool AppliedToPointee,
           DefaultDataSharingAttributes DefaultAttr) {
          return isOpenMPPrivate(C) && !AppliedToPointee &&
                 DefaultAttr == DSA_private;
        },
        [](OpenMPDirectiveKind) { return true; },
        DSAStack->isClauseParsingMode());
    if (DVarPrivate.CKind == OMPC_private && isa<OMPCapturedExprDecl>(D) &&
        DSAStack->isImplicitDefaultFirstprivateFD(cast<VarDecl>(D)) &&
        !DSAStack->isLoopControlVariable(D).first)
      return OMPC_private;
  }
  if (DSAStack->hasExplicitDirective(isOpenMPTaskingDirective, Level)) {
    bool IsTriviallyCopyable =
        D->getType().getNonReferenceType().isTriviallyCopyableType(
            getASTContext()) &&
        !D->getType()
             .getNonReferenceType()
             .getCanonicalType()
             ->getAsCXXRecordDecl();
    OpenMPDirectiveKind DKind = DSAStack->getDirective(Level);
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, DKind);
    if (isOpenMPTaskingDirective(CaptureRegions[CapLevel]) &&
        (IsTriviallyCopyable ||
         !isOpenMPTaskLoopDirective(CaptureRegions[CapLevel]))) {
      if (DSAStack->hasExplicitDSA(
              D,
              [](OpenMPClauseKind K, bool) { return K == OMPC_firstprivate; },
              Level, /*NotLastprivate=*/true))
        return OMPC_firstprivate;
      DSAStackTy::DSAVarData DVar = DSAStack->getImplicitDSA(D, Level);
      if (DVar.CKind != OMPC_shared &&
          !DSAStack->isLoopControlVariable(D, Level).first && !DVar.RefExpr) {
        DSAStack->addImplicitTaskFirstprivate(Level, D);
        return OMPC_firstprivate;
      }
    }
  }
  if (isOpenMPLoopDirective(DSAStack->getCurrentDirective()) &&
      !isOpenMPLoopTransformationDirective(DSAStack->getCurrentDirective())) {
    if (DSAStack->getAssociatedLoops() > 0 && !DSAStack->isLoopStarted()) {
      DSAStack->resetPossibleLoopCounter(D);
      DSAStack->loopStart();
      return OMPC_private;
    }
    if ((DSAStack->getPossiblyLoopCounter() == D->getCanonicalDecl() ||
         DSAStack->isLoopControlVariable(D).first) &&
        !DSAStack->hasExplicitDSA(
            D, [](OpenMPClauseKind K, bool) { return K != OMPC_private; },
            Level) &&
        !isOpenMPSimdDirective(DSAStack->getCurrentDirective()))
      return OMPC_private;
  }
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (DSAStack->isThreadPrivate(const_cast<VarDecl *>(VD)) &&
        DSAStack->isForceVarCapturing() &&
        !DSAStack->hasExplicitDSA(
            D, [](OpenMPClauseKind K, bool) { return K == OMPC_copyin; },
            Level))
      return OMPC_private;
  }
  // User-defined allocators are private since they must be defined in the
  // context of target region.
  if (DSAStack->hasExplicitDirective(isOpenMPTargetExecutionDirective, Level) &&
      DSAStack->isUsesAllocatorsDecl(Level, D).value_or(
          DSAStackTy::UsesAllocatorsDeclKind::AllocatorTrait) ==
          DSAStackTy::UsesAllocatorsDeclKind::UserDefinedAllocator)
    return OMPC_private;
  return (DSAStack->hasExplicitDSA(
              D, [](OpenMPClauseKind K, bool) { return K == OMPC_private; },
              Level) ||
          (DSAStack->isClauseParsingMode() &&
           DSAStack->getClauseParsingMode() == OMPC_private) ||
          // Consider taskgroup reduction descriptor variable a private
          // to avoid possible capture in the region.
          (DSAStack->hasExplicitDirective(
               [](OpenMPDirectiveKind K) {
                 return K == OMPD_taskgroup ||
                        ((isOpenMPParallelDirective(K) ||
                          isOpenMPWorksharingDirective(K)) &&
                         !isOpenMPSimdDirective(K));
               },
               Level) &&
           DSAStack->isTaskgroupReductionRef(D, Level)))
             ? OMPC_private
             : OMPC_unknown;
}

void SemaOpenMP::setOpenMPCaptureKind(FieldDecl *FD, const ValueDecl *D,
                                      unsigned Level) {
  assert(getLangOpts().OpenMP && "OpenMP is not allowed");
  D = getCanonicalDecl(D);
  OpenMPClauseKind OMPC = OMPC_unknown;
  for (unsigned I = DSAStack->getNestingLevel() + 1; I > Level; --I) {
    const unsigned NewLevel = I - 1;
    if (DSAStack->hasExplicitDSA(
            D,
            [&OMPC](const OpenMPClauseKind K, bool AppliedToPointee) {
              if (isOpenMPPrivate(K) && !AppliedToPointee) {
                OMPC = K;
                return true;
              }
              return false;
            },
            NewLevel))
      break;
    if (DSAStack->checkMappableExprComponentListsForDeclAtLevel(
            D, NewLevel,
            [](OMPClauseMappableExprCommon::MappableExprComponentListRef,
               OpenMPClauseKind) { return true; })) {
      OMPC = OMPC_map;
      break;
    }
    if (DSAStack->hasExplicitDirective(isOpenMPTargetExecutionDirective,
                                       NewLevel)) {
      OMPC = OMPC_map;
      if (DSAStack->mustBeFirstprivateAtLevel(
              NewLevel, getVariableCategoryFromDecl(getLangOpts(), D)))
        OMPC = OMPC_firstprivate;
      break;
    }
  }
  if (OMPC != OMPC_unknown)
    FD->addAttr(
        OMPCaptureKindAttr::CreateImplicit(getASTContext(), unsigned(OMPC)));
}

bool SemaOpenMP::isOpenMPTargetCapturedDecl(const ValueDecl *D, unsigned Level,
                                            unsigned CaptureLevel) const {
  assert(getLangOpts().OpenMP && "OpenMP is not allowed");
  // Return true if the current level is no longer enclosed in a target region.

  SmallVector<OpenMPDirectiveKind, 4> Regions;
  getOpenMPCaptureRegions(Regions, DSAStack->getDirective(Level));
  const auto *VD = dyn_cast<VarDecl>(D);
  return VD && !VD->hasLocalStorage() &&
         DSAStack->hasExplicitDirective(isOpenMPTargetExecutionDirective,
                                        Level) &&
         Regions[CaptureLevel] != OMPD_task;
}

bool SemaOpenMP::isOpenMPGlobalCapturedDecl(ValueDecl *D, unsigned Level,
                                            unsigned CaptureLevel) const {
  assert(getLangOpts().OpenMP && "OpenMP is not allowed");
  // Return true if the current level is no longer enclosed in a target region.

  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (!VD->hasLocalStorage()) {
      if (isInOpenMPTargetExecutionDirective())
        return true;
      DSAStackTy::DSAVarData TopDVar =
          DSAStack->getTopDSA(D, /*FromParent=*/false);
      unsigned NumLevels =
          getOpenMPCaptureLevels(DSAStack->getDirective(Level));
      if (Level == 0)
        // non-file scope static variable with default(firstprivate)
        // should be global captured.
        return (NumLevels == CaptureLevel + 1 &&
                (TopDVar.CKind != OMPC_shared ||
                 DSAStack->getDefaultDSA() == DSA_firstprivate));
      do {
        --Level;
        DSAStackTy::DSAVarData DVar = DSAStack->getImplicitDSA(D, Level);
        if (DVar.CKind != OMPC_shared)
          return true;
      } while (Level > 0);
    }
  }
  return true;
}

void SemaOpenMP::DestroyDataSharingAttributesStack() { delete DSAStack; }

void SemaOpenMP::ActOnOpenMPBeginDeclareVariant(SourceLocation Loc,
                                                OMPTraitInfo &TI) {
  OMPDeclareVariantScopes.push_back(OMPDeclareVariantScope(TI));
}

void SemaOpenMP::ActOnOpenMPEndDeclareVariant() {
  assert(isInOpenMPDeclareVariantScope() &&
         "Not in OpenMP declare variant scope!");

  OMPDeclareVariantScopes.pop_back();
}

void SemaOpenMP::finalizeOpenMPDelayedAnalysis(const FunctionDecl *Caller,
                                               const FunctionDecl *Callee,
                                               SourceLocation Loc) {
  assert(getLangOpts().OpenMP && "Expected OpenMP compilation mode.");
  std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> DevTy =
      OMPDeclareTargetDeclAttr::getDeviceType(Caller->getMostRecentDecl());
  // Ignore host functions during device analysis.
  if (getLangOpts().OpenMPIsTargetDevice &&
      (!DevTy || *DevTy == OMPDeclareTargetDeclAttr::DT_Host))
    return;
  // Ignore nohost functions during host analysis.
  if (!getLangOpts().OpenMPIsTargetDevice && DevTy &&
      *DevTy == OMPDeclareTargetDeclAttr::DT_NoHost)
    return;
  const FunctionDecl *FD = Callee->getMostRecentDecl();
  DevTy = OMPDeclareTargetDeclAttr::getDeviceType(FD);
  if (getLangOpts().OpenMPIsTargetDevice && DevTy &&
      *DevTy == OMPDeclareTargetDeclAttr::DT_Host) {
    // Diagnose host function called during device codegen.
    StringRef HostDevTy =
        getOpenMPSimpleClauseTypeName(OMPC_device_type, OMPC_DEVICE_TYPE_host);
    Diag(Loc, diag::err_omp_wrong_device_function_call) << HostDevTy << 0;
    Diag(*OMPDeclareTargetDeclAttr::getLocation(FD),
         diag::note_omp_marked_device_type_here)
        << HostDevTy;
    return;
  }
  if (!getLangOpts().OpenMPIsTargetDevice &&
      !getLangOpts().OpenMPOffloadMandatory && DevTy &&
      *DevTy == OMPDeclareTargetDeclAttr::DT_NoHost) {
    // In OpenMP 5.2 or later, if the function has a host variant then allow
    // that to be called instead
    auto &&HasHostAttr = [](const FunctionDecl *Callee) {
      for (OMPDeclareVariantAttr *A :
           Callee->specific_attrs<OMPDeclareVariantAttr>()) {
        auto *DeclRefVariant = cast<DeclRefExpr>(A->getVariantFuncRef());
        auto *VariantFD = cast<FunctionDecl>(DeclRefVariant->getDecl());
        std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> DevTy =
            OMPDeclareTargetDeclAttr::getDeviceType(
                VariantFD->getMostRecentDecl());
        if (!DevTy || *DevTy == OMPDeclareTargetDeclAttr::DT_Host)
          return true;
      }
      return false;
    };
    if (getLangOpts().OpenMP >= 52 &&
        Callee->hasAttr<OMPDeclareVariantAttr>() && HasHostAttr(Callee))
      return;
    // Diagnose nohost function called during host codegen.
    StringRef NoHostDevTy = getOpenMPSimpleClauseTypeName(
        OMPC_device_type, OMPC_DEVICE_TYPE_nohost);
    Diag(Loc, diag::err_omp_wrong_device_function_call) << NoHostDevTy << 1;
    Diag(*OMPDeclareTargetDeclAttr::getLocation(FD),
         diag::note_omp_marked_device_type_here)
        << NoHostDevTy;
  }
}

void SemaOpenMP::StartOpenMPDSABlock(OpenMPDirectiveKind DKind,
                                     const DeclarationNameInfo &DirName,
                                     Scope *CurScope, SourceLocation Loc) {
  DSAStack->push(DKind, DirName, CurScope, Loc);
  SemaRef.PushExpressionEvaluationContext(
      Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
}

void SemaOpenMP::StartOpenMPClause(OpenMPClauseKind K) {
  DSAStack->setClauseParsingMode(K);
}

void SemaOpenMP::EndOpenMPClause() {
  DSAStack->setClauseParsingMode(/*K=*/OMPC_unknown);
  SemaRef.CleanupVarDeclMarking();
}

static std::pair<ValueDecl *, bool>
getPrivateItem(Sema &S, Expr *&RefExpr, SourceLocation &ELoc,
               SourceRange &ERange, bool AllowArraySection = false,
               StringRef DiagType = "");

/// Check consistency of the reduction clauses.
static void checkReductionClauses(Sema &S, DSAStackTy *Stack,
                                  ArrayRef<OMPClause *> Clauses) {
  bool InscanFound = false;
  SourceLocation InscanLoc;
  // OpenMP 5.0, 2.19.5.4 reduction Clause, Restrictions.
  // A reduction clause without the inscan reduction-modifier may not appear on
  // a construct on which a reduction clause with the inscan reduction-modifier
  // appears.
  for (OMPClause *C : Clauses) {
    if (C->getClauseKind() != OMPC_reduction)
      continue;
    auto *RC = cast<OMPReductionClause>(C);
    if (RC->getModifier() == OMPC_REDUCTION_inscan) {
      InscanFound = true;
      InscanLoc = RC->getModifierLoc();
      continue;
    }
    if (RC->getModifier() == OMPC_REDUCTION_task) {
      // OpenMP 5.0, 2.19.5.4 reduction Clause.
      // A reduction clause with the task reduction-modifier may only appear on
      // a parallel construct, a worksharing construct or a combined or
      // composite construct for which any of the aforementioned constructs is a
      // constituent construct and simd or loop are not constituent constructs.
      OpenMPDirectiveKind CurDir = Stack->getCurrentDirective();
      if (!(isOpenMPParallelDirective(CurDir) ||
            isOpenMPWorksharingDirective(CurDir)) ||
          isOpenMPSimdDirective(CurDir))
        S.Diag(RC->getModifierLoc(),
               diag::err_omp_reduction_task_not_parallel_or_worksharing);
      continue;
    }
  }
  if (InscanFound) {
    for (OMPClause *C : Clauses) {
      if (C->getClauseKind() != OMPC_reduction)
        continue;
      auto *RC = cast<OMPReductionClause>(C);
      if (RC->getModifier() != OMPC_REDUCTION_inscan) {
        S.Diag(RC->getModifier() == OMPC_REDUCTION_unknown
                   ? RC->getBeginLoc()
                   : RC->getModifierLoc(),
               diag::err_omp_inscan_reduction_expected);
        S.Diag(InscanLoc, diag::note_omp_previous_inscan_reduction);
        continue;
      }
      for (Expr *Ref : RC->varlists()) {
        assert(Ref && "NULL expr in OpenMP nontemporal clause.");
        SourceLocation ELoc;
        SourceRange ERange;
        Expr *SimpleRefExpr = Ref;
        auto Res = getPrivateItem(S, SimpleRefExpr, ELoc, ERange,
                                  /*AllowArraySection=*/true);
        ValueDecl *D = Res.first;
        if (!D)
          continue;
        if (!Stack->isUsedInScanDirective(getCanonicalDecl(D))) {
          S.Diag(Ref->getExprLoc(),
                 diag::err_omp_reduction_not_inclusive_exclusive)
              << Ref->getSourceRange();
        }
      }
    }
  }
}

static void checkAllocateClauses(Sema &S, DSAStackTy *Stack,
                                 ArrayRef<OMPClause *> Clauses);
static DeclRefExpr *buildCapture(Sema &S, ValueDecl *D, Expr *CaptureExpr,
                                 bool WithInit);

static void reportOriginalDsa(Sema &SemaRef, const DSAStackTy *Stack,
                              const ValueDecl *D,
                              const DSAStackTy::DSAVarData &DVar,
                              bool IsLoopIterVar = false);

void SemaOpenMP::EndOpenMPDSABlock(Stmt *CurDirective) {
  // OpenMP [2.14.3.5, Restrictions, C/C++, p.1]
  //  A variable of class type (or array thereof) that appears in a lastprivate
  //  clause requires an accessible, unambiguous default constructor for the
  //  class type, unless the list item is also specified in a firstprivate
  //  clause.
  if (const auto *D = dyn_cast_or_null<OMPExecutableDirective>(CurDirective)) {
    for (OMPClause *C : D->clauses()) {
      if (auto *Clause = dyn_cast<OMPLastprivateClause>(C)) {
        SmallVector<Expr *, 8> PrivateCopies;
        for (Expr *DE : Clause->varlists()) {
          if (DE->isValueDependent() || DE->isTypeDependent()) {
            PrivateCopies.push_back(nullptr);
            continue;
          }
          auto *DRE = cast<DeclRefExpr>(DE->IgnoreParens());
          auto *VD = cast<VarDecl>(DRE->getDecl());
          QualType Type = VD->getType().getNonReferenceType();
          const DSAStackTy::DSAVarData DVar =
              DSAStack->getTopDSA(VD, /*FromParent=*/false);
          if (DVar.CKind == OMPC_lastprivate) {
            // Generate helper private variable and initialize it with the
            // default value. The address of the original variable is replaced
            // by the address of the new private variable in CodeGen. This new
            // variable is not added to IdResolver, so the code in the OpenMP
            // region uses original variable for proper diagnostics.
            VarDecl *VDPrivate = buildVarDecl(
                SemaRef, DE->getExprLoc(), Type.getUnqualifiedType(),
                VD->getName(), VD->hasAttrs() ? &VD->getAttrs() : nullptr, DRE);
            SemaRef.ActOnUninitializedDecl(VDPrivate);
            if (VDPrivate->isInvalidDecl()) {
              PrivateCopies.push_back(nullptr);
              continue;
            }
            PrivateCopies.push_back(buildDeclRefExpr(
                SemaRef, VDPrivate, DE->getType(), DE->getExprLoc()));
          } else {
            // The variable is also a firstprivate, so initialization sequence
            // for private copy is generated already.
            PrivateCopies.push_back(nullptr);
          }
        }
        Clause->setPrivateCopies(PrivateCopies);
        continue;
      }
      // Finalize nontemporal clause by handling private copies, if any.
      if (auto *Clause = dyn_cast<OMPNontemporalClause>(C)) {
        SmallVector<Expr *, 8> PrivateRefs;
        for (Expr *RefExpr : Clause->varlists()) {
          assert(RefExpr && "NULL expr in OpenMP nontemporal clause.");
          SourceLocation ELoc;
          SourceRange ERange;
          Expr *SimpleRefExpr = RefExpr;
          auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
          if (Res.second)
            // It will be analyzed later.
            PrivateRefs.push_back(RefExpr);
          ValueDecl *D = Res.first;
          if (!D)
            continue;

          const DSAStackTy::DSAVarData DVar =
              DSAStack->getTopDSA(D, /*FromParent=*/false);
          PrivateRefs.push_back(DVar.PrivateCopy ? DVar.PrivateCopy
                                                 : SimpleRefExpr);
        }
        Clause->setPrivateRefs(PrivateRefs);
        continue;
      }
      if (auto *Clause = dyn_cast<OMPUsesAllocatorsClause>(C)) {
        for (unsigned I = 0, E = Clause->getNumberOfAllocators(); I < E; ++I) {
          OMPUsesAllocatorsClause::Data D = Clause->getAllocatorData(I);
          auto *DRE = dyn_cast<DeclRefExpr>(D.Allocator->IgnoreParenImpCasts());
          if (!DRE)
            continue;
          ValueDecl *VD = DRE->getDecl();
          if (!VD || !isa<VarDecl>(VD))
            continue;
          DSAStackTy::DSAVarData DVar =
              DSAStack->getTopDSA(VD, /*FromParent=*/false);
          // OpenMP [2.12.5, target Construct]
          // Memory allocators that appear in a uses_allocators clause cannot
          // appear in other data-sharing attribute clauses or data-mapping
          // attribute clauses in the same construct.
          Expr *MapExpr = nullptr;
          if (DVar.RefExpr ||
              DSAStack->checkMappableExprComponentListsForDecl(
                  VD, /*CurrentRegionOnly=*/true,
                  [VD, &MapExpr](
                      OMPClauseMappableExprCommon::MappableExprComponentListRef
                          MapExprComponents,
                      OpenMPClauseKind C) {
                    auto MI = MapExprComponents.rbegin();
                    auto ME = MapExprComponents.rend();
                    if (MI != ME &&
                        MI->getAssociatedDeclaration()->getCanonicalDecl() ==
                            VD->getCanonicalDecl()) {
                      MapExpr = MI->getAssociatedExpression();
                      return true;
                    }
                    return false;
                  })) {
            Diag(D.Allocator->getExprLoc(),
                 diag::err_omp_allocator_used_in_clauses)
                << D.Allocator->getSourceRange();
            if (DVar.RefExpr)
              reportOriginalDsa(SemaRef, DSAStack, VD, DVar);
            else
              Diag(MapExpr->getExprLoc(), diag::note_used_here)
                  << MapExpr->getSourceRange();
          }
        }
        continue;
      }
    }
    // Check allocate clauses.
    if (!SemaRef.CurContext->isDependentContext())
      checkAllocateClauses(SemaRef, DSAStack, D->clauses());
    checkReductionClauses(SemaRef, DSAStack, D->clauses());
  }

  DSAStack->pop();
  SemaRef.DiscardCleanupsInEvaluationContext();
  SemaRef.PopExpressionEvaluationContext();
}

static bool FinishOpenMPLinearClause(OMPLinearClause &Clause, DeclRefExpr *IV,
                                     Expr *NumIterations, Sema &SemaRef,
                                     Scope *S, DSAStackTy *Stack);

static bool finishLinearClauses(Sema &SemaRef, ArrayRef<OMPClause *> Clauses,
                                OMPLoopBasedDirective::HelperExprs &B,
                                DSAStackTy *Stack) {
  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "loop exprs were not built");

  if (SemaRef.CurContext->isDependentContext())
    return false;

  // Finalize the clauses that need pre-built expressions for CodeGen.
  for (OMPClause *C : Clauses) {
    auto *LC = dyn_cast<OMPLinearClause>(C);
    if (!LC)
      continue;
    if (FinishOpenMPLinearClause(*LC, cast<DeclRefExpr>(B.IterationVarRef),
                                 B.NumIterations, SemaRef,
                                 SemaRef.getCurScope(), Stack))
      return true;
  }

  return false;
}

namespace {

class VarDeclFilterCCC final : public CorrectionCandidateCallback {
private:
  Sema &SemaRef;

public:
  explicit VarDeclFilterCCC(Sema &S) : SemaRef(S) {}
  bool ValidateCandidate(const TypoCorrection &Candidate) override {
    NamedDecl *ND = Candidate.getCorrectionDecl();
    if (const auto *VD = dyn_cast_or_null<VarDecl>(ND)) {
      return VD->hasGlobalStorage() &&
             SemaRef.isDeclInScope(ND, SemaRef.getCurLexicalContext(),
                                   SemaRef.getCurScope());
    }
    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<VarDeclFilterCCC>(*this);
  }
};

class VarOrFuncDeclFilterCCC final : public CorrectionCandidateCallback {
private:
  Sema &SemaRef;

public:
  explicit VarOrFuncDeclFilterCCC(Sema &S) : SemaRef(S) {}
  bool ValidateCandidate(const TypoCorrection &Candidate) override {
    NamedDecl *ND = Candidate.getCorrectionDecl();
    if (ND && ((isa<VarDecl>(ND) && ND->getKind() == Decl::Var) ||
               isa<FunctionDecl>(ND))) {
      return SemaRef.isDeclInScope(ND, SemaRef.getCurLexicalContext(),
                                   SemaRef.getCurScope());
    }
    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<VarOrFuncDeclFilterCCC>(*this);
  }
};

} // namespace

ExprResult SemaOpenMP::ActOnOpenMPIdExpression(Scope *CurScope,
                                               CXXScopeSpec &ScopeSpec,
                                               const DeclarationNameInfo &Id,
                                               OpenMPDirectiveKind Kind) {
  ASTContext &Context = getASTContext();
  LookupResult Lookup(SemaRef, Id, Sema::LookupOrdinaryName);
  SemaRef.LookupParsedName(Lookup, CurScope, &ScopeSpec,
                           /*ObjectType=*/QualType(),
                           /*AllowBuiltinCreation=*/true);

  if (Lookup.isAmbiguous())
    return ExprError();

  VarDecl *VD;
  if (!Lookup.isSingleResult()) {
    VarDeclFilterCCC CCC(SemaRef);
    if (TypoCorrection Corrected =
            SemaRef.CorrectTypo(Id, Sema::LookupOrdinaryName, CurScope, nullptr,
                                CCC, Sema::CTK_ErrorRecovery)) {
      SemaRef.diagnoseTypo(Corrected,
                           PDiag(Lookup.empty()
                                     ? diag::err_undeclared_var_use_suggest
                                     : diag::err_omp_expected_var_arg_suggest)
                               << Id.getName());
      VD = Corrected.getCorrectionDeclAs<VarDecl>();
    } else {
      Diag(Id.getLoc(), Lookup.empty() ? diag::err_undeclared_var_use
                                       : diag::err_omp_expected_var_arg)
          << Id.getName();
      return ExprError();
    }
  } else if (!(VD = Lookup.getAsSingle<VarDecl>())) {
    Diag(Id.getLoc(), diag::err_omp_expected_var_arg) << Id.getName();
    Diag(Lookup.getFoundDecl()->getLocation(), diag::note_declared_at);
    return ExprError();
  }
  Lookup.suppressDiagnostics();

  // OpenMP [2.9.2, Syntax, C/C++]
  //   Variables must be file-scope, namespace-scope, or static block-scope.
  if (Kind == OMPD_threadprivate && !VD->hasGlobalStorage()) {
    Diag(Id.getLoc(), diag::err_omp_global_var_arg)
        << getOpenMPDirectiveName(Kind) << !VD->isStaticLocal();
    bool IsDecl =
        VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
    Diag(VD->getLocation(),
         IsDecl ? diag::note_previous_decl : diag::note_defined_here)
        << VD;
    return ExprError();
  }

  VarDecl *CanonicalVD = VD->getCanonicalDecl();
  NamedDecl *ND = CanonicalVD;
  // OpenMP [2.9.2, Restrictions, C/C++, p.2]
  //   A threadprivate directive for file-scope variables must appear outside
  //   any definition or declaration.
  if (CanonicalVD->getDeclContext()->isTranslationUnit() &&
      !SemaRef.getCurLexicalContext()->isTranslationUnit()) {
    Diag(Id.getLoc(), diag::err_omp_var_scope)
        << getOpenMPDirectiveName(Kind) << VD;
    bool IsDecl =
        VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
    Diag(VD->getLocation(),
         IsDecl ? diag::note_previous_decl : diag::note_defined_here)
        << VD;
    return ExprError();
  }
  // OpenMP [2.9.2, Restrictions, C/C++, p.3]
  //   A threadprivate directive for static class member variables must appear
  //   in the class definition, in the same scope in which the member
  //   variables are declared.
  if (CanonicalVD->isStaticDataMember() &&
      !CanonicalVD->getDeclContext()->Equals(SemaRef.getCurLexicalContext())) {
    Diag(Id.getLoc(), diag::err_omp_var_scope)
        << getOpenMPDirectiveName(Kind) << VD;
    bool IsDecl =
        VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
    Diag(VD->getLocation(),
         IsDecl ? diag::note_previous_decl : diag::note_defined_here)
        << VD;
    return ExprError();
  }
  // OpenMP [2.9.2, Restrictions, C/C++, p.4]
  //   A threadprivate directive for namespace-scope variables must appear
  //   outside any definition or declaration other than the namespace
  //   definition itself.
  if (CanonicalVD->getDeclContext()->isNamespace() &&
      (!SemaRef.getCurLexicalContext()->isFileContext() ||
       !SemaRef.getCurLexicalContext()->Encloses(
           CanonicalVD->getDeclContext()))) {
    Diag(Id.getLoc(), diag::err_omp_var_scope)
        << getOpenMPDirectiveName(Kind) << VD;
    bool IsDecl =
        VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
    Diag(VD->getLocation(),
         IsDecl ? diag::note_previous_decl : diag::note_defined_here)
        << VD;
    return ExprError();
  }
  // OpenMP [2.9.2, Restrictions, C/C++, p.6]
  //   A threadprivate directive for static block-scope variables must appear
  //   in the scope of the variable and not in a nested scope.
  if (CanonicalVD->isLocalVarDecl() && CurScope &&
      !SemaRef.isDeclInScope(ND, SemaRef.getCurLexicalContext(), CurScope)) {
    Diag(Id.getLoc(), diag::err_omp_var_scope)
        << getOpenMPDirectiveName(Kind) << VD;
    bool IsDecl =
        VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
    Diag(VD->getLocation(),
         IsDecl ? diag::note_previous_decl : diag::note_defined_here)
        << VD;
    return ExprError();
  }

  // OpenMP [2.9.2, Restrictions, C/C++, p.2-6]
  //   A threadprivate directive must lexically precede all references to any
  //   of the variables in its list.
  if (Kind == OMPD_threadprivate && VD->isUsed() &&
      !DSAStack->isThreadPrivate(VD)) {
    Diag(Id.getLoc(), diag::err_omp_var_used)
        << getOpenMPDirectiveName(Kind) << VD;
    return ExprError();
  }

  QualType ExprType = VD->getType().getNonReferenceType();
  return DeclRefExpr::Create(Context, NestedNameSpecifierLoc(),
                             SourceLocation(), VD,
                             /*RefersToEnclosingVariableOrCapture=*/false,
                             Id.getLoc(), ExprType, VK_LValue);
}

SemaOpenMP::DeclGroupPtrTy
SemaOpenMP::ActOnOpenMPThreadprivateDirective(SourceLocation Loc,
                                              ArrayRef<Expr *> VarList) {
  if (OMPThreadPrivateDecl *D = CheckOMPThreadPrivateDecl(Loc, VarList)) {
    SemaRef.CurContext->addDecl(D);
    return DeclGroupPtrTy::make(DeclGroupRef(D));
  }
  return nullptr;
}

namespace {
class LocalVarRefChecker final
    : public ConstStmtVisitor<LocalVarRefChecker, bool> {
  Sema &SemaRef;

public:
  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    if (const auto *VD = dyn_cast<VarDecl>(E->getDecl())) {
      if (VD->hasLocalStorage()) {
        SemaRef.Diag(E->getBeginLoc(),
                     diag::err_omp_local_var_in_threadprivate_init)
            << E->getSourceRange();
        SemaRef.Diag(VD->getLocation(), diag::note_defined_here)
            << VD << VD->getSourceRange();
        return true;
      }
    }
    return false;
  }
  bool VisitStmt(const Stmt *S) {
    for (const Stmt *Child : S->children()) {
      if (Child && Visit(Child))
        return true;
    }
    return false;
  }
  explicit LocalVarRefChecker(Sema &SemaRef) : SemaRef(SemaRef) {}
};
} // namespace

OMPThreadPrivateDecl *
SemaOpenMP::CheckOMPThreadPrivateDecl(SourceLocation Loc,
                                      ArrayRef<Expr *> VarList) {
  ASTContext &Context = getASTContext();
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    auto *DE = cast<DeclRefExpr>(RefExpr);
    auto *VD = cast<VarDecl>(DE->getDecl());
    SourceLocation ILoc = DE->getExprLoc();

    // Mark variable as used.
    VD->setReferenced();
    VD->markUsed(Context);

    QualType QType = VD->getType();
    if (QType->isDependentType() || QType->isInstantiationDependentType()) {
      // It will be analyzed later.
      Vars.push_back(DE);
      continue;
    }

    // OpenMP [2.9.2, Restrictions, C/C++, p.10]
    //   A threadprivate variable must not have an incomplete type.
    if (SemaRef.RequireCompleteType(
            ILoc, VD->getType(), diag::err_omp_threadprivate_incomplete_type)) {
      continue;
    }

    // OpenMP [2.9.2, Restrictions, C/C++, p.10]
    //   A threadprivate variable must not have a reference type.
    if (VD->getType()->isReferenceType()) {
      Diag(ILoc, diag::err_omp_ref_type_arg)
          << getOpenMPDirectiveName(OMPD_threadprivate) << VD->getType();
      bool IsDecl =
          VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
      Diag(VD->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << VD;
      continue;
    }

    // Check if this is a TLS variable. If TLS is not being supported, produce
    // the corresponding diagnostic.
    if ((VD->getTLSKind() != VarDecl::TLS_None &&
         !(VD->hasAttr<OMPThreadPrivateDeclAttr>() &&
           getLangOpts().OpenMPUseTLS &&
           getASTContext().getTargetInfo().isTLSSupported())) ||
        (VD->getStorageClass() == SC_Register && VD->hasAttr<AsmLabelAttr>() &&
         !VD->isLocalVarDecl())) {
      Diag(ILoc, diag::err_omp_var_thread_local)
          << VD << ((VD->getTLSKind() != VarDecl::TLS_None) ? 0 : 1);
      bool IsDecl =
          VD->isThisDeclarationADefinition(Context) == VarDecl::DeclarationOnly;
      Diag(VD->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << VD;
      continue;
    }

    // Check if initial value of threadprivate variable reference variable with
    // local storage (it is not supported by runtime).
    if (const Expr *Init = VD->getAnyInitializer()) {
      LocalVarRefChecker Checker(SemaRef);
      if (Checker.Visit(Init))
        continue;
    }

    Vars.push_back(RefExpr);
    DSAStack->addDSA(VD, DE, OMPC_threadprivate);
    VD->addAttr(OMPThreadPrivateDeclAttr::CreateImplicit(
        Context, SourceRange(Loc, Loc)));
    if (ASTMutationListener *ML = Context.getASTMutationListener())
      ML->DeclarationMarkedOpenMPThreadPrivate(VD);
  }
  OMPThreadPrivateDecl *D = nullptr;
  if (!Vars.empty()) {
    D = OMPThreadPrivateDecl::Create(Context, SemaRef.getCurLexicalContext(),
                                     Loc, Vars);
    D->setAccess(AS_public);
  }
  return D;
}

static OMPAllocateDeclAttr::AllocatorTypeTy
getAllocatorKind(Sema &S, DSAStackTy *Stack, Expr *Allocator) {
  if (!Allocator)
    return OMPAllocateDeclAttr::OMPNullMemAlloc;
  if (Allocator->isTypeDependent() || Allocator->isValueDependent() ||
      Allocator->isInstantiationDependent() ||
      Allocator->containsUnexpandedParameterPack())
    return OMPAllocateDeclAttr::OMPUserDefinedMemAlloc;
  auto AllocatorKindRes = OMPAllocateDeclAttr::OMPUserDefinedMemAlloc;
  llvm::FoldingSetNodeID AEId;
  const Expr *AE = Allocator->IgnoreParenImpCasts();
  AE->IgnoreImpCasts()->Profile(AEId, S.getASTContext(), /*Canonical=*/true);
  for (int I = 0; I < OMPAllocateDeclAttr::OMPUserDefinedMemAlloc; ++I) {
    auto AllocatorKind = static_cast<OMPAllocateDeclAttr::AllocatorTypeTy>(I);
    const Expr *DefAllocator = Stack->getAllocator(AllocatorKind);
    llvm::FoldingSetNodeID DAEId;
    DefAllocator->IgnoreImpCasts()->Profile(DAEId, S.getASTContext(),
                                            /*Canonical=*/true);
    if (AEId == DAEId) {
      AllocatorKindRes = AllocatorKind;
      break;
    }
  }
  return AllocatorKindRes;
}

static bool checkPreviousOMPAllocateAttribute(
    Sema &S, DSAStackTy *Stack, Expr *RefExpr, VarDecl *VD,
    OMPAllocateDeclAttr::AllocatorTypeTy AllocatorKind, Expr *Allocator) {
  if (!VD->hasAttr<OMPAllocateDeclAttr>())
    return false;
  const auto *A = VD->getAttr<OMPAllocateDeclAttr>();
  Expr *PrevAllocator = A->getAllocator();
  OMPAllocateDeclAttr::AllocatorTypeTy PrevAllocatorKind =
      getAllocatorKind(S, Stack, PrevAllocator);
  bool AllocatorsMatch = AllocatorKind == PrevAllocatorKind;
  if (AllocatorsMatch &&
      AllocatorKind == OMPAllocateDeclAttr::OMPUserDefinedMemAlloc &&
      Allocator && PrevAllocator) {
    const Expr *AE = Allocator->IgnoreParenImpCasts();
    const Expr *PAE = PrevAllocator->IgnoreParenImpCasts();
    llvm::FoldingSetNodeID AEId, PAEId;
    AE->Profile(AEId, S.Context, /*Canonical=*/true);
    PAE->Profile(PAEId, S.Context, /*Canonical=*/true);
    AllocatorsMatch = AEId == PAEId;
  }
  if (!AllocatorsMatch) {
    SmallString<256> AllocatorBuffer;
    llvm::raw_svector_ostream AllocatorStream(AllocatorBuffer);
    if (Allocator)
      Allocator->printPretty(AllocatorStream, nullptr, S.getPrintingPolicy());
    SmallString<256> PrevAllocatorBuffer;
    llvm::raw_svector_ostream PrevAllocatorStream(PrevAllocatorBuffer);
    if (PrevAllocator)
      PrevAllocator->printPretty(PrevAllocatorStream, nullptr,
                                 S.getPrintingPolicy());

    SourceLocation AllocatorLoc =
        Allocator ? Allocator->getExprLoc() : RefExpr->getExprLoc();
    SourceRange AllocatorRange =
        Allocator ? Allocator->getSourceRange() : RefExpr->getSourceRange();
    SourceLocation PrevAllocatorLoc =
        PrevAllocator ? PrevAllocator->getExprLoc() : A->getLocation();
    SourceRange PrevAllocatorRange =
        PrevAllocator ? PrevAllocator->getSourceRange() : A->getRange();
    S.Diag(AllocatorLoc, diag::warn_omp_used_different_allocator)
        << (Allocator ? 1 : 0) << AllocatorStream.str()
        << (PrevAllocator ? 1 : 0) << PrevAllocatorStream.str()
        << AllocatorRange;
    S.Diag(PrevAllocatorLoc, diag::note_omp_previous_allocator)
        << PrevAllocatorRange;
    return true;
  }
  return false;
}

static void
applyOMPAllocateAttribute(Sema &S, VarDecl *VD,
                          OMPAllocateDeclAttr::AllocatorTypeTy AllocatorKind,
                          Expr *Allocator, Expr *Alignment, SourceRange SR) {
  if (VD->hasAttr<OMPAllocateDeclAttr>())
    return;
  if (Alignment &&
      (Alignment->isTypeDependent() || Alignment->isValueDependent() ||
       Alignment->isInstantiationDependent() ||
       Alignment->containsUnexpandedParameterPack()))
    // Apply later when we have a usable value.
    return;
  if (Allocator &&
      (Allocator->isTypeDependent() || Allocator->isValueDependent() ||
       Allocator->isInstantiationDependent() ||
       Allocator->containsUnexpandedParameterPack()))
    return;
  auto *A = OMPAllocateDeclAttr::CreateImplicit(S.Context, AllocatorKind,
                                                Allocator, Alignment, SR);
  VD->addAttr(A);
  if (ASTMutationListener *ML = S.Context.getASTMutationListener())
    ML->DeclarationMarkedOpenMPAllocate(VD, A);
}

SemaOpenMP::DeclGroupPtrTy SemaOpenMP::ActOnOpenMPAllocateDirective(
    SourceLocation Loc, ArrayRef<Expr *> VarList, ArrayRef<OMPClause *> Clauses,
    DeclContext *Owner) {
  assert(Clauses.size() <= 2 && "Expected at most two clauses.");
  Expr *Alignment = nullptr;
  Expr *Allocator = nullptr;
  if (Clauses.empty()) {
    // OpenMP 5.0, 2.11.3 allocate Directive, Restrictions.
    // allocate directives that appear in a target region must specify an
    // allocator clause unless a requires directive with the dynamic_allocators
    // clause is present in the same compilation unit.
    if (getLangOpts().OpenMPIsTargetDevice &&
        !DSAStack->hasRequiresDeclWithClause<OMPDynamicAllocatorsClause>())
      SemaRef.targetDiag(Loc, diag::err_expected_allocator_clause);
  } else {
    for (const OMPClause *C : Clauses)
      if (const auto *AC = dyn_cast<OMPAllocatorClause>(C))
        Allocator = AC->getAllocator();
      else if (const auto *AC = dyn_cast<OMPAlignClause>(C))
        Alignment = AC->getAlignment();
      else
        llvm_unreachable("Unexpected clause on allocate directive");
  }
  OMPAllocateDeclAttr::AllocatorTypeTy AllocatorKind =
      getAllocatorKind(SemaRef, DSAStack, Allocator);
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    auto *DE = cast<DeclRefExpr>(RefExpr);
    auto *VD = cast<VarDecl>(DE->getDecl());

    // Check if this is a TLS variable or global register.
    if (VD->getTLSKind() != VarDecl::TLS_None ||
        VD->hasAttr<OMPThreadPrivateDeclAttr>() ||
        (VD->getStorageClass() == SC_Register && VD->hasAttr<AsmLabelAttr>() &&
         !VD->isLocalVarDecl()))
      continue;

    // If the used several times in the allocate directive, the same allocator
    // must be used.
    if (checkPreviousOMPAllocateAttribute(SemaRef, DSAStack, RefExpr, VD,
                                          AllocatorKind, Allocator))
      continue;

    // OpenMP, 2.11.3 allocate Directive, Restrictions, C / C++
    // If a list item has a static storage type, the allocator expression in the
    // allocator clause must be a constant expression that evaluates to one of
    // the predefined memory allocator values.
    if (Allocator && VD->hasGlobalStorage()) {
      if (AllocatorKind == OMPAllocateDeclAttr::OMPUserDefinedMemAlloc) {
        Diag(Allocator->getExprLoc(),
             diag::err_omp_expected_predefined_allocator)
            << Allocator->getSourceRange();
        bool IsDecl = VD->isThisDeclarationADefinition(getASTContext()) ==
                      VarDecl::DeclarationOnly;
        Diag(VD->getLocation(),
             IsDecl ? diag::note_previous_decl : diag::note_defined_here)
            << VD;
        continue;
      }
    }

    Vars.push_back(RefExpr);
    applyOMPAllocateAttribute(SemaRef, VD, AllocatorKind, Allocator, Alignment,
                              DE->getSourceRange());
  }
  if (Vars.empty())
    return nullptr;
  if (!Owner)
    Owner = SemaRef.getCurLexicalContext();
  auto *D = OMPAllocateDecl::Create(getASTContext(), Owner, Loc, Vars, Clauses);
  D->setAccess(AS_public);
  Owner->addDecl(D);
  return DeclGroupPtrTy::make(DeclGroupRef(D));
}

SemaOpenMP::DeclGroupPtrTy
SemaOpenMP::ActOnOpenMPRequiresDirective(SourceLocation Loc,
                                         ArrayRef<OMPClause *> ClauseList) {
  OMPRequiresDecl *D = nullptr;
  if (!SemaRef.CurContext->isFileContext()) {
    Diag(Loc, diag::err_omp_invalid_scope) << "requires";
  } else {
    D = CheckOMPRequiresDecl(Loc, ClauseList);
    if (D) {
      SemaRef.CurContext->addDecl(D);
      DSAStack->addRequiresDecl(D);
    }
  }
  return DeclGroupPtrTy::make(DeclGroupRef(D));
}

void SemaOpenMP::ActOnOpenMPAssumesDirective(SourceLocation Loc,
                                             OpenMPDirectiveKind DKind,
                                             ArrayRef<std::string> Assumptions,
                                             bool SkippedClauses) {
  if (!SkippedClauses && Assumptions.empty())
    Diag(Loc, diag::err_omp_no_clause_for_directive)
        << llvm::omp::getAllAssumeClauseOptions()
        << llvm::omp::getOpenMPDirectiveName(DKind);

  auto *AA =
      OMPAssumeAttr::Create(getASTContext(), llvm::join(Assumptions, ","), Loc);
  if (DKind == llvm::omp::Directive::OMPD_begin_assumes) {
    OMPAssumeScoped.push_back(AA);
    return;
  }

  // Global assumes without assumption clauses are ignored.
  if (Assumptions.empty())
    return;

  assert(DKind == llvm::omp::Directive::OMPD_assumes &&
         "Unexpected omp assumption directive!");
  OMPAssumeGlobal.push_back(AA);

  // The OMPAssumeGlobal scope above will take care of new declarations but
  // we also want to apply the assumption to existing ones, e.g., to
  // declarations in included headers. To this end, we traverse all existing
  // declaration contexts and annotate function declarations here.
  SmallVector<DeclContext *, 8> DeclContexts;
  auto *Ctx = SemaRef.CurContext;
  while (Ctx->getLexicalParent())
    Ctx = Ctx->getLexicalParent();
  DeclContexts.push_back(Ctx);
  while (!DeclContexts.empty()) {
    DeclContext *DC = DeclContexts.pop_back_val();
    for (auto *SubDC : DC->decls()) {
      if (SubDC->isInvalidDecl())
        continue;
      if (auto *CTD = dyn_cast<ClassTemplateDecl>(SubDC)) {
        DeclContexts.push_back(CTD->getTemplatedDecl());
        llvm::append_range(DeclContexts, CTD->specializations());
        continue;
      }
      if (auto *DC = dyn_cast<DeclContext>(SubDC))
        DeclContexts.push_back(DC);
      if (auto *F = dyn_cast<FunctionDecl>(SubDC)) {
        F->addAttr(AA);
        continue;
      }
    }
  }
}

void SemaOpenMP::ActOnOpenMPEndAssumesDirective() {
  assert(isInOpenMPAssumeScope() && "Not in OpenMP assumes scope!");
  OMPAssumeScoped.pop_back();
}

OMPRequiresDecl *
SemaOpenMP::CheckOMPRequiresDecl(SourceLocation Loc,
                                 ArrayRef<OMPClause *> ClauseList) {
  /// For target specific clauses, the requires directive cannot be
  /// specified after the handling of any of the target regions in the
  /// current compilation unit.
  ArrayRef<SourceLocation> TargetLocations =
      DSAStack->getEncounteredTargetLocs();
  SourceLocation AtomicLoc = DSAStack->getAtomicDirectiveLoc();
  if (!TargetLocations.empty() || !AtomicLoc.isInvalid()) {
    for (const OMPClause *CNew : ClauseList) {
      // Check if any of the requires clauses affect target regions.
      if (isa<OMPUnifiedSharedMemoryClause>(CNew) ||
          isa<OMPUnifiedAddressClause>(CNew) ||
          isa<OMPReverseOffloadClause>(CNew) ||
          isa<OMPDynamicAllocatorsClause>(CNew)) {
        Diag(Loc, diag::err_omp_directive_before_requires)
            << "target" << getOpenMPClauseName(CNew->getClauseKind());
        for (SourceLocation TargetLoc : TargetLocations) {
          Diag(TargetLoc, diag::note_omp_requires_encountered_directive)
              << "target";
        }
      } else if (!AtomicLoc.isInvalid() &&
                 isa<OMPAtomicDefaultMemOrderClause>(CNew)) {
        Diag(Loc, diag::err_omp_directive_before_requires)
            << "atomic" << getOpenMPClauseName(CNew->getClauseKind());
        Diag(AtomicLoc, diag::note_omp_requires_encountered_directive)
            << "atomic";
      }
    }
  }

  if (!DSAStack->hasDuplicateRequiresClause(ClauseList))
    return OMPRequiresDecl::Create(
        getASTContext(), SemaRef.getCurLexicalContext(), Loc, ClauseList);
  return nullptr;
}

static void reportOriginalDsa(Sema &SemaRef, const DSAStackTy *Stack,
                              const ValueDecl *D,
                              const DSAStackTy::DSAVarData &DVar,
                              bool IsLoopIterVar) {
  if (DVar.RefExpr) {
    SemaRef.Diag(DVar.RefExpr->getExprLoc(), diag::note_omp_explicit_dsa)
        << getOpenMPClauseName(DVar.CKind);
    return;
  }
  enum {
    PDSA_StaticMemberShared,
    PDSA_StaticLocalVarShared,
    PDSA_LoopIterVarPrivate,
    PDSA_LoopIterVarLinear,
    PDSA_LoopIterVarLastprivate,
    PDSA_ConstVarShared,
    PDSA_GlobalVarShared,
    PDSA_TaskVarFirstprivate,
    PDSA_LocalVarPrivate,
    PDSA_Implicit
  } Reason = PDSA_Implicit;
  bool ReportHint = false;
  auto ReportLoc = D->getLocation();
  auto *VD = dyn_cast<VarDecl>(D);
  if (IsLoopIterVar) {
    if (DVar.CKind == OMPC_private)
      Reason = PDSA_LoopIterVarPrivate;
    else if (DVar.CKind == OMPC_lastprivate)
      Reason = PDSA_LoopIterVarLastprivate;
    else
      Reason = PDSA_LoopIterVarLinear;
  } else if (isOpenMPTaskingDirective(DVar.DKind) &&
             DVar.CKind == OMPC_firstprivate) {
    Reason = PDSA_TaskVarFirstprivate;
    ReportLoc = DVar.ImplicitDSALoc;
  } else if (VD && VD->isStaticLocal())
    Reason = PDSA_StaticLocalVarShared;
  else if (VD && VD->isStaticDataMember())
    Reason = PDSA_StaticMemberShared;
  else if (VD && VD->isFileVarDecl())
    Reason = PDSA_GlobalVarShared;
  else if (D->getType().isConstant(SemaRef.getASTContext()))
    Reason = PDSA_ConstVarShared;
  else if (VD && VD->isLocalVarDecl() && DVar.CKind == OMPC_private) {
    ReportHint = true;
    Reason = PDSA_LocalVarPrivate;
  }
  if (Reason != PDSA_Implicit) {
    SemaRef.Diag(ReportLoc, diag::note_omp_predetermined_dsa)
        << Reason << ReportHint
        << getOpenMPDirectiveName(Stack->getCurrentDirective());
  } else if (DVar.ImplicitDSALoc.isValid()) {
    SemaRef.Diag(DVar.ImplicitDSALoc, diag::note_omp_implicit_dsa)
        << getOpenMPClauseName(DVar.CKind);
  }
}

static OpenMPMapClauseKind
getMapClauseKindFromModifier(OpenMPDefaultmapClauseModifier M,
                             bool IsAggregateOrDeclareTarget) {
  OpenMPMapClauseKind Kind = OMPC_MAP_unknown;
  switch (M) {
  case OMPC_DEFAULTMAP_MODIFIER_alloc:
    Kind = OMPC_MAP_alloc;
    break;
  case OMPC_DEFAULTMAP_MODIFIER_to:
    Kind = OMPC_MAP_to;
    break;
  case OMPC_DEFAULTMAP_MODIFIER_from:
    Kind = OMPC_MAP_from;
    break;
  case OMPC_DEFAULTMAP_MODIFIER_tofrom:
    Kind = OMPC_MAP_tofrom;
    break;
  case OMPC_DEFAULTMAP_MODIFIER_present:
    // OpenMP 5.1 [2.21.7.3] defaultmap clause, Description]
    // If implicit-behavior is present, each variable referenced in the
    // construct in the category specified by variable-category is treated as if
    // it had been listed in a map clause with the map-type of alloc and
    // map-type-modifier of present.
    Kind = OMPC_MAP_alloc;
    break;
  case OMPC_DEFAULTMAP_MODIFIER_firstprivate:
  case OMPC_DEFAULTMAP_MODIFIER_last:
    llvm_unreachable("Unexpected defaultmap implicit behavior");
  case OMPC_DEFAULTMAP_MODIFIER_none:
  case OMPC_DEFAULTMAP_MODIFIER_default:
  case OMPC_DEFAULTMAP_MODIFIER_unknown:
    // IsAggregateOrDeclareTarget could be true if:
    // 1. the implicit behavior for aggregate is tofrom
    // 2. it's a declare target link
    if (IsAggregateOrDeclareTarget) {
      Kind = OMPC_MAP_tofrom;
      break;
    }
    llvm_unreachable("Unexpected defaultmap implicit behavior");
  }
  assert(Kind != OMPC_MAP_unknown && "Expect map kind to be known");
  return Kind;
}

namespace {
class DSAAttrChecker final : public StmtVisitor<DSAAttrChecker, void> {
  DSAStackTy *Stack;
  Sema &SemaRef;
  bool ErrorFound = false;
  bool TryCaptureCXXThisMembers = false;
  CapturedStmt *CS = nullptr;
  const static unsigned DefaultmapKindNum = OMPC_DEFAULTMAP_unknown + 1;
  llvm::SmallVector<Expr *, 4> ImplicitFirstprivate;
  llvm::SmallVector<Expr *, 4> ImplicitPrivate;
  llvm::SmallVector<Expr *, 4> ImplicitMap[DefaultmapKindNum][OMPC_MAP_delete];
  llvm::SmallVector<OpenMPMapModifierKind, NumberOfOMPMapClauseModifiers>
      ImplicitMapModifier[DefaultmapKindNum];
  SemaOpenMP::VarsWithInheritedDSAType VarsWithInheritedDSA;
  llvm::SmallDenseSet<const ValueDecl *, 4> ImplicitDeclarations;

  void VisitSubCaptures(OMPExecutableDirective *S) {
    // Check implicitly captured variables.
    if (!S->hasAssociatedStmt() || !S->getAssociatedStmt())
      return;
    if (S->getDirectiveKind() == OMPD_atomic ||
        S->getDirectiveKind() == OMPD_critical ||
        S->getDirectiveKind() == OMPD_section ||
        S->getDirectiveKind() == OMPD_master ||
        S->getDirectiveKind() == OMPD_masked ||
        S->getDirectiveKind() == OMPD_scope ||
        isOpenMPLoopTransformationDirective(S->getDirectiveKind())) {
      Visit(S->getAssociatedStmt());
      return;
    }
    visitSubCaptures(S->getInnermostCapturedStmt());
    // Try to capture inner this->member references to generate correct mappings
    // and diagnostics.
    if (TryCaptureCXXThisMembers ||
        (isOpenMPTargetExecutionDirective(Stack->getCurrentDirective()) &&
         llvm::any_of(S->getInnermostCapturedStmt()->captures(),
                      [](const CapturedStmt::Capture &C) {
                        return C.capturesThis();
                      }))) {
      bool SavedTryCaptureCXXThisMembers = TryCaptureCXXThisMembers;
      TryCaptureCXXThisMembers = true;
      Visit(S->getInnermostCapturedStmt()->getCapturedStmt());
      TryCaptureCXXThisMembers = SavedTryCaptureCXXThisMembers;
    }
    // In tasks firstprivates are not captured anymore, need to analyze them
    // explicitly.
    if (isOpenMPTaskingDirective(S->getDirectiveKind()) &&
        !isOpenMPTaskLoopDirective(S->getDirectiveKind())) {
      for (OMPClause *C : S->clauses())
        if (auto *FC = dyn_cast<OMPFirstprivateClause>(C)) {
          for (Expr *Ref : FC->varlists())
            Visit(Ref);
        }
    }
  }

public:
  void VisitDeclRefExpr(DeclRefExpr *E) {
    if (TryCaptureCXXThisMembers || E->isTypeDependent() ||
        E->isValueDependent() || E->containsUnexpandedParameterPack() ||
        E->isInstantiationDependent() ||
        E->isNonOdrUse() == clang::NOUR_Unevaluated)
      return;
    if (auto *VD = dyn_cast<VarDecl>(E->getDecl())) {
      // Check the datasharing rules for the expressions in the clauses.
      if (!CS || (isa<OMPCapturedExprDecl>(VD) && !CS->capturesVariable(VD) &&
                  !Stack->getTopDSA(VD, /*FromParent=*/false).RefExpr &&
                  !Stack->isImplicitDefaultFirstprivateFD(VD))) {
        if (auto *CED = dyn_cast<OMPCapturedExprDecl>(VD))
          if (!CED->hasAttr<OMPCaptureNoInitAttr>()) {
            Visit(CED->getInit());
            return;
          }
      } else if (VD->isImplicit() || isa<OMPCapturedExprDecl>(VD))
        // Do not analyze internal variables and do not enclose them into
        // implicit clauses.
        if (!Stack->isImplicitDefaultFirstprivateFD(VD))
          return;
      VD = VD->getCanonicalDecl();
      // Skip internally declared variables.
      if (VD->hasLocalStorage() && CS && !CS->capturesVariable(VD) &&
          !Stack->isImplicitDefaultFirstprivateFD(VD) &&
          !Stack->isImplicitTaskFirstprivate(VD))
        return;
      // Skip allocators in uses_allocators clauses.
      if (Stack->isUsesAllocatorsDecl(VD))
        return;

      DSAStackTy::DSAVarData DVar = Stack->getTopDSA(VD, /*FromParent=*/false);
      // Check if the variable has explicit DSA set and stop analysis if it so.
      if (DVar.RefExpr || !ImplicitDeclarations.insert(VD).second)
        return;

      // Skip internally declared static variables.
      std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
          OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
      if (VD->hasGlobalStorage() && CS && !CS->capturesVariable(VD) &&
          (Stack->hasRequiresDeclWithClause<OMPUnifiedSharedMemoryClause>() ||
           !Res || *Res != OMPDeclareTargetDeclAttr::MT_Link) &&
          !Stack->isImplicitDefaultFirstprivateFD(VD) &&
          !Stack->isImplicitTaskFirstprivate(VD))
        return;

      SourceLocation ELoc = E->getExprLoc();
      OpenMPDirectiveKind DKind = Stack->getCurrentDirective();
      // The default(none) clause requires that each variable that is referenced
      // in the construct, and does not have a predetermined data-sharing
      // attribute, must have its data-sharing attribute explicitly determined
      // by being listed in a data-sharing attribute clause.
      if (DVar.CKind == OMPC_unknown &&
          (Stack->getDefaultDSA() == DSA_none ||
           Stack->getDefaultDSA() == DSA_private ||
           Stack->getDefaultDSA() == DSA_firstprivate) &&
          isImplicitOrExplicitTaskingRegion(DKind) &&
          VarsWithInheritedDSA.count(VD) == 0) {
        bool InheritedDSA = Stack->getDefaultDSA() == DSA_none;
        if (!InheritedDSA && (Stack->getDefaultDSA() == DSA_firstprivate ||
                              Stack->getDefaultDSA() == DSA_private)) {
          DSAStackTy::DSAVarData DVar =
              Stack->getImplicitDSA(VD, /*FromParent=*/false);
          InheritedDSA = DVar.CKind == OMPC_unknown;
        }
        if (InheritedDSA)
          VarsWithInheritedDSA[VD] = E;
        if (Stack->getDefaultDSA() == DSA_none)
          return;
      }

      // OpenMP 5.0 [2.19.7.2, defaultmap clause, Description]
      // If implicit-behavior is none, each variable referenced in the
      // construct that does not have a predetermined data-sharing attribute
      // and does not appear in a to or link clause on a declare target
      // directive must be listed in a data-mapping attribute clause, a
      // data-sharing attribute clause (including a data-sharing attribute
      // clause on a combined construct where target. is one of the
      // constituent constructs), or an is_device_ptr clause.
      OpenMPDefaultmapClauseKind ClauseKind =
          getVariableCategoryFromDecl(SemaRef.getLangOpts(), VD);
      if (SemaRef.getLangOpts().OpenMP >= 50) {
        bool IsModifierNone = Stack->getDefaultmapModifier(ClauseKind) ==
                              OMPC_DEFAULTMAP_MODIFIER_none;
        if (DVar.CKind == OMPC_unknown && IsModifierNone &&
            VarsWithInheritedDSA.count(VD) == 0 && !Res) {
          // Only check for data-mapping attribute and is_device_ptr here
          // since we have already make sure that the declaration does not
          // have a data-sharing attribute above
          if (!Stack->checkMappableExprComponentListsForDecl(
                  VD, /*CurrentRegionOnly=*/true,
                  [VD](OMPClauseMappableExprCommon::MappableExprComponentListRef
                           MapExprComponents,
                       OpenMPClauseKind) {
                    auto MI = MapExprComponents.rbegin();
                    auto ME = MapExprComponents.rend();
                    return MI != ME && MI->getAssociatedDeclaration() == VD;
                  })) {
            VarsWithInheritedDSA[VD] = E;
            return;
          }
        }
      }
      if (SemaRef.getLangOpts().OpenMP > 50) {
        bool IsModifierPresent = Stack->getDefaultmapModifier(ClauseKind) ==
                                 OMPC_DEFAULTMAP_MODIFIER_present;
        if (IsModifierPresent) {
          if (!llvm::is_contained(ImplicitMapModifier[ClauseKind],
                                  OMPC_MAP_MODIFIER_present)) {
            ImplicitMapModifier[ClauseKind].push_back(
                OMPC_MAP_MODIFIER_present);
          }
        }
      }

      if (isOpenMPTargetExecutionDirective(DKind) &&
          !Stack->isLoopControlVariable(VD).first) {
        if (!Stack->checkMappableExprComponentListsForDecl(
                VD, /*CurrentRegionOnly=*/true,
                [this](OMPClauseMappableExprCommon::MappableExprComponentListRef
                           StackComponents,
                       OpenMPClauseKind) {
                  if (SemaRef.LangOpts.OpenMP >= 50)
                    return !StackComponents.empty();
                  // Variable is used if it has been marked as an array, array
                  // section, array shaping or the variable itself.
                  return StackComponents.size() == 1 ||
                         llvm::all_of(
                             llvm::drop_begin(llvm::reverse(StackComponents)),
                             [](const OMPClauseMappableExprCommon::
                                    MappableComponent &MC) {
                               return MC.getAssociatedDeclaration() ==
                                          nullptr &&
                                      (isa<ArraySectionExpr>(
                                           MC.getAssociatedExpression()) ||
                                       isa<OMPArrayShapingExpr>(
                                           MC.getAssociatedExpression()) ||
                                       isa<ArraySubscriptExpr>(
                                           MC.getAssociatedExpression()));
                             });
                })) {
          bool IsFirstprivate = false;
          // By default lambdas are captured as firstprivates.
          if (const auto *RD =
                  VD->getType().getNonReferenceType()->getAsCXXRecordDecl())
            IsFirstprivate = RD->isLambda();
          IsFirstprivate =
              IsFirstprivate || (Stack->mustBeFirstprivate(ClauseKind) && !Res);
          if (IsFirstprivate) {
            ImplicitFirstprivate.emplace_back(E);
          } else {
            OpenMPDefaultmapClauseModifier M =
                Stack->getDefaultmapModifier(ClauseKind);
            OpenMPMapClauseKind Kind = getMapClauseKindFromModifier(
                M, ClauseKind == OMPC_DEFAULTMAP_aggregate || Res);
            ImplicitMap[ClauseKind][Kind].emplace_back(E);
          }
          return;
        }
      }

      // OpenMP [2.9.3.6, Restrictions, p.2]
      //  A list item that appears in a reduction clause of the innermost
      //  enclosing worksharing or parallel construct may not be accessed in an
      //  explicit task.
      DVar = Stack->hasInnermostDSA(
          VD,
          [](OpenMPClauseKind C, bool AppliedToPointee) {
            return C == OMPC_reduction && !AppliedToPointee;
          },
          [](OpenMPDirectiveKind K) {
            return isOpenMPParallelDirective(K) ||
                   isOpenMPWorksharingDirective(K) || isOpenMPTeamsDirective(K);
          },
          /*FromParent=*/true);
      if (isOpenMPTaskingDirective(DKind) && DVar.CKind == OMPC_reduction) {
        ErrorFound = true;
        SemaRef.Diag(ELoc, diag::err_omp_reduction_in_task);
        reportOriginalDsa(SemaRef, Stack, VD, DVar);
        return;
      }

      // Define implicit data-sharing attributes for task.
      DVar = Stack->getImplicitDSA(VD, /*FromParent=*/false);
      if (((isOpenMPTaskingDirective(DKind) && DVar.CKind != OMPC_shared) ||
           (((Stack->getDefaultDSA() == DSA_firstprivate &&
              DVar.CKind == OMPC_firstprivate) ||
             (Stack->getDefaultDSA() == DSA_private &&
              DVar.CKind == OMPC_private)) &&
            !DVar.RefExpr)) &&
          !Stack->isLoopControlVariable(VD).first) {
        if (Stack->getDefaultDSA() == DSA_private)
          ImplicitPrivate.push_back(E);
        else
          ImplicitFirstprivate.push_back(E);
        return;
      }

      // Store implicitly used globals with declare target link for parent
      // target.
      if (!isOpenMPTargetExecutionDirective(DKind) && Res &&
          *Res == OMPDeclareTargetDeclAttr::MT_Link) {
        Stack->addToParentTargetRegionLinkGlobals(E);
        return;
      }
    }
  }
  void VisitMemberExpr(MemberExpr *E) {
    if (E->isTypeDependent() || E->isValueDependent() ||
        E->containsUnexpandedParameterPack() || E->isInstantiationDependent())
      return;
    auto *FD = dyn_cast<FieldDecl>(E->getMemberDecl());
    OpenMPDirectiveKind DKind = Stack->getCurrentDirective();
    if (auto *TE = dyn_cast<CXXThisExpr>(E->getBase()->IgnoreParenCasts())) {
      if (!FD)
        return;
      DSAStackTy::DSAVarData DVar = Stack->getTopDSA(FD, /*FromParent=*/false);
      // Check if the variable has explicit DSA set and stop analysis if it
      // so.
      if (DVar.RefExpr || !ImplicitDeclarations.insert(FD).second)
        return;

      if (isOpenMPTargetExecutionDirective(DKind) &&
          !Stack->isLoopControlVariable(FD).first &&
          !Stack->checkMappableExprComponentListsForDecl(
              FD, /*CurrentRegionOnly=*/true,
              [](OMPClauseMappableExprCommon::MappableExprComponentListRef
                     StackComponents,
                 OpenMPClauseKind) {
                return isa<CXXThisExpr>(
                    cast<MemberExpr>(
                        StackComponents.back().getAssociatedExpression())
                        ->getBase()
                        ->IgnoreParens());
              })) {
        // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C/C++, p.3]
        //  A bit-field cannot appear in a map clause.
        //
        if (FD->isBitField())
          return;

        // Check to see if the member expression is referencing a class that
        // has already been explicitly mapped
        if (Stack->isClassPreviouslyMapped(TE->getType()))
          return;

        OpenMPDefaultmapClauseModifier Modifier =
            Stack->getDefaultmapModifier(OMPC_DEFAULTMAP_aggregate);
        OpenMPDefaultmapClauseKind ClauseKind =
            getVariableCategoryFromDecl(SemaRef.getLangOpts(), FD);
        OpenMPMapClauseKind Kind = getMapClauseKindFromModifier(
            Modifier, /*IsAggregateOrDeclareTarget*/ true);
        ImplicitMap[ClauseKind][Kind].emplace_back(E);
        return;
      }

      SourceLocation ELoc = E->getExprLoc();
      // OpenMP [2.9.3.6, Restrictions, p.2]
      //  A list item that appears in a reduction clause of the innermost
      //  enclosing worksharing or parallel construct may not be accessed in
      //  an  explicit task.
      DVar = Stack->hasInnermostDSA(
          FD,
          [](OpenMPClauseKind C, bool AppliedToPointee) {
            return C == OMPC_reduction && !AppliedToPointee;
          },
          [](OpenMPDirectiveKind K) {
            return isOpenMPParallelDirective(K) ||
                   isOpenMPWorksharingDirective(K) || isOpenMPTeamsDirective(K);
          },
          /*FromParent=*/true);
      if (isOpenMPTaskingDirective(DKind) && DVar.CKind == OMPC_reduction) {
        ErrorFound = true;
        SemaRef.Diag(ELoc, diag::err_omp_reduction_in_task);
        reportOriginalDsa(SemaRef, Stack, FD, DVar);
        return;
      }

      // Define implicit data-sharing attributes for task.
      DVar = Stack->getImplicitDSA(FD, /*FromParent=*/false);
      if (isOpenMPTaskingDirective(DKind) && DVar.CKind != OMPC_shared &&
          !Stack->isLoopControlVariable(FD).first) {
        // Check if there is a captured expression for the current field in the
        // region. Do not mark it as firstprivate unless there is no captured
        // expression.
        // TODO: try to make it firstprivate.
        if (DVar.CKind != OMPC_unknown)
          ImplicitFirstprivate.push_back(E);
      }
      return;
    }
    if (isOpenMPTargetExecutionDirective(DKind)) {
      OMPClauseMappableExprCommon::MappableExprComponentList CurComponents;
      if (!checkMapClauseExpressionBase(SemaRef, E, CurComponents, OMPC_map,
                                        Stack->getCurrentDirective(),
                                        /*NoDiagnose=*/true))
        return;
      const auto *VD = cast<ValueDecl>(
          CurComponents.back().getAssociatedDeclaration()->getCanonicalDecl());
      if (!Stack->checkMappableExprComponentListsForDecl(
              VD, /*CurrentRegionOnly=*/true,
              [&CurComponents](
                  OMPClauseMappableExprCommon::MappableExprComponentListRef
                      StackComponents,
                  OpenMPClauseKind) {
                auto CCI = CurComponents.rbegin();
                auto CCE = CurComponents.rend();
                for (const auto &SC : llvm::reverse(StackComponents)) {
                  // Do both expressions have the same kind?
                  if (CCI->getAssociatedExpression()->getStmtClass() !=
                      SC.getAssociatedExpression()->getStmtClass())
                    if (!((isa<ArraySectionExpr>(
                               SC.getAssociatedExpression()) ||
                           isa<OMPArrayShapingExpr>(
                               SC.getAssociatedExpression())) &&
                          isa<ArraySubscriptExpr>(
                              CCI->getAssociatedExpression())))
                      return false;

                  const Decl *CCD = CCI->getAssociatedDeclaration();
                  const Decl *SCD = SC.getAssociatedDeclaration();
                  CCD = CCD ? CCD->getCanonicalDecl() : nullptr;
                  SCD = SCD ? SCD->getCanonicalDecl() : nullptr;
                  if (SCD != CCD)
                    return false;
                  std::advance(CCI, 1);
                  if (CCI == CCE)
                    break;
                }
                return true;
              })) {
        Visit(E->getBase());
      }
    } else if (!TryCaptureCXXThisMembers) {
      Visit(E->getBase());
    }
  }
  void VisitOMPExecutableDirective(OMPExecutableDirective *S) {
    for (OMPClause *C : S->clauses()) {
      // Skip analysis of arguments of private clauses for task|target
      // directives.
      if (isa_and_nonnull<OMPPrivateClause>(C))
        continue;
      // Skip analysis of arguments of implicitly defined firstprivate clause
      // for task|target directives.
      // Skip analysis of arguments of implicitly defined map clause for target
      // directives.
      if (C && !((isa<OMPFirstprivateClause>(C) || isa<OMPMapClause>(C)) &&
                 C->isImplicit() &&
                 !isOpenMPTaskingDirective(Stack->getCurrentDirective()))) {
        for (Stmt *CC : C->children()) {
          if (CC)
            Visit(CC);
        }
      }
    }
    // Check implicitly captured variables.
    VisitSubCaptures(S);
  }

  void VisitOMPLoopTransformationDirective(OMPLoopTransformationDirective *S) {
    // Loop transformation directives do not introduce data sharing
    VisitStmt(S);
  }

  void VisitCallExpr(CallExpr *S) {
    for (Stmt *C : S->arguments()) {
      if (C) {
        // Check implicitly captured variables in the task-based directives to
        // check if they must be firstprivatized.
        Visit(C);
      }
    }
    if (Expr *Callee = S->getCallee()) {
      auto *CI = Callee->IgnoreParenImpCasts();
      if (auto *CE = dyn_cast<MemberExpr>(CI))
        Visit(CE->getBase());
      else if (auto *CE = dyn_cast<DeclRefExpr>(CI))
        Visit(CE);
    }
  }
  void VisitStmt(Stmt *S) {
    for (Stmt *C : S->children()) {
      if (C) {
        // Check implicitly captured variables in the task-based directives to
        // check if they must be firstprivatized.
        Visit(C);
      }
    }
  }

  void visitSubCaptures(CapturedStmt *S) {
    for (const CapturedStmt::Capture &Cap : S->captures()) {
      if (!Cap.capturesVariable() && !Cap.capturesVariableByCopy())
        continue;
      VarDecl *VD = Cap.getCapturedVar();
      // Do not try to map the variable if it or its sub-component was mapped
      // already.
      if (isOpenMPTargetExecutionDirective(Stack->getCurrentDirective()) &&
          Stack->checkMappableExprComponentListsForDecl(
              VD, /*CurrentRegionOnly=*/true,
              [](OMPClauseMappableExprCommon::MappableExprComponentListRef,
                 OpenMPClauseKind) { return true; }))
        continue;
      DeclRefExpr *DRE = buildDeclRefExpr(
          SemaRef, VD, VD->getType().getNonLValueExprType(SemaRef.Context),
          Cap.getLocation(), /*RefersToCapture=*/true);
      Visit(DRE);
    }
  }
  bool isErrorFound() const { return ErrorFound; }
  ArrayRef<Expr *> getImplicitFirstprivate() const {
    return ImplicitFirstprivate;
  }
  ArrayRef<Expr *> getImplicitPrivate() const { return ImplicitPrivate; }
  ArrayRef<Expr *> getImplicitMap(OpenMPDefaultmapClauseKind DK,
                                  OpenMPMapClauseKind MK) const {
    return ImplicitMap[DK][MK];
  }
  ArrayRef<OpenMPMapModifierKind>
  getImplicitMapModifier(OpenMPDefaultmapClauseKind Kind) const {
    return ImplicitMapModifier[Kind];
  }
  const SemaOpenMP::VarsWithInheritedDSAType &getVarsWithInheritedDSA() const {
    return VarsWithInheritedDSA;
  }

  DSAAttrChecker(DSAStackTy *S, Sema &SemaRef, CapturedStmt *CS)
      : Stack(S), SemaRef(SemaRef), ErrorFound(false), CS(CS) {
    // Process declare target link variables for the target directives.
    if (isOpenMPTargetExecutionDirective(S->getCurrentDirective())) {
      for (DeclRefExpr *E : Stack->getLinkGlobals())
        Visit(E);
    }
  }
};
} // namespace

static void handleDeclareVariantConstructTrait(DSAStackTy *Stack,
                                               OpenMPDirectiveKind DKind,
                                               bool ScopeEntry) {
  SmallVector<llvm::omp::TraitProperty, 8> Traits;
  if (isOpenMPTargetExecutionDirective(DKind))
    Traits.emplace_back(llvm::omp::TraitProperty::construct_target_target);
  if (isOpenMPTeamsDirective(DKind))
    Traits.emplace_back(llvm::omp::TraitProperty::construct_teams_teams);
  if (isOpenMPParallelDirective(DKind))
    Traits.emplace_back(llvm::omp::TraitProperty::construct_parallel_parallel);
  if (isOpenMPWorksharingDirective(DKind))
    Traits.emplace_back(llvm::omp::TraitProperty::construct_for_for);
  if (isOpenMPSimdDirective(DKind))
    Traits.emplace_back(llvm::omp::TraitProperty::construct_simd_simd);
  Stack->handleConstructTrait(Traits, ScopeEntry);
}

static SmallVector<SemaOpenMP::CapturedParamNameType>
getParallelRegionParams(Sema &SemaRef, bool LoopBoundSharing) {
  ASTContext &Context = SemaRef.getASTContext();
  QualType KmpInt32Ty =
      Context.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1).withConst();
  QualType KmpInt32PtrTy =
      Context.getPointerType(KmpInt32Ty).withConst().withRestrict();
  SmallVector<SemaOpenMP::CapturedParamNameType> Params{
      std::make_pair(".global_tid.", KmpInt32PtrTy),
      std::make_pair(".bound_tid.", KmpInt32PtrTy),
  };
  if (LoopBoundSharing) {
    QualType KmpSizeTy = Context.getSizeType().withConst();
    Params.push_back(std::make_pair(".previous.lb.", KmpSizeTy));
    Params.push_back(std::make_pair(".previous.ub.", KmpSizeTy));
  }

  // __context with shared vars
  Params.push_back(std::make_pair(StringRef(), QualType()));
  return Params;
}

static SmallVector<SemaOpenMP::CapturedParamNameType>
getTeamsRegionParams(Sema &SemaRef) {
  return getParallelRegionParams(SemaRef, /*LoopBoundSharing=*/false);
}

static SmallVector<SemaOpenMP::CapturedParamNameType>
getTaskRegionParams(Sema &SemaRef) {
  ASTContext &Context = SemaRef.getASTContext();
  QualType KmpInt32Ty = Context.getIntTypeForBitwidth(32, 1).withConst();
  QualType VoidPtrTy = Context.VoidPtrTy.withConst().withRestrict();
  QualType KmpInt32PtrTy =
      Context.getPointerType(KmpInt32Ty).withConst().withRestrict();
  QualType Args[] = {VoidPtrTy};
  FunctionProtoType::ExtProtoInfo EPI;
  EPI.Variadic = true;
  QualType CopyFnType = Context.getFunctionType(Context.VoidTy, Args, EPI);
  SmallVector<SemaOpenMP::CapturedParamNameType> Params{
      std::make_pair(".global_tid.", KmpInt32Ty),
      std::make_pair(".part_id.", KmpInt32PtrTy),
      std::make_pair(".privates.", VoidPtrTy),
      std::make_pair(
          ".copy_fn.",
          Context.getPointerType(CopyFnType).withConst().withRestrict()),
      std::make_pair(".task_t.", Context.VoidPtrTy.withConst()),
      std::make_pair(StringRef(), QualType()) // __context with shared vars
  };
  return Params;
}

static SmallVector<SemaOpenMP::CapturedParamNameType>
getTargetRegionParams(Sema &SemaRef) {
  ASTContext &Context = SemaRef.getASTContext();
  SmallVector<SemaOpenMP::CapturedParamNameType> Params;
  if (SemaRef.getLangOpts().OpenMPIsTargetDevice) {
    QualType VoidPtrTy = Context.VoidPtrTy.withConst().withRestrict();
    Params.push_back(std::make_pair(StringRef("dyn_ptr"), VoidPtrTy));
  }
  // __context with shared vars
  Params.push_back(std::make_pair(StringRef(), QualType()));
  return Params;
}

static SmallVector<SemaOpenMP::CapturedParamNameType>
getUnknownRegionParams(Sema &SemaRef) {
  SmallVector<SemaOpenMP::CapturedParamNameType> Params{
      std::make_pair(StringRef(), QualType()) // __context with shared vars
  };
  return Params;
}

static SmallVector<SemaOpenMP::CapturedParamNameType>
getTaskloopRegionParams(Sema &SemaRef) {
  ASTContext &Context = SemaRef.getASTContext();
  QualType KmpInt32Ty =
      Context.getIntTypeForBitwidth(/*DestWidth=*/32, /*Signed=*/1).withConst();
  QualType KmpUInt64Ty =
      Context.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/0).withConst();
  QualType KmpInt64Ty =
      Context.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1).withConst();
  QualType VoidPtrTy = Context.VoidPtrTy.withConst().withRestrict();
  QualType KmpInt32PtrTy =
      Context.getPointerType(KmpInt32Ty).withConst().withRestrict();
  QualType Args[] = {VoidPtrTy};
  FunctionProtoType::ExtProtoInfo EPI;
  EPI.Variadic = true;
  QualType CopyFnType = Context.getFunctionType(Context.VoidTy, Args, EPI);
  SmallVector<SemaOpenMP::CapturedParamNameType> Params{
      std::make_pair(".global_tid.", KmpInt32Ty),
      std::make_pair(".part_id.", KmpInt32PtrTy),
      std::make_pair(".privates.", VoidPtrTy),
      std::make_pair(
          ".copy_fn.",
          Context.getPointerType(CopyFnType).withConst().withRestrict()),
      std::make_pair(".task_t.", Context.VoidPtrTy.withConst()),
      std::make_pair(".lb.", KmpUInt64Ty),
      std::make_pair(".ub.", KmpUInt64Ty),
      std::make_pair(".st.", KmpInt64Ty),
      std::make_pair(".liter.", KmpInt32Ty),
      std::make_pair(".reductions.", VoidPtrTy),
      std::make_pair(StringRef(), QualType()) // __context with shared vars
  };
  return Params;
}

static void processCapturedRegions(Sema &SemaRef, OpenMPDirectiveKind DKind,
                                   Scope *CurScope, SourceLocation Loc) {
  SmallVector<OpenMPDirectiveKind> Regions;
  getOpenMPCaptureRegions(Regions, DKind);

  bool LoopBoundSharing = isOpenMPLoopBoundSharingDirective(DKind);

  auto MarkAsInlined = [&](CapturedRegionScopeInfo *CSI) {
    CSI->TheCapturedDecl->addAttr(AlwaysInlineAttr::CreateImplicit(
        SemaRef.getASTContext(), {}, AlwaysInlineAttr::Keyword_forceinline));
  };

  for (auto [Level, RKind] : llvm::enumerate(Regions)) {
    switch (RKind) {
    // All region kinds that can be returned from `getOpenMPCaptureRegions`
    // are listed here.
    case OMPD_parallel:
      SemaRef.ActOnCapturedRegionStart(
          Loc, CurScope, CR_OpenMP,
          getParallelRegionParams(SemaRef, LoopBoundSharing), Level);
      break;
    case OMPD_teams:
      SemaRef.ActOnCapturedRegionStart(Loc, CurScope, CR_OpenMP,
                                       getTeamsRegionParams(SemaRef), Level);
      break;
    case OMPD_task:
      SemaRef.ActOnCapturedRegionStart(Loc, CurScope, CR_OpenMP,
                                       getTaskRegionParams(SemaRef), Level);
      // Mark this captured region as inlined, because we don't use outlined
      // function directly.
      MarkAsInlined(SemaRef.getCurCapturedRegion());
      break;
    case OMPD_taskloop:
      SemaRef.ActOnCapturedRegionStart(Loc, CurScope, CR_OpenMP,
                                       getTaskloopRegionParams(SemaRef), Level);
      // Mark this captured region as inlined, because we don't use outlined
      // function directly.
      MarkAsInlined(SemaRef.getCurCapturedRegion());
      break;
    case OMPD_target:
      SemaRef.ActOnCapturedRegionStart(Loc, CurScope, CR_OpenMP,
                                       getTargetRegionParams(SemaRef), Level);
      break;
    case OMPD_unknown:
      SemaRef.ActOnCapturedRegionStart(Loc, CurScope, CR_OpenMP,
                                       getUnknownRegionParams(SemaRef));
      break;
    case OMPD_metadirective:
    case OMPD_nothing:
    default:
      llvm_unreachable("Unexpected capture region");
    }
  }
}

void SemaOpenMP::ActOnOpenMPRegionStart(OpenMPDirectiveKind DKind,
                                        Scope *CurScope) {
  switch (DKind) {
  case OMPD_atomic:
  case OMPD_critical:
  case OMPD_masked:
  case OMPD_master:
  case OMPD_section:
  case OMPD_tile:
  case OMPD_unroll:
  case OMPD_reverse:
  case OMPD_interchange:
    break;
  default:
    processCapturedRegions(SemaRef, DKind, CurScope,
                           DSAStack->getConstructLoc());
    break;
  }

  DSAStack->setContext(SemaRef.CurContext);
  handleDeclareVariantConstructTrait(DSAStack, DKind, /*ScopeEntry=*/true);
}

int SemaOpenMP::getNumberOfConstructScopes(unsigned Level) const {
  return getOpenMPCaptureLevels(DSAStack->getDirective(Level));
}

int SemaOpenMP::getOpenMPCaptureLevels(OpenMPDirectiveKind DKind) {
  SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
  getOpenMPCaptureRegions(CaptureRegions, DKind);
  return CaptureRegions.size();
}

static OMPCapturedExprDecl *buildCaptureDecl(Sema &S, IdentifierInfo *Id,
                                             Expr *CaptureExpr, bool WithInit,
                                             DeclContext *CurContext,
                                             bool AsExpression) {
  assert(CaptureExpr);
  ASTContext &C = S.getASTContext();
  Expr *Init = AsExpression ? CaptureExpr : CaptureExpr->IgnoreImpCasts();
  QualType Ty = Init->getType();
  if (CaptureExpr->getObjectKind() == OK_Ordinary && CaptureExpr->isGLValue()) {
    if (S.getLangOpts().CPlusPlus) {
      Ty = C.getLValueReferenceType(Ty);
    } else {
      Ty = C.getPointerType(Ty);
      ExprResult Res =
          S.CreateBuiltinUnaryOp(CaptureExpr->getExprLoc(), UO_AddrOf, Init);
      if (!Res.isUsable())
        return nullptr;
      Init = Res.get();
    }
    WithInit = true;
  }
  auto *CED = OMPCapturedExprDecl::Create(C, CurContext, Id, Ty,
                                          CaptureExpr->getBeginLoc());
  if (!WithInit)
    CED->addAttr(OMPCaptureNoInitAttr::CreateImplicit(C));
  CurContext->addHiddenDecl(CED);
  Sema::TentativeAnalysisScope Trap(S);
  S.AddInitializerToDecl(CED, Init, /*DirectInit=*/false);
  return CED;
}

static DeclRefExpr *buildCapture(Sema &S, ValueDecl *D, Expr *CaptureExpr,
                                 bool WithInit) {
  OMPCapturedExprDecl *CD;
  if (VarDecl *VD = S.OpenMP().isOpenMPCapturedDecl(D))
    CD = cast<OMPCapturedExprDecl>(VD);
  else
    CD = buildCaptureDecl(S, D->getIdentifier(), CaptureExpr, WithInit,
                          S.CurContext,
                          /*AsExpression=*/false);
  return buildDeclRefExpr(S, CD, CD->getType().getNonReferenceType(),
                          CaptureExpr->getExprLoc());
}

static ExprResult buildCapture(Sema &S, Expr *CaptureExpr, DeclRefExpr *&Ref,
                               StringRef Name) {
  CaptureExpr = S.DefaultLvalueConversion(CaptureExpr).get();
  if (!Ref) {
    OMPCapturedExprDecl *CD = buildCaptureDecl(
        S, &S.getASTContext().Idents.get(Name), CaptureExpr,
        /*WithInit=*/true, S.CurContext, /*AsExpression=*/true);
    Ref = buildDeclRefExpr(S, CD, CD->getType().getNonReferenceType(),
                           CaptureExpr->getExprLoc());
  }
  ExprResult Res = Ref;
  if (!S.getLangOpts().CPlusPlus &&
      CaptureExpr->getObjectKind() == OK_Ordinary && CaptureExpr->isGLValue() &&
      Ref->getType()->isPointerType()) {
    Res = S.CreateBuiltinUnaryOp(CaptureExpr->getExprLoc(), UO_Deref, Ref);
    if (!Res.isUsable())
      return ExprError();
  }
  return S.DefaultLvalueConversion(Res.get());
}

namespace {
// OpenMP directives parsed in this section are represented as a
// CapturedStatement with an associated statement.  If a syntax error
// is detected during the parsing of the associated statement, the
// compiler must abort processing and close the CapturedStatement.
//
// Combined directives such as 'target parallel' have more than one
// nested CapturedStatements.  This RAII ensures that we unwind out
// of all the nested CapturedStatements when an error is found.
class CaptureRegionUnwinderRAII {
private:
  Sema &S;
  bool &ErrorFound;
  OpenMPDirectiveKind DKind = OMPD_unknown;

public:
  CaptureRegionUnwinderRAII(Sema &S, bool &ErrorFound,
                            OpenMPDirectiveKind DKind)
      : S(S), ErrorFound(ErrorFound), DKind(DKind) {}
  ~CaptureRegionUnwinderRAII() {
    if (ErrorFound) {
      int ThisCaptureLevel = S.OpenMP().getOpenMPCaptureLevels(DKind);
      while (--ThisCaptureLevel >= 0)
        S.ActOnCapturedRegionError();
    }
  }
};
} // namespace

void SemaOpenMP::tryCaptureOpenMPLambdas(ValueDecl *V) {
  // Capture variables captured by reference in lambdas for target-based
  // directives.
  if (!SemaRef.CurContext->isDependentContext() &&
      (isOpenMPTargetExecutionDirective(DSAStack->getCurrentDirective()) ||
       isOpenMPTargetDataManagementDirective(
           DSAStack->getCurrentDirective()))) {
    QualType Type = V->getType();
    if (const auto *RD = Type.getCanonicalType()
                             .getNonReferenceType()
                             ->getAsCXXRecordDecl()) {
      bool SavedForceCaptureByReferenceInTargetExecutable =
          DSAStack->isForceCaptureByReferenceInTargetExecutable();
      DSAStack->setForceCaptureByReferenceInTargetExecutable(
          /*V=*/true);
      if (RD->isLambda()) {
        llvm::DenseMap<const ValueDecl *, FieldDecl *> Captures;
        FieldDecl *ThisCapture;
        RD->getCaptureFields(Captures, ThisCapture);
        for (const LambdaCapture &LC : RD->captures()) {
          if (LC.getCaptureKind() == LCK_ByRef) {
            VarDecl *VD = cast<VarDecl>(LC.getCapturedVar());
            DeclContext *VDC = VD->getDeclContext();
            if (!VDC->Encloses(SemaRef.CurContext))
              continue;
            SemaRef.MarkVariableReferenced(LC.getLocation(), VD);
          } else if (LC.getCaptureKind() == LCK_This) {
            QualType ThisTy = SemaRef.getCurrentThisType();
            if (!ThisTy.isNull() && getASTContext().typesAreCompatible(
                                        ThisTy, ThisCapture->getType()))
              SemaRef.CheckCXXThisCapture(LC.getLocation());
          }
        }
      }
      DSAStack->setForceCaptureByReferenceInTargetExecutable(
          SavedForceCaptureByReferenceInTargetExecutable);
    }
  }
}

static bool checkOrderedOrderSpecified(Sema &S,
                                       const ArrayRef<OMPClause *> Clauses) {
  const OMPOrderedClause *Ordered = nullptr;
  const OMPOrderClause *Order = nullptr;

  for (const OMPClause *Clause : Clauses) {
    if (Clause->getClauseKind() == OMPC_ordered)
      Ordered = cast<OMPOrderedClause>(Clause);
    else if (Clause->getClauseKind() == OMPC_order) {
      Order = cast<OMPOrderClause>(Clause);
      if (Order->getKind() != OMPC_ORDER_concurrent)
        Order = nullptr;
    }
    if (Ordered && Order)
      break;
  }

  if (Ordered && Order) {
    S.Diag(Order->getKindKwLoc(),
           diag::err_omp_simple_clause_incompatible_with_ordered)
        << getOpenMPClauseName(OMPC_order)
        << getOpenMPSimpleClauseTypeName(OMPC_order, OMPC_ORDER_concurrent)
        << SourceRange(Order->getBeginLoc(), Order->getEndLoc());
    S.Diag(Ordered->getBeginLoc(), diag::note_omp_ordered_param)
        << 0 << SourceRange(Ordered->getBeginLoc(), Ordered->getEndLoc());
    return true;
  }
  return false;
}

StmtResult SemaOpenMP::ActOnOpenMPRegionEnd(StmtResult S,
                                            ArrayRef<OMPClause *> Clauses) {
  handleDeclareVariantConstructTrait(DSAStack, DSAStack->getCurrentDirective(),
                                     /* ScopeEntry */ false);
  if (!isOpenMPCapturingDirective(DSAStack->getCurrentDirective()))
    return S;

  bool ErrorFound = false;
  CaptureRegionUnwinderRAII CaptureRegionUnwinder(
      SemaRef, ErrorFound, DSAStack->getCurrentDirective());
  if (!S.isUsable()) {
    ErrorFound = true;
    return StmtError();
  }

  SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
  getOpenMPCaptureRegions(CaptureRegions, DSAStack->getCurrentDirective());
  OMPOrderedClause *OC = nullptr;
  OMPScheduleClause *SC = nullptr;
  SmallVector<const OMPLinearClause *, 4> LCs;
  SmallVector<const OMPClauseWithPreInit *, 4> PICs;
  // This is required for proper codegen.
  for (OMPClause *Clause : Clauses) {
    if (!getLangOpts().OpenMPSimd &&
        (isOpenMPTaskingDirective(DSAStack->getCurrentDirective()) ||
         DSAStack->getCurrentDirective() == OMPD_target) &&
        Clause->getClauseKind() == OMPC_in_reduction) {
      // Capture taskgroup task_reduction descriptors inside the tasking regions
      // with the corresponding in_reduction items.
      auto *IRC = cast<OMPInReductionClause>(Clause);
      for (Expr *E : IRC->taskgroup_descriptors())
        if (E)
          SemaRef.MarkDeclarationsReferencedInExpr(E);
    }
    if (isOpenMPPrivate(Clause->getClauseKind()) ||
        Clause->getClauseKind() == OMPC_copyprivate ||
        (getLangOpts().OpenMPUseTLS &&
         getASTContext().getTargetInfo().isTLSSupported() &&
         Clause->getClauseKind() == OMPC_copyin)) {
      DSAStack->setForceVarCapturing(Clause->getClauseKind() == OMPC_copyin);
      // Mark all variables in private list clauses as used in inner region.
      for (Stmt *VarRef : Clause->children()) {
        if (auto *E = cast_or_null<Expr>(VarRef)) {
          SemaRef.MarkDeclarationsReferencedInExpr(E);
        }
      }
      DSAStack->setForceVarCapturing(/*V=*/false);
    } else if (CaptureRegions.size() > 1 ||
               CaptureRegions.back() != OMPD_unknown) {
      if (auto *C = OMPClauseWithPreInit::get(Clause))
        PICs.push_back(C);
      if (auto *C = OMPClauseWithPostUpdate::get(Clause)) {
        if (Expr *E = C->getPostUpdateExpr())
          SemaRef.MarkDeclarationsReferencedInExpr(E);
      }
    }
    if (Clause->getClauseKind() == OMPC_schedule)
      SC = cast<OMPScheduleClause>(Clause);
    else if (Clause->getClauseKind() == OMPC_ordered)
      OC = cast<OMPOrderedClause>(Clause);
    else if (Clause->getClauseKind() == OMPC_linear)
      LCs.push_back(cast<OMPLinearClause>(Clause));
  }
  // Capture allocator expressions if used.
  for (Expr *E : DSAStack->getInnerAllocators())
    SemaRef.MarkDeclarationsReferencedInExpr(E);
  // OpenMP, 2.7.1 Loop Construct, Restrictions
  // The nonmonotonic modifier cannot be specified if an ordered clause is
  // specified.
  if (SC &&
      (SC->getFirstScheduleModifier() == OMPC_SCHEDULE_MODIFIER_nonmonotonic ||
       SC->getSecondScheduleModifier() ==
           OMPC_SCHEDULE_MODIFIER_nonmonotonic) &&
      OC) {
    Diag(SC->getFirstScheduleModifier() == OMPC_SCHEDULE_MODIFIER_nonmonotonic
             ? SC->getFirstScheduleModifierLoc()
             : SC->getSecondScheduleModifierLoc(),
         diag::err_omp_simple_clause_incompatible_with_ordered)
        << getOpenMPClauseName(OMPC_schedule)
        << getOpenMPSimpleClauseTypeName(OMPC_schedule,
                                         OMPC_SCHEDULE_MODIFIER_nonmonotonic)
        << SourceRange(OC->getBeginLoc(), OC->getEndLoc());
    ErrorFound = true;
  }
  // OpenMP 5.0, 2.9.2 Worksharing-Loop Construct, Restrictions.
  // If an order(concurrent) clause is present, an ordered clause may not appear
  // on the same directive.
  if (checkOrderedOrderSpecified(SemaRef, Clauses))
    ErrorFound = true;
  if (!LCs.empty() && OC && OC->getNumForLoops()) {
    for (const OMPLinearClause *C : LCs) {
      Diag(C->getBeginLoc(), diag::err_omp_linear_ordered)
          << SourceRange(OC->getBeginLoc(), OC->getEndLoc());
    }
    ErrorFound = true;
  }
  if (isOpenMPWorksharingDirective(DSAStack->getCurrentDirective()) &&
      isOpenMPSimdDirective(DSAStack->getCurrentDirective()) && OC &&
      OC->getNumForLoops()) {
    Diag(OC->getBeginLoc(), diag::err_omp_ordered_simd)
        << getOpenMPDirectiveName(DSAStack->getCurrentDirective());
    ErrorFound = true;
  }
  if (ErrorFound) {
    return StmtError();
  }
  StmtResult SR = S;
  unsigned CompletedRegions = 0;
  for (OpenMPDirectiveKind ThisCaptureRegion : llvm::reverse(CaptureRegions)) {
    // Mark all variables in private list clauses as used in inner region.
    // Required for proper codegen of combined directives.
    // TODO: add processing for other clauses.
    if (ThisCaptureRegion != OMPD_unknown) {
      for (const clang::OMPClauseWithPreInit *C : PICs) {
        OpenMPDirectiveKind CaptureRegion = C->getCaptureRegion();
        // Find the particular capture region for the clause if the
        // directive is a combined one with multiple capture regions.
        // If the directive is not a combined one, the capture region
        // associated with the clause is OMPD_unknown and is generated
        // only once.
        if (CaptureRegion == ThisCaptureRegion ||
            CaptureRegion == OMPD_unknown) {
          if (auto *DS = cast_or_null<DeclStmt>(C->getPreInitStmt())) {
            for (Decl *D : DS->decls())
              SemaRef.MarkVariableReferenced(D->getLocation(),
                                             cast<VarDecl>(D));
          }
        }
      }
    }
    if (ThisCaptureRegion == OMPD_target) {
      // Capture allocator traits in the target region. They are used implicitly
      // and, thus, are not captured by default.
      for (OMPClause *C : Clauses) {
        if (const auto *UAC = dyn_cast<OMPUsesAllocatorsClause>(C)) {
          for (unsigned I = 0, End = UAC->getNumberOfAllocators(); I < End;
               ++I) {
            OMPUsesAllocatorsClause::Data D = UAC->getAllocatorData(I);
            if (Expr *E = D.AllocatorTraits)
              SemaRef.MarkDeclarationsReferencedInExpr(E);
          }
          continue;
        }
      }
    }
    if (ThisCaptureRegion == OMPD_parallel) {
      // Capture temp arrays for inscan reductions and locals in aligned
      // clauses.
      for (OMPClause *C : Clauses) {
        if (auto *RC = dyn_cast<OMPReductionClause>(C)) {
          if (RC->getModifier() != OMPC_REDUCTION_inscan)
            continue;
          for (Expr *E : RC->copy_array_temps())
            if (E)
              SemaRef.MarkDeclarationsReferencedInExpr(E);
        }
        if (auto *AC = dyn_cast<OMPAlignedClause>(C)) {
          for (Expr *E : AC->varlists())
            SemaRef.MarkDeclarationsReferencedInExpr(E);
        }
      }
    }
    if (++CompletedRegions == CaptureRegions.size())
      DSAStack->setBodyComplete();
    SR = SemaRef.ActOnCapturedRegionEnd(SR.get());
  }
  return SR;
}

static bool checkCancelRegion(Sema &SemaRef, OpenMPDirectiveKind CurrentRegion,
                              OpenMPDirectiveKind CancelRegion,
                              SourceLocation StartLoc) {
  // CancelRegion is only needed for cancel and cancellation_point.
  if (CurrentRegion != OMPD_cancel && CurrentRegion != OMPD_cancellation_point)
    return false;

  if (CancelRegion == OMPD_parallel || CancelRegion == OMPD_for ||
      CancelRegion == OMPD_sections || CancelRegion == OMPD_taskgroup)
    return false;

  SemaRef.Diag(StartLoc, diag::err_omp_wrong_cancel_region)
      << getOpenMPDirectiveName(CancelRegion);
  return true;
}

static bool checkNestingOfRegions(Sema &SemaRef, const DSAStackTy *Stack,
                                  OpenMPDirectiveKind CurrentRegion,
                                  const DeclarationNameInfo &CurrentName,
                                  OpenMPDirectiveKind CancelRegion,
                                  OpenMPBindClauseKind BindKind,
                                  SourceLocation StartLoc) {
  if (!Stack->getCurScope())
    return false;

  OpenMPDirectiveKind ParentRegion = Stack->getParentDirective();
  OpenMPDirectiveKind OffendingRegion = ParentRegion;
  bool NestingProhibited = false;
  bool CloseNesting = true;
  bool OrphanSeen = false;
  enum {
    NoRecommend,
    ShouldBeInParallelRegion,
    ShouldBeInOrderedRegion,
    ShouldBeInTargetRegion,
    ShouldBeInTeamsRegion,
    ShouldBeInLoopSimdRegion,
  } Recommend = NoRecommend;

  SmallVector<OpenMPDirectiveKind, 4> LeafOrComposite;
  ArrayRef<OpenMPDirectiveKind> ParentLOC =
      getLeafOrCompositeConstructs(ParentRegion, LeafOrComposite);
  OpenMPDirectiveKind EnclosingConstruct = ParentLOC.back();

  if (SemaRef.LangOpts.OpenMP >= 51 && Stack->isParentOrderConcurrent() &&
      CurrentRegion != OMPD_simd && CurrentRegion != OMPD_loop &&
      CurrentRegion != OMPD_parallel &&
      !isOpenMPCombinedParallelADirective(CurrentRegion)) {
    SemaRef.Diag(StartLoc, diag::err_omp_prohibited_region_order)
        << getOpenMPDirectiveName(CurrentRegion);
    return true;
  }
  if (isOpenMPSimdDirective(ParentRegion) &&
      ((SemaRef.LangOpts.OpenMP <= 45 && CurrentRegion != OMPD_ordered) ||
       (SemaRef.LangOpts.OpenMP >= 50 && CurrentRegion != OMPD_ordered &&
        CurrentRegion != OMPD_simd && CurrentRegion != OMPD_atomic &&
        CurrentRegion != OMPD_scan))) {
    // OpenMP [2.16, Nesting of Regions]
    // OpenMP constructs may not be nested inside a simd region.
    // OpenMP [2.8.1,simd Construct, Restrictions]
    // An ordered construct with the simd clause is the only OpenMP
    // construct that can appear in the simd region.
    // Allowing a SIMD construct nested in another SIMD construct is an
    // extension. The OpenMP 4.5 spec does not allow it. Issue a warning
    // message.
    // OpenMP 5.0 [2.9.3.1, simd Construct, Restrictions]
    // The only OpenMP constructs that can be encountered during execution of
    // a simd region are the atomic construct, the loop construct, the simd
    // construct and the ordered construct with the simd clause.
    SemaRef.Diag(StartLoc, (CurrentRegion != OMPD_simd)
                               ? diag::err_omp_prohibited_region_simd
                               : diag::warn_omp_nesting_simd)
        << (SemaRef.LangOpts.OpenMP >= 50 ? 1 : 0);
    return CurrentRegion != OMPD_simd;
  }
  if (EnclosingConstruct == OMPD_atomic) {
    // OpenMP [2.16, Nesting of Regions]
    // OpenMP constructs may not be nested inside an atomic region.
    SemaRef.Diag(StartLoc, diag::err_omp_prohibited_region_atomic);
    return true;
  }
  if (CurrentRegion == OMPD_section) {
    // OpenMP [2.7.2, sections Construct, Restrictions]
    // Orphaned section directives are prohibited. That is, the section
    // directives must appear within the sections construct and must not be
    // encountered elsewhere in the sections region.
    if (EnclosingConstruct != OMPD_sections) {
      SemaRef.Diag(StartLoc, diag::err_omp_orphaned_section_directive)
          << (ParentRegion != OMPD_unknown)
          << getOpenMPDirectiveName(ParentRegion);
      return true;
    }
    return false;
  }
  // Allow some constructs (except teams and cancellation constructs) to be
  // orphaned (they could be used in functions, called from OpenMP regions
  // with the required preconditions).
  if (ParentRegion == OMPD_unknown &&
      !isOpenMPNestingTeamsDirective(CurrentRegion) &&
      CurrentRegion != OMPD_cancellation_point &&
      CurrentRegion != OMPD_cancel && CurrentRegion != OMPD_scan)
    return false;
  // Checks needed for mapping "loop" construct. Please check mapLoopConstruct
  // for a detailed explanation
  if (SemaRef.LangOpts.OpenMP >= 50 && CurrentRegion == OMPD_loop &&
      (BindKind == OMPC_BIND_parallel || BindKind == OMPC_BIND_teams) &&
      (isOpenMPWorksharingDirective(ParentRegion) ||
       EnclosingConstruct == OMPD_loop)) {
    int ErrorMsgNumber = (BindKind == OMPC_BIND_parallel) ? 1 : 4;
    SemaRef.Diag(StartLoc, diag::err_omp_prohibited_region)
        << true << getOpenMPDirectiveName(ParentRegion) << ErrorMsgNumber
        << getOpenMPDirectiveName(CurrentRegion);
    return true;
  }
  if (CurrentRegion == OMPD_cancellation_point ||
      CurrentRegion == OMPD_cancel) {
    // OpenMP [2.16, Nesting of Regions]
    // A cancellation point construct for which construct-type-clause is
    // taskgroup must be nested inside a task construct. A cancellation
    // point construct for which construct-type-clause is not taskgroup must
    // be closely nested inside an OpenMP construct that matches the type
    // specified in construct-type-clause.
    // A cancel construct for which construct-type-clause is taskgroup must be
    // nested inside a task construct. A cancel construct for which
    // construct-type-clause is not taskgroup must be closely nested inside an
    // OpenMP construct that matches the type specified in
    // construct-type-clause.
    ArrayRef<OpenMPDirectiveKind> Leafs = getLeafConstructsOrSelf(ParentRegion);
    if (CancelRegion == OMPD_taskgroup) {
      NestingProhibited = EnclosingConstruct != OMPD_task &&
                          (SemaRef.getLangOpts().OpenMP < 50 ||
                           EnclosingConstruct != OMPD_taskloop);
    } else if (CancelRegion == OMPD_sections) {
      NestingProhibited = EnclosingConstruct != OMPD_section &&
                          EnclosingConstruct != OMPD_sections;
    } else {
      NestingProhibited = CancelRegion != Leafs.back();
    }
    OrphanSeen = ParentRegion == OMPD_unknown;
  } else if (CurrentRegion == OMPD_master || CurrentRegion == OMPD_masked) {
    // OpenMP 5.1 [2.22, Nesting of Regions]
    // A masked region may not be closely nested inside a worksharing, loop,
    // atomic, task, or taskloop region.
    NestingProhibited = isOpenMPWorksharingDirective(ParentRegion) ||
                        isOpenMPGenericLoopDirective(ParentRegion) ||
                        isOpenMPTaskingDirective(ParentRegion);
  } else if (CurrentRegion == OMPD_critical && CurrentName.getName()) {
    // OpenMP [2.16, Nesting of Regions]
    // A critical region may not be nested (closely or otherwise) inside a
    // critical region with the same name. Note that this restriction is not
    // sufficient to prevent deadlock.
    SourceLocation PreviousCriticalLoc;
    bool DeadLock = Stack->hasDirective(
        [CurrentName, &PreviousCriticalLoc](OpenMPDirectiveKind K,
                                            const DeclarationNameInfo &DNI,
                                            SourceLocation Loc) {
          if (K == OMPD_critical && DNI.getName() == CurrentName.getName()) {
            PreviousCriticalLoc = Loc;
            return true;
          }
          return false;
        },
        false /* skip top directive */);
    if (DeadLock) {
      SemaRef.Diag(StartLoc, diag::err_omp_prohibited_region_critical_same_name)
          << CurrentName.getName();
      if (PreviousCriticalLoc.isValid())
        SemaRef.Diag(PreviousCriticalLoc,
                     diag::note_omp_previous_critical_region);
      return true;
    }
  } else if (CurrentRegion == OMPD_barrier || CurrentRegion == OMPD_scope) {
    // OpenMP 5.1 [2.22, Nesting of Regions]
    // A scope region may not be closely nested inside a worksharing, loop,
    // task, taskloop, critical, ordered, atomic, or masked region.
    // OpenMP 5.1 [2.22, Nesting of Regions]
    // A barrier region may not be closely nested inside a worksharing, loop,
    // task, taskloop, critical, ordered, atomic, or masked region.
    NestingProhibited = isOpenMPWorksharingDirective(ParentRegion) ||
                        isOpenMPGenericLoopDirective(ParentRegion) ||
                        isOpenMPTaskingDirective(ParentRegion) ||
                        llvm::is_contained({OMPD_masked, OMPD_master,
                                            OMPD_critical, OMPD_ordered},
                                           EnclosingConstruct);
  } else if (isOpenMPWorksharingDirective(CurrentRegion) &&
             !isOpenMPParallelDirective(CurrentRegion) &&
             !isOpenMPTeamsDirective(CurrentRegion)) {
    // OpenMP 5.1 [2.22, Nesting of Regions]
    // A loop region that binds to a parallel region or a worksharing region
    // may not be closely nested inside a worksharing, loop, task, taskloop,
    // critical, ordered, atomic, or masked region.
    NestingProhibited = isOpenMPWorksharingDirective(ParentRegion) ||
                        isOpenMPGenericLoopDirective(ParentRegion) ||
                        isOpenMPTaskingDirective(ParentRegion) ||
                        llvm::is_contained({OMPD_masked, OMPD_master,
                                            OMPD_critical, OMPD_ordered},
                                           EnclosingConstruct);
    Recommend = ShouldBeInParallelRegion;
  } else if (CurrentRegion == OMPD_ordered) {
    // OpenMP [2.16, Nesting of Regions]
    // An ordered region may not be closely nested inside a critical,
    // atomic, or explicit task region.
    // An ordered region must be closely nested inside a loop region (or
    // parallel loop region) with an ordered clause.
    // OpenMP [2.8.1,simd Construct, Restrictions]
    // An ordered construct with the simd clause is the only OpenMP construct
    // that can appear in the simd region.
    NestingProhibited = EnclosingConstruct == OMPD_critical ||
                        isOpenMPTaskingDirective(ParentRegion) ||
                        !(isOpenMPSimdDirective(ParentRegion) ||
                          Stack->isParentOrderedRegion());
    Recommend = ShouldBeInOrderedRegion;
  } else if (isOpenMPNestingTeamsDirective(CurrentRegion)) {
    // OpenMP [2.16, Nesting of Regions]
    // If specified, a teams construct must be contained within a target
    // construct.
    NestingProhibited =
        (SemaRef.LangOpts.OpenMP <= 45 && EnclosingConstruct != OMPD_target) ||
        (SemaRef.LangOpts.OpenMP >= 50 && EnclosingConstruct != OMPD_unknown &&
         EnclosingConstruct != OMPD_target);
    OrphanSeen = ParentRegion == OMPD_unknown;
    Recommend = ShouldBeInTargetRegion;
  } else if (CurrentRegion == OMPD_scan) {
    if (SemaRef.LangOpts.OpenMP >= 50) {
      // OpenMP spec 5.0 and 5.1 require scan to be directly enclosed by for,
      // simd, or for simd. This has to take into account combined directives.
      // In 5.2 this seems to be implied by the fact that the specified
      // separated constructs are do, for, and simd.
      NestingProhibited = !llvm::is_contained(
          {OMPD_for, OMPD_simd, OMPD_for_simd}, EnclosingConstruct);
    } else {
      NestingProhibited = true;
    }
    OrphanSeen = ParentRegion == OMPD_unknown;
    Recommend = ShouldBeInLoopSimdRegion;
  }
  if (!NestingProhibited && !isOpenMPTargetExecutionDirective(CurrentRegion) &&
      !isOpenMPTargetDataManagementDirective(CurrentRegion) &&
      EnclosingConstruct == OMPD_teams) {
    // OpenMP [5.1, 2.22, Nesting of Regions]
    // distribute, distribute simd, distribute parallel worksharing-loop,
    // distribute parallel worksharing-loop SIMD, loop, parallel regions,
    // including any parallel regions arising from combined constructs,
    // omp_get_num_teams() regions, and omp_get_team_num() regions are the
    // only OpenMP regions that may be strictly nested inside the teams
    // region.
    //
    // As an extension, we permit atomic within teams as well.
    NestingProhibited = !isOpenMPParallelDirective(CurrentRegion) &&
                        !isOpenMPDistributeDirective(CurrentRegion) &&
                        CurrentRegion != OMPD_loop &&
                        !(SemaRef.getLangOpts().OpenMPExtensions &&
                          CurrentRegion == OMPD_atomic);
    Recommend = ShouldBeInParallelRegion;
  }
  if (!NestingProhibited && CurrentRegion == OMPD_loop) {
    // OpenMP [5.1, 2.11.7, loop Construct, Restrictions]
    // If the bind clause is present on the loop construct and binding is
    // teams then the corresponding loop region must be strictly nested inside
    // a teams region.
    NestingProhibited =
        BindKind == OMPC_BIND_teams && EnclosingConstruct != OMPD_teams;
    Recommend = ShouldBeInTeamsRegion;
  }
  if (!NestingProhibited && isOpenMPNestingDistributeDirective(CurrentRegion)) {
    // OpenMP 4.5 [2.17 Nesting of Regions]
    // The region associated with the distribute construct must be strictly
    // nested inside a teams region
    NestingProhibited = EnclosingConstruct != OMPD_teams;
    Recommend = ShouldBeInTeamsRegion;
  }
  if (!NestingProhibited &&
      (isOpenMPTargetExecutionDirective(CurrentRegion) ||
       isOpenMPTargetDataManagementDirective(CurrentRegion))) {
    // OpenMP 4.5 [2.17 Nesting of Regions]
    // If a target, target update, target data, target enter data, or
    // target exit data construct is encountered during execution of a
    // target region, the behavior is unspecified.
    NestingProhibited = Stack->hasDirective(
        [&OffendingRegion](OpenMPDirectiveKind K, const DeclarationNameInfo &,
                           SourceLocation) {
          if (isOpenMPTargetExecutionDirective(K)) {
            OffendingRegion = K;
            return true;
          }
          return false;
        },
        false /* don't skip top directive */);
    CloseNesting = false;
  }
  if (NestingProhibited) {
    if (OrphanSeen) {
      SemaRef.Diag(StartLoc, diag::err_omp_orphaned_device_directive)
          << getOpenMPDirectiveName(CurrentRegion) << Recommend;
    } else {
      SemaRef.Diag(StartLoc, diag::err_omp_prohibited_region)
          << CloseNesting << getOpenMPDirectiveName(OffendingRegion)
          << Recommend << getOpenMPDirectiveName(CurrentRegion);
    }
    return true;
  }
  return false;
}

struct Kind2Unsigned {
  using argument_type = OpenMPDirectiveKind;
  unsigned operator()(argument_type DK) { return unsigned(DK); }
};
static bool checkIfClauses(Sema &S, OpenMPDirectiveKind Kind,
                           ArrayRef<OMPClause *> Clauses,
                           ArrayRef<OpenMPDirectiveKind> AllowedNameModifiers) {
  bool ErrorFound = false;
  unsigned NamedModifiersNumber = 0;
  llvm::IndexedMap<const OMPIfClause *, Kind2Unsigned> FoundNameModifiers;
  FoundNameModifiers.resize(llvm::omp::Directive_enumSize + 1);
  SmallVector<SourceLocation, 4> NameModifierLoc;
  for (const OMPClause *C : Clauses) {
    if (const auto *IC = dyn_cast_or_null<OMPIfClause>(C)) {
      // At most one if clause without a directive-name-modifier can appear on
      // the directive.
      OpenMPDirectiveKind CurNM = IC->getNameModifier();
      if (FoundNameModifiers[CurNM]) {
        S.Diag(C->getBeginLoc(), diag::err_omp_more_one_clause)
            << getOpenMPDirectiveName(Kind) << getOpenMPClauseName(OMPC_if)
            << (CurNM != OMPD_unknown) << getOpenMPDirectiveName(CurNM);
        ErrorFound = true;
      } else if (CurNM != OMPD_unknown) {
        NameModifierLoc.push_back(IC->getNameModifierLoc());
        ++NamedModifiersNumber;
      }
      FoundNameModifiers[CurNM] = IC;
      if (CurNM == OMPD_unknown)
        continue;
      // Check if the specified name modifier is allowed for the current
      // directive.
      // At most one if clause with the particular directive-name-modifier can
      // appear on the directive.
      if (!llvm::is_contained(AllowedNameModifiers, CurNM)) {
        S.Diag(IC->getNameModifierLoc(),
               diag::err_omp_wrong_if_directive_name_modifier)
            << getOpenMPDirectiveName(CurNM) << getOpenMPDirectiveName(Kind);
        ErrorFound = true;
      }
    }
  }
  // If any if clause on the directive includes a directive-name-modifier then
  // all if clauses on the directive must include a directive-name-modifier.
  if (FoundNameModifiers[OMPD_unknown] && NamedModifiersNumber > 0) {
    if (NamedModifiersNumber == AllowedNameModifiers.size()) {
      S.Diag(FoundNameModifiers[OMPD_unknown]->getBeginLoc(),
             diag::err_omp_no_more_if_clause);
    } else {
      std::string Values;
      std::string Sep(", ");
      unsigned AllowedCnt = 0;
      unsigned TotalAllowedNum =
          AllowedNameModifiers.size() - NamedModifiersNumber;
      for (unsigned Cnt = 0, End = AllowedNameModifiers.size(); Cnt < End;
           ++Cnt) {
        OpenMPDirectiveKind NM = AllowedNameModifiers[Cnt];
        if (!FoundNameModifiers[NM]) {
          Values += "'";
          Values += getOpenMPDirectiveName(NM);
          Values += "'";
          if (AllowedCnt + 2 == TotalAllowedNum)
            Values += " or ";
          else if (AllowedCnt + 1 != TotalAllowedNum)
            Values += Sep;
          ++AllowedCnt;
        }
      }
      S.Diag(FoundNameModifiers[OMPD_unknown]->getCondition()->getBeginLoc(),
             diag::err_omp_unnamed_if_clause)
          << (TotalAllowedNum > 1) << Values;
    }
    for (SourceLocation Loc : NameModifierLoc) {
      S.Diag(Loc, diag::note_omp_previous_named_if_clause);
    }
    ErrorFound = true;
  }
  return ErrorFound;
}

static std::pair<ValueDecl *, bool> getPrivateItem(Sema &S, Expr *&RefExpr,
                                                   SourceLocation &ELoc,
                                                   SourceRange &ERange,
                                                   bool AllowArraySection,
                                                   StringRef DiagType) {
  if (RefExpr->isTypeDependent() || RefExpr->isValueDependent() ||
      RefExpr->containsUnexpandedParameterPack())
    return std::make_pair(nullptr, true);

  // OpenMP [3.1, C/C++]
  //  A list item is a variable name.
  // OpenMP  [2.9.3.3, Restrictions, p.1]
  //  A variable that is part of another variable (as an array or
  //  structure element) cannot appear in a private clause.
  RefExpr = RefExpr->IgnoreParens();
  enum {
    NoArrayExpr = -1,
    ArraySubscript = 0,
    OMPArraySection = 1
  } IsArrayExpr = NoArrayExpr;
  if (AllowArraySection) {
    if (auto *ASE = dyn_cast_or_null<ArraySubscriptExpr>(RefExpr)) {
      Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
      while (auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
        Base = TempASE->getBase()->IgnoreParenImpCasts();
      RefExpr = Base;
      IsArrayExpr = ArraySubscript;
    } else if (auto *OASE = dyn_cast_or_null<ArraySectionExpr>(RefExpr)) {
      Expr *Base = OASE->getBase()->IgnoreParenImpCasts();
      while (auto *TempOASE = dyn_cast<ArraySectionExpr>(Base))
        Base = TempOASE->getBase()->IgnoreParenImpCasts();
      while (auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base))
        Base = TempASE->getBase()->IgnoreParenImpCasts();
      RefExpr = Base;
      IsArrayExpr = OMPArraySection;
    }
  }
  ELoc = RefExpr->getExprLoc();
  ERange = RefExpr->getSourceRange();
  RefExpr = RefExpr->IgnoreParenImpCasts();
  auto *DE = dyn_cast_or_null<DeclRefExpr>(RefExpr);
  auto *ME = dyn_cast_or_null<MemberExpr>(RefExpr);
  if ((!DE || !isa<VarDecl>(DE->getDecl())) &&
      (S.getCurrentThisType().isNull() || !ME ||
       !isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts()) ||
       !isa<FieldDecl>(ME->getMemberDecl()))) {
    if (IsArrayExpr != NoArrayExpr) {
      S.Diag(ELoc, diag::err_omp_expected_base_var_name)
          << IsArrayExpr << ERange;
    } else if (!DiagType.empty()) {
      unsigned DiagSelect = S.getLangOpts().CPlusPlus
                                ? (S.getCurrentThisType().isNull() ? 1 : 2)
                                : 0;
      S.Diag(ELoc, diag::err_omp_expected_var_name_member_expr_with_type)
          << DiagSelect << DiagType << ERange;
    } else {
      S.Diag(ELoc,
             AllowArraySection
                 ? diag::err_omp_expected_var_name_member_expr_or_array_item
                 : diag::err_omp_expected_var_name_member_expr)
          << (S.getCurrentThisType().isNull() ? 0 : 1) << ERange;
    }
    return std::make_pair(nullptr, false);
  }
  return std::make_pair(
      getCanonicalDecl(DE ? DE->getDecl() : ME->getMemberDecl()), false);
}

namespace {
/// Checks if the allocator is used in uses_allocators clause to be allowed in
/// target regions.
class AllocatorChecker final : public ConstStmtVisitor<AllocatorChecker, bool> {
  DSAStackTy *S = nullptr;

public:
  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    return S->isUsesAllocatorsDecl(E->getDecl())
               .value_or(DSAStackTy::UsesAllocatorsDeclKind::AllocatorTrait) ==
           DSAStackTy::UsesAllocatorsDeclKind::AllocatorTrait;
  }
  bool VisitStmt(const Stmt *S) {
    for (const Stmt *Child : S->children()) {
      if (Child && Visit(Child))
        return true;
    }
    return false;
  }
  explicit AllocatorChecker(DSAStackTy *S) : S(S) {}
};
} // namespace

static void checkAllocateClauses(Sema &S, DSAStackTy *Stack,
                                 ArrayRef<OMPClause *> Clauses) {
  assert(!S.CurContext->isDependentContext() &&
         "Expected non-dependent context.");
  auto AllocateRange =
      llvm::make_filter_range(Clauses, OMPAllocateClause::classof);
  llvm::DenseMap<CanonicalDeclPtr<Decl>, CanonicalDeclPtr<VarDecl>> DeclToCopy;
  auto PrivateRange = llvm::make_filter_range(Clauses, [](const OMPClause *C) {
    return isOpenMPPrivate(C->getClauseKind());
  });
  for (OMPClause *Cl : PrivateRange) {
    MutableArrayRef<Expr *>::iterator I, It, Et;
    if (Cl->getClauseKind() == OMPC_private) {
      auto *PC = cast<OMPPrivateClause>(Cl);
      I = PC->private_copies().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else if (Cl->getClauseKind() == OMPC_firstprivate) {
      auto *PC = cast<OMPFirstprivateClause>(Cl);
      I = PC->private_copies().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else if (Cl->getClauseKind() == OMPC_lastprivate) {
      auto *PC = cast<OMPLastprivateClause>(Cl);
      I = PC->private_copies().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else if (Cl->getClauseKind() == OMPC_linear) {
      auto *PC = cast<OMPLinearClause>(Cl);
      I = PC->privates().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else if (Cl->getClauseKind() == OMPC_reduction) {
      auto *PC = cast<OMPReductionClause>(Cl);
      I = PC->privates().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else if (Cl->getClauseKind() == OMPC_task_reduction) {
      auto *PC = cast<OMPTaskReductionClause>(Cl);
      I = PC->privates().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else if (Cl->getClauseKind() == OMPC_in_reduction) {
      auto *PC = cast<OMPInReductionClause>(Cl);
      I = PC->privates().begin();
      It = PC->varlist_begin();
      Et = PC->varlist_end();
    } else {
      llvm_unreachable("Expected private clause.");
    }
    for (Expr *E : llvm::make_range(It, Et)) {
      if (!*I) {
        ++I;
        continue;
      }
      SourceLocation ELoc;
      SourceRange ERange;
      Expr *SimpleRefExpr = E;
      auto Res = getPrivateItem(S, SimpleRefExpr, ELoc, ERange,
                                /*AllowArraySection=*/true);
      DeclToCopy.try_emplace(Res.first,
                             cast<VarDecl>(cast<DeclRefExpr>(*I)->getDecl()));
      ++I;
    }
  }
  for (OMPClause *C : AllocateRange) {
    auto *AC = cast<OMPAllocateClause>(C);
    if (S.getLangOpts().OpenMP >= 50 &&
        !Stack->hasRequiresDeclWithClause<OMPDynamicAllocatorsClause>() &&
        isOpenMPTargetExecutionDirective(Stack->getCurrentDirective()) &&
        AC->getAllocator()) {
      Expr *Allocator = AC->getAllocator();
      // OpenMP, 2.12.5 target Construct
      // Memory allocators that do not appear in a uses_allocators clause cannot
      // appear as an allocator in an allocate clause or be used in the target
      // region unless a requires directive with the dynamic_allocators clause
      // is present in the same compilation unit.
      AllocatorChecker Checker(Stack);
      if (Checker.Visit(Allocator))
        S.Diag(Allocator->getExprLoc(),
               diag::err_omp_allocator_not_in_uses_allocators)
            << Allocator->getSourceRange();
    }
    OMPAllocateDeclAttr::AllocatorTypeTy AllocatorKind =
        getAllocatorKind(S, Stack, AC->getAllocator());
    // OpenMP, 2.11.4 allocate Clause, Restrictions.
    // For task, taskloop or target directives, allocation requests to memory
    // allocators with the trait access set to thread result in unspecified
    // behavior.
    if (AllocatorKind == OMPAllocateDeclAttr::OMPThreadMemAlloc &&
        (isOpenMPTaskingDirective(Stack->getCurrentDirective()) ||
         isOpenMPTargetExecutionDirective(Stack->getCurrentDirective()))) {
      S.Diag(AC->getAllocator()->getExprLoc(),
             diag::warn_omp_allocate_thread_on_task_target_directive)
          << getOpenMPDirectiveName(Stack->getCurrentDirective());
    }
    for (Expr *E : AC->varlists()) {
      SourceLocation ELoc;
      SourceRange ERange;
      Expr *SimpleRefExpr = E;
      auto Res = getPrivateItem(S, SimpleRefExpr, ELoc, ERange);
      ValueDecl *VD = Res.first;
      DSAStackTy::DSAVarData Data = Stack->getTopDSA(VD, /*FromParent=*/false);
      if (!isOpenMPPrivate(Data.CKind)) {
        S.Diag(E->getExprLoc(),
               diag::err_omp_expected_private_copy_for_allocate);
        continue;
      }
      VarDecl *PrivateVD = DeclToCopy[VD];
      if (checkPreviousOMPAllocateAttribute(S, Stack, E, PrivateVD,
                                            AllocatorKind, AC->getAllocator()))
        continue;
      // Placeholder until allocate clause supports align modifier.
      Expr *Alignment = nullptr;
      applyOMPAllocateAttribute(S, PrivateVD, AllocatorKind, AC->getAllocator(),
                                Alignment, E->getSourceRange());
    }
  }
}

namespace {
/// Rewrite statements and expressions for Sema \p Actions CurContext.
///
/// Used to wrap already parsed statements/expressions into a new CapturedStmt
/// context. DeclRefExpr used inside the new context are changed to refer to the
/// captured variable instead.
class CaptureVars : public TreeTransform<CaptureVars> {
  using BaseTransform = TreeTransform<CaptureVars>;

public:
  CaptureVars(Sema &Actions) : BaseTransform(Actions) {}

  bool AlwaysRebuild() { return true; }
};
} // namespace

static VarDecl *precomputeExpr(Sema &Actions,
                               SmallVectorImpl<Stmt *> &BodyStmts, Expr *E,
                               StringRef Name) {
  Expr *NewE = AssertSuccess(CaptureVars(Actions).TransformExpr(E));
  VarDecl *NewVar = buildVarDecl(Actions, {}, NewE->getType(), Name, nullptr,
                                 dyn_cast<DeclRefExpr>(E->IgnoreImplicit()));
  auto *NewDeclStmt = cast<DeclStmt>(AssertSuccess(
      Actions.ActOnDeclStmt(Actions.ConvertDeclToDeclGroup(NewVar), {}, {})));
  Actions.AddInitializerToDecl(NewDeclStmt->getSingleDecl(), NewE, false);
  BodyStmts.push_back(NewDeclStmt);
  return NewVar;
}

/// Create a closure that computes the number of iterations of a loop.
///
/// \param Actions   The Sema object.
/// \param LogicalTy Type for the logical iteration number.
/// \param Rel       Comparison operator of the loop condition.
/// \param StartExpr Value of the loop counter at the first iteration.
/// \param StopExpr  Expression the loop counter is compared against in the loop
/// condition. \param StepExpr      Amount of increment after each iteration.
///
/// \return Closure (CapturedStmt) of the distance calculation.
static CapturedStmt *buildDistanceFunc(Sema &Actions, QualType LogicalTy,
                                       BinaryOperator::Opcode Rel,
                                       Expr *StartExpr, Expr *StopExpr,
                                       Expr *StepExpr) {
  ASTContext &Ctx = Actions.getASTContext();
  TypeSourceInfo *LogicalTSI = Ctx.getTrivialTypeSourceInfo(LogicalTy);

  // Captured regions currently don't support return values, we use an
  // out-parameter instead. All inputs are implicit captures.
  // TODO: Instead of capturing each DeclRefExpr occurring in
  // StartExpr/StopExpr/Step, these could also be passed as a value capture.
  QualType ResultTy = Ctx.getLValueReferenceType(LogicalTy);
  Sema::CapturedParamNameType Params[] = {{"Distance", ResultTy},
                                          {StringRef(), QualType()}};
  Actions.ActOnCapturedRegionStart({}, nullptr, CR_Default, Params);

  Stmt *Body;
  {
    Sema::CompoundScopeRAII CompoundScope(Actions);
    CapturedDecl *CS = cast<CapturedDecl>(Actions.CurContext);

    // Get the LValue expression for the result.
    ImplicitParamDecl *DistParam = CS->getParam(0);
    DeclRefExpr *DistRef = Actions.BuildDeclRefExpr(
        DistParam, LogicalTy, VK_LValue, {}, nullptr, nullptr, {}, nullptr);

    SmallVector<Stmt *, 4> BodyStmts;

    // Capture all referenced variable references.
    // TODO: Instead of computing NewStart/NewStop/NewStep inside the
    // CapturedStmt, we could compute them before and capture the result, to be
    // used jointly with the LoopVar function.
    VarDecl *NewStart = precomputeExpr(Actions, BodyStmts, StartExpr, ".start");
    VarDecl *NewStop = precomputeExpr(Actions, BodyStmts, StopExpr, ".stop");
    VarDecl *NewStep = precomputeExpr(Actions, BodyStmts, StepExpr, ".step");
    auto BuildVarRef = [&](VarDecl *VD) {
      return buildDeclRefExpr(Actions, VD, VD->getType(), {});
    };

    IntegerLiteral *Zero = IntegerLiteral::Create(
        Ctx, llvm::APInt(Ctx.getIntWidth(LogicalTy), 0), LogicalTy, {});
    IntegerLiteral *One = IntegerLiteral::Create(
        Ctx, llvm::APInt(Ctx.getIntWidth(LogicalTy), 1), LogicalTy, {});
    Expr *Dist;
    if (Rel == BO_NE) {
      // When using a != comparison, the increment can be +1 or -1. This can be
      // dynamic at runtime, so we need to check for the direction.
      Expr *IsNegStep = AssertSuccess(
          Actions.BuildBinOp(nullptr, {}, BO_LT, BuildVarRef(NewStep), Zero));

      // Positive increment.
      Expr *ForwardRange = AssertSuccess(Actions.BuildBinOp(
          nullptr, {}, BO_Sub, BuildVarRef(NewStop), BuildVarRef(NewStart)));
      ForwardRange = AssertSuccess(
          Actions.BuildCStyleCastExpr({}, LogicalTSI, {}, ForwardRange));
      Expr *ForwardDist = AssertSuccess(Actions.BuildBinOp(
          nullptr, {}, BO_Div, ForwardRange, BuildVarRef(NewStep)));

      // Negative increment.
      Expr *BackwardRange = AssertSuccess(Actions.BuildBinOp(
          nullptr, {}, BO_Sub, BuildVarRef(NewStart), BuildVarRef(NewStop)));
      BackwardRange = AssertSuccess(
          Actions.BuildCStyleCastExpr({}, LogicalTSI, {}, BackwardRange));
      Expr *NegIncAmount = AssertSuccess(
          Actions.BuildUnaryOp(nullptr, {}, UO_Minus, BuildVarRef(NewStep)));
      Expr *BackwardDist = AssertSuccess(
          Actions.BuildBinOp(nullptr, {}, BO_Div, BackwardRange, NegIncAmount));

      // Use the appropriate case.
      Dist = AssertSuccess(Actions.ActOnConditionalOp(
          {}, {}, IsNegStep, BackwardDist, ForwardDist));
    } else {
      assert((Rel == BO_LT || Rel == BO_LE || Rel == BO_GE || Rel == BO_GT) &&
             "Expected one of these relational operators");

      // We can derive the direction from any other comparison operator. It is
      // non well-formed OpenMP if Step increments/decrements in the other
      // directions. Whether at least the first iteration passes the loop
      // condition.
      Expr *HasAnyIteration = AssertSuccess(Actions.BuildBinOp(
          nullptr, {}, Rel, BuildVarRef(NewStart), BuildVarRef(NewStop)));

      // Compute the range between first and last counter value.
      Expr *Range;
      if (Rel == BO_GE || Rel == BO_GT)
        Range = AssertSuccess(Actions.BuildBinOp(
            nullptr, {}, BO_Sub, BuildVarRef(NewStart), BuildVarRef(NewStop)));
      else
        Range = AssertSuccess(Actions.BuildBinOp(
            nullptr, {}, BO_Sub, BuildVarRef(NewStop), BuildVarRef(NewStart)));

      // Ensure unsigned range space.
      Range =
          AssertSuccess(Actions.BuildCStyleCastExpr({}, LogicalTSI, {}, Range));

      if (Rel == BO_LE || Rel == BO_GE) {
        // Add one to the range if the relational operator is inclusive.
        Range =
            AssertSuccess(Actions.BuildBinOp(nullptr, {}, BO_Add, Range, One));
      }

      // Divide by the absolute step amount. If the range is not a multiple of
      // the step size, rounding-up the effective upper bound ensures that the
      // last iteration is included.
      // Note that the rounding-up may cause an overflow in a temporary that
      // could be avoided, but would have occurred in a C-style for-loop as
      // well.
      Expr *Divisor = BuildVarRef(NewStep);
      if (Rel == BO_GE || Rel == BO_GT)
        Divisor =
            AssertSuccess(Actions.BuildUnaryOp(nullptr, {}, UO_Minus, Divisor));
      Expr *DivisorMinusOne =
          AssertSuccess(Actions.BuildBinOp(nullptr, {}, BO_Sub, Divisor, One));
      Expr *RangeRoundUp = AssertSuccess(
          Actions.BuildBinOp(nullptr, {}, BO_Add, Range, DivisorMinusOne));
      Dist = AssertSuccess(
          Actions.BuildBinOp(nullptr, {}, BO_Div, RangeRoundUp, Divisor));

      // If there is not at least one iteration, the range contains garbage. Fix
      // to zero in this case.
      Dist = AssertSuccess(
          Actions.ActOnConditionalOp({}, {}, HasAnyIteration, Dist, Zero));
    }

    // Assign the result to the out-parameter.
    Stmt *ResultAssign = AssertSuccess(Actions.BuildBinOp(
        Actions.getCurScope(), {}, BO_Assign, DistRef, Dist));
    BodyStmts.push_back(ResultAssign);

    Body = AssertSuccess(Actions.ActOnCompoundStmt({}, {}, BodyStmts, false));
  }

  return cast<CapturedStmt>(
      AssertSuccess(Actions.ActOnCapturedRegionEnd(Body)));
}

/// Create a closure that computes the loop variable from the logical iteration
/// number.
///
/// \param Actions   The Sema object.
/// \param LoopVarTy Type for the loop variable used for result value.
/// \param LogicalTy Type for the logical iteration number.
/// \param StartExpr Value of the loop counter at the first iteration.
/// \param Step      Amount of increment after each iteration.
/// \param Deref     Whether the loop variable is a dereference of the loop
/// counter variable.
///
/// \return Closure (CapturedStmt) of the loop value calculation.
static CapturedStmt *buildLoopVarFunc(Sema &Actions, QualType LoopVarTy,
                                      QualType LogicalTy,
                                      DeclRefExpr *StartExpr, Expr *Step,
                                      bool Deref) {
  ASTContext &Ctx = Actions.getASTContext();

  // Pass the result as an out-parameter. Passing as return value would require
  // the OpenMPIRBuilder to know additional C/C++ semantics, such as how to
  // invoke a copy constructor.
  QualType TargetParamTy = Ctx.getLValueReferenceType(LoopVarTy);
  SemaOpenMP::CapturedParamNameType Params[] = {{"LoopVar", TargetParamTy},
                                                {"Logical", LogicalTy},
                                                {StringRef(), QualType()}};
  Actions.ActOnCapturedRegionStart({}, nullptr, CR_Default, Params);

  // Capture the initial iterator which represents the LoopVar value at the
  // zero's logical iteration. Since the original ForStmt/CXXForRangeStmt update
  // it in every iteration, capture it by value before it is modified.
  VarDecl *StartVar = cast<VarDecl>(StartExpr->getDecl());
  bool Invalid = Actions.tryCaptureVariable(StartVar, {},
                                            Sema::TryCapture_ExplicitByVal, {});
  (void)Invalid;
  assert(!Invalid && "Expecting capture-by-value to work.");

  Expr *Body;
  {
    Sema::CompoundScopeRAII CompoundScope(Actions);
    auto *CS = cast<CapturedDecl>(Actions.CurContext);

    ImplicitParamDecl *TargetParam = CS->getParam(0);
    DeclRefExpr *TargetRef = Actions.BuildDeclRefExpr(
        TargetParam, LoopVarTy, VK_LValue, {}, nullptr, nullptr, {}, nullptr);
    ImplicitParamDecl *IndvarParam = CS->getParam(1);
    DeclRefExpr *LogicalRef = Actions.BuildDeclRefExpr(
        IndvarParam, LogicalTy, VK_LValue, {}, nullptr, nullptr, {}, nullptr);

    // Capture the Start expression.
    CaptureVars Recap(Actions);
    Expr *NewStart = AssertSuccess(Recap.TransformExpr(StartExpr));
    Expr *NewStep = AssertSuccess(Recap.TransformExpr(Step));

    Expr *Skip = AssertSuccess(
        Actions.BuildBinOp(nullptr, {}, BO_Mul, NewStep, LogicalRef));
    // TODO: Explicitly cast to the iterator's difference_type instead of
    // relying on implicit conversion.
    Expr *Advanced =
        AssertSuccess(Actions.BuildBinOp(nullptr, {}, BO_Add, NewStart, Skip));

    if (Deref) {
      // For range-based for-loops convert the loop counter value to a concrete
      // loop variable value by dereferencing the iterator.
      Advanced =
          AssertSuccess(Actions.BuildUnaryOp(nullptr, {}, UO_Deref, Advanced));
    }

    // Assign the result to the output parameter.
    Body = AssertSuccess(Actions.BuildBinOp(Actions.getCurScope(), {},
                                            BO_Assign, TargetRef, Advanced));
  }
  return cast<CapturedStmt>(
      AssertSuccess(Actions.ActOnCapturedRegionEnd(Body)));
}

StmtResult SemaOpenMP::ActOnOpenMPCanonicalLoop(Stmt *AStmt) {
  ASTContext &Ctx = getASTContext();

  // Extract the common elements of ForStmt and CXXForRangeStmt:
  // Loop variable, repeat condition, increment
  Expr *Cond, *Inc;
  VarDecl *LIVDecl, *LUVDecl;
  if (auto *For = dyn_cast<ForStmt>(AStmt)) {
    Stmt *Init = For->getInit();
    if (auto *LCVarDeclStmt = dyn_cast<DeclStmt>(Init)) {
      // For statement declares loop variable.
      LIVDecl = cast<VarDecl>(LCVarDeclStmt->getSingleDecl());
    } else if (auto *LCAssign = dyn_cast<BinaryOperator>(Init)) {
      // For statement reuses variable.
      assert(LCAssign->getOpcode() == BO_Assign &&
             "init part must be a loop variable assignment");
      auto *CounterRef = cast<DeclRefExpr>(LCAssign->getLHS());
      LIVDecl = cast<VarDecl>(CounterRef->getDecl());
    } else
      llvm_unreachable("Cannot determine loop variable");
    LUVDecl = LIVDecl;

    Cond = For->getCond();
    Inc = For->getInc();
  } else if (auto *RangeFor = dyn_cast<CXXForRangeStmt>(AStmt)) {
    DeclStmt *BeginStmt = RangeFor->getBeginStmt();
    LIVDecl = cast<VarDecl>(BeginStmt->getSingleDecl());
    LUVDecl = RangeFor->getLoopVariable();

    Cond = RangeFor->getCond();
    Inc = RangeFor->getInc();
  } else
    llvm_unreachable("unhandled kind of loop");

  QualType CounterTy = LIVDecl->getType();
  QualType LVTy = LUVDecl->getType();

  // Analyze the loop condition.
  Expr *LHS, *RHS;
  BinaryOperator::Opcode CondRel;
  Cond = Cond->IgnoreImplicit();
  if (auto *CondBinExpr = dyn_cast<BinaryOperator>(Cond)) {
    LHS = CondBinExpr->getLHS();
    RHS = CondBinExpr->getRHS();
    CondRel = CondBinExpr->getOpcode();
  } else if (auto *CondCXXOp = dyn_cast<CXXOperatorCallExpr>(Cond)) {
    assert(CondCXXOp->getNumArgs() == 2 && "Comparison should have 2 operands");
    LHS = CondCXXOp->getArg(0);
    RHS = CondCXXOp->getArg(1);
    switch (CondCXXOp->getOperator()) {
    case OO_ExclaimEqual:
      CondRel = BO_NE;
      break;
    case OO_Less:
      CondRel = BO_LT;
      break;
    case OO_LessEqual:
      CondRel = BO_LE;
      break;
    case OO_Greater:
      CondRel = BO_GT;
      break;
    case OO_GreaterEqual:
      CondRel = BO_GE;
      break;
    default:
      llvm_unreachable("unexpected iterator operator");
    }
  } else
    llvm_unreachable("unexpected loop condition");

  // Normalize such that the loop counter is on the LHS.
  if (!isa<DeclRefExpr>(LHS->IgnoreImplicit()) ||
      cast<DeclRefExpr>(LHS->IgnoreImplicit())->getDecl() != LIVDecl) {
    std::swap(LHS, RHS);
    CondRel = BinaryOperator::reverseComparisonOp(CondRel);
  }
  auto *CounterRef = cast<DeclRefExpr>(LHS->IgnoreImplicit());

  // Decide the bit width for the logical iteration counter. By default use the
  // unsigned ptrdiff_t integer size (for iterators and pointers).
  // TODO: For iterators, use iterator::difference_type,
  // std::iterator_traits<>::difference_type or decltype(it - end).
  QualType LogicalTy = Ctx.getUnsignedPointerDiffType();
  if (CounterTy->isIntegerType()) {
    unsigned BitWidth = Ctx.getIntWidth(CounterTy);
    LogicalTy = Ctx.getIntTypeForBitwidth(BitWidth, false);
  }

  // Analyze the loop increment.
  Expr *Step;
  if (auto *IncUn = dyn_cast<UnaryOperator>(Inc)) {
    int Direction;
    switch (IncUn->getOpcode()) {
    case UO_PreInc:
    case UO_PostInc:
      Direction = 1;
      break;
    case UO_PreDec:
    case UO_PostDec:
      Direction = -1;
      break;
    default:
      llvm_unreachable("unhandled unary increment operator");
    }
    Step = IntegerLiteral::Create(
        Ctx, llvm::APInt(Ctx.getIntWidth(LogicalTy), Direction), LogicalTy, {});
  } else if (auto *IncBin = dyn_cast<BinaryOperator>(Inc)) {
    if (IncBin->getOpcode() == BO_AddAssign) {
      Step = IncBin->getRHS();
    } else if (IncBin->getOpcode() == BO_SubAssign) {
      Step = AssertSuccess(
          SemaRef.BuildUnaryOp(nullptr, {}, UO_Minus, IncBin->getRHS()));
    } else
      llvm_unreachable("unhandled binary increment operator");
  } else if (auto *CondCXXOp = dyn_cast<CXXOperatorCallExpr>(Inc)) {
    switch (CondCXXOp->getOperator()) {
    case OO_PlusPlus:
      Step = IntegerLiteral::Create(
          Ctx, llvm::APInt(Ctx.getIntWidth(LogicalTy), 1), LogicalTy, {});
      break;
    case OO_MinusMinus:
      Step = IntegerLiteral::Create(
          Ctx, llvm::APInt(Ctx.getIntWidth(LogicalTy), -1), LogicalTy, {});
      break;
    case OO_PlusEqual:
      Step = CondCXXOp->getArg(1);
      break;
    case OO_MinusEqual:
      Step = AssertSuccess(
          SemaRef.BuildUnaryOp(nullptr, {}, UO_Minus, CondCXXOp->getArg(1)));
      break;
    default:
      llvm_unreachable("unhandled overloaded increment operator");
    }
  } else
    llvm_unreachable("unknown increment expression");

  CapturedStmt *DistanceFunc =
      buildDistanceFunc(SemaRef, LogicalTy, CondRel, LHS, RHS, Step);
  CapturedStmt *LoopVarFunc = buildLoopVarFunc(
      SemaRef, LVTy, LogicalTy, CounterRef, Step, isa<CXXForRangeStmt>(AStmt));
  DeclRefExpr *LVRef =
      SemaRef.BuildDeclRefExpr(LUVDecl, LUVDecl->getType(), VK_LValue, {},
                               nullptr, nullptr, {}, nullptr);
  return OMPCanonicalLoop::create(getASTContext(), AStmt, DistanceFunc,
                                  LoopVarFunc, LVRef);
}

StmtResult SemaOpenMP::ActOnOpenMPLoopnest(Stmt *AStmt) {
  // Handle a literal loop.
  if (isa<ForStmt>(AStmt) || isa<CXXForRangeStmt>(AStmt))
    return ActOnOpenMPCanonicalLoop(AStmt);

  // If not a literal loop, it must be the result of a loop transformation.
  OMPExecutableDirective *LoopTransform = cast<OMPExecutableDirective>(AStmt);
  assert(
      isOpenMPLoopTransformationDirective(LoopTransform->getDirectiveKind()) &&
      "Loop transformation directive expected");
  return LoopTransform;
}

static ExprResult buildUserDefinedMapperRef(Sema &SemaRef, Scope *S,
                                            CXXScopeSpec &MapperIdScopeSpec,
                                            const DeclarationNameInfo &MapperId,
                                            QualType Type,
                                            Expr *UnresolvedMapper);

/// Perform DFS through the structure/class data members trying to find
/// member(s) with user-defined 'default' mapper and generate implicit map
/// clauses for such members with the found 'default' mapper.
static void
processImplicitMapsWithDefaultMappers(Sema &S, DSAStackTy *Stack,
                                      SmallVectorImpl<OMPClause *> &Clauses) {
  // Check for the default mapper for data members.
  if (S.getLangOpts().OpenMP < 50)
    return;
  SmallVector<OMPClause *, 4> ImplicitMaps;
  for (int Cnt = 0, EndCnt = Clauses.size(); Cnt < EndCnt; ++Cnt) {
    auto *C = dyn_cast<OMPMapClause>(Clauses[Cnt]);
    if (!C)
      continue;
    SmallVector<Expr *, 4> SubExprs;
    auto *MI = C->mapperlist_begin();
    for (auto I = C->varlist_begin(), End = C->varlist_end(); I != End;
         ++I, ++MI) {
      // Expression is mapped using mapper - skip it.
      if (*MI)
        continue;
      Expr *E = *I;
      // Expression is dependent - skip it, build the mapper when it gets
      // instantiated.
      if (E->isTypeDependent() || E->isValueDependent() ||
          E->containsUnexpandedParameterPack())
        continue;
      // Array section - need to check for the mapping of the array section
      // element.
      QualType CanonType = E->getType().getCanonicalType();
      if (CanonType->isSpecificBuiltinType(BuiltinType::ArraySection)) {
        const auto *OASE = cast<ArraySectionExpr>(E->IgnoreParenImpCasts());
        QualType BaseType =
            ArraySectionExpr::getBaseOriginalType(OASE->getBase());
        QualType ElemType;
        if (const auto *ATy = BaseType->getAsArrayTypeUnsafe())
          ElemType = ATy->getElementType();
        else
          ElemType = BaseType->getPointeeType();
        CanonType = ElemType;
      }

      // DFS over data members in structures/classes.
      SmallVector<std::pair<QualType, FieldDecl *>, 4> Types(
          1, {CanonType, nullptr});
      llvm::DenseMap<const Type *, Expr *> Visited;
      SmallVector<std::pair<FieldDecl *, unsigned>, 4> ParentChain(
          1, {nullptr, 1});
      while (!Types.empty()) {
        QualType BaseType;
        FieldDecl *CurFD;
        std::tie(BaseType, CurFD) = Types.pop_back_val();
        while (ParentChain.back().second == 0)
          ParentChain.pop_back();
        --ParentChain.back().second;
        if (BaseType.isNull())
          continue;
        // Only structs/classes are allowed to have mappers.
        const RecordDecl *RD = BaseType.getCanonicalType()->getAsRecordDecl();
        if (!RD)
          continue;
        auto It = Visited.find(BaseType.getTypePtr());
        if (It == Visited.end()) {
          // Try to find the associated user-defined mapper.
          CXXScopeSpec MapperIdScopeSpec;
          DeclarationNameInfo DefaultMapperId;
          DefaultMapperId.setName(S.Context.DeclarationNames.getIdentifier(
              &S.Context.Idents.get("default")));
          DefaultMapperId.setLoc(E->getExprLoc());
          ExprResult ER = buildUserDefinedMapperRef(
              S, Stack->getCurScope(), MapperIdScopeSpec, DefaultMapperId,
              BaseType, /*UnresolvedMapper=*/nullptr);
          if (ER.isInvalid())
            continue;
          It = Visited.try_emplace(BaseType.getTypePtr(), ER.get()).first;
        }
        // Found default mapper.
        if (It->second) {
          auto *OE = new (S.Context) OpaqueValueExpr(E->getExprLoc(), CanonType,
                                                     VK_LValue, OK_Ordinary, E);
          OE->setIsUnique(/*V=*/true);
          Expr *BaseExpr = OE;
          for (const auto &P : ParentChain) {
            if (P.first) {
              BaseExpr = S.BuildMemberExpr(
                  BaseExpr, /*IsArrow=*/false, E->getExprLoc(),
                  NestedNameSpecifierLoc(), SourceLocation(), P.first,
                  DeclAccessPair::make(P.first, P.first->getAccess()),
                  /*HadMultipleCandidates=*/false, DeclarationNameInfo(),
                  P.first->getType(), VK_LValue, OK_Ordinary);
              BaseExpr = S.DefaultLvalueConversion(BaseExpr).get();
            }
          }
          if (CurFD)
            BaseExpr = S.BuildMemberExpr(
                BaseExpr, /*IsArrow=*/false, E->getExprLoc(),
                NestedNameSpecifierLoc(), SourceLocation(), CurFD,
                DeclAccessPair::make(CurFD, CurFD->getAccess()),
                /*HadMultipleCandidates=*/false, DeclarationNameInfo(),
                CurFD->getType(), VK_LValue, OK_Ordinary);
          SubExprs.push_back(BaseExpr);
          continue;
        }
        // Check for the "default" mapper for data members.
        bool FirstIter = true;
        for (FieldDecl *FD : RD->fields()) {
          if (!FD)
            continue;
          QualType FieldTy = FD->getType();
          if (FieldTy.isNull() ||
              !(FieldTy->isStructureOrClassType() || FieldTy->isUnionType()))
            continue;
          if (FirstIter) {
            FirstIter = false;
            ParentChain.emplace_back(CurFD, 1);
          } else {
            ++ParentChain.back().second;
          }
          Types.emplace_back(FieldTy, FD);
        }
      }
    }
    if (SubExprs.empty())
      continue;
    CXXScopeSpec MapperIdScopeSpec;
    DeclarationNameInfo MapperId;
    if (OMPClause *NewClause = S.OpenMP().ActOnOpenMPMapClause(
            nullptr, C->getMapTypeModifiers(), C->getMapTypeModifiersLoc(),
            MapperIdScopeSpec, MapperId, C->getMapType(),
            /*IsMapTypeImplicit=*/true, SourceLocation(), SourceLocation(),
            SubExprs, OMPVarListLocTy()))
      Clauses.push_back(NewClause);
  }
}

namespace {
/// A 'teams loop' with a nested 'loop bind(parallel)' or generic function
/// call in the associated loop-nest cannot be a 'parallel for'.
class TeamsLoopChecker final : public ConstStmtVisitor<TeamsLoopChecker> {
  Sema &SemaRef;

public:
  bool teamsLoopCanBeParallelFor() const { return TeamsLoopCanBeParallelFor; }

  // Is there a nested OpenMP loop bind(parallel)
  void VisitOMPExecutableDirective(const OMPExecutableDirective *D) {
    if (D->getDirectiveKind() == llvm::omp::Directive::OMPD_loop) {
      if (const auto *C = D->getSingleClause<OMPBindClause>())
        if (C->getBindKind() == OMPC_BIND_parallel) {
          TeamsLoopCanBeParallelFor = false;
          // No need to continue visiting any more
          return;
        }
    }
    for (const Stmt *Child : D->children())
      if (Child)
        Visit(Child);
  }

  void VisitCallExpr(const CallExpr *C) {
    // Function calls inhibit parallel loop translation of 'target teams loop'
    // unless the assume-no-nested-parallelism flag has been specified.
    // OpenMP API runtime library calls do not inhibit parallel loop
    // translation, regardless of the assume-no-nested-parallelism.
    bool IsOpenMPAPI = false;
    auto *FD = dyn_cast_or_null<FunctionDecl>(C->getCalleeDecl());
    if (FD) {
      std::string Name = FD->getNameInfo().getAsString();
      IsOpenMPAPI = Name.find("omp_") == 0;
    }
    TeamsLoopCanBeParallelFor =
        IsOpenMPAPI || SemaRef.getLangOpts().OpenMPNoNestedParallelism;
    if (!TeamsLoopCanBeParallelFor)
      return;

    for (const Stmt *Child : C->children())
      if (Child)
        Visit(Child);
  }

  void VisitCapturedStmt(const CapturedStmt *S) {
    if (!S)
      return;
    Visit(S->getCapturedDecl()->getBody());
  }

  void VisitStmt(const Stmt *S) {
    if (!S)
      return;
    for (const Stmt *Child : S->children())
      if (Child)
        Visit(Child);
  }
  explicit TeamsLoopChecker(Sema &SemaRef)
      : SemaRef(SemaRef), TeamsLoopCanBeParallelFor(true) {}

private:
  bool TeamsLoopCanBeParallelFor;
};
} // namespace

static bool teamsLoopCanBeParallelFor(Stmt *AStmt, Sema &SemaRef) {
  TeamsLoopChecker Checker(SemaRef);
  Checker.Visit(AStmt);
  return Checker.teamsLoopCanBeParallelFor();
}

bool SemaOpenMP::mapLoopConstruct(
    llvm::SmallVector<OMPClause *> &ClausesWithoutBind,
    ArrayRef<OMPClause *> Clauses, OpenMPBindClauseKind &BindKind,
    OpenMPDirectiveKind &Kind, OpenMPDirectiveKind &PrevMappedDirective,
    SourceLocation StartLoc, SourceLocation EndLoc,
    const DeclarationNameInfo &DirName, OpenMPDirectiveKind CancelRegion) {

  bool UseClausesWithoutBind = false;

  // Restricting to "#pragma omp loop bind"
  if (getLangOpts().OpenMP >= 50 && Kind == OMPD_loop) {

    const OpenMPDirectiveKind ParentDirective = DSAStack->getParentDirective();

    if (BindKind == OMPC_BIND_unknown) {
      // Setting the enclosing teams or parallel construct for the loop
      // directive without bind clause.
      // [5.0:129:25-28] If the bind clause is not present on the construct and
      // the loop construct is closely nested inside a teams or parallel
      // construct, the binding region is the corresponding teams or parallel
      // region. If none of those conditions hold, the binding region is not
      // defined.
      BindKind = OMPC_BIND_thread; // Default bind(thread) if binding is unknown
      ArrayRef<OpenMPDirectiveKind> ParentLeafs =
          getLeafConstructsOrSelf(ParentDirective);

      if (ParentDirective == OMPD_unknown) {
        Diag(DSAStack->getDefaultDSALocation(),
             diag::err_omp_bind_required_on_loop);
      } else if (ParentLeafs.back() == OMPD_parallel) {
        BindKind = OMPC_BIND_parallel;
      } else if (ParentLeafs.back() == OMPD_teams) {
        BindKind = OMPC_BIND_teams;
      }
    } else {
      // bind clause is present in loop directive. When the loop directive is
      // changed to a new directive the bind clause is not used. So, we should
      // set flag indicating to only use the clauses that aren't the
      // bind clause.
      UseClausesWithoutBind = true;
    }

    for (OMPClause *C : Clauses) {
      // Spec restriction : bind(teams) and reduction not permitted.
      if (BindKind == OMPC_BIND_teams &&
          C->getClauseKind() == llvm::omp::Clause::OMPC_reduction)
        Diag(DSAStack->getDefaultDSALocation(),
             diag::err_omp_loop_reduction_clause);

      // A new Vector ClausesWithoutBind, which does not contain the bind
      // clause, for passing to new directive.
      if (C->getClauseKind() != llvm::omp::Clause::OMPC_bind)
        ClausesWithoutBind.push_back(C);
    }

    switch (BindKind) {
    case OMPC_BIND_parallel:
      Kind = OMPD_for;
      DSAStack->setCurrentDirective(OMPD_for);
      DSAStack->setMappedDirective(OMPD_loop);
      PrevMappedDirective = OMPD_loop;
      break;
    case OMPC_BIND_teams:
      Kind = OMPD_distribute;
      DSAStack->setCurrentDirective(OMPD_distribute);
      DSAStack->setMappedDirective(OMPD_loop);
      PrevMappedDirective = OMPD_loop;
      break;
    case OMPC_BIND_thread:
      Kind = OMPD_simd;
      DSAStack->setCurrentDirective(OMPD_simd);
      DSAStack->setMappedDirective(OMPD_loop);
      PrevMappedDirective = OMPD_loop;
      break;
    case OMPC_BIND_unknown:
      break;
    }
  } else if (PrevMappedDirective == OMPD_loop) {
    /// An initial pass after recognizing all the statements is done in the
    /// Parser when the directive OMPD_loop is mapped to OMPD_for,
    /// OMPD_distribute or OMPD_simd. A second transform pass with call from
    /// clang::TreeTransform::TransformOMPExecutableDirective() is done
    /// with the Directive as one of the above mapped directive without
    /// the bind clause. Then "PrevMappedDirective" stored in the
    /// OMPExecutableDirective is accessed and hence this else statement.

    DSAStack->setMappedDirective(OMPD_loop);
  }

  return UseClausesWithoutBind;
}

StmtResult SemaOpenMP::ActOnOpenMPExecutableDirective(
    OpenMPDirectiveKind Kind, const DeclarationNameInfo &DirName,
    OpenMPDirectiveKind CancelRegion, ArrayRef<OMPClause *> Clauses,
    Stmt *AStmt, SourceLocation StartLoc, SourceLocation EndLoc,
    OpenMPDirectiveKind PrevMappedDirective) {
  assert(isOpenMPExecutableDirective(Kind) && "Unexpected directive category");

  StmtResult Res = StmtError();
  OpenMPBindClauseKind BindKind = OMPC_BIND_unknown;
  llvm::SmallVector<OMPClause *> ClausesWithoutBind;
  bool UseClausesWithoutBind = false;

  if (const OMPBindClause *BC =
          OMPExecutableDirective::getSingleClause<OMPBindClause>(Clauses))
    BindKind = BC->getBindKind();

  // Variable used to note down the DirectiveKind because mapLoopConstruct may
  // change "Kind" variable, due to mapping of "omp loop" to other directives.
  OpenMPDirectiveKind DK = Kind;
  if (Kind == OMPD_loop || PrevMappedDirective == OMPD_loop) {
    UseClausesWithoutBind = mapLoopConstruct(
        ClausesWithoutBind, Clauses, BindKind, Kind, PrevMappedDirective,
        StartLoc, EndLoc, DirName, CancelRegion);
    DK = OMPD_loop;
  }

  // First check CancelRegion which is then used in checkNestingOfRegions.
  if (checkCancelRegion(SemaRef, Kind, CancelRegion, StartLoc) ||
      checkNestingOfRegions(SemaRef, DSAStack, DK, DirName, CancelRegion,
                            BindKind, StartLoc)) {
    return StmtError();
  }

  // Report affected OpenMP target offloading behavior when in HIP lang-mode.
  if (getLangOpts().HIP && (isOpenMPTargetExecutionDirective(Kind) ||
                            isOpenMPTargetDataManagementDirective(Kind)))
    Diag(StartLoc, diag::warn_hip_omp_target_directives);

  llvm::SmallVector<OMPClause *, 8> ClausesWithImplicit;
  VarsWithInheritedDSAType VarsWithInheritedDSA;
  bool ErrorFound = false;
  if (getLangOpts().OpenMP >= 50 && UseClausesWithoutBind) {
    ClausesWithImplicit.append(ClausesWithoutBind.begin(),
                               ClausesWithoutBind.end());
  } else {
    ClausesWithImplicit.append(Clauses.begin(), Clauses.end());
  }
  if (AStmt && !SemaRef.CurContext->isDependentContext() &&
      isOpenMPCapturingDirective(Kind)) {
    assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");

    // Check default data sharing attributes for referenced variables.
    DSAAttrChecker DSAChecker(DSAStack, SemaRef, cast<CapturedStmt>(AStmt));
    int ThisCaptureLevel = getOpenMPCaptureLevels(Kind);
    Stmt *S = AStmt;
    while (--ThisCaptureLevel >= 0)
      S = cast<CapturedStmt>(S)->getCapturedStmt();
    DSAChecker.Visit(S);
    if (!isOpenMPTargetDataManagementDirective(Kind) &&
        !isOpenMPTaskingDirective(Kind)) {
      // Visit subcaptures to generate implicit clauses for captured vars.
      auto *CS = cast<CapturedStmt>(AStmt);
      SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
      getOpenMPCaptureRegions(CaptureRegions, Kind);
      // Ignore outer tasking regions for target directives.
      if (CaptureRegions.size() > 1 && CaptureRegions.front() == OMPD_task)
        CS = cast<CapturedStmt>(CS->getCapturedStmt());
      DSAChecker.visitSubCaptures(CS);
    }
    if (DSAChecker.isErrorFound())
      return StmtError();
    // Generate list of implicitly defined firstprivate variables.
    VarsWithInheritedDSA = DSAChecker.getVarsWithInheritedDSA();

    SmallVector<Expr *, 4> ImplicitFirstprivates(
        DSAChecker.getImplicitFirstprivate().begin(),
        DSAChecker.getImplicitFirstprivate().end());
    SmallVector<Expr *, 4> ImplicitPrivates(
        DSAChecker.getImplicitPrivate().begin(),
        DSAChecker.getImplicitPrivate().end());
    const unsigned DefaultmapKindNum = OMPC_DEFAULTMAP_unknown + 1;
    SmallVector<Expr *, 4> ImplicitMaps[DefaultmapKindNum][OMPC_MAP_delete];
    SmallVector<OpenMPMapModifierKind, NumberOfOMPMapClauseModifiers>
        ImplicitMapModifiers[DefaultmapKindNum];
    SmallVector<SourceLocation, NumberOfOMPMapClauseModifiers>
        ImplicitMapModifiersLoc[DefaultmapKindNum];
    // Get the original location of present modifier from Defaultmap clause.
    SourceLocation PresentModifierLocs[DefaultmapKindNum];
    for (OMPClause *C : Clauses) {
      if (auto *DMC = dyn_cast<OMPDefaultmapClause>(C))
        if (DMC->getDefaultmapModifier() == OMPC_DEFAULTMAP_MODIFIER_present)
          PresentModifierLocs[DMC->getDefaultmapKind()] =
              DMC->getDefaultmapModifierLoc();
    }
    for (unsigned VC = 0; VC < DefaultmapKindNum; ++VC) {
      auto K = static_cast<OpenMPDefaultmapClauseKind>(VC);
      for (unsigned I = 0; I < OMPC_MAP_delete; ++I) {
        ArrayRef<Expr *> ImplicitMap =
            DSAChecker.getImplicitMap(K, static_cast<OpenMPMapClauseKind>(I));
        ImplicitMaps[VC][I].append(ImplicitMap.begin(), ImplicitMap.end());
      }
      ArrayRef<OpenMPMapModifierKind> ImplicitModifier =
          DSAChecker.getImplicitMapModifier(K);
      ImplicitMapModifiers[VC].append(ImplicitModifier.begin(),
                                      ImplicitModifier.end());
      std::fill_n(std::back_inserter(ImplicitMapModifiersLoc[VC]),
                  ImplicitModifier.size(), PresentModifierLocs[VC]);
    }
    // Mark taskgroup task_reduction descriptors as implicitly firstprivate.
    for (OMPClause *C : Clauses) {
      if (auto *IRC = dyn_cast<OMPInReductionClause>(C)) {
        for (Expr *E : IRC->taskgroup_descriptors())
          if (E)
            ImplicitFirstprivates.emplace_back(E);
      }
      // OpenMP 5.0, 2.10.1 task Construct
      // [detach clause]... The event-handle will be considered as if it was
      // specified on a firstprivate clause.
      if (auto *DC = dyn_cast<OMPDetachClause>(C))
        ImplicitFirstprivates.push_back(DC->getEventHandler());
    }
    if (!ImplicitFirstprivates.empty()) {
      if (OMPClause *Implicit = ActOnOpenMPFirstprivateClause(
              ImplicitFirstprivates, SourceLocation(), SourceLocation(),
              SourceLocation())) {
        ClausesWithImplicit.push_back(Implicit);
        ErrorFound = cast<OMPFirstprivateClause>(Implicit)->varlist_size() !=
                     ImplicitFirstprivates.size();
      } else {
        ErrorFound = true;
      }
    }
    if (!ImplicitPrivates.empty()) {
      if (OMPClause *Implicit =
              ActOnOpenMPPrivateClause(ImplicitPrivates, SourceLocation(),
                                       SourceLocation(), SourceLocation())) {
        ClausesWithImplicit.push_back(Implicit);
        ErrorFound = cast<OMPPrivateClause>(Implicit)->varlist_size() !=
                     ImplicitPrivates.size();
      } else {
        ErrorFound = true;
      }
    }
    // OpenMP 5.0 [2.19.7]
    // If a list item appears in a reduction, lastprivate or linear
    // clause on a combined target construct then it is treated as
    // if it also appears in a map clause with a map-type of tofrom
    if (getLangOpts().OpenMP >= 50 && Kind != OMPD_target &&
        isOpenMPTargetExecutionDirective(Kind)) {
      SmallVector<Expr *, 4> ImplicitExprs;
      for (OMPClause *C : Clauses) {
        if (auto *RC = dyn_cast<OMPReductionClause>(C))
          for (Expr *E : RC->varlists())
            if (!isa<DeclRefExpr>(E->IgnoreParenImpCasts()))
              ImplicitExprs.emplace_back(E);
      }
      if (!ImplicitExprs.empty()) {
        ArrayRef<Expr *> Exprs = ImplicitExprs;
        CXXScopeSpec MapperIdScopeSpec;
        DeclarationNameInfo MapperId;
        if (OMPClause *Implicit = ActOnOpenMPMapClause(
                nullptr, OMPC_MAP_MODIFIER_unknown, SourceLocation(),
                MapperIdScopeSpec, MapperId, OMPC_MAP_tofrom,
                /*IsMapTypeImplicit=*/true, SourceLocation(), SourceLocation(),
                Exprs, OMPVarListLocTy(), /*NoDiagnose=*/true))
          ClausesWithImplicit.emplace_back(Implicit);
      }
    }
    for (unsigned I = 0, E = DefaultmapKindNum; I < E; ++I) {
      int ClauseKindCnt = -1;
      for (ArrayRef<Expr *> ImplicitMap : ImplicitMaps[I]) {
        ++ClauseKindCnt;
        if (ImplicitMap.empty())
          continue;
        CXXScopeSpec MapperIdScopeSpec;
        DeclarationNameInfo MapperId;
        auto K = static_cast<OpenMPMapClauseKind>(ClauseKindCnt);
        if (OMPClause *Implicit = ActOnOpenMPMapClause(
                nullptr, ImplicitMapModifiers[I], ImplicitMapModifiersLoc[I],
                MapperIdScopeSpec, MapperId, K, /*IsMapTypeImplicit=*/true,
                SourceLocation(), SourceLocation(), ImplicitMap,
                OMPVarListLocTy())) {
          ClausesWithImplicit.emplace_back(Implicit);
          ErrorFound |= cast<OMPMapClause>(Implicit)->varlist_size() !=
                        ImplicitMap.size();
        } else {
          ErrorFound = true;
        }
      }
    }
    // Build expressions for implicit maps of data members with 'default'
    // mappers.
    if (getLangOpts().OpenMP >= 50)
      processImplicitMapsWithDefaultMappers(SemaRef, DSAStack,
                                            ClausesWithImplicit);
  }

  switch (Kind) {
  case OMPD_parallel:
    Res = ActOnOpenMPParallelDirective(ClausesWithImplicit, AStmt, StartLoc,
                                       EndLoc);
    break;
  case OMPD_simd:
    Res = ActOnOpenMPSimdDirective(ClausesWithImplicit, AStmt, StartLoc, EndLoc,
                                   VarsWithInheritedDSA);
    break;
  case OMPD_tile:
    Res =
        ActOnOpenMPTileDirective(ClausesWithImplicit, AStmt, StartLoc, EndLoc);
    break;
  case OMPD_unroll:
    Res = ActOnOpenMPUnrollDirective(ClausesWithImplicit, AStmt, StartLoc,
                                     EndLoc);
    break;
  case OMPD_reverse:
    assert(ClausesWithImplicit.empty() &&
           "reverse directive does not support any clauses");
    Res = ActOnOpenMPReverseDirective(AStmt, StartLoc, EndLoc);
    break;
  case OMPD_interchange:
    Res = ActOnOpenMPInterchangeDirective(ClausesWithImplicit, AStmt, StartLoc,
                                          EndLoc);
    break;
  case OMPD_for:
    Res = ActOnOpenMPForDirective(ClausesWithImplicit, AStmt, StartLoc, EndLoc,
                                  VarsWithInheritedDSA);
    break;
  case OMPD_for_simd:
    Res = ActOnOpenMPForSimdDirective(ClausesWithImplicit, AStmt, StartLoc,
                                      EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_sections:
    Res = ActOnOpenMPSectionsDirective(ClausesWithImplicit, AStmt, StartLoc,
                                       EndLoc);
    break;
  case OMPD_section:
    assert(ClausesWithImplicit.empty() &&
           "No clauses are allowed for 'omp section' directive");
    Res = ActOnOpenMPSectionDirective(AStmt, StartLoc, EndLoc);
    break;
  case OMPD_single:
    Res = ActOnOpenMPSingleDirective(ClausesWithImplicit, AStmt, StartLoc,
                                     EndLoc);
    break;
  case OMPD_master:
    assert(ClausesWithImplicit.empty() &&
           "No clauses are allowed for 'omp master' directive");
    Res = ActOnOpenMPMasterDirective(AStmt, StartLoc, EndLoc);
    break;
  case OMPD_masked:
    Res = ActOnOpenMPMaskedDirective(ClausesWithImplicit, AStmt, StartLoc,
                                     EndLoc);
    break;
  case OMPD_critical:
    Res = ActOnOpenMPCriticalDirective(DirName, ClausesWithImplicit, AStmt,
                                       StartLoc, EndLoc);
    break;
  case OMPD_parallel_for:
    Res = ActOnOpenMPParallelForDirective(ClausesWithImplicit, AStmt, StartLoc,
                                          EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_parallel_for_simd:
    Res = ActOnOpenMPParallelForSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_scope:
    Res =
        ActOnOpenMPScopeDirective(ClausesWithImplicit, AStmt, StartLoc, EndLoc);
    break;
  case OMPD_parallel_master:
    Res = ActOnOpenMPParallelMasterDirective(ClausesWithImplicit, AStmt,
                                             StartLoc, EndLoc);
    break;
  case OMPD_parallel_masked:
    Res = ActOnOpenMPParallelMaskedDirective(ClausesWithImplicit, AStmt,
                                             StartLoc, EndLoc);
    break;
  case OMPD_parallel_sections:
    Res = ActOnOpenMPParallelSectionsDirective(ClausesWithImplicit, AStmt,
                                               StartLoc, EndLoc);
    break;
  case OMPD_task:
    Res =
        ActOnOpenMPTaskDirective(ClausesWithImplicit, AStmt, StartLoc, EndLoc);
    break;
  case OMPD_taskyield:
    assert(ClausesWithImplicit.empty() &&
           "No clauses are allowed for 'omp taskyield' directive");
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp taskyield' directive");
    Res = ActOnOpenMPTaskyieldDirective(StartLoc, EndLoc);
    break;
  case OMPD_error:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp error' directive");
    Res = ActOnOpenMPErrorDirective(ClausesWithImplicit, StartLoc, EndLoc);
    break;
  case OMPD_barrier:
    assert(ClausesWithImplicit.empty() &&
           "No clauses are allowed for 'omp barrier' directive");
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp barrier' directive");
    Res = ActOnOpenMPBarrierDirective(StartLoc, EndLoc);
    break;
  case OMPD_taskwait:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp taskwait' directive");
    Res = ActOnOpenMPTaskwaitDirective(ClausesWithImplicit, StartLoc, EndLoc);
    break;
  case OMPD_taskgroup:
    Res = ActOnOpenMPTaskgroupDirective(ClausesWithImplicit, AStmt, StartLoc,
                                        EndLoc);
    break;
  case OMPD_flush:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp flush' directive");
    Res = ActOnOpenMPFlushDirective(ClausesWithImplicit, StartLoc, EndLoc);
    break;
  case OMPD_depobj:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp depobj' directive");
    Res = ActOnOpenMPDepobjDirective(ClausesWithImplicit, StartLoc, EndLoc);
    break;
  case OMPD_scan:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp scan' directive");
    Res = ActOnOpenMPScanDirective(ClausesWithImplicit, StartLoc, EndLoc);
    break;
  case OMPD_ordered:
    Res = ActOnOpenMPOrderedDirective(ClausesWithImplicit, AStmt, StartLoc,
                                      EndLoc);
    break;
  case OMPD_atomic:
    Res = ActOnOpenMPAtomicDirective(ClausesWithImplicit, AStmt, StartLoc,
                                     EndLoc);
    break;
  case OMPD_teams:
    Res =
        ActOnOpenMPTeamsDirective(ClausesWithImplicit, AStmt, StartLoc, EndLoc);
    break;
  case OMPD_target:
    Res = ActOnOpenMPTargetDirective(ClausesWithImplicit, AStmt, StartLoc,
                                     EndLoc);
    break;
  case OMPD_target_parallel:
    Res = ActOnOpenMPTargetParallelDirective(ClausesWithImplicit, AStmt,
                                             StartLoc, EndLoc);
    break;
  case OMPD_target_parallel_for:
    Res = ActOnOpenMPTargetParallelForDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_cancellation_point:
    assert(ClausesWithImplicit.empty() &&
           "No clauses are allowed for 'omp cancellation point' directive");
    assert(AStmt == nullptr && "No associated statement allowed for 'omp "
                               "cancellation point' directive");
    Res = ActOnOpenMPCancellationPointDirective(StartLoc, EndLoc, CancelRegion);
    break;
  case OMPD_cancel:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp cancel' directive");
    Res = ActOnOpenMPCancelDirective(ClausesWithImplicit, StartLoc, EndLoc,
                                     CancelRegion);
    break;
  case OMPD_target_data:
    Res = ActOnOpenMPTargetDataDirective(ClausesWithImplicit, AStmt, StartLoc,
                                         EndLoc);
    break;
  case OMPD_target_enter_data:
    Res = ActOnOpenMPTargetEnterDataDirective(ClausesWithImplicit, StartLoc,
                                              EndLoc, AStmt);
    break;
  case OMPD_target_exit_data:
    Res = ActOnOpenMPTargetExitDataDirective(ClausesWithImplicit, StartLoc,
                                             EndLoc, AStmt);
    break;
  case OMPD_taskloop:
    Res = ActOnOpenMPTaskLoopDirective(ClausesWithImplicit, AStmt, StartLoc,
                                       EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_taskloop_simd:
    Res = ActOnOpenMPTaskLoopSimdDirective(ClausesWithImplicit, AStmt, StartLoc,
                                           EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_master_taskloop:
    Res = ActOnOpenMPMasterTaskLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_masked_taskloop:
    Res = ActOnOpenMPMaskedTaskLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_master_taskloop_simd:
    Res = ActOnOpenMPMasterTaskLoopSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_masked_taskloop_simd:
    Res = ActOnOpenMPMaskedTaskLoopSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_parallel_master_taskloop:
    Res = ActOnOpenMPParallelMasterTaskLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_parallel_masked_taskloop:
    Res = ActOnOpenMPParallelMaskedTaskLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_parallel_master_taskloop_simd:
    Res = ActOnOpenMPParallelMasterTaskLoopSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_parallel_masked_taskloop_simd:
    Res = ActOnOpenMPParallelMaskedTaskLoopSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_distribute:
    Res = ActOnOpenMPDistributeDirective(ClausesWithImplicit, AStmt, StartLoc,
                                         EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_update:
    Res = ActOnOpenMPTargetUpdateDirective(ClausesWithImplicit, StartLoc,
                                           EndLoc, AStmt);
    break;
  case OMPD_distribute_parallel_for:
    Res = ActOnOpenMPDistributeParallelForDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_distribute_parallel_for_simd:
    Res = ActOnOpenMPDistributeParallelForSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_distribute_simd:
    Res = ActOnOpenMPDistributeSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_parallel_for_simd:
    Res = ActOnOpenMPTargetParallelForSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_simd:
    Res = ActOnOpenMPTargetSimdDirective(ClausesWithImplicit, AStmt, StartLoc,
                                         EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_teams_distribute:
    Res = ActOnOpenMPTeamsDistributeDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_teams_distribute_simd:
    Res = ActOnOpenMPTeamsDistributeSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_teams_distribute_parallel_for_simd:
    Res = ActOnOpenMPTeamsDistributeParallelForSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_teams_distribute_parallel_for:
    Res = ActOnOpenMPTeamsDistributeParallelForDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_teams:
    Res = ActOnOpenMPTargetTeamsDirective(ClausesWithImplicit, AStmt, StartLoc,
                                          EndLoc);
    break;
  case OMPD_target_teams_distribute:
    Res = ActOnOpenMPTargetTeamsDistributeDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_teams_distribute_parallel_for:
    Res = ActOnOpenMPTargetTeamsDistributeParallelForDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_teams_distribute_parallel_for_simd:
    Res = ActOnOpenMPTargetTeamsDistributeParallelForSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_teams_distribute_simd:
    Res = ActOnOpenMPTargetTeamsDistributeSimdDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_interop:
    assert(AStmt == nullptr &&
           "No associated statement allowed for 'omp interop' directive");
    Res = ActOnOpenMPInteropDirective(ClausesWithImplicit, StartLoc, EndLoc);
    break;
  case OMPD_dispatch:
    Res = ActOnOpenMPDispatchDirective(ClausesWithImplicit, AStmt, StartLoc,
                                       EndLoc);
    break;
  case OMPD_loop:
    Res = ActOnOpenMPGenericLoopDirective(ClausesWithImplicit, AStmt, StartLoc,
                                          EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_teams_loop:
    Res = ActOnOpenMPTeamsGenericLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_teams_loop:
    Res = ActOnOpenMPTargetTeamsGenericLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_parallel_loop:
    Res = ActOnOpenMPParallelGenericLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_target_parallel_loop:
    Res = ActOnOpenMPTargetParallelGenericLoopDirective(
        ClausesWithImplicit, AStmt, StartLoc, EndLoc, VarsWithInheritedDSA);
    break;
  case OMPD_declare_target:
  case OMPD_end_declare_target:
  case OMPD_threadprivate:
  case OMPD_allocate:
  case OMPD_declare_reduction:
  case OMPD_declare_mapper:
  case OMPD_declare_simd:
  case OMPD_requires:
  case OMPD_declare_variant:
  case OMPD_begin_declare_variant:
  case OMPD_end_declare_variant:
    llvm_unreachable("OpenMP Directive is not allowed");
  case OMPD_unknown:
  default:
    llvm_unreachable("Unknown OpenMP directive");
  }

  ErrorFound = Res.isInvalid() || ErrorFound;

  // Check variables in the clauses if default(none) or
  // default(firstprivate) was specified.
  if (DSAStack->getDefaultDSA() == DSA_none ||
      DSAStack->getDefaultDSA() == DSA_private ||
      DSAStack->getDefaultDSA() == DSA_firstprivate) {
    DSAAttrChecker DSAChecker(DSAStack, SemaRef, nullptr);
    for (OMPClause *C : Clauses) {
      switch (C->getClauseKind()) {
      case OMPC_num_threads:
      case OMPC_dist_schedule:
        // Do not analyze if no parent teams directive.
        if (isOpenMPTeamsDirective(Kind))
          break;
        continue;
      case OMPC_if:
        if (isOpenMPTeamsDirective(Kind) &&
            cast<OMPIfClause>(C)->getNameModifier() != OMPD_target)
          break;
        if (isOpenMPParallelDirective(Kind) &&
            isOpenMPTaskLoopDirective(Kind) &&
            cast<OMPIfClause>(C)->getNameModifier() != OMPD_parallel)
          break;
        continue;
      case OMPC_schedule:
      case OMPC_detach:
        break;
      case OMPC_grainsize:
      case OMPC_num_tasks:
      case OMPC_final:
      case OMPC_priority:
      case OMPC_novariants:
      case OMPC_nocontext:
        // Do not analyze if no parent parallel directive.
        if (isOpenMPParallelDirective(Kind))
          break;
        continue;
      case OMPC_ordered:
      case OMPC_device:
      case OMPC_num_teams:
      case OMPC_thread_limit:
      case OMPC_hint:
      case OMPC_collapse:
      case OMPC_safelen:
      case OMPC_simdlen:
      case OMPC_sizes:
      case OMPC_default:
      case OMPC_proc_bind:
      case OMPC_private:
      case OMPC_firstprivate:
      case OMPC_lastprivate:
      case OMPC_shared:
      case OMPC_reduction:
      case OMPC_task_reduction:
      case OMPC_in_reduction:
      case OMPC_linear:
      case OMPC_aligned:
      case OMPC_copyin:
      case OMPC_copyprivate:
      case OMPC_nowait:
      case OMPC_untied:
      case OMPC_mergeable:
      case OMPC_allocate:
      case OMPC_read:
      case OMPC_write:
      case OMPC_update:
      case OMPC_capture:
      case OMPC_compare:
      case OMPC_seq_cst:
      case OMPC_acq_rel:
      case OMPC_acquire:
      case OMPC_release:
      case OMPC_relaxed:
      case OMPC_depend:
      case OMPC_threads:
      case OMPC_simd:
      case OMPC_map:
      case OMPC_nogroup:
      case OMPC_defaultmap:
      case OMPC_to:
      case OMPC_from:
      case OMPC_use_device_ptr:
      case OMPC_use_device_addr:
      case OMPC_is_device_ptr:
      case OMPC_has_device_addr:
      case OMPC_nontemporal:
      case OMPC_order:
      case OMPC_destroy:
      case OMPC_inclusive:
      case OMPC_exclusive:
      case OMPC_uses_allocators:
      case OMPC_affinity:
      case OMPC_bind:
      case OMPC_filter:
        continue;
      case OMPC_allocator:
      case OMPC_flush:
      case OMPC_depobj:
      case OMPC_threadprivate:
      case OMPC_uniform:
      case OMPC_unknown:
      case OMPC_unified_address:
      case OMPC_unified_shared_memory:
      case OMPC_reverse_offload:
      case OMPC_dynamic_allocators:
      case OMPC_atomic_default_mem_order:
      case OMPC_device_type:
      case OMPC_match:
      case OMPC_when:
      case OMPC_at:
      case OMPC_severity:
      case OMPC_message:
      default:
        llvm_unreachable("Unexpected clause");
      }
      for (Stmt *CC : C->children()) {
        if (CC)
          DSAChecker.Visit(CC);
      }
    }
    for (const auto &P : DSAChecker.getVarsWithInheritedDSA())
      VarsWithInheritedDSA[P.getFirst()] = P.getSecond();
  }
  for (const auto &P : VarsWithInheritedDSA) {
    if (P.getFirst()->isImplicit() || isa<OMPCapturedExprDecl>(P.getFirst()))
      continue;
    ErrorFound = true;
    if (DSAStack->getDefaultDSA() == DSA_none ||
        DSAStack->getDefaultDSA() == DSA_private ||
        DSAStack->getDefaultDSA() == DSA_firstprivate) {
      Diag(P.second->getExprLoc(), diag::err_omp_no_dsa_for_variable)
          << P.first << P.second->getSourceRange();
      Diag(DSAStack->getDefaultDSALocation(), diag::note_omp_default_dsa_none);
    } else if (getLangOpts().OpenMP >= 50) {
      Diag(P.second->getExprLoc(),
           diag::err_omp_defaultmap_no_attr_for_variable)
          << P.first << P.second->getSourceRange();
      Diag(DSAStack->getDefaultDSALocation(),
           diag::note_omp_defaultmap_attr_none);
    }
  }

  llvm::SmallVector<OpenMPDirectiveKind, 4> AllowedNameModifiers;
  for (OpenMPDirectiveKind D : getLeafConstructsOrSelf(Kind)) {
    if (isAllowedClauseForDirective(D, OMPC_if, getLangOpts().OpenMP))
      AllowedNameModifiers.push_back(D);
  }
  if (!AllowedNameModifiers.empty())
    ErrorFound = checkIfClauses(SemaRef, Kind, Clauses, AllowedNameModifiers) ||
                 ErrorFound;

  if (ErrorFound)
    return StmtError();

  if (!SemaRef.CurContext->isDependentContext() &&
      isOpenMPTargetExecutionDirective(Kind) &&
      !(DSAStack->hasRequiresDeclWithClause<OMPUnifiedSharedMemoryClause>() ||
        DSAStack->hasRequiresDeclWithClause<OMPUnifiedAddressClause>() ||
        DSAStack->hasRequiresDeclWithClause<OMPReverseOffloadClause>() ||
        DSAStack->hasRequiresDeclWithClause<OMPDynamicAllocatorsClause>())) {
    // Register target to DSA Stack.
    DSAStack->addTargetDirLocation(StartLoc);
  }

  return Res;
}

SemaOpenMP::DeclGroupPtrTy SemaOpenMP::ActOnOpenMPDeclareSimdDirective(
    DeclGroupPtrTy DG, OMPDeclareSimdDeclAttr::BranchStateTy BS, Expr *Simdlen,
    ArrayRef<Expr *> Uniforms, ArrayRef<Expr *> Aligneds,
    ArrayRef<Expr *> Alignments, ArrayRef<Expr *> Linears,
    ArrayRef<unsigned> LinModifiers, ArrayRef<Expr *> Steps, SourceRange SR) {
  assert(Aligneds.size() == Alignments.size());
  assert(Linears.size() == LinModifiers.size());
  assert(Linears.size() == Steps.size());
  if (!DG || DG.get().isNull())
    return DeclGroupPtrTy();

  const int SimdId = 0;
  if (!DG.get().isSingleDecl()) {
    Diag(SR.getBegin(), diag::err_omp_single_decl_in_declare_simd_variant)
        << SimdId;
    return DG;
  }
  Decl *ADecl = DG.get().getSingleDecl();
  if (auto *FTD = dyn_cast<FunctionTemplateDecl>(ADecl))
    ADecl = FTD->getTemplatedDecl();

  auto *FD = dyn_cast<FunctionDecl>(ADecl);
  if (!FD) {
    Diag(ADecl->getLocation(), diag::err_omp_function_expected) << SimdId;
    return DeclGroupPtrTy();
  }

  // OpenMP [2.8.2, declare simd construct, Description]
  // The parameter of the simdlen clause must be a constant positive integer
  // expression.
  ExprResult SL;
  if (Simdlen)
    SL = VerifyPositiveIntegerConstantInClause(Simdlen, OMPC_simdlen);
  // OpenMP [2.8.2, declare simd construct, Description]
  // The special this pointer can be used as if was one of the arguments to the
  // function in any of the linear, aligned, or uniform clauses.
  // The uniform clause declares one or more arguments to have an invariant
  // value for all concurrent invocations of the function in the execution of a
  // single SIMD loop.
  llvm::DenseMap<const Decl *, const Expr *> UniformedArgs;
  const Expr *UniformedLinearThis = nullptr;
  for (const Expr *E : Uniforms) {
    E = E->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl()))
        if (FD->getNumParams() > PVD->getFunctionScopeIndex() &&
            FD->getParamDecl(PVD->getFunctionScopeIndex())
                    ->getCanonicalDecl() == PVD->getCanonicalDecl()) {
          UniformedArgs.try_emplace(PVD->getCanonicalDecl(), E);
          continue;
        }
    if (isa<CXXThisExpr>(E)) {
      UniformedLinearThis = E;
      continue;
    }
    Diag(E->getExprLoc(), diag::err_omp_param_or_this_in_clause)
        << FD->getDeclName() << (isa<CXXMethodDecl>(ADecl) ? 1 : 0);
  }
  // OpenMP [2.8.2, declare simd construct, Description]
  // The aligned clause declares that the object to which each list item points
  // is aligned to the number of bytes expressed in the optional parameter of
  // the aligned clause.
  // The special this pointer can be used as if was one of the arguments to the
  // function in any of the linear, aligned, or uniform clauses.
  // The type of list items appearing in the aligned clause must be array,
  // pointer, reference to array, or reference to pointer.
  llvm::DenseMap<const Decl *, const Expr *> AlignedArgs;
  const Expr *AlignedThis = nullptr;
  for (const Expr *E : Aligneds) {
    E = E->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
        const VarDecl *CanonPVD = PVD->getCanonicalDecl();
        if (FD->getNumParams() > PVD->getFunctionScopeIndex() &&
            FD->getParamDecl(PVD->getFunctionScopeIndex())
                    ->getCanonicalDecl() == CanonPVD) {
          // OpenMP  [2.8.1, simd construct, Restrictions]
          // A list-item cannot appear in more than one aligned clause.
          if (AlignedArgs.count(CanonPVD) > 0) {
            Diag(E->getExprLoc(), diag::err_omp_used_in_clause_twice)
                << 1 << getOpenMPClauseName(OMPC_aligned)
                << E->getSourceRange();
            Diag(AlignedArgs[CanonPVD]->getExprLoc(),
                 diag::note_omp_explicit_dsa)
                << getOpenMPClauseName(OMPC_aligned);
            continue;
          }
          AlignedArgs[CanonPVD] = E;
          QualType QTy = PVD->getType()
                             .getNonReferenceType()
                             .getUnqualifiedType()
                             .getCanonicalType();
          const Type *Ty = QTy.getTypePtrOrNull();
          if (!Ty || (!Ty->isArrayType() && !Ty->isPointerType())) {
            Diag(E->getExprLoc(), diag::err_omp_aligned_expected_array_or_ptr)
                << QTy << getLangOpts().CPlusPlus << E->getSourceRange();
            Diag(PVD->getLocation(), diag::note_previous_decl) << PVD;
          }
          continue;
        }
      }
    if (isa<CXXThisExpr>(E)) {
      if (AlignedThis) {
        Diag(E->getExprLoc(), diag::err_omp_used_in_clause_twice)
            << 2 << getOpenMPClauseName(OMPC_aligned) << E->getSourceRange();
        Diag(AlignedThis->getExprLoc(), diag::note_omp_explicit_dsa)
            << getOpenMPClauseName(OMPC_aligned);
      }
      AlignedThis = E;
      continue;
    }
    Diag(E->getExprLoc(), diag::err_omp_param_or_this_in_clause)
        << FD->getDeclName() << (isa<CXXMethodDecl>(ADecl) ? 1 : 0);
  }
  // The optional parameter of the aligned clause, alignment, must be a constant
  // positive integer expression. If no optional parameter is specified,
  // implementation-defined default alignments for SIMD instructions on the
  // target platforms are assumed.
  SmallVector<const Expr *, 4> NewAligns;
  for (Expr *E : Alignments) {
    ExprResult Align;
    if (E)
      Align = VerifyPositiveIntegerConstantInClause(E, OMPC_aligned);
    NewAligns.push_back(Align.get());
  }
  // OpenMP [2.8.2, declare simd construct, Description]
  // The linear clause declares one or more list items to be private to a SIMD
  // lane and to have a linear relationship with respect to the iteration space
  // of a loop.
  // The special this pointer can be used as if was one of the arguments to the
  // function in any of the linear, aligned, or uniform clauses.
  // When a linear-step expression is specified in a linear clause it must be
  // either a constant integer expression or an integer-typed parameter that is
  // specified in a uniform clause on the directive.
  llvm::DenseMap<const Decl *, const Expr *> LinearArgs;
  const bool IsUniformedThis = UniformedLinearThis != nullptr;
  auto MI = LinModifiers.begin();
  for (const Expr *E : Linears) {
    auto LinKind = static_cast<OpenMPLinearClauseKind>(*MI);
    ++MI;
    E = E->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
        const VarDecl *CanonPVD = PVD->getCanonicalDecl();
        if (FD->getNumParams() > PVD->getFunctionScopeIndex() &&
            FD->getParamDecl(PVD->getFunctionScopeIndex())
                    ->getCanonicalDecl() == CanonPVD) {
          // OpenMP  [2.15.3.7, linear Clause, Restrictions]
          // A list-item cannot appear in more than one linear clause.
          if (LinearArgs.count(CanonPVD) > 0) {
            Diag(E->getExprLoc(), diag::err_omp_wrong_dsa)
                << getOpenMPClauseName(OMPC_linear)
                << getOpenMPClauseName(OMPC_linear) << E->getSourceRange();
            Diag(LinearArgs[CanonPVD]->getExprLoc(),
                 diag::note_omp_explicit_dsa)
                << getOpenMPClauseName(OMPC_linear);
            continue;
          }
          // Each argument can appear in at most one uniform or linear clause.
          if (UniformedArgs.count(CanonPVD) > 0) {
            Diag(E->getExprLoc(), diag::err_omp_wrong_dsa)
                << getOpenMPClauseName(OMPC_linear)
                << getOpenMPClauseName(OMPC_uniform) << E->getSourceRange();
            Diag(UniformedArgs[CanonPVD]->getExprLoc(),
                 diag::note_omp_explicit_dsa)
                << getOpenMPClauseName(OMPC_uniform);
            continue;
          }
          LinearArgs[CanonPVD] = E;
          if (E->isValueDependent() || E->isTypeDependent() ||
              E->isInstantiationDependent() ||
              E->containsUnexpandedParameterPack())
            continue;
          (void)CheckOpenMPLinearDecl(CanonPVD, E->getExprLoc(), LinKind,
                                      PVD->getOriginalType(),
                                      /*IsDeclareSimd=*/true);
          continue;
        }
      }
    if (isa<CXXThisExpr>(E)) {
      if (UniformedLinearThis) {
        Diag(E->getExprLoc(), diag::err_omp_wrong_dsa)
            << getOpenMPClauseName(OMPC_linear)
            << getOpenMPClauseName(IsUniformedThis ? OMPC_uniform : OMPC_linear)
            << E->getSourceRange();
        Diag(UniformedLinearThis->getExprLoc(), diag::note_omp_explicit_dsa)
            << getOpenMPClauseName(IsUniformedThis ? OMPC_uniform
                                                   : OMPC_linear);
        continue;
      }
      UniformedLinearThis = E;
      if (E->isValueDependent() || E->isTypeDependent() ||
          E->isInstantiationDependent() || E->containsUnexpandedParameterPack())
        continue;
      (void)CheckOpenMPLinearDecl(/*D=*/nullptr, E->getExprLoc(), LinKind,
                                  E->getType(), /*IsDeclareSimd=*/true);
      continue;
    }
    Diag(E->getExprLoc(), diag::err_omp_param_or_this_in_clause)
        << FD->getDeclName() << (isa<CXXMethodDecl>(ADecl) ? 1 : 0);
  }
  Expr *Step = nullptr;
  Expr *NewStep = nullptr;
  SmallVector<Expr *, 4> NewSteps;
  for (Expr *E : Steps) {
    // Skip the same step expression, it was checked already.
    if (Step == E || !E) {
      NewSteps.push_back(E ? NewStep : nullptr);
      continue;
    }
    Step = E;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Step))
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
        const VarDecl *CanonPVD = PVD->getCanonicalDecl();
        if (UniformedArgs.count(CanonPVD) == 0) {
          Diag(Step->getExprLoc(), diag::err_omp_expected_uniform_param)
              << Step->getSourceRange();
        } else if (E->isValueDependent() || E->isTypeDependent() ||
                   E->isInstantiationDependent() ||
                   E->containsUnexpandedParameterPack() ||
                   CanonPVD->getType()->hasIntegerRepresentation()) {
          NewSteps.push_back(Step);
        } else {
          Diag(Step->getExprLoc(), diag::err_omp_expected_int_param)
              << Step->getSourceRange();
        }
        continue;
      }
    NewStep = Step;
    if (Step && !Step->isValueDependent() && !Step->isTypeDependent() &&
        !Step->isInstantiationDependent() &&
        !Step->containsUnexpandedParameterPack()) {
      NewStep = PerformOpenMPImplicitIntegerConversion(Step->getExprLoc(), Step)
                    .get();
      if (NewStep)
        NewStep = SemaRef
                      .VerifyIntegerConstantExpression(
                          NewStep, /*FIXME*/ Sema::AllowFold)
                      .get();
    }
    NewSteps.push_back(NewStep);
  }
  auto *NewAttr = OMPDeclareSimdDeclAttr::CreateImplicit(
      getASTContext(), BS, SL.get(), const_cast<Expr **>(Uniforms.data()),
      Uniforms.size(), const_cast<Expr **>(Aligneds.data()), Aligneds.size(),
      const_cast<Expr **>(NewAligns.data()), NewAligns.size(),
      const_cast<Expr **>(Linears.data()), Linears.size(),
      const_cast<unsigned *>(LinModifiers.data()), LinModifiers.size(),
      NewSteps.data(), NewSteps.size(), SR);
  ADecl->addAttr(NewAttr);
  return DG;
}

static void setPrototype(Sema &S, FunctionDecl *FD, FunctionDecl *FDWithProto,
                         QualType NewType) {
  assert(NewType->isFunctionProtoType() &&
         "Expected function type with prototype.");
  assert(FD->getType()->isFunctionNoProtoType() &&
         "Expected function with type with no prototype.");
  assert(FDWithProto->getType()->isFunctionProtoType() &&
         "Expected function with prototype.");
  // Synthesize parameters with the same types.
  FD->setType(NewType);
  SmallVector<ParmVarDecl *, 16> Params;
  for (const ParmVarDecl *P : FDWithProto->parameters()) {
    auto *Param = ParmVarDecl::Create(S.getASTContext(), FD, SourceLocation(),
                                      SourceLocation(), nullptr, P->getType(),
                                      /*TInfo=*/nullptr, SC_None, nullptr);
    Param->setScopeInfo(0, Params.size());
    Param->setImplicit();
    Params.push_back(Param);
  }

  FD->setParams(Params);
}

void SemaOpenMP::ActOnFinishedFunctionDefinitionInOpenMPAssumeScope(Decl *D) {
  if (D->isInvalidDecl())
    return;
  FunctionDecl *FD = nullptr;
  if (auto *UTemplDecl = dyn_cast<FunctionTemplateDecl>(D))
    FD = UTemplDecl->getTemplatedDecl();
  else
    FD = cast<FunctionDecl>(D);
  assert(FD && "Expected a function declaration!");

  // If we are instantiating templates we do *not* apply scoped assumptions but
  // only global ones. We apply scoped assumption to the template definition
  // though.
  if (!SemaRef.inTemplateInstantiation()) {
    for (OMPAssumeAttr *AA : OMPAssumeScoped)
      FD->addAttr(AA);
  }
  for (OMPAssumeAttr *AA : OMPAssumeGlobal)
    FD->addAttr(AA);
}

SemaOpenMP::OMPDeclareVariantScope::OMPDeclareVariantScope(OMPTraitInfo &TI)
    : TI(&TI), NameSuffix(TI.getMangledName()) {}

void SemaOpenMP::ActOnStartOfFunctionDefinitionInOpenMPDeclareVariantScope(
    Scope *S, Declarator &D, MultiTemplateParamsArg TemplateParamLists,
    SmallVectorImpl<FunctionDecl *> &Bases) {
  if (!D.getIdentifier())
    return;

  OMPDeclareVariantScope &DVScope = OMPDeclareVariantScopes.back();

  // Template specialization is an extension, check if we do it.
  bool IsTemplated = !TemplateParamLists.empty();
  if (IsTemplated &&
      !DVScope.TI->isExtensionActive(
          llvm::omp::TraitProperty::implementation_extension_allow_templates))
    return;

  const IdentifierInfo *BaseII = D.getIdentifier();
  LookupResult Lookup(SemaRef, DeclarationName(BaseII), D.getIdentifierLoc(),
                      Sema::LookupOrdinaryName);
  SemaRef.LookupParsedName(Lookup, S, &D.getCXXScopeSpec(),
                           /*ObjectType=*/QualType());

  TypeSourceInfo *TInfo = SemaRef.GetTypeForDeclarator(D);
  QualType FType = TInfo->getType();

  bool IsConstexpr =
      D.getDeclSpec().getConstexprSpecifier() == ConstexprSpecKind::Constexpr;
  bool IsConsteval =
      D.getDeclSpec().getConstexprSpecifier() == ConstexprSpecKind::Consteval;

  for (auto *Candidate : Lookup) {
    auto *CandidateDecl = Candidate->getUnderlyingDecl();
    FunctionDecl *UDecl = nullptr;
    if (IsTemplated && isa<FunctionTemplateDecl>(CandidateDecl)) {
      auto *FTD = cast<FunctionTemplateDecl>(CandidateDecl);
      if (FTD->getTemplateParameters()->size() == TemplateParamLists.size())
        UDecl = FTD->getTemplatedDecl();
    } else if (!IsTemplated)
      UDecl = dyn_cast<FunctionDecl>(CandidateDecl);
    if (!UDecl)
      continue;

    // Don't specialize constexpr/consteval functions with
    // non-constexpr/consteval functions.
    if (UDecl->isConstexpr() && !IsConstexpr)
      continue;
    if (UDecl->isConsteval() && !IsConsteval)
      continue;

    QualType UDeclTy = UDecl->getType();
    if (!UDeclTy->isDependentType()) {
      QualType NewType = getASTContext().mergeFunctionTypes(
          FType, UDeclTy, /* OfBlockPointer */ false,
          /* Unqualified */ false, /* AllowCXX */ true);
      if (NewType.isNull())
        continue;
    }

    // Found a base!
    Bases.push_back(UDecl);
  }

  bool UseImplicitBase = !DVScope.TI->isExtensionActive(
      llvm::omp::TraitProperty::implementation_extension_disable_implicit_base);
  // If no base was found we create a declaration that we use as base.
  if (Bases.empty() && UseImplicitBase) {
    D.setFunctionDefinitionKind(FunctionDefinitionKind::Declaration);
    Decl *BaseD = SemaRef.HandleDeclarator(S, D, TemplateParamLists);
    BaseD->setImplicit(true);
    if (auto *BaseTemplD = dyn_cast<FunctionTemplateDecl>(BaseD))
      Bases.push_back(BaseTemplD->getTemplatedDecl());
    else
      Bases.push_back(cast<FunctionDecl>(BaseD));
  }

  std::string MangledName;
  MangledName += D.getIdentifier()->getName();
  MangledName += getOpenMPVariantManglingSeparatorStr();
  MangledName += DVScope.NameSuffix;
  IdentifierInfo &VariantII = getASTContext().Idents.get(MangledName);

  VariantII.setMangledOpenMPVariantName(true);
  D.SetIdentifier(&VariantII, D.getBeginLoc());
}

void SemaOpenMP::ActOnFinishedFunctionDefinitionInOpenMPDeclareVariantScope(
    Decl *D, SmallVectorImpl<FunctionDecl *> &Bases) {
  // Do not mark function as is used to prevent its emission if this is the
  // only place where it is used.
  EnterExpressionEvaluationContext Unevaluated(
      SemaRef, Sema::ExpressionEvaluationContext::Unevaluated);

  FunctionDecl *FD = nullptr;
  if (auto *UTemplDecl = dyn_cast<FunctionTemplateDecl>(D))
    FD = UTemplDecl->getTemplatedDecl();
  else
    FD = cast<FunctionDecl>(D);
  auto *VariantFuncRef = DeclRefExpr::Create(
      getASTContext(), NestedNameSpecifierLoc(), SourceLocation(), FD,
      /* RefersToEnclosingVariableOrCapture */ false,
      /* NameLoc */ FD->getLocation(), FD->getType(),
      ExprValueKind::VK_PRValue);

  OMPDeclareVariantScope &DVScope = OMPDeclareVariantScopes.back();
  auto *OMPDeclareVariantA = OMPDeclareVariantAttr::CreateImplicit(
      getASTContext(), VariantFuncRef, DVScope.TI,
      /*NothingArgs=*/nullptr, /*NothingArgsSize=*/0,
      /*NeedDevicePtrArgs=*/nullptr, /*NeedDevicePtrArgsSize=*/0,
      /*AppendArgs=*/nullptr, /*AppendArgsSize=*/0);
  for (FunctionDecl *BaseFD : Bases)
    BaseFD->addAttr(OMPDeclareVariantA);
}

ExprResult SemaOpenMP::ActOnOpenMPCall(ExprResult Call, Scope *Scope,
                                       SourceLocation LParenLoc,
                                       MultiExprArg ArgExprs,
                                       SourceLocation RParenLoc,
                                       Expr *ExecConfig) {
  // The common case is a regular call we do not want to specialize at all. Try
  // to make that case fast by bailing early.
  CallExpr *CE = dyn_cast<CallExpr>(Call.get());
  if (!CE)
    return Call;

  FunctionDecl *CalleeFnDecl = CE->getDirectCallee();
  if (!CalleeFnDecl)
    return Call;

  if (getLangOpts().OpenMP >= 51 && CalleeFnDecl->getIdentifier() &&
      CalleeFnDecl->getName().starts_with_insensitive("omp_")) {
    // checking for any calls inside an Order region
    if (Scope && Scope->isOpenMPOrderClauseScope())
      Diag(LParenLoc, diag::err_omp_unexpected_call_to_omp_runtime_api);
  }

  if (!CalleeFnDecl->hasAttr<OMPDeclareVariantAttr>())
    return Call;

  ASTContext &Context = getASTContext();
  std::function<void(StringRef)> DiagUnknownTrait = [this,
                                                     CE](StringRef ISATrait) {
    // TODO Track the selector locations in a way that is accessible here to
    // improve the diagnostic location.
    Diag(CE->getBeginLoc(), diag::warn_unknown_declare_variant_isa_trait)
        << ISATrait;
  };
  TargetOMPContext OMPCtx(Context, std::move(DiagUnknownTrait),
                          SemaRef.getCurFunctionDecl(),
                          DSAStack->getConstructTraits());

  QualType CalleeFnType = CalleeFnDecl->getType();

  SmallVector<Expr *, 4> Exprs;
  SmallVector<VariantMatchInfo, 4> VMIs;
  while (CalleeFnDecl) {
    for (OMPDeclareVariantAttr *A :
         CalleeFnDecl->specific_attrs<OMPDeclareVariantAttr>()) {
      Expr *VariantRef = A->getVariantFuncRef();

      VariantMatchInfo VMI;
      OMPTraitInfo &TI = A->getTraitInfo();
      TI.getAsVariantMatchInfo(Context, VMI);
      if (!isVariantApplicableInContext(VMI, OMPCtx,
                                        /* DeviceSetOnly */ false))
        continue;

      VMIs.push_back(VMI);
      Exprs.push_back(VariantRef);
    }

    CalleeFnDecl = CalleeFnDecl->getPreviousDecl();
  }

  ExprResult NewCall;
  do {
    int BestIdx = getBestVariantMatchForContext(VMIs, OMPCtx);
    if (BestIdx < 0)
      return Call;
    Expr *BestExpr = cast<DeclRefExpr>(Exprs[BestIdx]);
    Decl *BestDecl = cast<DeclRefExpr>(BestExpr)->getDecl();

    {
      // Try to build a (member) call expression for the current best applicable
      // variant expression. We allow this to fail in which case we continue
      // with the next best variant expression. The fail case is part of the
      // implementation defined behavior in the OpenMP standard when it talks
      // about what differences in the function prototypes: "Any differences
      // that the specific OpenMP context requires in the prototype of the
      // variant from the base function prototype are implementation defined."
      // This wording is there to allow the specialized variant to have a
      // different type than the base function. This is intended and OK but if
      // we cannot create a call the difference is not in the "implementation
      // defined range" we allow.
      Sema::TentativeAnalysisScope Trap(SemaRef);

      if (auto *SpecializedMethod = dyn_cast<CXXMethodDecl>(BestDecl)) {
        auto *MemberCall = dyn_cast<CXXMemberCallExpr>(CE);
        BestExpr = MemberExpr::CreateImplicit(
            Context, MemberCall->getImplicitObjectArgument(),
            /* IsArrow */ false, SpecializedMethod, Context.BoundMemberTy,
            MemberCall->getValueKind(), MemberCall->getObjectKind());
      }
      NewCall = SemaRef.BuildCallExpr(Scope, BestExpr, LParenLoc, ArgExprs,
                                      RParenLoc, ExecConfig);
      if (NewCall.isUsable()) {
        if (CallExpr *NCE = dyn_cast<CallExpr>(NewCall.get())) {
          FunctionDecl *NewCalleeFnDecl = NCE->getDirectCallee();
          QualType NewType = getASTContext().mergeFunctionTypes(
              CalleeFnType, NewCalleeFnDecl->getType(),
              /* OfBlockPointer */ false,
              /* Unqualified */ false, /* AllowCXX */ true);
          if (!NewType.isNull())
            break;
          // Don't use the call if the function type was not compatible.
          NewCall = nullptr;
        }
      }
    }

    VMIs.erase(VMIs.begin() + BestIdx);
    Exprs.erase(Exprs.begin() + BestIdx);
  } while (!VMIs.empty());

  if (!NewCall.isUsable())
    return Call;
  return PseudoObjectExpr::Create(getASTContext(), CE, {NewCall.get()}, 0);
}

std::optional<std::pair<FunctionDecl *, Expr *>>
SemaOpenMP::checkOpenMPDeclareVariantFunction(SemaOpenMP::DeclGroupPtrTy DG,
                                              Expr *VariantRef,
                                              OMPTraitInfo &TI,
                                              unsigned NumAppendArgs,
                                              SourceRange SR) {
  ASTContext &Context = getASTContext();
  if (!DG || DG.get().isNull())
    return std::nullopt;

  const int VariantId = 1;
  // Must be applied only to single decl.
  if (!DG.get().isSingleDecl()) {
    Diag(SR.getBegin(), diag::err_omp_single_decl_in_declare_simd_variant)
        << VariantId << SR;
    return std::nullopt;
  }
  Decl *ADecl = DG.get().getSingleDecl();
  if (auto *FTD = dyn_cast<FunctionTemplateDecl>(ADecl))
    ADecl = FTD->getTemplatedDecl();

  // Decl must be a function.
  auto *FD = dyn_cast<FunctionDecl>(ADecl);
  if (!FD) {
    Diag(ADecl->getLocation(), diag::err_omp_function_expected)
        << VariantId << SR;
    return std::nullopt;
  }

  auto &&HasMultiVersionAttributes = [](const FunctionDecl *FD) {
    // The 'target' attribute needs to be separately checked because it does
    // not always signify a multiversion function declaration.
    return FD->isMultiVersion() || FD->hasAttr<TargetAttr>();
  };
  // OpenMP is not compatible with multiversion function attributes.
  if (HasMultiVersionAttributes(FD)) {
    Diag(FD->getLocation(), diag::err_omp_declare_variant_incompat_attributes)
        << SR;
    return std::nullopt;
  }

  // Allow #pragma omp declare variant only if the function is not used.
  if (FD->isUsed(false))
    Diag(SR.getBegin(), diag::warn_omp_declare_variant_after_used)
        << FD->getLocation();

  // Check if the function was emitted already.
  const FunctionDecl *Definition;
  if (!FD->isThisDeclarationADefinition() && FD->isDefined(Definition) &&
      (getLangOpts().EmitAllDecls || Context.DeclMustBeEmitted(Definition)))
    Diag(SR.getBegin(), diag::warn_omp_declare_variant_after_emitted)
        << FD->getLocation();

  // The VariantRef must point to function.
  if (!VariantRef) {
    Diag(SR.getBegin(), diag::err_omp_function_expected) << VariantId;
    return std::nullopt;
  }

  auto ShouldDelayChecks = [](Expr *&E, bool) {
    return E && (E->isTypeDependent() || E->isValueDependent() ||
                 E->containsUnexpandedParameterPack() ||
                 E->isInstantiationDependent());
  };
  // Do not check templates, wait until instantiation.
  if (FD->isDependentContext() || ShouldDelayChecks(VariantRef, false) ||
      TI.anyScoreOrCondition(ShouldDelayChecks))
    return std::make_pair(FD, VariantRef);

  // Deal with non-constant score and user condition expressions.
  auto HandleNonConstantScoresAndConditions = [this](Expr *&E,
                                                     bool IsScore) -> bool {
    if (!E || E->isIntegerConstantExpr(getASTContext()))
      return false;

    if (IsScore) {
      // We warn on non-constant scores and pretend they were not present.
      Diag(E->getExprLoc(), diag::warn_omp_declare_variant_score_not_constant)
          << E;
      E = nullptr;
    } else {
      // We could replace a non-constant user condition with "false" but we
      // will soon need to handle these anyway for the dynamic version of
      // OpenMP context selectors.
      Diag(E->getExprLoc(),
           diag::err_omp_declare_variant_user_condition_not_constant)
          << E;
    }
    return true;
  };
  if (TI.anyScoreOrCondition(HandleNonConstantScoresAndConditions))
    return std::nullopt;

  QualType AdjustedFnType = FD->getType();
  if (NumAppendArgs) {
    const auto *PTy = AdjustedFnType->getAsAdjusted<FunctionProtoType>();
    if (!PTy) {
      Diag(FD->getLocation(), diag::err_omp_declare_variant_prototype_required)
          << SR;
      return std::nullopt;
    }
    // Adjust the function type to account for an extra omp_interop_t for each
    // specified in the append_args clause.
    const TypeDecl *TD = nullptr;
    LookupResult Result(SemaRef, &Context.Idents.get("omp_interop_t"),
                        SR.getBegin(), Sema::LookupOrdinaryName);
    if (SemaRef.LookupName(Result, SemaRef.getCurScope())) {
      NamedDecl *ND = Result.getFoundDecl();
      TD = dyn_cast_or_null<TypeDecl>(ND);
    }
    if (!TD) {
      Diag(SR.getBegin(), diag::err_omp_interop_type_not_found) << SR;
      return std::nullopt;
    }
    QualType InteropType = Context.getTypeDeclType(TD);
    if (PTy->isVariadic()) {
      Diag(FD->getLocation(), diag::err_omp_append_args_with_varargs) << SR;
      return std::nullopt;
    }
    llvm::SmallVector<QualType, 8> Params;
    Params.append(PTy->param_type_begin(), PTy->param_type_end());
    Params.insert(Params.end(), NumAppendArgs, InteropType);
    AdjustedFnType = Context.getFunctionType(PTy->getReturnType(), Params,
                                             PTy->getExtProtoInfo());
  }

  // Convert VariantRef expression to the type of the original function to
  // resolve possible conflicts.
  ExprResult VariantRefCast = VariantRef;
  if (getLangOpts().CPlusPlus) {
    QualType FnPtrType;
    auto *Method = dyn_cast<CXXMethodDecl>(FD);
    if (Method && !Method->isStatic()) {
      const Type *ClassType =
          Context.getTypeDeclType(Method->getParent()).getTypePtr();
      FnPtrType = Context.getMemberPointerType(AdjustedFnType, ClassType);
      ExprResult ER;
      {
        // Build addr_of unary op to correctly handle type checks for member
        // functions.
        Sema::TentativeAnalysisScope Trap(SemaRef);
        ER = SemaRef.CreateBuiltinUnaryOp(VariantRef->getBeginLoc(), UO_AddrOf,
                                          VariantRef);
      }
      if (!ER.isUsable()) {
        Diag(VariantRef->getExprLoc(), diag::err_omp_function_expected)
            << VariantId << VariantRef->getSourceRange();
        return std::nullopt;
      }
      VariantRef = ER.get();
    } else {
      FnPtrType = Context.getPointerType(AdjustedFnType);
    }
    QualType VarianPtrType = Context.getPointerType(VariantRef->getType());
    if (VarianPtrType.getUnqualifiedType() != FnPtrType.getUnqualifiedType()) {
      ImplicitConversionSequence ICS = SemaRef.TryImplicitConversion(
          VariantRef, FnPtrType.getUnqualifiedType(),
          /*SuppressUserConversions=*/false, Sema::AllowedExplicit::None,
          /*InOverloadResolution=*/false,
          /*CStyle=*/false,
          /*AllowObjCWritebackConversion=*/false);
      if (ICS.isFailure()) {
        Diag(VariantRef->getExprLoc(),
             diag::err_omp_declare_variant_incompat_types)
            << VariantRef->getType()
            << ((Method && !Method->isStatic()) ? FnPtrType : FD->getType())
            << (NumAppendArgs ? 1 : 0) << VariantRef->getSourceRange();
        return std::nullopt;
      }
      VariantRefCast = SemaRef.PerformImplicitConversion(
          VariantRef, FnPtrType.getUnqualifiedType(), Sema::AA_Converting);
      if (!VariantRefCast.isUsable())
        return std::nullopt;
    }
    // Drop previously built artificial addr_of unary op for member functions.
    if (Method && !Method->isStatic()) {
      Expr *PossibleAddrOfVariantRef = VariantRefCast.get();
      if (auto *UO = dyn_cast<UnaryOperator>(
              PossibleAddrOfVariantRef->IgnoreImplicit()))
        VariantRefCast = UO->getSubExpr();
    }
  }

  ExprResult ER = SemaRef.CheckPlaceholderExpr(VariantRefCast.get());
  if (!ER.isUsable() ||
      !ER.get()->IgnoreParenImpCasts()->getType()->isFunctionType()) {
    Diag(VariantRef->getExprLoc(), diag::err_omp_function_expected)
        << VariantId << VariantRef->getSourceRange();
    return std::nullopt;
  }

  // The VariantRef must point to function.
  auto *DRE = dyn_cast<DeclRefExpr>(ER.get()->IgnoreParenImpCasts());
  if (!DRE) {
    Diag(VariantRef->getExprLoc(), diag::err_omp_function_expected)
        << VariantId << VariantRef->getSourceRange();
    return std::nullopt;
  }
  auto *NewFD = dyn_cast_or_null<FunctionDecl>(DRE->getDecl());
  if (!NewFD) {
    Diag(VariantRef->getExprLoc(), diag::err_omp_function_expected)
        << VariantId << VariantRef->getSourceRange();
    return std::nullopt;
  }

  if (FD->getCanonicalDecl() == NewFD->getCanonicalDecl()) {
    Diag(VariantRef->getExprLoc(),
         diag::err_omp_declare_variant_same_base_function)
        << VariantRef->getSourceRange();
    return std::nullopt;
  }

  // Check if function types are compatible in C.
  if (!getLangOpts().CPlusPlus) {
    QualType NewType =
        Context.mergeFunctionTypes(AdjustedFnType, NewFD->getType());
    if (NewType.isNull()) {
      Diag(VariantRef->getExprLoc(),
           diag::err_omp_declare_variant_incompat_types)
          << NewFD->getType() << FD->getType() << (NumAppendArgs ? 1 : 0)
          << VariantRef->getSourceRange();
      return std::nullopt;
    }
    if (NewType->isFunctionProtoType()) {
      if (FD->getType()->isFunctionNoProtoType())
        setPrototype(SemaRef, FD, NewFD, NewType);
      else if (NewFD->getType()->isFunctionNoProtoType())
        setPrototype(SemaRef, NewFD, FD, NewType);
    }
  }

  // Check if variant function is not marked with declare variant directive.
  if (NewFD->hasAttrs() && NewFD->hasAttr<OMPDeclareVariantAttr>()) {
    Diag(VariantRef->getExprLoc(),
         diag::warn_omp_declare_variant_marked_as_declare_variant)
        << VariantRef->getSourceRange();
    SourceRange SR =
        NewFD->specific_attr_begin<OMPDeclareVariantAttr>()->getRange();
    Diag(SR.getBegin(), diag::note_omp_marked_declare_variant_here) << SR;
    return std::nullopt;
  }

  enum DoesntSupport {
    VirtFuncs = 1,
    Constructors = 3,
    Destructors = 4,
    DeletedFuncs = 5,
    DefaultedFuncs = 6,
    ConstexprFuncs = 7,
    ConstevalFuncs = 8,
  };
  if (const auto *CXXFD = dyn_cast<CXXMethodDecl>(FD)) {
    if (CXXFD->isVirtual()) {
      Diag(FD->getLocation(), diag::err_omp_declare_variant_doesnt_support)
          << VirtFuncs;
      return std::nullopt;
    }

    if (isa<CXXConstructorDecl>(FD)) {
      Diag(FD->getLocation(), diag::err_omp_declare_variant_doesnt_support)
          << Constructors;
      return std::nullopt;
    }

    if (isa<CXXDestructorDecl>(FD)) {
      Diag(FD->getLocation(), diag::err_omp_declare_variant_doesnt_support)
          << Destructors;
      return std::nullopt;
    }
  }

  if (FD->isDeleted()) {
    Diag(FD->getLocation(), diag::err_omp_declare_variant_doesnt_support)
        << DeletedFuncs;
    return std::nullopt;
  }

  if (FD->isDefaulted()) {
    Diag(FD->getLocation(), diag::err_omp_declare_variant_doesnt_support)
        << DefaultedFuncs;
    return std::nullopt;
  }

  if (FD->isConstexpr()) {
    Diag(FD->getLocation(), diag::err_omp_declare_variant_doesnt_support)
        << (NewFD->isConsteval() ? ConstevalFuncs : ConstexprFuncs);
    return std::nullopt;
  }

  // Check general compatibility.
  if (SemaRef.areMultiversionVariantFunctionsCompatible(
          FD, NewFD, PartialDiagnostic::NullDiagnostic(),
          PartialDiagnosticAt(SourceLocation(),
                              PartialDiagnostic::NullDiagnostic()),
          PartialDiagnosticAt(
              VariantRef->getExprLoc(),
              PDiag(diag::err_omp_declare_variant_doesnt_support)),
          PartialDiagnosticAt(VariantRef->getExprLoc(),
                              PDiag(diag::err_omp_declare_variant_diff)
                                  << FD->getLocation()),
          /*TemplatesSupported=*/true, /*ConstexprSupported=*/false,
          /*CLinkageMayDiffer=*/true))
    return std::nullopt;
  return std::make_pair(FD, cast<Expr>(DRE));
}

void SemaOpenMP::ActOnOpenMPDeclareVariantDirective(
    FunctionDecl *FD, Expr *VariantRef, OMPTraitInfo &TI,
    ArrayRef<Expr *> AdjustArgsNothing,
    ArrayRef<Expr *> AdjustArgsNeedDevicePtr,
    ArrayRef<OMPInteropInfo> AppendArgs, SourceLocation AdjustArgsLoc,
    SourceLocation AppendArgsLoc, SourceRange SR) {

  // OpenMP 5.1 [2.3.5, declare variant directive, Restrictions]
  // An adjust_args clause or append_args clause can only be specified if the
  // dispatch selector of the construct selector set appears in the match
  // clause.

  SmallVector<Expr *, 8> AllAdjustArgs;
  llvm::append_range(AllAdjustArgs, AdjustArgsNothing);
  llvm::append_range(AllAdjustArgs, AdjustArgsNeedDevicePtr);

  if (!AllAdjustArgs.empty() || !AppendArgs.empty()) {
    VariantMatchInfo VMI;
    TI.getAsVariantMatchInfo(getASTContext(), VMI);
    if (!llvm::is_contained(
            VMI.ConstructTraits,
            llvm::omp::TraitProperty::construct_dispatch_dispatch)) {
      if (!AllAdjustArgs.empty())
        Diag(AdjustArgsLoc, diag::err_omp_clause_requires_dispatch_construct)
            << getOpenMPClauseName(OMPC_adjust_args);
      if (!AppendArgs.empty())
        Diag(AppendArgsLoc, diag::err_omp_clause_requires_dispatch_construct)
            << getOpenMPClauseName(OMPC_append_args);
      return;
    }
  }

  // OpenMP 5.1 [2.3.5, declare variant directive, Restrictions]
  // Each argument can only appear in a single adjust_args clause for each
  // declare variant directive.
  llvm::SmallPtrSet<const VarDecl *, 4> AdjustVars;

  for (Expr *E : AllAdjustArgs) {
    E = E->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
        const VarDecl *CanonPVD = PVD->getCanonicalDecl();
        if (FD->getNumParams() > PVD->getFunctionScopeIndex() &&
            FD->getParamDecl(PVD->getFunctionScopeIndex())
                    ->getCanonicalDecl() == CanonPVD) {
          // It's a parameter of the function, check duplicates.
          if (!AdjustVars.insert(CanonPVD).second) {
            Diag(DRE->getLocation(), diag::err_omp_adjust_arg_multiple_clauses)
                << PVD;
            return;
          }
          continue;
        }
      }
    }
    // Anything that is not a function parameter is an error.
    Diag(E->getExprLoc(), diag::err_omp_param_or_this_in_clause) << FD << 0;
    return;
  }

  auto *NewAttr = OMPDeclareVariantAttr::CreateImplicit(
      getASTContext(), VariantRef, &TI,
      const_cast<Expr **>(AdjustArgsNothing.data()), AdjustArgsNothing.size(),
      const_cast<Expr **>(AdjustArgsNeedDevicePtr.data()),
      AdjustArgsNeedDevicePtr.size(),
      const_cast<OMPInteropInfo *>(AppendArgs.data()), AppendArgs.size(), SR);
  FD->addAttr(NewAttr);
}

static CapturedStmt *
setBranchProtectedScope(Sema &SemaRef, OpenMPDirectiveKind DKind, Stmt *AStmt) {
  auto *CS = dyn_cast<CapturedStmt>(AStmt);
  assert(CS && "Captured statement expected");
  // 1.2.2 OpenMP Language Terminology
  // Structured block - An executable statement with a single entry at the
  // top and a single exit at the bottom.
  // The point of exit cannot be a branch out of the structured block.
  // longjmp() and throw() must not violate the entry/exit criteria.
  CS->getCapturedDecl()->setNothrow();

  for (int ThisCaptureLevel = SemaRef.OpenMP().getOpenMPCaptureLevels(DKind);
       ThisCaptureLevel > 1; --ThisCaptureLevel) {
    CS = cast<CapturedStmt>(CS->getCapturedStmt());
    // 1.2.2 OpenMP Language Terminology
    // Structured block - An executable statement with a single entry at the
    // top and a single exit at the bottom.
    // The point of exit cannot be a branch out of the structured block.
    // longjmp() and throw() must not violate the entry/exit criteria.
    CS->getCapturedDecl()->setNothrow();
  }
  SemaRef.setFunctionHasBranchProtectedScope();
  return CS;
}

StmtResult
SemaOpenMP::ActOnOpenMPParallelDirective(ArrayRef<OMPClause *> Clauses,
                                         Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_parallel, AStmt);

  return OMPParallelDirective::Create(
      getASTContext(), StartLoc, EndLoc, Clauses, AStmt,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

namespace {
/// Iteration space of a single for loop.
struct LoopIterationSpace final {
  /// True if the condition operator is the strict compare operator (<, > or
  /// !=).
  bool IsStrictCompare = false;
  /// Condition of the loop.
  Expr *PreCond = nullptr;
  /// This expression calculates the number of iterations in the loop.
  /// It is always possible to calculate it before starting the loop.
  Expr *NumIterations = nullptr;
  /// The loop counter variable.
  Expr *CounterVar = nullptr;
  /// Private loop counter variable.
  Expr *PrivateCounterVar = nullptr;
  /// This is initializer for the initial value of #CounterVar.
  Expr *CounterInit = nullptr;
  /// This is step for the #CounterVar used to generate its update:
  /// #CounterVar = #CounterInit + #CounterStep * CurrentIteration.
  Expr *CounterStep = nullptr;
  /// Should step be subtracted?
  bool Subtract = false;
  /// Source range of the loop init.
  SourceRange InitSrcRange;
  /// Source range of the loop condition.
  SourceRange CondSrcRange;
  /// Source range of the loop increment.
  SourceRange IncSrcRange;
  /// Minimum value that can have the loop control variable. Used to support
  /// non-rectangular loops. Applied only for LCV with the non-iterator types,
  /// since only such variables can be used in non-loop invariant expressions.
  Expr *MinValue = nullptr;
  /// Maximum value that can have the loop control variable. Used to support
  /// non-rectangular loops. Applied only for LCV with the non-iterator type,
  /// since only such variables can be used in non-loop invariant expressions.
  Expr *MaxValue = nullptr;
  /// true, if the lower bound depends on the outer loop control var.
  bool IsNonRectangularLB = false;
  /// true, if the upper bound depends on the outer loop control var.
  bool IsNonRectangularUB = false;
  /// Index of the loop this loop depends on and forms non-rectangular loop
  /// nest.
  unsigned LoopDependentIdx = 0;
  /// Final condition for the non-rectangular loop nest support. It is used to
  /// check that the number of iterations for this particular counter must be
  /// finished.
  Expr *FinalCondition = nullptr;
};

/// Helper class for checking canonical form of the OpenMP loops and
/// extracting iteration space of each loop in the loop nest, that will be used
/// for IR generation.
class OpenMPIterationSpaceChecker {
  /// Reference to Sema.
  Sema &SemaRef;
  /// Does the loop associated directive support non-rectangular loops?
  bool SupportsNonRectangular;
  /// Data-sharing stack.
  DSAStackTy &Stack;
  /// A location for diagnostics (when there is no some better location).
  SourceLocation DefaultLoc;
  /// A location for diagnostics (when increment is not compatible).
  SourceLocation ConditionLoc;
  /// A source location for referring to loop init later.
  SourceRange InitSrcRange;
  /// A source location for referring to condition later.
  SourceRange ConditionSrcRange;
  /// A source location for referring to increment later.
  SourceRange IncrementSrcRange;
  /// Loop variable.
  ValueDecl *LCDecl = nullptr;
  /// Reference to loop variable.
  Expr *LCRef = nullptr;
  /// Lower bound (initializer for the var).
  Expr *LB = nullptr;
  /// Upper bound.
  Expr *UB = nullptr;
  /// Loop step (increment).
  Expr *Step = nullptr;
  /// This flag is true when condition is one of:
  ///   Var <  UB
  ///   Var <= UB
  ///   UB  >  Var
  ///   UB  >= Var
  /// This will have no value when the condition is !=
  std::optional<bool> TestIsLessOp;
  /// This flag is true when condition is strict ( < or > ).
  bool TestIsStrictOp = false;
  /// This flag is true when step is subtracted on each iteration.
  bool SubtractStep = false;
  /// The outer loop counter this loop depends on (if any).
  const ValueDecl *DepDecl = nullptr;
  /// Contains number of loop (starts from 1) on which loop counter init
  /// expression of this loop depends on.
  std::optional<unsigned> InitDependOnLC;
  /// Contains number of loop (starts from 1) on which loop counter condition
  /// expression of this loop depends on.
  std::optional<unsigned> CondDependOnLC;
  /// Checks if the provide statement depends on the loop counter.
  std::optional<unsigned> doesDependOnLoopCounter(const Stmt *S,
                                                  bool IsInitializer);
  /// Original condition required for checking of the exit condition for
  /// non-rectangular loop.
  Expr *Condition = nullptr;

public:
  OpenMPIterationSpaceChecker(Sema &SemaRef, bool SupportsNonRectangular,
                              DSAStackTy &Stack, SourceLocation DefaultLoc)
      : SemaRef(SemaRef), SupportsNonRectangular(SupportsNonRectangular),
        Stack(Stack), DefaultLoc(DefaultLoc), ConditionLoc(DefaultLoc) {}
  /// Check init-expr for canonical loop form and save loop counter
  /// variable - #Var and its initialization value - #LB.
  bool checkAndSetInit(Stmt *S, bool EmitDiags = true);
  /// Check test-expr for canonical form, save upper-bound (#UB), flags
  /// for less/greater and for strict/non-strict comparison.
  bool checkAndSetCond(Expr *S);
  /// Check incr-expr for canonical loop form and return true if it
  /// does not conform, otherwise save loop step (#Step).
  bool checkAndSetInc(Expr *S);
  /// Return the loop counter variable.
  ValueDecl *getLoopDecl() const { return LCDecl; }
  /// Return the reference expression to loop counter variable.
  Expr *getLoopDeclRefExpr() const { return LCRef; }
  /// Source range of the loop init.
  SourceRange getInitSrcRange() const { return InitSrcRange; }
  /// Source range of the loop condition.
  SourceRange getConditionSrcRange() const { return ConditionSrcRange; }
  /// Source range of the loop increment.
  SourceRange getIncrementSrcRange() const { return IncrementSrcRange; }
  /// True if the step should be subtracted.
  bool shouldSubtractStep() const { return SubtractStep; }
  /// True, if the compare operator is strict (<, > or !=).
  bool isStrictTestOp() const { return TestIsStrictOp; }
  /// Build the expression to calculate the number of iterations.
  Expr *buildNumIterations(
      Scope *S, ArrayRef<LoopIterationSpace> ResultIterSpaces, bool LimitedType,
      llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) const;
  /// Build the precondition expression for the loops.
  Expr *
  buildPreCond(Scope *S, Expr *Cond,
               llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) const;
  /// Build reference expression to the counter be used for codegen.
  DeclRefExpr *
  buildCounterVar(llvm::MapVector<const Expr *, DeclRefExpr *> &Captures,
                  DSAStackTy &DSA) const;
  /// Build reference expression to the private counter be used for
  /// codegen.
  Expr *buildPrivateCounterVar() const;
  /// Build initialization of the counter be used for codegen.
  Expr *buildCounterInit() const;
  /// Build step of the counter be used for codegen.
  Expr *buildCounterStep() const;
  /// Build loop data with counter value for depend clauses in ordered
  /// directives.
  Expr *
  buildOrderedLoopData(Scope *S, Expr *Counter,
                       llvm::MapVector<const Expr *, DeclRefExpr *> &Captures,
                       SourceLocation Loc, Expr *Inc = nullptr,
                       OverloadedOperatorKind OOK = OO_Amp);
  /// Builds the minimum value for the loop counter.
  std::pair<Expr *, Expr *> buildMinMaxValues(
      Scope *S, llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) const;
  /// Builds final condition for the non-rectangular loops.
  Expr *buildFinalCondition(Scope *S) const;
  /// Return true if any expression is dependent.
  bool dependent() const;
  /// Returns true if the initializer forms non-rectangular loop.
  bool doesInitDependOnLC() const { return InitDependOnLC.has_value(); }
  /// Returns true if the condition forms non-rectangular loop.
  bool doesCondDependOnLC() const { return CondDependOnLC.has_value(); }
  /// Returns index of the loop we depend on (starting from 1), or 0 otherwise.
  unsigned getLoopDependentIdx() const {
    return InitDependOnLC.value_or(CondDependOnLC.value_or(0));
  }

private:
  /// Check the right-hand side of an assignment in the increment
  /// expression.
  bool checkAndSetIncRHS(Expr *RHS);
  /// Helper to set loop counter variable and its initializer.
  bool setLCDeclAndLB(ValueDecl *NewLCDecl, Expr *NewDeclRefExpr, Expr *NewLB,
                      bool EmitDiags);
  /// Helper to set upper bound.
  bool setUB(Expr *NewUB, std::optional<bool> LessOp, bool StrictOp,
             SourceRange SR, SourceLocation SL);
  /// Helper to set loop increment.
  bool setStep(Expr *NewStep, bool Subtract);
};

bool OpenMPIterationSpaceChecker::dependent() const {
  if (!LCDecl) {
    assert(!LB && !UB && !Step);
    return false;
  }
  return LCDecl->getType()->isDependentType() ||
         (LB && LB->isValueDependent()) || (UB && UB->isValueDependent()) ||
         (Step && Step->isValueDependent());
}

bool OpenMPIterationSpaceChecker::setLCDeclAndLB(ValueDecl *NewLCDecl,
                                                 Expr *NewLCRefExpr,
                                                 Expr *NewLB, bool EmitDiags) {
  // State consistency checking to ensure correct usage.
  assert(LCDecl == nullptr && LB == nullptr && LCRef == nullptr &&
         UB == nullptr && Step == nullptr && !TestIsLessOp && !TestIsStrictOp);
  if (!NewLCDecl || !NewLB || NewLB->containsErrors())
    return true;
  LCDecl = getCanonicalDecl(NewLCDecl);
  LCRef = NewLCRefExpr;
  if (auto *CE = dyn_cast_or_null<CXXConstructExpr>(NewLB))
    if (const CXXConstructorDecl *Ctor = CE->getConstructor())
      if ((Ctor->isCopyOrMoveConstructor() ||
           Ctor->isConvertingConstructor(/*AllowExplicit=*/false)) &&
          CE->getNumArgs() > 0 && CE->getArg(0) != nullptr)
        NewLB = CE->getArg(0)->IgnoreParenImpCasts();
  LB = NewLB;
  if (EmitDiags)
    InitDependOnLC = doesDependOnLoopCounter(LB, /*IsInitializer=*/true);
  return false;
}

bool OpenMPIterationSpaceChecker::setUB(Expr *NewUB, std::optional<bool> LessOp,
                                        bool StrictOp, SourceRange SR,
                                        SourceLocation SL) {
  // State consistency checking to ensure correct usage.
  assert(LCDecl != nullptr && LB != nullptr && UB == nullptr &&
         Step == nullptr && !TestIsLessOp && !TestIsStrictOp);
  if (!NewUB || NewUB->containsErrors())
    return true;
  UB = NewUB;
  if (LessOp)
    TestIsLessOp = LessOp;
  TestIsStrictOp = StrictOp;
  ConditionSrcRange = SR;
  ConditionLoc = SL;
  CondDependOnLC = doesDependOnLoopCounter(UB, /*IsInitializer=*/false);
  return false;
}

bool OpenMPIterationSpaceChecker::setStep(Expr *NewStep, bool Subtract) {
  // State consistency checking to ensure correct usage.
  assert(LCDecl != nullptr && LB != nullptr && Step == nullptr);
  if (!NewStep || NewStep->containsErrors())
    return true;
  if (!NewStep->isValueDependent()) {
    // Check that the step is integer expression.
    SourceLocation StepLoc = NewStep->getBeginLoc();
    ExprResult Val = SemaRef.OpenMP().PerformOpenMPImplicitIntegerConversion(
        StepLoc, getExprAsWritten(NewStep));
    if (Val.isInvalid())
      return true;
    NewStep = Val.get();

    // OpenMP [2.6, Canonical Loop Form, Restrictions]
    //  If test-expr is of form var relational-op b and relational-op is < or
    //  <= then incr-expr must cause var to increase on each iteration of the
    //  loop. If test-expr is of form var relational-op b and relational-op is
    //  > or >= then incr-expr must cause var to decrease on each iteration of
    //  the loop.
    //  If test-expr is of form b relational-op var and relational-op is < or
    //  <= then incr-expr must cause var to decrease on each iteration of the
    //  loop. If test-expr is of form b relational-op var and relational-op is
    //  > or >= then incr-expr must cause var to increase on each iteration of
    //  the loop.
    std::optional<llvm::APSInt> Result =
        NewStep->getIntegerConstantExpr(SemaRef.Context);
    bool IsUnsigned = !NewStep->getType()->hasSignedIntegerRepresentation();
    bool IsConstNeg =
        Result && Result->isSigned() && (Subtract != Result->isNegative());
    bool IsConstPos =
        Result && Result->isSigned() && (Subtract == Result->isNegative());
    bool IsConstZero = Result && !Result->getBoolValue();

    // != with increment is treated as <; != with decrement is treated as >
    if (!TestIsLessOp)
      TestIsLessOp = IsConstPos || (IsUnsigned && !Subtract);
    if (UB && (IsConstZero ||
               (*TestIsLessOp ? (IsConstNeg || (IsUnsigned && Subtract))
                              : (IsConstPos || (IsUnsigned && !Subtract))))) {
      SemaRef.Diag(NewStep->getExprLoc(),
                   diag::err_omp_loop_incr_not_compatible)
          << LCDecl << *TestIsLessOp << NewStep->getSourceRange();
      SemaRef.Diag(ConditionLoc,
                   diag::note_omp_loop_cond_requires_compatible_incr)
          << *TestIsLessOp << ConditionSrcRange;
      return true;
    }
    if (*TestIsLessOp == Subtract) {
      NewStep =
          SemaRef.CreateBuiltinUnaryOp(NewStep->getExprLoc(), UO_Minus, NewStep)
              .get();
      Subtract = !Subtract;
    }
  }

  Step = NewStep;
  SubtractStep = Subtract;
  return false;
}

namespace {
/// Checker for the non-rectangular loops. Checks if the initializer or
/// condition expression references loop counter variable.
class LoopCounterRefChecker final
    : public ConstStmtVisitor<LoopCounterRefChecker, bool> {
  Sema &SemaRef;
  DSAStackTy &Stack;
  const ValueDecl *CurLCDecl = nullptr;
  const ValueDecl *DepDecl = nullptr;
  const ValueDecl *PrevDepDecl = nullptr;
  bool IsInitializer = true;
  bool SupportsNonRectangular;
  unsigned BaseLoopId = 0;
  bool checkDecl(const Expr *E, const ValueDecl *VD) {
    if (getCanonicalDecl(VD) == getCanonicalDecl(CurLCDecl)) {
      SemaRef.Diag(E->getExprLoc(), diag::err_omp_stmt_depends_on_loop_counter)
          << (IsInitializer ? 0 : 1);
      return false;
    }
    const auto &&Data = Stack.isLoopControlVariable(VD);
    // OpenMP, 2.9.1 Canonical Loop Form, Restrictions.
    // The type of the loop iterator on which we depend may not have a random
    // access iterator type.
    if (Data.first && VD->getType()->isRecordType()) {
      SmallString<128> Name;
      llvm::raw_svector_ostream OS(Name);
      VD->getNameForDiagnostic(OS, SemaRef.getPrintingPolicy(),
                               /*Qualified=*/true);
      SemaRef.Diag(E->getExprLoc(),
                   diag::err_omp_wrong_dependency_iterator_type)
          << OS.str();
      SemaRef.Diag(VD->getLocation(), diag::note_previous_decl) << VD;
      return false;
    }
    if (Data.first && !SupportsNonRectangular) {
      SemaRef.Diag(E->getExprLoc(), diag::err_omp_invariant_dependency);
      return false;
    }
    if (Data.first &&
        (DepDecl || (PrevDepDecl &&
                     getCanonicalDecl(VD) != getCanonicalDecl(PrevDepDecl)))) {
      if (!DepDecl && PrevDepDecl)
        DepDecl = PrevDepDecl;
      SmallString<128> Name;
      llvm::raw_svector_ostream OS(Name);
      DepDecl->getNameForDiagnostic(OS, SemaRef.getPrintingPolicy(),
                                    /*Qualified=*/true);
      SemaRef.Diag(E->getExprLoc(),
                   diag::err_omp_invariant_or_linear_dependency)
          << OS.str();
      return false;
    }
    if (Data.first) {
      DepDecl = VD;
      BaseLoopId = Data.first;
    }
    return Data.first;
  }

public:
  bool VisitDeclRefExpr(const DeclRefExpr *E) {
    const ValueDecl *VD = E->getDecl();
    if (isa<VarDecl>(VD))
      return checkDecl(E, VD);
    return false;
  }
  bool VisitMemberExpr(const MemberExpr *E) {
    if (isa<CXXThisExpr>(E->getBase()->IgnoreParens())) {
      const ValueDecl *VD = E->getMemberDecl();
      if (isa<VarDecl>(VD) || isa<FieldDecl>(VD))
        return checkDecl(E, VD);
    }
    return false;
  }
  bool VisitStmt(const Stmt *S) {
    bool Res = false;
    for (const Stmt *Child : S->children())
      Res = (Child && Visit(Child)) || Res;
    return Res;
  }
  explicit LoopCounterRefChecker(Sema &SemaRef, DSAStackTy &Stack,
                                 const ValueDecl *CurLCDecl, bool IsInitializer,
                                 const ValueDecl *PrevDepDecl = nullptr,
                                 bool SupportsNonRectangular = true)
      : SemaRef(SemaRef), Stack(Stack), CurLCDecl(CurLCDecl),
        PrevDepDecl(PrevDepDecl), IsInitializer(IsInitializer),
        SupportsNonRectangular(SupportsNonRectangular) {}
  unsigned getBaseLoopId() const {
    assert(CurLCDecl && "Expected loop dependency.");
    return BaseLoopId;
  }
  const ValueDecl *getDepDecl() const {
    assert(CurLCDecl && "Expected loop dependency.");
    return DepDecl;
  }
};
} // namespace

std::optional<unsigned>
OpenMPIterationSpaceChecker::doesDependOnLoopCounter(const Stmt *S,
                                                     bool IsInitializer) {
  // Check for the non-rectangular loops.
  LoopCounterRefChecker LoopStmtChecker(SemaRef, Stack, LCDecl, IsInitializer,
                                        DepDecl, SupportsNonRectangular);
  if (LoopStmtChecker.Visit(S)) {
    DepDecl = LoopStmtChecker.getDepDecl();
    return LoopStmtChecker.getBaseLoopId();
  }
  return std::nullopt;
}

bool OpenMPIterationSpaceChecker::checkAndSetInit(Stmt *S, bool EmitDiags) {
  // Check init-expr for canonical loop form and save loop counter
  // variable - #Var and its initialization value - #LB.
  // OpenMP [2.6] Canonical loop form. init-expr may be one of the following:
  //   var = lb
  //   integer-type var = lb
  //   random-access-iterator-type var = lb
  //   pointer-type var = lb
  //
  if (!S) {
    if (EmitDiags) {
      SemaRef.Diag(DefaultLoc, diag::err_omp_loop_not_canonical_init);
    }
    return true;
  }
  if (auto *ExprTemp = dyn_cast<ExprWithCleanups>(S))
    if (!ExprTemp->cleanupsHaveSideEffects())
      S = ExprTemp->getSubExpr();

  InitSrcRange = S->getSourceRange();
  if (Expr *E = dyn_cast<Expr>(S))
    S = E->IgnoreParens();
  if (auto *BO = dyn_cast<BinaryOperator>(S)) {
    if (BO->getOpcode() == BO_Assign) {
      Expr *LHS = BO->getLHS()->IgnoreParens();
      if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
        if (auto *CED = dyn_cast<OMPCapturedExprDecl>(DRE->getDecl()))
          if (auto *ME = dyn_cast<MemberExpr>(getExprAsWritten(CED->getInit())))
            return setLCDeclAndLB(ME->getMemberDecl(), ME, BO->getRHS(),
                                  EmitDiags);
        return setLCDeclAndLB(DRE->getDecl(), DRE, BO->getRHS(), EmitDiags);
      }
      if (auto *ME = dyn_cast<MemberExpr>(LHS)) {
        if (ME->isArrow() &&
            isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts()))
          return setLCDeclAndLB(ME->getMemberDecl(), ME, BO->getRHS(),
                                EmitDiags);
      }
    }
  } else if (auto *DS = dyn_cast<DeclStmt>(S)) {
    if (DS->isSingleDecl()) {
      if (auto *Var = dyn_cast_or_null<VarDecl>(DS->getSingleDecl())) {
        if (Var->hasInit() && !Var->getType()->isReferenceType()) {
          // Accept non-canonical init form here but emit ext. warning.
          if (Var->getInitStyle() != VarDecl::CInit && EmitDiags)
            SemaRef.Diag(S->getBeginLoc(),
                         diag::ext_omp_loop_not_canonical_init)
                << S->getSourceRange();
          return setLCDeclAndLB(
              Var,
              buildDeclRefExpr(SemaRef, Var,
                               Var->getType().getNonReferenceType(),
                               DS->getBeginLoc()),
              Var->getInit(), EmitDiags);
        }
      }
    }
  } else if (auto *CE = dyn_cast<CXXOperatorCallExpr>(S)) {
    if (CE->getOperator() == OO_Equal) {
      Expr *LHS = CE->getArg(0);
      if (auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
        if (auto *CED = dyn_cast<OMPCapturedExprDecl>(DRE->getDecl()))
          if (auto *ME = dyn_cast<MemberExpr>(getExprAsWritten(CED->getInit())))
            return setLCDeclAndLB(ME->getMemberDecl(), ME, BO->getRHS(),
                                  EmitDiags);
        return setLCDeclAndLB(DRE->getDecl(), DRE, CE->getArg(1), EmitDiags);
      }
      if (auto *ME = dyn_cast<MemberExpr>(LHS)) {
        if (ME->isArrow() &&
            isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts()))
          return setLCDeclAndLB(ME->getMemberDecl(), ME, BO->getRHS(),
                                EmitDiags);
      }
    }
  }

  if (dependent() || SemaRef.CurContext->isDependentContext())
    return false;
  if (EmitDiags) {
    SemaRef.Diag(S->getBeginLoc(), diag::err_omp_loop_not_canonical_init)
        << S->getSourceRange();
  }
  return true;
}

/// Ignore parenthesizes, implicit casts, copy constructor and return the
/// variable (which may be the loop variable) if possible.
static const ValueDecl *getInitLCDecl(const Expr *E) {
  if (!E)
    return nullptr;
  E = getExprAsWritten(E);
  if (const auto *CE = dyn_cast_or_null<CXXConstructExpr>(E))
    if (const CXXConstructorDecl *Ctor = CE->getConstructor())
      if ((Ctor->isCopyOrMoveConstructor() ||
           Ctor->isConvertingConstructor(/*AllowExplicit=*/false)) &&
          CE->getNumArgs() > 0 && CE->getArg(0) != nullptr)
        E = CE->getArg(0)->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast_or_null<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      return getCanonicalDecl(VD);
  }
  if (const auto *ME = dyn_cast_or_null<MemberExpr>(E))
    if (ME->isArrow() && isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts()))
      return getCanonicalDecl(ME->getMemberDecl());
  return nullptr;
}

bool OpenMPIterationSpaceChecker::checkAndSetCond(Expr *S) {
  // Check test-expr for canonical form, save upper-bound UB, flags for
  // less/greater and for strict/non-strict comparison.
  // OpenMP [2.9] Canonical loop form. Test-expr may be one of the following:
  //   var relational-op b
  //   b relational-op var
  //
  bool IneqCondIsCanonical = SemaRef.getLangOpts().OpenMP >= 50;
  if (!S) {
    SemaRef.Diag(DefaultLoc, diag::err_omp_loop_not_canonical_cond)
        << (IneqCondIsCanonical ? 1 : 0) << LCDecl;
    return true;
  }
  Condition = S;
  S = getExprAsWritten(S);
  SourceLocation CondLoc = S->getBeginLoc();
  auto &&CheckAndSetCond =
      [this, IneqCondIsCanonical](BinaryOperatorKind Opcode, const Expr *LHS,
                                  const Expr *RHS, SourceRange SR,
                                  SourceLocation OpLoc) -> std::optional<bool> {
    if (BinaryOperator::isRelationalOp(Opcode)) {
      if (getInitLCDecl(LHS) == LCDecl)
        return setUB(const_cast<Expr *>(RHS),
                     (Opcode == BO_LT || Opcode == BO_LE),
                     (Opcode == BO_LT || Opcode == BO_GT), SR, OpLoc);
      if (getInitLCDecl(RHS) == LCDecl)
        return setUB(const_cast<Expr *>(LHS),
                     (Opcode == BO_GT || Opcode == BO_GE),
                     (Opcode == BO_LT || Opcode == BO_GT), SR, OpLoc);
    } else if (IneqCondIsCanonical && Opcode == BO_NE) {
      return setUB(const_cast<Expr *>(getInitLCDecl(LHS) == LCDecl ? RHS : LHS),
                   /*LessOp=*/std::nullopt,
                   /*StrictOp=*/true, SR, OpLoc);
    }
    return std::nullopt;
  };
  std::optional<bool> Res;
  if (auto *RBO = dyn_cast<CXXRewrittenBinaryOperator>(S)) {
    CXXRewrittenBinaryOperator::DecomposedForm DF = RBO->getDecomposedForm();
    Res = CheckAndSetCond(DF.Opcode, DF.LHS, DF.RHS, RBO->getSourceRange(),
                          RBO->getOperatorLoc());
  } else if (auto *BO = dyn_cast<BinaryOperator>(S)) {
    Res = CheckAndSetCond(BO->getOpcode(), BO->getLHS(), BO->getRHS(),
                          BO->getSourceRange(), BO->getOperatorLoc());
  } else if (auto *CE = dyn_cast<CXXOperatorCallExpr>(S)) {
    if (CE->getNumArgs() == 2) {
      Res = CheckAndSetCond(
          BinaryOperator::getOverloadedOpcode(CE->getOperator()), CE->getArg(0),
          CE->getArg(1), CE->getSourceRange(), CE->getOperatorLoc());
    }
  }
  if (Res)
    return *Res;
  if (dependent() || SemaRef.CurContext->isDependentContext())
    return false;
  SemaRef.Diag(CondLoc, diag::err_omp_loop_not_canonical_cond)
      << (IneqCondIsCanonical ? 1 : 0) << S->getSourceRange() << LCDecl;
  return true;
}

bool OpenMPIterationSpaceChecker::checkAndSetIncRHS(Expr *RHS) {
  // RHS of canonical loop form increment can be:
  //   var + incr
  //   incr + var
  //   var - incr
  //
  RHS = RHS->IgnoreParenImpCasts();
  if (auto *BO = dyn_cast<BinaryOperator>(RHS)) {
    if (BO->isAdditiveOp()) {
      bool IsAdd = BO->getOpcode() == BO_Add;
      if (getInitLCDecl(BO->getLHS()) == LCDecl)
        return setStep(BO->getRHS(), !IsAdd);
      if (IsAdd && getInitLCDecl(BO->getRHS()) == LCDecl)
        return setStep(BO->getLHS(), /*Subtract=*/false);
    }
  } else if (auto *CE = dyn_cast<CXXOperatorCallExpr>(RHS)) {
    bool IsAdd = CE->getOperator() == OO_Plus;
    if ((IsAdd || CE->getOperator() == OO_Minus) && CE->getNumArgs() == 2) {
      if (getInitLCDecl(CE->getArg(0)) == LCDecl)
        return setStep(CE->getArg(1), !IsAdd);
      if (IsAdd && getInitLCDecl(CE->getArg(1)) == LCDecl)
        return setStep(CE->getArg(0), /*Subtract=*/false);
    }
  }
  if (dependent() || SemaRef.CurContext->isDependentContext())
    return false;
  SemaRef.Diag(RHS->getBeginLoc(), diag::err_omp_loop_not_canonical_incr)
      << RHS->getSourceRange() << LCDecl;
  return true;
}

bool OpenMPIterationSpaceChecker::checkAndSetInc(Expr *S) {
  // Check incr-expr for canonical loop form and return true if it
  // does not conform.
  // OpenMP [2.6] Canonical loop form. Test-expr may be one of the following:
  //   ++var
  //   var++
  //   --var
  //   var--
  //   var += incr
  //   var -= incr
  //   var = var + incr
  //   var = incr + var
  //   var = var - incr
  //
  if (!S) {
    SemaRef.Diag(DefaultLoc, diag::err_omp_loop_not_canonical_incr) << LCDecl;
    return true;
  }
  if (auto *ExprTemp = dyn_cast<ExprWithCleanups>(S))
    if (!ExprTemp->cleanupsHaveSideEffects())
      S = ExprTemp->getSubExpr();

  IncrementSrcRange = S->getSourceRange();
  S = S->IgnoreParens();
  if (auto *UO = dyn_cast<UnaryOperator>(S)) {
    if (UO->isIncrementDecrementOp() &&
        getInitLCDecl(UO->getSubExpr()) == LCDecl)
      return setStep(SemaRef
                         .ActOnIntegerConstant(UO->getBeginLoc(),
                                               (UO->isDecrementOp() ? -1 : 1))
                         .get(),
                     /*Subtract=*/false);
  } else if (auto *BO = dyn_cast<BinaryOperator>(S)) {
    switch (BO->getOpcode()) {
    case BO_AddAssign:
    case BO_SubAssign:
      if (getInitLCDecl(BO->getLHS()) == LCDecl)
        return setStep(BO->getRHS(), BO->getOpcode() == BO_SubAssign);
      break;
    case BO_Assign:
      if (getInitLCDecl(BO->getLHS()) == LCDecl)
        return checkAndSetIncRHS(BO->getRHS());
      break;
    default:
      break;
    }
  } else if (auto *CE = dyn_cast<CXXOperatorCallExpr>(S)) {
    switch (CE->getOperator()) {
    case OO_PlusPlus:
    case OO_MinusMinus:
      if (getInitLCDecl(CE->getArg(0)) == LCDecl)
        return setStep(SemaRef
                           .ActOnIntegerConstant(
                               CE->getBeginLoc(),
                               ((CE->getOperator() == OO_MinusMinus) ? -1 : 1))
                           .get(),
                       /*Subtract=*/false);
      break;
    case OO_PlusEqual:
    case OO_MinusEqual:
      if (getInitLCDecl(CE->getArg(0)) == LCDecl)
        return setStep(CE->getArg(1), CE->getOperator() == OO_MinusEqual);
      break;
    case OO_Equal:
      if (getInitLCDecl(CE->getArg(0)) == LCDecl)
        return checkAndSetIncRHS(CE->getArg(1));
      break;
    default:
      break;
    }
  }
  if (dependent() || SemaRef.CurContext->isDependentContext())
    return false;
  SemaRef.Diag(S->getBeginLoc(), diag::err_omp_loop_not_canonical_incr)
      << S->getSourceRange() << LCDecl;
  return true;
}

static ExprResult
tryBuildCapture(Sema &SemaRef, Expr *Capture,
                llvm::MapVector<const Expr *, DeclRefExpr *> &Captures,
                StringRef Name = ".capture_expr.") {
  if (SemaRef.CurContext->isDependentContext() || Capture->containsErrors())
    return Capture;
  if (Capture->isEvaluatable(SemaRef.Context, Expr::SE_AllowSideEffects))
    return SemaRef.PerformImplicitConversion(
        Capture->IgnoreImpCasts(), Capture->getType(), Sema::AA_Converting,
        /*AllowExplicit=*/true);
  auto I = Captures.find(Capture);
  if (I != Captures.end())
    return buildCapture(SemaRef, Capture, I->second, Name);
  DeclRefExpr *Ref = nullptr;
  ExprResult Res = buildCapture(SemaRef, Capture, Ref, Name);
  Captures[Capture] = Ref;
  return Res;
}

/// Calculate number of iterations, transforming to unsigned, if number of
/// iterations may be larger than the original type.
static Expr *
calculateNumIters(Sema &SemaRef, Scope *S, SourceLocation DefaultLoc,
                  Expr *Lower, Expr *Upper, Expr *Step, QualType LCTy,
                  bool TestIsStrictOp, bool RoundToStep,
                  llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) {
  ExprResult NewStep = tryBuildCapture(SemaRef, Step, Captures, ".new_step");
  if (!NewStep.isUsable())
    return nullptr;
  llvm::APSInt LRes, SRes;
  bool IsLowerConst = false, IsStepConst = false;
  if (std::optional<llvm::APSInt> Res =
          Lower->getIntegerConstantExpr(SemaRef.Context)) {
    LRes = *Res;
    IsLowerConst = true;
  }
  if (std::optional<llvm::APSInt> Res =
          Step->getIntegerConstantExpr(SemaRef.Context)) {
    SRes = *Res;
    IsStepConst = true;
  }
  bool NoNeedToConvert = IsLowerConst && !RoundToStep &&
                         ((!TestIsStrictOp && LRes.isNonNegative()) ||
                          (TestIsStrictOp && LRes.isStrictlyPositive()));
  bool NeedToReorganize = false;
  // Check if any subexpressions in Lower -Step [+ 1] lead to overflow.
  if (!NoNeedToConvert && IsLowerConst &&
      (TestIsStrictOp || (RoundToStep && IsStepConst))) {
    NoNeedToConvert = true;
    if (RoundToStep) {
      unsigned BW = LRes.getBitWidth() > SRes.getBitWidth()
                        ? LRes.getBitWidth()
                        : SRes.getBitWidth();
      LRes = LRes.extend(BW + 1);
      LRes.setIsSigned(true);
      SRes = SRes.extend(BW + 1);
      SRes.setIsSigned(true);
      LRes -= SRes;
      NoNeedToConvert = LRes.trunc(BW).extend(BW + 1) == LRes;
      LRes = LRes.trunc(BW);
    }
    if (TestIsStrictOp) {
      unsigned BW = LRes.getBitWidth();
      LRes = LRes.extend(BW + 1);
      LRes.setIsSigned(true);
      ++LRes;
      NoNeedToConvert =
          NoNeedToConvert && LRes.trunc(BW).extend(BW + 1) == LRes;
      // truncate to the original bitwidth.
      LRes = LRes.trunc(BW);
    }
    NeedToReorganize = NoNeedToConvert;
  }
  llvm::APSInt URes;
  bool IsUpperConst = false;
  if (std::optional<llvm::APSInt> Res =
          Upper->getIntegerConstantExpr(SemaRef.Context)) {
    URes = *Res;
    IsUpperConst = true;
  }
  if (NoNeedToConvert && IsLowerConst && IsUpperConst &&
      (!RoundToStep || IsStepConst)) {
    unsigned BW = LRes.getBitWidth() > URes.getBitWidth() ? LRes.getBitWidth()
                                                          : URes.getBitWidth();
    LRes = LRes.extend(BW + 1);
    LRes.setIsSigned(true);
    URes = URes.extend(BW + 1);
    URes.setIsSigned(true);
    URes -= LRes;
    NoNeedToConvert = URes.trunc(BW).extend(BW + 1) == URes;
    NeedToReorganize = NoNeedToConvert;
  }
  // If the boundaries are not constant or (Lower - Step [+ 1]) is not constant
  // or less than zero (Upper - (Lower - Step [+ 1]) may overflow) - promote to
  // unsigned.
  if ((!NoNeedToConvert || (LRes.isNegative() && !IsUpperConst)) &&
      !LCTy->isDependentType() && LCTy->isIntegerType()) {
    QualType LowerTy = Lower->getType();
    QualType UpperTy = Upper->getType();
    uint64_t LowerSize = SemaRef.Context.getTypeSize(LowerTy);
    uint64_t UpperSize = SemaRef.Context.getTypeSize(UpperTy);
    if ((LowerSize <= UpperSize && UpperTy->hasSignedIntegerRepresentation()) ||
        (LowerSize > UpperSize && LowerTy->hasSignedIntegerRepresentation())) {
      QualType CastType = SemaRef.Context.getIntTypeForBitwidth(
          LowerSize > UpperSize ? LowerSize : UpperSize, /*Signed=*/0);
      Upper =
          SemaRef
              .PerformImplicitConversion(
                  SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Upper).get(),
                  CastType, Sema::AA_Converting)
              .get();
      Lower = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Lower).get();
      NewStep = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, NewStep.get());
    }
  }
  if (!Lower || !Upper || NewStep.isInvalid())
    return nullptr;

  ExprResult Diff;
  // If need to reorganize, then calculate the form as Upper - (Lower - Step [+
  // 1]).
  if (NeedToReorganize) {
    Diff = Lower;

    if (RoundToStep) {
      // Lower - Step
      Diff =
          SemaRef.BuildBinOp(S, DefaultLoc, BO_Sub, Diff.get(), NewStep.get());
      if (!Diff.isUsable())
        return nullptr;
    }

    // Lower - Step [+ 1]
    if (TestIsStrictOp)
      Diff = SemaRef.BuildBinOp(
          S, DefaultLoc, BO_Add, Diff.get(),
          SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get());
    if (!Diff.isUsable())
      return nullptr;

    Diff = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Diff.get());
    if (!Diff.isUsable())
      return nullptr;

    // Upper - (Lower - Step [+ 1]).
    Diff = SemaRef.BuildBinOp(S, DefaultLoc, BO_Sub, Upper, Diff.get());
    if (!Diff.isUsable())
      return nullptr;
  } else {
    Diff = SemaRef.BuildBinOp(S, DefaultLoc, BO_Sub, Upper, Lower);

    if (!Diff.isUsable() && LCTy->getAsCXXRecordDecl()) {
      // BuildBinOp already emitted error, this one is to point user to upper
      // and lower bound, and to tell what is passed to 'operator-'.
      SemaRef.Diag(Upper->getBeginLoc(), diag::err_omp_loop_diff_cxx)
          << Upper->getSourceRange() << Lower->getSourceRange();
      return nullptr;
    }

    if (!Diff.isUsable())
      return nullptr;

    // Upper - Lower [- 1]
    if (TestIsStrictOp)
      Diff = SemaRef.BuildBinOp(
          S, DefaultLoc, BO_Sub, Diff.get(),
          SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get());
    if (!Diff.isUsable())
      return nullptr;

    if (RoundToStep) {
      // Upper - Lower [- 1] + Step
      Diff =
          SemaRef.BuildBinOp(S, DefaultLoc, BO_Add, Diff.get(), NewStep.get());
      if (!Diff.isUsable())
        return nullptr;
    }
  }

  // Parentheses (for dumping/debugging purposes only).
  Diff = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Diff.get());
  if (!Diff.isUsable())
    return nullptr;

  // (Upper - Lower [- 1] + Step) / Step or (Upper - Lower) / Step
  Diff = SemaRef.BuildBinOp(S, DefaultLoc, BO_Div, Diff.get(), NewStep.get());
  if (!Diff.isUsable())
    return nullptr;

  return Diff.get();
}

/// Build the expression to calculate the number of iterations.
Expr *OpenMPIterationSpaceChecker::buildNumIterations(
    Scope *S, ArrayRef<LoopIterationSpace> ResultIterSpaces, bool LimitedType,
    llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) const {
  QualType VarType = LCDecl->getType().getNonReferenceType();
  if (!VarType->isIntegerType() && !VarType->isPointerType() &&
      !SemaRef.getLangOpts().CPlusPlus)
    return nullptr;
  Expr *LBVal = LB;
  Expr *UBVal = UB;
  // OuterVar = (LB = TestIsLessOp.getValue() ? min(LB(MinVal), LB(MaxVal)) :
  // max(LB(MinVal), LB(MaxVal)))
  if (InitDependOnLC) {
    const LoopIterationSpace &IS = ResultIterSpaces[*InitDependOnLC - 1];
    if (!IS.MinValue || !IS.MaxValue)
      return nullptr;
    // OuterVar = Min
    ExprResult MinValue =
        SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, IS.MinValue);
    if (!MinValue.isUsable())
      return nullptr;

    ExprResult LBMinVal = SemaRef.BuildBinOp(S, DefaultLoc, BO_Assign,
                                             IS.CounterVar, MinValue.get());
    if (!LBMinVal.isUsable())
      return nullptr;
    // OuterVar = Min, LBVal
    LBMinVal =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_Comma, LBMinVal.get(), LBVal);
    if (!LBMinVal.isUsable())
      return nullptr;
    // (OuterVar = Min, LBVal)
    LBMinVal = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, LBMinVal.get());
    if (!LBMinVal.isUsable())
      return nullptr;

    // OuterVar = Max
    ExprResult MaxValue =
        SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, IS.MaxValue);
    if (!MaxValue.isUsable())
      return nullptr;

    ExprResult LBMaxVal = SemaRef.BuildBinOp(S, DefaultLoc, BO_Assign,
                                             IS.CounterVar, MaxValue.get());
    if (!LBMaxVal.isUsable())
      return nullptr;
    // OuterVar = Max, LBVal
    LBMaxVal =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_Comma, LBMaxVal.get(), LBVal);
    if (!LBMaxVal.isUsable())
      return nullptr;
    // (OuterVar = Max, LBVal)
    LBMaxVal = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, LBMaxVal.get());
    if (!LBMaxVal.isUsable())
      return nullptr;

    Expr *LBMin =
        tryBuildCapture(SemaRef, LBMinVal.get(), Captures, ".lb_min").get();
    Expr *LBMax =
        tryBuildCapture(SemaRef, LBMaxVal.get(), Captures, ".lb_max").get();
    if (!LBMin || !LBMax)
      return nullptr;
    // LB(MinVal) < LB(MaxVal)
    ExprResult MinLessMaxRes =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_LT, LBMin, LBMax);
    if (!MinLessMaxRes.isUsable())
      return nullptr;
    Expr *MinLessMax =
        tryBuildCapture(SemaRef, MinLessMaxRes.get(), Captures, ".min_less_max")
            .get();
    if (!MinLessMax)
      return nullptr;
    if (*TestIsLessOp) {
      // LB(MinVal) < LB(MaxVal) ? LB(MinVal) : LB(MaxVal) - min(LB(MinVal),
      // LB(MaxVal))
      ExprResult MinLB = SemaRef.ActOnConditionalOp(DefaultLoc, DefaultLoc,
                                                    MinLessMax, LBMin, LBMax);
      if (!MinLB.isUsable())
        return nullptr;
      LBVal = MinLB.get();
    } else {
      // LB(MinVal) < LB(MaxVal) ? LB(MaxVal) : LB(MinVal) - max(LB(MinVal),
      // LB(MaxVal))
      ExprResult MaxLB = SemaRef.ActOnConditionalOp(DefaultLoc, DefaultLoc,
                                                    MinLessMax, LBMax, LBMin);
      if (!MaxLB.isUsable())
        return nullptr;
      LBVal = MaxLB.get();
    }
    // OuterVar = LB
    LBMinVal =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_Assign, IS.CounterVar, LBVal);
    if (!LBMinVal.isUsable())
      return nullptr;
    LBVal = LBMinVal.get();
  }
  // UB = TestIsLessOp.getValue() ? max(UB(MinVal), UB(MaxVal)) :
  // min(UB(MinVal), UB(MaxVal))
  if (CondDependOnLC) {
    const LoopIterationSpace &IS = ResultIterSpaces[*CondDependOnLC - 1];
    if (!IS.MinValue || !IS.MaxValue)
      return nullptr;
    // OuterVar = Min
    ExprResult MinValue =
        SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, IS.MinValue);
    if (!MinValue.isUsable())
      return nullptr;

    ExprResult UBMinVal = SemaRef.BuildBinOp(S, DefaultLoc, BO_Assign,
                                             IS.CounterVar, MinValue.get());
    if (!UBMinVal.isUsable())
      return nullptr;
    // OuterVar = Min, UBVal
    UBMinVal =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_Comma, UBMinVal.get(), UBVal);
    if (!UBMinVal.isUsable())
      return nullptr;
    // (OuterVar = Min, UBVal)
    UBMinVal = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, UBMinVal.get());
    if (!UBMinVal.isUsable())
      return nullptr;

    // OuterVar = Max
    ExprResult MaxValue =
        SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, IS.MaxValue);
    if (!MaxValue.isUsable())
      return nullptr;

    ExprResult UBMaxVal = SemaRef.BuildBinOp(S, DefaultLoc, BO_Assign,
                                             IS.CounterVar, MaxValue.get());
    if (!UBMaxVal.isUsable())
      return nullptr;
    // OuterVar = Max, UBVal
    UBMaxVal =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_Comma, UBMaxVal.get(), UBVal);
    if (!UBMaxVal.isUsable())
      return nullptr;
    // (OuterVar = Max, UBVal)
    UBMaxVal = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, UBMaxVal.get());
    if (!UBMaxVal.isUsable())
      return nullptr;

    Expr *UBMin =
        tryBuildCapture(SemaRef, UBMinVal.get(), Captures, ".ub_min").get();
    Expr *UBMax =
        tryBuildCapture(SemaRef, UBMaxVal.get(), Captures, ".ub_max").get();
    if (!UBMin || !UBMax)
      return nullptr;
    // UB(MinVal) > UB(MaxVal)
    ExprResult MinGreaterMaxRes =
        SemaRef.BuildBinOp(S, DefaultLoc, BO_GT, UBMin, UBMax);
    if (!MinGreaterMaxRes.isUsable())
      return nullptr;
    Expr *MinGreaterMax = tryBuildCapture(SemaRef, MinGreaterMaxRes.get(),
                                          Captures, ".min_greater_max")
                              .get();
    if (!MinGreaterMax)
      return nullptr;
    if (*TestIsLessOp) {
      // UB(MinVal) > UB(MaxVal) ? UB(MinVal) : UB(MaxVal) - max(UB(MinVal),
      // UB(MaxVal))
      ExprResult MaxUB = SemaRef.ActOnConditionalOp(
          DefaultLoc, DefaultLoc, MinGreaterMax, UBMin, UBMax);
      if (!MaxUB.isUsable())
        return nullptr;
      UBVal = MaxUB.get();
    } else {
      // UB(MinVal) > UB(MaxVal) ? UB(MaxVal) : UB(MinVal) - min(UB(MinVal),
      // UB(MaxVal))
      ExprResult MinUB = SemaRef.ActOnConditionalOp(
          DefaultLoc, DefaultLoc, MinGreaterMax, UBMax, UBMin);
      if (!MinUB.isUsable())
        return nullptr;
      UBVal = MinUB.get();
    }
  }
  Expr *UBExpr = *TestIsLessOp ? UBVal : LBVal;
  Expr *LBExpr = *TestIsLessOp ? LBVal : UBVal;
  Expr *Upper = tryBuildCapture(SemaRef, UBExpr, Captures, ".upper").get();
  Expr *Lower = tryBuildCapture(SemaRef, LBExpr, Captures, ".lower").get();
  if (!Upper || !Lower)
    return nullptr;

  ExprResult Diff = calculateNumIters(SemaRef, S, DefaultLoc, Lower, Upper,
                                      Step, VarType, TestIsStrictOp,
                                      /*RoundToStep=*/true, Captures);
  if (!Diff.isUsable())
    return nullptr;

  // OpenMP runtime requires 32-bit or 64-bit loop variables.
  QualType Type = Diff.get()->getType();
  ASTContext &C = SemaRef.Context;
  bool UseVarType = VarType->hasIntegerRepresentation() &&
                    C.getTypeSize(Type) > C.getTypeSize(VarType);
  if (!Type->isIntegerType() || UseVarType) {
    unsigned NewSize =
        UseVarType ? C.getTypeSize(VarType) : C.getTypeSize(Type);
    bool IsSigned = UseVarType ? VarType->hasSignedIntegerRepresentation()
                               : Type->hasSignedIntegerRepresentation();
    Type = C.getIntTypeForBitwidth(NewSize, IsSigned);
    if (!SemaRef.Context.hasSameType(Diff.get()->getType(), Type)) {
      Diff = SemaRef.PerformImplicitConversion(
          Diff.get(), Type, Sema::AA_Converting, /*AllowExplicit=*/true);
      if (!Diff.isUsable())
        return nullptr;
    }
  }
  if (LimitedType) {
    unsigned NewSize = (C.getTypeSize(Type) > 32) ? 64 : 32;
    if (NewSize != C.getTypeSize(Type)) {
      if (NewSize < C.getTypeSize(Type)) {
        assert(NewSize == 64 && "incorrect loop var size");
        SemaRef.Diag(DefaultLoc, diag::warn_omp_loop_64_bit_var)
            << InitSrcRange << ConditionSrcRange;
      }
      QualType NewType = C.getIntTypeForBitwidth(
          NewSize, Type->hasSignedIntegerRepresentation() ||
                       C.getTypeSize(Type) < NewSize);
      if (!SemaRef.Context.hasSameType(Diff.get()->getType(), NewType)) {
        Diff = SemaRef.PerformImplicitConversion(Diff.get(), NewType,
                                                 Sema::AA_Converting, true);
        if (!Diff.isUsable())
          return nullptr;
      }
    }
  }

  return Diff.get();
}

std::pair<Expr *, Expr *> OpenMPIterationSpaceChecker::buildMinMaxValues(
    Scope *S, llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) const {
  // Do not build for iterators, they cannot be used in non-rectangular loop
  // nests.
  if (LCDecl->getType()->isRecordType())
    return std::make_pair(nullptr, nullptr);
  // If we subtract, the min is in the condition, otherwise the min is in the
  // init value.
  Expr *MinExpr = nullptr;
  Expr *MaxExpr = nullptr;
  Expr *LBExpr = *TestIsLessOp ? LB : UB;
  Expr *UBExpr = *TestIsLessOp ? UB : LB;
  bool LBNonRect =
      *TestIsLessOp ? InitDependOnLC.has_value() : CondDependOnLC.has_value();
  bool UBNonRect =
      *TestIsLessOp ? CondDependOnLC.has_value() : InitDependOnLC.has_value();
  Expr *Lower =
      LBNonRect ? LBExpr : tryBuildCapture(SemaRef, LBExpr, Captures).get();
  Expr *Upper =
      UBNonRect ? UBExpr : tryBuildCapture(SemaRef, UBExpr, Captures).get();
  if (!Upper || !Lower)
    return std::make_pair(nullptr, nullptr);

  if (*TestIsLessOp)
    MinExpr = Lower;
  else
    MaxExpr = Upper;

  // Build minimum/maximum value based on number of iterations.
  QualType VarType = LCDecl->getType().getNonReferenceType();

  ExprResult Diff = calculateNumIters(SemaRef, S, DefaultLoc, Lower, Upper,
                                      Step, VarType, TestIsStrictOp,
                                      /*RoundToStep=*/false, Captures);
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  // ((Upper - Lower [- 1]) / Step) * Step
  // Parentheses (for dumping/debugging purposes only).
  Diff = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Diff.get());
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  ExprResult NewStep = tryBuildCapture(SemaRef, Step, Captures, ".new_step");
  if (!NewStep.isUsable())
    return std::make_pair(nullptr, nullptr);
  Diff = SemaRef.BuildBinOp(S, DefaultLoc, BO_Mul, Diff.get(), NewStep.get());
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  // Parentheses (for dumping/debugging purposes only).
  Diff = SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Diff.get());
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  // Convert to the ptrdiff_t, if original type is pointer.
  if (VarType->isAnyPointerType() &&
      !SemaRef.Context.hasSameType(
          Diff.get()->getType(),
          SemaRef.Context.getUnsignedPointerDiffType())) {
    Diff = SemaRef.PerformImplicitConversion(
        Diff.get(), SemaRef.Context.getUnsignedPointerDiffType(),
        Sema::AA_Converting, /*AllowExplicit=*/true);
  }
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  if (*TestIsLessOp) {
    // MinExpr = Lower;
    // MaxExpr = Lower + (((Upper - Lower [- 1]) / Step) * Step)
    Diff = SemaRef.BuildBinOp(
        S, DefaultLoc, BO_Add,
        SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Lower).get(),
        Diff.get());
    if (!Diff.isUsable())
      return std::make_pair(nullptr, nullptr);
  } else {
    // MaxExpr = Upper;
    // MinExpr = Upper - (((Upper - Lower [- 1]) / Step) * Step)
    Diff = SemaRef.BuildBinOp(
        S, DefaultLoc, BO_Sub,
        SemaRef.ActOnParenExpr(DefaultLoc, DefaultLoc, Upper).get(),
        Diff.get());
    if (!Diff.isUsable())
      return std::make_pair(nullptr, nullptr);
  }

  // Convert to the original type.
  if (SemaRef.Context.hasSameType(Diff.get()->getType(), VarType))
    Diff = SemaRef.PerformImplicitConversion(Diff.get(), VarType,
                                             Sema::AA_Converting,
                                             /*AllowExplicit=*/true);
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  Sema::TentativeAnalysisScope Trap(SemaRef);
  Diff = SemaRef.ActOnFinishFullExpr(Diff.get(), /*DiscardedValue=*/false);
  if (!Diff.isUsable())
    return std::make_pair(nullptr, nullptr);

  if (*TestIsLessOp)
    MaxExpr = Diff.get();
  else
    MinExpr = Diff.get();

  return std::make_pair(MinExpr, MaxExpr);
}

Expr *OpenMPIterationSpaceChecker::buildFinalCondition(Scope *S) const {
  if (InitDependOnLC || CondDependOnLC)
    return Condition;
  return nullptr;
}

Expr *OpenMPIterationSpaceChecker::buildPreCond(
    Scope *S, Expr *Cond,
    llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) const {
  // Do not build a precondition when the condition/initialization is dependent
  // to prevent pessimistic early loop exit.
  // TODO: this can be improved by calculating min/max values but not sure that
  // it will be very effective.
  if (CondDependOnLC || InitDependOnLC)
    return SemaRef
        .PerformImplicitConversion(
            SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get(),
            SemaRef.Context.BoolTy, /*Action=*/Sema::AA_Casting,
            /*AllowExplicit=*/true)
        .get();

  // Try to build LB <op> UB, where <op> is <, >, <=, or >=.
  Sema::TentativeAnalysisScope Trap(SemaRef);

  ExprResult NewLB = tryBuildCapture(SemaRef, LB, Captures);
  ExprResult NewUB = tryBuildCapture(SemaRef, UB, Captures);
  if (!NewLB.isUsable() || !NewUB.isUsable())
    return nullptr;

  ExprResult CondExpr =
      SemaRef.BuildBinOp(S, DefaultLoc,
                         *TestIsLessOp ? (TestIsStrictOp ? BO_LT : BO_LE)
                                       : (TestIsStrictOp ? BO_GT : BO_GE),
                         NewLB.get(), NewUB.get());
  if (CondExpr.isUsable()) {
    if (!SemaRef.Context.hasSameUnqualifiedType(CondExpr.get()->getType(),
                                                SemaRef.Context.BoolTy))
      CondExpr = SemaRef.PerformImplicitConversion(
          CondExpr.get(), SemaRef.Context.BoolTy, /*Action=*/Sema::AA_Casting,
          /*AllowExplicit=*/true);
  }

  // Otherwise use original loop condition and evaluate it in runtime.
  return CondExpr.isUsable() ? CondExpr.get() : Cond;
}

/// Build reference expression to the counter be used for codegen.
DeclRefExpr *OpenMPIterationSpaceChecker::buildCounterVar(
    llvm::MapVector<const Expr *, DeclRefExpr *> &Captures,
    DSAStackTy &DSA) const {
  auto *VD = dyn_cast<VarDecl>(LCDecl);
  if (!VD) {
    VD = SemaRef.OpenMP().isOpenMPCapturedDecl(LCDecl);
    DeclRefExpr *Ref = buildDeclRefExpr(
        SemaRef, VD, VD->getType().getNonReferenceType(), DefaultLoc);
    const DSAStackTy::DSAVarData Data =
        DSA.getTopDSA(LCDecl, /*FromParent=*/false);
    // If the loop control decl is explicitly marked as private, do not mark it
    // as captured again.
    if (!isOpenMPPrivate(Data.CKind) || !Data.RefExpr)
      Captures.insert(std::make_pair(LCRef, Ref));
    return Ref;
  }
  return cast<DeclRefExpr>(LCRef);
}

Expr *OpenMPIterationSpaceChecker::buildPrivateCounterVar() const {
  if (LCDecl && !LCDecl->isInvalidDecl()) {
    QualType Type = LCDecl->getType().getNonReferenceType();
    VarDecl *PrivateVar = buildVarDecl(
        SemaRef, DefaultLoc, Type, LCDecl->getName(),
        LCDecl->hasAttrs() ? &LCDecl->getAttrs() : nullptr,
        isa<VarDecl>(LCDecl)
            ? buildDeclRefExpr(SemaRef, cast<VarDecl>(LCDecl), Type, DefaultLoc)
            : nullptr);
    if (PrivateVar->isInvalidDecl())
      return nullptr;
    return buildDeclRefExpr(SemaRef, PrivateVar, Type, DefaultLoc);
  }
  return nullptr;
}

/// Build initialization of the counter to be used for codegen.
Expr *OpenMPIterationSpaceChecker::buildCounterInit() const { return LB; }

/// Build step of the counter be used for codegen.
Expr *OpenMPIterationSpaceChecker::buildCounterStep() const { return Step; }

Expr *OpenMPIterationSpaceChecker::buildOrderedLoopData(
    Scope *S, Expr *Counter,
    llvm::MapVector<const Expr *, DeclRefExpr *> &Captures, SourceLocation Loc,
    Expr *Inc, OverloadedOperatorKind OOK) {
  Expr *Cnt = SemaRef.DefaultLvalueConversion(Counter).get();
  if (!Cnt)
    return nullptr;
  if (Inc) {
    assert((OOK == OO_Plus || OOK == OO_Minus) &&
           "Expected only + or - operations for depend clauses.");
    BinaryOperatorKind BOK = (OOK == OO_Plus) ? BO_Add : BO_Sub;
    Cnt = SemaRef.BuildBinOp(S, Loc, BOK, Cnt, Inc).get();
    if (!Cnt)
      return nullptr;
  }
  QualType VarType = LCDecl->getType().getNonReferenceType();
  if (!VarType->isIntegerType() && !VarType->isPointerType() &&
      !SemaRef.getLangOpts().CPlusPlus)
    return nullptr;
  // Upper - Lower
  Expr *Upper =
      *TestIsLessOp ? Cnt : tryBuildCapture(SemaRef, LB, Captures).get();
  Expr *Lower =
      *TestIsLessOp ? tryBuildCapture(SemaRef, LB, Captures).get() : Cnt;
  if (!Upper || !Lower)
    return nullptr;

  ExprResult Diff = calculateNumIters(
      SemaRef, S, DefaultLoc, Lower, Upper, Step, VarType,
      /*TestIsStrictOp=*/false, /*RoundToStep=*/false, Captures);
  if (!Diff.isUsable())
    return nullptr;

  return Diff.get();
}
} // namespace

void SemaOpenMP::ActOnOpenMPLoopInitialization(SourceLocation ForLoc,
                                               Stmt *Init) {
  assert(getLangOpts().OpenMP && "OpenMP is not active.");
  assert(Init && "Expected loop in canonical form.");
  unsigned AssociatedLoops = DSAStack->getAssociatedLoops();
  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  if (AssociatedLoops == 0 || !isOpenMPLoopDirective(DKind))
    return;

  DSAStack->loopStart();
  OpenMPIterationSpaceChecker ISC(SemaRef, /*SupportsNonRectangular=*/true,
                                  *DSAStack, ForLoc);
  if (!ISC.checkAndSetInit(Init, /*EmitDiags=*/false)) {
    if (ValueDecl *D = ISC.getLoopDecl()) {
      auto *VD = dyn_cast<VarDecl>(D);
      DeclRefExpr *PrivateRef = nullptr;
      if (!VD) {
        if (VarDecl *Private = isOpenMPCapturedDecl(D)) {
          VD = Private;
        } else {
          PrivateRef = buildCapture(SemaRef, D, ISC.getLoopDeclRefExpr(),
                                    /*WithInit=*/false);
          VD = cast<VarDecl>(PrivateRef->getDecl());
        }
      }
      DSAStack->addLoopControlVariable(D, VD);
      const Decl *LD = DSAStack->getPossiblyLoopCounter();
      if (LD != D->getCanonicalDecl()) {
        DSAStack->resetPossibleLoopCounter();
        if (auto *Var = dyn_cast_or_null<VarDecl>(LD))
          SemaRef.MarkDeclarationsReferencedInExpr(buildDeclRefExpr(
              SemaRef, const_cast<VarDecl *>(Var),
              Var->getType().getNonLValueExprType(getASTContext()), ForLoc,
              /*RefersToCapture=*/true));
      }
      // OpenMP [2.14.1.1, Data-sharing Attribute Rules for Variables
      // Referenced in a Construct, C/C++]. The loop iteration variable in the
      // associated for-loop of a simd construct with just one associated
      // for-loop may be listed in a linear clause with a constant-linear-step
      // that is the increment of the associated for-loop. The loop iteration
      // variable(s) in the associated for-loop(s) of a for or parallel for
      // construct may be listed in a private or lastprivate clause.
      DSAStackTy::DSAVarData DVar =
          DSAStack->getTopDSA(D, /*FromParent=*/false);
      // If LoopVarRefExpr is nullptr it means the corresponding loop variable
      // is declared in the loop and it is predetermined as a private.
      Expr *LoopDeclRefExpr = ISC.getLoopDeclRefExpr();
      OpenMPClauseKind PredeterminedCKind =
          isOpenMPSimdDirective(DKind)
              ? (DSAStack->hasMutipleLoops() ? OMPC_lastprivate : OMPC_linear)
              : OMPC_private;
      auto IsOpenMPTaskloopDirective = [](OpenMPDirectiveKind DK) {
        return getLeafConstructsOrSelf(DK).back() == OMPD_taskloop;
      };
      if (((isOpenMPSimdDirective(DKind) && DVar.CKind != OMPC_unknown &&
            DVar.CKind != PredeterminedCKind && DVar.RefExpr &&
            (getLangOpts().OpenMP <= 45 ||
             (DVar.CKind != OMPC_lastprivate && DVar.CKind != OMPC_private))) ||
           ((isOpenMPWorksharingDirective(DKind) ||
             IsOpenMPTaskloopDirective(DKind) ||
             isOpenMPDistributeDirective(DKind)) &&
            !isOpenMPSimdDirective(DKind) && DVar.CKind != OMPC_unknown &&
            DVar.CKind != OMPC_private && DVar.CKind != OMPC_lastprivate)) &&
          (DVar.CKind != OMPC_private || DVar.RefExpr)) {
        Diag(Init->getBeginLoc(), diag::err_omp_loop_var_dsa)
            << getOpenMPClauseName(DVar.CKind) << getOpenMPDirectiveName(DKind)
            << getOpenMPClauseName(PredeterminedCKind);
        if (DVar.RefExpr == nullptr)
          DVar.CKind = PredeterminedCKind;
        reportOriginalDsa(SemaRef, DSAStack, D, DVar, /*IsLoopIterVar=*/true);
      } else if (LoopDeclRefExpr) {
        // Make the loop iteration variable private (for worksharing
        // constructs), linear (for simd directives with the only one
        // associated loop) or lastprivate (for simd directives with several
        // collapsed or ordered loops).
        if (DVar.CKind == OMPC_unknown)
          DSAStack->addDSA(D, LoopDeclRefExpr, PredeterminedCKind, PrivateRef);
      }
    }
  }
  DSAStack->setAssociatedLoops(AssociatedLoops - 1);
}

namespace {
// Utility for OpenMP doacross clause kind
class OMPDoacrossKind {
public:
  bool isSource(const OMPDoacrossClause *C) {
    return C->getDependenceType() == OMPC_DOACROSS_source ||
           C->getDependenceType() == OMPC_DOACROSS_source_omp_cur_iteration;
  }
  bool isSink(const OMPDoacrossClause *C) {
    return C->getDependenceType() == OMPC_DOACROSS_sink;
  }
  bool isSinkIter(const OMPDoacrossClause *C) {
    return C->getDependenceType() == OMPC_DOACROSS_sink_omp_cur_iteration;
  }
};
} // namespace
/// Called on a for stmt to check and extract its iteration space
/// for further processing (such as collapsing).
static bool checkOpenMPIterationSpace(
    OpenMPDirectiveKind DKind, Stmt *S, Sema &SemaRef, DSAStackTy &DSA,
    unsigned CurrentNestedLoopCount, unsigned NestedLoopCount,
    unsigned TotalNestedLoopCount, Expr *CollapseLoopCountExpr,
    Expr *OrderedLoopCountExpr,
    SemaOpenMP::VarsWithInheritedDSAType &VarsWithImplicitDSA,
    llvm::MutableArrayRef<LoopIterationSpace> ResultIterSpaces,
    llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) {
  bool SupportsNonRectangular = !isOpenMPLoopTransformationDirective(DKind);
  // OpenMP [2.9.1, Canonical Loop Form]
  //   for (init-expr; test-expr; incr-expr) structured-block
  //   for (range-decl: range-expr) structured-block
  if (auto *CanonLoop = dyn_cast_or_null<OMPCanonicalLoop>(S))
    S = CanonLoop->getLoopStmt();
  auto *For = dyn_cast_or_null<ForStmt>(S);
  auto *CXXFor = dyn_cast_or_null<CXXForRangeStmt>(S);
  // Ranged for is supported only in OpenMP 5.0.
  if (!For && (SemaRef.LangOpts.OpenMP <= 45 || !CXXFor)) {
    OpenMPDirectiveKind DK = (SemaRef.getLangOpts().OpenMP < 50 ||
                              DSA.getMappedDirective() == OMPD_unknown)
                                 ? DKind
                                 : DSA.getMappedDirective();
    SemaRef.Diag(S->getBeginLoc(), diag::err_omp_not_for)
        << (CollapseLoopCountExpr != nullptr || OrderedLoopCountExpr != nullptr)
        << getOpenMPDirectiveName(DK) << TotalNestedLoopCount
        << (CurrentNestedLoopCount > 0) << CurrentNestedLoopCount;
    if (TotalNestedLoopCount > 1) {
      if (CollapseLoopCountExpr && OrderedLoopCountExpr)
        SemaRef.Diag(DSA.getConstructLoc(),
                     diag::note_omp_collapse_ordered_expr)
            << 2 << CollapseLoopCountExpr->getSourceRange()
            << OrderedLoopCountExpr->getSourceRange();
      else if (CollapseLoopCountExpr)
        SemaRef.Diag(CollapseLoopCountExpr->getExprLoc(),
                     diag::note_omp_collapse_ordered_expr)
            << 0 << CollapseLoopCountExpr->getSourceRange();
      else
        SemaRef.Diag(OrderedLoopCountExpr->getExprLoc(),
                     diag::note_omp_collapse_ordered_expr)
            << 1 << OrderedLoopCountExpr->getSourceRange();
    }
    return true;
  }
  assert(((For && For->getBody()) || (CXXFor && CXXFor->getBody())) &&
         "No loop body.");
  // Postpone analysis in dependent contexts for ranged for loops.
  if (CXXFor && SemaRef.CurContext->isDependentContext())
    return false;

  OpenMPIterationSpaceChecker ISC(SemaRef, SupportsNonRectangular, DSA,
                                  For ? For->getForLoc() : CXXFor->getForLoc());

  // Check init.
  Stmt *Init = For ? For->getInit() : CXXFor->getBeginStmt();
  if (ISC.checkAndSetInit(Init))
    return true;

  bool HasErrors = false;

  // Check loop variable's type.
  if (ValueDecl *LCDecl = ISC.getLoopDecl()) {
    // OpenMP [2.6, Canonical Loop Form]
    // Var is one of the following:
    //   A variable of signed or unsigned integer type.
    //   For C++, a variable of a random access iterator type.
    //   For C, a variable of a pointer type.
    QualType VarType = LCDecl->getType().getNonReferenceType();
    if (!VarType->isDependentType() && !VarType->isIntegerType() &&
        !VarType->isPointerType() &&
        !(SemaRef.getLangOpts().CPlusPlus && VarType->isOverloadableType())) {
      SemaRef.Diag(Init->getBeginLoc(), diag::err_omp_loop_variable_type)
          << SemaRef.getLangOpts().CPlusPlus;
      HasErrors = true;
    }

    // OpenMP, 2.14.1.1 Data-sharing Attribute Rules for Variables Referenced in
    // a Construct
    // The loop iteration variable(s) in the associated for-loop(s) of a for or
    // parallel for construct is (are) private.
    // The loop iteration variable in the associated for-loop of a simd
    // construct with just one associated for-loop is linear with a
    // constant-linear-step that is the increment of the associated for-loop.
    // Exclude loop var from the list of variables with implicitly defined data
    // sharing attributes.
    VarsWithImplicitDSA.erase(LCDecl);

    assert(isOpenMPLoopDirective(DKind) && "DSA for non-loop vars");

    // Check test-expr.
    HasErrors |= ISC.checkAndSetCond(For ? For->getCond() : CXXFor->getCond());

    // Check incr-expr.
    HasErrors |= ISC.checkAndSetInc(For ? For->getInc() : CXXFor->getInc());
  }

  if (ISC.dependent() || SemaRef.CurContext->isDependentContext() || HasErrors)
    return HasErrors;

  // Build the loop's iteration space representation.
  ResultIterSpaces[CurrentNestedLoopCount].PreCond = ISC.buildPreCond(
      DSA.getCurScope(), For ? For->getCond() : CXXFor->getCond(), Captures);
  ResultIterSpaces[CurrentNestedLoopCount].NumIterations =
      ISC.buildNumIterations(DSA.getCurScope(), ResultIterSpaces,
                             (isOpenMPWorksharingDirective(DKind) ||
                              isOpenMPGenericLoopDirective(DKind) ||
                              isOpenMPTaskLoopDirective(DKind) ||
                              isOpenMPDistributeDirective(DKind) ||
                              isOpenMPLoopTransformationDirective(DKind)),
                             Captures);
  ResultIterSpaces[CurrentNestedLoopCount].CounterVar =
      ISC.buildCounterVar(Captures, DSA);
  ResultIterSpaces[CurrentNestedLoopCount].PrivateCounterVar =
      ISC.buildPrivateCounterVar();
  ResultIterSpaces[CurrentNestedLoopCount].CounterInit = ISC.buildCounterInit();
  ResultIterSpaces[CurrentNestedLoopCount].CounterStep = ISC.buildCounterStep();
  ResultIterSpaces[CurrentNestedLoopCount].InitSrcRange = ISC.getInitSrcRange();
  ResultIterSpaces[CurrentNestedLoopCount].CondSrcRange =
      ISC.getConditionSrcRange();
  ResultIterSpaces[CurrentNestedLoopCount].IncSrcRange =
      ISC.getIncrementSrcRange();
  ResultIterSpaces[CurrentNestedLoopCount].Subtract = ISC.shouldSubtractStep();
  ResultIterSpaces[CurrentNestedLoopCount].IsStrictCompare =
      ISC.isStrictTestOp();
  std::tie(ResultIterSpaces[CurrentNestedLoopCount].MinValue,
           ResultIterSpaces[CurrentNestedLoopCount].MaxValue) =
      ISC.buildMinMaxValues(DSA.getCurScope(), Captures);
  ResultIterSpaces[CurrentNestedLoopCount].FinalCondition =
      ISC.buildFinalCondition(DSA.getCurScope());
  ResultIterSpaces[CurrentNestedLoopCount].IsNonRectangularLB =
      ISC.doesInitDependOnLC();
  ResultIterSpaces[CurrentNestedLoopCount].IsNonRectangularUB =
      ISC.doesCondDependOnLC();
  ResultIterSpaces[CurrentNestedLoopCount].LoopDependentIdx =
      ISC.getLoopDependentIdx();

  HasErrors |=
      (ResultIterSpaces[CurrentNestedLoopCount].PreCond == nullptr ||
       ResultIterSpaces[CurrentNestedLoopCount].NumIterations == nullptr ||
       ResultIterSpaces[CurrentNestedLoopCount].CounterVar == nullptr ||
       ResultIterSpaces[CurrentNestedLoopCount].PrivateCounterVar == nullptr ||
       ResultIterSpaces[CurrentNestedLoopCount].CounterInit == nullptr ||
       ResultIterSpaces[CurrentNestedLoopCount].CounterStep == nullptr);
  if (!HasErrors && DSA.isOrderedRegion()) {
    if (DSA.getOrderedRegionParam().second->getNumForLoops()) {
      if (CurrentNestedLoopCount <
          DSA.getOrderedRegionParam().second->getLoopNumIterations().size()) {
        DSA.getOrderedRegionParam().second->setLoopNumIterations(
            CurrentNestedLoopCount,
            ResultIterSpaces[CurrentNestedLoopCount].NumIterations);
        DSA.getOrderedRegionParam().second->setLoopCounter(
            CurrentNestedLoopCount,
            ResultIterSpaces[CurrentNestedLoopCount].CounterVar);
      }
    }
    for (auto &Pair : DSA.getDoacrossDependClauses()) {
      auto *DependC = dyn_cast<OMPDependClause>(Pair.first);
      auto *DoacrossC = dyn_cast<OMPDoacrossClause>(Pair.first);
      unsigned NumLoops =
          DependC ? DependC->getNumLoops() : DoacrossC->getNumLoops();
      if (CurrentNestedLoopCount >= NumLoops) {
        // Erroneous case - clause has some problems.
        continue;
      }
      if (DependC && DependC->getDependencyKind() == OMPC_DEPEND_sink &&
          Pair.second.size() <= CurrentNestedLoopCount) {
        // Erroneous case - clause has some problems.
        DependC->setLoopData(CurrentNestedLoopCount, nullptr);
        continue;
      }
      OMPDoacrossKind ODK;
      if (DoacrossC && ODK.isSink(DoacrossC) &&
          Pair.second.size() <= CurrentNestedLoopCount) {
        // Erroneous case - clause has some problems.
        DoacrossC->setLoopData(CurrentNestedLoopCount, nullptr);
        continue;
      }
      Expr *CntValue;
      SourceLocation DepLoc =
          DependC ? DependC->getDependencyLoc() : DoacrossC->getDependenceLoc();
      if ((DependC && DependC->getDependencyKind() == OMPC_DEPEND_source) ||
          (DoacrossC && ODK.isSource(DoacrossC)))
        CntValue = ISC.buildOrderedLoopData(
            DSA.getCurScope(),
            ResultIterSpaces[CurrentNestedLoopCount].CounterVar, Captures,
            DepLoc);
      else if (DoacrossC && ODK.isSinkIter(DoacrossC)) {
        Expr *Cnt = SemaRef
                        .DefaultLvalueConversion(
                            ResultIterSpaces[CurrentNestedLoopCount].CounterVar)
                        .get();
        if (!Cnt)
          continue;
        // build CounterVar - 1
        Expr *Inc =
            SemaRef.ActOnIntegerConstant(DoacrossC->getColonLoc(), /*Val=*/1)
                .get();
        CntValue = ISC.buildOrderedLoopData(
            DSA.getCurScope(),
            ResultIterSpaces[CurrentNestedLoopCount].CounterVar, Captures,
            DepLoc, Inc, clang::OO_Minus);
      } else
        CntValue = ISC.buildOrderedLoopData(
            DSA.getCurScope(),
            ResultIterSpaces[CurrentNestedLoopCount].CounterVar, Captures,
            DepLoc, Pair.second[CurrentNestedLoopCount].first,
            Pair.second[CurrentNestedLoopCount].second);
      if (DependC)
        DependC->setLoopData(CurrentNestedLoopCount, CntValue);
      else
        DoacrossC->setLoopData(CurrentNestedLoopCount, CntValue);
    }
  }

  return HasErrors;
}

/// Build 'VarRef = Start.
static ExprResult
buildCounterInit(Sema &SemaRef, Scope *S, SourceLocation Loc, ExprResult VarRef,
                 ExprResult Start, bool IsNonRectangularLB,
                 llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) {
  // Build 'VarRef = Start.
  ExprResult NewStart = IsNonRectangularLB
                            ? Start.get()
                            : tryBuildCapture(SemaRef, Start.get(), Captures);
  if (!NewStart.isUsable())
    return ExprError();
  if (!SemaRef.Context.hasSameType(NewStart.get()->getType(),
                                   VarRef.get()->getType())) {
    NewStart = SemaRef.PerformImplicitConversion(
        NewStart.get(), VarRef.get()->getType(), Sema::AA_Converting,
        /*AllowExplicit=*/true);
    if (!NewStart.isUsable())
      return ExprError();
  }

  ExprResult Init =
      SemaRef.BuildBinOp(S, Loc, BO_Assign, VarRef.get(), NewStart.get());
  return Init;
}

/// Build 'VarRef = Start + Iter * Step'.
static ExprResult buildCounterUpdate(
    Sema &SemaRef, Scope *S, SourceLocation Loc, ExprResult VarRef,
    ExprResult Start, ExprResult Iter, ExprResult Step, bool Subtract,
    bool IsNonRectangularLB,
    llvm::MapVector<const Expr *, DeclRefExpr *> *Captures = nullptr) {
  // Add parentheses (for debugging purposes only).
  Iter = SemaRef.ActOnParenExpr(Loc, Loc, Iter.get());
  if (!VarRef.isUsable() || !Start.isUsable() || !Iter.isUsable() ||
      !Step.isUsable())
    return ExprError();

  ExprResult NewStep = Step;
  if (Captures)
    NewStep = tryBuildCapture(SemaRef, Step.get(), *Captures);
  if (NewStep.isInvalid())
    return ExprError();
  ExprResult Update =
      SemaRef.BuildBinOp(S, Loc, BO_Mul, Iter.get(), NewStep.get());
  if (!Update.isUsable())
    return ExprError();

  // Try to build 'VarRef = Start, VarRef (+|-)= Iter * Step' or
  // 'VarRef = Start (+|-) Iter * Step'.
  if (!Start.isUsable())
    return ExprError();
  ExprResult NewStart = SemaRef.ActOnParenExpr(Loc, Loc, Start.get());
  if (!NewStart.isUsable())
    return ExprError();
  if (Captures && !IsNonRectangularLB)
    NewStart = tryBuildCapture(SemaRef, Start.get(), *Captures);
  if (NewStart.isInvalid())
    return ExprError();

  // First attempt: try to build 'VarRef = Start, VarRef += Iter * Step'.
  ExprResult SavedUpdate = Update;
  ExprResult UpdateVal;
  if (VarRef.get()->getType()->isOverloadableType() ||
      NewStart.get()->getType()->isOverloadableType() ||
      Update.get()->getType()->isOverloadableType()) {
    Sema::TentativeAnalysisScope Trap(SemaRef);

    Update =
        SemaRef.BuildBinOp(S, Loc, BO_Assign, VarRef.get(), NewStart.get());
    if (Update.isUsable()) {
      UpdateVal =
          SemaRef.BuildBinOp(S, Loc, Subtract ? BO_SubAssign : BO_AddAssign,
                             VarRef.get(), SavedUpdate.get());
      if (UpdateVal.isUsable()) {
        Update = SemaRef.CreateBuiltinBinOp(Loc, BO_Comma, Update.get(),
                                            UpdateVal.get());
      }
    }
  }

  // Second attempt: try to build 'VarRef = Start (+|-) Iter * Step'.
  if (!Update.isUsable() || !UpdateVal.isUsable()) {
    Update = SemaRef.BuildBinOp(S, Loc, Subtract ? BO_Sub : BO_Add,
                                NewStart.get(), SavedUpdate.get());
    if (!Update.isUsable())
      return ExprError();

    if (!SemaRef.Context.hasSameType(Update.get()->getType(),
                                     VarRef.get()->getType())) {
      Update = SemaRef.PerformImplicitConversion(
          Update.get(), VarRef.get()->getType(), Sema::AA_Converting, true);
      if (!Update.isUsable())
        return ExprError();
    }

    Update = SemaRef.BuildBinOp(S, Loc, BO_Assign, VarRef.get(), Update.get());
  }
  return Update;
}

/// Convert integer expression \a E to make it have at least \a Bits
/// bits.
static ExprResult widenIterationCount(unsigned Bits, Expr *E, Sema &SemaRef) {
  if (E == nullptr)
    return ExprError();
  ASTContext &C = SemaRef.Context;
  QualType OldType = E->getType();
  unsigned HasBits = C.getTypeSize(OldType);
  if (HasBits >= Bits)
    return ExprResult(E);
  // OK to convert to signed, because new type has more bits than old.
  QualType NewType = C.getIntTypeForBitwidth(Bits, /* Signed */ true);
  return SemaRef.PerformImplicitConversion(E, NewType, Sema::AA_Converting,
                                           true);
}

/// Check if the given expression \a E is a constant integer that fits
/// into \a Bits bits.
static bool fitsInto(unsigned Bits, bool Signed, const Expr *E, Sema &SemaRef) {
  if (E == nullptr)
    return false;
  if (std::optional<llvm::APSInt> Result =
          E->getIntegerConstantExpr(SemaRef.Context))
    return Signed ? Result->isSignedIntN(Bits) : Result->isIntN(Bits);
  return false;
}

/// Build preinits statement for the given declarations.
static Stmt *buildPreInits(ASTContext &Context,
                           MutableArrayRef<Decl *> PreInits) {
  if (!PreInits.empty()) {
    return new (Context) DeclStmt(
        DeclGroupRef::Create(Context, PreInits.begin(), PreInits.size()),
        SourceLocation(), SourceLocation());
  }
  return nullptr;
}

/// Append the \p Item or the content of a CompoundStmt to the list \p
/// TargetList.
///
/// A CompoundStmt is used as container in case multiple statements need to be
/// stored in lieu of using an explicit list. Flattening is necessary because
/// contained DeclStmts need to be visible after the execution of the list. Used
/// for OpenMP pre-init declarations/statements.
static void appendFlattenedStmtList(SmallVectorImpl<Stmt *> &TargetList,
                                     Stmt *Item) {
  // nullptr represents an empty list.
  if (!Item)
    return;

  if (auto *CS = dyn_cast<CompoundStmt>(Item))
    llvm::append_range(TargetList, CS->body());
  else
    TargetList.push_back(Item);
}

/// Build preinits statement for the given declarations.
static Stmt *
buildPreInits(ASTContext &Context,
              const llvm::MapVector<const Expr *, DeclRefExpr *> &Captures) {
  if (!Captures.empty()) {
    SmallVector<Decl *, 16> PreInits;
    for (const auto &Pair : Captures)
      PreInits.push_back(Pair.second->getDecl());
    return buildPreInits(Context, PreInits);
  }
  return nullptr;
}

/// Build pre-init statement for the given statements.
static Stmt *buildPreInits(ASTContext &Context, ArrayRef<Stmt *> PreInits) {
  if (PreInits.empty())
    return nullptr;

  SmallVector<Stmt *> Stmts;
  for (Stmt *S : PreInits)
    appendFlattenedStmtList(Stmts, S);
  return CompoundStmt::Create(Context, PreInits, FPOptionsOverride(), {}, {});
}

/// Build postupdate expression for the given list of postupdates expressions.
static Expr *buildPostUpdate(Sema &S, ArrayRef<Expr *> PostUpdates) {
  Expr *PostUpdate = nullptr;
  if (!PostUpdates.empty()) {
    for (Expr *E : PostUpdates) {
      Expr *ConvE = S.BuildCStyleCastExpr(
                         E->getExprLoc(),
                         S.Context.getTrivialTypeSourceInfo(S.Context.VoidTy),
                         E->getExprLoc(), E)
                        .get();
      PostUpdate = PostUpdate
                       ? S.CreateBuiltinBinOp(ConvE->getExprLoc(), BO_Comma,
                                              PostUpdate, ConvE)
                             .get()
                       : ConvE;
    }
  }
  return PostUpdate;
}

/// Called on a for stmt to check itself and nested loops (if any).
/// \return Returns 0 if one of the collapsed stmts is not canonical for loop,
/// number of collapsed loops otherwise.
static unsigned
checkOpenMPLoop(OpenMPDirectiveKind DKind, Expr *CollapseLoopCountExpr,
                Expr *OrderedLoopCountExpr, Stmt *AStmt, Sema &SemaRef,
                DSAStackTy &DSA,
                SemaOpenMP::VarsWithInheritedDSAType &VarsWithImplicitDSA,
                OMPLoopBasedDirective::HelperExprs &Built) {
  unsigned NestedLoopCount = 1;
  bool SupportsNonPerfectlyNested = (SemaRef.LangOpts.OpenMP >= 50) &&
                                    !isOpenMPLoopTransformationDirective(DKind);

  if (CollapseLoopCountExpr) {
    // Found 'collapse' clause - calculate collapse number.
    Expr::EvalResult Result;
    if (!CollapseLoopCountExpr->isValueDependent() &&
        CollapseLoopCountExpr->EvaluateAsInt(Result, SemaRef.getASTContext())) {
      NestedLoopCount = Result.Val.getInt().getLimitedValue();
    } else {
      Built.clear(/*Size=*/1);
      return 1;
    }
  }
  unsigned OrderedLoopCount = 1;
  if (OrderedLoopCountExpr) {
    // Found 'ordered' clause - calculate collapse number.
    Expr::EvalResult EVResult;
    if (!OrderedLoopCountExpr->isValueDependent() &&
        OrderedLoopCountExpr->EvaluateAsInt(EVResult,
                                            SemaRef.getASTContext())) {
      llvm::APSInt Result = EVResult.Val.getInt();
      if (Result.getLimitedValue() < NestedLoopCount) {
        SemaRef.Diag(OrderedLoopCountExpr->getExprLoc(),
                     diag::err_omp_wrong_ordered_loop_count)
            << OrderedLoopCountExpr->getSourceRange();
        SemaRef.Diag(CollapseLoopCountExpr->getExprLoc(),
                     diag::note_collapse_loop_count)
            << CollapseLoopCountExpr->getSourceRange();
      }
      OrderedLoopCount = Result.getLimitedValue();
    } else {
      Built.clear(/*Size=*/1);
      return 1;
    }
  }
  // This is helper routine for loop directives (e.g., 'for', 'simd',
  // 'for simd', etc.).
  llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
  unsigned NumLoops = std::max(OrderedLoopCount, NestedLoopCount);
  SmallVector<LoopIterationSpace, 4> IterSpaces(NumLoops);
  if (!OMPLoopBasedDirective::doForAllLoops(
          AStmt->IgnoreContainers(!isOpenMPLoopTransformationDirective(DKind)),
          SupportsNonPerfectlyNested, NumLoops,
          [DKind, &SemaRef, &DSA, NumLoops, NestedLoopCount,
           CollapseLoopCountExpr, OrderedLoopCountExpr, &VarsWithImplicitDSA,
           &IterSpaces, &Captures](unsigned Cnt, Stmt *CurStmt) {
            if (checkOpenMPIterationSpace(
                    DKind, CurStmt, SemaRef, DSA, Cnt, NestedLoopCount,
                    NumLoops, CollapseLoopCountExpr, OrderedLoopCountExpr,
                    VarsWithImplicitDSA, IterSpaces, Captures))
              return true;
            if (Cnt > 0 && Cnt >= NestedLoopCount &&
                IterSpaces[Cnt].CounterVar) {
              // Handle initialization of captured loop iterator variables.
              auto *DRE = cast<DeclRefExpr>(IterSpaces[Cnt].CounterVar);
              if (isa<OMPCapturedExprDecl>(DRE->getDecl())) {
                Captures[DRE] = DRE;
              }
            }
            return false;
          },
          [&SemaRef, &Captures](OMPLoopTransformationDirective *Transform) {
            Stmt *DependentPreInits = Transform->getPreInits();
            if (!DependentPreInits)
              return;

            // Search for pre-init declared variables that need to be captured
            // to be referenceable inside the directive.
            SmallVector<Stmt *> Constituents;
            appendFlattenedStmtList(Constituents, DependentPreInits);
            for (Stmt *S : Constituents) {
              if (auto *DC = dyn_cast<DeclStmt>(S)) {
                for (Decl *C : DC->decls()) {
                  auto *D = cast<VarDecl>(C);
                  DeclRefExpr *Ref = buildDeclRefExpr(
                      SemaRef, D, D->getType().getNonReferenceType(),
                      Transform->getBeginLoc());
                  Captures[Ref] = Ref;
                }
              }
            }
          }))
    return 0;

  Built.clear(/* size */ NestedLoopCount);

  if (SemaRef.CurContext->isDependentContext())
    return NestedLoopCount;

  // An example of what is generated for the following code:
  //
  //   #pragma omp simd collapse(2) ordered(2)
  //   for (i = 0; i < NI; ++i)
  //     for (k = 0; k < NK; ++k)
  //       for (j = J0; j < NJ; j+=2) {
  //         <loop body>
  //       }
  //
  // We generate the code below.
  // Note: the loop body may be outlined in CodeGen.
  // Note: some counters may be C++ classes, operator- is used to find number of
  // iterations and operator+= to calculate counter value.
  // Note: decltype(NumIterations) must be integer type (in 'omp for', only i32
  // or i64 is currently supported).
  //
  //   #define NumIterations (NI * ((NJ - J0 - 1 + 2) / 2))
  //   for (int[32|64]_t IV = 0; IV < NumIterations; ++IV ) {
  //     .local.i = IV / ((NJ - J0 - 1 + 2) / 2);
  //     .local.j = J0 + (IV % ((NJ - J0 - 1 + 2) / 2)) * 2;
  //     // similar updates for vars in clauses (e.g. 'linear')
  //     <loop body (using local i and j)>
  //   }
  //   i = NI; // assign final values of counters
  //   j = NJ;
  //

  // Last iteration number is (I1 * I2 * ... In) - 1, where I1, I2 ... In are
  // the iteration counts of the collapsed for loops.
  // Precondition tests if there is at least one iteration (all conditions are
  // true).
  auto PreCond = ExprResult(IterSpaces[0].PreCond);
  Expr *N0 = IterSpaces[0].NumIterations;
  ExprResult LastIteration32 =
      widenIterationCount(/*Bits=*/32,
                          SemaRef
                              .PerformImplicitConversion(
                                  N0->IgnoreImpCasts(), N0->getType(),
                                  Sema::AA_Converting, /*AllowExplicit=*/true)
                              .get(),
                          SemaRef);
  ExprResult LastIteration64 = widenIterationCount(
      /*Bits=*/64,
      SemaRef
          .PerformImplicitConversion(N0->IgnoreImpCasts(), N0->getType(),
                                     Sema::AA_Converting,
                                     /*AllowExplicit=*/true)
          .get(),
      SemaRef);

  if (!LastIteration32.isUsable() || !LastIteration64.isUsable())
    return NestedLoopCount;

  ASTContext &C = SemaRef.Context;
  bool AllCountsNeedLessThan32Bits = C.getTypeSize(N0->getType()) < 32;

  Scope *CurScope = DSA.getCurScope();
  for (unsigned Cnt = 1; Cnt < NestedLoopCount; ++Cnt) {
    if (PreCond.isUsable()) {
      PreCond =
          SemaRef.BuildBinOp(CurScope, PreCond.get()->getExprLoc(), BO_LAnd,
                             PreCond.get(), IterSpaces[Cnt].PreCond);
    }
    Expr *N = IterSpaces[Cnt].NumIterations;
    SourceLocation Loc = N->getExprLoc();
    AllCountsNeedLessThan32Bits &= C.getTypeSize(N->getType()) < 32;
    if (LastIteration32.isUsable())
      LastIteration32 = SemaRef.BuildBinOp(
          CurScope, Loc, BO_Mul, LastIteration32.get(),
          SemaRef
              .PerformImplicitConversion(N->IgnoreImpCasts(), N->getType(),
                                         Sema::AA_Converting,
                                         /*AllowExplicit=*/true)
              .get());
    if (LastIteration64.isUsable())
      LastIteration64 = SemaRef.BuildBinOp(
          CurScope, Loc, BO_Mul, LastIteration64.get(),
          SemaRef
              .PerformImplicitConversion(N->IgnoreImpCasts(), N->getType(),
                                         Sema::AA_Converting,
                                         /*AllowExplicit=*/true)
              .get());
  }

  // Choose either the 32-bit or 64-bit version.
  ExprResult LastIteration = LastIteration64;
  if (SemaRef.getLangOpts().OpenMPOptimisticCollapse ||
      (LastIteration32.isUsable() &&
       C.getTypeSize(LastIteration32.get()->getType()) == 32 &&
       (AllCountsNeedLessThan32Bits || NestedLoopCount == 1 ||
        fitsInto(
            /*Bits=*/32,
            LastIteration32.get()->getType()->hasSignedIntegerRepresentation(),
            LastIteration64.get(), SemaRef))))
    LastIteration = LastIteration32;
  QualType VType = LastIteration.get()->getType();
  QualType RealVType = VType;
  QualType StrideVType = VType;
  if (isOpenMPTaskLoopDirective(DKind)) {
    VType =
        SemaRef.Context.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/0);
    StrideVType =
        SemaRef.Context.getIntTypeForBitwidth(/*DestWidth=*/64, /*Signed=*/1);
  }

  if (!LastIteration.isUsable())
    return 0;

  // Save the number of iterations.
  ExprResult NumIterations = LastIteration;
  {
    LastIteration = SemaRef.BuildBinOp(
        CurScope, LastIteration.get()->getExprLoc(), BO_Sub,
        LastIteration.get(),
        SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get());
    if (!LastIteration.isUsable())
      return 0;
  }

  // Calculate the last iteration number beforehand instead of doing this on
  // each iteration. Do not do this if the number of iterations may be kfold-ed.
  bool IsConstant = LastIteration.get()->isIntegerConstantExpr(SemaRef.Context);
  ExprResult CalcLastIteration;
  if (!IsConstant) {
    ExprResult SaveRef =
        tryBuildCapture(SemaRef, LastIteration.get(), Captures);
    LastIteration = SaveRef;

    // Prepare SaveRef + 1.
    NumIterations = SemaRef.BuildBinOp(
        CurScope, SaveRef.get()->getExprLoc(), BO_Add, SaveRef.get(),
        SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get());
    if (!NumIterations.isUsable())
      return 0;
  }

  SourceLocation InitLoc = IterSpaces[0].InitSrcRange.getBegin();

  // Build variables passed into runtime, necessary for worksharing directives.
  ExprResult LB, UB, IL, ST, EUB, CombLB, CombUB, PrevLB, PrevUB, CombEUB;
  if (isOpenMPWorksharingDirective(DKind) || isOpenMPTaskLoopDirective(DKind) ||
      isOpenMPDistributeDirective(DKind) ||
      isOpenMPGenericLoopDirective(DKind) ||
      isOpenMPLoopTransformationDirective(DKind)) {
    // Lower bound variable, initialized with zero.
    VarDecl *LBDecl = buildVarDecl(SemaRef, InitLoc, VType, ".omp.lb");
    LB = buildDeclRefExpr(SemaRef, LBDecl, VType, InitLoc);
    SemaRef.AddInitializerToDecl(LBDecl,
                                 SemaRef.ActOnIntegerConstant(InitLoc, 0).get(),
                                 /*DirectInit*/ false);

    // Upper bound variable, initialized with last iteration number.
    VarDecl *UBDecl = buildVarDecl(SemaRef, InitLoc, VType, ".omp.ub");
    UB = buildDeclRefExpr(SemaRef, UBDecl, VType, InitLoc);
    SemaRef.AddInitializerToDecl(UBDecl, LastIteration.get(),
                                 /*DirectInit*/ false);

    // A 32-bit variable-flag where runtime returns 1 for the last iteration.
    // This will be used to implement clause 'lastprivate'.
    QualType Int32Ty = SemaRef.Context.getIntTypeForBitwidth(32, true);
    VarDecl *ILDecl = buildVarDecl(SemaRef, InitLoc, Int32Ty, ".omp.is_last");
    IL = buildDeclRefExpr(SemaRef, ILDecl, Int32Ty, InitLoc);
    SemaRef.AddInitializerToDecl(ILDecl,
                                 SemaRef.ActOnIntegerConstant(InitLoc, 0).get(),
                                 /*DirectInit*/ false);

    // Stride variable returned by runtime (we initialize it to 1 by default).
    VarDecl *STDecl =
        buildVarDecl(SemaRef, InitLoc, StrideVType, ".omp.stride");
    ST = buildDeclRefExpr(SemaRef, STDecl, StrideVType, InitLoc);
    SemaRef.AddInitializerToDecl(STDecl,
                                 SemaRef.ActOnIntegerConstant(InitLoc, 1).get(),
                                 /*DirectInit*/ false);

    // Build expression: UB = min(UB, LastIteration)
    // It is necessary for CodeGen of directives with static scheduling.
    ExprResult IsUBGreater = SemaRef.BuildBinOp(CurScope, InitLoc, BO_GT,
                                                UB.get(), LastIteration.get());
    ExprResult CondOp = SemaRef.ActOnConditionalOp(
        LastIteration.get()->getExprLoc(), InitLoc, IsUBGreater.get(),
        LastIteration.get(), UB.get());
    EUB = SemaRef.BuildBinOp(CurScope, InitLoc, BO_Assign, UB.get(),
                             CondOp.get());
    EUB = SemaRef.ActOnFinishFullExpr(EUB.get(), /*DiscardedValue*/ false);

    // If we have a combined directive that combines 'distribute', 'for' or
    // 'simd' we need to be able to access the bounds of the schedule of the
    // enclosing region. E.g. in 'distribute parallel for' the bounds obtained
    // by scheduling 'distribute' have to be passed to the schedule of 'for'.
    if (isOpenMPLoopBoundSharingDirective(DKind)) {
      // Lower bound variable, initialized with zero.
      VarDecl *CombLBDecl =
          buildVarDecl(SemaRef, InitLoc, VType, ".omp.comb.lb");
      CombLB = buildDeclRefExpr(SemaRef, CombLBDecl, VType, InitLoc);
      SemaRef.AddInitializerToDecl(
          CombLBDecl, SemaRef.ActOnIntegerConstant(InitLoc, 0).get(),
          /*DirectInit*/ false);

      // Upper bound variable, initialized with last iteration number.
      VarDecl *CombUBDecl =
          buildVarDecl(SemaRef, InitLoc, VType, ".omp.comb.ub");
      CombUB = buildDeclRefExpr(SemaRef, CombUBDecl, VType, InitLoc);
      SemaRef.AddInitializerToDecl(CombUBDecl, LastIteration.get(),
                                   /*DirectInit*/ false);

      ExprResult CombIsUBGreater = SemaRef.BuildBinOp(
          CurScope, InitLoc, BO_GT, CombUB.get(), LastIteration.get());
      ExprResult CombCondOp =
          SemaRef.ActOnConditionalOp(InitLoc, InitLoc, CombIsUBGreater.get(),
                                     LastIteration.get(), CombUB.get());
      CombEUB = SemaRef.BuildBinOp(CurScope, InitLoc, BO_Assign, CombUB.get(),
                                   CombCondOp.get());
      CombEUB =
          SemaRef.ActOnFinishFullExpr(CombEUB.get(), /*DiscardedValue*/ false);

      const CapturedDecl *CD = cast<CapturedStmt>(AStmt)->getCapturedDecl();
      // We expect to have at least 2 more parameters than the 'parallel'
      // directive does - the lower and upper bounds of the previous schedule.
      assert(CD->getNumParams() >= 4 &&
             "Unexpected number of parameters in loop combined directive");

      // Set the proper type for the bounds given what we learned from the
      // enclosed loops.
      ImplicitParamDecl *PrevLBDecl = CD->getParam(/*PrevLB=*/2);
      ImplicitParamDecl *PrevUBDecl = CD->getParam(/*PrevUB=*/3);

      // Previous lower and upper bounds are obtained from the region
      // parameters.
      PrevLB =
          buildDeclRefExpr(SemaRef, PrevLBDecl, PrevLBDecl->getType(), InitLoc);
      PrevUB =
          buildDeclRefExpr(SemaRef, PrevUBDecl, PrevUBDecl->getType(), InitLoc);
    }
  }

  // Build the iteration variable and its initialization before loop.
  ExprResult IV;
  ExprResult Init, CombInit;
  {
    VarDecl *IVDecl = buildVarDecl(SemaRef, InitLoc, RealVType, ".omp.iv");
    IV = buildDeclRefExpr(SemaRef, IVDecl, RealVType, InitLoc);
    Expr *RHS = (isOpenMPWorksharingDirective(DKind) ||
                 isOpenMPGenericLoopDirective(DKind) ||
                 isOpenMPTaskLoopDirective(DKind) ||
                 isOpenMPDistributeDirective(DKind) ||
                 isOpenMPLoopTransformationDirective(DKind))
                    ? LB.get()
                    : SemaRef.ActOnIntegerConstant(SourceLocation(), 0).get();
    Init = SemaRef.BuildBinOp(CurScope, InitLoc, BO_Assign, IV.get(), RHS);
    Init = SemaRef.ActOnFinishFullExpr(Init.get(), /*DiscardedValue*/ false);

    if (isOpenMPLoopBoundSharingDirective(DKind)) {
      Expr *CombRHS =
          (isOpenMPWorksharingDirective(DKind) ||
           isOpenMPGenericLoopDirective(DKind) ||
           isOpenMPTaskLoopDirective(DKind) ||
           isOpenMPDistributeDirective(DKind))
              ? CombLB.get()
              : SemaRef.ActOnIntegerConstant(SourceLocation(), 0).get();
      CombInit =
          SemaRef.BuildBinOp(CurScope, InitLoc, BO_Assign, IV.get(), CombRHS);
      CombInit =
          SemaRef.ActOnFinishFullExpr(CombInit.get(), /*DiscardedValue*/ false);
    }
  }

  bool UseStrictCompare =
      RealVType->hasUnsignedIntegerRepresentation() &&
      llvm::all_of(IterSpaces, [](const LoopIterationSpace &LIS) {
        return LIS.IsStrictCompare;
      });
  // Loop condition (IV < NumIterations) or (IV <= UB or IV < UB + 1 (for
  // unsigned IV)) for worksharing loops.
  SourceLocation CondLoc = AStmt->getBeginLoc();
  Expr *BoundUB = UB.get();
  if (UseStrictCompare) {
    BoundUB =
        SemaRef
            .BuildBinOp(CurScope, CondLoc, BO_Add, BoundUB,
                        SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get())
            .get();
    BoundUB =
        SemaRef.ActOnFinishFullExpr(BoundUB, /*DiscardedValue*/ false).get();
  }
  ExprResult Cond =
      (isOpenMPWorksharingDirective(DKind) ||
       isOpenMPGenericLoopDirective(DKind) ||
       isOpenMPTaskLoopDirective(DKind) || isOpenMPDistributeDirective(DKind) ||
       isOpenMPLoopTransformationDirective(DKind))
          ? SemaRef.BuildBinOp(CurScope, CondLoc,
                               UseStrictCompare ? BO_LT : BO_LE, IV.get(),
                               BoundUB)
          : SemaRef.BuildBinOp(CurScope, CondLoc, BO_LT, IV.get(),
                               NumIterations.get());
  ExprResult CombDistCond;
  if (isOpenMPLoopBoundSharingDirective(DKind)) {
    CombDistCond = SemaRef.BuildBinOp(CurScope, CondLoc, BO_LT, IV.get(),
                                      NumIterations.get());
  }

  ExprResult CombCond;
  if (isOpenMPLoopBoundSharingDirective(DKind)) {
    Expr *BoundCombUB = CombUB.get();
    if (UseStrictCompare) {
      BoundCombUB =
          SemaRef
              .BuildBinOp(
                  CurScope, CondLoc, BO_Add, BoundCombUB,
                  SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get())
              .get();
      BoundCombUB =
          SemaRef.ActOnFinishFullExpr(BoundCombUB, /*DiscardedValue*/ false)
              .get();
    }
    CombCond =
        SemaRef.BuildBinOp(CurScope, CondLoc, UseStrictCompare ? BO_LT : BO_LE,
                           IV.get(), BoundCombUB);
  }
  // Loop increment (IV = IV + 1)
  SourceLocation IncLoc = AStmt->getBeginLoc();
  ExprResult Inc =
      SemaRef.BuildBinOp(CurScope, IncLoc, BO_Add, IV.get(),
                         SemaRef.ActOnIntegerConstant(IncLoc, 1).get());
  if (!Inc.isUsable())
    return 0;
  Inc = SemaRef.BuildBinOp(CurScope, IncLoc, BO_Assign, IV.get(), Inc.get());
  Inc = SemaRef.ActOnFinishFullExpr(Inc.get(), /*DiscardedValue*/ false);
  if (!Inc.isUsable())
    return 0;

  // Increments for worksharing loops (LB = LB + ST; UB = UB + ST).
  // Used for directives with static scheduling.
  // In combined construct, add combined version that use CombLB and CombUB
  // base variables for the update
  ExprResult NextLB, NextUB, CombNextLB, CombNextUB;
  if (isOpenMPWorksharingDirective(DKind) || isOpenMPTaskLoopDirective(DKind) ||
      isOpenMPGenericLoopDirective(DKind) ||
      isOpenMPDistributeDirective(DKind) ||
      isOpenMPLoopTransformationDirective(DKind)) {
    // LB + ST
    NextLB = SemaRef.BuildBinOp(CurScope, IncLoc, BO_Add, LB.get(), ST.get());
    if (!NextLB.isUsable())
      return 0;
    // LB = LB + ST
    NextLB =
        SemaRef.BuildBinOp(CurScope, IncLoc, BO_Assign, LB.get(), NextLB.get());
    NextLB =
        SemaRef.ActOnFinishFullExpr(NextLB.get(), /*DiscardedValue*/ false);
    if (!NextLB.isUsable())
      return 0;
    // UB + ST
    NextUB = SemaRef.BuildBinOp(CurScope, IncLoc, BO_Add, UB.get(), ST.get());
    if (!NextUB.isUsable())
      return 0;
    // UB = UB + ST
    NextUB =
        SemaRef.BuildBinOp(CurScope, IncLoc, BO_Assign, UB.get(), NextUB.get());
    NextUB =
        SemaRef.ActOnFinishFullExpr(NextUB.get(), /*DiscardedValue*/ false);
    if (!NextUB.isUsable())
      return 0;
    if (isOpenMPLoopBoundSharingDirective(DKind)) {
      CombNextLB =
          SemaRef.BuildBinOp(CurScope, IncLoc, BO_Add, CombLB.get(), ST.get());
      if (!NextLB.isUsable())
        return 0;
      // LB = LB + ST
      CombNextLB = SemaRef.BuildBinOp(CurScope, IncLoc, BO_Assign, CombLB.get(),
                                      CombNextLB.get());
      CombNextLB = SemaRef.ActOnFinishFullExpr(CombNextLB.get(),
                                               /*DiscardedValue*/ false);
      if (!CombNextLB.isUsable())
        return 0;
      // UB + ST
      CombNextUB =
          SemaRef.BuildBinOp(CurScope, IncLoc, BO_Add, CombUB.get(), ST.get());
      if (!CombNextUB.isUsable())
        return 0;
      // UB = UB + ST
      CombNextUB = SemaRef.BuildBinOp(CurScope, IncLoc, BO_Assign, CombUB.get(),
                                      CombNextUB.get());
      CombNextUB = SemaRef.ActOnFinishFullExpr(CombNextUB.get(),
                                               /*DiscardedValue*/ false);
      if (!CombNextUB.isUsable())
        return 0;
    }
  }

  // Create increment expression for distribute loop when combined in a same
  // directive with for as IV = IV + ST; ensure upper bound expression based
  // on PrevUB instead of NumIterations - used to implement 'for' when found
  // in combination with 'distribute', like in 'distribute parallel for'
  SourceLocation DistIncLoc = AStmt->getBeginLoc();
  ExprResult DistCond, DistInc, PrevEUB, ParForInDistCond;
  if (isOpenMPLoopBoundSharingDirective(DKind)) {
    DistCond = SemaRef.BuildBinOp(
        CurScope, CondLoc, UseStrictCompare ? BO_LT : BO_LE, IV.get(), BoundUB);
    assert(DistCond.isUsable() && "distribute cond expr was not built");

    DistInc =
        SemaRef.BuildBinOp(CurScope, DistIncLoc, BO_Add, IV.get(), ST.get());
    assert(DistInc.isUsable() && "distribute inc expr was not built");
    DistInc = SemaRef.BuildBinOp(CurScope, DistIncLoc, BO_Assign, IV.get(),
                                 DistInc.get());
    DistInc =
        SemaRef.ActOnFinishFullExpr(DistInc.get(), /*DiscardedValue*/ false);
    assert(DistInc.isUsable() && "distribute inc expr was not built");

    // Build expression: UB = min(UB, prevUB) for #for in composite or combined
    // construct
    ExprResult NewPrevUB = PrevUB;
    SourceLocation DistEUBLoc = AStmt->getBeginLoc();
    if (!SemaRef.Context.hasSameType(UB.get()->getType(),
                                     PrevUB.get()->getType())) {
      NewPrevUB = SemaRef.BuildCStyleCastExpr(
          DistEUBLoc,
          SemaRef.Context.getTrivialTypeSourceInfo(UB.get()->getType()),
          DistEUBLoc, NewPrevUB.get());
      if (!NewPrevUB.isUsable())
        return 0;
    }
    ExprResult IsUBGreater = SemaRef.BuildBinOp(CurScope, DistEUBLoc, BO_GT,
                                                UB.get(), NewPrevUB.get());
    ExprResult CondOp = SemaRef.ActOnConditionalOp(
        DistEUBLoc, DistEUBLoc, IsUBGreater.get(), NewPrevUB.get(), UB.get());
    PrevEUB = SemaRef.BuildBinOp(CurScope, DistIncLoc, BO_Assign, UB.get(),
                                 CondOp.get());
    PrevEUB =
        SemaRef.ActOnFinishFullExpr(PrevEUB.get(), /*DiscardedValue*/ false);

    // Build IV <= PrevUB or IV < PrevUB + 1 for unsigned IV to be used in
    // parallel for is in combination with a distribute directive with
    // schedule(static, 1)
    Expr *BoundPrevUB = PrevUB.get();
    if (UseStrictCompare) {
      BoundPrevUB =
          SemaRef
              .BuildBinOp(
                  CurScope, CondLoc, BO_Add, BoundPrevUB,
                  SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get())
              .get();
      BoundPrevUB =
          SemaRef.ActOnFinishFullExpr(BoundPrevUB, /*DiscardedValue*/ false)
              .get();
    }
    ParForInDistCond =
        SemaRef.BuildBinOp(CurScope, CondLoc, UseStrictCompare ? BO_LT : BO_LE,
                           IV.get(), BoundPrevUB);
  }

  // Build updates and final values of the loop counters.
  bool HasErrors = false;
  Built.Counters.resize(NestedLoopCount);
  Built.Inits.resize(NestedLoopCount);
  Built.Updates.resize(NestedLoopCount);
  Built.Finals.resize(NestedLoopCount);
  Built.DependentCounters.resize(NestedLoopCount);
  Built.DependentInits.resize(NestedLoopCount);
  Built.FinalsConditions.resize(NestedLoopCount);
  {
    // We implement the following algorithm for obtaining the
    // original loop iteration variable values based on the
    // value of the collapsed loop iteration variable IV.
    //
    // Let n+1 be the number of collapsed loops in the nest.
    // Iteration variables (I0, I1, .... In)
    // Iteration counts (N0, N1, ... Nn)
    //
    // Acc = IV;
    //
    // To compute Ik for loop k, 0 <= k <= n, generate:
    //    Prod = N(k+1) * N(k+2) * ... * Nn;
    //    Ik = Acc / Prod;
    //    Acc -= Ik * Prod;
    //
    ExprResult Acc = IV;
    for (unsigned int Cnt = 0; Cnt < NestedLoopCount; ++Cnt) {
      LoopIterationSpace &IS = IterSpaces[Cnt];
      SourceLocation UpdLoc = IS.IncSrcRange.getBegin();
      ExprResult Iter;

      // Compute prod
      ExprResult Prod = SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get();
      for (unsigned int K = Cnt + 1; K < NestedLoopCount; ++K)
        Prod = SemaRef.BuildBinOp(CurScope, UpdLoc, BO_Mul, Prod.get(),
                                  IterSpaces[K].NumIterations);

      // Iter = Acc / Prod
      // If there is at least one more inner loop to avoid
      // multiplication by 1.
      if (Cnt + 1 < NestedLoopCount)
        Iter =
            SemaRef.BuildBinOp(CurScope, UpdLoc, BO_Div, Acc.get(), Prod.get());
      else
        Iter = Acc;
      if (!Iter.isUsable()) {
        HasErrors = true;
        break;
      }

      // Update Acc:
      // Acc -= Iter * Prod
      // Check if there is at least one more inner loop to avoid
      // multiplication by 1.
      if (Cnt + 1 < NestedLoopCount)
        Prod = SemaRef.BuildBinOp(CurScope, UpdLoc, BO_Mul, Iter.get(),
                                  Prod.get());
      else
        Prod = Iter;
      Acc = SemaRef.BuildBinOp(CurScope, UpdLoc, BO_Sub, Acc.get(), Prod.get());

      // Build update: IS.CounterVar(Private) = IS.Start + Iter * IS.Step
      auto *VD = cast<VarDecl>(cast<DeclRefExpr>(IS.CounterVar)->getDecl());
      DeclRefExpr *CounterVar = buildDeclRefExpr(
          SemaRef, VD, IS.CounterVar->getType(), IS.CounterVar->getExprLoc(),
          /*RefersToCapture=*/true);
      ExprResult Init =
          buildCounterInit(SemaRef, CurScope, UpdLoc, CounterVar,
                           IS.CounterInit, IS.IsNonRectangularLB, Captures);
      if (!Init.isUsable()) {
        HasErrors = true;
        break;
      }
      ExprResult Update = buildCounterUpdate(
          SemaRef, CurScope, UpdLoc, CounterVar, IS.CounterInit, Iter,
          IS.CounterStep, IS.Subtract, IS.IsNonRectangularLB, &Captures);
      if (!Update.isUsable()) {
        HasErrors = true;
        break;
      }

      // Build final: IS.CounterVar = IS.Start + IS.NumIters * IS.Step
      ExprResult Final =
          buildCounterUpdate(SemaRef, CurScope, UpdLoc, CounterVar,
                             IS.CounterInit, IS.NumIterations, IS.CounterStep,
                             IS.Subtract, IS.IsNonRectangularLB, &Captures);
      if (!Final.isUsable()) {
        HasErrors = true;
        break;
      }

      if (!Update.isUsable() || !Final.isUsable()) {
        HasErrors = true;
        break;
      }
      // Save results
      Built.Counters[Cnt] = IS.CounterVar;
      Built.PrivateCounters[Cnt] = IS.PrivateCounterVar;
      Built.Inits[Cnt] = Init.get();
      Built.Updates[Cnt] = Update.get();
      Built.Finals[Cnt] = Final.get();
      Built.DependentCounters[Cnt] = nullptr;
      Built.DependentInits[Cnt] = nullptr;
      Built.FinalsConditions[Cnt] = nullptr;
      if (IS.IsNonRectangularLB || IS.IsNonRectangularUB) {
        Built.DependentCounters[Cnt] = Built.Counters[IS.LoopDependentIdx - 1];
        Built.DependentInits[Cnt] = Built.Inits[IS.LoopDependentIdx - 1];
        Built.FinalsConditions[Cnt] = IS.FinalCondition;
      }
    }
  }

  if (HasErrors)
    return 0;

  // Save results
  Built.IterationVarRef = IV.get();
  Built.LastIteration = LastIteration.get();
  Built.NumIterations = NumIterations.get();
  Built.CalcLastIteration = SemaRef
                                .ActOnFinishFullExpr(CalcLastIteration.get(),
                                                     /*DiscardedValue=*/false)
                                .get();
  Built.PreCond = PreCond.get();
  Built.PreInits = buildPreInits(C, Captures);
  Built.Cond = Cond.get();
  Built.Init = Init.get();
  Built.Inc = Inc.get();
  Built.LB = LB.get();
  Built.UB = UB.get();
  Built.IL = IL.get();
  Built.ST = ST.get();
  Built.EUB = EUB.get();
  Built.NLB = NextLB.get();
  Built.NUB = NextUB.get();
  Built.PrevLB = PrevLB.get();
  Built.PrevUB = PrevUB.get();
  Built.DistInc = DistInc.get();
  Built.PrevEUB = PrevEUB.get();
  Built.DistCombinedFields.LB = CombLB.get();
  Built.DistCombinedFields.UB = CombUB.get();
  Built.DistCombinedFields.EUB = CombEUB.get();
  Built.DistCombinedFields.Init = CombInit.get();
  Built.DistCombinedFields.Cond = CombCond.get();
  Built.DistCombinedFields.NLB = CombNextLB.get();
  Built.DistCombinedFields.NUB = CombNextUB.get();
  Built.DistCombinedFields.DistCond = CombDistCond.get();
  Built.DistCombinedFields.ParForInDistCond = ParForInDistCond.get();

  return NestedLoopCount;
}

static Expr *getCollapseNumberExpr(ArrayRef<OMPClause *> Clauses) {
  auto CollapseClauses =
      OMPExecutableDirective::getClausesOfKind<OMPCollapseClause>(Clauses);
  if (CollapseClauses.begin() != CollapseClauses.end())
    return (*CollapseClauses.begin())->getNumForLoops();
  return nullptr;
}

static Expr *getOrderedNumberExpr(ArrayRef<OMPClause *> Clauses) {
  auto OrderedClauses =
      OMPExecutableDirective::getClausesOfKind<OMPOrderedClause>(Clauses);
  if (OrderedClauses.begin() != OrderedClauses.end())
    return (*OrderedClauses.begin())->getNumForLoops();
  return nullptr;
}

static bool checkSimdlenSafelenSpecified(Sema &S,
                                         const ArrayRef<OMPClause *> Clauses) {
  const OMPSafelenClause *Safelen = nullptr;
  const OMPSimdlenClause *Simdlen = nullptr;

  for (const OMPClause *Clause : Clauses) {
    if (Clause->getClauseKind() == OMPC_safelen)
      Safelen = cast<OMPSafelenClause>(Clause);
    else if (Clause->getClauseKind() == OMPC_simdlen)
      Simdlen = cast<OMPSimdlenClause>(Clause);
    if (Safelen && Simdlen)
      break;
  }

  if (Simdlen && Safelen) {
    const Expr *SimdlenLength = Simdlen->getSimdlen();
    const Expr *SafelenLength = Safelen->getSafelen();
    if (SimdlenLength->isValueDependent() || SimdlenLength->isTypeDependent() ||
        SimdlenLength->isInstantiationDependent() ||
        SimdlenLength->containsUnexpandedParameterPack())
      return false;
    if (SafelenLength->isValueDependent() || SafelenLength->isTypeDependent() ||
        SafelenLength->isInstantiationDependent() ||
        SafelenLength->containsUnexpandedParameterPack())
      return false;
    Expr::EvalResult SimdlenResult, SafelenResult;
    SimdlenLength->EvaluateAsInt(SimdlenResult, S.Context);
    SafelenLength->EvaluateAsInt(SafelenResult, S.Context);
    llvm::APSInt SimdlenRes = SimdlenResult.Val.getInt();
    llvm::APSInt SafelenRes = SafelenResult.Val.getInt();
    // OpenMP 4.5 [2.8.1, simd Construct, Restrictions]
    // If both simdlen and safelen clauses are specified, the value of the
    // simdlen parameter must be less than or equal to the value of the safelen
    // parameter.
    if (SimdlenRes > SafelenRes) {
      S.Diag(SimdlenLength->getExprLoc(),
             diag::err_omp_wrong_simdlen_safelen_values)
          << SimdlenLength->getSourceRange() << SafelenLength->getSourceRange();
      return true;
    }
  }
  return false;
}

static bool checkGenericLoopLastprivate(Sema &S, ArrayRef<OMPClause *> Clauses,
                                        OpenMPDirectiveKind K,
                                        DSAStackTy *Stack);

bool SemaOpenMP::checkLastPrivateForMappedDirectives(
    ArrayRef<OMPClause *> Clauses) {

  // Check for syntax of lastprivate
  // Param of the lastprivate have different meanings in the mapped directives
  // e.g. "omp loop" Only loop iteration vars are allowed in lastprivate clause
  //      "omp for"  lastprivate vars must be shared
  if (getLangOpts().OpenMP >= 50 &&
      DSAStack->getMappedDirective() == OMPD_loop &&
      checkGenericLoopLastprivate(SemaRef, Clauses, OMPD_loop, DSAStack)) {
    return false;
  }
  return true;
}

StmtResult SemaOpenMP::ActOnOpenMPSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  if (!checkLastPrivateForMappedDirectives(Clauses))
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_simd, getCollapseNumberExpr(Clauses), getOrderedNumberExpr(Clauses),
      AStmt, SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  auto *SimdDirective = OMPSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getMappedDirective());
  return SimdDirective;
}

StmtResult SemaOpenMP::ActOnOpenMPForDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  if (!checkLastPrivateForMappedDirectives(Clauses))
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_for, getCollapseNumberExpr(Clauses), getOrderedNumberExpr(Clauses),
      AStmt, SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  auto *ForDirective = OMPForDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion(),
      DSAStack->getMappedDirective());
  return ForDirective;
}

StmtResult SemaOpenMP::ActOnOpenMPForSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_for_simd, getCollapseNumberExpr(Clauses),
                      getOrderedNumberExpr(Clauses), AStmt, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPForSimdDirective::Create(getASTContext(), StartLoc, EndLoc,
                                     NestedLoopCount, Clauses, AStmt, B);
}

static bool checkSectionsDirective(Sema &SemaRef, OpenMPDirectiveKind DKind,
                                   Stmt *AStmt, DSAStackTy *Stack) {
  if (!AStmt)
    return true;

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  auto BaseStmt = AStmt;
  while (auto *CS = dyn_cast_or_null<CapturedStmt>(BaseStmt))
    BaseStmt = CS->getCapturedStmt();
  if (auto *C = dyn_cast_or_null<CompoundStmt>(BaseStmt)) {
    auto S = C->children();
    if (S.begin() == S.end())
      return true;
    // All associated statements must be '#pragma omp section' except for
    // the first one.
    for (Stmt *SectionStmt : llvm::drop_begin(S)) {
      if (!SectionStmt || !isa<OMPSectionDirective>(SectionStmt)) {
        if (SectionStmt)
          SemaRef.Diag(SectionStmt->getBeginLoc(),
                       diag::err_omp_sections_substmt_not_section)
              << getOpenMPDirectiveName(DKind);
        return true;
      }
      cast<OMPSectionDirective>(SectionStmt)
          ->setHasCancel(Stack->isCancelRegion());
    }
  } else {
    SemaRef.Diag(AStmt->getBeginLoc(), diag::err_omp_sections_not_compound_stmt)
        << getOpenMPDirectiveName(DKind);
    return true;
  }
  return false;
}

StmtResult
SemaOpenMP::ActOnOpenMPSectionsDirective(ArrayRef<OMPClause *> Clauses,
                                         Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc) {
  if (checkSectionsDirective(SemaRef, OMPD_sections, AStmt, DSAStack))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPSectionsDirective::Create(
      getASTContext(), StartLoc, EndLoc, Clauses, AStmt,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPSectionDirective(Stmt *AStmt,
                                                   SourceLocation StartLoc,
                                                   SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  DSAStack->setParentCancelRegion(DSAStack->isCancelRegion());

  return OMPSectionDirective::Create(getASTContext(), StartLoc, EndLoc, AStmt,
                                     DSAStack->isCancelRegion());
}

static Expr *getDirectCallExpr(Expr *E) {
  E = E->IgnoreParenCasts()->IgnoreImplicit();
  if (auto *CE = dyn_cast<CallExpr>(E))
    if (CE->getDirectCallee())
      return E;
  return nullptr;
}

StmtResult
SemaOpenMP::ActOnOpenMPDispatchDirective(ArrayRef<OMPClause *> Clauses,
                                         Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  Stmt *S = cast<CapturedStmt>(AStmt)->getCapturedStmt();

  // 5.1 OpenMP
  // expression-stmt : an expression statement with one of the following forms:
  //   expression = target-call ( [expression-list] );
  //   target-call ( [expression-list] );

  SourceLocation TargetCallLoc;

  if (!SemaRef.CurContext->isDependentContext()) {
    Expr *TargetCall = nullptr;

    auto *E = dyn_cast<Expr>(S);
    if (!E) {
      Diag(S->getBeginLoc(), diag::err_omp_dispatch_statement_call);
      return StmtError();
    }

    E = E->IgnoreParenCasts()->IgnoreImplicit();

    if (auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Assign)
        TargetCall = getDirectCallExpr(BO->getRHS());
    } else {
      if (auto *COCE = dyn_cast<CXXOperatorCallExpr>(E))
        if (COCE->getOperator() == OO_Equal)
          TargetCall = getDirectCallExpr(COCE->getArg(1));
      if (!TargetCall)
        TargetCall = getDirectCallExpr(E);
    }
    if (!TargetCall) {
      Diag(E->getBeginLoc(), diag::err_omp_dispatch_statement_call);
      return StmtError();
    }
    TargetCallLoc = TargetCall->getExprLoc();
  }

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPDispatchDirective::Create(getASTContext(), StartLoc, EndLoc,
                                      Clauses, AStmt, TargetCallLoc);
}

static bool checkGenericLoopLastprivate(Sema &S, ArrayRef<OMPClause *> Clauses,
                                        OpenMPDirectiveKind K,
                                        DSAStackTy *Stack) {
  bool ErrorFound = false;
  for (OMPClause *C : Clauses) {
    if (auto *LPC = dyn_cast<OMPLastprivateClause>(C)) {
      for (Expr *RefExpr : LPC->varlists()) {
        SourceLocation ELoc;
        SourceRange ERange;
        Expr *SimpleRefExpr = RefExpr;
        auto Res = getPrivateItem(S, SimpleRefExpr, ELoc, ERange);
        if (ValueDecl *D = Res.first) {
          auto &&Info = Stack->isLoopControlVariable(D);
          if (!Info.first) {
            S.Diag(ELoc, diag::err_omp_lastprivate_loop_var_non_loop_iteration)
                << getOpenMPDirectiveName(K);
            ErrorFound = true;
          }
        }
      }
    }
  }
  return ErrorFound;
}

StmtResult SemaOpenMP::ActOnOpenMPGenericLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  // OpenMP 5.1 [2.11.7, loop construct, Restrictions]
  // A list item may not appear in a lastprivate clause unless it is the
  // loop iteration variable of a loop that is associated with the construct.
  if (checkGenericLoopLastprivate(SemaRef, Clauses, OMPD_loop, DSAStack))
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_loop, AStmt);

  OMPLoopDirective::HelperExprs B;
  // In presence of clause 'collapse', it will define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_loop, getCollapseNumberExpr(Clauses), getOrderedNumberExpr(Clauses),
      AStmt, SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp loop exprs were not built");

  return OMPGenericLoopDirective::Create(getASTContext(), StartLoc, EndLoc,
                                         NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTeamsGenericLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  // OpenMP 5.1 [2.11.7, loop construct, Restrictions]
  // A list item may not appear in a lastprivate clause unless it is the
  // loop iteration variable of a loop that is associated with the construct.
  if (checkGenericLoopLastprivate(SemaRef, Clauses, OMPD_teams_loop, DSAStack))
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(SemaRef, OMPD_teams_loop, AStmt);

  OMPLoopDirective::HelperExprs B;
  // In presence of clause 'collapse', it will define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_teams_loop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp loop exprs were not built");

  DSAStack->setParentTeamsRegionLoc(StartLoc);

  return OMPTeamsGenericLoopDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetTeamsGenericLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  // OpenMP 5.1 [2.11.7, loop construct, Restrictions]
  // A list item may not appear in a lastprivate clause unless it is the
  // loop iteration variable of a loop that is associated with the construct.
  if (checkGenericLoopLastprivate(SemaRef, Clauses, OMPD_target_teams_loop,
                                  DSAStack))
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_target_teams_loop, AStmt);

  OMPLoopDirective::HelperExprs B;
  // In presence of clause 'collapse', it will define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_target_teams_loop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp loop exprs were not built");

  return OMPTargetTeamsGenericLoopDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      teamsLoopCanBeParallelFor(AStmt, SemaRef));
}

StmtResult SemaOpenMP::ActOnOpenMPParallelGenericLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  // OpenMP 5.1 [2.11.7, loop construct, Restrictions]
  // A list item may not appear in a lastprivate clause unless it is the
  // loop iteration variable of a loop that is associated with the construct.
  if (checkGenericLoopLastprivate(SemaRef, Clauses, OMPD_parallel_loop,
                                  DSAStack))
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_parallel_loop, AStmt);

  OMPLoopDirective::HelperExprs B;
  // In presence of clause 'collapse', it will define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_parallel_loop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp loop exprs were not built");

  return OMPParallelGenericLoopDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetParallelGenericLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  // OpenMP 5.1 [2.11.7, loop construct, Restrictions]
  // A list item may not appear in a lastprivate clause unless it is the
  // loop iteration variable of a loop that is associated with the construct.
  if (checkGenericLoopLastprivate(SemaRef, Clauses, OMPD_target_parallel_loop,
                                  DSAStack))
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_target_parallel_loop, AStmt);

  OMPLoopDirective::HelperExprs B;
  // In presence of clause 'collapse', it will define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_target_parallel_loop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp loop exprs were not built");

  return OMPTargetParallelGenericLoopDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPSingleDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");

  SemaRef.setFunctionHasBranchProtectedScope();

  // OpenMP [2.7.3, single Construct, Restrictions]
  // The copyprivate clause must not be used with the nowait clause.
  const OMPClause *Nowait = nullptr;
  const OMPClause *Copyprivate = nullptr;
  for (const OMPClause *Clause : Clauses) {
    if (Clause->getClauseKind() == OMPC_nowait)
      Nowait = Clause;
    else if (Clause->getClauseKind() == OMPC_copyprivate)
      Copyprivate = Clause;
    if (Copyprivate && Nowait) {
      Diag(Copyprivate->getBeginLoc(),
           diag::err_omp_single_copyprivate_with_nowait);
      Diag(Nowait->getBeginLoc(), diag::note_omp_nowait_clause_here);
      return StmtError();
    }
  }

  return OMPSingleDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                    AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPMasterDirective(Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPMasterDirective::Create(getASTContext(), StartLoc, EndLoc, AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPMaskedDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPMaskedDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                    AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPCriticalDirective(
    const DeclarationNameInfo &DirName, ArrayRef<OMPClause *> Clauses,
    Stmt *AStmt, SourceLocation StartLoc, SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  bool ErrorFound = false;
  llvm::APSInt Hint;
  SourceLocation HintLoc;
  bool DependentHint = false;
  for (const OMPClause *C : Clauses) {
    if (C->getClauseKind() == OMPC_hint) {
      if (!DirName.getName()) {
        Diag(C->getBeginLoc(), diag::err_omp_hint_clause_no_name);
        ErrorFound = true;
      }
      Expr *E = cast<OMPHintClause>(C)->getHint();
      if (E->isTypeDependent() || E->isValueDependent() ||
          E->isInstantiationDependent()) {
        DependentHint = true;
      } else {
        Hint = E->EvaluateKnownConstInt(getASTContext());
        HintLoc = C->getBeginLoc();
      }
    }
  }
  if (ErrorFound)
    return StmtError();
  const auto Pair = DSAStack->getCriticalWithHint(DirName);
  if (Pair.first && DirName.getName() && !DependentHint) {
    if (llvm::APSInt::compareValues(Hint, Pair.second) != 0) {
      Diag(StartLoc, diag::err_omp_critical_with_hint);
      if (HintLoc.isValid())
        Diag(HintLoc, diag::note_omp_critical_hint_here)
            << 0 << toString(Hint, /*Radix=*/10, /*Signed=*/false);
      else
        Diag(StartLoc, diag::note_omp_critical_no_hint) << 0;
      if (const auto *C = Pair.first->getSingleClause<OMPHintClause>()) {
        Diag(C->getBeginLoc(), diag::note_omp_critical_hint_here)
            << 1
            << toString(C->getHint()->EvaluateKnownConstInt(getASTContext()),
                        /*Radix=*/10, /*Signed=*/false);
      } else {
        Diag(Pair.first->getBeginLoc(), diag::note_omp_critical_no_hint) << 1;
      }
    }
  }

  SemaRef.setFunctionHasBranchProtectedScope();

  auto *Dir = OMPCriticalDirective::Create(getASTContext(), DirName, StartLoc,
                                           EndLoc, Clauses, AStmt);
  if (!Pair.first && DirName.getName() && !DependentHint)
    DSAStack->addCriticalWithHint(Dir, Hint);
  return Dir;
}

StmtResult SemaOpenMP::ActOnOpenMPParallelForDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_parallel_for, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_parallel_for, getCollapseNumberExpr(Clauses),
                      getOrderedNumberExpr(Clauses), AStmt, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  return OMPParallelForDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPParallelForSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_parallel_for_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_parallel_for_simd, getCollapseNumberExpr(Clauses),
                      getOrderedNumberExpr(Clauses), AStmt, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPParallelForSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPParallelMasterDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_parallel_master, AStmt);

  return OMPParallelMasterDirective::Create(
      getASTContext(), StartLoc, EndLoc, Clauses, AStmt,
      DSAStack->getTaskgroupReductionRef());
}

StmtResult SemaOpenMP::ActOnOpenMPParallelMaskedDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_parallel_masked, AStmt);

  return OMPParallelMaskedDirective::Create(
      getASTContext(), StartLoc, EndLoc, Clauses, AStmt,
      DSAStack->getTaskgroupReductionRef());
}

StmtResult SemaOpenMP::ActOnOpenMPParallelSectionsDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc) {
  if (checkSectionsDirective(SemaRef, OMPD_parallel_sections, AStmt, DSAStack))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPParallelSectionsDirective::Create(
      getASTContext(), StartLoc, EndLoc, Clauses, AStmt,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

/// Find and diagnose mutually exclusive clause kinds.
static bool checkMutuallyExclusiveClauses(
    Sema &S, ArrayRef<OMPClause *> Clauses,
    ArrayRef<OpenMPClauseKind> MutuallyExclusiveClauses) {
  const OMPClause *PrevClause = nullptr;
  bool ErrorFound = false;
  for (const OMPClause *C : Clauses) {
    if (llvm::is_contained(MutuallyExclusiveClauses, C->getClauseKind())) {
      if (!PrevClause) {
        PrevClause = C;
      } else if (PrevClause->getClauseKind() != C->getClauseKind()) {
        S.Diag(C->getBeginLoc(), diag::err_omp_clauses_mutually_exclusive)
            << getOpenMPClauseName(C->getClauseKind())
            << getOpenMPClauseName(PrevClause->getClauseKind());
        S.Diag(PrevClause->getBeginLoc(), diag::note_omp_previous_clause)
            << getOpenMPClauseName(PrevClause->getClauseKind());
        ErrorFound = true;
      }
    }
  }
  return ErrorFound;
}

StmtResult SemaOpenMP::ActOnOpenMPTaskDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  // OpenMP 5.0, 2.10.1 task Construct
  // If a detach clause appears on the directive, then a mergeable clause cannot
  // appear on the same directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_detach, OMPC_mergeable}))
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_task, AStmt);

  return OMPTaskDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                  AStmt, DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPTaskyieldDirective(SourceLocation StartLoc,
                                                     SourceLocation EndLoc) {
  return OMPTaskyieldDirective::Create(getASTContext(), StartLoc, EndLoc);
}

StmtResult SemaOpenMP::ActOnOpenMPBarrierDirective(SourceLocation StartLoc,
                                                   SourceLocation EndLoc) {
  return OMPBarrierDirective::Create(getASTContext(), StartLoc, EndLoc);
}

StmtResult SemaOpenMP::ActOnOpenMPErrorDirective(ArrayRef<OMPClause *> Clauses,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc,
                                                 bool InExContext) {
  const OMPAtClause *AtC =
      OMPExecutableDirective::getSingleClause<OMPAtClause>(Clauses);

  if (AtC && !InExContext && AtC->getAtKind() == OMPC_AT_execution) {
    Diag(AtC->getAtKindKwLoc(), diag::err_omp_unexpected_execution_modifier);
    return StmtError();
  }

  const OMPSeverityClause *SeverityC =
      OMPExecutableDirective::getSingleClause<OMPSeverityClause>(Clauses);
  const OMPMessageClause *MessageC =
      OMPExecutableDirective::getSingleClause<OMPMessageClause>(Clauses);
  Expr *ME = MessageC ? MessageC->getMessageString() : nullptr;

  if (!AtC || AtC->getAtKind() == OMPC_AT_compilation) {
    if (SeverityC && SeverityC->getSeverityKind() == OMPC_SEVERITY_warning)
      Diag(SeverityC->getSeverityKindKwLoc(), diag::warn_diagnose_if_succeeded)
          << (ME ? cast<StringLiteral>(ME)->getString() : "WARNING");
    else
      Diag(StartLoc, diag::err_diagnose_if_succeeded)
          << (ME ? cast<StringLiteral>(ME)->getString() : "ERROR");
    if (!SeverityC || SeverityC->getSeverityKind() != OMPC_SEVERITY_warning)
      return StmtError();
  }
  return OMPErrorDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses);
}

StmtResult
SemaOpenMP::ActOnOpenMPTaskwaitDirective(ArrayRef<OMPClause *> Clauses,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc) {
  const OMPNowaitClause *NowaitC =
      OMPExecutableDirective::getSingleClause<OMPNowaitClause>(Clauses);
  bool HasDependC =
      !OMPExecutableDirective::getClausesOfKind<OMPDependClause>(Clauses)
           .empty();
  if (NowaitC && !HasDependC) {
    Diag(StartLoc, diag::err_omp_nowait_clause_without_depend);
    return StmtError();
  }

  return OMPTaskwaitDirective::Create(getASTContext(), StartLoc, EndLoc,
                                      Clauses);
}

StmtResult
SemaOpenMP::ActOnOpenMPTaskgroupDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPTaskgroupDirective::Create(getASTContext(), StartLoc, EndLoc,
                                       Clauses, AStmt,
                                       DSAStack->getTaskgroupReductionRef());
}

StmtResult SemaOpenMP::ActOnOpenMPFlushDirective(ArrayRef<OMPClause *> Clauses,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc) {
  OMPFlushClause *FC = nullptr;
  OMPClause *OrderClause = nullptr;
  for (OMPClause *C : Clauses) {
    if (C->getClauseKind() == OMPC_flush)
      FC = cast<OMPFlushClause>(C);
    else
      OrderClause = C;
  }
  OpenMPClauseKind MemOrderKind = OMPC_unknown;
  SourceLocation MemOrderLoc;
  for (const OMPClause *C : Clauses) {
    if (C->getClauseKind() == OMPC_acq_rel ||
        C->getClauseKind() == OMPC_acquire ||
        C->getClauseKind() == OMPC_release) {
      if (MemOrderKind != OMPC_unknown) {
        Diag(C->getBeginLoc(), diag::err_omp_several_mem_order_clauses)
            << getOpenMPDirectiveName(OMPD_flush) << 1
            << SourceRange(C->getBeginLoc(), C->getEndLoc());
        Diag(MemOrderLoc, diag::note_omp_previous_mem_order_clause)
            << getOpenMPClauseName(MemOrderKind);
      } else {
        MemOrderKind = C->getClauseKind();
        MemOrderLoc = C->getBeginLoc();
      }
    }
  }
  if (FC && OrderClause) {
    Diag(FC->getLParenLoc(), diag::err_omp_flush_order_clause_and_list)
        << getOpenMPClauseName(OrderClause->getClauseKind());
    Diag(OrderClause->getBeginLoc(), diag::note_omp_flush_order_clause_here)
        << getOpenMPClauseName(OrderClause->getClauseKind());
    return StmtError();
  }
  return OMPFlushDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses);
}

StmtResult SemaOpenMP::ActOnOpenMPDepobjDirective(ArrayRef<OMPClause *> Clauses,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  if (Clauses.empty()) {
    Diag(StartLoc, diag::err_omp_depobj_expected);
    return StmtError();
  } else if (Clauses[0]->getClauseKind() != OMPC_depobj) {
    Diag(Clauses[0]->getBeginLoc(), diag::err_omp_depobj_expected);
    return StmtError();
  }
  // Only depobj expression and another single clause is allowed.
  if (Clauses.size() > 2) {
    Diag(Clauses[2]->getBeginLoc(),
         diag::err_omp_depobj_single_clause_expected);
    return StmtError();
  } else if (Clauses.size() < 1) {
    Diag(Clauses[0]->getEndLoc(), diag::err_omp_depobj_single_clause_expected);
    return StmtError();
  }
  return OMPDepobjDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses);
}

StmtResult SemaOpenMP::ActOnOpenMPScanDirective(ArrayRef<OMPClause *> Clauses,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  // Check that exactly one clause is specified.
  if (Clauses.size() != 1) {
    Diag(Clauses.empty() ? EndLoc : Clauses[1]->getBeginLoc(),
         diag::err_omp_scan_single_clause_expected);
    return StmtError();
  }
  // Check that scan directive is used in the scope of the OpenMP loop body.
  if (Scope *S = DSAStack->getCurScope()) {
    Scope *ParentS = S->getParent();
    if (!ParentS || ParentS->getParent() != ParentS->getBreakParent() ||
        !ParentS->getBreakParent()->isOpenMPLoopScope())
      return StmtError(Diag(StartLoc, diag::err_omp_orphaned_device_directive)
                       << getOpenMPDirectiveName(OMPD_scan) << 5);
  }
  // Check that only one instance of scan directives is used in the same outer
  // region.
  if (DSAStack->doesParentHasScanDirective()) {
    Diag(StartLoc, diag::err_omp_several_directives_in_region) << "scan";
    Diag(DSAStack->getParentScanDirectiveLoc(),
         diag::note_omp_previous_directive)
        << "scan";
    return StmtError();
  }
  DSAStack->setParentHasScanDirective(StartLoc);
  return OMPScanDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses);
}

StmtResult
SemaOpenMP::ActOnOpenMPOrderedDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc) {
  const OMPClause *DependFound = nullptr;
  const OMPClause *DependSourceClause = nullptr;
  const OMPClause *DependSinkClause = nullptr;
  const OMPClause *DoacrossFound = nullptr;
  const OMPClause *DoacrossSourceClause = nullptr;
  const OMPClause *DoacrossSinkClause = nullptr;
  bool ErrorFound = false;
  const OMPThreadsClause *TC = nullptr;
  const OMPSIMDClause *SC = nullptr;
  for (const OMPClause *C : Clauses) {
    auto DOC = dyn_cast<OMPDoacrossClause>(C);
    auto DC = dyn_cast<OMPDependClause>(C);
    if (DC || DOC) {
      DependFound = DC ? C : nullptr;
      DoacrossFound = DOC ? C : nullptr;
      OMPDoacrossKind ODK;
      if ((DC && DC->getDependencyKind() == OMPC_DEPEND_source) ||
          (DOC && (ODK.isSource(DOC)))) {
        if ((DC && DependSourceClause) || (DOC && DoacrossSourceClause)) {
          Diag(C->getBeginLoc(), diag::err_omp_more_one_clause)
              << getOpenMPDirectiveName(OMPD_ordered)
              << getOpenMPClauseName(DC ? OMPC_depend : OMPC_doacross) << 2;
          ErrorFound = true;
        } else {
          if (DC)
            DependSourceClause = C;
          else
            DoacrossSourceClause = C;
        }
        if ((DC && DependSinkClause) || (DOC && DoacrossSinkClause)) {
          Diag(C->getBeginLoc(), diag::err_omp_sink_and_source_not_allowed)
              << (DC ? "depend" : "doacross") << 0;
          ErrorFound = true;
        }
      } else if ((DC && DC->getDependencyKind() == OMPC_DEPEND_sink) ||
                 (DOC && (ODK.isSink(DOC) || ODK.isSinkIter(DOC)))) {
        if (DependSourceClause || DoacrossSourceClause) {
          Diag(C->getBeginLoc(), diag::err_omp_sink_and_source_not_allowed)
              << (DC ? "depend" : "doacross") << 1;
          ErrorFound = true;
        }
        if (DC)
          DependSinkClause = C;
        else
          DoacrossSinkClause = C;
      }
    } else if (C->getClauseKind() == OMPC_threads) {
      TC = cast<OMPThreadsClause>(C);
    } else if (C->getClauseKind() == OMPC_simd) {
      SC = cast<OMPSIMDClause>(C);
    }
  }
  if (!ErrorFound && !SC &&
      isOpenMPSimdDirective(DSAStack->getParentDirective())) {
    // OpenMP [2.8.1,simd Construct, Restrictions]
    // An ordered construct with the simd clause is the only OpenMP construct
    // that can appear in the simd region.
    Diag(StartLoc, diag::err_omp_prohibited_region_simd)
        << (getLangOpts().OpenMP >= 50 ? 1 : 0);
    ErrorFound = true;
  } else if ((DependFound || DoacrossFound) && (TC || SC)) {
    SourceLocation Loc =
        DependFound ? DependFound->getBeginLoc() : DoacrossFound->getBeginLoc();
    Diag(Loc, diag::err_omp_depend_clause_thread_simd)
        << getOpenMPClauseName(DependFound ? OMPC_depend : OMPC_doacross)
        << getOpenMPClauseName(TC ? TC->getClauseKind() : SC->getClauseKind());
    ErrorFound = true;
  } else if ((DependFound || DoacrossFound) &&
             !DSAStack->getParentOrderedRegionParam().first) {
    SourceLocation Loc =
        DependFound ? DependFound->getBeginLoc() : DoacrossFound->getBeginLoc();
    Diag(Loc, diag::err_omp_ordered_directive_without_param)
        << getOpenMPClauseName(DependFound ? OMPC_depend : OMPC_doacross);
    ErrorFound = true;
  } else if (TC || Clauses.empty()) {
    if (const Expr *Param = DSAStack->getParentOrderedRegionParam().first) {
      SourceLocation ErrLoc = TC ? TC->getBeginLoc() : StartLoc;
      Diag(ErrLoc, diag::err_omp_ordered_directive_with_param)
          << (TC != nullptr);
      Diag(Param->getBeginLoc(), diag::note_omp_ordered_param) << 1;
      ErrorFound = true;
    }
  }
  if ((!AStmt && !DependFound && !DoacrossFound) || ErrorFound)
    return StmtError();

  // OpenMP 5.0, 2.17.9, ordered Construct, Restrictions.
  // During execution of an iteration of a worksharing-loop or a loop nest
  // within a worksharing-loop, simd, or worksharing-loop SIMD region, a thread
  // must not execute more than one ordered region corresponding to an ordered
  // construct without a depend clause.
  if (!DependFound && !DoacrossFound) {
    if (DSAStack->doesParentHasOrderedDirective()) {
      Diag(StartLoc, diag::err_omp_several_directives_in_region) << "ordered";
      Diag(DSAStack->getParentOrderedDirectiveLoc(),
           diag::note_omp_previous_directive)
          << "ordered";
      return StmtError();
    }
    DSAStack->setParentHasOrderedDirective(StartLoc);
  }

  if (AStmt) {
    assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");

    SemaRef.setFunctionHasBranchProtectedScope();
  }

  return OMPOrderedDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                     AStmt);
}

namespace {
/// Helper class for checking expression in 'omp atomic [update]'
/// construct.
class OpenMPAtomicUpdateChecker {
  /// Error results for atomic update expressions.
  enum ExprAnalysisErrorCode {
    /// A statement is not an expression statement.
    NotAnExpression,
    /// Expression is not builtin binary or unary operation.
    NotABinaryOrUnaryExpression,
    /// Unary operation is not post-/pre- increment/decrement operation.
    NotAnUnaryIncDecExpression,
    /// An expression is not of scalar type.
    NotAScalarType,
    /// A binary operation is not an assignment operation.
    NotAnAssignmentOp,
    /// RHS part of the binary operation is not a binary expression.
    NotABinaryExpression,
    /// RHS part is not additive/multiplicative/shift/bitwise binary
    /// expression.
    NotABinaryOperator,
    /// RHS binary operation does not have reference to the updated LHS
    /// part.
    NotAnUpdateExpression,
    /// An expression contains semantical error not related to
    /// 'omp atomic [update]'
    NotAValidExpression,
    /// No errors is found.
    NoError
  };
  /// Reference to Sema.
  Sema &SemaRef;
  /// A location for note diagnostics (when error is found).
  SourceLocation NoteLoc;
  /// 'x' lvalue part of the source atomic expression.
  Expr *X;
  /// 'expr' rvalue part of the source atomic expression.
  Expr *E;
  /// Helper expression of the form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  Expr *UpdateExpr;
  /// Is 'x' a LHS in a RHS part of full update expression. It is
  /// important for non-associative operations.
  bool IsXLHSInRHSPart;
  BinaryOperatorKind Op;
  SourceLocation OpLoc;
  /// true if the source expression is a postfix unary operation, false
  /// if it is a prefix unary operation.
  bool IsPostfixUpdate;

public:
  OpenMPAtomicUpdateChecker(Sema &SemaRef)
      : SemaRef(SemaRef), X(nullptr), E(nullptr), UpdateExpr(nullptr),
        IsXLHSInRHSPart(false), Op(BO_PtrMemD), IsPostfixUpdate(false) {}
  /// Check specified statement that it is suitable for 'atomic update'
  /// constructs and extract 'x', 'expr' and Operation from the original
  /// expression. If DiagId and NoteId == 0, then only check is performed
  /// without error notification.
  /// \param DiagId Diagnostic which should be emitted if error is found.
  /// \param NoteId Diagnostic note for the main error message.
  /// \return true if statement is not an update expression, false otherwise.
  bool checkStatement(Stmt *S, unsigned DiagId = 0, unsigned NoteId = 0);
  /// Return the 'x' lvalue part of the source atomic expression.
  Expr *getX() const { return X; }
  /// Return the 'expr' rvalue part of the source atomic expression.
  Expr *getExpr() const { return E; }
  /// Return the update expression used in calculation of the updated
  /// value. Always has form 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  Expr *getUpdateExpr() const { return UpdateExpr; }
  /// Return true if 'x' is LHS in RHS part of full update expression,
  /// false otherwise.
  bool isXLHSInRHSPart() const { return IsXLHSInRHSPart; }

  /// true if the source expression is a postfix unary operation, false
  /// if it is a prefix unary operation.
  bool isPostfixUpdate() const { return IsPostfixUpdate; }

private:
  bool checkBinaryOperation(BinaryOperator *AtomicBinOp, unsigned DiagId = 0,
                            unsigned NoteId = 0);
};

bool OpenMPAtomicUpdateChecker::checkBinaryOperation(
    BinaryOperator *AtomicBinOp, unsigned DiagId, unsigned NoteId) {
  ExprAnalysisErrorCode ErrorFound = NoError;
  SourceLocation ErrorLoc, NoteLoc;
  SourceRange ErrorRange, NoteRange;
  // Allowed constructs are:
  //  x = x binop expr;
  //  x = expr binop x;
  if (AtomicBinOp->getOpcode() == BO_Assign) {
    X = AtomicBinOp->getLHS();
    if (const auto *AtomicInnerBinOp = dyn_cast<BinaryOperator>(
            AtomicBinOp->getRHS()->IgnoreParenImpCasts())) {
      if (AtomicInnerBinOp->isMultiplicativeOp() ||
          AtomicInnerBinOp->isAdditiveOp() || AtomicInnerBinOp->isShiftOp() ||
          AtomicInnerBinOp->isBitwiseOp()) {
        Op = AtomicInnerBinOp->getOpcode();
        OpLoc = AtomicInnerBinOp->getOperatorLoc();
        Expr *LHS = AtomicInnerBinOp->getLHS();
        Expr *RHS = AtomicInnerBinOp->getRHS();
        llvm::FoldingSetNodeID XId, LHSId, RHSId;
        X->IgnoreParenImpCasts()->Profile(XId, SemaRef.getASTContext(),
                                          /*Canonical=*/true);
        LHS->IgnoreParenImpCasts()->Profile(LHSId, SemaRef.getASTContext(),
                                            /*Canonical=*/true);
        RHS->IgnoreParenImpCasts()->Profile(RHSId, SemaRef.getASTContext(),
                                            /*Canonical=*/true);
        if (XId == LHSId) {
          E = RHS;
          IsXLHSInRHSPart = true;
        } else if (XId == RHSId) {
          E = LHS;
          IsXLHSInRHSPart = false;
        } else {
          ErrorLoc = AtomicInnerBinOp->getExprLoc();
          ErrorRange = AtomicInnerBinOp->getSourceRange();
          NoteLoc = X->getExprLoc();
          NoteRange = X->getSourceRange();
          ErrorFound = NotAnUpdateExpression;
        }
      } else {
        ErrorLoc = AtomicInnerBinOp->getExprLoc();
        ErrorRange = AtomicInnerBinOp->getSourceRange();
        NoteLoc = AtomicInnerBinOp->getOperatorLoc();
        NoteRange = SourceRange(NoteLoc, NoteLoc);
        ErrorFound = NotABinaryOperator;
      }
    } else {
      NoteLoc = ErrorLoc = AtomicBinOp->getRHS()->getExprLoc();
      NoteRange = ErrorRange = AtomicBinOp->getRHS()->getSourceRange();
      ErrorFound = NotABinaryExpression;
    }
  } else {
    ErrorLoc = AtomicBinOp->getExprLoc();
    ErrorRange = AtomicBinOp->getSourceRange();
    NoteLoc = AtomicBinOp->getOperatorLoc();
    NoteRange = SourceRange(NoteLoc, NoteLoc);
    ErrorFound = NotAnAssignmentOp;
  }
  if (ErrorFound != NoError && DiagId != 0 && NoteId != 0) {
    SemaRef.Diag(ErrorLoc, DiagId) << ErrorRange;
    SemaRef.Diag(NoteLoc, NoteId) << ErrorFound << NoteRange;
    return true;
  }
  if (SemaRef.CurContext->isDependentContext())
    E = X = UpdateExpr = nullptr;
  return ErrorFound != NoError;
}

bool OpenMPAtomicUpdateChecker::checkStatement(Stmt *S, unsigned DiagId,
                                               unsigned NoteId) {
  ExprAnalysisErrorCode ErrorFound = NoError;
  SourceLocation ErrorLoc, NoteLoc;
  SourceRange ErrorRange, NoteRange;
  // Allowed constructs are:
  //  x++;
  //  x--;
  //  ++x;
  //  --x;
  //  x binop= expr;
  //  x = x binop expr;
  //  x = expr binop x;
  if (auto *AtomicBody = dyn_cast<Expr>(S)) {
    AtomicBody = AtomicBody->IgnoreParenImpCasts();
    if (AtomicBody->getType()->isScalarType() ||
        AtomicBody->isInstantiationDependent()) {
      if (const auto *AtomicCompAssignOp = dyn_cast<CompoundAssignOperator>(
              AtomicBody->IgnoreParenImpCasts())) {
        // Check for Compound Assignment Operation
        Op = BinaryOperator::getOpForCompoundAssignment(
            AtomicCompAssignOp->getOpcode());
        OpLoc = AtomicCompAssignOp->getOperatorLoc();
        E = AtomicCompAssignOp->getRHS();
        X = AtomicCompAssignOp->getLHS()->IgnoreParens();
        IsXLHSInRHSPart = true;
      } else if (auto *AtomicBinOp = dyn_cast<BinaryOperator>(
                     AtomicBody->IgnoreParenImpCasts())) {
        // Check for Binary Operation
        if (checkBinaryOperation(AtomicBinOp, DiagId, NoteId))
          return true;
      } else if (const auto *AtomicUnaryOp = dyn_cast<UnaryOperator>(
                     AtomicBody->IgnoreParenImpCasts())) {
        // Check for Unary Operation
        if (AtomicUnaryOp->isIncrementDecrementOp()) {
          IsPostfixUpdate = AtomicUnaryOp->isPostfix();
          Op = AtomicUnaryOp->isIncrementOp() ? BO_Add : BO_Sub;
          OpLoc = AtomicUnaryOp->getOperatorLoc();
          X = AtomicUnaryOp->getSubExpr()->IgnoreParens();
          E = SemaRef.ActOnIntegerConstant(OpLoc, /*uint64_t Val=*/1).get();
          IsXLHSInRHSPart = true;
        } else {
          ErrorFound = NotAnUnaryIncDecExpression;
          ErrorLoc = AtomicUnaryOp->getExprLoc();
          ErrorRange = AtomicUnaryOp->getSourceRange();
          NoteLoc = AtomicUnaryOp->getOperatorLoc();
          NoteRange = SourceRange(NoteLoc, NoteLoc);
        }
      } else if (!AtomicBody->isInstantiationDependent()) {
        ErrorFound = NotABinaryOrUnaryExpression;
        NoteLoc = ErrorLoc = AtomicBody->getExprLoc();
        NoteRange = ErrorRange = AtomicBody->getSourceRange();
      } else if (AtomicBody->containsErrors()) {
        ErrorFound = NotAValidExpression;
        NoteLoc = ErrorLoc = AtomicBody->getExprLoc();
        NoteRange = ErrorRange = AtomicBody->getSourceRange();
      }
    } else {
      ErrorFound = NotAScalarType;
      NoteLoc = ErrorLoc = AtomicBody->getBeginLoc();
      NoteRange = ErrorRange = SourceRange(NoteLoc, NoteLoc);
    }
  } else {
    ErrorFound = NotAnExpression;
    NoteLoc = ErrorLoc = S->getBeginLoc();
    NoteRange = ErrorRange = SourceRange(NoteLoc, NoteLoc);
  }
  if (ErrorFound != NoError && DiagId != 0 && NoteId != 0) {
    SemaRef.Diag(ErrorLoc, DiagId) << ErrorRange;
    SemaRef.Diag(NoteLoc, NoteId) << ErrorFound << NoteRange;
    return true;
  }
  if (SemaRef.CurContext->isDependentContext())
    E = X = UpdateExpr = nullptr;
  if (ErrorFound == NoError && E && X) {
    // Build an update expression of form 'OpaqueValueExpr(x) binop
    // OpaqueValueExpr(expr)' or 'OpaqueValueExpr(expr) binop
    // OpaqueValueExpr(x)' and then cast it to the type of the 'x' expression.
    auto *OVEX = new (SemaRef.getASTContext())
        OpaqueValueExpr(X->getExprLoc(), X->getType(), VK_PRValue);
    auto *OVEExpr = new (SemaRef.getASTContext())
        OpaqueValueExpr(E->getExprLoc(), E->getType(), VK_PRValue);
    ExprResult Update =
        SemaRef.CreateBuiltinBinOp(OpLoc, Op, IsXLHSInRHSPart ? OVEX : OVEExpr,
                                   IsXLHSInRHSPart ? OVEExpr : OVEX);
    if (Update.isInvalid())
      return true;
    Update = SemaRef.PerformImplicitConversion(Update.get(), X->getType(),
                                               Sema::AA_Casting);
    if (Update.isInvalid())
      return true;
    UpdateExpr = Update.get();
  }
  return ErrorFound != NoError;
}

/// Get the node id of the fixed point of an expression \a S.
llvm::FoldingSetNodeID getNodeId(ASTContext &Context, const Expr *S) {
  llvm::FoldingSetNodeID Id;
  S->IgnoreParenImpCasts()->Profile(Id, Context, true);
  return Id;
}

/// Check if two expressions are same.
bool checkIfTwoExprsAreSame(ASTContext &Context, const Expr *LHS,
                            const Expr *RHS) {
  return getNodeId(Context, LHS) == getNodeId(Context, RHS);
}

class OpenMPAtomicCompareChecker {
public:
  /// All kinds of errors that can occur in `atomic compare`
  enum ErrorTy {
    /// Empty compound statement.
    NoStmt = 0,
    /// More than one statement in a compound statement.
    MoreThanOneStmt,
    /// Not an assignment binary operator.
    NotAnAssignment,
    /// Not a conditional operator.
    NotCondOp,
    /// Wrong false expr. According to the spec, 'x' should be at the false
    /// expression of a conditional expression.
    WrongFalseExpr,
    /// The condition of a conditional expression is not a binary operator.
    NotABinaryOp,
    /// Invalid binary operator (not <, >, or ==).
    InvalidBinaryOp,
    /// Invalid comparison (not x == e, e == x, x ordop expr, or expr ordop x).
    InvalidComparison,
    /// X is not a lvalue.
    XNotLValue,
    /// Not a scalar.
    NotScalar,
    /// Not an integer.
    NotInteger,
    /// 'else' statement is not expected.
    UnexpectedElse,
    /// Not an equality operator.
    NotEQ,
    /// Invalid assignment (not v == x).
    InvalidAssignment,
    /// Not if statement
    NotIfStmt,
    /// More than two statements in a compound statement.
    MoreThanTwoStmts,
    /// Not a compound statement.
    NotCompoundStmt,
    /// No else statement.
    NoElse,
    /// Not 'if (r)'.
    InvalidCondition,
    /// No error.
    NoError,
  };

  struct ErrorInfoTy {
    ErrorTy Error;
    SourceLocation ErrorLoc;
    SourceRange ErrorRange;
    SourceLocation NoteLoc;
    SourceRange NoteRange;
  };

  OpenMPAtomicCompareChecker(Sema &S) : ContextRef(S.getASTContext()) {}

  /// Check if statement \a S is valid for <tt>atomic compare</tt>.
  bool checkStmt(Stmt *S, ErrorInfoTy &ErrorInfo);

  Expr *getX() const { return X; }
  Expr *getE() const { return E; }
  Expr *getD() const { return D; }
  Expr *getCond() const { return C; }
  bool isXBinopExpr() const { return IsXBinopExpr; }

protected:
  /// Reference to ASTContext
  ASTContext &ContextRef;
  /// 'x' lvalue part of the source atomic expression.
  Expr *X = nullptr;
  /// 'expr' or 'e' rvalue part of the source atomic expression.
  Expr *E = nullptr;
  /// 'd' rvalue part of the source atomic expression.
  Expr *D = nullptr;
  /// 'cond' part of the source atomic expression. It is in one of the following
  /// forms:
  /// expr ordop x
  /// x ordop expr
  /// x == e
  /// e == x
  Expr *C = nullptr;
  /// True if the cond expr is in the form of 'x ordop expr'.
  bool IsXBinopExpr = true;

  /// Check if it is a valid conditional update statement (cond-update-stmt).
  bool checkCondUpdateStmt(IfStmt *S, ErrorInfoTy &ErrorInfo);

  /// Check if it is a valid conditional expression statement (cond-expr-stmt).
  bool checkCondExprStmt(Stmt *S, ErrorInfoTy &ErrorInfo);

  /// Check if all captured values have right type.
  bool checkType(ErrorInfoTy &ErrorInfo) const;

  static bool CheckValue(const Expr *E, ErrorInfoTy &ErrorInfo,
                         bool ShouldBeLValue, bool ShouldBeInteger = false) {
    if (E->isInstantiationDependent())
      return true;

    if (ShouldBeLValue && !E->isLValue()) {
      ErrorInfo.Error = ErrorTy::XNotLValue;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = E->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = E->getSourceRange();
      return false;
    }

    QualType QTy = E->getType();
    if (!QTy->isScalarType()) {
      ErrorInfo.Error = ErrorTy::NotScalar;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = E->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = E->getSourceRange();
      return false;
    }
    if (ShouldBeInteger && !QTy->isIntegerType()) {
      ErrorInfo.Error = ErrorTy::NotInteger;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = E->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = E->getSourceRange();
      return false;
    }

    return true;
  }
};

bool OpenMPAtomicCompareChecker::checkCondUpdateStmt(IfStmt *S,
                                                     ErrorInfoTy &ErrorInfo) {
  auto *Then = S->getThen();
  if (auto *CS = dyn_cast<CompoundStmt>(Then)) {
    if (CS->body_empty()) {
      ErrorInfo.Error = ErrorTy::NoStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
      return false;
    }
    if (CS->size() > 1) {
      ErrorInfo.Error = ErrorTy::MoreThanOneStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getSourceRange();
      return false;
    }
    Then = CS->body_front();
  }

  auto *BO = dyn_cast<BinaryOperator>(Then);
  if (!BO) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Then->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Then->getSourceRange();
    return false;
  }
  if (BO->getOpcode() != BO_Assign) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = BO->getExprLoc();
    ErrorInfo.NoteLoc = BO->getOperatorLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getSourceRange();
    return false;
  }

  X = BO->getLHS();

  auto *Cond = dyn_cast<BinaryOperator>(S->getCond());
  if (!Cond) {
    ErrorInfo.Error = ErrorTy::NotABinaryOp;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S->getCond()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getCond()->getSourceRange();
    return false;
  }

  switch (Cond->getOpcode()) {
  case BO_EQ: {
    C = Cond;
    D = BO->getRHS();
    if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getLHS())) {
      E = Cond->getRHS();
    } else if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getRHS())) {
      E = Cond->getLHS();
    } else {
      ErrorInfo.Error = ErrorTy::InvalidComparison;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
      return false;
    }
    break;
  }
  case BO_LT:
  case BO_GT: {
    E = BO->getRHS();
    if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getLHS()) &&
        checkIfTwoExprsAreSame(ContextRef, E, Cond->getRHS())) {
      C = Cond;
    } else if (checkIfTwoExprsAreSame(ContextRef, E, Cond->getLHS()) &&
               checkIfTwoExprsAreSame(ContextRef, X, Cond->getRHS())) {
      C = Cond;
      IsXBinopExpr = false;
    } else {
      ErrorInfo.Error = ErrorTy::InvalidComparison;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
      return false;
    }
    break;
  }
  default:
    ErrorInfo.Error = ErrorTy::InvalidBinaryOp;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
    return false;
  }

  if (S->getElse()) {
    ErrorInfo.Error = ErrorTy::UnexpectedElse;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S->getElse()->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getElse()->getSourceRange();
    return false;
  }

  return true;
}

bool OpenMPAtomicCompareChecker::checkCondExprStmt(Stmt *S,
                                                   ErrorInfoTy &ErrorInfo) {
  auto *BO = dyn_cast<BinaryOperator>(S);
  if (!BO) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getSourceRange();
    return false;
  }
  if (BO->getOpcode() != BO_Assign) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = BO->getExprLoc();
    ErrorInfo.NoteLoc = BO->getOperatorLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getSourceRange();
    return false;
  }

  X = BO->getLHS();

  auto *CO = dyn_cast<ConditionalOperator>(BO->getRHS()->IgnoreParenImpCasts());
  if (!CO) {
    ErrorInfo.Error = ErrorTy::NotCondOp;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = BO->getRHS()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getRHS()->getSourceRange();
    return false;
  }

  if (!checkIfTwoExprsAreSame(ContextRef, X, CO->getFalseExpr())) {
    ErrorInfo.Error = ErrorTy::WrongFalseExpr;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CO->getFalseExpr()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange =
        CO->getFalseExpr()->getSourceRange();
    return false;
  }

  auto *Cond = dyn_cast<BinaryOperator>(CO->getCond());
  if (!Cond) {
    ErrorInfo.Error = ErrorTy::NotABinaryOp;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CO->getCond()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange =
        CO->getCond()->getSourceRange();
    return false;
  }

  switch (Cond->getOpcode()) {
  case BO_EQ: {
    C = Cond;
    D = CO->getTrueExpr();
    if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getLHS())) {
      E = Cond->getRHS();
    } else if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getRHS())) {
      E = Cond->getLHS();
    } else {
      ErrorInfo.Error = ErrorTy::InvalidComparison;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
      return false;
    }
    break;
  }
  case BO_LT:
  case BO_GT: {
    E = CO->getTrueExpr();
    if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getLHS()) &&
        checkIfTwoExprsAreSame(ContextRef, E, Cond->getRHS())) {
      C = Cond;
    } else if (checkIfTwoExprsAreSame(ContextRef, E, Cond->getLHS()) &&
               checkIfTwoExprsAreSame(ContextRef, X, Cond->getRHS())) {
      C = Cond;
      IsXBinopExpr = false;
    } else {
      ErrorInfo.Error = ErrorTy::InvalidComparison;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
      return false;
    }
    break;
  }
  default:
    ErrorInfo.Error = ErrorTy::InvalidBinaryOp;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
    return false;
  }

  return true;
}

bool OpenMPAtomicCompareChecker::checkType(ErrorInfoTy &ErrorInfo) const {
  // 'x' and 'e' cannot be nullptr
  assert(X && E && "X and E cannot be nullptr");

  if (!CheckValue(X, ErrorInfo, true))
    return false;

  if (!CheckValue(E, ErrorInfo, false))
    return false;

  if (D && !CheckValue(D, ErrorInfo, false))
    return false;

  return true;
}

bool OpenMPAtomicCompareChecker::checkStmt(
    Stmt *S, OpenMPAtomicCompareChecker::ErrorInfoTy &ErrorInfo) {
  auto *CS = dyn_cast<CompoundStmt>(S);
  if (CS) {
    if (CS->body_empty()) {
      ErrorInfo.Error = ErrorTy::NoStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
      return false;
    }

    if (CS->size() != 1) {
      ErrorInfo.Error = ErrorTy::MoreThanOneStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
      return false;
    }
    S = CS->body_front();
  }

  auto Res = false;

  if (auto *IS = dyn_cast<IfStmt>(S)) {
    // Check if the statement is in one of the following forms
    // (cond-update-stmt):
    // if (expr ordop x) { x = expr; }
    // if (x ordop expr) { x = expr; }
    // if (x == e) { x = d; }
    Res = checkCondUpdateStmt(IS, ErrorInfo);
  } else {
    // Check if the statement is in one of the following forms (cond-expr-stmt):
    // x = expr ordop x ? expr : x;
    // x = x ordop expr ? expr : x;
    // x = x == e ? d : x;
    Res = checkCondExprStmt(S, ErrorInfo);
  }

  if (!Res)
    return false;

  return checkType(ErrorInfo);
}

class OpenMPAtomicCompareCaptureChecker final
    : public OpenMPAtomicCompareChecker {
public:
  OpenMPAtomicCompareCaptureChecker(Sema &S) : OpenMPAtomicCompareChecker(S) {}

  Expr *getV() const { return V; }
  Expr *getR() const { return R; }
  bool isFailOnly() const { return IsFailOnly; }
  bool isPostfixUpdate() const { return IsPostfixUpdate; }

  /// Check if statement \a S is valid for <tt>atomic compare capture</tt>.
  bool checkStmt(Stmt *S, ErrorInfoTy &ErrorInfo);

private:
  bool checkType(ErrorInfoTy &ErrorInfo);

  // NOTE: Form 3, 4, 5 in the following comments mean the 3rd, 4th, and 5th
  // form of 'conditional-update-capture-atomic' structured block on the v5.2
  // spec p.p. 82:
  // (1) { v = x; cond-update-stmt }
  // (2) { cond-update-stmt v = x; }
  // (3) if(x == e) { x = d; } else { v = x; }
  // (4) { r = x == e; if(r) { x = d; } }
  // (5) { r = x == e; if(r) { x = d; } else { v = x; } }

  /// Check if it is valid 'if(x == e) { x = d; } else { v = x; }' (form 3)
  bool checkForm3(IfStmt *S, ErrorInfoTy &ErrorInfo);

  /// Check if it is valid '{ r = x == e; if(r) { x = d; } }',
  /// or '{ r = x == e; if(r) { x = d; } else { v = x; } }' (form 4 and 5)
  bool checkForm45(Stmt *S, ErrorInfoTy &ErrorInfo);

  /// 'v' lvalue part of the source atomic expression.
  Expr *V = nullptr;
  /// 'r' lvalue part of the source atomic expression.
  Expr *R = nullptr;
  /// If 'v' is only updated when the comparison fails.
  bool IsFailOnly = false;
  /// If original value of 'x' must be stored in 'v', not an updated one.
  bool IsPostfixUpdate = false;
};

bool OpenMPAtomicCompareCaptureChecker::checkType(ErrorInfoTy &ErrorInfo) {
  if (!OpenMPAtomicCompareChecker::checkType(ErrorInfo))
    return false;

  if (V && !CheckValue(V, ErrorInfo, true))
    return false;

  if (R && !CheckValue(R, ErrorInfo, true, true))
    return false;

  return true;
}

bool OpenMPAtomicCompareCaptureChecker::checkForm3(IfStmt *S,
                                                   ErrorInfoTy &ErrorInfo) {
  IsFailOnly = true;

  auto *Then = S->getThen();
  if (auto *CS = dyn_cast<CompoundStmt>(Then)) {
    if (CS->body_empty()) {
      ErrorInfo.Error = ErrorTy::NoStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
      return false;
    }
    if (CS->size() > 1) {
      ErrorInfo.Error = ErrorTy::MoreThanOneStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
      return false;
    }
    Then = CS->body_front();
  }

  auto *BO = dyn_cast<BinaryOperator>(Then);
  if (!BO) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Then->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Then->getSourceRange();
    return false;
  }
  if (BO->getOpcode() != BO_Assign) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = BO->getExprLoc();
    ErrorInfo.NoteLoc = BO->getOperatorLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getSourceRange();
    return false;
  }

  X = BO->getLHS();
  D = BO->getRHS();

  auto *Cond = dyn_cast<BinaryOperator>(S->getCond());
  if (!Cond) {
    ErrorInfo.Error = ErrorTy::NotABinaryOp;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S->getCond()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getCond()->getSourceRange();
    return false;
  }
  if (Cond->getOpcode() != BO_EQ) {
    ErrorInfo.Error = ErrorTy::NotEQ;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
    return false;
  }

  if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getLHS())) {
    E = Cond->getRHS();
  } else if (checkIfTwoExprsAreSame(ContextRef, X, Cond->getRHS())) {
    E = Cond->getLHS();
  } else {
    ErrorInfo.Error = ErrorTy::InvalidComparison;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Cond->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();
    return false;
  }

  C = Cond;

  if (!S->getElse()) {
    ErrorInfo.Error = ErrorTy::NoElse;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getSourceRange();
    return false;
  }

  auto *Else = S->getElse();
  if (auto *CS = dyn_cast<CompoundStmt>(Else)) {
    if (CS->body_empty()) {
      ErrorInfo.Error = ErrorTy::NoStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
      return false;
    }
    if (CS->size() > 1) {
      ErrorInfo.Error = ErrorTy::MoreThanOneStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getSourceRange();
      return false;
    }
    Else = CS->body_front();
  }

  auto *ElseBO = dyn_cast<BinaryOperator>(Else);
  if (!ElseBO) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Else->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Else->getSourceRange();
    return false;
  }
  if (ElseBO->getOpcode() != BO_Assign) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ElseBO->getExprLoc();
    ErrorInfo.NoteLoc = ElseBO->getOperatorLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ElseBO->getSourceRange();
    return false;
  }

  if (!checkIfTwoExprsAreSame(ContextRef, X, ElseBO->getRHS())) {
    ErrorInfo.Error = ErrorTy::InvalidAssignment;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = ElseBO->getRHS()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange =
        ElseBO->getRHS()->getSourceRange();
    return false;
  }

  V = ElseBO->getLHS();

  return checkType(ErrorInfo);
}

bool OpenMPAtomicCompareCaptureChecker::checkForm45(Stmt *S,
                                                    ErrorInfoTy &ErrorInfo) {
  // We don't check here as they should be already done before call this
  // function.
  auto *CS = cast<CompoundStmt>(S);
  assert(CS->size() == 2 && "CompoundStmt size is not expected");
  auto *S1 = cast<BinaryOperator>(CS->body_front());
  auto *S2 = cast<IfStmt>(CS->body_back());
  assert(S1->getOpcode() == BO_Assign && "unexpected binary operator");

  if (!checkIfTwoExprsAreSame(ContextRef, S1->getLHS(), S2->getCond())) {
    ErrorInfo.Error = ErrorTy::InvalidCondition;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S2->getCond()->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S1->getLHS()->getSourceRange();
    return false;
  }

  R = S1->getLHS();

  auto *Then = S2->getThen();
  if (auto *ThenCS = dyn_cast<CompoundStmt>(Then)) {
    if (ThenCS->body_empty()) {
      ErrorInfo.Error = ErrorTy::NoStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = ThenCS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ThenCS->getSourceRange();
      return false;
    }
    if (ThenCS->size() > 1) {
      ErrorInfo.Error = ErrorTy::MoreThanOneStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = ThenCS->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ThenCS->getSourceRange();
      return false;
    }
    Then = ThenCS->body_front();
  }

  auto *ThenBO = dyn_cast<BinaryOperator>(Then);
  if (!ThenBO) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S2->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S2->getSourceRange();
    return false;
  }
  if (ThenBO->getOpcode() != BO_Assign) {
    ErrorInfo.Error = ErrorTy::NotAnAssignment;
    ErrorInfo.ErrorLoc = ThenBO->getExprLoc();
    ErrorInfo.NoteLoc = ThenBO->getOperatorLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ThenBO->getSourceRange();
    return false;
  }

  X = ThenBO->getLHS();
  D = ThenBO->getRHS();

  auto *BO = cast<BinaryOperator>(S1->getRHS()->IgnoreImpCasts());
  if (BO->getOpcode() != BO_EQ) {
    ErrorInfo.Error = ErrorTy::NotEQ;
    ErrorInfo.ErrorLoc = BO->getExprLoc();
    ErrorInfo.NoteLoc = BO->getOperatorLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getSourceRange();
    return false;
  }

  C = BO;

  if (checkIfTwoExprsAreSame(ContextRef, X, BO->getLHS())) {
    E = BO->getRHS();
  } else if (checkIfTwoExprsAreSame(ContextRef, X, BO->getRHS())) {
    E = BO->getLHS();
  } else {
    ErrorInfo.Error = ErrorTy::InvalidComparison;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = BO->getExprLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getSourceRange();
    return false;
  }

  if (S2->getElse()) {
    IsFailOnly = true;

    auto *Else = S2->getElse();
    if (auto *ElseCS = dyn_cast<CompoundStmt>(Else)) {
      if (ElseCS->body_empty()) {
        ErrorInfo.Error = ErrorTy::NoStmt;
        ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = ElseCS->getBeginLoc();
        ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ElseCS->getSourceRange();
        return false;
      }
      if (ElseCS->size() > 1) {
        ErrorInfo.Error = ErrorTy::MoreThanOneStmt;
        ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = ElseCS->getBeginLoc();
        ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ElseCS->getSourceRange();
        return false;
      }
      Else = ElseCS->body_front();
    }

    auto *ElseBO = dyn_cast<BinaryOperator>(Else);
    if (!ElseBO) {
      ErrorInfo.Error = ErrorTy::NotAnAssignment;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = Else->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Else->getSourceRange();
      return false;
    }
    if (ElseBO->getOpcode() != BO_Assign) {
      ErrorInfo.Error = ErrorTy::NotAnAssignment;
      ErrorInfo.ErrorLoc = ElseBO->getExprLoc();
      ErrorInfo.NoteLoc = ElseBO->getOperatorLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange = ElseBO->getSourceRange();
      return false;
    }
    if (!checkIfTwoExprsAreSame(ContextRef, X, ElseBO->getRHS())) {
      ErrorInfo.Error = ErrorTy::InvalidAssignment;
      ErrorInfo.ErrorLoc = ElseBO->getRHS()->getExprLoc();
      ErrorInfo.NoteLoc = X->getExprLoc();
      ErrorInfo.ErrorRange = ElseBO->getRHS()->getSourceRange();
      ErrorInfo.NoteRange = X->getSourceRange();
      return false;
    }

    V = ElseBO->getLHS();
  }

  return checkType(ErrorInfo);
}

bool OpenMPAtomicCompareCaptureChecker::checkStmt(Stmt *S,
                                                  ErrorInfoTy &ErrorInfo) {
  // if(x == e) { x = d; } else { v = x; }
  if (auto *IS = dyn_cast<IfStmt>(S))
    return checkForm3(IS, ErrorInfo);

  auto *CS = dyn_cast<CompoundStmt>(S);
  if (!CS) {
    ErrorInfo.Error = ErrorTy::NotCompoundStmt;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = S->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = S->getSourceRange();
    return false;
  }
  if (CS->body_empty()) {
    ErrorInfo.Error = ErrorTy::NoStmt;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
    return false;
  }

  // { if(x == e) { x = d; } else { v = x; } }
  if (CS->size() == 1) {
    auto *IS = dyn_cast<IfStmt>(CS->body_front());
    if (!IS) {
      ErrorInfo.Error = ErrorTy::NotIfStmt;
      ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->body_front()->getBeginLoc();
      ErrorInfo.ErrorRange = ErrorInfo.NoteRange =
          CS->body_front()->getSourceRange();
      return false;
    }

    return checkForm3(IS, ErrorInfo);
  } else if (CS->size() == 2) {
    auto *S1 = CS->body_front();
    auto *S2 = CS->body_back();

    Stmt *UpdateStmt = nullptr;
    Stmt *CondUpdateStmt = nullptr;
    Stmt *CondExprStmt = nullptr;

    if (auto *BO = dyn_cast<BinaryOperator>(S1)) {
      // It could be one of the following cases:
      // { v = x; cond-update-stmt }
      // { v = x; cond-expr-stmt }
      // { cond-expr-stmt; v = x; }
      // form 45
      if (isa<BinaryOperator>(BO->getRHS()->IgnoreImpCasts()) ||
          isa<ConditionalOperator>(BO->getRHS()->IgnoreImpCasts())) {
        // check if form 45
        if (isa<IfStmt>(S2))
          return checkForm45(CS, ErrorInfo);
        // { cond-expr-stmt; v = x; }
        CondExprStmt = S1;
        UpdateStmt = S2;
      } else {
        IsPostfixUpdate = true;
        UpdateStmt = S1;
        if (isa<IfStmt>(S2)) {
          // { v = x; cond-update-stmt }
          CondUpdateStmt = S2;
        } else {
          // { v = x; cond-expr-stmt }
          CondExprStmt = S2;
        }
      }
    } else {
      // { cond-update-stmt v = x; }
      UpdateStmt = S2;
      CondUpdateStmt = S1;
    }

    auto CheckCondUpdateStmt = [this, &ErrorInfo](Stmt *CUS) {
      auto *IS = dyn_cast<IfStmt>(CUS);
      if (!IS) {
        ErrorInfo.Error = ErrorTy::NotIfStmt;
        ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CUS->getBeginLoc();
        ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CUS->getSourceRange();
        return false;
      }

      return checkCondUpdateStmt(IS, ErrorInfo);
    };

    // CheckUpdateStmt has to be called *after* CheckCondUpdateStmt.
    auto CheckUpdateStmt = [this, &ErrorInfo](Stmt *US) {
      auto *BO = dyn_cast<BinaryOperator>(US);
      if (!BO) {
        ErrorInfo.Error = ErrorTy::NotAnAssignment;
        ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = US->getBeginLoc();
        ErrorInfo.ErrorRange = ErrorInfo.NoteRange = US->getSourceRange();
        return false;
      }
      if (BO->getOpcode() != BO_Assign) {
        ErrorInfo.Error = ErrorTy::NotAnAssignment;
        ErrorInfo.ErrorLoc = BO->getExprLoc();
        ErrorInfo.NoteLoc = BO->getOperatorLoc();
        ErrorInfo.ErrorRange = ErrorInfo.NoteRange = BO->getSourceRange();
        return false;
      }
      if (!checkIfTwoExprsAreSame(ContextRef, this->X, BO->getRHS())) {
        ErrorInfo.Error = ErrorTy::InvalidAssignment;
        ErrorInfo.ErrorLoc = BO->getRHS()->getExprLoc();
        ErrorInfo.NoteLoc = this->X->getExprLoc();
        ErrorInfo.ErrorRange = BO->getRHS()->getSourceRange();
        ErrorInfo.NoteRange = this->X->getSourceRange();
        return false;
      }

      this->V = BO->getLHS();

      return true;
    };

    if (CondUpdateStmt && !CheckCondUpdateStmt(CondUpdateStmt))
      return false;
    if (CondExprStmt && !checkCondExprStmt(CondExprStmt, ErrorInfo))
      return false;
    if (!CheckUpdateStmt(UpdateStmt))
      return false;
  } else {
    ErrorInfo.Error = ErrorTy::MoreThanTwoStmts;
    ErrorInfo.ErrorLoc = ErrorInfo.NoteLoc = CS->getBeginLoc();
    ErrorInfo.ErrorRange = ErrorInfo.NoteRange = CS->getSourceRange();
    return false;
  }

  return checkType(ErrorInfo);
}
} // namespace

StmtResult SemaOpenMP::ActOnOpenMPAtomicDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  ASTContext &Context = getASTContext();
  // Register location of the first atomic directive.
  DSAStack->addAtomicDirectiveLoc(StartLoc);
  if (!AStmt)
    return StmtError();

  // 1.2.2 OpenMP Language Terminology
  // Structured block - An executable statement with a single entry at the
  // top and a single exit at the bottom.
  // The point of exit cannot be a branch out of the structured block.
  // longjmp() and throw() must not violate the entry/exit criteria.
  OpenMPClauseKind AtomicKind = OMPC_unknown;
  SourceLocation AtomicKindLoc;
  OpenMPClauseKind MemOrderKind = OMPC_unknown;
  SourceLocation MemOrderLoc;
  bool MutexClauseEncountered = false;
  llvm::SmallSet<OpenMPClauseKind, 2> EncounteredAtomicKinds;
  for (const OMPClause *C : Clauses) {
    switch (C->getClauseKind()) {
    case OMPC_read:
    case OMPC_write:
    case OMPC_update:
      MutexClauseEncountered = true;
      [[fallthrough]];
    case OMPC_capture:
    case OMPC_compare: {
      if (AtomicKind != OMPC_unknown && MutexClauseEncountered) {
        Diag(C->getBeginLoc(), diag::err_omp_atomic_several_clauses)
            << SourceRange(C->getBeginLoc(), C->getEndLoc());
        Diag(AtomicKindLoc, diag::note_omp_previous_mem_order_clause)
            << getOpenMPClauseName(AtomicKind);
      } else {
        AtomicKind = C->getClauseKind();
        AtomicKindLoc = C->getBeginLoc();
        if (!EncounteredAtomicKinds.insert(C->getClauseKind()).second) {
          Diag(C->getBeginLoc(), diag::err_omp_atomic_several_clauses)
              << SourceRange(C->getBeginLoc(), C->getEndLoc());
          Diag(AtomicKindLoc, diag::note_omp_previous_mem_order_clause)
              << getOpenMPClauseName(AtomicKind);
        }
      }
      break;
    }
    case OMPC_weak:
    case OMPC_fail: {
      if (!EncounteredAtomicKinds.contains(OMPC_compare)) {
        Diag(C->getBeginLoc(), diag::err_omp_atomic_no_compare)
            << getOpenMPClauseName(C->getClauseKind())
            << SourceRange(C->getBeginLoc(), C->getEndLoc());
        return StmtError();
      }
      break;
    }
    case OMPC_seq_cst:
    case OMPC_acq_rel:
    case OMPC_acquire:
    case OMPC_release:
    case OMPC_relaxed: {
      if (MemOrderKind != OMPC_unknown) {
        Diag(C->getBeginLoc(), diag::err_omp_several_mem_order_clauses)
            << getOpenMPDirectiveName(OMPD_atomic) << 0
            << SourceRange(C->getBeginLoc(), C->getEndLoc());
        Diag(MemOrderLoc, diag::note_omp_previous_mem_order_clause)
            << getOpenMPClauseName(MemOrderKind);
      } else {
        MemOrderKind = C->getClauseKind();
        MemOrderLoc = C->getBeginLoc();
      }
      break;
    }
    // The following clauses are allowed, but we don't need to do anything here.
    case OMPC_hint:
      break;
    default:
      llvm_unreachable("unknown clause is encountered");
    }
  }
  bool IsCompareCapture = false;
  if (EncounteredAtomicKinds.contains(OMPC_compare) &&
      EncounteredAtomicKinds.contains(OMPC_capture)) {
    IsCompareCapture = true;
    AtomicKind = OMPC_compare;
  }
  // OpenMP 5.0, 2.17.7 atomic Construct, Restrictions
  // If atomic-clause is read then memory-order-clause must not be acq_rel or
  // release.
  // If atomic-clause is write then memory-order-clause must not be acq_rel or
  // acquire.
  // If atomic-clause is update or not present then memory-order-clause must not
  // be acq_rel or acquire.
  if ((AtomicKind == OMPC_read &&
       (MemOrderKind == OMPC_acq_rel || MemOrderKind == OMPC_release)) ||
      ((AtomicKind == OMPC_write || AtomicKind == OMPC_update ||
        AtomicKind == OMPC_unknown) &&
       (MemOrderKind == OMPC_acq_rel || MemOrderKind == OMPC_acquire))) {
    SourceLocation Loc = AtomicKindLoc;
    if (AtomicKind == OMPC_unknown)
      Loc = StartLoc;
    Diag(Loc, diag::err_omp_atomic_incompatible_mem_order_clause)
        << getOpenMPClauseName(AtomicKind)
        << (AtomicKind == OMPC_unknown ? 1 : 0)
        << getOpenMPClauseName(MemOrderKind);
    Diag(MemOrderLoc, diag::note_omp_previous_mem_order_clause)
        << getOpenMPClauseName(MemOrderKind);
  }

  Stmt *Body = AStmt;
  if (auto *EWC = dyn_cast<ExprWithCleanups>(Body))
    Body = EWC->getSubExpr();

  Expr *X = nullptr;
  Expr *V = nullptr;
  Expr *E = nullptr;
  Expr *UE = nullptr;
  Expr *D = nullptr;
  Expr *CE = nullptr;
  Expr *R = nullptr;
  bool IsXLHSInRHSPart = false;
  bool IsPostfixUpdate = false;
  bool IsFailOnly = false;
  // OpenMP [2.12.6, atomic Construct]
  // In the next expressions:
  // * x and v (as applicable) are both l-value expressions with scalar type.
  // * During the execution of an atomic region, multiple syntactic
  // occurrences of x must designate the same storage location.
  // * Neither of v and expr (as applicable) may access the storage location
  // designated by x.
  // * Neither of x and expr (as applicable) may access the storage location
  // designated by v.
  // * expr is an expression with scalar type.
  // * binop is one of +, *, -, /, &, ^, |, <<, or >>.
  // * binop, binop=, ++, and -- are not overloaded operators.
  // * The expression x binop expr must be numerically equivalent to x binop
  // (expr). This requirement is satisfied if the operators in expr have
  // precedence greater than binop, or by using parentheses around expr or
  // subexpressions of expr.
  // * The expression expr binop x must be numerically equivalent to (expr)
  // binop x. This requirement is satisfied if the operators in expr have
  // precedence equal to or greater than binop, or by using parentheses around
  // expr or subexpressions of expr.
  // * For forms that allow multiple occurrences of x, the number of times
  // that x is evaluated is unspecified.
  if (AtomicKind == OMPC_read) {
    enum {
      NotAnExpression,
      NotAnAssignmentOp,
      NotAScalarType,
      NotAnLValue,
      NoError
    } ErrorFound = NoError;
    SourceLocation ErrorLoc, NoteLoc;
    SourceRange ErrorRange, NoteRange;
    // If clause is read:
    //  v = x;
    if (const auto *AtomicBody = dyn_cast<Expr>(Body)) {
      const auto *AtomicBinOp =
          dyn_cast<BinaryOperator>(AtomicBody->IgnoreParenImpCasts());
      if (AtomicBinOp && AtomicBinOp->getOpcode() == BO_Assign) {
        X = AtomicBinOp->getRHS()->IgnoreParenImpCasts();
        V = AtomicBinOp->getLHS()->IgnoreParenImpCasts();
        if ((X->isInstantiationDependent() || X->getType()->isScalarType()) &&
            (V->isInstantiationDependent() || V->getType()->isScalarType())) {
          if (!X->isLValue() || !V->isLValue()) {
            const Expr *NotLValueExpr = X->isLValue() ? V : X;
            ErrorFound = NotAnLValue;
            ErrorLoc = AtomicBinOp->getExprLoc();
            ErrorRange = AtomicBinOp->getSourceRange();
            NoteLoc = NotLValueExpr->getExprLoc();
            NoteRange = NotLValueExpr->getSourceRange();
          }
        } else if (!X->isInstantiationDependent() ||
                   !V->isInstantiationDependent()) {
          const Expr *NotScalarExpr =
              (X->isInstantiationDependent() || X->getType()->isScalarType())
                  ? V
                  : X;
          ErrorFound = NotAScalarType;
          ErrorLoc = AtomicBinOp->getExprLoc();
          ErrorRange = AtomicBinOp->getSourceRange();
          NoteLoc = NotScalarExpr->getExprLoc();
          NoteRange = NotScalarExpr->getSourceRange();
        }
      } else if (!AtomicBody->isInstantiationDependent()) {
        ErrorFound = NotAnAssignmentOp;
        ErrorLoc = AtomicBody->getExprLoc();
        ErrorRange = AtomicBody->getSourceRange();
        NoteLoc = AtomicBinOp ? AtomicBinOp->getOperatorLoc()
                              : AtomicBody->getExprLoc();
        NoteRange = AtomicBinOp ? AtomicBinOp->getSourceRange()
                                : AtomicBody->getSourceRange();
      }
    } else {
      ErrorFound = NotAnExpression;
      NoteLoc = ErrorLoc = Body->getBeginLoc();
      NoteRange = ErrorRange = SourceRange(NoteLoc, NoteLoc);
    }
    if (ErrorFound != NoError) {
      Diag(ErrorLoc, diag::err_omp_atomic_read_not_expression_statement)
          << ErrorRange;
      Diag(NoteLoc, diag::note_omp_atomic_read_write)
          << ErrorFound << NoteRange;
      return StmtError();
    }
    if (SemaRef.CurContext->isDependentContext())
      V = X = nullptr;
  } else if (AtomicKind == OMPC_write) {
    enum {
      NotAnExpression,
      NotAnAssignmentOp,
      NotAScalarType,
      NotAnLValue,
      NoError
    } ErrorFound = NoError;
    SourceLocation ErrorLoc, NoteLoc;
    SourceRange ErrorRange, NoteRange;
    // If clause is write:
    //  x = expr;
    if (const auto *AtomicBody = dyn_cast<Expr>(Body)) {
      const auto *AtomicBinOp =
          dyn_cast<BinaryOperator>(AtomicBody->IgnoreParenImpCasts());
      if (AtomicBinOp && AtomicBinOp->getOpcode() == BO_Assign) {
        X = AtomicBinOp->getLHS();
        E = AtomicBinOp->getRHS();
        if ((X->isInstantiationDependent() || X->getType()->isScalarType()) &&
            (E->isInstantiationDependent() || E->getType()->isScalarType())) {
          if (!X->isLValue()) {
            ErrorFound = NotAnLValue;
            ErrorLoc = AtomicBinOp->getExprLoc();
            ErrorRange = AtomicBinOp->getSourceRange();
            NoteLoc = X->getExprLoc();
            NoteRange = X->getSourceRange();
          }
        } else if (!X->isInstantiationDependent() ||
                   !E->isInstantiationDependent()) {
          const Expr *NotScalarExpr =
              (X->isInstantiationDependent() || X->getType()->isScalarType())
                  ? E
                  : X;
          ErrorFound = NotAScalarType;
          ErrorLoc = AtomicBinOp->getExprLoc();
          ErrorRange = AtomicBinOp->getSourceRange();
          NoteLoc = NotScalarExpr->getExprLoc();
          NoteRange = NotScalarExpr->getSourceRange();
        }
      } else if (!AtomicBody->isInstantiationDependent()) {
        ErrorFound = NotAnAssignmentOp;
        ErrorLoc = AtomicBody->getExprLoc();
        ErrorRange = AtomicBody->getSourceRange();
        NoteLoc = AtomicBinOp ? AtomicBinOp->getOperatorLoc()
                              : AtomicBody->getExprLoc();
        NoteRange = AtomicBinOp ? AtomicBinOp->getSourceRange()
                                : AtomicBody->getSourceRange();
      }
    } else {
      ErrorFound = NotAnExpression;
      NoteLoc = ErrorLoc = Body->getBeginLoc();
      NoteRange = ErrorRange = SourceRange(NoteLoc, NoteLoc);
    }
    if (ErrorFound != NoError) {
      Diag(ErrorLoc, diag::err_omp_atomic_write_not_expression_statement)
          << ErrorRange;
      Diag(NoteLoc, diag::note_omp_atomic_read_write)
          << ErrorFound << NoteRange;
      return StmtError();
    }
    if (SemaRef.CurContext->isDependentContext())
      E = X = nullptr;
  } else if (AtomicKind == OMPC_update || AtomicKind == OMPC_unknown) {
    // If clause is update:
    //  x++;
    //  x--;
    //  ++x;
    //  --x;
    //  x binop= expr;
    //  x = x binop expr;
    //  x = expr binop x;
    OpenMPAtomicUpdateChecker Checker(SemaRef);
    if (Checker.checkStatement(
            Body,
            (AtomicKind == OMPC_update)
                ? diag::err_omp_atomic_update_not_expression_statement
                : diag::err_omp_atomic_not_expression_statement,
            diag::note_omp_atomic_update))
      return StmtError();
    if (!SemaRef.CurContext->isDependentContext()) {
      E = Checker.getExpr();
      X = Checker.getX();
      UE = Checker.getUpdateExpr();
      IsXLHSInRHSPart = Checker.isXLHSInRHSPart();
    }
  } else if (AtomicKind == OMPC_capture) {
    enum {
      NotAnAssignmentOp,
      NotACompoundStatement,
      NotTwoSubstatements,
      NotASpecificExpression,
      NoError
    } ErrorFound = NoError;
    SourceLocation ErrorLoc, NoteLoc;
    SourceRange ErrorRange, NoteRange;
    if (const auto *AtomicBody = dyn_cast<Expr>(Body)) {
      // If clause is a capture:
      //  v = x++;
      //  v = x--;
      //  v = ++x;
      //  v = --x;
      //  v = x binop= expr;
      //  v = x = x binop expr;
      //  v = x = expr binop x;
      const auto *AtomicBinOp =
          dyn_cast<BinaryOperator>(AtomicBody->IgnoreParenImpCasts());
      if (AtomicBinOp && AtomicBinOp->getOpcode() == BO_Assign) {
        V = AtomicBinOp->getLHS();
        Body = AtomicBinOp->getRHS()->IgnoreParenImpCasts();
        OpenMPAtomicUpdateChecker Checker(SemaRef);
        if (Checker.checkStatement(
                Body, diag::err_omp_atomic_capture_not_expression_statement,
                diag::note_omp_atomic_update))
          return StmtError();
        E = Checker.getExpr();
        X = Checker.getX();
        UE = Checker.getUpdateExpr();
        IsXLHSInRHSPart = Checker.isXLHSInRHSPart();
        IsPostfixUpdate = Checker.isPostfixUpdate();
      } else if (!AtomicBody->isInstantiationDependent()) {
        ErrorLoc = AtomicBody->getExprLoc();
        ErrorRange = AtomicBody->getSourceRange();
        NoteLoc = AtomicBinOp ? AtomicBinOp->getOperatorLoc()
                              : AtomicBody->getExprLoc();
        NoteRange = AtomicBinOp ? AtomicBinOp->getSourceRange()
                                : AtomicBody->getSourceRange();
        ErrorFound = NotAnAssignmentOp;
      }
      if (ErrorFound != NoError) {
        Diag(ErrorLoc, diag::err_omp_atomic_capture_not_expression_statement)
            << ErrorRange;
        Diag(NoteLoc, diag::note_omp_atomic_capture) << ErrorFound << NoteRange;
        return StmtError();
      }
      if (SemaRef.CurContext->isDependentContext())
        UE = V = E = X = nullptr;
    } else {
      // If clause is a capture:
      //  { v = x; x = expr; }
      //  { v = x; x++; }
      //  { v = x; x--; }
      //  { v = x; ++x; }
      //  { v = x; --x; }
      //  { v = x; x binop= expr; }
      //  { v = x; x = x binop expr; }
      //  { v = x; x = expr binop x; }
      //  { x++; v = x; }
      //  { x--; v = x; }
      //  { ++x; v = x; }
      //  { --x; v = x; }
      //  { x binop= expr; v = x; }
      //  { x = x binop expr; v = x; }
      //  { x = expr binop x; v = x; }
      if (auto *CS = dyn_cast<CompoundStmt>(Body)) {
        // Check that this is { expr1; expr2; }
        if (CS->size() == 2) {
          Stmt *First = CS->body_front();
          Stmt *Second = CS->body_back();
          if (auto *EWC = dyn_cast<ExprWithCleanups>(First))
            First = EWC->getSubExpr()->IgnoreParenImpCasts();
          if (auto *EWC = dyn_cast<ExprWithCleanups>(Second))
            Second = EWC->getSubExpr()->IgnoreParenImpCasts();
          // Need to find what subexpression is 'v' and what is 'x'.
          OpenMPAtomicUpdateChecker Checker(SemaRef);
          bool IsUpdateExprFound = !Checker.checkStatement(Second);
          BinaryOperator *BinOp = nullptr;
          if (IsUpdateExprFound) {
            BinOp = dyn_cast<BinaryOperator>(First);
            IsUpdateExprFound = BinOp && BinOp->getOpcode() == BO_Assign;
          }
          if (IsUpdateExprFound && !SemaRef.CurContext->isDependentContext()) {
            //  { v = x; x++; }
            //  { v = x; x--; }
            //  { v = x; ++x; }
            //  { v = x; --x; }
            //  { v = x; x binop= expr; }
            //  { v = x; x = x binop expr; }
            //  { v = x; x = expr binop x; }
            // Check that the first expression has form v = x.
            Expr *PossibleX = BinOp->getRHS()->IgnoreParenImpCasts();
            llvm::FoldingSetNodeID XId, PossibleXId;
            Checker.getX()->Profile(XId, Context, /*Canonical=*/true);
            PossibleX->Profile(PossibleXId, Context, /*Canonical=*/true);
            IsUpdateExprFound = XId == PossibleXId;
            if (IsUpdateExprFound) {
              V = BinOp->getLHS();
              X = Checker.getX();
              E = Checker.getExpr();
              UE = Checker.getUpdateExpr();
              IsXLHSInRHSPart = Checker.isXLHSInRHSPart();
              IsPostfixUpdate = true;
            }
          }
          if (!IsUpdateExprFound) {
            IsUpdateExprFound = !Checker.checkStatement(First);
            BinOp = nullptr;
            if (IsUpdateExprFound) {
              BinOp = dyn_cast<BinaryOperator>(Second);
              IsUpdateExprFound = BinOp && BinOp->getOpcode() == BO_Assign;
            }
            if (IsUpdateExprFound &&
                !SemaRef.CurContext->isDependentContext()) {
              //  { x++; v = x; }
              //  { x--; v = x; }
              //  { ++x; v = x; }
              //  { --x; v = x; }
              //  { x binop= expr; v = x; }
              //  { x = x binop expr; v = x; }
              //  { x = expr binop x; v = x; }
              // Check that the second expression has form v = x.
              Expr *PossibleX = BinOp->getRHS()->IgnoreParenImpCasts();
              llvm::FoldingSetNodeID XId, PossibleXId;
              Checker.getX()->Profile(XId, Context, /*Canonical=*/true);
              PossibleX->Profile(PossibleXId, Context, /*Canonical=*/true);
              IsUpdateExprFound = XId == PossibleXId;
              if (IsUpdateExprFound) {
                V = BinOp->getLHS();
                X = Checker.getX();
                E = Checker.getExpr();
                UE = Checker.getUpdateExpr();
                IsXLHSInRHSPart = Checker.isXLHSInRHSPart();
                IsPostfixUpdate = false;
              }
            }
          }
          if (!IsUpdateExprFound) {
            //  { v = x; x = expr; }
            auto *FirstExpr = dyn_cast<Expr>(First);
            auto *SecondExpr = dyn_cast<Expr>(Second);
            if (!FirstExpr || !SecondExpr ||
                !(FirstExpr->isInstantiationDependent() ||
                  SecondExpr->isInstantiationDependent())) {
              auto *FirstBinOp = dyn_cast<BinaryOperator>(First);
              if (!FirstBinOp || FirstBinOp->getOpcode() != BO_Assign) {
                ErrorFound = NotAnAssignmentOp;
                NoteLoc = ErrorLoc = FirstBinOp ? FirstBinOp->getOperatorLoc()
                                                : First->getBeginLoc();
                NoteRange = ErrorRange = FirstBinOp
                                             ? FirstBinOp->getSourceRange()
                                             : SourceRange(ErrorLoc, ErrorLoc);
              } else {
                auto *SecondBinOp = dyn_cast<BinaryOperator>(Second);
                if (!SecondBinOp || SecondBinOp->getOpcode() != BO_Assign) {
                  ErrorFound = NotAnAssignmentOp;
                  NoteLoc = ErrorLoc = SecondBinOp
                                           ? SecondBinOp->getOperatorLoc()
                                           : Second->getBeginLoc();
                  NoteRange = ErrorRange =
                      SecondBinOp ? SecondBinOp->getSourceRange()
                                  : SourceRange(ErrorLoc, ErrorLoc);
                } else {
                  Expr *PossibleXRHSInFirst =
                      FirstBinOp->getRHS()->IgnoreParenImpCasts();
                  Expr *PossibleXLHSInSecond =
                      SecondBinOp->getLHS()->IgnoreParenImpCasts();
                  llvm::FoldingSetNodeID X1Id, X2Id;
                  PossibleXRHSInFirst->Profile(X1Id, Context,
                                               /*Canonical=*/true);
                  PossibleXLHSInSecond->Profile(X2Id, Context,
                                                /*Canonical=*/true);
                  IsUpdateExprFound = X1Id == X2Id;
                  if (IsUpdateExprFound) {
                    V = FirstBinOp->getLHS();
                    X = SecondBinOp->getLHS();
                    E = SecondBinOp->getRHS();
                    UE = nullptr;
                    IsXLHSInRHSPart = false;
                    IsPostfixUpdate = true;
                  } else {
                    ErrorFound = NotASpecificExpression;
                    ErrorLoc = FirstBinOp->getExprLoc();
                    ErrorRange = FirstBinOp->getSourceRange();
                    NoteLoc = SecondBinOp->getLHS()->getExprLoc();
                    NoteRange = SecondBinOp->getRHS()->getSourceRange();
                  }
                }
              }
            }
          }
        } else {
          NoteLoc = ErrorLoc = Body->getBeginLoc();
          NoteRange = ErrorRange =
              SourceRange(Body->getBeginLoc(), Body->getBeginLoc());
          ErrorFound = NotTwoSubstatements;
        }
      } else {
        NoteLoc = ErrorLoc = Body->getBeginLoc();
        NoteRange = ErrorRange =
            SourceRange(Body->getBeginLoc(), Body->getBeginLoc());
        ErrorFound = NotACompoundStatement;
      }
    }
    if (ErrorFound != NoError) {
      Diag(ErrorLoc, diag::err_omp_atomic_capture_not_compound_statement)
          << ErrorRange;
      Diag(NoteLoc, diag::note_omp_atomic_capture) << ErrorFound << NoteRange;
      return StmtError();
    }
    if (SemaRef.CurContext->isDependentContext())
      UE = V = E = X = nullptr;
  } else if (AtomicKind == OMPC_compare) {
    if (IsCompareCapture) {
      OpenMPAtomicCompareCaptureChecker::ErrorInfoTy ErrorInfo;
      OpenMPAtomicCompareCaptureChecker Checker(SemaRef);
      if (!Checker.checkStmt(Body, ErrorInfo)) {
        Diag(ErrorInfo.ErrorLoc, diag::err_omp_atomic_compare_capture)
            << ErrorInfo.ErrorRange;
        Diag(ErrorInfo.NoteLoc, diag::note_omp_atomic_compare)
            << ErrorInfo.Error << ErrorInfo.NoteRange;
        return StmtError();
      }
      X = Checker.getX();
      E = Checker.getE();
      D = Checker.getD();
      CE = Checker.getCond();
      V = Checker.getV();
      R = Checker.getR();
      // We reuse IsXLHSInRHSPart to tell if it is in the form 'x ordop expr'.
      IsXLHSInRHSPart = Checker.isXBinopExpr();
      IsFailOnly = Checker.isFailOnly();
      IsPostfixUpdate = Checker.isPostfixUpdate();
    } else {
      OpenMPAtomicCompareChecker::ErrorInfoTy ErrorInfo;
      OpenMPAtomicCompareChecker Checker(SemaRef);
      if (!Checker.checkStmt(Body, ErrorInfo)) {
        Diag(ErrorInfo.ErrorLoc, diag::err_omp_atomic_compare)
            << ErrorInfo.ErrorRange;
        Diag(ErrorInfo.NoteLoc, diag::note_omp_atomic_compare)
            << ErrorInfo.Error << ErrorInfo.NoteRange;
        return StmtError();
      }
      X = Checker.getX();
      E = Checker.getE();
      D = Checker.getD();
      CE = Checker.getCond();
      // The weak clause may only appear if the resulting atomic operation is
      // an atomic conditional update for which the comparison tests for
      // equality. It was not possible to do this check in
      // OpenMPAtomicCompareChecker::checkStmt() as the check for OMPC_weak
      // could not be performed (Clauses are not available).
      auto *It = find_if(Clauses, [](OMPClause *C) {
        return C->getClauseKind() == llvm::omp::Clause::OMPC_weak;
      });
      if (It != Clauses.end()) {
        auto *Cond = dyn_cast<BinaryOperator>(CE);
        if (Cond->getOpcode() != BO_EQ) {
          ErrorInfo.Error = Checker.ErrorTy::NotAnAssignment;
          ErrorInfo.ErrorLoc = Cond->getExprLoc();
          ErrorInfo.NoteLoc = Cond->getOperatorLoc();
          ErrorInfo.ErrorRange = ErrorInfo.NoteRange = Cond->getSourceRange();

          Diag(ErrorInfo.ErrorLoc, diag::err_omp_atomic_weak_no_equality)
              << ErrorInfo.ErrorRange;
          return StmtError();
        }
      }
      // We reuse IsXLHSInRHSPart to tell if it is in the form 'x ordop expr'.
      IsXLHSInRHSPart = Checker.isXBinopExpr();
    }
  }

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPAtomicDirective::Create(
      Context, StartLoc, EndLoc, Clauses, AStmt,
      {X, V, R, E, UE, D, CE, IsXLHSInRHSPart, IsPostfixUpdate, IsFailOnly});
}

StmtResult SemaOpenMP::ActOnOpenMPTargetDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(SemaRef, OMPD_target, AStmt);

  // OpenMP [2.16, Nesting of Regions]
  // If specified, a teams construct must be contained within a target
  // construct. That target construct must contain no statements or directives
  // outside of the teams construct.
  if (DSAStack->hasInnerTeamsRegion()) {
    const Stmt *S = CS->IgnoreContainers(/*IgnoreCaptured=*/true);
    bool OMPTeamsFound = true;
    if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
      auto I = CS->body_begin();
      while (I != CS->body_end()) {
        const auto *OED = dyn_cast<OMPExecutableDirective>(*I);
        bool IsTeams = OED && isOpenMPTeamsDirective(OED->getDirectiveKind());
        if (!IsTeams || I != CS->body_begin()) {
          OMPTeamsFound = false;
          if (IsTeams && I != CS->body_begin()) {
            // This is the two teams case. Since the InnerTeamsRegionLoc will
            // point to this second one reset the iterator to the other teams.
            --I;
          }
          break;
        }
        ++I;
      }
      assert(I != CS->body_end() && "Not found statement");
      S = *I;
    } else {
      const auto *OED = dyn_cast<OMPExecutableDirective>(S);
      OMPTeamsFound = OED && isOpenMPTeamsDirective(OED->getDirectiveKind());
    }
    if (!OMPTeamsFound) {
      Diag(StartLoc, diag::err_omp_target_contains_not_only_teams);
      Diag(DSAStack->getInnerTeamsRegionLoc(),
           diag::note_omp_nested_teams_construct_here);
      Diag(S->getBeginLoc(), diag::note_omp_nested_statement_here)
          << isa<OMPExecutableDirective>(S);
      return StmtError();
    }
  }

  return OMPTargetDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                    AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetParallelDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_target_parallel, AStmt);

  return OMPTargetParallelDirective::Create(
      getASTContext(), StartLoc, EndLoc, Clauses, AStmt,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPTargetParallelForDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_target_parallel_for, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_target_parallel_for, getCollapseNumberExpr(Clauses),
                      getOrderedNumberExpr(Clauses), CS, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  return OMPTargetParallelForDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

/// Check for existence of a map clause in the list of clauses.
static bool hasClauses(ArrayRef<OMPClause *> Clauses,
                       const OpenMPClauseKind K) {
  return llvm::any_of(
      Clauses, [K](const OMPClause *C) { return C->getClauseKind() == K; });
}

template <typename... Params>
static bool hasClauses(ArrayRef<OMPClause *> Clauses, const OpenMPClauseKind K,
                       const Params... ClauseTypes) {
  return hasClauses(Clauses, K) || hasClauses(Clauses, ClauseTypes...);
}

/// Check if the variables in the mapping clause are externally visible.
static bool isClauseMappable(ArrayRef<OMPClause *> Clauses) {
  for (const OMPClause *C : Clauses) {
    if (auto *TC = dyn_cast<OMPToClause>(C))
      return llvm::all_of(TC->all_decls(), [](ValueDecl *VD) {
        return !VD || !VD->hasAttr<OMPDeclareTargetDeclAttr>() ||
               (VD->isExternallyVisible() &&
                VD->getVisibility() != HiddenVisibility);
      });
    else if (auto *FC = dyn_cast<OMPFromClause>(C))
      return llvm::all_of(FC->all_decls(), [](ValueDecl *VD) {
        return !VD || !VD->hasAttr<OMPDeclareTargetDeclAttr>() ||
               (VD->isExternallyVisible() &&
                VD->getVisibility() != HiddenVisibility);
      });
  }

  return true;
}

StmtResult
SemaOpenMP::ActOnOpenMPTargetDataDirective(ArrayRef<OMPClause *> Clauses,
                                           Stmt *AStmt, SourceLocation StartLoc,
                                           SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");

  // OpenMP [2.12.2, target data Construct, Restrictions]
  // At least one map, use_device_addr or use_device_ptr clause must appear on
  // the directive.
  if (!hasClauses(Clauses, OMPC_map, OMPC_use_device_ptr) &&
      (getLangOpts().OpenMP < 50 ||
       !hasClauses(Clauses, OMPC_use_device_addr))) {
    StringRef Expected;
    if (getLangOpts().OpenMP < 50)
      Expected = "'map' or 'use_device_ptr'";
    else
      Expected = "'map', 'use_device_ptr', or 'use_device_addr'";
    Diag(StartLoc, diag::err_omp_no_clause_for_directive)
        << Expected << getOpenMPDirectiveName(OMPD_target_data);
    return StmtError();
  }

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPTargetDataDirective::Create(getASTContext(), StartLoc, EndLoc,
                                        Clauses, AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetEnterDataDirective(
    ArrayRef<OMPClause *> Clauses, SourceLocation StartLoc,
    SourceLocation EndLoc, Stmt *AStmt) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_target_enter_data, AStmt);

  // OpenMP [2.10.2, Restrictions, p. 99]
  // At least one map clause must appear on the directive.
  if (!hasClauses(Clauses, OMPC_map)) {
    Diag(StartLoc, diag::err_omp_no_clause_for_directive)
        << "'map'" << getOpenMPDirectiveName(OMPD_target_enter_data);
    return StmtError();
  }

  return OMPTargetEnterDataDirective::Create(getASTContext(), StartLoc, EndLoc,
                                             Clauses, AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetExitDataDirective(
    ArrayRef<OMPClause *> Clauses, SourceLocation StartLoc,
    SourceLocation EndLoc, Stmt *AStmt) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_target_exit_data, AStmt);

  // OpenMP [2.10.3, Restrictions, p. 102]
  // At least one map clause must appear on the directive.
  if (!hasClauses(Clauses, OMPC_map)) {
    Diag(StartLoc, diag::err_omp_no_clause_for_directive)
        << "'map'" << getOpenMPDirectiveName(OMPD_target_exit_data);
    return StmtError();
  }

  return OMPTargetExitDataDirective::Create(getASTContext(), StartLoc, EndLoc,
                                            Clauses, AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetUpdateDirective(
    ArrayRef<OMPClause *> Clauses, SourceLocation StartLoc,
    SourceLocation EndLoc, Stmt *AStmt) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_target_update, AStmt);

  if (!hasClauses(Clauses, OMPC_to, OMPC_from)) {
    Diag(StartLoc, diag::err_omp_at_least_one_motion_clause_required);
    return StmtError();
  }

  if (!isClauseMappable(Clauses)) {
    Diag(StartLoc, diag::err_omp_cannot_update_with_internal_linkage);
    return StmtError();
  }

  return OMPTargetUpdateDirective::Create(getASTContext(), StartLoc, EndLoc,
                                          Clauses, AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPTeamsDirective(ArrayRef<OMPClause *> Clauses,
                                                 Stmt *AStmt,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  // Report affected OpenMP target offloading behavior when in HIP lang-mode.
  if (getLangOpts().HIP && (DSAStack->getParentDirective() == OMPD_target))
    Diag(StartLoc, diag::warn_hip_omp_target_directives);

  setBranchProtectedScope(SemaRef, OMPD_teams, AStmt);

  DSAStack->setParentTeamsRegionLoc(StartLoc);

  return OMPTeamsDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                   AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPCancellationPointDirective(
    SourceLocation StartLoc, SourceLocation EndLoc,
    OpenMPDirectiveKind CancelRegion) {
  if (DSAStack->isParentNowaitRegion()) {
    Diag(StartLoc, diag::err_omp_parent_cancel_region_nowait) << 0;
    return StmtError();
  }
  if (DSAStack->isParentOrderedRegion()) {
    Diag(StartLoc, diag::err_omp_parent_cancel_region_ordered) << 0;
    return StmtError();
  }
  return OMPCancellationPointDirective::Create(getASTContext(), StartLoc,
                                               EndLoc, CancelRegion);
}

StmtResult SemaOpenMP::ActOnOpenMPCancelDirective(
    ArrayRef<OMPClause *> Clauses, SourceLocation StartLoc,
    SourceLocation EndLoc, OpenMPDirectiveKind CancelRegion) {
  if (DSAStack->isParentNowaitRegion()) {
    Diag(StartLoc, diag::err_omp_parent_cancel_region_nowait) << 1;
    return StmtError();
  }
  if (DSAStack->isParentOrderedRegion()) {
    Diag(StartLoc, diag::err_omp_parent_cancel_region_ordered) << 1;
    return StmtError();
  }
  DSAStack->setParentCancelRegion(/*Cancel=*/true);
  return OMPCancelDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                    CancelRegion);
}

static bool checkReductionClauseWithNogroup(Sema &S,
                                            ArrayRef<OMPClause *> Clauses) {
  const OMPClause *ReductionClause = nullptr;
  const OMPClause *NogroupClause = nullptr;
  for (const OMPClause *C : Clauses) {
    if (C->getClauseKind() == OMPC_reduction) {
      ReductionClause = C;
      if (NogroupClause)
        break;
      continue;
    }
    if (C->getClauseKind() == OMPC_nogroup) {
      NogroupClause = C;
      if (ReductionClause)
        break;
      continue;
    }
  }
  if (ReductionClause && NogroupClause) {
    S.Diag(ReductionClause->getBeginLoc(), diag::err_omp_reduction_with_nogroup)
        << SourceRange(NogroupClause->getBeginLoc(),
                       NogroupClause->getEndLoc());
    return true;
  }
  return false;
}

StmtResult SemaOpenMP::ActOnOpenMPTaskLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_taskloop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, AStmt, SemaRef,
                      *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPTaskLoopDirective::Create(getASTContext(), StartLoc, EndLoc,
                                      NestedLoopCount, Clauses, AStmt, B,
                                      DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPTaskLoopSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_taskloop_simd, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, AStmt, SemaRef,
                      *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();
  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPTaskLoopSimdDirective::Create(getASTContext(), StartLoc, EndLoc,
                                          NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPMasterTaskLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_master_taskloop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, AStmt, SemaRef,
                      *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPMasterTaskLoopDirective::Create(getASTContext(), StartLoc, EndLoc,
                                            NestedLoopCount, Clauses, AStmt, B,
                                            DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPMaskedTaskLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_masked_taskloop, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, AStmt, SemaRef,
                      *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPMaskedTaskLoopDirective::Create(getASTContext(), StartLoc, EndLoc,
                                            NestedLoopCount, Clauses, AStmt, B,
                                            DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPMasterTaskLoopSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_master_taskloop_simd, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, AStmt, SemaRef,
                      *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();
  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPMasterTaskLoopSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPMaskedTaskLoopSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_masked_taskloop_simd, getCollapseNumberExpr(Clauses),
                      /*OrderedLoopCountExpr=*/nullptr, AStmt, SemaRef,
                      *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();
  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();
  return OMPMaskedTaskLoopSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPParallelMasterTaskLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_parallel_master_taskloop, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_parallel_master_taskloop, getCollapseNumberExpr(Clauses),
      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();

  return OMPParallelMasterTaskLoopDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPParallelMaskedTaskLoopDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_parallel_masked_taskloop, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_parallel_masked_taskloop, getCollapseNumberExpr(Clauses),
      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();

  return OMPParallelMaskedTaskLoopDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPParallelMasterTaskLoopSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_parallel_master_taskloop_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_parallel_master_taskloop_simd, getCollapseNumberExpr(Clauses),
      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();
  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPParallelMasterTaskLoopSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPParallelMaskedTaskLoopSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_parallel_masked_taskloop_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_parallel_masked_taskloop_simd, getCollapseNumberExpr(Clauses),
      /*OrderedLoopCountExpr=*/nullptr, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // The grainsize clause and num_tasks clause are mutually exclusive and may
  // not appear on the same taskloop directive.
  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_grainsize, OMPC_num_tasks}))
    return StmtError();
  // OpenMP, [2.9.2 taskloop Construct, Restrictions]
  // If a reduction clause is present on the taskloop directive, the nogroup
  // clause must not be specified.
  if (checkReductionClauseWithNogroup(SemaRef, Clauses))
    return StmtError();
  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPParallelMaskedTaskLoopSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPDistributeDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  if (!checkLastPrivateForMappedDirectives(Clauses))
    return StmtError();

  assert(isa<CapturedStmt>(AStmt) && "Captured statement expected");
  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_distribute, getCollapseNumberExpr(Clauses),
                      nullptr /*ordered not a clause on distribute*/, AStmt,
                      SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  SemaRef.setFunctionHasBranchProtectedScope();
  auto *DistributeDirective = OMPDistributeDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getMappedDirective());
  return DistributeDirective;
}

StmtResult SemaOpenMP::ActOnOpenMPDistributeParallelForDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_distribute_parallel_for, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_distribute_parallel_for, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  return OMPDistributeParallelForDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPDistributeParallelForSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_distribute_parallel_for_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_distribute_parallel_for_simd, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPDistributeParallelForSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPDistributeSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_distribute_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_distribute_simd, getCollapseNumberExpr(Clauses),
                      nullptr /*ordered not a clause on distribute*/, CS,
                      SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPDistributeSimdDirective::Create(getASTContext(), StartLoc, EndLoc,
                                            NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetParallelForSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_target_parallel_for_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' or 'ordered' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_target_parallel_for_simd, getCollapseNumberExpr(Clauses),
      getOrderedNumberExpr(Clauses), CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPTargetParallelForSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(SemaRef, OMPD_target_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will define the
  // nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_target_simd, getCollapseNumberExpr(Clauses),
                      getOrderedNumberExpr(Clauses), CS, SemaRef, *DSAStack,
                      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPTargetSimdDirective::Create(getASTContext(), StartLoc, EndLoc,
                                        NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTeamsDistributeDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_teams_distribute, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_teams_distribute, getCollapseNumberExpr(Clauses),
                      nullptr /*ordered not a clause on distribute*/, CS,
                      SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp teams distribute loop exprs were not built");

  DSAStack->setParentTeamsRegionLoc(StartLoc);

  return OMPTeamsDistributeDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTeamsDistributeSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_teams_distribute_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_teams_distribute_simd, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  DSAStack->setParentTeamsRegionLoc(StartLoc);

  return OMPTeamsDistributeSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTeamsDistributeParallelForSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_teams_distribute_parallel_for_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_teams_distribute_parallel_for_simd, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  DSAStack->setParentTeamsRegionLoc(StartLoc);

  return OMPTeamsDistributeParallelForSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTeamsDistributeParallelForDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_teams_distribute_parallel_for, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_teams_distribute_parallel_for, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);

  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp for loop exprs were not built");

  DSAStack->setParentTeamsRegionLoc(StartLoc);

  return OMPTeamsDistributeParallelForDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPTargetTeamsDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  setBranchProtectedScope(SemaRef, OMPD_target_teams, AStmt);

  const OMPClause *BareClause = nullptr;
  bool HasThreadLimitAndNumTeamsClause = hasClauses(Clauses, OMPC_num_teams) &&
                                         hasClauses(Clauses, OMPC_thread_limit);
  bool HasBareClause = llvm::any_of(Clauses, [&](const OMPClause *C) {
    BareClause = C;
    return C->getClauseKind() == OMPC_ompx_bare;
  });

  if (HasBareClause && !HasThreadLimitAndNumTeamsClause) {
    Diag(BareClause->getBeginLoc(), diag::err_ompx_bare_no_grid);
    return StmtError();
  }

  return OMPTargetTeamsDirective::Create(getASTContext(), StartLoc, EndLoc,
                                         Clauses, AStmt);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetTeamsDistributeDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS =
      setBranchProtectedScope(SemaRef, OMPD_target_teams_distribute, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_target_teams_distribute, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  assert((SemaRef.CurContext->isDependentContext() || B.builtAll()) &&
         "omp target teams distribute loop exprs were not built");

  return OMPTargetTeamsDistributeDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetTeamsDistributeParallelForDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_target_teams_distribute_parallel_for, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_target_teams_distribute_parallel_for, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  return OMPTargetTeamsDistributeParallelForDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B,
      DSAStack->getTaskgroupReductionRef(), DSAStack->isCancelRegion());
}

StmtResult SemaOpenMP::ActOnOpenMPTargetTeamsDistributeParallelForSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_target_teams_distribute_parallel_for_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount =
      checkOpenMPLoop(OMPD_target_teams_distribute_parallel_for_simd,
                      getCollapseNumberExpr(Clauses),
                      nullptr /*ordered not a clause on distribute*/, CS,
                      SemaRef, *DSAStack, VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPTargetTeamsDistributeParallelForSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

StmtResult SemaOpenMP::ActOnOpenMPTargetTeamsDistributeSimdDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA) {
  if (!AStmt)
    return StmtError();

  CapturedStmt *CS = setBranchProtectedScope(
      SemaRef, OMPD_target_teams_distribute_simd, AStmt);

  OMPLoopBasedDirective::HelperExprs B;
  // In presence of clause 'collapse' with number of loops, it will
  // define the nested loops number.
  unsigned NestedLoopCount = checkOpenMPLoop(
      OMPD_target_teams_distribute_simd, getCollapseNumberExpr(Clauses),
      nullptr /*ordered not a clause on distribute*/, CS, SemaRef, *DSAStack,
      VarsWithImplicitDSA, B);
  if (NestedLoopCount == 0)
    return StmtError();

  if (finishLinearClauses(SemaRef, Clauses, B, DSAStack))
    return StmtError();

  if (checkSimdlenSafelenSpecified(SemaRef, Clauses))
    return StmtError();

  return OMPTargetTeamsDistributeSimdDirective::Create(
      getASTContext(), StartLoc, EndLoc, NestedLoopCount, Clauses, AStmt, B);
}

bool SemaOpenMP::checkTransformableLoopNest(
    OpenMPDirectiveKind Kind, Stmt *AStmt, int NumLoops,
    SmallVectorImpl<OMPLoopBasedDirective::HelperExprs> &LoopHelpers,
    Stmt *&Body, SmallVectorImpl<SmallVector<Stmt *, 0>> &OriginalInits) {
  OriginalInits.emplace_back();
  bool Result = OMPLoopBasedDirective::doForAllLoops(
      AStmt->IgnoreContainers(), /*TryImperfectlyNestedLoops=*/false, NumLoops,
      [this, &LoopHelpers, &Body, &OriginalInits, Kind](unsigned Cnt,
                                                        Stmt *CurStmt) {
        VarsWithInheritedDSAType TmpDSA;
        unsigned SingleNumLoops =
            checkOpenMPLoop(Kind, nullptr, nullptr, CurStmt, SemaRef, *DSAStack,
                            TmpDSA, LoopHelpers[Cnt]);
        if (SingleNumLoops == 0)
          return true;
        assert(SingleNumLoops == 1 && "Expect single loop iteration space");
        if (auto *For = dyn_cast<ForStmt>(CurStmt)) {
          OriginalInits.back().push_back(For->getInit());
          Body = For->getBody();
        } else {
          assert(isa<CXXForRangeStmt>(CurStmt) &&
                 "Expected canonical for or range-based for loops.");
          auto *CXXFor = cast<CXXForRangeStmt>(CurStmt);
          OriginalInits.back().push_back(CXXFor->getBeginStmt());
          Body = CXXFor->getBody();
        }
        OriginalInits.emplace_back();
        return false;
      },
      [&OriginalInits](OMPLoopBasedDirective *Transform) {
        Stmt *DependentPreInits;
        if (auto *Dir = dyn_cast<OMPTileDirective>(Transform))
          DependentPreInits = Dir->getPreInits();
        else if (auto *Dir = dyn_cast<OMPUnrollDirective>(Transform))
          DependentPreInits = Dir->getPreInits();
        else if (auto *Dir = dyn_cast<OMPReverseDirective>(Transform))
          DependentPreInits = Dir->getPreInits();
        else if (auto *Dir = dyn_cast<OMPInterchangeDirective>(Transform))
          DependentPreInits = Dir->getPreInits();
        else
          llvm_unreachable("Unhandled loop transformation");

        appendFlattenedStmtList(OriginalInits.back(), DependentPreInits);
      });
  assert(OriginalInits.back().empty() && "No preinit after innermost loop");
  OriginalInits.pop_back();
  return Result;
}

/// Add preinit statements that need to be propageted from the selected loop.
static void addLoopPreInits(ASTContext &Context,
                            OMPLoopBasedDirective::HelperExprs &LoopHelper,
                            Stmt *LoopStmt, ArrayRef<Stmt *> OriginalInit,
                            SmallVectorImpl<Stmt *> &PreInits) {

  // For range-based for-statements, ensure that their syntactic sugar is
  // executed by adding them as pre-init statements.
  if (auto *CXXRangeFor = dyn_cast<CXXForRangeStmt>(LoopStmt)) {
    Stmt *RangeInit = CXXRangeFor->getInit();
    if (RangeInit)
      PreInits.push_back(RangeInit);

    DeclStmt *RangeStmt = CXXRangeFor->getRangeStmt();
    PreInits.push_back(new (Context) DeclStmt(RangeStmt->getDeclGroup(),
                                              RangeStmt->getBeginLoc(),
                                              RangeStmt->getEndLoc()));

    DeclStmt *RangeEnd = CXXRangeFor->getEndStmt();
    PreInits.push_back(new (Context) DeclStmt(RangeEnd->getDeclGroup(),
                                              RangeEnd->getBeginLoc(),
                                              RangeEnd->getEndLoc()));
  }

  llvm::append_range(PreInits, OriginalInit);

  // List of OMPCapturedExprDecl, for __begin, __end, and NumIterations
  if (auto *PI = cast_or_null<DeclStmt>(LoopHelper.PreInits)) {
    PreInits.push_back(new (Context) DeclStmt(
        PI->getDeclGroup(), PI->getBeginLoc(), PI->getEndLoc()));
  }

  // Gather declarations for the data members used as counters.
  for (Expr *CounterRef : LoopHelper.Counters) {
    auto *CounterDecl = cast<DeclRefExpr>(CounterRef)->getDecl();
    if (isa<OMPCapturedExprDecl>(CounterDecl))
      PreInits.push_back(new (Context) DeclStmt(
          DeclGroupRef(CounterDecl), SourceLocation(), SourceLocation()));
  }
}

/// Collect the loop statements (ForStmt or CXXRangeForStmt) of the affected
/// loop of a construct.
static void collectLoopStmts(Stmt *AStmt, MutableArrayRef<Stmt *> LoopStmts) {
  size_t NumLoops = LoopStmts.size();
  OMPLoopBasedDirective::doForAllLoops(
      AStmt, /*TryImperfectlyNestedLoops=*/false, NumLoops,
      [LoopStmts](unsigned Cnt, Stmt *CurStmt) {
        assert(!LoopStmts[Cnt] && "Loop statement must not yet be assigned");
        LoopStmts[Cnt] = CurStmt;
        return false;
      });
  assert(!is_contained(LoopStmts, nullptr) &&
         "Expecting a loop statement for each affected loop");
}

StmtResult SemaOpenMP::ActOnOpenMPTileDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  ASTContext &Context = getASTContext();
  Scope *CurScope = SemaRef.getCurScope();

  const auto *SizesClause =
      OMPExecutableDirective::getSingleClause<OMPSizesClause>(Clauses);
  if (!SizesClause ||
      llvm::any_of(SizesClause->getSizesRefs(), [](Expr *E) { return !E; }))
    return StmtError();
  unsigned NumLoops = SizesClause->getNumSizes();

  // Empty statement should only be possible if there already was an error.
  if (!AStmt)
    return StmtError();

  // Verify and diagnose loop nest.
  SmallVector<OMPLoopBasedDirective::HelperExprs, 4> LoopHelpers(NumLoops);
  Stmt *Body = nullptr;
  SmallVector<SmallVector<Stmt *, 0>, 4> OriginalInits;
  if (!checkTransformableLoopNest(OMPD_tile, AStmt, NumLoops, LoopHelpers, Body,
                                  OriginalInits))
    return StmtError();

  // Delay tiling to when template is completely instantiated.
  if (SemaRef.CurContext->isDependentContext())
    return OMPTileDirective::Create(Context, StartLoc, EndLoc, Clauses,
                                    NumLoops, AStmt, nullptr, nullptr);

  assert(LoopHelpers.size() == NumLoops &&
         "Expecting loop iteration space dimensionality to match number of "
         "affected loops");
  assert(OriginalInits.size() == NumLoops &&
         "Expecting loop iteration space dimensionality to match number of "
         "affected loops");

  // Collect all affected loop statements.
  SmallVector<Stmt *> LoopStmts(NumLoops, nullptr);
  collectLoopStmts(AStmt, LoopStmts);

  SmallVector<Stmt *, 4> PreInits;
  CaptureVars CopyTransformer(SemaRef);

  // Create iteration variables for the generated loops.
  SmallVector<VarDecl *, 4> FloorIndVars;
  SmallVector<VarDecl *, 4> TileIndVars;
  FloorIndVars.resize(NumLoops);
  TileIndVars.resize(NumLoops);
  for (unsigned I = 0; I < NumLoops; ++I) {
    OMPLoopBasedDirective::HelperExprs &LoopHelper = LoopHelpers[I];

    assert(LoopHelper.Counters.size() == 1 &&
           "Expect single-dimensional loop iteration space");
    auto *OrigCntVar = cast<DeclRefExpr>(LoopHelper.Counters.front());
    std::string OrigVarName = OrigCntVar->getNameInfo().getAsString();
    DeclRefExpr *IterVarRef = cast<DeclRefExpr>(LoopHelper.IterationVarRef);
    QualType CntTy = IterVarRef->getType();

    // Iteration variable for the floor (i.e. outer) loop.
    {
      std::string FloorCntName =
          (Twine(".floor_") + llvm::utostr(I) + ".iv." + OrigVarName).str();
      VarDecl *FloorCntDecl =
          buildVarDecl(SemaRef, {}, CntTy, FloorCntName, nullptr, OrigCntVar);
      FloorIndVars[I] = FloorCntDecl;
    }

    // Iteration variable for the tile (i.e. inner) loop.
    {
      std::string TileCntName =
          (Twine(".tile_") + llvm::utostr(I) + ".iv." + OrigVarName).str();

      // Reuse the iteration variable created by checkOpenMPLoop. It is also
      // used by the expressions to derive the original iteration variable's
      // value from the logical iteration number.
      auto *TileCntDecl = cast<VarDecl>(IterVarRef->getDecl());
      TileCntDecl->setDeclName(
          &SemaRef.PP.getIdentifierTable().get(TileCntName));
      TileIndVars[I] = TileCntDecl;
    }

    addLoopPreInits(Context, LoopHelper, LoopStmts[I], OriginalInits[I],
                    PreInits);
  }

  // Once the original iteration values are set, append the innermost body.
  Stmt *Inner = Body;

  auto MakeDimTileSize = [&SemaRef = this->SemaRef, &CopyTransformer, &Context,
                          SizesClause, CurScope](int I) -> Expr * {
    Expr *DimTileSizeExpr = SizesClause->getSizesRefs()[I];
    if (isa<ConstantExpr>(DimTileSizeExpr))
      return AssertSuccess(CopyTransformer.TransformExpr(DimTileSizeExpr));

    // When the tile size is not a constant but a variable, it is possible to
    // pass non-positive numbers. For instance:
    // \code{c}
    //   int a = 0;
    //   #pragma omp tile sizes(a)
    //   for (int i = 0; i < 42; ++i)
    //     body(i);
    // \endcode
    // Although there is no meaningful interpretation of the tile size, the body
    // should still be executed 42 times to avoid surprises. To preserve the
    // invariant that every loop iteration is executed exactly once and not
    // cause an infinite loop, apply a minimum tile size of one.
    // Build expr:
    // \code{c}
    //   (TS <= 0) ? 1 : TS
    // \endcode
    QualType DimTy = DimTileSizeExpr->getType();
    uint64_t DimWidth = Context.getTypeSize(DimTy);
    IntegerLiteral *Zero = IntegerLiteral::Create(
        Context, llvm::APInt::getZero(DimWidth), DimTy, {});
    IntegerLiteral *One =
        IntegerLiteral::Create(Context, llvm::APInt(DimWidth, 1), DimTy, {});
    Expr *Cond = AssertSuccess(SemaRef.BuildBinOp(
        CurScope, {}, BO_LE,
        AssertSuccess(CopyTransformer.TransformExpr(DimTileSizeExpr)), Zero));
    Expr *MinOne = new (Context) ConditionalOperator(
        Cond, {}, One, {},
        AssertSuccess(CopyTransformer.TransformExpr(DimTileSizeExpr)), DimTy,
        VK_PRValue, OK_Ordinary);
    return MinOne;
  };

  // Create tile loops from the inside to the outside.
  for (int I = NumLoops - 1; I >= 0; --I) {
    OMPLoopBasedDirective::HelperExprs &LoopHelper = LoopHelpers[I];
    Expr *NumIterations = LoopHelper.NumIterations;
    auto *OrigCntVar = cast<DeclRefExpr>(LoopHelper.Counters[0]);
    QualType IVTy = NumIterations->getType();
    Stmt *LoopStmt = LoopStmts[I];

    // Commonly used variables. One of the constraints of an AST is that every
    // node object must appear at most once, hence we define lamdas that create
    // a new AST node at every use.
    auto MakeTileIVRef = [&SemaRef = this->SemaRef, &TileIndVars, I, IVTy,
                          OrigCntVar]() {
      return buildDeclRefExpr(SemaRef, TileIndVars[I], IVTy,
                              OrigCntVar->getExprLoc());
    };
    auto MakeFloorIVRef = [&SemaRef = this->SemaRef, &FloorIndVars, I, IVTy,
                           OrigCntVar]() {
      return buildDeclRefExpr(SemaRef, FloorIndVars[I], IVTy,
                              OrigCntVar->getExprLoc());
    };

    // For init-statement: auto .tile.iv = .floor.iv
    SemaRef.AddInitializerToDecl(
        TileIndVars[I], SemaRef.DefaultLvalueConversion(MakeFloorIVRef()).get(),
        /*DirectInit=*/false);
    Decl *CounterDecl = TileIndVars[I];
    StmtResult InitStmt = new (Context)
        DeclStmt(DeclGroupRef::Create(Context, &CounterDecl, 1),
                 OrigCntVar->getBeginLoc(), OrigCntVar->getEndLoc());
    if (!InitStmt.isUsable())
      return StmtError();

    // For cond-expression:
    //   .tile.iv < min(.floor.iv + DimTileSize, NumIterations)
    ExprResult EndOfTile =
        SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_Add,
                           MakeFloorIVRef(), MakeDimTileSize(I));
    if (!EndOfTile.isUsable())
      return StmtError();
    ExprResult IsPartialTile =
        SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                           NumIterations, EndOfTile.get());
    if (!IsPartialTile.isUsable())
      return StmtError();
    ExprResult MinTileAndIterSpace = SemaRef.ActOnConditionalOp(
        LoopHelper.Cond->getBeginLoc(), LoopHelper.Cond->getEndLoc(),
        IsPartialTile.get(), NumIterations, EndOfTile.get());
    if (!MinTileAndIterSpace.isUsable())
      return StmtError();
    ExprResult CondExpr =
        SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                           MakeTileIVRef(), MinTileAndIterSpace.get());
    if (!CondExpr.isUsable())
      return StmtError();

    // For incr-statement: ++.tile.iv
    ExprResult IncrStmt = SemaRef.BuildUnaryOp(
        CurScope, LoopHelper.Inc->getExprLoc(), UO_PreInc, MakeTileIVRef());
    if (!IncrStmt.isUsable())
      return StmtError();

    // Statements to set the original iteration variable's value from the
    // logical iteration number.
    // Generated for loop is:
    // \code
    // Original_for_init;
    // for (auto .tile.iv = .floor.iv;
    //      .tile.iv < min(.floor.iv + DimTileSize, NumIterations);
    //      ++.tile.iv) {
    //   Original_Body;
    //   Original_counter_update;
    // }
    // \endcode
    // FIXME: If the innermost body is an loop itself, inserting these
    // statements stops it being recognized  as a perfectly nested loop (e.g.
    // for applying tiling again). If this is the case, sink the expressions
    // further into the inner loop.
    SmallVector<Stmt *, 4> BodyParts;
    BodyParts.append(LoopHelper.Updates.begin(), LoopHelper.Updates.end());
    if (auto *SourceCXXFor = dyn_cast<CXXForRangeStmt>(LoopStmt))
      BodyParts.push_back(SourceCXXFor->getLoopVarStmt());
    BodyParts.push_back(Inner);
    Inner = CompoundStmt::Create(Context, BodyParts, FPOptionsOverride(),
                                 Inner->getBeginLoc(), Inner->getEndLoc());
    Inner = new (Context)
        ForStmt(Context, InitStmt.get(), CondExpr.get(), nullptr,
                IncrStmt.get(), Inner, LoopHelper.Init->getBeginLoc(),
                LoopHelper.Init->getBeginLoc(), LoopHelper.Inc->getEndLoc());
  }

  // Create floor loops from the inside to the outside.
  for (int I = NumLoops - 1; I >= 0; --I) {
    auto &LoopHelper = LoopHelpers[I];
    Expr *NumIterations = LoopHelper.NumIterations;
    DeclRefExpr *OrigCntVar = cast<DeclRefExpr>(LoopHelper.Counters[0]);
    QualType IVTy = NumIterations->getType();

    // Commonly used variables. One of the constraints of an AST is that every
    // node object must appear at most once, hence we define lamdas that create
    // a new AST node at every use.
    auto MakeFloorIVRef = [&SemaRef = this->SemaRef, &FloorIndVars, I, IVTy,
                           OrigCntVar]() {
      return buildDeclRefExpr(SemaRef, FloorIndVars[I], IVTy,
                              OrigCntVar->getExprLoc());
    };

    // For init-statement: auto .floor.iv = 0
    SemaRef.AddInitializerToDecl(
        FloorIndVars[I],
        SemaRef.ActOnIntegerConstant(LoopHelper.Init->getExprLoc(), 0).get(),
        /*DirectInit=*/false);
    Decl *CounterDecl = FloorIndVars[I];
    StmtResult InitStmt = new (Context)
        DeclStmt(DeclGroupRef::Create(Context, &CounterDecl, 1),
                 OrigCntVar->getBeginLoc(), OrigCntVar->getEndLoc());
    if (!InitStmt.isUsable())
      return StmtError();

    // For cond-expression: .floor.iv < NumIterations
    ExprResult CondExpr =
        SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                           MakeFloorIVRef(), NumIterations);
    if (!CondExpr.isUsable())
      return StmtError();

    // For incr-statement: .floor.iv += DimTileSize
    ExprResult IncrStmt =
        SemaRef.BuildBinOp(CurScope, LoopHelper.Inc->getExprLoc(), BO_AddAssign,
                           MakeFloorIVRef(), MakeDimTileSize(I));
    if (!IncrStmt.isUsable())
      return StmtError();

    Inner = new (Context)
        ForStmt(Context, InitStmt.get(), CondExpr.get(), nullptr,
                IncrStmt.get(), Inner, LoopHelper.Init->getBeginLoc(),
                LoopHelper.Init->getBeginLoc(), LoopHelper.Inc->getEndLoc());
  }

  return OMPTileDirective::Create(Context, StartLoc, EndLoc, Clauses, NumLoops,
                                  AStmt, Inner,
                                  buildPreInits(Context, PreInits));
}

StmtResult SemaOpenMP::ActOnOpenMPUnrollDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  ASTContext &Context = getASTContext();
  Scope *CurScope = SemaRef.getCurScope();
  // Empty statement should only be possible if there already was an error.
  if (!AStmt)
    return StmtError();

  if (checkMutuallyExclusiveClauses(SemaRef, Clauses,
                                    {OMPC_partial, OMPC_full}))
    return StmtError();

  const OMPFullClause *FullClause =
      OMPExecutableDirective::getSingleClause<OMPFullClause>(Clauses);
  const OMPPartialClause *PartialClause =
      OMPExecutableDirective::getSingleClause<OMPPartialClause>(Clauses);
  assert(!(FullClause && PartialClause) &&
         "mutual exclusivity must have been checked before");

  constexpr unsigned NumLoops = 1;
  Stmt *Body = nullptr;
  SmallVector<OMPLoopBasedDirective::HelperExprs, NumLoops> LoopHelpers(
      NumLoops);
  SmallVector<SmallVector<Stmt *, 0>, NumLoops + 1> OriginalInits;
  if (!checkTransformableLoopNest(OMPD_unroll, AStmt, NumLoops, LoopHelpers,
                                  Body, OriginalInits))
    return StmtError();

  unsigned NumGeneratedLoops = PartialClause ? 1 : 0;

  // Delay unrolling to when template is completely instantiated.
  if (SemaRef.CurContext->isDependentContext())
    return OMPUnrollDirective::Create(Context, StartLoc, EndLoc, Clauses, AStmt,
                                      NumGeneratedLoops, nullptr, nullptr);

  assert(LoopHelpers.size() == NumLoops &&
         "Expecting a single-dimensional loop iteration space");
  assert(OriginalInits.size() == NumLoops &&
         "Expecting a single-dimensional loop iteration space");
  OMPLoopBasedDirective::HelperExprs &LoopHelper = LoopHelpers.front();

  if (FullClause) {
    if (!VerifyPositiveIntegerConstantInClause(
             LoopHelper.NumIterations, OMPC_full, /*StrictlyPositive=*/false,
             /*SuppressExprDiags=*/true)
             .isUsable()) {
      Diag(AStmt->getBeginLoc(), diag::err_omp_unroll_full_variable_trip_count);
      Diag(FullClause->getBeginLoc(), diag::note_omp_directive_here)
          << "#pragma omp unroll full";
      return StmtError();
    }
  }

  // The generated loop may only be passed to other loop-associated directive
  // when a partial clause is specified. Without the requirement it is
  // sufficient to generate loop unroll metadata at code-generation.
  if (NumGeneratedLoops == 0)
    return OMPUnrollDirective::Create(Context, StartLoc, EndLoc, Clauses, AStmt,
                                      NumGeneratedLoops, nullptr, nullptr);

  // Otherwise, we need to provide a de-sugared/transformed AST that can be
  // associated with another loop directive.
  //
  // The canonical loop analysis return by checkTransformableLoopNest assumes
  // the following structure to be the same loop without transformations or
  // directives applied: \code OriginalInits; LoopHelper.PreInits;
  // LoopHelper.Counters;
  // for (; IV < LoopHelper.NumIterations; ++IV) {
  //   LoopHelper.Updates;
  //   Body;
  // }
  // \endcode
  // where IV is a variable declared and initialized to 0 in LoopHelper.PreInits
  // and referenced by LoopHelper.IterationVarRef.
  //
  // The unrolling directive transforms this into the following loop:
  // \code
  // OriginalInits;         \
  // LoopHelper.PreInits;    > NewPreInits
  // LoopHelper.Counters;   /
  // for (auto UIV = 0; UIV < LoopHelper.NumIterations; UIV+=Factor) {
  //   #pragma clang loop unroll_count(Factor)
  //   for (IV = UIV; IV < UIV + Factor && UIV < LoopHelper.NumIterations; ++IV)
  //   {
  //     LoopHelper.Updates;
  //     Body;
  //   }
  // }
  // \endcode
  // where UIV is a new logical iteration counter. IV must be the same VarDecl
  // as the original LoopHelper.IterationVarRef because LoopHelper.Updates
  // references it. If the partially unrolled loop is associated with another
  // loop directive (like an OMPForDirective), it will use checkOpenMPLoop to
  // analyze this loop, i.e. the outer loop must fulfill the constraints of an
  // OpenMP canonical loop. The inner loop is not an associable canonical loop
  // and only exists to defer its unrolling to LLVM's LoopUnroll instead of
  // doing it in the frontend (by adding loop metadata). NewPreInits becomes a
  // property of the OMPLoopBasedDirective instead of statements in
  // CompoundStatement. This is to allow the loop to become a non-outermost loop
  // of a canonical loop nest where these PreInits are emitted before the
  // outermost directive.

  // Find the loop statement.
  Stmt *LoopStmt = nullptr;
  collectLoopStmts(AStmt, {LoopStmt});

  // Determine the PreInit declarations.
  SmallVector<Stmt *, 4> PreInits;
  addLoopPreInits(Context, LoopHelper, LoopStmt, OriginalInits[0], PreInits);

  auto *IterationVarRef = cast<DeclRefExpr>(LoopHelper.IterationVarRef);
  QualType IVTy = IterationVarRef->getType();
  assert(LoopHelper.Counters.size() == 1 &&
         "Expecting a single-dimensional loop iteration space");
  auto *OrigVar = cast<DeclRefExpr>(LoopHelper.Counters.front());

  // Determine the unroll factor.
  uint64_t Factor;
  SourceLocation FactorLoc;
  if (Expr *FactorVal = PartialClause->getFactor()) {
    Factor = FactorVal->getIntegerConstantExpr(Context)->getZExtValue();
    FactorLoc = FactorVal->getExprLoc();
  } else {
    // TODO: Use a better profitability model.
    Factor = 2;
  }
  assert(Factor > 0 && "Expected positive unroll factor");
  auto MakeFactorExpr = [this, Factor, IVTy, FactorLoc]() {
    return IntegerLiteral::Create(
        getASTContext(), llvm::APInt(getASTContext().getIntWidth(IVTy), Factor),
        IVTy, FactorLoc);
  };

  // Iteration variable SourceLocations.
  SourceLocation OrigVarLoc = OrigVar->getExprLoc();
  SourceLocation OrigVarLocBegin = OrigVar->getBeginLoc();
  SourceLocation OrigVarLocEnd = OrigVar->getEndLoc();

  // Internal variable names.
  std::string OrigVarName = OrigVar->getNameInfo().getAsString();
  std::string OuterIVName = (Twine(".unrolled.iv.") + OrigVarName).str();
  std::string InnerIVName = (Twine(".unroll_inner.iv.") + OrigVarName).str();
  std::string InnerTripCountName =
      (Twine(".unroll_inner.tripcount.") + OrigVarName).str();

  // Create the iteration variable for the unrolled loop.
  VarDecl *OuterIVDecl =
      buildVarDecl(SemaRef, {}, IVTy, OuterIVName, nullptr, OrigVar);
  auto MakeOuterRef = [this, OuterIVDecl, IVTy, OrigVarLoc]() {
    return buildDeclRefExpr(SemaRef, OuterIVDecl, IVTy, OrigVarLoc);
  };

  // Iteration variable for the inner loop: Reuse the iteration variable created
  // by checkOpenMPLoop.
  auto *InnerIVDecl = cast<VarDecl>(IterationVarRef->getDecl());
  InnerIVDecl->setDeclName(&SemaRef.PP.getIdentifierTable().get(InnerIVName));
  auto MakeInnerRef = [this, InnerIVDecl, IVTy, OrigVarLoc]() {
    return buildDeclRefExpr(SemaRef, InnerIVDecl, IVTy, OrigVarLoc);
  };

  // Make a copy of the NumIterations expression for each use: By the AST
  // constraints, every expression object in a DeclContext must be unique.
  CaptureVars CopyTransformer(SemaRef);
  auto MakeNumIterations = [&CopyTransformer, &LoopHelper]() -> Expr * {
    return AssertSuccess(
        CopyTransformer.TransformExpr(LoopHelper.NumIterations));
  };

  // Inner For init-statement: auto .unroll_inner.iv = .unrolled.iv
  ExprResult LValueConv = SemaRef.DefaultLvalueConversion(MakeOuterRef());
  SemaRef.AddInitializerToDecl(InnerIVDecl, LValueConv.get(),
                               /*DirectInit=*/false);
  StmtResult InnerInit = new (Context)
      DeclStmt(DeclGroupRef(InnerIVDecl), OrigVarLocBegin, OrigVarLocEnd);
  if (!InnerInit.isUsable())
    return StmtError();

  // Inner For cond-expression:
  // \code
  //   .unroll_inner.iv < .unrolled.iv + Factor &&
  //   .unroll_inner.iv < NumIterations
  // \endcode
  // This conjunction of two conditions allows ScalarEvolution to derive the
  // maximum trip count of the inner loop.
  ExprResult EndOfTile =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_Add,
                         MakeOuterRef(), MakeFactorExpr());
  if (!EndOfTile.isUsable())
    return StmtError();
  ExprResult InnerCond1 =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                         MakeInnerRef(), EndOfTile.get());
  if (!InnerCond1.isUsable())
    return StmtError();
  ExprResult InnerCond2 =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                         MakeInnerRef(), MakeNumIterations());
  if (!InnerCond2.isUsable())
    return StmtError();
  ExprResult InnerCond =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LAnd,
                         InnerCond1.get(), InnerCond2.get());
  if (!InnerCond.isUsable())
    return StmtError();

  // Inner For incr-statement: ++.unroll_inner.iv
  ExprResult InnerIncr = SemaRef.BuildUnaryOp(
      CurScope, LoopHelper.Inc->getExprLoc(), UO_PreInc, MakeInnerRef());
  if (!InnerIncr.isUsable())
    return StmtError();

  // Inner For statement.
  SmallVector<Stmt *> InnerBodyStmts;
  InnerBodyStmts.append(LoopHelper.Updates.begin(), LoopHelper.Updates.end());
  if (auto *CXXRangeFor = dyn_cast<CXXForRangeStmt>(LoopStmt))
    InnerBodyStmts.push_back(CXXRangeFor->getLoopVarStmt());
  InnerBodyStmts.push_back(Body);
  CompoundStmt *InnerBody =
      CompoundStmt::Create(getASTContext(), InnerBodyStmts, FPOptionsOverride(),
                           Body->getBeginLoc(), Body->getEndLoc());
  ForStmt *InnerFor = new (Context)
      ForStmt(Context, InnerInit.get(), InnerCond.get(), nullptr,
              InnerIncr.get(), InnerBody, LoopHelper.Init->getBeginLoc(),
              LoopHelper.Init->getBeginLoc(), LoopHelper.Inc->getEndLoc());

  // Unroll metadata for the inner loop.
  // This needs to take into account the remainder portion of the unrolled loop,
  // hence `unroll(full)` does not apply here, even though the LoopUnroll pass
  // supports multiple loop exits. Instead, unroll using a factor equivalent to
  // the maximum trip count, which will also generate a remainder loop. Just
  // `unroll(enable)` (which could have been useful if the user has not
  // specified a concrete factor; even though the outer loop cannot be
  // influenced anymore, would avoid more code bloat than necessary) will refuse
  // the loop because "Won't unroll; remainder loop could not be generated when
  // assuming runtime trip count". Even if it did work, it must not choose a
  // larger unroll factor than the maximum loop length, or it would always just
  // execute the remainder loop.
  LoopHintAttr *UnrollHintAttr =
      LoopHintAttr::CreateImplicit(Context, LoopHintAttr::UnrollCount,
                                   LoopHintAttr::Numeric, MakeFactorExpr());
  AttributedStmt *InnerUnrolled = AttributedStmt::Create(
      getASTContext(), StartLoc, {UnrollHintAttr}, InnerFor);

  // Outer For init-statement: auto .unrolled.iv = 0
  SemaRef.AddInitializerToDecl(
      OuterIVDecl,
      SemaRef.ActOnIntegerConstant(LoopHelper.Init->getExprLoc(), 0).get(),
      /*DirectInit=*/false);
  StmtResult OuterInit = new (Context)
      DeclStmt(DeclGroupRef(OuterIVDecl), OrigVarLocBegin, OrigVarLocEnd);
  if (!OuterInit.isUsable())
    return StmtError();

  // Outer For cond-expression: .unrolled.iv < NumIterations
  ExprResult OuterConde =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                         MakeOuterRef(), MakeNumIterations());
  if (!OuterConde.isUsable())
    return StmtError();

  // Outer For incr-statement: .unrolled.iv += Factor
  ExprResult OuterIncr =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Inc->getExprLoc(), BO_AddAssign,
                         MakeOuterRef(), MakeFactorExpr());
  if (!OuterIncr.isUsable())
    return StmtError();

  // Outer For statement.
  ForStmt *OuterFor = new (Context)
      ForStmt(Context, OuterInit.get(), OuterConde.get(), nullptr,
              OuterIncr.get(), InnerUnrolled, LoopHelper.Init->getBeginLoc(),
              LoopHelper.Init->getBeginLoc(), LoopHelper.Inc->getEndLoc());

  return OMPUnrollDirective::Create(Context, StartLoc, EndLoc, Clauses, AStmt,
                                    NumGeneratedLoops, OuterFor,
                                    buildPreInits(Context, PreInits));
}

StmtResult SemaOpenMP::ActOnOpenMPReverseDirective(Stmt *AStmt,
                                                   SourceLocation StartLoc,
                                                   SourceLocation EndLoc) {
  ASTContext &Context = getASTContext();
  Scope *CurScope = SemaRef.getCurScope();

  // Empty statement should only be possible if there already was an error.
  if (!AStmt)
    return StmtError();

  constexpr unsigned NumLoops = 1;
  Stmt *Body = nullptr;
  SmallVector<OMPLoopBasedDirective::HelperExprs, NumLoops> LoopHelpers(
      NumLoops);
  SmallVector<SmallVector<Stmt *, 0>, NumLoops + 1> OriginalInits;
  if (!checkTransformableLoopNest(OMPD_reverse, AStmt, NumLoops, LoopHelpers,
                                  Body, OriginalInits))
    return StmtError();

  // Delay applying the transformation to when template is completely
  // instantiated.
  if (SemaRef.CurContext->isDependentContext())
    return OMPReverseDirective::Create(Context, StartLoc, EndLoc, AStmt,
                                       nullptr, nullptr);

  assert(LoopHelpers.size() == NumLoops &&
         "Expecting a single-dimensional loop iteration space");
  assert(OriginalInits.size() == NumLoops &&
         "Expecting a single-dimensional loop iteration space");
  OMPLoopBasedDirective::HelperExprs &LoopHelper = LoopHelpers.front();

  // Find the loop statement.
  Stmt *LoopStmt = nullptr;
  collectLoopStmts(AStmt, {LoopStmt});

  // Determine the PreInit declarations.
  SmallVector<Stmt *> PreInits;
  addLoopPreInits(Context, LoopHelper, LoopStmt, OriginalInits[0], PreInits);

  auto *IterationVarRef = cast<DeclRefExpr>(LoopHelper.IterationVarRef);
  QualType IVTy = IterationVarRef->getType();
  uint64_t IVWidth = Context.getTypeSize(IVTy);
  auto *OrigVar = cast<DeclRefExpr>(LoopHelper.Counters.front());

  // Iteration variable SourceLocations.
  SourceLocation OrigVarLoc = OrigVar->getExprLoc();
  SourceLocation OrigVarLocBegin = OrigVar->getBeginLoc();
  SourceLocation OrigVarLocEnd = OrigVar->getEndLoc();

  // Locations pointing to the transformation.
  SourceLocation TransformLoc = StartLoc;
  SourceLocation TransformLocBegin = StartLoc;
  SourceLocation TransformLocEnd = EndLoc;

  // Internal variable names.
  std::string OrigVarName = OrigVar->getNameInfo().getAsString();
  SmallString<64> ForwardIVName(".forward.iv.");
  ForwardIVName += OrigVarName;
  SmallString<64> ReversedIVName(".reversed.iv.");
  ReversedIVName += OrigVarName;

  // LoopHelper.Updates will read the logical iteration number from
  // LoopHelper.IterationVarRef, compute the value of the user loop counter of
  // that logical iteration from it, then assign it to the user loop counter
  // variable. We cannot directly use LoopHelper.IterationVarRef as the
  // induction variable of the generated loop because it may cause an underflow:
  // \code{.c}
  //   for (unsigned i = 0; i < n; ++i)
  //     body(i);
  // \endcode
  //
  // Naive reversal:
  // \code{.c}
  //   for (unsigned i = n-1; i >= 0; --i)
  //     body(i);
  // \endcode
  //
  // Instead, we introduce a new iteration variable representing the logical
  // iteration counter of the original loop, convert it to the logical iteration
  // number of the reversed loop, then let LoopHelper.Updates compute the user's
  // loop iteration variable from it.
  // \code{.cpp}
  //   for (auto .forward.iv = 0; .forward.iv < n; ++.forward.iv) {
  //     auto .reversed.iv = n - .forward.iv - 1;
  //     i = (.reversed.iv + 0) * 1;                // LoopHelper.Updates
  //     body(i);                                   // Body
  //   }
  // \endcode

  // Subexpressions with more than one use. One of the constraints of an AST is
  // that every node object must appear at most once, hence we define a lambda
  // that creates a new AST node at every use.
  CaptureVars CopyTransformer(SemaRef);
  auto MakeNumIterations = [&CopyTransformer, &LoopHelper]() -> Expr * {
    return AssertSuccess(
        CopyTransformer.TransformExpr(LoopHelper.NumIterations));
  };

  // Create the iteration variable for the forward loop (from 0 to n-1).
  VarDecl *ForwardIVDecl =
      buildVarDecl(SemaRef, {}, IVTy, ForwardIVName, nullptr, OrigVar);
  auto MakeForwardRef = [&SemaRef = this->SemaRef, ForwardIVDecl, IVTy,
                         OrigVarLoc]() {
    return buildDeclRefExpr(SemaRef, ForwardIVDecl, IVTy, OrigVarLoc);
  };

  // Iteration variable for the reversed induction variable (from n-1 downto 0):
  // Reuse the iteration variable created by checkOpenMPLoop.
  auto *ReversedIVDecl = cast<VarDecl>(IterationVarRef->getDecl());
  ReversedIVDecl->setDeclName(
      &SemaRef.PP.getIdentifierTable().get(ReversedIVName));

  // For init-statement:
  // \code{.cpp}
  //   auto .forward.iv = 0;
  // \endcode
  auto *Zero = IntegerLiteral::Create(Context, llvm::APInt::getZero(IVWidth),
                                      ForwardIVDecl->getType(), OrigVarLoc);
  SemaRef.AddInitializerToDecl(ForwardIVDecl, Zero, /*DirectInit=*/false);
  StmtResult Init = new (Context)
      DeclStmt(DeclGroupRef(ForwardIVDecl), OrigVarLocBegin, OrigVarLocEnd);
  if (!Init.isUsable())
    return StmtError();

  // Forward iv cond-expression:
  // \code{.cpp}
  //   .forward.iv < MakeNumIterations()
  // \endcode
  ExprResult Cond =
      SemaRef.BuildBinOp(CurScope, LoopHelper.Cond->getExprLoc(), BO_LT,
                         MakeForwardRef(), MakeNumIterations());
  if (!Cond.isUsable())
    return StmtError();

  // Forward incr-statement:
  // \code{.c}
  //   ++.forward.iv
  // \endcode
  ExprResult Incr = SemaRef.BuildUnaryOp(CurScope, LoopHelper.Inc->getExprLoc(),
                                         UO_PreInc, MakeForwardRef());
  if (!Incr.isUsable())
    return StmtError();

  // Reverse the forward-iv:
  // \code{.cpp}
  //   auto .reversed.iv = MakeNumIterations() - 1 - .forward.iv
  // \endcode
  auto *One = IntegerLiteral::Create(Context, llvm::APInt(IVWidth, 1), IVTy,
                                     TransformLoc);
  ExprResult Minus = SemaRef.BuildBinOp(CurScope, TransformLoc, BO_Sub,
                                        MakeNumIterations(), One);
  if (!Minus.isUsable())
    return StmtError();
  Minus = SemaRef.BuildBinOp(CurScope, TransformLoc, BO_Sub, Minus.get(),
                             MakeForwardRef());
  if (!Minus.isUsable())
    return StmtError();
  StmtResult InitReversed = new (Context) DeclStmt(
      DeclGroupRef(ReversedIVDecl), TransformLocBegin, TransformLocEnd);
  if (!InitReversed.isUsable())
    return StmtError();
  SemaRef.AddInitializerToDecl(ReversedIVDecl, Minus.get(),
                               /*DirectInit=*/false);

  // The new loop body.
  SmallVector<Stmt *, 4> BodyStmts;
  BodyStmts.reserve(LoopHelper.Updates.size() + 2 +
                    (isa<CXXForRangeStmt>(LoopStmt) ? 1 : 0));
  BodyStmts.push_back(InitReversed.get());
  llvm::append_range(BodyStmts, LoopHelper.Updates);
  if (auto *CXXRangeFor = dyn_cast<CXXForRangeStmt>(LoopStmt))
    BodyStmts.push_back(CXXRangeFor->getLoopVarStmt());
  BodyStmts.push_back(Body);
  auto *ReversedBody =
      CompoundStmt::Create(Context, BodyStmts, FPOptionsOverride(),
                           Body->getBeginLoc(), Body->getEndLoc());

  // Finally create the reversed For-statement.
  auto *ReversedFor = new (Context)
      ForStmt(Context, Init.get(), Cond.get(), nullptr, Incr.get(),
              ReversedBody, LoopHelper.Init->getBeginLoc(),
              LoopHelper.Init->getBeginLoc(), LoopHelper.Inc->getEndLoc());
  return OMPReverseDirective::Create(Context, StartLoc, EndLoc, AStmt,
                                     ReversedFor,
                                     buildPreInits(Context, PreInits));
}

StmtResult SemaOpenMP::ActOnOpenMPInterchangeDirective(
    ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
    SourceLocation EndLoc) {
  ASTContext &Context = getASTContext();
  DeclContext *CurContext = SemaRef.CurContext;
  Scope *CurScope = SemaRef.getCurScope();

  // Empty statement should only be possible if there already was an error.
  if (!AStmt)
    return StmtError();

  // interchange without permutation clause swaps two loops.
  constexpr size_t NumLoops = 2;

  // Verify and diagnose loop nest.
  SmallVector<OMPLoopBasedDirective::HelperExprs, 4> LoopHelpers(NumLoops);
  Stmt *Body = nullptr;
  SmallVector<SmallVector<Stmt *, 0>, 2> OriginalInits;
  if (!checkTransformableLoopNest(OMPD_interchange, AStmt, NumLoops,
                                  LoopHelpers, Body, OriginalInits))
    return StmtError();

  // Delay interchange to when template is completely instantiated.
  if (CurContext->isDependentContext())
    return OMPInterchangeDirective::Create(Context, StartLoc, EndLoc, Clauses,
                                           NumLoops, AStmt, nullptr, nullptr);

  assert(LoopHelpers.size() == NumLoops &&
         "Expecting loop iteration space dimensionaly to match number of "
         "affected loops");
  assert(OriginalInits.size() == NumLoops &&
         "Expecting loop iteration space dimensionaly to match number of "
         "affected loops");

  // Decode the permutation clause.
  constexpr uint64_t Permutation[] = {1, 0};

  // Find the affected loops.
  SmallVector<Stmt *> LoopStmts(NumLoops, nullptr);
  collectLoopStmts(AStmt, LoopStmts);

  // Collect pre-init statements on the order before the permuation.
  SmallVector<Stmt *> PreInits;
  for (auto I : llvm::seq<int>(NumLoops)) {
    OMPLoopBasedDirective::HelperExprs &LoopHelper = LoopHelpers[I];

    assert(LoopHelper.Counters.size() == 1 &&
           "Single-dimensional loop iteration space expected");
    auto *OrigCntVar = cast<DeclRefExpr>(LoopHelper.Counters.front());

    std::string OrigVarName = OrigCntVar->getNameInfo().getAsString();
    addLoopPreInits(Context, LoopHelper, LoopStmts[I], OriginalInits[I],
                    PreInits);
  }

  SmallVector<VarDecl *> PermutedIndVars(NumLoops);
  CaptureVars CopyTransformer(SemaRef);

  // Create the permuted loops from the inside to the outside of the
  // interchanged loop nest. Body of the innermost new loop is the original
  // innermost body.
  Stmt *Inner = Body;
  for (auto TargetIdx : llvm::reverse(llvm::seq<int>(NumLoops))) {
    // Get the original loop that belongs to this new position.
    uint64_t SourceIdx = Permutation[TargetIdx];
    OMPLoopBasedDirective::HelperExprs &SourceHelper = LoopHelpers[SourceIdx];
    Stmt *SourceLoopStmt = LoopStmts[SourceIdx];
    assert(SourceHelper.Counters.size() == 1 &&
           "Single-dimensional loop iteration space expected");
    auto *OrigCntVar = cast<DeclRefExpr>(SourceHelper.Counters.front());

    // Normalized loop counter variable: From 0 to n-1, always an integer type.
    DeclRefExpr *IterVarRef = cast<DeclRefExpr>(SourceHelper.IterationVarRef);
    QualType IVTy = IterVarRef->getType();
    assert(IVTy->isIntegerType() &&
           "Expected the logical iteration counter to be an integer");

    std::string OrigVarName = OrigCntVar->getNameInfo().getAsString();
    SourceLocation OrigVarLoc = IterVarRef->getExprLoc();

    // Make a copy of the NumIterations expression for each use: By the AST
    // constraints, every expression object in a DeclContext must be unique.
    auto MakeNumIterations = [&CopyTransformer, &SourceHelper]() -> Expr * {
      return AssertSuccess(
          CopyTransformer.TransformExpr(SourceHelper.NumIterations));
    };

    // Iteration variable for the permuted loop. Reuse the one from
    // checkOpenMPLoop which will also be used to update the original loop
    // variable.
    SmallString<64> PermutedCntName(".permuted_");
    PermutedCntName.append({llvm::utostr(TargetIdx), ".iv.", OrigVarName});
    auto *PermutedCntDecl = cast<VarDecl>(IterVarRef->getDecl());
    PermutedCntDecl->setDeclName(
        &SemaRef.PP.getIdentifierTable().get(PermutedCntName));
    PermutedIndVars[TargetIdx] = PermutedCntDecl;
    auto MakePermutedRef = [this, PermutedCntDecl, IVTy, OrigVarLoc]() {
      return buildDeclRefExpr(SemaRef, PermutedCntDecl, IVTy, OrigVarLoc);
    };

    // For init-statement:
    // \code
    //   auto .permuted_{target}.iv = 0
    // \endcode
    ExprResult Zero = SemaRef.ActOnIntegerConstant(OrigVarLoc, 0);
    if (!Zero.isUsable())
      return StmtError();
    SemaRef.AddInitializerToDecl(PermutedCntDecl, Zero.get(),
                                 /*DirectInit=*/false);
    StmtResult InitStmt = new (Context)
        DeclStmt(DeclGroupRef(PermutedCntDecl), OrigCntVar->getBeginLoc(),
                 OrigCntVar->getEndLoc());
    if (!InitStmt.isUsable())
      return StmtError();

    // For cond-expression:
    // \code
    //   .permuted_{target}.iv < MakeNumIterations()
    // \endcode
    ExprResult CondExpr =
        SemaRef.BuildBinOp(CurScope, SourceHelper.Cond->getExprLoc(), BO_LT,
                           MakePermutedRef(), MakeNumIterations());
    if (!CondExpr.isUsable())
      return StmtError();

    // For incr-statement:
    // \code
    //   ++.tile.iv
    // \endcode
    ExprResult IncrStmt = SemaRef.BuildUnaryOp(
        CurScope, SourceHelper.Inc->getExprLoc(), UO_PreInc, MakePermutedRef());
    if (!IncrStmt.isUsable())
      return StmtError();

    SmallVector<Stmt *, 4> BodyParts(SourceHelper.Updates.begin(),
                                     SourceHelper.Updates.end());
    if (auto *SourceCXXFor = dyn_cast<CXXForRangeStmt>(SourceLoopStmt))
      BodyParts.push_back(SourceCXXFor->getLoopVarStmt());
    BodyParts.push_back(Inner);
    Inner = CompoundStmt::Create(Context, BodyParts, FPOptionsOverride(),
                                 Inner->getBeginLoc(), Inner->getEndLoc());
    Inner = new (Context) ForStmt(
        Context, InitStmt.get(), CondExpr.get(), nullptr, IncrStmt.get(), Inner,
        SourceHelper.Init->getBeginLoc(), SourceHelper.Init->getBeginLoc(),
        SourceHelper.Inc->getEndLoc());
  }

  return OMPInterchangeDirective::Create(Context, StartLoc, EndLoc, Clauses,
                                         NumLoops, AStmt, Inner,
                                         buildPreInits(Context, PreInits));
}

OMPClause *SemaOpenMP::ActOnOpenMPSingleExprClause(OpenMPClauseKind Kind,
                                                   Expr *Expr,
                                                   SourceLocation StartLoc,
                                                   SourceLocation LParenLoc,
                                                   SourceLocation EndLoc) {
  OMPClause *Res = nullptr;
  switch (Kind) {
  case OMPC_final:
    Res = ActOnOpenMPFinalClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_num_threads:
    Res = ActOnOpenMPNumThreadsClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_safelen:
    Res = ActOnOpenMPSafelenClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_simdlen:
    Res = ActOnOpenMPSimdlenClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_allocator:
    Res = ActOnOpenMPAllocatorClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_collapse:
    Res = ActOnOpenMPCollapseClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_ordered:
    Res = ActOnOpenMPOrderedClause(StartLoc, EndLoc, LParenLoc, Expr);
    break;
  case OMPC_num_teams:
    Res = ActOnOpenMPNumTeamsClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_thread_limit:
    Res = ActOnOpenMPThreadLimitClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_priority:
    Res = ActOnOpenMPPriorityClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_hint:
    Res = ActOnOpenMPHintClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_depobj:
    Res = ActOnOpenMPDepobjClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_detach:
    Res = ActOnOpenMPDetachClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_novariants:
    Res = ActOnOpenMPNovariantsClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_nocontext:
    Res = ActOnOpenMPNocontextClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_filter:
    Res = ActOnOpenMPFilterClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_partial:
    Res = ActOnOpenMPPartialClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_message:
    Res = ActOnOpenMPMessageClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_align:
    Res = ActOnOpenMPAlignClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_ompx_dyn_cgroup_mem:
    Res = ActOnOpenMPXDynCGroupMemClause(Expr, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_grainsize:
  case OMPC_num_tasks:
  case OMPC_device:
  case OMPC_if:
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_schedule:
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_lastprivate:
  case OMPC_shared:
  case OMPC_reduction:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_linear:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_threadprivate:
  case OMPC_sizes:
  case OMPC_allocate:
  case OMPC_flush:
  case OMPC_read:
  case OMPC_write:
  case OMPC_update:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_depend:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_map:
  case OMPC_nogroup:
  case OMPC_dist_schedule:
  case OMPC_defaultmap:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_atomic_default_mem_order:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_order:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_destroy:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_bind:
  default:
    llvm_unreachable("Clause is not allowed.");
  }
  return Res;
}

// An OpenMP directive such as 'target parallel' has two captured regions:
// for the 'target' and 'parallel' respectively.  This function returns
// the region in which to capture expressions associated with a clause.
// A return value of OMPD_unknown signifies that the expression should not
// be captured.
static OpenMPDirectiveKind getOpenMPCaptureRegionForClause(
    OpenMPDirectiveKind DKind, OpenMPClauseKind CKind, unsigned OpenMPVersion,
    OpenMPDirectiveKind NameModifier = OMPD_unknown) {
  assert(isAllowedClauseForDirective(DKind, CKind, OpenMPVersion) &&
         "Invalid directive with CKind-clause");

  // Invalid modifier will be diagnosed separately, just return OMPD_unknown.
  if (NameModifier != OMPD_unknown &&
      !isAllowedClauseForDirective(NameModifier, CKind, OpenMPVersion))
    return OMPD_unknown;

  ArrayRef<OpenMPDirectiveKind> Leafs = getLeafConstructsOrSelf(DKind);

  // [5.2:341:24-30]
  // If the clauses have expressions on them, such as for various clauses where
  // the argument of the clause is an expression, or lower-bound, length, or
  // stride expressions inside array sections (or subscript and stride
  // expressions in subscript-triplet for Fortran), or linear-step or alignment
  // expressions, the expressions are evaluated immediately before the construct
  // to which the clause has been split or duplicated per the above rules
  // (therefore inside of the outer leaf constructs). However, the expressions
  // inside the num_teams and thread_limit clauses are always evaluated before
  // the outermost leaf construct.

  // Process special cases first.
  switch (CKind) {
  case OMPC_if:
    switch (DKind) {
    case OMPD_teams_loop:
    case OMPD_target_teams_loop:
      // For [target] teams loop, assume capture region is 'teams' so it's
      // available for codegen later to use if/when necessary.
      return OMPD_teams;
    case OMPD_target_update:
    case OMPD_target_enter_data:
    case OMPD_target_exit_data:
      return OMPD_task;
    default:
      break;
    }
    break;
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_ompx_dyn_cgroup_mem:
    if (Leafs[0] == OMPD_target)
      return OMPD_target;
    break;
  case OMPC_device:
    if (Leafs[0] == OMPD_target ||
        llvm::is_contained({OMPD_dispatch, OMPD_target_update,
                            OMPD_target_enter_data, OMPD_target_exit_data},
                           DKind))
      return OMPD_task;
    break;
  case OMPC_novariants:
  case OMPC_nocontext:
    if (DKind == OMPD_dispatch)
      return OMPD_task;
    break;
  case OMPC_when:
    if (DKind == OMPD_metadirective)
      return OMPD_metadirective;
    break;
  case OMPC_filter:
    return OMPD_unknown;
  default:
    break;
  }

  // If none of the special cases above applied, and DKind is a capturing
  // directive, find the innermost enclosing leaf construct that allows the
  // clause, and returns the corresponding capture region.

  auto GetEnclosingRegion = [&](int EndIdx, OpenMPClauseKind Clause) {
    // Find the index in "Leafs" of the last leaf that allows the given
    // clause. The search will only include indexes [0, EndIdx).
    // EndIdx may be set to the index of the NameModifier, if present.
    int InnermostIdx = [&]() {
      for (int I = EndIdx - 1; I >= 0; --I) {
        if (isAllowedClauseForDirective(Leafs[I], Clause, OpenMPVersion))
          return I;
      }
      return -1;
    }();

    // Find the nearest enclosing capture region.
    SmallVector<OpenMPDirectiveKind, 2> Regions;
    for (int I = InnermostIdx - 1; I >= 0; --I) {
      if (!isOpenMPCapturingDirective(Leafs[I]))
        continue;
      Regions.clear();
      getOpenMPCaptureRegions(Regions, Leafs[I]);
      if (Regions[0] != OMPD_unknown)
        return Regions.back();
    }
    return OMPD_unknown;
  };

  if (isOpenMPCapturingDirective(DKind)) {
    auto GetLeafIndex = [&](OpenMPDirectiveKind Dir) {
      for (int I = 0, E = Leafs.size(); I != E; ++I) {
        if (Leafs[I] == Dir)
          return I + 1;
      }
      return 0;
    };

    int End = NameModifier == OMPD_unknown ? Leafs.size()
                                           : GetLeafIndex(NameModifier);
    return GetEnclosingRegion(End, CKind);
  }

  return OMPD_unknown;
}

OMPClause *SemaOpenMP::ActOnOpenMPIfClause(
    OpenMPDirectiveKind NameModifier, Expr *Condition, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation NameModifierLoc,
    SourceLocation ColonLoc, SourceLocation EndLoc) {
  Expr *ValExpr = Condition;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;
  if (!Condition->isValueDependent() && !Condition->isTypeDependent() &&
      !Condition->isInstantiationDependent() &&
      !Condition->containsUnexpandedParameterPack()) {
    ExprResult Val = SemaRef.CheckBooleanCondition(StartLoc, Condition);
    if (Val.isInvalid())
      return nullptr;

    ValExpr = Val.get();

    OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
    CaptureRegion = getOpenMPCaptureRegionForClause(
        DKind, OMPC_if, getLangOpts().OpenMP, NameModifier);
    if (CaptureRegion != OMPD_unknown &&
        !SemaRef.CurContext->isDependentContext()) {
      ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
      llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
      ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
      HelperValStmt = buildPreInits(getASTContext(), Captures);
    }
  }

  return new (getASTContext())
      OMPIfClause(NameModifier, ValExpr, HelperValStmt, CaptureRegion, StartLoc,
                  LParenLoc, NameModifierLoc, ColonLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPFinalClause(Expr *Condition,
                                              SourceLocation StartLoc,
                                              SourceLocation LParenLoc,
                                              SourceLocation EndLoc) {
  Expr *ValExpr = Condition;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;
  if (!Condition->isValueDependent() && !Condition->isTypeDependent() &&
      !Condition->isInstantiationDependent() &&
      !Condition->containsUnexpandedParameterPack()) {
    ExprResult Val = SemaRef.CheckBooleanCondition(StartLoc, Condition);
    if (Val.isInvalid())
      return nullptr;

    ValExpr = SemaRef.MakeFullExpr(Val.get()).get();

    OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
    CaptureRegion = getOpenMPCaptureRegionForClause(DKind, OMPC_final,
                                                    getLangOpts().OpenMP);
    if (CaptureRegion != OMPD_unknown &&
        !SemaRef.CurContext->isDependentContext()) {
      ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
      llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
      ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
      HelperValStmt = buildPreInits(getASTContext(), Captures);
    }
  }

  return new (getASTContext()) OMPFinalClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

ExprResult
SemaOpenMP::PerformOpenMPImplicitIntegerConversion(SourceLocation Loc,
                                                   Expr *Op) {
  if (!Op)
    return ExprError();

  class IntConvertDiagnoser : public Sema::ICEConvertDiagnoser {
  public:
    IntConvertDiagnoser()
        : ICEConvertDiagnoser(/*AllowScopedEnumerations*/ false, false, true) {}
    SemaDiagnosticBuilder diagnoseNotInt(Sema &S, SourceLocation Loc,
                                         QualType T) override {
      return S.Diag(Loc, diag::err_omp_not_integral) << T;
    }
    SemaDiagnosticBuilder diagnoseIncomplete(Sema &S, SourceLocation Loc,
                                             QualType T) override {
      return S.Diag(Loc, diag::err_omp_incomplete_type) << T;
    }
    SemaDiagnosticBuilder diagnoseExplicitConv(Sema &S, SourceLocation Loc,
                                               QualType T,
                                               QualType ConvTy) override {
      return S.Diag(Loc, diag::err_omp_explicit_conversion) << T << ConvTy;
    }
    SemaDiagnosticBuilder noteExplicitConv(Sema &S, CXXConversionDecl *Conv,
                                           QualType ConvTy) override {
      return S.Diag(Conv->getLocation(), diag::note_omp_conversion_here)
             << ConvTy->isEnumeralType() << ConvTy;
    }
    SemaDiagnosticBuilder diagnoseAmbiguous(Sema &S, SourceLocation Loc,
                                            QualType T) override {
      return S.Diag(Loc, diag::err_omp_ambiguous_conversion) << T;
    }
    SemaDiagnosticBuilder noteAmbiguous(Sema &S, CXXConversionDecl *Conv,
                                        QualType ConvTy) override {
      return S.Diag(Conv->getLocation(), diag::note_omp_conversion_here)
             << ConvTy->isEnumeralType() << ConvTy;
    }
    SemaDiagnosticBuilder diagnoseConversion(Sema &, SourceLocation, QualType,
                                             QualType) override {
      llvm_unreachable("conversion functions are permitted");
    }
  } ConvertDiagnoser;
  return SemaRef.PerformContextualImplicitConversion(Loc, Op, ConvertDiagnoser);
}

static bool
isNonNegativeIntegerValue(Expr *&ValExpr, Sema &SemaRef, OpenMPClauseKind CKind,
                          bool StrictlyPositive, bool BuildCapture = false,
                          OpenMPDirectiveKind DKind = OMPD_unknown,
                          OpenMPDirectiveKind *CaptureRegion = nullptr,
                          Stmt **HelperValStmt = nullptr) {
  if (!ValExpr->isTypeDependent() && !ValExpr->isValueDependent() &&
      !ValExpr->isInstantiationDependent()) {
    SourceLocation Loc = ValExpr->getExprLoc();
    ExprResult Value =
        SemaRef.OpenMP().PerformOpenMPImplicitIntegerConversion(Loc, ValExpr);
    if (Value.isInvalid())
      return false;

    ValExpr = Value.get();
    // The expression must evaluate to a non-negative integer value.
    if (std::optional<llvm::APSInt> Result =
            ValExpr->getIntegerConstantExpr(SemaRef.Context)) {
      if (Result->isSigned() &&
          !((!StrictlyPositive && Result->isNonNegative()) ||
            (StrictlyPositive && Result->isStrictlyPositive()))) {
        SemaRef.Diag(Loc, diag::err_omp_negative_expression_in_clause)
            << getOpenMPClauseName(CKind) << (StrictlyPositive ? 1 : 0)
            << ValExpr->getSourceRange();
        return false;
      }
    }
    if (!BuildCapture)
      return true;
    *CaptureRegion =
        getOpenMPCaptureRegionForClause(DKind, CKind, SemaRef.LangOpts.OpenMP);
    if (*CaptureRegion != OMPD_unknown &&
        !SemaRef.CurContext->isDependentContext()) {
      ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
      llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
      ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
      *HelperValStmt = buildPreInits(SemaRef.Context, Captures);
    }
  }
  return true;
}

OMPClause *SemaOpenMP::ActOnOpenMPNumThreadsClause(Expr *NumThreads,
                                                   SourceLocation StartLoc,
                                                   SourceLocation LParenLoc,
                                                   SourceLocation EndLoc) {
  Expr *ValExpr = NumThreads;
  Stmt *HelperValStmt = nullptr;

  // OpenMP [2.5, Restrictions]
  //  The num_threads expression must evaluate to a positive integer value.
  if (!isNonNegativeIntegerValue(ValExpr, SemaRef, OMPC_num_threads,
                                 /*StrictlyPositive=*/true))
    return nullptr;

  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  OpenMPDirectiveKind CaptureRegion = getOpenMPCaptureRegionForClause(
      DKind, OMPC_num_threads, getLangOpts().OpenMP);
  if (CaptureRegion != OMPD_unknown &&
      !SemaRef.CurContext->isDependentContext()) {
    ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
    llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
    ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
    HelperValStmt = buildPreInits(getASTContext(), Captures);
  }

  return new (getASTContext()) OMPNumThreadsClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

ExprResult SemaOpenMP::VerifyPositiveIntegerConstantInClause(
    Expr *E, OpenMPClauseKind CKind, bool StrictlyPositive,
    bool SuppressExprDiags) {
  if (!E)
    return ExprError();
  if (E->isValueDependent() || E->isTypeDependent() ||
      E->isInstantiationDependent() || E->containsUnexpandedParameterPack())
    return E;

  llvm::APSInt Result;
  ExprResult ICE;
  if (SuppressExprDiags) {
    // Use a custom diagnoser that suppresses 'note' diagnostics about the
    // expression.
    struct SuppressedDiagnoser : public Sema::VerifyICEDiagnoser {
      SuppressedDiagnoser() : VerifyICEDiagnoser(/*Suppress=*/true) {}
      SemaBase::SemaDiagnosticBuilder
      diagnoseNotICE(Sema &S, SourceLocation Loc) override {
        llvm_unreachable("Diagnostic suppressed");
      }
    } Diagnoser;
    ICE = SemaRef.VerifyIntegerConstantExpression(E, &Result, Diagnoser,
                                                  Sema::AllowFold);
  } else {
    ICE = SemaRef.VerifyIntegerConstantExpression(E, &Result,
                                                  /*FIXME*/ Sema::AllowFold);
  }
  if (ICE.isInvalid())
    return ExprError();

  if ((StrictlyPositive && !Result.isStrictlyPositive()) ||
      (!StrictlyPositive && !Result.isNonNegative())) {
    Diag(E->getExprLoc(), diag::err_omp_negative_expression_in_clause)
        << getOpenMPClauseName(CKind) << (StrictlyPositive ? 1 : 0)
        << E->getSourceRange();
    return ExprError();
  }
  if ((CKind == OMPC_aligned || CKind == OMPC_align) && !Result.isPowerOf2()) {
    Diag(E->getExprLoc(), diag::warn_omp_alignment_not_power_of_two)
        << E->getSourceRange();
    return ExprError();
  }
  if (CKind == OMPC_collapse && DSAStack->getAssociatedLoops() == 1)
    DSAStack->setAssociatedLoops(Result.getExtValue());
  else if (CKind == OMPC_ordered)
    DSAStack->setAssociatedLoops(Result.getExtValue());
  return ICE;
}

OMPClause *SemaOpenMP::ActOnOpenMPSafelenClause(Expr *Len,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation EndLoc) {
  // OpenMP [2.8.1, simd construct, Description]
  // The parameter of the safelen clause must be a constant
  // positive integer expression.
  ExprResult Safelen = VerifyPositiveIntegerConstantInClause(Len, OMPC_safelen);
  if (Safelen.isInvalid())
    return nullptr;
  return new (getASTContext())
      OMPSafelenClause(Safelen.get(), StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPSimdlenClause(Expr *Len,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation EndLoc) {
  // OpenMP [2.8.1, simd construct, Description]
  // The parameter of the simdlen clause must be a constant
  // positive integer expression.
  ExprResult Simdlen = VerifyPositiveIntegerConstantInClause(Len, OMPC_simdlen);
  if (Simdlen.isInvalid())
    return nullptr;
  return new (getASTContext())
      OMPSimdlenClause(Simdlen.get(), StartLoc, LParenLoc, EndLoc);
}

/// Tries to find omp_allocator_handle_t type.
static bool findOMPAllocatorHandleT(Sema &S, SourceLocation Loc,
                                    DSAStackTy *Stack) {
  if (!Stack->getOMPAllocatorHandleT().isNull())
    return true;

  // Set the allocator handle type.
  IdentifierInfo *II = &S.PP.getIdentifierTable().get("omp_allocator_handle_t");
  ParsedType PT = S.getTypeName(*II, Loc, S.getCurScope());
  if (!PT.getAsOpaquePtr() || PT.get().isNull()) {
    S.Diag(Loc, diag::err_omp_implied_type_not_found)
        << "omp_allocator_handle_t";
    return false;
  }
  QualType AllocatorHandleEnumTy = PT.get();
  AllocatorHandleEnumTy.addConst();
  Stack->setOMPAllocatorHandleT(AllocatorHandleEnumTy);

  // Fill the predefined allocator map.
  bool ErrorFound = false;
  for (int I = 0; I < OMPAllocateDeclAttr::OMPUserDefinedMemAlloc; ++I) {
    auto AllocatorKind = static_cast<OMPAllocateDeclAttr::AllocatorTypeTy>(I);
    StringRef Allocator =
        OMPAllocateDeclAttr::ConvertAllocatorTypeTyToStr(AllocatorKind);
    DeclarationName AllocatorName = &S.getASTContext().Idents.get(Allocator);
    auto *VD = dyn_cast_or_null<ValueDecl>(
        S.LookupSingleName(S.TUScope, AllocatorName, Loc, Sema::LookupAnyName));
    if (!VD) {
      ErrorFound = true;
      break;
    }
    QualType AllocatorType =
        VD->getType().getNonLValueExprType(S.getASTContext());
    ExprResult Res = S.BuildDeclRefExpr(VD, AllocatorType, VK_LValue, Loc);
    if (!Res.isUsable()) {
      ErrorFound = true;
      break;
    }
    Res = S.PerformImplicitConversion(Res.get(), AllocatorHandleEnumTy,
                                      Sema::AA_Initializing,
                                      /* AllowExplicit */ true);
    if (!Res.isUsable()) {
      ErrorFound = true;
      break;
    }
    Stack->setAllocator(AllocatorKind, Res.get());
  }
  if (ErrorFound) {
    S.Diag(Loc, diag::err_omp_implied_type_not_found)
        << "omp_allocator_handle_t";
    return false;
  }

  return true;
}

OMPClause *SemaOpenMP::ActOnOpenMPAllocatorClause(Expr *A,
                                                  SourceLocation StartLoc,
                                                  SourceLocation LParenLoc,
                                                  SourceLocation EndLoc) {
  // OpenMP [2.11.3, allocate Directive, Description]
  // allocator is an expression of omp_allocator_handle_t type.
  if (!findOMPAllocatorHandleT(SemaRef, A->getExprLoc(), DSAStack))
    return nullptr;

  ExprResult Allocator = SemaRef.DefaultLvalueConversion(A);
  if (Allocator.isInvalid())
    return nullptr;
  Allocator = SemaRef.PerformImplicitConversion(
      Allocator.get(), DSAStack->getOMPAllocatorHandleT(),
      Sema::AA_Initializing,
      /*AllowExplicit=*/true);
  if (Allocator.isInvalid())
    return nullptr;
  return new (getASTContext())
      OMPAllocatorClause(Allocator.get(), StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPCollapseClause(Expr *NumForLoops,
                                                 SourceLocation StartLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation EndLoc) {
  // OpenMP [2.7.1, loop construct, Description]
  // OpenMP [2.8.1, simd construct, Description]
  // OpenMP [2.9.6, distribute construct, Description]
  // The parameter of the collapse clause must be a constant
  // positive integer expression.
  ExprResult NumForLoopsResult =
      VerifyPositiveIntegerConstantInClause(NumForLoops, OMPC_collapse);
  if (NumForLoopsResult.isInvalid())
    return nullptr;
  return new (getASTContext())
      OMPCollapseClause(NumForLoopsResult.get(), StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPOrderedClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc,
                                                SourceLocation LParenLoc,
                                                Expr *NumForLoops) {
  // OpenMP [2.7.1, loop construct, Description]
  // OpenMP [2.8.1, simd construct, Description]
  // OpenMP [2.9.6, distribute construct, Description]
  // The parameter of the ordered clause must be a constant
  // positive integer expression if any.
  if (NumForLoops && LParenLoc.isValid()) {
    ExprResult NumForLoopsResult =
        VerifyPositiveIntegerConstantInClause(NumForLoops, OMPC_ordered);
    if (NumForLoopsResult.isInvalid())
      return nullptr;
    NumForLoops = NumForLoopsResult.get();
  } else {
    NumForLoops = nullptr;
  }
  auto *Clause =
      OMPOrderedClause::Create(getASTContext(), NumForLoops,
                               NumForLoops ? DSAStack->getAssociatedLoops() : 0,
                               StartLoc, LParenLoc, EndLoc);
  DSAStack->setOrderedRegion(/*IsOrdered=*/true, NumForLoops, Clause);
  return Clause;
}

OMPClause *SemaOpenMP::ActOnOpenMPSimpleClause(
    OpenMPClauseKind Kind, unsigned Argument, SourceLocation ArgumentLoc,
    SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc) {
  OMPClause *Res = nullptr;
  switch (Kind) {
  case OMPC_default:
    Res = ActOnOpenMPDefaultClause(static_cast<DefaultKind>(Argument),
                                   ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_proc_bind:
    Res = ActOnOpenMPProcBindClause(static_cast<ProcBindKind>(Argument),
                                    ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_atomic_default_mem_order:
    Res = ActOnOpenMPAtomicDefaultMemOrderClause(
        static_cast<OpenMPAtomicDefaultMemOrderClauseKind>(Argument),
        ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_fail:
    Res = ActOnOpenMPFailClause(static_cast<OpenMPClauseKind>(Argument),
                                ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_update:
    Res = ActOnOpenMPUpdateClause(static_cast<OpenMPDependClauseKind>(Argument),
                                  ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_bind:
    Res = ActOnOpenMPBindClause(static_cast<OpenMPBindClauseKind>(Argument),
                                ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_at:
    Res = ActOnOpenMPAtClause(static_cast<OpenMPAtClauseKind>(Argument),
                              ArgumentLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_severity:
    Res = ActOnOpenMPSeverityClause(
        static_cast<OpenMPSeverityClauseKind>(Argument), ArgumentLoc, StartLoc,
        LParenLoc, EndLoc);
    break;
  case OMPC_if:
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_collapse:
  case OMPC_schedule:
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_lastprivate:
  case OMPC_shared:
  case OMPC_reduction:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_linear:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_threadprivate:
  case OMPC_allocate:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_read:
  case OMPC_write:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_depend:
  case OMPC_device:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_map:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_grainsize:
  case OMPC_nogroup:
  case OMPC_num_tasks:
  case OMPC_hint:
  case OMPC_dist_schedule:
  case OMPC_defaultmap:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_destroy:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_detach:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_message:
  default:
    llvm_unreachable("Clause is not allowed.");
  }
  return Res;
}

static std::string
getListOfPossibleValues(OpenMPClauseKind K, unsigned First, unsigned Last,
                        ArrayRef<unsigned> Exclude = std::nullopt) {
  SmallString<256> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  unsigned Skipped = Exclude.size();
  for (unsigned I = First; I < Last; ++I) {
    if (llvm::is_contained(Exclude, I)) {
      --Skipped;
      continue;
    }
    Out << "'" << getOpenMPSimpleClauseTypeName(K, I) << "'";
    if (I + Skipped + 2 == Last)
      Out << " or ";
    else if (I + Skipped + 1 != Last)
      Out << ", ";
  }
  return std::string(Out.str());
}

OMPClause *SemaOpenMP::ActOnOpenMPDefaultClause(DefaultKind Kind,
                                                SourceLocation KindKwLoc,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation EndLoc) {
  if (Kind == OMP_DEFAULT_unknown) {
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_default, /*First=*/0,
                                   /*Last=*/unsigned(OMP_DEFAULT_unknown))
        << getOpenMPClauseName(OMPC_default);
    return nullptr;
  }

  switch (Kind) {
  case OMP_DEFAULT_none:
    DSAStack->setDefaultDSANone(KindKwLoc);
    break;
  case OMP_DEFAULT_shared:
    DSAStack->setDefaultDSAShared(KindKwLoc);
    break;
  case OMP_DEFAULT_firstprivate:
    DSAStack->setDefaultDSAFirstPrivate(KindKwLoc);
    break;
  case OMP_DEFAULT_private:
    DSAStack->setDefaultDSAPrivate(KindKwLoc);
    break;
  default:
    llvm_unreachable("DSA unexpected in OpenMP default clause");
  }

  return new (getASTContext())
      OMPDefaultClause(Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPProcBindClause(ProcBindKind Kind,
                                                 SourceLocation KindKwLoc,
                                                 SourceLocation StartLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation EndLoc) {
  if (Kind == OMP_PROC_BIND_unknown) {
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_proc_bind,
                                   /*First=*/unsigned(OMP_PROC_BIND_master),
                                   /*Last=*/
                                   unsigned(getLangOpts().OpenMP > 50
                                                ? OMP_PROC_BIND_primary
                                                : OMP_PROC_BIND_spread) +
                                       1)
        << getOpenMPClauseName(OMPC_proc_bind);
    return nullptr;
  }
  if (Kind == OMP_PROC_BIND_primary && getLangOpts().OpenMP < 51)
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_proc_bind,
                                   /*First=*/unsigned(OMP_PROC_BIND_master),
                                   /*Last=*/
                                   unsigned(OMP_PROC_BIND_spread) + 1)
        << getOpenMPClauseName(OMPC_proc_bind);
  return new (getASTContext())
      OMPProcBindClause(Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPAtomicDefaultMemOrderClause(
    OpenMPAtomicDefaultMemOrderClauseKind Kind, SourceLocation KindKwLoc,
    SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc) {
  if (Kind == OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown) {
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(
               OMPC_atomic_default_mem_order, /*First=*/0,
               /*Last=*/OMPC_ATOMIC_DEFAULT_MEM_ORDER_unknown)
        << getOpenMPClauseName(OMPC_atomic_default_mem_order);
    return nullptr;
  }
  return new (getASTContext()) OMPAtomicDefaultMemOrderClause(
      Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPAtClause(OpenMPAtClauseKind Kind,
                                           SourceLocation KindKwLoc,
                                           SourceLocation StartLoc,
                                           SourceLocation LParenLoc,
                                           SourceLocation EndLoc) {
  if (Kind == OMPC_AT_unknown) {
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_at, /*First=*/0,
                                   /*Last=*/OMPC_AT_unknown)
        << getOpenMPClauseName(OMPC_at);
    return nullptr;
  }
  return new (getASTContext())
      OMPAtClause(Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPSeverityClause(OpenMPSeverityClauseKind Kind,
                                                 SourceLocation KindKwLoc,
                                                 SourceLocation StartLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation EndLoc) {
  if (Kind == OMPC_SEVERITY_unknown) {
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_severity, /*First=*/0,
                                   /*Last=*/OMPC_SEVERITY_unknown)
        << getOpenMPClauseName(OMPC_severity);
    return nullptr;
  }
  return new (getASTContext())
      OMPSeverityClause(Kind, KindKwLoc, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPMessageClause(Expr *ME,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation EndLoc) {
  assert(ME && "NULL expr in Message clause");
  if (!isa<StringLiteral>(ME)) {
    Diag(ME->getBeginLoc(), diag::warn_clause_expected_string)
        << getOpenMPClauseName(OMPC_message);
    return nullptr;
  }
  return new (getASTContext())
      OMPMessageClause(ME, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPOrderClause(
    OpenMPOrderClauseModifier Modifier, OpenMPOrderClauseKind Kind,
    SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation MLoc,
    SourceLocation KindLoc, SourceLocation EndLoc) {
  if (Kind != OMPC_ORDER_concurrent ||
      (getLangOpts().OpenMP < 51 && MLoc.isValid())) {
    // Kind should be concurrent,
    // Modifiers introduced in OpenMP 5.1
    static_assert(OMPC_ORDER_unknown > 0,
                  "OMPC_ORDER_unknown not greater than 0");

    Diag(KindLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_order,
                                   /*First=*/0,
                                   /*Last=*/OMPC_ORDER_unknown)
        << getOpenMPClauseName(OMPC_order);
    return nullptr;
  }
  if (getLangOpts().OpenMP >= 51) {
    if (Modifier == OMPC_ORDER_MODIFIER_unknown && MLoc.isValid()) {
      Diag(MLoc, diag::err_omp_unexpected_clause_value)
          << getListOfPossibleValues(OMPC_order,
                                     /*First=*/OMPC_ORDER_MODIFIER_unknown + 1,
                                     /*Last=*/OMPC_ORDER_MODIFIER_last)
          << getOpenMPClauseName(OMPC_order);
    } else {
      DSAStack->setRegionHasOrderConcurrent(/*HasOrderConcurrent=*/true);
      if (DSAStack->getCurScope()) {
        // mark the current scope with 'order' flag
        unsigned existingFlags = DSAStack->getCurScope()->getFlags();
        DSAStack->getCurScope()->setFlags(existingFlags |
                                          Scope::OpenMPOrderClauseScope);
      }
    }
  }
  return new (getASTContext()) OMPOrderClause(
      Kind, KindLoc, StartLoc, LParenLoc, EndLoc, Modifier, MLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPUpdateClause(OpenMPDependClauseKind Kind,
                                               SourceLocation KindKwLoc,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc) {
  if (Kind == OMPC_DEPEND_unknown || Kind == OMPC_DEPEND_source ||
      Kind == OMPC_DEPEND_sink || Kind == OMPC_DEPEND_depobj) {
    SmallVector<unsigned> Except = {
        OMPC_DEPEND_source, OMPC_DEPEND_sink, OMPC_DEPEND_depobj,
        OMPC_DEPEND_outallmemory, OMPC_DEPEND_inoutallmemory};
    if (getLangOpts().OpenMP < 51)
      Except.push_back(OMPC_DEPEND_inoutset);
    Diag(KindKwLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_depend, /*First=*/0,
                                   /*Last=*/OMPC_DEPEND_unknown, Except)
        << getOpenMPClauseName(OMPC_update);
    return nullptr;
  }
  return OMPUpdateClause::Create(getASTContext(), StartLoc, LParenLoc,
                                 KindKwLoc, Kind, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPSizesClause(ArrayRef<Expr *> SizeExprs,
                                              SourceLocation StartLoc,
                                              SourceLocation LParenLoc,
                                              SourceLocation EndLoc) {
  SmallVector<Expr *> SanitizedSizeExprs(SizeExprs);

  for (Expr *&SizeExpr : SanitizedSizeExprs) {
    // Skip if already sanitized, e.g. during a partial template instantiation.
    if (!SizeExpr)
      continue;

    bool IsValid = isNonNegativeIntegerValue(SizeExpr, SemaRef, OMPC_sizes,
                                             /*StrictlyPositive=*/true);

    // isNonNegativeIntegerValue returns true for non-integral types (but still
    // emits error diagnostic), so check for the expected type explicitly.
    QualType SizeTy = SizeExpr->getType();
    if (!SizeTy->isIntegerType())
      IsValid = false;

    // Handling in templates is tricky. There are four possibilities to
    // consider:
    //
    // 1a. The expression is valid and we are in a instantiated template or not
    //     in a template:
    //       Pass valid expression to be further analysed later in Sema.
    // 1b. The expression is valid and we are in a template (including partial
    //     instantiation):
    //       isNonNegativeIntegerValue skipped any checks so there is no
    //       guarantee it will be correct after instantiation.
    //       ActOnOpenMPSizesClause will be called again at instantiation when
    //       it is not in a dependent context anymore. This may cause warnings
    //       to be emitted multiple times.
    // 2a. The expression is invalid and we are in an instantiated template or
    //     not in a template:
    //       Invalidate the expression with a clearly wrong value (nullptr) so
    //       later in Sema we do not have to do the same validity analysis again
    //       or crash from unexpected data. Error diagnostics have already been
    //       emitted.
    // 2b. The expression is invalid and we are in a template (including partial
    //     instantiation):
    //       Pass the invalid expression as-is, template instantiation may
    //       replace unexpected types/values with valid ones. The directives
    //       with this clause must not try to use these expressions in dependent
    //       contexts, but delay analysis until full instantiation.
    if (!SizeExpr->isInstantiationDependent() && !IsValid)
      SizeExpr = nullptr;
  }

  return OMPSizesClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                SanitizedSizeExprs);
}

OMPClause *SemaOpenMP::ActOnOpenMPFullClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc) {
  return OMPFullClause::Create(getASTContext(), StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPPartialClause(Expr *FactorExpr,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation EndLoc) {
  if (FactorExpr) {
    // If an argument is specified, it must be a constant (or an unevaluated
    // template expression).
    ExprResult FactorResult = VerifyPositiveIntegerConstantInClause(
        FactorExpr, OMPC_partial, /*StrictlyPositive=*/true);
    if (FactorResult.isInvalid())
      return nullptr;
    FactorExpr = FactorResult.get();
  }

  return OMPPartialClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                  FactorExpr);
}

OMPClause *SemaOpenMP::ActOnOpenMPAlignClause(Expr *A, SourceLocation StartLoc,
                                              SourceLocation LParenLoc,
                                              SourceLocation EndLoc) {
  ExprResult AlignVal;
  AlignVal = VerifyPositiveIntegerConstantInClause(A, OMPC_align);
  if (AlignVal.isInvalid())
    return nullptr;
  return OMPAlignClause::Create(getASTContext(), AlignVal.get(), StartLoc,
                                LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPSingleExprWithArgClause(
    OpenMPClauseKind Kind, ArrayRef<unsigned> Argument, Expr *Expr,
    SourceLocation StartLoc, SourceLocation LParenLoc,
    ArrayRef<SourceLocation> ArgumentLoc, SourceLocation DelimLoc,
    SourceLocation EndLoc) {
  OMPClause *Res = nullptr;
  switch (Kind) {
  case OMPC_schedule:
    enum { Modifier1, Modifier2, ScheduleKind, NumberOfElements };
    assert(Argument.size() == NumberOfElements &&
           ArgumentLoc.size() == NumberOfElements);
    Res = ActOnOpenMPScheduleClause(
        static_cast<OpenMPScheduleClauseModifier>(Argument[Modifier1]),
        static_cast<OpenMPScheduleClauseModifier>(Argument[Modifier2]),
        static_cast<OpenMPScheduleClauseKind>(Argument[ScheduleKind]), Expr,
        StartLoc, LParenLoc, ArgumentLoc[Modifier1], ArgumentLoc[Modifier2],
        ArgumentLoc[ScheduleKind], DelimLoc, EndLoc);
    break;
  case OMPC_if:
    assert(Argument.size() == 1 && ArgumentLoc.size() == 1);
    Res = ActOnOpenMPIfClause(static_cast<OpenMPDirectiveKind>(Argument.back()),
                              Expr, StartLoc, LParenLoc, ArgumentLoc.back(),
                              DelimLoc, EndLoc);
    break;
  case OMPC_dist_schedule:
    Res = ActOnOpenMPDistScheduleClause(
        static_cast<OpenMPDistScheduleClauseKind>(Argument.back()), Expr,
        StartLoc, LParenLoc, ArgumentLoc.back(), DelimLoc, EndLoc);
    break;
  case OMPC_defaultmap:
    enum { Modifier, DefaultmapKind };
    Res = ActOnOpenMPDefaultmapClause(
        static_cast<OpenMPDefaultmapClauseModifier>(Argument[Modifier]),
        static_cast<OpenMPDefaultmapClauseKind>(Argument[DefaultmapKind]),
        StartLoc, LParenLoc, ArgumentLoc[Modifier], ArgumentLoc[DefaultmapKind],
        EndLoc);
    break;
  case OMPC_order:
    enum { OrderModifier, OrderKind };
    Res = ActOnOpenMPOrderClause(
        static_cast<OpenMPOrderClauseModifier>(Argument[OrderModifier]),
        static_cast<OpenMPOrderClauseKind>(Argument[OrderKind]), StartLoc,
        LParenLoc, ArgumentLoc[OrderModifier], ArgumentLoc[OrderKind], EndLoc);
    break;
  case OMPC_device:
    assert(Argument.size() == 1 && ArgumentLoc.size() == 1);
    Res = ActOnOpenMPDeviceClause(
        static_cast<OpenMPDeviceClauseModifier>(Argument.back()), Expr,
        StartLoc, LParenLoc, ArgumentLoc.back(), EndLoc);
    break;
  case OMPC_grainsize:
    assert(Argument.size() == 1 && ArgumentLoc.size() == 1 &&
           "Modifier for grainsize clause and its location are expected.");
    Res = ActOnOpenMPGrainsizeClause(
        static_cast<OpenMPGrainsizeClauseModifier>(Argument.back()), Expr,
        StartLoc, LParenLoc, ArgumentLoc.back(), EndLoc);
    break;
  case OMPC_num_tasks:
    assert(Argument.size() == 1 && ArgumentLoc.size() == 1 &&
           "Modifier for num_tasks clause and its location are expected.");
    Res = ActOnOpenMPNumTasksClause(
        static_cast<OpenMPNumTasksClauseModifier>(Argument.back()), Expr,
        StartLoc, LParenLoc, ArgumentLoc.back(), EndLoc);
    break;
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_collapse:
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_lastprivate:
  case OMPC_shared:
  case OMPC_reduction:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_linear:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_threadprivate:
  case OMPC_allocate:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_read:
  case OMPC_write:
  case OMPC_update:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_depend:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_map:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_nogroup:
  case OMPC_hint:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_atomic_default_mem_order:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_message:
  case OMPC_destroy:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_detach:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_bind:
  default:
    llvm_unreachable("Clause is not allowed.");
  }
  return Res;
}

static bool checkScheduleModifiers(Sema &S, OpenMPScheduleClauseModifier M1,
                                   OpenMPScheduleClauseModifier M2,
                                   SourceLocation M1Loc, SourceLocation M2Loc) {
  if (M1 == OMPC_SCHEDULE_MODIFIER_unknown && M1Loc.isValid()) {
    SmallVector<unsigned, 2> Excluded;
    if (M2 != OMPC_SCHEDULE_MODIFIER_unknown)
      Excluded.push_back(M2);
    if (M2 == OMPC_SCHEDULE_MODIFIER_nonmonotonic)
      Excluded.push_back(OMPC_SCHEDULE_MODIFIER_monotonic);
    if (M2 == OMPC_SCHEDULE_MODIFIER_monotonic)
      Excluded.push_back(OMPC_SCHEDULE_MODIFIER_nonmonotonic);
    S.Diag(M1Loc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_schedule,
                                   /*First=*/OMPC_SCHEDULE_MODIFIER_unknown + 1,
                                   /*Last=*/OMPC_SCHEDULE_MODIFIER_last,
                                   Excluded)
        << getOpenMPClauseName(OMPC_schedule);
    return true;
  }
  return false;
}

OMPClause *SemaOpenMP::ActOnOpenMPScheduleClause(
    OpenMPScheduleClauseModifier M1, OpenMPScheduleClauseModifier M2,
    OpenMPScheduleClauseKind Kind, Expr *ChunkSize, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation M1Loc, SourceLocation M2Loc,
    SourceLocation KindLoc, SourceLocation CommaLoc, SourceLocation EndLoc) {
  if (checkScheduleModifiers(SemaRef, M1, M2, M1Loc, M2Loc) ||
      checkScheduleModifiers(SemaRef, M2, M1, M2Loc, M1Loc))
    return nullptr;
  // OpenMP, 2.7.1, Loop Construct, Restrictions
  // Either the monotonic modifier or the nonmonotonic modifier can be specified
  // but not both.
  if ((M1 == M2 && M1 != OMPC_SCHEDULE_MODIFIER_unknown) ||
      (M1 == OMPC_SCHEDULE_MODIFIER_monotonic &&
       M2 == OMPC_SCHEDULE_MODIFIER_nonmonotonic) ||
      (M1 == OMPC_SCHEDULE_MODIFIER_nonmonotonic &&
       M2 == OMPC_SCHEDULE_MODIFIER_monotonic)) {
    Diag(M2Loc, diag::err_omp_unexpected_schedule_modifier)
        << getOpenMPSimpleClauseTypeName(OMPC_schedule, M2)
        << getOpenMPSimpleClauseTypeName(OMPC_schedule, M1);
    return nullptr;
  }
  if (Kind == OMPC_SCHEDULE_unknown) {
    std::string Values;
    if (M1Loc.isInvalid() && M2Loc.isInvalid()) {
      unsigned Exclude[] = {OMPC_SCHEDULE_unknown};
      Values = getListOfPossibleValues(OMPC_schedule, /*First=*/0,
                                       /*Last=*/OMPC_SCHEDULE_MODIFIER_last,
                                       Exclude);
    } else {
      Values = getListOfPossibleValues(OMPC_schedule, /*First=*/0,
                                       /*Last=*/OMPC_SCHEDULE_unknown);
    }
    Diag(KindLoc, diag::err_omp_unexpected_clause_value)
        << Values << getOpenMPClauseName(OMPC_schedule);
    return nullptr;
  }
  // OpenMP, 2.7.1, Loop Construct, Restrictions
  // The nonmonotonic modifier can only be specified with schedule(dynamic) or
  // schedule(guided).
  // OpenMP 5.0 does not have this restriction.
  if (getLangOpts().OpenMP < 50 &&
      (M1 == OMPC_SCHEDULE_MODIFIER_nonmonotonic ||
       M2 == OMPC_SCHEDULE_MODIFIER_nonmonotonic) &&
      Kind != OMPC_SCHEDULE_dynamic && Kind != OMPC_SCHEDULE_guided) {
    Diag(M1 == OMPC_SCHEDULE_MODIFIER_nonmonotonic ? M1Loc : M2Loc,
         diag::err_omp_schedule_nonmonotonic_static);
    return nullptr;
  }
  Expr *ValExpr = ChunkSize;
  Stmt *HelperValStmt = nullptr;
  if (ChunkSize) {
    if (!ChunkSize->isValueDependent() && !ChunkSize->isTypeDependent() &&
        !ChunkSize->isInstantiationDependent() &&
        !ChunkSize->containsUnexpandedParameterPack()) {
      SourceLocation ChunkSizeLoc = ChunkSize->getBeginLoc();
      ExprResult Val =
          PerformOpenMPImplicitIntegerConversion(ChunkSizeLoc, ChunkSize);
      if (Val.isInvalid())
        return nullptr;

      ValExpr = Val.get();

      // OpenMP [2.7.1, Restrictions]
      //  chunk_size must be a loop invariant integer expression with a positive
      //  value.
      if (std::optional<llvm::APSInt> Result =
              ValExpr->getIntegerConstantExpr(getASTContext())) {
        if (Result->isSigned() && !Result->isStrictlyPositive()) {
          Diag(ChunkSizeLoc, diag::err_omp_negative_expression_in_clause)
              << "schedule" << 1 << ChunkSize->getSourceRange();
          return nullptr;
        }
      } else if (getOpenMPCaptureRegionForClause(
                     DSAStack->getCurrentDirective(), OMPC_schedule,
                     getLangOpts().OpenMP) != OMPD_unknown &&
                 !SemaRef.CurContext->isDependentContext()) {
        ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
        llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
        ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
        HelperValStmt = buildPreInits(getASTContext(), Captures);
      }
    }
  }

  return new (getASTContext())
      OMPScheduleClause(StartLoc, LParenLoc, KindLoc, CommaLoc, EndLoc, Kind,
                        ValExpr, HelperValStmt, M1, M1Loc, M2, M2Loc);
}

OMPClause *SemaOpenMP::ActOnOpenMPClause(OpenMPClauseKind Kind,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc) {
  OMPClause *Res = nullptr;
  switch (Kind) {
  case OMPC_ordered:
    Res = ActOnOpenMPOrderedClause(StartLoc, EndLoc);
    break;
  case OMPC_nowait:
    Res = ActOnOpenMPNowaitClause(StartLoc, EndLoc);
    break;
  case OMPC_untied:
    Res = ActOnOpenMPUntiedClause(StartLoc, EndLoc);
    break;
  case OMPC_mergeable:
    Res = ActOnOpenMPMergeableClause(StartLoc, EndLoc);
    break;
  case OMPC_read:
    Res = ActOnOpenMPReadClause(StartLoc, EndLoc);
    break;
  case OMPC_write:
    Res = ActOnOpenMPWriteClause(StartLoc, EndLoc);
    break;
  case OMPC_update:
    Res = ActOnOpenMPUpdateClause(StartLoc, EndLoc);
    break;
  case OMPC_capture:
    Res = ActOnOpenMPCaptureClause(StartLoc, EndLoc);
    break;
  case OMPC_compare:
    Res = ActOnOpenMPCompareClause(StartLoc, EndLoc);
    break;
  case OMPC_fail:
    Res = ActOnOpenMPFailClause(StartLoc, EndLoc);
    break;
  case OMPC_seq_cst:
    Res = ActOnOpenMPSeqCstClause(StartLoc, EndLoc);
    break;
  case OMPC_acq_rel:
    Res = ActOnOpenMPAcqRelClause(StartLoc, EndLoc);
    break;
  case OMPC_acquire:
    Res = ActOnOpenMPAcquireClause(StartLoc, EndLoc);
    break;
  case OMPC_release:
    Res = ActOnOpenMPReleaseClause(StartLoc, EndLoc);
    break;
  case OMPC_relaxed:
    Res = ActOnOpenMPRelaxedClause(StartLoc, EndLoc);
    break;
  case OMPC_weak:
    Res = ActOnOpenMPWeakClause(StartLoc, EndLoc);
    break;
  case OMPC_threads:
    Res = ActOnOpenMPThreadsClause(StartLoc, EndLoc);
    break;
  case OMPC_simd:
    Res = ActOnOpenMPSIMDClause(StartLoc, EndLoc);
    break;
  case OMPC_nogroup:
    Res = ActOnOpenMPNogroupClause(StartLoc, EndLoc);
    break;
  case OMPC_unified_address:
    Res = ActOnOpenMPUnifiedAddressClause(StartLoc, EndLoc);
    break;
  case OMPC_unified_shared_memory:
    Res = ActOnOpenMPUnifiedSharedMemoryClause(StartLoc, EndLoc);
    break;
  case OMPC_reverse_offload:
    Res = ActOnOpenMPReverseOffloadClause(StartLoc, EndLoc);
    break;
  case OMPC_dynamic_allocators:
    Res = ActOnOpenMPDynamicAllocatorsClause(StartLoc, EndLoc);
    break;
  case OMPC_destroy:
    Res = ActOnOpenMPDestroyClause(/*InteropVar=*/nullptr, StartLoc,
                                   /*LParenLoc=*/SourceLocation(),
                                   /*VarLoc=*/SourceLocation(), EndLoc);
    break;
  case OMPC_full:
    Res = ActOnOpenMPFullClause(StartLoc, EndLoc);
    break;
  case OMPC_partial:
    Res = ActOnOpenMPPartialClause(nullptr, StartLoc, /*LParenLoc=*/{}, EndLoc);
    break;
  case OMPC_ompx_bare:
    Res = ActOnOpenMPXBareClause(StartLoc, EndLoc);
    break;
  case OMPC_if:
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_collapse:
  case OMPC_schedule:
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_lastprivate:
  case OMPC_shared:
  case OMPC_reduction:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_linear:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_threadprivate:
  case OMPC_allocate:
  case OMPC_flush:
  case OMPC_depobj:
  case OMPC_depend:
  case OMPC_device:
  case OMPC_map:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_grainsize:
  case OMPC_num_tasks:
  case OMPC_hint:
  case OMPC_dist_schedule:
  case OMPC_defaultmap:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_atomic_default_mem_order:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_nontemporal:
  case OMPC_order:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_message:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_detach:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_uses_allocators:
  case OMPC_affinity:
  case OMPC_when:
  case OMPC_ompx_dyn_cgroup_mem:
  default:
    llvm_unreachable("Clause is not allowed.");
  }
  return Res;
}

OMPClause *SemaOpenMP::ActOnOpenMPNowaitClause(SourceLocation StartLoc,
                                               SourceLocation EndLoc) {
  DSAStack->setNowaitRegion();
  return new (getASTContext()) OMPNowaitClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPUntiedClause(SourceLocation StartLoc,
                                               SourceLocation EndLoc) {
  DSAStack->setUntiedRegion();
  return new (getASTContext()) OMPUntiedClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPMergeableClause(SourceLocation StartLoc,
                                                  SourceLocation EndLoc) {
  return new (getASTContext()) OMPMergeableClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPReadClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc) {
  return new (getASTContext()) OMPReadClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPWriteClause(SourceLocation StartLoc,
                                              SourceLocation EndLoc) {
  return new (getASTContext()) OMPWriteClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPUpdateClause(SourceLocation StartLoc,
                                               SourceLocation EndLoc) {
  return OMPUpdateClause::Create(getASTContext(), StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPCaptureClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPCaptureClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPCompareClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPCompareClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPFailClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc) {
  return new (getASTContext()) OMPFailClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPFailClause(OpenMPClauseKind Parameter,
                                             SourceLocation KindLoc,
                                             SourceLocation StartLoc,
                                             SourceLocation LParenLoc,
                                             SourceLocation EndLoc) {

  if (!checkFailClauseParameter(Parameter)) {
    Diag(KindLoc, diag::err_omp_atomic_fail_wrong_or_no_clauses);
    return nullptr;
  }
  return new (getASTContext())
      OMPFailClause(Parameter, KindLoc, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPSeqCstClause(SourceLocation StartLoc,
                                               SourceLocation EndLoc) {
  return new (getASTContext()) OMPSeqCstClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPAcqRelClause(SourceLocation StartLoc,
                                               SourceLocation EndLoc) {
  return new (getASTContext()) OMPAcqRelClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPAcquireClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPAcquireClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPReleaseClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPReleaseClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPRelaxedClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPRelaxedClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPWeakClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc) {
  return new (getASTContext()) OMPWeakClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPThreadsClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPThreadsClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPSIMDClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc) {
  return new (getASTContext()) OMPSIMDClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPNogroupClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc) {
  return new (getASTContext()) OMPNogroupClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPUnifiedAddressClause(SourceLocation StartLoc,
                                                       SourceLocation EndLoc) {
  return new (getASTContext()) OMPUnifiedAddressClause(StartLoc, EndLoc);
}

OMPClause *
SemaOpenMP::ActOnOpenMPUnifiedSharedMemoryClause(SourceLocation StartLoc,
                                                 SourceLocation EndLoc) {
  return new (getASTContext()) OMPUnifiedSharedMemoryClause(StartLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPReverseOffloadClause(SourceLocation StartLoc,
                                                       SourceLocation EndLoc) {
  return new (getASTContext()) OMPReverseOffloadClause(StartLoc, EndLoc);
}

OMPClause *
SemaOpenMP::ActOnOpenMPDynamicAllocatorsClause(SourceLocation StartLoc,
                                               SourceLocation EndLoc) {
  return new (getASTContext()) OMPDynamicAllocatorsClause(StartLoc, EndLoc);
}

StmtResult
SemaOpenMP::ActOnOpenMPInteropDirective(ArrayRef<OMPClause *> Clauses,
                                        SourceLocation StartLoc,
                                        SourceLocation EndLoc) {

  // OpenMP 5.1 [2.15.1, interop Construct, Restrictions]
  // At least one action-clause must appear on a directive.
  if (!hasClauses(Clauses, OMPC_init, OMPC_use, OMPC_destroy, OMPC_nowait)) {
    StringRef Expected = "'init', 'use', 'destroy', or 'nowait'";
    Diag(StartLoc, diag::err_omp_no_clause_for_directive)
        << Expected << getOpenMPDirectiveName(OMPD_interop);
    return StmtError();
  }

  // OpenMP 5.1 [2.15.1, interop Construct, Restrictions]
  // A depend clause can only appear on the directive if a targetsync
  // interop-type is present or the interop-var was initialized with
  // the targetsync interop-type.

  // If there is any 'init' clause diagnose if there is no 'init' clause with
  // interop-type of 'targetsync'. Cases involving other directives cannot be
  // diagnosed.
  const OMPDependClause *DependClause = nullptr;
  bool HasInitClause = false;
  bool IsTargetSync = false;
  for (const OMPClause *C : Clauses) {
    if (IsTargetSync)
      break;
    if (const auto *InitClause = dyn_cast<OMPInitClause>(C)) {
      HasInitClause = true;
      if (InitClause->getIsTargetSync())
        IsTargetSync = true;
    } else if (const auto *DC = dyn_cast<OMPDependClause>(C)) {
      DependClause = DC;
    }
  }
  if (DependClause && HasInitClause && !IsTargetSync) {
    Diag(DependClause->getBeginLoc(), diag::err_omp_interop_bad_depend_clause);
    return StmtError();
  }

  // OpenMP 5.1 [2.15.1, interop Construct, Restrictions]
  // Each interop-var may be specified for at most one action-clause of each
  // interop construct.
  llvm::SmallPtrSet<const ValueDecl *, 4> InteropVars;
  for (OMPClause *C : Clauses) {
    OpenMPClauseKind ClauseKind = C->getClauseKind();
    std::pair<ValueDecl *, bool> DeclResult;
    SourceLocation ELoc;
    SourceRange ERange;

    if (ClauseKind == OMPC_init) {
      auto *E = cast<OMPInitClause>(C)->getInteropVar();
      DeclResult = getPrivateItem(SemaRef, E, ELoc, ERange);
    } else if (ClauseKind == OMPC_use) {
      auto *E = cast<OMPUseClause>(C)->getInteropVar();
      DeclResult = getPrivateItem(SemaRef, E, ELoc, ERange);
    } else if (ClauseKind == OMPC_destroy) {
      auto *E = cast<OMPDestroyClause>(C)->getInteropVar();
      DeclResult = getPrivateItem(SemaRef, E, ELoc, ERange);
    }

    if (DeclResult.first) {
      if (!InteropVars.insert(DeclResult.first).second) {
        Diag(ELoc, diag::err_omp_interop_var_multiple_actions)
            << DeclResult.first;
        return StmtError();
      }
    }
  }

  return OMPInteropDirective::Create(getASTContext(), StartLoc, EndLoc,
                                     Clauses);
}

static bool isValidInteropVariable(Sema &SemaRef, Expr *InteropVarExpr,
                                   SourceLocation VarLoc,
                                   OpenMPClauseKind Kind) {
  SourceLocation ELoc;
  SourceRange ERange;
  Expr *RefExpr = InteropVarExpr;
  auto Res =
      getPrivateItem(SemaRef, RefExpr, ELoc, ERange,
                     /*AllowArraySection=*/false, /*DiagType=*/"omp_interop_t");

  if (Res.second) {
    // It will be analyzed later.
    return true;
  }

  if (!Res.first)
    return false;

  // Interop variable should be of type omp_interop_t.
  bool HasError = false;
  QualType InteropType;
  LookupResult Result(SemaRef, &SemaRef.Context.Idents.get("omp_interop_t"),
                      VarLoc, Sema::LookupOrdinaryName);
  if (SemaRef.LookupName(Result, SemaRef.getCurScope())) {
    NamedDecl *ND = Result.getFoundDecl();
    if (const auto *TD = dyn_cast<TypeDecl>(ND)) {
      InteropType = QualType(TD->getTypeForDecl(), 0);
    } else {
      HasError = true;
    }
  } else {
    HasError = true;
  }

  if (HasError) {
    SemaRef.Diag(VarLoc, diag::err_omp_implied_type_not_found)
        << "omp_interop_t";
    return false;
  }

  QualType VarType = InteropVarExpr->getType().getUnqualifiedType();
  if (!SemaRef.Context.hasSameType(InteropType, VarType)) {
    SemaRef.Diag(VarLoc, diag::err_omp_interop_variable_wrong_type);
    return false;
  }

  // OpenMP 5.1 [2.15.1, interop Construct, Restrictions]
  // The interop-var passed to init or destroy must be non-const.
  if ((Kind == OMPC_init || Kind == OMPC_destroy) &&
      isConstNotMutableType(SemaRef, InteropVarExpr->getType())) {
    SemaRef.Diag(VarLoc, diag::err_omp_interop_variable_expected)
        << /*non-const*/ 1;
    return false;
  }
  return true;
}

OMPClause *SemaOpenMP::ActOnOpenMPInitClause(
    Expr *InteropVar, OMPInteropInfo &InteropInfo, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation VarLoc, SourceLocation EndLoc) {

  if (!isValidInteropVariable(SemaRef, InteropVar, VarLoc, OMPC_init))
    return nullptr;

  // Check prefer_type values.  These foreign-runtime-id values are either
  // string literals or constant integral expressions.
  for (const Expr *E : InteropInfo.PreferTypes) {
    if (E->isValueDependent() || E->isTypeDependent() ||
        E->isInstantiationDependent() || E->containsUnexpandedParameterPack())
      continue;
    if (E->isIntegerConstantExpr(getASTContext()))
      continue;
    if (isa<StringLiteral>(E))
      continue;
    Diag(E->getExprLoc(), diag::err_omp_interop_prefer_type);
    return nullptr;
  }

  return OMPInitClause::Create(getASTContext(), InteropVar, InteropInfo,
                               StartLoc, LParenLoc, VarLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPUseClause(Expr *InteropVar,
                                            SourceLocation StartLoc,
                                            SourceLocation LParenLoc,
                                            SourceLocation VarLoc,
                                            SourceLocation EndLoc) {

  if (!isValidInteropVariable(SemaRef, InteropVar, VarLoc, OMPC_use))
    return nullptr;

  return new (getASTContext())
      OMPUseClause(InteropVar, StartLoc, LParenLoc, VarLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPDestroyClause(Expr *InteropVar,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation VarLoc,
                                                SourceLocation EndLoc) {
  if (!InteropVar && getLangOpts().OpenMP >= 52 &&
      DSAStack->getCurrentDirective() == OMPD_depobj) {
    Diag(StartLoc, diag::err_omp_expected_clause_argument)
        << getOpenMPClauseName(OMPC_destroy)
        << getOpenMPDirectiveName(OMPD_depobj);
    return nullptr;
  }
  if (InteropVar &&
      !isValidInteropVariable(SemaRef, InteropVar, VarLoc, OMPC_destroy))
    return nullptr;

  return new (getASTContext())
      OMPDestroyClause(InteropVar, StartLoc, LParenLoc, VarLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPNovariantsClause(Expr *Condition,
                                                   SourceLocation StartLoc,
                                                   SourceLocation LParenLoc,
                                                   SourceLocation EndLoc) {
  Expr *ValExpr = Condition;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;
  if (!Condition->isValueDependent() && !Condition->isTypeDependent() &&
      !Condition->isInstantiationDependent() &&
      !Condition->containsUnexpandedParameterPack()) {
    ExprResult Val = SemaRef.CheckBooleanCondition(StartLoc, Condition);
    if (Val.isInvalid())
      return nullptr;

    ValExpr = SemaRef.MakeFullExpr(Val.get()).get();

    OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
    CaptureRegion = getOpenMPCaptureRegionForClause(DKind, OMPC_novariants,
                                                    getLangOpts().OpenMP);
    if (CaptureRegion != OMPD_unknown &&
        !SemaRef.CurContext->isDependentContext()) {
      ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
      llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
      ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
      HelperValStmt = buildPreInits(getASTContext(), Captures);
    }
  }

  return new (getASTContext()) OMPNovariantsClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPNocontextClause(Expr *Condition,
                                                  SourceLocation StartLoc,
                                                  SourceLocation LParenLoc,
                                                  SourceLocation EndLoc) {
  Expr *ValExpr = Condition;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;
  if (!Condition->isValueDependent() && !Condition->isTypeDependent() &&
      !Condition->isInstantiationDependent() &&
      !Condition->containsUnexpandedParameterPack()) {
    ExprResult Val = SemaRef.CheckBooleanCondition(StartLoc, Condition);
    if (Val.isInvalid())
      return nullptr;

    ValExpr = SemaRef.MakeFullExpr(Val.get()).get();

    OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
    CaptureRegion = getOpenMPCaptureRegionForClause(DKind, OMPC_nocontext,
                                                    getLangOpts().OpenMP);
    if (CaptureRegion != OMPD_unknown &&
        !SemaRef.CurContext->isDependentContext()) {
      ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
      llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
      ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
      HelperValStmt = buildPreInits(getASTContext(), Captures);
    }
  }

  return new (getASTContext()) OMPNocontextClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPFilterClause(Expr *ThreadID,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc) {
  Expr *ValExpr = ThreadID;
  Stmt *HelperValStmt = nullptr;

  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  OpenMPDirectiveKind CaptureRegion =
      getOpenMPCaptureRegionForClause(DKind, OMPC_filter, getLangOpts().OpenMP);
  if (CaptureRegion != OMPD_unknown &&
      !SemaRef.CurContext->isDependentContext()) {
    ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
    llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
    ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
    HelperValStmt = buildPreInits(getASTContext(), Captures);
  }

  return new (getASTContext()) OMPFilterClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPVarListClause(OpenMPClauseKind Kind,
                                                ArrayRef<Expr *> VarList,
                                                const OMPVarListLocTy &Locs,
                                                OpenMPVarListDataTy &Data) {
  SourceLocation StartLoc = Locs.StartLoc;
  SourceLocation LParenLoc = Locs.LParenLoc;
  SourceLocation EndLoc = Locs.EndLoc;
  OMPClause *Res = nullptr;
  int ExtraModifier = Data.ExtraModifier;
  SourceLocation ExtraModifierLoc = Data.ExtraModifierLoc;
  SourceLocation ColonLoc = Data.ColonLoc;
  switch (Kind) {
  case OMPC_private:
    Res = ActOnOpenMPPrivateClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_firstprivate:
    Res = ActOnOpenMPFirstprivateClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_lastprivate:
    assert(0 <= ExtraModifier && ExtraModifier <= OMPC_LASTPRIVATE_unknown &&
           "Unexpected lastprivate modifier.");
    Res = ActOnOpenMPLastprivateClause(
        VarList, static_cast<OpenMPLastprivateModifier>(ExtraModifier),
        ExtraModifierLoc, ColonLoc, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_shared:
    Res = ActOnOpenMPSharedClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_reduction:
    assert(0 <= ExtraModifier && ExtraModifier <= OMPC_REDUCTION_unknown &&
           "Unexpected lastprivate modifier.");
    Res = ActOnOpenMPReductionClause(
        VarList, static_cast<OpenMPReductionClauseModifier>(ExtraModifier),
        StartLoc, LParenLoc, ExtraModifierLoc, ColonLoc, EndLoc,
        Data.ReductionOrMapperIdScopeSpec, Data.ReductionOrMapperId);
    break;
  case OMPC_task_reduction:
    Res = ActOnOpenMPTaskReductionClause(
        VarList, StartLoc, LParenLoc, ColonLoc, EndLoc,
        Data.ReductionOrMapperIdScopeSpec, Data.ReductionOrMapperId);
    break;
  case OMPC_in_reduction:
    Res = ActOnOpenMPInReductionClause(
        VarList, StartLoc, LParenLoc, ColonLoc, EndLoc,
        Data.ReductionOrMapperIdScopeSpec, Data.ReductionOrMapperId);
    break;
  case OMPC_linear:
    assert(0 <= ExtraModifier && ExtraModifier <= OMPC_LINEAR_unknown &&
           "Unexpected linear modifier.");
    Res = ActOnOpenMPLinearClause(
        VarList, Data.DepModOrTailExpr, StartLoc, LParenLoc,
        static_cast<OpenMPLinearClauseKind>(ExtraModifier), ExtraModifierLoc,
        ColonLoc, Data.StepModifierLoc, EndLoc);
    break;
  case OMPC_aligned:
    Res = ActOnOpenMPAlignedClause(VarList, Data.DepModOrTailExpr, StartLoc,
                                   LParenLoc, ColonLoc, EndLoc);
    break;
  case OMPC_copyin:
    Res = ActOnOpenMPCopyinClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_copyprivate:
    Res = ActOnOpenMPCopyprivateClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_flush:
    Res = ActOnOpenMPFlushClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_depend:
    assert(0 <= ExtraModifier && ExtraModifier <= OMPC_DEPEND_unknown &&
           "Unexpected depend modifier.");
    Res = ActOnOpenMPDependClause(
        {static_cast<OpenMPDependClauseKind>(ExtraModifier), ExtraModifierLoc,
         ColonLoc, Data.OmpAllMemoryLoc},
        Data.DepModOrTailExpr, VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_map:
    assert(0 <= ExtraModifier && ExtraModifier <= OMPC_MAP_unknown &&
           "Unexpected map modifier.");
    Res = ActOnOpenMPMapClause(
        Data.IteratorExpr, Data.MapTypeModifiers, Data.MapTypeModifiersLoc,
        Data.ReductionOrMapperIdScopeSpec, Data.ReductionOrMapperId,
        static_cast<OpenMPMapClauseKind>(ExtraModifier), Data.IsMapTypeImplicit,
        ExtraModifierLoc, ColonLoc, VarList, Locs);
    break;
  case OMPC_to:
    Res =
        ActOnOpenMPToClause(Data.MotionModifiers, Data.MotionModifiersLoc,
                            Data.ReductionOrMapperIdScopeSpec,
                            Data.ReductionOrMapperId, ColonLoc, VarList, Locs);
    break;
  case OMPC_from:
    Res = ActOnOpenMPFromClause(Data.MotionModifiers, Data.MotionModifiersLoc,
                                Data.ReductionOrMapperIdScopeSpec,
                                Data.ReductionOrMapperId, ColonLoc, VarList,
                                Locs);
    break;
  case OMPC_use_device_ptr:
    Res = ActOnOpenMPUseDevicePtrClause(VarList, Locs);
    break;
  case OMPC_use_device_addr:
    Res = ActOnOpenMPUseDeviceAddrClause(VarList, Locs);
    break;
  case OMPC_is_device_ptr:
    Res = ActOnOpenMPIsDevicePtrClause(VarList, Locs);
    break;
  case OMPC_has_device_addr:
    Res = ActOnOpenMPHasDeviceAddrClause(VarList, Locs);
    break;
  case OMPC_allocate:
    Res = ActOnOpenMPAllocateClause(Data.DepModOrTailExpr, VarList, StartLoc,
                                    LParenLoc, ColonLoc, EndLoc);
    break;
  case OMPC_nontemporal:
    Res = ActOnOpenMPNontemporalClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_inclusive:
    Res = ActOnOpenMPInclusiveClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_exclusive:
    Res = ActOnOpenMPExclusiveClause(VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_affinity:
    Res = ActOnOpenMPAffinityClause(StartLoc, LParenLoc, ColonLoc, EndLoc,
                                    Data.DepModOrTailExpr, VarList);
    break;
  case OMPC_doacross:
    Res = ActOnOpenMPDoacrossClause(
        static_cast<OpenMPDoacrossClauseModifier>(ExtraModifier),
        ExtraModifierLoc, ColonLoc, VarList, StartLoc, LParenLoc, EndLoc);
    break;
  case OMPC_if:
  case OMPC_depobj:
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_sizes:
  case OMPC_allocator:
  case OMPC_collapse:
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_schedule:
  case OMPC_ordered:
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_threadprivate:
  case OMPC_read:
  case OMPC_write:
  case OMPC_update:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_device:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_grainsize:
  case OMPC_nogroup:
  case OMPC_num_tasks:
  case OMPC_hint:
  case OMPC_dist_schedule:
  case OMPC_defaultmap:
  case OMPC_unknown:
  case OMPC_uniform:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_atomic_default_mem_order:
  case OMPC_device_type:
  case OMPC_match:
  case OMPC_order:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_message:
  case OMPC_destroy:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_detach:
  case OMPC_uses_allocators:
  case OMPC_when:
  case OMPC_bind:
  default:
    llvm_unreachable("Clause is not allowed.");
  }
  return Res;
}

ExprResult SemaOpenMP::getOpenMPCapturedExpr(VarDecl *Capture, ExprValueKind VK,
                                             ExprObjectKind OK,
                                             SourceLocation Loc) {
  ExprResult Res = SemaRef.BuildDeclRefExpr(
      Capture, Capture->getType().getNonReferenceType(), VK_LValue, Loc);
  if (!Res.isUsable())
    return ExprError();
  if (OK == OK_Ordinary && !getLangOpts().CPlusPlus) {
    Res = SemaRef.CreateBuiltinUnaryOp(Loc, UO_Deref, Res.get());
    if (!Res.isUsable())
      return ExprError();
  }
  if (VK != VK_LValue && Res.get()->isGLValue()) {
    Res = SemaRef.DefaultLvalueConversion(Res.get());
    if (!Res.isUsable())
      return ExprError();
  }
  return Res;
}

OMPClause *SemaOpenMP::ActOnOpenMPPrivateClause(ArrayRef<Expr *> VarList,
                                                SourceLocation StartLoc,
                                                SourceLocation LParenLoc,
                                                SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  SmallVector<Expr *, 8> PrivateCopies;
  bool IsImplicitClause =
      StartLoc.isInvalid() && LParenLoc.isInvalid() && EndLoc.isInvalid();
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP private clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      PrivateCopies.push_back(nullptr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType Type = D->getType();
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP [2.9.3.3, Restrictions, C/C++, p.3]
    //  A variable that appears in a private clause must not have an incomplete
    //  type or a reference type.
    if (SemaRef.RequireCompleteType(ELoc, Type,
                                    diag::err_omp_private_incomplete_type))
      continue;
    Type = Type.getNonReferenceType();

    // OpenMP 5.0 [2.19.3, List Item Privatization, Restrictions]
    // A variable that is privatized must not have a const-qualified type
    // unless it is of class type with a mutable member. This restriction does
    // not apply to the firstprivate clause.
    //
    // OpenMP 3.1 [2.9.3.3, private clause, Restrictions]
    // A variable that appears in a private clause must not have a
    // const-qualified type unless it is of class type with a mutable member.
    if (rejectConstNotMutableType(SemaRef, D, Type, OMPC_private, ELoc))
      continue;

    // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a Construct]
    //  Variables with the predetermined data-sharing attributes may not be
    //  listed in data-sharing attributes clauses, except for the cases
    //  listed below. For these exceptions only, listing a predetermined
    //  variable in a data-sharing attribute clause is allowed and overrides
    //  the variable's predetermined data-sharing attributes.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(D, /*FromParent=*/false);
    if (DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_private) {
      Diag(ELoc, diag::err_omp_wrong_dsa) << getOpenMPClauseName(DVar.CKind)
                                          << getOpenMPClauseName(OMPC_private);
      reportOriginalDsa(SemaRef, DSAStack, D, DVar);
      continue;
    }

    OpenMPDirectiveKind CurrDir = DSAStack->getCurrentDirective();
    // Variably modified types are not supported for tasks.
    if (!Type->isAnyPointerType() && Type->isVariablyModifiedType() &&
        isOpenMPTaskingDirective(CurrDir)) {
      Diag(ELoc, diag::err_omp_variably_modified_type_not_supported)
          << getOpenMPClauseName(OMPC_private) << Type
          << getOpenMPDirectiveName(CurrDir);
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(getASTContext()) ==
                               VarDecl::DeclarationOnly;
      Diag(D->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
      continue;
    }

    // OpenMP 4.5 [2.15.5.1, Restrictions, p.3]
    // A list item cannot appear in both a map clause and a data-sharing
    // attribute clause on the same construct
    //
    // OpenMP 5.0 [2.19.7.1, Restrictions, p.7]
    // A list item cannot appear in both a map clause and a data-sharing
    // attribute clause on the same construct unless the construct is a
    // combined construct.
    if ((getLangOpts().OpenMP <= 45 &&
         isOpenMPTargetExecutionDirective(CurrDir)) ||
        CurrDir == OMPD_target) {
      OpenMPClauseKind ConflictKind;
      if (DSAStack->checkMappableExprComponentListsForDecl(
              VD, /*CurrentRegionOnly=*/true,
              [&](OMPClauseMappableExprCommon::MappableExprComponentListRef,
                  OpenMPClauseKind WhereFoundClauseKind) -> bool {
                ConflictKind = WhereFoundClauseKind;
                return true;
              })) {
        Diag(ELoc, diag::err_omp_variable_in_given_clause_and_dsa)
            << getOpenMPClauseName(OMPC_private)
            << getOpenMPClauseName(ConflictKind)
            << getOpenMPDirectiveName(CurrDir);
        reportOriginalDsa(SemaRef, DSAStack, D, DVar);
        continue;
      }
    }

    // OpenMP [2.9.3.3, Restrictions, C/C++, p.1]
    //  A variable of class type (or array thereof) that appears in a private
    //  clause requires an accessible, unambiguous default constructor for the
    //  class type.
    // Generate helper private variable and initialize it with the default
    // value. The address of the original variable is replaced by the address of
    // the new private variable in CodeGen. This new variable is not added to
    // IdResolver, so the code in the OpenMP region uses original variable for
    // proper diagnostics.
    Type = Type.getUnqualifiedType();
    VarDecl *VDPrivate =
        buildVarDecl(SemaRef, ELoc, Type, D->getName(),
                     D->hasAttrs() ? &D->getAttrs() : nullptr,
                     VD ? cast<DeclRefExpr>(SimpleRefExpr) : nullptr);
    SemaRef.ActOnUninitializedDecl(VDPrivate);
    if (VDPrivate->isInvalidDecl())
      continue;
    DeclRefExpr *VDPrivateRefExpr = buildDeclRefExpr(
        SemaRef, VDPrivate, RefExpr->getType().getUnqualifiedType(), ELoc);

    DeclRefExpr *Ref = nullptr;
    if (!VD && !SemaRef.CurContext->isDependentContext()) {
      auto *FD = dyn_cast<FieldDecl>(D);
      VarDecl *VD = FD ? DSAStack->getImplicitFDCapExprDecl(FD) : nullptr;
      if (VD)
        Ref = buildDeclRefExpr(SemaRef, VD, VD->getType().getNonReferenceType(),
                               RefExpr->getExprLoc());
      else
        Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/false);
    }
    if (!IsImplicitClause)
      DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_private, Ref);
    Vars.push_back((VD || SemaRef.CurContext->isDependentContext())
                       ? RefExpr->IgnoreParens()
                       : Ref);
    PrivateCopies.push_back(VDPrivateRefExpr);
  }

  if (Vars.empty())
    return nullptr;

  return OMPPrivateClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                  Vars, PrivateCopies);
}

OMPClause *SemaOpenMP::ActOnOpenMPFirstprivateClause(ArrayRef<Expr *> VarList,
                                                     SourceLocation StartLoc,
                                                     SourceLocation LParenLoc,
                                                     SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  SmallVector<Expr *, 8> PrivateCopies;
  SmallVector<Expr *, 8> Inits;
  SmallVector<Decl *, 4> ExprCaptures;
  bool IsImplicitClause =
      StartLoc.isInvalid() && LParenLoc.isInvalid() && EndLoc.isInvalid();
  SourceLocation ImplicitClauseLoc = DSAStack->getConstructLoc();

  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP firstprivate clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      PrivateCopies.push_back(nullptr);
      Inits.push_back(nullptr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    ELoc = IsImplicitClause ? ImplicitClauseLoc : ELoc;
    QualType Type = D->getType();
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP [2.9.3.3, Restrictions, C/C++, p.3]
    //  A variable that appears in a private clause must not have an incomplete
    //  type or a reference type.
    if (SemaRef.RequireCompleteType(ELoc, Type,
                                    diag::err_omp_firstprivate_incomplete_type))
      continue;
    Type = Type.getNonReferenceType();

    // OpenMP [2.9.3.4, Restrictions, C/C++, p.1]
    //  A variable of class type (or array thereof) that appears in a private
    //  clause requires an accessible, unambiguous copy constructor for the
    //  class type.
    QualType ElemType =
        getASTContext().getBaseElementType(Type).getNonReferenceType();

    // If an implicit firstprivate variable found it was checked already.
    DSAStackTy::DSAVarData TopDVar;
    if (!IsImplicitClause) {
      DSAStackTy::DSAVarData DVar =
          DSAStack->getTopDSA(D, /*FromParent=*/false);
      TopDVar = DVar;
      OpenMPDirectiveKind CurrDir = DSAStack->getCurrentDirective();
      bool IsConstant = ElemType.isConstant(getASTContext());
      // OpenMP [2.4.13, Data-sharing Attribute Clauses]
      //  A list item that specifies a given variable may not appear in more
      // than one clause on the same directive, except that a variable may be
      //  specified in both firstprivate and lastprivate clauses.
      // OpenMP 4.5 [2.10.8, Distribute Construct, p.3]
      // A list item may appear in a firstprivate or lastprivate clause but not
      // both.
      if (DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_firstprivate &&
          (isOpenMPDistributeDirective(CurrDir) ||
           DVar.CKind != OMPC_lastprivate) &&
          DVar.RefExpr) {
        Diag(ELoc, diag::err_omp_wrong_dsa)
            << getOpenMPClauseName(DVar.CKind)
            << getOpenMPClauseName(OMPC_firstprivate);
        reportOriginalDsa(SemaRef, DSAStack, D, DVar);
        continue;
      }

      // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
      // in a Construct]
      //  Variables with the predetermined data-sharing attributes may not be
      //  listed in data-sharing attributes clauses, except for the cases
      //  listed below. For these exceptions only, listing a predetermined
      //  variable in a data-sharing attribute clause is allowed and overrides
      //  the variable's predetermined data-sharing attributes.
      // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
      // in a Construct, C/C++, p.2]
      //  Variables with const-qualified type having no mutable member may be
      //  listed in a firstprivate clause, even if they are static data members.
      if (!(IsConstant || (VD && VD->isStaticDataMember())) && !DVar.RefExpr &&
          DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_shared) {
        Diag(ELoc, diag::err_omp_wrong_dsa)
            << getOpenMPClauseName(DVar.CKind)
            << getOpenMPClauseName(OMPC_firstprivate);
        reportOriginalDsa(SemaRef, DSAStack, D, DVar);
        continue;
      }

      // OpenMP [2.9.3.4, Restrictions, p.2]
      //  A list item that is private within a parallel region must not appear
      //  in a firstprivate clause on a worksharing construct if any of the
      //  worksharing regions arising from the worksharing construct ever bind
      //  to any of the parallel regions arising from the parallel construct.
      // OpenMP 4.5 [2.15.3.4, Restrictions, p.3]
      // A list item that is private within a teams region must not appear in a
      // firstprivate clause on a distribute construct if any of the distribute
      // regions arising from the distribute construct ever bind to any of the
      // teams regions arising from the teams construct.
      // OpenMP 4.5 [2.15.3.4, Restrictions, p.3]
      // A list item that appears in a reduction clause of a teams construct
      // must not appear in a firstprivate clause on a distribute construct if
      // any of the distribute regions arising from the distribute construct
      // ever bind to any of the teams regions arising from the teams construct.
      if ((isOpenMPWorksharingDirective(CurrDir) ||
           isOpenMPDistributeDirective(CurrDir)) &&
          !isOpenMPParallelDirective(CurrDir) &&
          !isOpenMPTeamsDirective(CurrDir)) {
        DVar = DSAStack->getImplicitDSA(D, true);
        if (DVar.CKind != OMPC_shared &&
            (isOpenMPParallelDirective(DVar.DKind) ||
             isOpenMPTeamsDirective(DVar.DKind) ||
             DVar.DKind == OMPD_unknown)) {
          Diag(ELoc, diag::err_omp_required_access)
              << getOpenMPClauseName(OMPC_firstprivate)
              << getOpenMPClauseName(OMPC_shared);
          reportOriginalDsa(SemaRef, DSAStack, D, DVar);
          continue;
        }
      }
      // OpenMP [2.9.3.4, Restrictions, p.3]
      //  A list item that appears in a reduction clause of a parallel construct
      //  must not appear in a firstprivate clause on a worksharing or task
      //  construct if any of the worksharing or task regions arising from the
      //  worksharing or task construct ever bind to any of the parallel regions
      //  arising from the parallel construct.
      // OpenMP [2.9.3.4, Restrictions, p.4]
      //  A list item that appears in a reduction clause in worksharing
      //  construct must not appear in a firstprivate clause in a task construct
      //  encountered during execution of any of the worksharing regions arising
      //  from the worksharing construct.
      if (isOpenMPTaskingDirective(CurrDir)) {
        DVar = DSAStack->hasInnermostDSA(
            D,
            [](OpenMPClauseKind C, bool AppliedToPointee) {
              return C == OMPC_reduction && !AppliedToPointee;
            },
            [](OpenMPDirectiveKind K) {
              return isOpenMPParallelDirective(K) ||
                     isOpenMPWorksharingDirective(K) ||
                     isOpenMPTeamsDirective(K);
            },
            /*FromParent=*/true);
        if (DVar.CKind == OMPC_reduction &&
            (isOpenMPParallelDirective(DVar.DKind) ||
             isOpenMPWorksharingDirective(DVar.DKind) ||
             isOpenMPTeamsDirective(DVar.DKind))) {
          Diag(ELoc, diag::err_omp_parallel_reduction_in_task_firstprivate)
              << getOpenMPDirectiveName(DVar.DKind);
          reportOriginalDsa(SemaRef, DSAStack, D, DVar);
          continue;
        }
      }

      // OpenMP 4.5 [2.15.5.1, Restrictions, p.3]
      // A list item cannot appear in both a map clause and a data-sharing
      // attribute clause on the same construct
      //
      // OpenMP 5.0 [2.19.7.1, Restrictions, p.7]
      // A list item cannot appear in both a map clause and a data-sharing
      // attribute clause on the same construct unless the construct is a
      // combined construct.
      if ((getLangOpts().OpenMP <= 45 &&
           isOpenMPTargetExecutionDirective(CurrDir)) ||
          CurrDir == OMPD_target) {
        OpenMPClauseKind ConflictKind;
        if (DSAStack->checkMappableExprComponentListsForDecl(
                VD, /*CurrentRegionOnly=*/true,
                [&ConflictKind](
                    OMPClauseMappableExprCommon::MappableExprComponentListRef,
                    OpenMPClauseKind WhereFoundClauseKind) {
                  ConflictKind = WhereFoundClauseKind;
                  return true;
                })) {
          Diag(ELoc, diag::err_omp_variable_in_given_clause_and_dsa)
              << getOpenMPClauseName(OMPC_firstprivate)
              << getOpenMPClauseName(ConflictKind)
              << getOpenMPDirectiveName(DSAStack->getCurrentDirective());
          reportOriginalDsa(SemaRef, DSAStack, D, DVar);
          continue;
        }
      }
    }

    // Variably modified types are not supported for tasks.
    if (!Type->isAnyPointerType() && Type->isVariablyModifiedType() &&
        isOpenMPTaskingDirective(DSAStack->getCurrentDirective())) {
      Diag(ELoc, diag::err_omp_variably_modified_type_not_supported)
          << getOpenMPClauseName(OMPC_firstprivate) << Type
          << getOpenMPDirectiveName(DSAStack->getCurrentDirective());
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(getASTContext()) ==
                               VarDecl::DeclarationOnly;
      Diag(D->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
      continue;
    }

    Type = Type.getUnqualifiedType();
    VarDecl *VDPrivate =
        buildVarDecl(SemaRef, ELoc, Type, D->getName(),
                     D->hasAttrs() ? &D->getAttrs() : nullptr,
                     VD ? cast<DeclRefExpr>(SimpleRefExpr) : nullptr);
    // Generate helper private variable and initialize it with the value of the
    // original variable. The address of the original variable is replaced by
    // the address of the new private variable in the CodeGen. This new variable
    // is not added to IdResolver, so the code in the OpenMP region uses
    // original variable for proper diagnostics and variable capturing.
    Expr *VDInitRefExpr = nullptr;
    // For arrays generate initializer for single element and replace it by the
    // original array element in CodeGen.
    if (Type->isArrayType()) {
      VarDecl *VDInit =
          buildVarDecl(SemaRef, RefExpr->getExprLoc(), ElemType, D->getName());
      VDInitRefExpr = buildDeclRefExpr(SemaRef, VDInit, ElemType, ELoc);
      Expr *Init = SemaRef.DefaultLvalueConversion(VDInitRefExpr).get();
      ElemType = ElemType.getUnqualifiedType();
      VarDecl *VDInitTemp = buildVarDecl(SemaRef, RefExpr->getExprLoc(),
                                         ElemType, ".firstprivate.temp");
      InitializedEntity Entity =
          InitializedEntity::InitializeVariable(VDInitTemp);
      InitializationKind Kind = InitializationKind::CreateCopy(ELoc, ELoc);

      InitializationSequence InitSeq(SemaRef, Entity, Kind, Init);
      ExprResult Result = InitSeq.Perform(SemaRef, Entity, Kind, Init);
      if (Result.isInvalid())
        VDPrivate->setInvalidDecl();
      else
        VDPrivate->setInit(Result.getAs<Expr>());
      // Remove temp variable declaration.
      getASTContext().Deallocate(VDInitTemp);
    } else {
      VarDecl *VDInit = buildVarDecl(SemaRef, RefExpr->getExprLoc(), Type,
                                     ".firstprivate.temp");
      VDInitRefExpr = buildDeclRefExpr(SemaRef, VDInit, RefExpr->getType(),
                                       RefExpr->getExprLoc());
      SemaRef.AddInitializerToDecl(
          VDPrivate, SemaRef.DefaultLvalueConversion(VDInitRefExpr).get(),
          /*DirectInit=*/false);
    }
    if (VDPrivate->isInvalidDecl()) {
      if (IsImplicitClause) {
        Diag(RefExpr->getExprLoc(),
             diag::note_omp_task_predetermined_firstprivate_here);
      }
      continue;
    }
    SemaRef.CurContext->addDecl(VDPrivate);
    DeclRefExpr *VDPrivateRefExpr = buildDeclRefExpr(
        SemaRef, VDPrivate, RefExpr->getType().getUnqualifiedType(),
        RefExpr->getExprLoc());
    DeclRefExpr *Ref = nullptr;
    if (!VD && !SemaRef.CurContext->isDependentContext()) {
      if (TopDVar.CKind == OMPC_lastprivate) {
        Ref = TopDVar.PrivateCopy;
      } else {
        auto *FD = dyn_cast<FieldDecl>(D);
        VarDecl *VD = FD ? DSAStack->getImplicitFDCapExprDecl(FD) : nullptr;
        if (VD)
          Ref =
              buildDeclRefExpr(SemaRef, VD, VD->getType().getNonReferenceType(),
                               RefExpr->getExprLoc());
        else
          Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/true);
        if (VD || !isOpenMPCapturedDecl(D))
          ExprCaptures.push_back(Ref->getDecl());
      }
    }
    if (!IsImplicitClause)
      DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_firstprivate, Ref);
    Vars.push_back((VD || SemaRef.CurContext->isDependentContext())
                       ? RefExpr->IgnoreParens()
                       : Ref);
    PrivateCopies.push_back(VDPrivateRefExpr);
    Inits.push_back(VDInitRefExpr);
  }

  if (Vars.empty())
    return nullptr;

  return OMPFirstprivateClause::Create(
      getASTContext(), StartLoc, LParenLoc, EndLoc, Vars, PrivateCopies, Inits,
      buildPreInits(getASTContext(), ExprCaptures));
}

OMPClause *SemaOpenMP::ActOnOpenMPLastprivateClause(
    ArrayRef<Expr *> VarList, OpenMPLastprivateModifier LPKind,
    SourceLocation LPKindLoc, SourceLocation ColonLoc, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation EndLoc) {
  if (LPKind == OMPC_LASTPRIVATE_unknown && LPKindLoc.isValid()) {
    assert(ColonLoc.isValid() && "Colon location must be valid.");
    Diag(LPKindLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_lastprivate, /*First=*/0,
                                   /*Last=*/OMPC_LASTPRIVATE_unknown)
        << getOpenMPClauseName(OMPC_lastprivate);
    return nullptr;
  }

  SmallVector<Expr *, 8> Vars;
  SmallVector<Expr *, 8> SrcExprs;
  SmallVector<Expr *, 8> DstExprs;
  SmallVector<Expr *, 8> AssignmentOps;
  SmallVector<Decl *, 4> ExprCaptures;
  SmallVector<Expr *, 4> ExprPostUpdates;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP lastprivate clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      SrcExprs.push_back(nullptr);
      DstExprs.push_back(nullptr);
      AssignmentOps.push_back(nullptr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType Type = D->getType();
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP [2.14.3.5, Restrictions, C/C++, p.2]
    //  A variable that appears in a lastprivate clause must not have an
    //  incomplete type or a reference type.
    if (SemaRef.RequireCompleteType(ELoc, Type,
                                    diag::err_omp_lastprivate_incomplete_type))
      continue;
    Type = Type.getNonReferenceType();

    // OpenMP 5.0 [2.19.3, List Item Privatization, Restrictions]
    // A variable that is privatized must not have a const-qualified type
    // unless it is of class type with a mutable member. This restriction does
    // not apply to the firstprivate clause.
    //
    // OpenMP 3.1 [2.9.3.5, lastprivate clause, Restrictions]
    // A variable that appears in a lastprivate clause must not have a
    // const-qualified type unless it is of class type with a mutable member.
    if (rejectConstNotMutableType(SemaRef, D, Type, OMPC_lastprivate, ELoc))
      continue;

    // OpenMP 5.0 [2.19.4.5 lastprivate Clause, Restrictions]
    // A list item that appears in a lastprivate clause with the conditional
    // modifier must be a scalar variable.
    if (LPKind == OMPC_LASTPRIVATE_conditional && !Type->isScalarType()) {
      Diag(ELoc, diag::err_omp_lastprivate_conditional_non_scalar);
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(getASTContext()) ==
                               VarDecl::DeclarationOnly;
      Diag(D->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
      continue;
    }

    OpenMPDirectiveKind CurrDir = DSAStack->getCurrentDirective();
    // OpenMP [2.14.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a Construct]
    //  Variables with the predetermined data-sharing attributes may not be
    //  listed in data-sharing attributes clauses, except for the cases
    //  listed below.
    // OpenMP 4.5 [2.10.8, Distribute Construct, p.3]
    // A list item may appear in a firstprivate or lastprivate clause but not
    // both.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(D, /*FromParent=*/false);
    if (DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_lastprivate &&
        (isOpenMPDistributeDirective(CurrDir) ||
         DVar.CKind != OMPC_firstprivate) &&
        (DVar.CKind != OMPC_private || DVar.RefExpr != nullptr)) {
      Diag(ELoc, diag::err_omp_wrong_dsa)
          << getOpenMPClauseName(DVar.CKind)
          << getOpenMPClauseName(OMPC_lastprivate);
      reportOriginalDsa(SemaRef, DSAStack, D, DVar);
      continue;
    }

    // OpenMP [2.14.3.5, Restrictions, p.2]
    // A list item that is private within a parallel region, or that appears in
    // the reduction clause of a parallel construct, must not appear in a
    // lastprivate clause on a worksharing construct if any of the corresponding
    // worksharing regions ever binds to any of the corresponding parallel
    // regions.
    DSAStackTy::DSAVarData TopDVar = DVar;
    if (isOpenMPWorksharingDirective(CurrDir) &&
        !isOpenMPParallelDirective(CurrDir) &&
        !isOpenMPTeamsDirective(CurrDir)) {
      DVar = DSAStack->getImplicitDSA(D, true);
      if (DVar.CKind != OMPC_shared) {
        Diag(ELoc, diag::err_omp_required_access)
            << getOpenMPClauseName(OMPC_lastprivate)
            << getOpenMPClauseName(OMPC_shared);
        reportOriginalDsa(SemaRef, DSAStack, D, DVar);
        continue;
      }
    }

    // OpenMP [2.14.3.5, Restrictions, C++, p.1,2]
    //  A variable of class type (or array thereof) that appears in a
    //  lastprivate clause requires an accessible, unambiguous default
    //  constructor for the class type, unless the list item is also specified
    //  in a firstprivate clause.
    //  A variable of class type (or array thereof) that appears in a
    //  lastprivate clause requires an accessible, unambiguous copy assignment
    //  operator for the class type.
    Type = getASTContext().getBaseElementType(Type).getNonReferenceType();
    VarDecl *SrcVD = buildVarDecl(SemaRef, ERange.getBegin(),
                                  Type.getUnqualifiedType(), ".lastprivate.src",
                                  D->hasAttrs() ? &D->getAttrs() : nullptr);
    DeclRefExpr *PseudoSrcExpr =
        buildDeclRefExpr(SemaRef, SrcVD, Type.getUnqualifiedType(), ELoc);
    VarDecl *DstVD =
        buildVarDecl(SemaRef, ERange.getBegin(), Type, ".lastprivate.dst",
                     D->hasAttrs() ? &D->getAttrs() : nullptr);
    DeclRefExpr *PseudoDstExpr = buildDeclRefExpr(SemaRef, DstVD, Type, ELoc);
    // For arrays generate assignment operation for single element and replace
    // it by the original array element in CodeGen.
    ExprResult AssignmentOp = SemaRef.BuildBinOp(/*S=*/nullptr, ELoc, BO_Assign,
                                                 PseudoDstExpr, PseudoSrcExpr);
    if (AssignmentOp.isInvalid())
      continue;
    AssignmentOp = SemaRef.ActOnFinishFullExpr(AssignmentOp.get(), ELoc,
                                               /*DiscardedValue*/ false);
    if (AssignmentOp.isInvalid())
      continue;

    DeclRefExpr *Ref = nullptr;
    if (!VD && !SemaRef.CurContext->isDependentContext()) {
      if (TopDVar.CKind == OMPC_firstprivate) {
        Ref = TopDVar.PrivateCopy;
      } else {
        Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/false);
        if (!isOpenMPCapturedDecl(D))
          ExprCaptures.push_back(Ref->getDecl());
      }
      if ((TopDVar.CKind == OMPC_firstprivate && !TopDVar.PrivateCopy) ||
          (!isOpenMPCapturedDecl(D) &&
           Ref->getDecl()->hasAttr<OMPCaptureNoInitAttr>())) {
        ExprResult RefRes = SemaRef.DefaultLvalueConversion(Ref);
        if (!RefRes.isUsable())
          continue;
        ExprResult PostUpdateRes =
            SemaRef.BuildBinOp(DSAStack->getCurScope(), ELoc, BO_Assign,
                               SimpleRefExpr, RefRes.get());
        if (!PostUpdateRes.isUsable())
          continue;
        ExprPostUpdates.push_back(
            SemaRef.IgnoredValueConversions(PostUpdateRes.get()).get());
      }
    }
    DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_lastprivate, Ref);
    Vars.push_back((VD || SemaRef.CurContext->isDependentContext())
                       ? RefExpr->IgnoreParens()
                       : Ref);
    SrcExprs.push_back(PseudoSrcExpr);
    DstExprs.push_back(PseudoDstExpr);
    AssignmentOps.push_back(AssignmentOp.get());
  }

  if (Vars.empty())
    return nullptr;

  return OMPLastprivateClause::Create(
      getASTContext(), StartLoc, LParenLoc, EndLoc, Vars, SrcExprs, DstExprs,
      AssignmentOps, LPKind, LPKindLoc, ColonLoc,
      buildPreInits(getASTContext(), ExprCaptures),
      buildPostUpdate(SemaRef, ExprPostUpdates));
}

OMPClause *SemaOpenMP::ActOnOpenMPSharedClause(ArrayRef<Expr *> VarList,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP lastprivate clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    auto *VD = dyn_cast<VarDecl>(D);
    // OpenMP [2.9.1.1, Data-sharing Attribute Rules for Variables Referenced
    // in a Construct]
    //  Variables with the predetermined data-sharing attributes may not be
    //  listed in data-sharing attributes clauses, except for the cases
    //  listed below. For these exceptions only, listing a predetermined
    //  variable in a data-sharing attribute clause is allowed and overrides
    //  the variable's predetermined data-sharing attributes.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(D, /*FromParent=*/false);
    if (DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_shared &&
        DVar.RefExpr) {
      Diag(ELoc, diag::err_omp_wrong_dsa) << getOpenMPClauseName(DVar.CKind)
                                          << getOpenMPClauseName(OMPC_shared);
      reportOriginalDsa(SemaRef, DSAStack, D, DVar);
      continue;
    }

    DeclRefExpr *Ref = nullptr;
    if (!VD && isOpenMPCapturedDecl(D) &&
        !SemaRef.CurContext->isDependentContext())
      Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/true);
    DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_shared, Ref);
    Vars.push_back((VD || !Ref || SemaRef.CurContext->isDependentContext())
                       ? RefExpr->IgnoreParens()
                       : Ref);
  }

  if (Vars.empty())
    return nullptr;

  return OMPSharedClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                 Vars);
}

namespace {
class DSARefChecker : public StmtVisitor<DSARefChecker, bool> {
  DSAStackTy *Stack;

public:
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    if (auto *VD = dyn_cast<VarDecl>(E->getDecl())) {
      DSAStackTy::DSAVarData DVar = Stack->getTopDSA(VD, /*FromParent=*/false);
      if (DVar.CKind == OMPC_shared && !DVar.RefExpr)
        return false;
      if (DVar.CKind != OMPC_unknown)
        return true;
      DSAStackTy::DSAVarData DVarPrivate = Stack->hasDSA(
          VD,
          [](OpenMPClauseKind C, bool AppliedToPointee, bool) {
            return isOpenMPPrivate(C) && !AppliedToPointee;
          },
          [](OpenMPDirectiveKind) { return true; },
          /*FromParent=*/true);
      return DVarPrivate.CKind != OMPC_unknown;
    }
    return false;
  }
  bool VisitStmt(Stmt *S) {
    for (Stmt *Child : S->children()) {
      if (Child && Visit(Child))
        return true;
    }
    return false;
  }
  explicit DSARefChecker(DSAStackTy *S) : Stack(S) {}
};
} // namespace

namespace {
// Transform MemberExpression for specified FieldDecl of current class to
// DeclRefExpr to specified OMPCapturedExprDecl.
class TransformExprToCaptures : public TreeTransform<TransformExprToCaptures> {
  typedef TreeTransform<TransformExprToCaptures> BaseTransform;
  ValueDecl *Field = nullptr;
  DeclRefExpr *CapturedExpr = nullptr;

public:
  TransformExprToCaptures(Sema &SemaRef, ValueDecl *FieldDecl)
      : BaseTransform(SemaRef), Field(FieldDecl), CapturedExpr(nullptr) {}

  ExprResult TransformMemberExpr(MemberExpr *E) {
    if (isa<CXXThisExpr>(E->getBase()->IgnoreParenImpCasts()) &&
        E->getMemberDecl() == Field) {
      CapturedExpr = buildCapture(SemaRef, Field, E, /*WithInit=*/false);
      return CapturedExpr;
    }
    return BaseTransform::TransformMemberExpr(E);
  }
  DeclRefExpr *getCapturedExpr() { return CapturedExpr; }
};
} // namespace

template <typename T, typename U>
static T filterLookupForUDReductionAndMapper(
    SmallVectorImpl<U> &Lookups, const llvm::function_ref<T(ValueDecl *)> Gen) {
  for (U &Set : Lookups) {
    for (auto *D : Set) {
      if (T Res = Gen(cast<ValueDecl>(D)))
        return Res;
    }
  }
  return T();
}

static NamedDecl *findAcceptableDecl(Sema &SemaRef, NamedDecl *D) {
  assert(!LookupResult::isVisible(SemaRef, D) && "not in slow case");

  for (auto *RD : D->redecls()) {
    // Don't bother with extra checks if we already know this one isn't visible.
    if (RD == D)
      continue;

    auto ND = cast<NamedDecl>(RD);
    if (LookupResult::isVisible(SemaRef, ND))
      return ND;
  }

  return nullptr;
}

static void
argumentDependentLookup(Sema &SemaRef, const DeclarationNameInfo &Id,
                        SourceLocation Loc, QualType Ty,
                        SmallVectorImpl<UnresolvedSet<8>> &Lookups) {
  // Find all of the associated namespaces and classes based on the
  // arguments we have.
  Sema::AssociatedNamespaceSet AssociatedNamespaces;
  Sema::AssociatedClassSet AssociatedClasses;
  OpaqueValueExpr OVE(Loc, Ty, VK_LValue);
  SemaRef.FindAssociatedClassesAndNamespaces(Loc, &OVE, AssociatedNamespaces,
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
    DeclContext::lookup_result R = NS->lookup(Id.getName());
    for (auto *D : R) {
      auto *Underlying = D;
      if (auto *USD = dyn_cast<UsingShadowDecl>(D))
        Underlying = USD->getTargetDecl();

      if (!isa<OMPDeclareReductionDecl>(Underlying) &&
          !isa<OMPDeclareMapperDecl>(Underlying))
        continue;

      if (!SemaRef.isVisible(D)) {
        D = findAcceptableDecl(SemaRef, D);
        if (!D)
          continue;
        if (auto *USD = dyn_cast<UsingShadowDecl>(D))
          Underlying = USD->getTargetDecl();
      }
      Lookups.emplace_back();
      Lookups.back().addDecl(Underlying);
    }
  }
}

static ExprResult
buildDeclareReductionRef(Sema &SemaRef, SourceLocation Loc, SourceRange Range,
                         Scope *S, CXXScopeSpec &ReductionIdScopeSpec,
                         const DeclarationNameInfo &ReductionId, QualType Ty,
                         CXXCastPath &BasePath, Expr *UnresolvedReduction) {
  if (ReductionIdScopeSpec.isInvalid())
    return ExprError();
  SmallVector<UnresolvedSet<8>, 4> Lookups;
  if (S) {
    LookupResult Lookup(SemaRef, ReductionId, Sema::LookupOMPReductionName);
    Lookup.suppressDiagnostics();
    while (S && SemaRef.LookupParsedName(Lookup, S, &ReductionIdScopeSpec,
                                         /*ObjectType=*/QualType())) {
      NamedDecl *D = Lookup.getRepresentativeDecl();
      do {
        S = S->getParent();
      } while (S && !S->isDeclScope(D));
      if (S)
        S = S->getParent();
      Lookups.emplace_back();
      Lookups.back().append(Lookup.begin(), Lookup.end());
      Lookup.clear();
    }
  } else if (auto *ULE =
                 cast_or_null<UnresolvedLookupExpr>(UnresolvedReduction)) {
    Lookups.push_back(UnresolvedSet<8>());
    Decl *PrevD = nullptr;
    for (NamedDecl *D : ULE->decls()) {
      if (D == PrevD)
        Lookups.push_back(UnresolvedSet<8>());
      else if (auto *DRD = dyn_cast<OMPDeclareReductionDecl>(D))
        Lookups.back().addDecl(DRD);
      PrevD = D;
    }
  }
  if (SemaRef.CurContext->isDependentContext() || Ty->isDependentType() ||
      Ty->isInstantiationDependentType() ||
      Ty->containsUnexpandedParameterPack() ||
      filterLookupForUDReductionAndMapper<bool>(Lookups, [](ValueDecl *D) {
        return !D->isInvalidDecl() &&
               (D->getType()->isDependentType() ||
                D->getType()->isInstantiationDependentType() ||
                D->getType()->containsUnexpandedParameterPack());
      })) {
    UnresolvedSet<8> ResSet;
    for (const UnresolvedSet<8> &Set : Lookups) {
      if (Set.empty())
        continue;
      ResSet.append(Set.begin(), Set.end());
      // The last item marks the end of all declarations at the specified scope.
      ResSet.addDecl(Set[Set.size() - 1]);
    }
    return UnresolvedLookupExpr::Create(
        SemaRef.Context, /*NamingClass=*/nullptr,
        ReductionIdScopeSpec.getWithLocInContext(SemaRef.Context), ReductionId,
        /*ADL=*/true, ResSet.begin(), ResSet.end(), /*KnownDependent=*/false,
        /*KnownInstantiationDependent=*/false);
  }
  // Lookup inside the classes.
  // C++ [over.match.oper]p3:
  //   For a unary operator @ with an operand of a type whose
  //   cv-unqualified version is T1, and for a binary operator @ with
  //   a left operand of a type whose cv-unqualified version is T1 and
  //   a right operand of a type whose cv-unqualified version is T2,
  //   three sets of candidate functions, designated member
  //   candidates, non-member candidates and built-in candidates, are
  //   constructed as follows:
  //     -- If T1 is a complete class type or a class currently being
  //        defined, the set of member candidates is the result of the
  //        qualified lookup of T1::operator@ (13.3.1.1.1); otherwise,
  //        the set of member candidates is empty.
  LookupResult Lookup(SemaRef, ReductionId, Sema::LookupOMPReductionName);
  Lookup.suppressDiagnostics();
  if (const auto *TyRec = Ty->getAs<RecordType>()) {
    // Complete the type if it can be completed.
    // If the type is neither complete nor being defined, bail out now.
    if (SemaRef.isCompleteType(Loc, Ty) || TyRec->isBeingDefined() ||
        TyRec->getDecl()->getDefinition()) {
      Lookup.clear();
      SemaRef.LookupQualifiedName(Lookup, TyRec->getDecl());
      if (Lookup.empty()) {
        Lookups.emplace_back();
        Lookups.back().append(Lookup.begin(), Lookup.end());
      }
    }
  }
  // Perform ADL.
  if (SemaRef.getLangOpts().CPlusPlus)
    argumentDependentLookup(SemaRef, ReductionId, Loc, Ty, Lookups);
  if (auto *VD = filterLookupForUDReductionAndMapper<ValueDecl *>(
          Lookups, [&SemaRef, Ty](ValueDecl *D) -> ValueDecl * {
            if (!D->isInvalidDecl() &&
                SemaRef.Context.hasSameType(D->getType(), Ty))
              return D;
            return nullptr;
          }))
    return SemaRef.BuildDeclRefExpr(VD, VD->getType().getNonReferenceType(),
                                    VK_LValue, Loc);
  if (SemaRef.getLangOpts().CPlusPlus) {
    if (auto *VD = filterLookupForUDReductionAndMapper<ValueDecl *>(
            Lookups, [&SemaRef, Ty, Loc](ValueDecl *D) -> ValueDecl * {
              if (!D->isInvalidDecl() &&
                  SemaRef.IsDerivedFrom(Loc, Ty, D->getType()) &&
                  !Ty.isMoreQualifiedThan(D->getType()))
                return D;
              return nullptr;
            })) {
      CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                         /*DetectVirtual=*/false);
      if (SemaRef.IsDerivedFrom(Loc, Ty, VD->getType(), Paths)) {
        if (!Paths.isAmbiguous(SemaRef.Context.getCanonicalType(
                VD->getType().getUnqualifiedType()))) {
          if (SemaRef.CheckBaseClassAccess(
                  Loc, VD->getType(), Ty, Paths.front(),
                  /*DiagID=*/0) != Sema::AR_inaccessible) {
            SemaRef.BuildBasePathArray(Paths, BasePath);
            return SemaRef.BuildDeclRefExpr(
                VD, VD->getType().getNonReferenceType(), VK_LValue, Loc);
          }
        }
      }
    }
  }
  if (ReductionIdScopeSpec.isSet()) {
    SemaRef.Diag(Loc, diag::err_omp_not_resolved_reduction_identifier)
        << Ty << Range;
    return ExprError();
  }
  return ExprEmpty();
}

namespace {
/// Data for the reduction-based clauses.
struct ReductionData {
  /// List of original reduction items.
  SmallVector<Expr *, 8> Vars;
  /// List of private copies of the reduction items.
  SmallVector<Expr *, 8> Privates;
  /// LHS expressions for the reduction_op expressions.
  SmallVector<Expr *, 8> LHSs;
  /// RHS expressions for the reduction_op expressions.
  SmallVector<Expr *, 8> RHSs;
  /// Reduction operation expression.
  SmallVector<Expr *, 8> ReductionOps;
  /// inscan copy operation expressions.
  SmallVector<Expr *, 8> InscanCopyOps;
  /// inscan copy temp array expressions for prefix sums.
  SmallVector<Expr *, 8> InscanCopyArrayTemps;
  /// inscan copy temp array element expressions for prefix sums.
  SmallVector<Expr *, 8> InscanCopyArrayElems;
  /// Taskgroup descriptors for the corresponding reduction items in
  /// in_reduction clauses.
  SmallVector<Expr *, 8> TaskgroupDescriptors;
  /// List of captures for clause.
  SmallVector<Decl *, 4> ExprCaptures;
  /// List of postupdate expressions.
  SmallVector<Expr *, 4> ExprPostUpdates;
  /// Reduction modifier.
  unsigned RedModifier = 0;
  ReductionData() = delete;
  /// Reserves required memory for the reduction data.
  ReductionData(unsigned Size, unsigned Modifier = 0) : RedModifier(Modifier) {
    Vars.reserve(Size);
    Privates.reserve(Size);
    LHSs.reserve(Size);
    RHSs.reserve(Size);
    ReductionOps.reserve(Size);
    if (RedModifier == OMPC_REDUCTION_inscan) {
      InscanCopyOps.reserve(Size);
      InscanCopyArrayTemps.reserve(Size);
      InscanCopyArrayElems.reserve(Size);
    }
    TaskgroupDescriptors.reserve(Size);
    ExprCaptures.reserve(Size);
    ExprPostUpdates.reserve(Size);
  }
  /// Stores reduction item and reduction operation only (required for dependent
  /// reduction item).
  void push(Expr *Item, Expr *ReductionOp) {
    Vars.emplace_back(Item);
    Privates.emplace_back(nullptr);
    LHSs.emplace_back(nullptr);
    RHSs.emplace_back(nullptr);
    ReductionOps.emplace_back(ReductionOp);
    TaskgroupDescriptors.emplace_back(nullptr);
    if (RedModifier == OMPC_REDUCTION_inscan) {
      InscanCopyOps.push_back(nullptr);
      InscanCopyArrayTemps.push_back(nullptr);
      InscanCopyArrayElems.push_back(nullptr);
    }
  }
  /// Stores reduction data.
  void push(Expr *Item, Expr *Private, Expr *LHS, Expr *RHS, Expr *ReductionOp,
            Expr *TaskgroupDescriptor, Expr *CopyOp, Expr *CopyArrayTemp,
            Expr *CopyArrayElem) {
    Vars.emplace_back(Item);
    Privates.emplace_back(Private);
    LHSs.emplace_back(LHS);
    RHSs.emplace_back(RHS);
    ReductionOps.emplace_back(ReductionOp);
    TaskgroupDescriptors.emplace_back(TaskgroupDescriptor);
    if (RedModifier == OMPC_REDUCTION_inscan) {
      InscanCopyOps.push_back(CopyOp);
      InscanCopyArrayTemps.push_back(CopyArrayTemp);
      InscanCopyArrayElems.push_back(CopyArrayElem);
    } else {
      assert(CopyOp == nullptr && CopyArrayTemp == nullptr &&
             CopyArrayElem == nullptr &&
             "Copy operation must be used for inscan reductions only.");
    }
  }
};
} // namespace

static bool checkOMPArraySectionConstantForReduction(
    ASTContext &Context, const ArraySectionExpr *OASE, bool &SingleElement,
    SmallVectorImpl<llvm::APSInt> &ArraySizes) {
  const Expr *Length = OASE->getLength();
  if (Length == nullptr) {
    // For array sections of the form [1:] or [:], we would need to analyze
    // the lower bound...
    if (OASE->getColonLocFirst().isValid())
      return false;

    // This is an array subscript which has implicit length 1!
    SingleElement = true;
    ArraySizes.push_back(llvm::APSInt::get(1));
  } else {
    Expr::EvalResult Result;
    if (!Length->EvaluateAsInt(Result, Context))
      return false;

    llvm::APSInt ConstantLengthValue = Result.Val.getInt();
    SingleElement = (ConstantLengthValue.getSExtValue() == 1);
    ArraySizes.push_back(ConstantLengthValue);
  }

  // Get the base of this array section and walk up from there.
  const Expr *Base = OASE->getBase()->IgnoreParenImpCasts();

  // We require length = 1 for all array sections except the right-most to
  // guarantee that the memory region is contiguous and has no holes in it.
  while (const auto *TempOASE = dyn_cast<ArraySectionExpr>(Base)) {
    Length = TempOASE->getLength();
    if (Length == nullptr) {
      // For array sections of the form [1:] or [:], we would need to analyze
      // the lower bound...
      if (OASE->getColonLocFirst().isValid())
        return false;

      // This is an array subscript which has implicit length 1!
      ArraySizes.push_back(llvm::APSInt::get(1));
    } else {
      Expr::EvalResult Result;
      if (!Length->EvaluateAsInt(Result, Context))
        return false;

      llvm::APSInt ConstantLengthValue = Result.Val.getInt();
      if (ConstantLengthValue.getSExtValue() != 1)
        return false;

      ArraySizes.push_back(ConstantLengthValue);
    }
    Base = TempOASE->getBase()->IgnoreParenImpCasts();
  }

  // If we have a single element, we don't need to add the implicit lengths.
  if (!SingleElement) {
    while (const auto *TempASE = dyn_cast<ArraySubscriptExpr>(Base)) {
      // Has implicit length 1!
      ArraySizes.push_back(llvm::APSInt::get(1));
      Base = TempASE->getBase()->IgnoreParenImpCasts();
    }
  }

  // This array section can be privatized as a single value or as a constant
  // sized array.
  return true;
}

static BinaryOperatorKind
getRelatedCompoundReductionOp(BinaryOperatorKind BOK) {
  if (BOK == BO_Add)
    return BO_AddAssign;
  if (BOK == BO_Mul)
    return BO_MulAssign;
  if (BOK == BO_And)
    return BO_AndAssign;
  if (BOK == BO_Or)
    return BO_OrAssign;
  if (BOK == BO_Xor)
    return BO_XorAssign;
  return BOK;
}

static bool actOnOMPReductionKindClause(
    Sema &S, DSAStackTy *Stack, OpenMPClauseKind ClauseKind,
    ArrayRef<Expr *> VarList, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ColonLoc, SourceLocation EndLoc,
    CXXScopeSpec &ReductionIdScopeSpec, const DeclarationNameInfo &ReductionId,
    ArrayRef<Expr *> UnresolvedReductions, ReductionData &RD) {
  DeclarationName DN = ReductionId.getName();
  OverloadedOperatorKind OOK = DN.getCXXOverloadedOperator();
  BinaryOperatorKind BOK = BO_Comma;

  ASTContext &Context = S.Context;
  // OpenMP [2.14.3.6, reduction clause]
  // C
  // reduction-identifier is either an identifier or one of the following
  // operators: +, -, *,  &, |, ^, && and ||
  // C++
  // reduction-identifier is either an id-expression or one of the following
  // operators: +, -, *, &, |, ^, && and ||
  switch (OOK) {
  case OO_Plus:
    BOK = BO_Add;
    break;
  case OO_Minus:
    // Minus(-) operator is not supported in TR11 (OpenMP 6.0). Setting BOK to
    // BO_Comma will automatically diagnose it for OpenMP > 52 as not allowed
    // reduction identifier.
    if (S.LangOpts.OpenMP > 52)
      BOK = BO_Comma;
    else
      BOK = BO_Add;
    break;
  case OO_Star:
    BOK = BO_Mul;
    break;
  case OO_Amp:
    BOK = BO_And;
    break;
  case OO_Pipe:
    BOK = BO_Or;
    break;
  case OO_Caret:
    BOK = BO_Xor;
    break;
  case OO_AmpAmp:
    BOK = BO_LAnd;
    break;
  case OO_PipePipe:
    BOK = BO_LOr;
    break;
  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
  case OO_Slash:
  case OO_Percent:
  case OO_Tilde:
  case OO_Exclaim:
  case OO_Equal:
  case OO_Less:
  case OO_Greater:
  case OO_LessEqual:
  case OO_GreaterEqual:
  case OO_PlusEqual:
  case OO_MinusEqual:
  case OO_StarEqual:
  case OO_SlashEqual:
  case OO_PercentEqual:
  case OO_CaretEqual:
  case OO_AmpEqual:
  case OO_PipeEqual:
  case OO_LessLess:
  case OO_GreaterGreater:
  case OO_LessLessEqual:
  case OO_GreaterGreaterEqual:
  case OO_EqualEqual:
  case OO_ExclaimEqual:
  case OO_Spaceship:
  case OO_PlusPlus:
  case OO_MinusMinus:
  case OO_Comma:
  case OO_ArrowStar:
  case OO_Arrow:
  case OO_Call:
  case OO_Subscript:
  case OO_Conditional:
  case OO_Coawait:
  case NUM_OVERLOADED_OPERATORS:
    llvm_unreachable("Unexpected reduction identifier");
  case OO_None:
    if (IdentifierInfo *II = DN.getAsIdentifierInfo()) {
      if (II->isStr("max"))
        BOK = BO_GT;
      else if (II->isStr("min"))
        BOK = BO_LT;
    }
    break;
  }

  // OpenMP 5.2, 5.5.5 (see page 627, line 18) reduction Clause, Restrictions
  // A reduction clause with the minus (-) operator was deprecated
  if (OOK == OO_Minus && S.LangOpts.OpenMP == 52)
    S.Diag(ReductionId.getLoc(), diag::warn_omp_minus_in_reduction_deprecated);

  SourceRange ReductionIdRange;
  if (ReductionIdScopeSpec.isValid())
    ReductionIdRange.setBegin(ReductionIdScopeSpec.getBeginLoc());
  else
    ReductionIdRange.setBegin(ReductionId.getBeginLoc());
  ReductionIdRange.setEnd(ReductionId.getEndLoc());

  auto IR = UnresolvedReductions.begin(), ER = UnresolvedReductions.end();
  bool FirstIter = true;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "nullptr expr in OpenMP reduction clause.");
    // OpenMP [2.1, C/C++]
    //  A list item is a variable or array section, subject to the restrictions
    //  specified in Section 2.4 on page 42 and in each of the sections
    // describing clauses and directives for which a list appears.
    // OpenMP  [2.14.3.3, Restrictions, p.1]
    //  A variable that is part of another variable (as an array or
    //  structure element) cannot appear in a private clause.
    if (!FirstIter && IR != ER)
      ++IR;
    FirstIter = false;
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(S, SimpleRefExpr, ELoc, ERange,
                              /*AllowArraySection=*/true);
    if (Res.second) {
      // Try to find 'declare reduction' corresponding construct before using
      // builtin/overloaded operators.
      QualType Type = Context.DependentTy;
      CXXCastPath BasePath;
      ExprResult DeclareReductionRef = buildDeclareReductionRef(
          S, ELoc, ERange, Stack->getCurScope(), ReductionIdScopeSpec,
          ReductionId, Type, BasePath, IR == ER ? nullptr : *IR);
      Expr *ReductionOp = nullptr;
      if (S.CurContext->isDependentContext() &&
          (DeclareReductionRef.isUnset() ||
           isa<UnresolvedLookupExpr>(DeclareReductionRef.get())))
        ReductionOp = DeclareReductionRef.get();
      // It will be analyzed later.
      RD.push(RefExpr, ReductionOp);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    Expr *TaskgroupDescriptor = nullptr;
    QualType Type;
    auto *ASE = dyn_cast<ArraySubscriptExpr>(RefExpr->IgnoreParens());
    auto *OASE = dyn_cast<ArraySectionExpr>(RefExpr->IgnoreParens());
    if (ASE) {
      Type = ASE->getType().getNonReferenceType();
    } else if (OASE) {
      QualType BaseType =
          ArraySectionExpr::getBaseOriginalType(OASE->getBase());
      if (const auto *ATy = BaseType->getAsArrayTypeUnsafe())
        Type = ATy->getElementType();
      else
        Type = BaseType->getPointeeType();
      Type = Type.getNonReferenceType();
    } else {
      Type = Context.getBaseElementType(D->getType().getNonReferenceType());
    }
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP [2.9.3.3, Restrictions, C/C++, p.3]
    //  A variable that appears in a private clause must not have an incomplete
    //  type or a reference type.
    if (S.RequireCompleteType(ELoc, D->getType(),
                              diag::err_omp_reduction_incomplete_type))
      continue;
    // OpenMP [2.14.3.6, reduction clause, Restrictions]
    // A list item that appears in a reduction clause must not be
    // const-qualified.
    if (rejectConstNotMutableType(S, D, Type, ClauseKind, ELoc,
                                  /*AcceptIfMutable*/ false, ASE || OASE))
      continue;

    OpenMPDirectiveKind CurrDir = Stack->getCurrentDirective();
    // OpenMP [2.9.3.6, Restrictions, C/C++, p.4]
    //  If a list-item is a reference type then it must bind to the same object
    //  for all threads of the team.
    if (!ASE && !OASE) {
      if (VD) {
        VarDecl *VDDef = VD->getDefinition();
        if (VD->getType()->isReferenceType() && VDDef && VDDef->hasInit()) {
          DSARefChecker Check(Stack);
          if (Check.Visit(VDDef->getInit())) {
            S.Diag(ELoc, diag::err_omp_reduction_ref_type_arg)
                << getOpenMPClauseName(ClauseKind) << ERange;
            S.Diag(VDDef->getLocation(), diag::note_defined_here) << VDDef;
            continue;
          }
        }
      }

      // OpenMP [2.14.1.1, Data-sharing Attribute Rules for Variables Referenced
      // in a Construct]
      //  Variables with the predetermined data-sharing attributes may not be
      //  listed in data-sharing attributes clauses, except for the cases
      //  listed below. For these exceptions only, listing a predetermined
      //  variable in a data-sharing attribute clause is allowed and overrides
      //  the variable's predetermined data-sharing attributes.
      // OpenMP [2.14.3.6, Restrictions, p.3]
      //  Any number of reduction clauses can be specified on the directive,
      //  but a list item can appear only once in the reduction clauses for that
      //  directive.
      DSAStackTy::DSAVarData DVar = Stack->getTopDSA(D, /*FromParent=*/false);
      if (DVar.CKind == OMPC_reduction) {
        S.Diag(ELoc, diag::err_omp_once_referenced)
            << getOpenMPClauseName(ClauseKind);
        if (DVar.RefExpr)
          S.Diag(DVar.RefExpr->getExprLoc(), diag::note_omp_referenced);
        continue;
      }
      if (DVar.CKind != OMPC_unknown) {
        S.Diag(ELoc, diag::err_omp_wrong_dsa)
            << getOpenMPClauseName(DVar.CKind)
            << getOpenMPClauseName(OMPC_reduction);
        reportOriginalDsa(S, Stack, D, DVar);
        continue;
      }

      // OpenMP [2.14.3.6, Restrictions, p.1]
      //  A list item that appears in a reduction clause of a worksharing
      //  construct must be shared in the parallel regions to which any of the
      //  worksharing regions arising from the worksharing construct bind.
      if (isOpenMPWorksharingDirective(CurrDir) &&
          !isOpenMPParallelDirective(CurrDir) &&
          !isOpenMPTeamsDirective(CurrDir)) {
        DVar = Stack->getImplicitDSA(D, true);
        if (DVar.CKind != OMPC_shared) {
          S.Diag(ELoc, diag::err_omp_required_access)
              << getOpenMPClauseName(OMPC_reduction)
              << getOpenMPClauseName(OMPC_shared);
          reportOriginalDsa(S, Stack, D, DVar);
          continue;
        }
      }
    } else {
      // Threadprivates cannot be shared between threads, so dignose if the base
      // is a threadprivate variable.
      DSAStackTy::DSAVarData DVar = Stack->getTopDSA(D, /*FromParent=*/false);
      if (DVar.CKind == OMPC_threadprivate) {
        S.Diag(ELoc, diag::err_omp_wrong_dsa)
            << getOpenMPClauseName(DVar.CKind)
            << getOpenMPClauseName(OMPC_reduction);
        reportOriginalDsa(S, Stack, D, DVar);
        continue;
      }
    }

    // Try to find 'declare reduction' corresponding construct before using
    // builtin/overloaded operators.
    CXXCastPath BasePath;
    ExprResult DeclareReductionRef = buildDeclareReductionRef(
        S, ELoc, ERange, Stack->getCurScope(), ReductionIdScopeSpec,
        ReductionId, Type, BasePath, IR == ER ? nullptr : *IR);
    if (DeclareReductionRef.isInvalid())
      continue;
    if (S.CurContext->isDependentContext() &&
        (DeclareReductionRef.isUnset() ||
         isa<UnresolvedLookupExpr>(DeclareReductionRef.get()))) {
      RD.push(RefExpr, DeclareReductionRef.get());
      continue;
    }
    if (BOK == BO_Comma && DeclareReductionRef.isUnset()) {
      // Not allowed reduction identifier is found.
      if (S.LangOpts.OpenMP > 52)
        S.Diag(ReductionId.getBeginLoc(),
               diag::err_omp_unknown_reduction_identifier_since_omp_6_0)
            << Type << ReductionIdRange;
      else
        S.Diag(ReductionId.getBeginLoc(),
               diag::err_omp_unknown_reduction_identifier_prior_omp_6_0)
            << Type << ReductionIdRange;
      continue;
    }

    // OpenMP [2.14.3.6, reduction clause, Restrictions]
    // The type of a list item that appears in a reduction clause must be valid
    // for the reduction-identifier. For a max or min reduction in C, the type
    // of the list item must be an allowed arithmetic data type: char, int,
    // float, double, or _Bool, possibly modified with long, short, signed, or
    // unsigned. For a max or min reduction in C++, the type of the list item
    // must be an allowed arithmetic data type: char, wchar_t, int, float,
    // double, or bool, possibly modified with long, short, signed, or unsigned.
    if (DeclareReductionRef.isUnset()) {
      if ((BOK == BO_GT || BOK == BO_LT) &&
          !(Type->isScalarType() ||
            (S.getLangOpts().CPlusPlus && Type->isArithmeticType()))) {
        S.Diag(ELoc, diag::err_omp_clause_not_arithmetic_type_arg)
            << getOpenMPClauseName(ClauseKind) << S.getLangOpts().CPlusPlus;
        if (!ASE && !OASE) {
          bool IsDecl = !VD || VD->isThisDeclarationADefinition(Context) ==
                                   VarDecl::DeclarationOnly;
          S.Diag(D->getLocation(),
                 IsDecl ? diag::note_previous_decl : diag::note_defined_here)
              << D;
        }
        continue;
      }
      if ((BOK == BO_OrAssign || BOK == BO_AndAssign || BOK == BO_XorAssign) &&
          !S.getLangOpts().CPlusPlus && Type->isFloatingType()) {
        S.Diag(ELoc, diag::err_omp_clause_floating_type_arg)
            << getOpenMPClauseName(ClauseKind);
        if (!ASE && !OASE) {
          bool IsDecl = !VD || VD->isThisDeclarationADefinition(Context) ==
                                   VarDecl::DeclarationOnly;
          S.Diag(D->getLocation(),
                 IsDecl ? diag::note_previous_decl : diag::note_defined_here)
              << D;
        }
        continue;
      }
    }

    Type = Type.getNonLValueExprType(Context).getUnqualifiedType();
    VarDecl *LHSVD = buildVarDecl(S, ELoc, Type, ".reduction.lhs",
                                  D->hasAttrs() ? &D->getAttrs() : nullptr);
    VarDecl *RHSVD = buildVarDecl(S, ELoc, Type, D->getName(),
                                  D->hasAttrs() ? &D->getAttrs() : nullptr);
    QualType PrivateTy = Type;

    // Try if we can determine constant lengths for all array sections and avoid
    // the VLA.
    bool ConstantLengthOASE = false;
    if (OASE) {
      bool SingleElement;
      llvm::SmallVector<llvm::APSInt, 4> ArraySizes;
      ConstantLengthOASE = checkOMPArraySectionConstantForReduction(
          Context, OASE, SingleElement, ArraySizes);

      // If we don't have a single element, we must emit a constant array type.
      if (ConstantLengthOASE && !SingleElement) {
        for (llvm::APSInt &Size : ArraySizes)
          PrivateTy = Context.getConstantArrayType(PrivateTy, Size, nullptr,
                                                   ArraySizeModifier::Normal,
                                                   /*IndexTypeQuals=*/0);
      }
    }

    if ((OASE && !ConstantLengthOASE) ||
        (!OASE && !ASE &&
         D->getType().getNonReferenceType()->isVariablyModifiedType())) {
      if (!Context.getTargetInfo().isVLASupported()) {
        if (isOpenMPTargetExecutionDirective(Stack->getCurrentDirective())) {
          S.Diag(ELoc, diag::err_omp_reduction_vla_unsupported) << !!OASE;
          S.Diag(ELoc, diag::note_vla_unsupported);
          continue;
        } else {
          S.targetDiag(ELoc, diag::err_omp_reduction_vla_unsupported) << !!OASE;
          S.targetDiag(ELoc, diag::note_vla_unsupported);
        }
      }
      // For arrays/array sections only:
      // Create pseudo array type for private copy. The size for this array will
      // be generated during codegen.
      // For array subscripts or single variables Private Ty is the same as Type
      // (type of the variable or single array element).
      PrivateTy = Context.getVariableArrayType(
          Type,
          new (Context)
              OpaqueValueExpr(ELoc, Context.getSizeType(), VK_PRValue),
          ArraySizeModifier::Normal, /*IndexTypeQuals=*/0, SourceRange());
    } else if (!ASE && !OASE &&
               Context.getAsArrayType(D->getType().getNonReferenceType())) {
      PrivateTy = D->getType().getNonReferenceType();
    }
    // Private copy.
    VarDecl *PrivateVD =
        buildVarDecl(S, ELoc, PrivateTy, D->getName(),
                     D->hasAttrs() ? &D->getAttrs() : nullptr,
                     VD ? cast<DeclRefExpr>(SimpleRefExpr) : nullptr);
    // Add initializer for private variable.
    Expr *Init = nullptr;
    DeclRefExpr *LHSDRE = buildDeclRefExpr(S, LHSVD, Type, ELoc);
    DeclRefExpr *RHSDRE = buildDeclRefExpr(S, RHSVD, Type, ELoc);
    if (DeclareReductionRef.isUsable()) {
      auto *DRDRef = DeclareReductionRef.getAs<DeclRefExpr>();
      auto *DRD = cast<OMPDeclareReductionDecl>(DRDRef->getDecl());
      if (DRD->getInitializer()) {
        Init = DRDRef;
        RHSVD->setInit(DRDRef);
        RHSVD->setInitStyle(VarDecl::CallInit);
      }
    } else {
      switch (BOK) {
      case BO_Add:
      case BO_Xor:
      case BO_Or:
      case BO_LOr:
        // '+', '-', '^', '|', '||' reduction ops - initializer is '0'.
        if (Type->isScalarType() || Type->isAnyComplexType())
          Init = S.ActOnIntegerConstant(ELoc, /*Val=*/0).get();
        break;
      case BO_Mul:
      case BO_LAnd:
        if (Type->isScalarType() || Type->isAnyComplexType()) {
          // '*' and '&&' reduction ops - initializer is '1'.
          Init = S.ActOnIntegerConstant(ELoc, /*Val=*/1).get();
        }
        break;
      case BO_And: {
        // '&' reduction op - initializer is '~0'.
        QualType OrigType = Type;
        if (auto *ComplexTy = OrigType->getAs<ComplexType>())
          Type = ComplexTy->getElementType();
        if (Type->isRealFloatingType()) {
          llvm::APFloat InitValue = llvm::APFloat::getAllOnesValue(
              Context.getFloatTypeSemantics(Type));
          Init = FloatingLiteral::Create(Context, InitValue, /*isexact=*/true,
                                         Type, ELoc);
        } else if (Type->isScalarType()) {
          uint64_t Size = Context.getTypeSize(Type);
          QualType IntTy = Context.getIntTypeForBitwidth(Size, /*Signed=*/0);
          llvm::APInt InitValue = llvm::APInt::getAllOnes(Size);
          Init = IntegerLiteral::Create(Context, InitValue, IntTy, ELoc);
        }
        if (Init && OrigType->isAnyComplexType()) {
          // Init = 0xFFFF + 0xFFFFi;
          auto *Im = new (Context) ImaginaryLiteral(Init, OrigType);
          Init = S.CreateBuiltinBinOp(ELoc, BO_Add, Init, Im).get();
        }
        Type = OrigType;
        break;
      }
      case BO_LT:
      case BO_GT: {
        // 'min' reduction op - initializer is 'Largest representable number in
        // the reduction list item type'.
        // 'max' reduction op - initializer is 'Least representable number in
        // the reduction list item type'.
        if (Type->isIntegerType() || Type->isPointerType()) {
          bool IsSigned = Type->hasSignedIntegerRepresentation();
          uint64_t Size = Context.getTypeSize(Type);
          QualType IntTy =
              Context.getIntTypeForBitwidth(Size, /*Signed=*/IsSigned);
          llvm::APInt InitValue =
              (BOK != BO_LT) ? IsSigned ? llvm::APInt::getSignedMinValue(Size)
                                        : llvm::APInt::getMinValue(Size)
              : IsSigned ? llvm::APInt::getSignedMaxValue(Size)
                             : llvm::APInt::getMaxValue(Size);
          Init = IntegerLiteral::Create(Context, InitValue, IntTy, ELoc);
          if (Type->isPointerType()) {
            // Cast to pointer type.
            ExprResult CastExpr = S.BuildCStyleCastExpr(
                ELoc, Context.getTrivialTypeSourceInfo(Type, ELoc), ELoc, Init);
            if (CastExpr.isInvalid())
              continue;
            Init = CastExpr.get();
          }
        } else if (Type->isRealFloatingType()) {
          llvm::APFloat InitValue = llvm::APFloat::getLargest(
              Context.getFloatTypeSemantics(Type), BOK != BO_LT);
          Init = FloatingLiteral::Create(Context, InitValue, /*isexact=*/true,
                                         Type, ELoc);
        }
        break;
      }
      case BO_PtrMemD:
      case BO_PtrMemI:
      case BO_MulAssign:
      case BO_Div:
      case BO_Rem:
      case BO_Sub:
      case BO_Shl:
      case BO_Shr:
      case BO_LE:
      case BO_GE:
      case BO_EQ:
      case BO_NE:
      case BO_Cmp:
      case BO_AndAssign:
      case BO_XorAssign:
      case BO_OrAssign:
      case BO_Assign:
      case BO_AddAssign:
      case BO_SubAssign:
      case BO_DivAssign:
      case BO_RemAssign:
      case BO_ShlAssign:
      case BO_ShrAssign:
      case BO_Comma:
        llvm_unreachable("Unexpected reduction operation");
      }
    }
    if (Init && DeclareReductionRef.isUnset()) {
      S.AddInitializerToDecl(RHSVD, Init, /*DirectInit=*/false);
      // Store initializer for single element in private copy. Will be used
      // during codegen.
      PrivateVD->setInit(RHSVD->getInit());
      PrivateVD->setInitStyle(RHSVD->getInitStyle());
    } else if (!Init) {
      S.ActOnUninitializedDecl(RHSVD);
      // Store initializer for single element in private copy. Will be used
      // during codegen.
      PrivateVD->setInit(RHSVD->getInit());
      PrivateVD->setInitStyle(RHSVD->getInitStyle());
    }
    if (RHSVD->isInvalidDecl())
      continue;
    if (!RHSVD->hasInit() && DeclareReductionRef.isUnset()) {
      S.Diag(ELoc, diag::err_omp_reduction_id_not_compatible)
          << Type << ReductionIdRange;
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(Context) ==
                               VarDecl::DeclarationOnly;
      S.Diag(D->getLocation(),
             IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
      continue;
    }
    DeclRefExpr *PrivateDRE = buildDeclRefExpr(S, PrivateVD, PrivateTy, ELoc);
    ExprResult ReductionOp;
    if (DeclareReductionRef.isUsable()) {
      QualType RedTy = DeclareReductionRef.get()->getType();
      QualType PtrRedTy = Context.getPointerType(RedTy);
      ExprResult LHS = S.CreateBuiltinUnaryOp(ELoc, UO_AddrOf, LHSDRE);
      ExprResult RHS = S.CreateBuiltinUnaryOp(ELoc, UO_AddrOf, RHSDRE);
      if (!BasePath.empty()) {
        LHS = S.DefaultLvalueConversion(LHS.get());
        RHS = S.DefaultLvalueConversion(RHS.get());
        LHS = ImplicitCastExpr::Create(
            Context, PtrRedTy, CK_UncheckedDerivedToBase, LHS.get(), &BasePath,
            LHS.get()->getValueKind(), FPOptionsOverride());
        RHS = ImplicitCastExpr::Create(
            Context, PtrRedTy, CK_UncheckedDerivedToBase, RHS.get(), &BasePath,
            RHS.get()->getValueKind(), FPOptionsOverride());
      }
      FunctionProtoType::ExtProtoInfo EPI;
      QualType Params[] = {PtrRedTy, PtrRedTy};
      QualType FnTy = Context.getFunctionType(Context.VoidTy, Params, EPI);
      auto *OVE = new (Context) OpaqueValueExpr(
          ELoc, Context.getPointerType(FnTy), VK_PRValue, OK_Ordinary,
          S.DefaultLvalueConversion(DeclareReductionRef.get()).get());
      Expr *Args[] = {LHS.get(), RHS.get()};
      ReductionOp =
          CallExpr::Create(Context, OVE, Args, Context.VoidTy, VK_PRValue, ELoc,
                           S.CurFPFeatureOverrides());
    } else {
      BinaryOperatorKind CombBOK = getRelatedCompoundReductionOp(BOK);
      if (Type->isRecordType() && CombBOK != BOK) {
        Sema::TentativeAnalysisScope Trap(S);
        ReductionOp =
            S.BuildBinOp(Stack->getCurScope(), ReductionId.getBeginLoc(),
                         CombBOK, LHSDRE, RHSDRE);
      }
      if (!ReductionOp.isUsable()) {
        ReductionOp =
            S.BuildBinOp(Stack->getCurScope(), ReductionId.getBeginLoc(), BOK,
                         LHSDRE, RHSDRE);
        if (ReductionOp.isUsable()) {
          if (BOK != BO_LT && BOK != BO_GT) {
            ReductionOp =
                S.BuildBinOp(Stack->getCurScope(), ReductionId.getBeginLoc(),
                             BO_Assign, LHSDRE, ReductionOp.get());
          } else {
            auto *ConditionalOp = new (Context)
                ConditionalOperator(ReductionOp.get(), ELoc, LHSDRE, ELoc,
                                    RHSDRE, Type, VK_LValue, OK_Ordinary);
            ReductionOp =
                S.BuildBinOp(Stack->getCurScope(), ReductionId.getBeginLoc(),
                             BO_Assign, LHSDRE, ConditionalOp);
          }
        }
      }
      if (ReductionOp.isUsable())
        ReductionOp = S.ActOnFinishFullExpr(ReductionOp.get(),
                                            /*DiscardedValue*/ false);
      if (!ReductionOp.isUsable())
        continue;
    }

    // Add copy operations for inscan reductions.
    // LHS = RHS;
    ExprResult CopyOpRes, TempArrayRes, TempArrayElem;
    if (ClauseKind == OMPC_reduction &&
        RD.RedModifier == OMPC_REDUCTION_inscan) {
      ExprResult RHS = S.DefaultLvalueConversion(RHSDRE);
      CopyOpRes = S.BuildBinOp(Stack->getCurScope(), ELoc, BO_Assign, LHSDRE,
                               RHS.get());
      if (!CopyOpRes.isUsable())
        continue;
      CopyOpRes =
          S.ActOnFinishFullExpr(CopyOpRes.get(), /*DiscardedValue=*/true);
      if (!CopyOpRes.isUsable())
        continue;
      // For simd directive and simd-based directives in simd mode no need to
      // construct temp array, need just a single temp element.
      if (Stack->getCurrentDirective() == OMPD_simd ||
          (S.getLangOpts().OpenMPSimd &&
           isOpenMPSimdDirective(Stack->getCurrentDirective()))) {
        VarDecl *TempArrayVD =
            buildVarDecl(S, ELoc, PrivateTy, D->getName(),
                         D->hasAttrs() ? &D->getAttrs() : nullptr);
        // Add a constructor to the temp decl.
        S.ActOnUninitializedDecl(TempArrayVD);
        TempArrayRes = buildDeclRefExpr(S, TempArrayVD, PrivateTy, ELoc);
      } else {
        // Build temp array for prefix sum.
        auto *Dim = new (S.Context)
            OpaqueValueExpr(ELoc, S.Context.getSizeType(), VK_PRValue);
        QualType ArrayTy = S.Context.getVariableArrayType(
            PrivateTy, Dim, ArraySizeModifier::Normal,
            /*IndexTypeQuals=*/0, {ELoc, ELoc});
        VarDecl *TempArrayVD =
            buildVarDecl(S, ELoc, ArrayTy, D->getName(),
                         D->hasAttrs() ? &D->getAttrs() : nullptr);
        // Add a constructor to the temp decl.
        S.ActOnUninitializedDecl(TempArrayVD);
        TempArrayRes = buildDeclRefExpr(S, TempArrayVD, ArrayTy, ELoc);
        TempArrayElem =
            S.DefaultFunctionArrayLvalueConversion(TempArrayRes.get());
        auto *Idx = new (S.Context)
            OpaqueValueExpr(ELoc, S.Context.getSizeType(), VK_PRValue);
        TempArrayElem = S.CreateBuiltinArraySubscriptExpr(TempArrayElem.get(),
                                                          ELoc, Idx, ELoc);
      }
    }

    // OpenMP [2.15.4.6, Restrictions, p.2]
    // A list item that appears in an in_reduction clause of a task construct
    // must appear in a task_reduction clause of a construct associated with a
    // taskgroup region that includes the participating task in its taskgroup
    // set. The construct associated with the innermost region that meets this
    // condition must specify the same reduction-identifier as the in_reduction
    // clause.
    if (ClauseKind == OMPC_in_reduction) {
      SourceRange ParentSR;
      BinaryOperatorKind ParentBOK;
      const Expr *ParentReductionOp = nullptr;
      Expr *ParentBOKTD = nullptr, *ParentReductionOpTD = nullptr;
      DSAStackTy::DSAVarData ParentBOKDSA =
          Stack->getTopMostTaskgroupReductionData(D, ParentSR, ParentBOK,
                                                  ParentBOKTD);
      DSAStackTy::DSAVarData ParentReductionOpDSA =
          Stack->getTopMostTaskgroupReductionData(
              D, ParentSR, ParentReductionOp, ParentReductionOpTD);
      bool IsParentBOK = ParentBOKDSA.DKind != OMPD_unknown;
      bool IsParentReductionOp = ParentReductionOpDSA.DKind != OMPD_unknown;
      if ((DeclareReductionRef.isUnset() && IsParentReductionOp) ||
          (DeclareReductionRef.isUsable() && IsParentBOK) ||
          (IsParentBOK && BOK != ParentBOK) || IsParentReductionOp) {
        bool EmitError = true;
        if (IsParentReductionOp && DeclareReductionRef.isUsable()) {
          llvm::FoldingSetNodeID RedId, ParentRedId;
          ParentReductionOp->Profile(ParentRedId, Context, /*Canonical=*/true);
          DeclareReductionRef.get()->Profile(RedId, Context,
                                             /*Canonical=*/true);
          EmitError = RedId != ParentRedId;
        }
        if (EmitError) {
          S.Diag(ReductionId.getBeginLoc(),
                 diag::err_omp_reduction_identifier_mismatch)
              << ReductionIdRange << RefExpr->getSourceRange();
          S.Diag(ParentSR.getBegin(),
                 diag::note_omp_previous_reduction_identifier)
              << ParentSR
              << (IsParentBOK ? ParentBOKDSA.RefExpr
                              : ParentReductionOpDSA.RefExpr)
                     ->getSourceRange();
          continue;
        }
      }
      TaskgroupDescriptor = IsParentBOK ? ParentBOKTD : ParentReductionOpTD;
    }

    DeclRefExpr *Ref = nullptr;
    Expr *VarsExpr = RefExpr->IgnoreParens();
    if (!VD && !S.CurContext->isDependentContext()) {
      if (ASE || OASE) {
        TransformExprToCaptures RebuildToCapture(S, D);
        VarsExpr =
            RebuildToCapture.TransformExpr(RefExpr->IgnoreParens()).get();
        Ref = RebuildToCapture.getCapturedExpr();
      } else {
        VarsExpr = Ref = buildCapture(S, D, SimpleRefExpr, /*WithInit=*/false);
      }
      if (!S.OpenMP().isOpenMPCapturedDecl(D)) {
        RD.ExprCaptures.emplace_back(Ref->getDecl());
        if (Ref->getDecl()->hasAttr<OMPCaptureNoInitAttr>()) {
          ExprResult RefRes = S.DefaultLvalueConversion(Ref);
          if (!RefRes.isUsable())
            continue;
          ExprResult PostUpdateRes =
              S.BuildBinOp(Stack->getCurScope(), ELoc, BO_Assign, SimpleRefExpr,
                           RefRes.get());
          if (!PostUpdateRes.isUsable())
            continue;
          if (isOpenMPTaskingDirective(Stack->getCurrentDirective()) ||
              Stack->getCurrentDirective() == OMPD_taskgroup) {
            S.Diag(RefExpr->getExprLoc(),
                   diag::err_omp_reduction_non_addressable_expression)
                << RefExpr->getSourceRange();
            continue;
          }
          RD.ExprPostUpdates.emplace_back(
              S.IgnoredValueConversions(PostUpdateRes.get()).get());
        }
      }
    }
    // All reduction items are still marked as reduction (to do not increase
    // code base size).
    unsigned Modifier = RD.RedModifier;
    // Consider task_reductions as reductions with task modifier. Required for
    // correct analysis of in_reduction clauses.
    if (CurrDir == OMPD_taskgroup && ClauseKind == OMPC_task_reduction)
      Modifier = OMPC_REDUCTION_task;
    Stack->addDSA(D, RefExpr->IgnoreParens(), OMPC_reduction, Ref, Modifier,
                  ASE || OASE);
    if (Modifier == OMPC_REDUCTION_task &&
        (CurrDir == OMPD_taskgroup ||
         ((isOpenMPParallelDirective(CurrDir) ||
           isOpenMPWorksharingDirective(CurrDir)) &&
          !isOpenMPSimdDirective(CurrDir)))) {
      if (DeclareReductionRef.isUsable())
        Stack->addTaskgroupReductionData(D, ReductionIdRange,
                                         DeclareReductionRef.get());
      else
        Stack->addTaskgroupReductionData(D, ReductionIdRange, BOK);
    }
    RD.push(VarsExpr, PrivateDRE, LHSDRE, RHSDRE, ReductionOp.get(),
            TaskgroupDescriptor, CopyOpRes.get(), TempArrayRes.get(),
            TempArrayElem.get());
  }
  return RD.Vars.empty();
}

OMPClause *SemaOpenMP::ActOnOpenMPReductionClause(
    ArrayRef<Expr *> VarList, OpenMPReductionClauseModifier Modifier,
    SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ModifierLoc, SourceLocation ColonLoc, SourceLocation EndLoc,
    CXXScopeSpec &ReductionIdScopeSpec, const DeclarationNameInfo &ReductionId,
    ArrayRef<Expr *> UnresolvedReductions) {
  if (ModifierLoc.isValid() && Modifier == OMPC_REDUCTION_unknown) {
    Diag(LParenLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_reduction, /*First=*/0,
                                   /*Last=*/OMPC_REDUCTION_unknown)
        << getOpenMPClauseName(OMPC_reduction);
    return nullptr;
  }
  // OpenMP 5.0, 2.19.5.4 reduction Clause, Restrictions
  // A reduction clause with the inscan reduction-modifier may only appear on a
  // worksharing-loop construct, a worksharing-loop SIMD construct, a simd
  // construct, a parallel worksharing-loop construct or a parallel
  // worksharing-loop SIMD construct.
  if (Modifier == OMPC_REDUCTION_inscan &&
      (DSAStack->getCurrentDirective() != OMPD_for &&
       DSAStack->getCurrentDirective() != OMPD_for_simd &&
       DSAStack->getCurrentDirective() != OMPD_simd &&
       DSAStack->getCurrentDirective() != OMPD_parallel_for &&
       DSAStack->getCurrentDirective() != OMPD_parallel_for_simd)) {
    Diag(ModifierLoc, diag::err_omp_wrong_inscan_reduction);
    return nullptr;
  }

  ReductionData RD(VarList.size(), Modifier);
  if (actOnOMPReductionKindClause(SemaRef, DSAStack, OMPC_reduction, VarList,
                                  StartLoc, LParenLoc, ColonLoc, EndLoc,
                                  ReductionIdScopeSpec, ReductionId,
                                  UnresolvedReductions, RD))
    return nullptr;

  return OMPReductionClause::Create(
      getASTContext(), StartLoc, LParenLoc, ModifierLoc, ColonLoc, EndLoc,
      Modifier, RD.Vars,
      ReductionIdScopeSpec.getWithLocInContext(getASTContext()), ReductionId,
      RD.Privates, RD.LHSs, RD.RHSs, RD.ReductionOps, RD.InscanCopyOps,
      RD.InscanCopyArrayTemps, RD.InscanCopyArrayElems,
      buildPreInits(getASTContext(), RD.ExprCaptures),
      buildPostUpdate(SemaRef, RD.ExprPostUpdates));
}

OMPClause *SemaOpenMP::ActOnOpenMPTaskReductionClause(
    ArrayRef<Expr *> VarList, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ColonLoc, SourceLocation EndLoc,
    CXXScopeSpec &ReductionIdScopeSpec, const DeclarationNameInfo &ReductionId,
    ArrayRef<Expr *> UnresolvedReductions) {
  ReductionData RD(VarList.size());
  if (actOnOMPReductionKindClause(SemaRef, DSAStack, OMPC_task_reduction,
                                  VarList, StartLoc, LParenLoc, ColonLoc,
                                  EndLoc, ReductionIdScopeSpec, ReductionId,
                                  UnresolvedReductions, RD))
    return nullptr;

  return OMPTaskReductionClause::Create(
      getASTContext(), StartLoc, LParenLoc, ColonLoc, EndLoc, RD.Vars,
      ReductionIdScopeSpec.getWithLocInContext(getASTContext()), ReductionId,
      RD.Privates, RD.LHSs, RD.RHSs, RD.ReductionOps,
      buildPreInits(getASTContext(), RD.ExprCaptures),
      buildPostUpdate(SemaRef, RD.ExprPostUpdates));
}

OMPClause *SemaOpenMP::ActOnOpenMPInReductionClause(
    ArrayRef<Expr *> VarList, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ColonLoc, SourceLocation EndLoc,
    CXXScopeSpec &ReductionIdScopeSpec, const DeclarationNameInfo &ReductionId,
    ArrayRef<Expr *> UnresolvedReductions) {
  ReductionData RD(VarList.size());
  if (actOnOMPReductionKindClause(SemaRef, DSAStack, OMPC_in_reduction, VarList,
                                  StartLoc, LParenLoc, ColonLoc, EndLoc,
                                  ReductionIdScopeSpec, ReductionId,
                                  UnresolvedReductions, RD))
    return nullptr;

  return OMPInReductionClause::Create(
      getASTContext(), StartLoc, LParenLoc, ColonLoc, EndLoc, RD.Vars,
      ReductionIdScopeSpec.getWithLocInContext(getASTContext()), ReductionId,
      RD.Privates, RD.LHSs, RD.RHSs, RD.ReductionOps, RD.TaskgroupDescriptors,
      buildPreInits(getASTContext(), RD.ExprCaptures),
      buildPostUpdate(SemaRef, RD.ExprPostUpdates));
}

bool SemaOpenMP::CheckOpenMPLinearModifier(OpenMPLinearClauseKind LinKind,
                                           SourceLocation LinLoc) {
  if ((!getLangOpts().CPlusPlus && LinKind != OMPC_LINEAR_val) ||
      LinKind == OMPC_LINEAR_unknown || LinKind == OMPC_LINEAR_step) {
    Diag(LinLoc, diag::err_omp_wrong_linear_modifier)
        << getLangOpts().CPlusPlus;
    return true;
  }
  return false;
}

bool SemaOpenMP::CheckOpenMPLinearDecl(const ValueDecl *D, SourceLocation ELoc,
                                       OpenMPLinearClauseKind LinKind,
                                       QualType Type, bool IsDeclareSimd) {
  const auto *VD = dyn_cast_or_null<VarDecl>(D);
  // A variable must not have an incomplete type or a reference type.
  if (SemaRef.RequireCompleteType(ELoc, Type,
                                  diag::err_omp_linear_incomplete_type))
    return true;
  if ((LinKind == OMPC_LINEAR_uval || LinKind == OMPC_LINEAR_ref) &&
      !Type->isReferenceType()) {
    Diag(ELoc, diag::err_omp_wrong_linear_modifier_non_reference)
        << Type << getOpenMPSimpleClauseTypeName(OMPC_linear, LinKind);
    return true;
  }
  Type = Type.getNonReferenceType();

  // OpenMP 5.0 [2.19.3, List Item Privatization, Restrictions]
  // A variable that is privatized must not have a const-qualified type
  // unless it is of class type with a mutable member. This restriction does
  // not apply to the firstprivate clause, nor to the linear clause on
  // declarative directives (like declare simd).
  if (!IsDeclareSimd &&
      rejectConstNotMutableType(SemaRef, D, Type, OMPC_linear, ELoc))
    return true;

  // A list item must be of integral or pointer type.
  Type = Type.getUnqualifiedType().getCanonicalType();
  const auto *Ty = Type.getTypePtrOrNull();
  if (!Ty || (LinKind != OMPC_LINEAR_ref && !Ty->isDependentType() &&
              !Ty->isIntegralType(getASTContext()) && !Ty->isPointerType())) {
    Diag(ELoc, diag::err_omp_linear_expected_int_or_ptr) << Type;
    if (D) {
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(getASTContext()) ==
                               VarDecl::DeclarationOnly;
      Diag(D->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
    }
    return true;
  }
  return false;
}

OMPClause *SemaOpenMP::ActOnOpenMPLinearClause(
    ArrayRef<Expr *> VarList, Expr *Step, SourceLocation StartLoc,
    SourceLocation LParenLoc, OpenMPLinearClauseKind LinKind,
    SourceLocation LinLoc, SourceLocation ColonLoc,
    SourceLocation StepModifierLoc, SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  SmallVector<Expr *, 8> Privates;
  SmallVector<Expr *, 8> Inits;
  SmallVector<Decl *, 4> ExprCaptures;
  SmallVector<Expr *, 4> ExprPostUpdates;
  // OpenMP 5.2 [Section 5.4.6, linear clause]
  // step-simple-modifier is exclusive, can't be used with 'val', 'uval', or
  // 'ref'
  if (LinLoc.isValid() && StepModifierLoc.isInvalid() && Step &&
      getLangOpts().OpenMP >= 52)
    Diag(Step->getBeginLoc(), diag::err_omp_step_simple_modifier_exclusive);
  if (CheckOpenMPLinearModifier(LinKind, LinLoc))
    LinKind = OMPC_LINEAR_val;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP linear clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      Privates.push_back(nullptr);
      Inits.push_back(nullptr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType Type = D->getType();
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP [2.14.3.7, linear clause]
    //  A list-item cannot appear in more than one linear clause.
    //  A list-item that appears in a linear clause cannot appear in any
    //  other data-sharing attribute clause.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(D, /*FromParent=*/false);
    if (DVar.RefExpr) {
      Diag(ELoc, diag::err_omp_wrong_dsa) << getOpenMPClauseName(DVar.CKind)
                                          << getOpenMPClauseName(OMPC_linear);
      reportOriginalDsa(SemaRef, DSAStack, D, DVar);
      continue;
    }

    if (CheckOpenMPLinearDecl(D, ELoc, LinKind, Type))
      continue;
    Type = Type.getNonReferenceType().getUnqualifiedType().getCanonicalType();

    // Build private copy of original var.
    VarDecl *Private =
        buildVarDecl(SemaRef, ELoc, Type, D->getName(),
                     D->hasAttrs() ? &D->getAttrs() : nullptr,
                     VD ? cast<DeclRefExpr>(SimpleRefExpr) : nullptr);
    DeclRefExpr *PrivateRef = buildDeclRefExpr(SemaRef, Private, Type, ELoc);
    // Build var to save initial value.
    VarDecl *Init = buildVarDecl(SemaRef, ELoc, Type, ".linear.start");
    Expr *InitExpr;
    DeclRefExpr *Ref = nullptr;
    if (!VD && !SemaRef.CurContext->isDependentContext()) {
      Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/false);
      if (!isOpenMPCapturedDecl(D)) {
        ExprCaptures.push_back(Ref->getDecl());
        if (Ref->getDecl()->hasAttr<OMPCaptureNoInitAttr>()) {
          ExprResult RefRes = SemaRef.DefaultLvalueConversion(Ref);
          if (!RefRes.isUsable())
            continue;
          ExprResult PostUpdateRes =
              SemaRef.BuildBinOp(DSAStack->getCurScope(), ELoc, BO_Assign,
                                 SimpleRefExpr, RefRes.get());
          if (!PostUpdateRes.isUsable())
            continue;
          ExprPostUpdates.push_back(
              SemaRef.IgnoredValueConversions(PostUpdateRes.get()).get());
        }
      }
    }
    if (LinKind == OMPC_LINEAR_uval)
      InitExpr = VD ? VD->getInit() : SimpleRefExpr;
    else
      InitExpr = VD ? SimpleRefExpr : Ref;
    SemaRef.AddInitializerToDecl(
        Init, SemaRef.DefaultLvalueConversion(InitExpr).get(),
        /*DirectInit=*/false);
    DeclRefExpr *InitRef = buildDeclRefExpr(SemaRef, Init, Type, ELoc);

    DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_linear, Ref);
    Vars.push_back((VD || SemaRef.CurContext->isDependentContext())
                       ? RefExpr->IgnoreParens()
                       : Ref);
    Privates.push_back(PrivateRef);
    Inits.push_back(InitRef);
  }

  if (Vars.empty())
    return nullptr;

  Expr *StepExpr = Step;
  Expr *CalcStepExpr = nullptr;
  if (Step && !Step->isValueDependent() && !Step->isTypeDependent() &&
      !Step->isInstantiationDependent() &&
      !Step->containsUnexpandedParameterPack()) {
    SourceLocation StepLoc = Step->getBeginLoc();
    ExprResult Val = PerformOpenMPImplicitIntegerConversion(StepLoc, Step);
    if (Val.isInvalid())
      return nullptr;
    StepExpr = Val.get();

    // Build var to save the step value.
    VarDecl *SaveVar =
        buildVarDecl(SemaRef, StepLoc, StepExpr->getType(), ".linear.step");
    ExprResult SaveRef =
        buildDeclRefExpr(SemaRef, SaveVar, StepExpr->getType(), StepLoc);
    ExprResult CalcStep = SemaRef.BuildBinOp(
        SemaRef.getCurScope(), StepLoc, BO_Assign, SaveRef.get(), StepExpr);
    CalcStep =
        SemaRef.ActOnFinishFullExpr(CalcStep.get(), /*DiscardedValue*/ false);

    // Warn about zero linear step (it would be probably better specified as
    // making corresponding variables 'const').
    if (std::optional<llvm::APSInt> Result =
            StepExpr->getIntegerConstantExpr(getASTContext())) {
      if (!Result->isNegative() && !Result->isStrictlyPositive())
        Diag(StepLoc, diag::warn_omp_linear_step_zero)
            << Vars[0] << (Vars.size() > 1);
    } else if (CalcStep.isUsable()) {
      // Calculate the step beforehand instead of doing this on each iteration.
      // (This is not used if the number of iterations may be kfold-ed).
      CalcStepExpr = CalcStep.get();
    }
  }

  return OMPLinearClause::Create(getASTContext(), StartLoc, LParenLoc, LinKind,
                                 LinLoc, ColonLoc, StepModifierLoc, EndLoc,
                                 Vars, Privates, Inits, StepExpr, CalcStepExpr,
                                 buildPreInits(getASTContext(), ExprCaptures),
                                 buildPostUpdate(SemaRef, ExprPostUpdates));
}

static bool FinishOpenMPLinearClause(OMPLinearClause &Clause, DeclRefExpr *IV,
                                     Expr *NumIterations, Sema &SemaRef,
                                     Scope *S, DSAStackTy *Stack) {
  // Walk the vars and build update/final expressions for the CodeGen.
  SmallVector<Expr *, 8> Updates;
  SmallVector<Expr *, 8> Finals;
  SmallVector<Expr *, 8> UsedExprs;
  Expr *Step = Clause.getStep();
  Expr *CalcStep = Clause.getCalcStep();
  // OpenMP [2.14.3.7, linear clause]
  // If linear-step is not specified it is assumed to be 1.
  if (!Step)
    Step = SemaRef.ActOnIntegerConstant(SourceLocation(), 1).get();
  else if (CalcStep)
    Step = cast<BinaryOperator>(CalcStep)->getLHS();
  bool HasErrors = false;
  auto CurInit = Clause.inits().begin();
  auto CurPrivate = Clause.privates().begin();
  OpenMPLinearClauseKind LinKind = Clause.getModifier();
  for (Expr *RefExpr : Clause.varlists()) {
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    ValueDecl *D = Res.first;
    if (Res.second || !D) {
      Updates.push_back(nullptr);
      Finals.push_back(nullptr);
      HasErrors = true;
      continue;
    }
    auto &&Info = Stack->isLoopControlVariable(D);
    // OpenMP [2.15.11, distribute simd Construct]
    // A list item may not appear in a linear clause, unless it is the loop
    // iteration variable.
    if (isOpenMPDistributeDirective(Stack->getCurrentDirective()) &&
        isOpenMPSimdDirective(Stack->getCurrentDirective()) && !Info.first) {
      SemaRef.Diag(ELoc,
                   diag::err_omp_linear_distribute_var_non_loop_iteration);
      Updates.push_back(nullptr);
      Finals.push_back(nullptr);
      HasErrors = true;
      continue;
    }
    Expr *InitExpr = *CurInit;

    // Build privatized reference to the current linear var.
    auto *DE = cast<DeclRefExpr>(SimpleRefExpr);
    Expr *CapturedRef;
    if (LinKind == OMPC_LINEAR_uval)
      CapturedRef = cast<VarDecl>(DE->getDecl())->getInit();
    else
      CapturedRef =
          buildDeclRefExpr(SemaRef, cast<VarDecl>(DE->getDecl()),
                           DE->getType().getUnqualifiedType(), DE->getExprLoc(),
                           /*RefersToCapture=*/true);

    // Build update: Var = InitExpr + IV * Step
    ExprResult Update;
    if (!Info.first)
      Update = buildCounterUpdate(
          SemaRef, S, RefExpr->getExprLoc(), *CurPrivate, InitExpr, IV, Step,
          /*Subtract=*/false, /*IsNonRectangularLB=*/false);
    else
      Update = *CurPrivate;
    Update = SemaRef.ActOnFinishFullExpr(Update.get(), DE->getBeginLoc(),
                                         /*DiscardedValue*/ false);

    // Build final: Var = PrivCopy;
    ExprResult Final;
    if (!Info.first)
      Final = SemaRef.BuildBinOp(
          S, RefExpr->getExprLoc(), BO_Assign, CapturedRef,
          SemaRef.DefaultLvalueConversion(*CurPrivate).get());
    else
      Final = *CurPrivate;
    Final = SemaRef.ActOnFinishFullExpr(Final.get(), DE->getBeginLoc(),
                                        /*DiscardedValue*/ false);

    if (!Update.isUsable() || !Final.isUsable()) {
      Updates.push_back(nullptr);
      Finals.push_back(nullptr);
      UsedExprs.push_back(nullptr);
      HasErrors = true;
    } else {
      Updates.push_back(Update.get());
      Finals.push_back(Final.get());
      if (!Info.first)
        UsedExprs.push_back(SimpleRefExpr);
    }
    ++CurInit;
    ++CurPrivate;
  }
  if (Expr *S = Clause.getStep())
    UsedExprs.push_back(S);
  // Fill the remaining part with the nullptr.
  UsedExprs.append(Clause.varlist_size() + 1 - UsedExprs.size(), nullptr);
  Clause.setUpdates(Updates);
  Clause.setFinals(Finals);
  Clause.setUsedExprs(UsedExprs);
  return HasErrors;
}

OMPClause *SemaOpenMP::ActOnOpenMPAlignedClause(
    ArrayRef<Expr *> VarList, Expr *Alignment, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP linear clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType QType = D->getType();
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP  [2.8.1, simd construct, Restrictions]
    // The type of list items appearing in the aligned clause must be
    // array, pointer, reference to array, or reference to pointer.
    QType = QType.getNonReferenceType().getUnqualifiedType().getCanonicalType();
    const Type *Ty = QType.getTypePtrOrNull();
    if (!Ty || (!Ty->isArrayType() && !Ty->isPointerType())) {
      Diag(ELoc, diag::err_omp_aligned_expected_array_or_ptr)
          << QType << getLangOpts().CPlusPlus << ERange;
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(getASTContext()) ==
                               VarDecl::DeclarationOnly;
      Diag(D->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
      continue;
    }

    // OpenMP  [2.8.1, simd construct, Restrictions]
    // A list-item cannot appear in more than one aligned clause.
    if (const Expr *PrevRef = DSAStack->addUniqueAligned(D, SimpleRefExpr)) {
      Diag(ELoc, diag::err_omp_used_in_clause_twice)
          << 0 << getOpenMPClauseName(OMPC_aligned) << ERange;
      Diag(PrevRef->getExprLoc(), diag::note_omp_explicit_dsa)
          << getOpenMPClauseName(OMPC_aligned);
      continue;
    }

    DeclRefExpr *Ref = nullptr;
    if (!VD && isOpenMPCapturedDecl(D))
      Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/true);
    Vars.push_back(SemaRef
                       .DefaultFunctionArrayConversion(
                           (VD || !Ref) ? RefExpr->IgnoreParens() : Ref)
                       .get());
  }

  // OpenMP [2.8.1, simd construct, Description]
  // The parameter of the aligned clause, alignment, must be a constant
  // positive integer expression.
  // If no optional parameter is specified, implementation-defined default
  // alignments for SIMD instructions on the target platforms are assumed.
  if (Alignment != nullptr) {
    ExprResult AlignResult =
        VerifyPositiveIntegerConstantInClause(Alignment, OMPC_aligned);
    if (AlignResult.isInvalid())
      return nullptr;
    Alignment = AlignResult.get();
  }
  if (Vars.empty())
    return nullptr;

  return OMPAlignedClause::Create(getASTContext(), StartLoc, LParenLoc,
                                  ColonLoc, EndLoc, Vars, Alignment);
}

OMPClause *SemaOpenMP::ActOnOpenMPCopyinClause(ArrayRef<Expr *> VarList,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  SmallVector<Expr *, 8> SrcExprs;
  SmallVector<Expr *, 8> DstExprs;
  SmallVector<Expr *, 8> AssignmentOps;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP copyin clause.");
    if (isa<DependentScopeDeclRefExpr>(RefExpr)) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      SrcExprs.push_back(nullptr);
      DstExprs.push_back(nullptr);
      AssignmentOps.push_back(nullptr);
      continue;
    }

    SourceLocation ELoc = RefExpr->getExprLoc();
    // OpenMP [2.1, C/C++]
    //  A list item is a variable name.
    // OpenMP  [2.14.4.1, Restrictions, p.1]
    //  A list item that appears in a copyin clause must be threadprivate.
    auto *DE = dyn_cast<DeclRefExpr>(RefExpr);
    if (!DE || !isa<VarDecl>(DE->getDecl())) {
      Diag(ELoc, diag::err_omp_expected_var_name_member_expr)
          << 0 << RefExpr->getSourceRange();
      continue;
    }

    Decl *D = DE->getDecl();
    auto *VD = cast<VarDecl>(D);

    QualType Type = VD->getType();
    if (Type->isDependentType() || Type->isInstantiationDependentType()) {
      // It will be analyzed later.
      Vars.push_back(DE);
      SrcExprs.push_back(nullptr);
      DstExprs.push_back(nullptr);
      AssignmentOps.push_back(nullptr);
      continue;
    }

    // OpenMP [2.14.4.1, Restrictions, C/C++, p.1]
    //  A list item that appears in a copyin clause must be threadprivate.
    if (!DSAStack->isThreadPrivate(VD)) {
      Diag(ELoc, diag::err_omp_required_access)
          << getOpenMPClauseName(OMPC_copyin)
          << getOpenMPDirectiveName(OMPD_threadprivate);
      continue;
    }

    // OpenMP [2.14.4.1, Restrictions, C/C++, p.2]
    //  A variable of class type (or array thereof) that appears in a
    //  copyin clause requires an accessible, unambiguous copy assignment
    //  operator for the class type.
    QualType ElemType =
        getASTContext().getBaseElementType(Type).getNonReferenceType();
    VarDecl *SrcVD =
        buildVarDecl(SemaRef, DE->getBeginLoc(), ElemType.getUnqualifiedType(),
                     ".copyin.src", VD->hasAttrs() ? &VD->getAttrs() : nullptr);
    DeclRefExpr *PseudoSrcExpr = buildDeclRefExpr(
        SemaRef, SrcVD, ElemType.getUnqualifiedType(), DE->getExprLoc());
    VarDecl *DstVD =
        buildVarDecl(SemaRef, DE->getBeginLoc(), ElemType, ".copyin.dst",
                     VD->hasAttrs() ? &VD->getAttrs() : nullptr);
    DeclRefExpr *PseudoDstExpr =
        buildDeclRefExpr(SemaRef, DstVD, ElemType, DE->getExprLoc());
    // For arrays generate assignment operation for single element and replace
    // it by the original array element in CodeGen.
    ExprResult AssignmentOp =
        SemaRef.BuildBinOp(/*S=*/nullptr, DE->getExprLoc(), BO_Assign,
                           PseudoDstExpr, PseudoSrcExpr);
    if (AssignmentOp.isInvalid())
      continue;
    AssignmentOp =
        SemaRef.ActOnFinishFullExpr(AssignmentOp.get(), DE->getExprLoc(),
                                    /*DiscardedValue*/ false);
    if (AssignmentOp.isInvalid())
      continue;

    DSAStack->addDSA(VD, DE, OMPC_copyin);
    Vars.push_back(DE);
    SrcExprs.push_back(PseudoSrcExpr);
    DstExprs.push_back(PseudoDstExpr);
    AssignmentOps.push_back(AssignmentOp.get());
  }

  if (Vars.empty())
    return nullptr;

  return OMPCopyinClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                 Vars, SrcExprs, DstExprs, AssignmentOps);
}

OMPClause *SemaOpenMP::ActOnOpenMPCopyprivateClause(ArrayRef<Expr *> VarList,
                                                    SourceLocation StartLoc,
                                                    SourceLocation LParenLoc,
                                                    SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  SmallVector<Expr *, 8> SrcExprs;
  SmallVector<Expr *, 8> DstExprs;
  SmallVector<Expr *, 8> AssignmentOps;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP linear clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      SrcExprs.push_back(nullptr);
      DstExprs.push_back(nullptr);
      AssignmentOps.push_back(nullptr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType Type = D->getType();
    auto *VD = dyn_cast<VarDecl>(D);

    // OpenMP [2.14.4.2, Restrictions, p.2]
    //  A list item that appears in a copyprivate clause may not appear in a
    //  private or firstprivate clause on the single construct.
    if (!VD || !DSAStack->isThreadPrivate(VD)) {
      DSAStackTy::DSAVarData DVar =
          DSAStack->getTopDSA(D, /*FromParent=*/false);
      if (DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_copyprivate &&
          DVar.RefExpr) {
        Diag(ELoc, diag::err_omp_wrong_dsa)
            << getOpenMPClauseName(DVar.CKind)
            << getOpenMPClauseName(OMPC_copyprivate);
        reportOriginalDsa(SemaRef, DSAStack, D, DVar);
        continue;
      }

      // OpenMP [2.11.4.2, Restrictions, p.1]
      //  All list items that appear in a copyprivate clause must be either
      //  threadprivate or private in the enclosing context.
      if (DVar.CKind == OMPC_unknown) {
        DVar = DSAStack->getImplicitDSA(D, false);
        if (DVar.CKind == OMPC_shared) {
          Diag(ELoc, diag::err_omp_required_access)
              << getOpenMPClauseName(OMPC_copyprivate)
              << "threadprivate or private in the enclosing context";
          reportOriginalDsa(SemaRef, DSAStack, D, DVar);
          continue;
        }
      }
    }

    // Variably modified types are not supported.
    if (!Type->isAnyPointerType() && Type->isVariablyModifiedType()) {
      Diag(ELoc, diag::err_omp_variably_modified_type_not_supported)
          << getOpenMPClauseName(OMPC_copyprivate) << Type
          << getOpenMPDirectiveName(DSAStack->getCurrentDirective());
      bool IsDecl = !VD || VD->isThisDeclarationADefinition(getASTContext()) ==
                               VarDecl::DeclarationOnly;
      Diag(D->getLocation(),
           IsDecl ? diag::note_previous_decl : diag::note_defined_here)
          << D;
      continue;
    }

    // OpenMP [2.14.4.1, Restrictions, C/C++, p.2]
    //  A variable of class type (or array thereof) that appears in a
    //  copyin clause requires an accessible, unambiguous copy assignment
    //  operator for the class type.
    Type = getASTContext()
               .getBaseElementType(Type.getNonReferenceType())
               .getUnqualifiedType();
    VarDecl *SrcVD =
        buildVarDecl(SemaRef, RefExpr->getBeginLoc(), Type, ".copyprivate.src",
                     D->hasAttrs() ? &D->getAttrs() : nullptr);
    DeclRefExpr *PseudoSrcExpr = buildDeclRefExpr(SemaRef, SrcVD, Type, ELoc);
    VarDecl *DstVD =
        buildVarDecl(SemaRef, RefExpr->getBeginLoc(), Type, ".copyprivate.dst",
                     D->hasAttrs() ? &D->getAttrs() : nullptr);
    DeclRefExpr *PseudoDstExpr = buildDeclRefExpr(SemaRef, DstVD, Type, ELoc);
    ExprResult AssignmentOp = SemaRef.BuildBinOp(
        DSAStack->getCurScope(), ELoc, BO_Assign, PseudoDstExpr, PseudoSrcExpr);
    if (AssignmentOp.isInvalid())
      continue;
    AssignmentOp = SemaRef.ActOnFinishFullExpr(AssignmentOp.get(), ELoc,
                                               /*DiscardedValue*/ false);
    if (AssignmentOp.isInvalid())
      continue;

    // No need to mark vars as copyprivate, they are already threadprivate or
    // implicitly private.
    assert(VD || isOpenMPCapturedDecl(D));
    Vars.push_back(
        VD ? RefExpr->IgnoreParens()
           : buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/false));
    SrcExprs.push_back(PseudoSrcExpr);
    DstExprs.push_back(PseudoDstExpr);
    AssignmentOps.push_back(AssignmentOp.get());
  }

  if (Vars.empty())
    return nullptr;

  return OMPCopyprivateClause::Create(getASTContext(), StartLoc, LParenLoc,
                                      EndLoc, Vars, SrcExprs, DstExprs,
                                      AssignmentOps);
}

OMPClause *SemaOpenMP::ActOnOpenMPFlushClause(ArrayRef<Expr *> VarList,
                                              SourceLocation StartLoc,
                                              SourceLocation LParenLoc,
                                              SourceLocation EndLoc) {
  if (VarList.empty())
    return nullptr;

  return OMPFlushClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                VarList);
}

/// Tries to find omp_depend_t. type.
static bool findOMPDependT(Sema &S, SourceLocation Loc, DSAStackTy *Stack,
                           bool Diagnose = true) {
  QualType OMPDependT = Stack->getOMPDependT();
  if (!OMPDependT.isNull())
    return true;
  IdentifierInfo *II = &S.PP.getIdentifierTable().get("omp_depend_t");
  ParsedType PT = S.getTypeName(*II, Loc, S.getCurScope());
  if (!PT.getAsOpaquePtr() || PT.get().isNull()) {
    if (Diagnose)
      S.Diag(Loc, diag::err_omp_implied_type_not_found) << "omp_depend_t";
    return false;
  }
  Stack->setOMPDependT(PT.get());
  return true;
}

OMPClause *SemaOpenMP::ActOnOpenMPDepobjClause(Expr *Depobj,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc) {
  if (!Depobj)
    return nullptr;

  bool OMPDependTFound = findOMPDependT(SemaRef, StartLoc, DSAStack);

  // OpenMP 5.0, 2.17.10.1 depobj Construct
  // depobj is an lvalue expression of type omp_depend_t.
  if (!Depobj->isTypeDependent() && !Depobj->isValueDependent() &&
      !Depobj->isInstantiationDependent() &&
      !Depobj->containsUnexpandedParameterPack() &&
      (OMPDependTFound && !getASTContext().typesAreCompatible(
                              DSAStack->getOMPDependT(), Depobj->getType(),
                              /*CompareUnqualified=*/true))) {
    Diag(Depobj->getExprLoc(), diag::err_omp_expected_omp_depend_t_lvalue)
        << 0 << Depobj->getType() << Depobj->getSourceRange();
  }

  if (!Depobj->isLValue()) {
    Diag(Depobj->getExprLoc(), diag::err_omp_expected_omp_depend_t_lvalue)
        << 1 << Depobj->getSourceRange();
  }

  return OMPDepobjClause::Create(getASTContext(), StartLoc, LParenLoc, EndLoc,
                                 Depobj);
}

namespace {
// Utility struct that gathers the related info for doacross clause.
struct DoacrossDataInfoTy {
  // The list of expressions.
  SmallVector<Expr *, 8> Vars;
  // The OperatorOffset for doacross loop.
  DSAStackTy::OperatorOffsetTy OpsOffs;
  // The depended loop count.
  llvm::APSInt TotalDepCount;
};
} // namespace
static DoacrossDataInfoTy
ProcessOpenMPDoacrossClauseCommon(Sema &SemaRef, bool IsSource,
                                  ArrayRef<Expr *> VarList, DSAStackTy *Stack,
                                  SourceLocation EndLoc) {

  SmallVector<Expr *, 8> Vars;
  DSAStackTy::OperatorOffsetTy OpsOffs;
  llvm::APSInt DepCounter(/*BitWidth=*/32);
  llvm::APSInt TotalDepCount(/*BitWidth=*/32);

  if (const Expr *OrderedCountExpr =
          Stack->getParentOrderedRegionParam().first) {
    TotalDepCount = OrderedCountExpr->EvaluateKnownConstInt(SemaRef.Context);
    TotalDepCount.setIsUnsigned(/*Val=*/true);
  }

  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP doacross clause.");
    if (isa<DependentScopeDeclRefExpr>(RefExpr)) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      continue;
    }

    SourceLocation ELoc = RefExpr->getExprLoc();
    Expr *SimpleExpr = RefExpr->IgnoreParenCasts();
    if (!IsSource) {
      if (Stack->getParentOrderedRegionParam().first &&
          DepCounter >= TotalDepCount) {
        SemaRef.Diag(ELoc, diag::err_omp_depend_sink_unexpected_expr);
        continue;
      }
      ++DepCounter;
      // OpenMP  [2.13.9, Summary]
      // depend(dependence-type : vec), where dependence-type is:
      // 'sink' and where vec is the iteration vector, which has the form:
      //  x1 [+- d1], x2 [+- d2 ], . . . , xn [+- dn]
      // where n is the value specified by the ordered clause in the loop
      // directive, xi denotes the loop iteration variable of the i-th nested
      // loop associated with the loop directive, and di is a constant
      // non-negative integer.
      if (SemaRef.CurContext->isDependentContext()) {
        // It will be analyzed later.
        Vars.push_back(RefExpr);
        continue;
      }
      SimpleExpr = SimpleExpr->IgnoreImplicit();
      OverloadedOperatorKind OOK = OO_None;
      SourceLocation OOLoc;
      Expr *LHS = SimpleExpr;
      Expr *RHS = nullptr;
      if (auto *BO = dyn_cast<BinaryOperator>(SimpleExpr)) {
        OOK = BinaryOperator::getOverloadedOperator(BO->getOpcode());
        OOLoc = BO->getOperatorLoc();
        LHS = BO->getLHS()->IgnoreParenImpCasts();
        RHS = BO->getRHS()->IgnoreParenImpCasts();
      } else if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(SimpleExpr)) {
        OOK = OCE->getOperator();
        OOLoc = OCE->getOperatorLoc();
        LHS = OCE->getArg(/*Arg=*/0)->IgnoreParenImpCasts();
        RHS = OCE->getArg(/*Arg=*/1)->IgnoreParenImpCasts();
      } else if (auto *MCE = dyn_cast<CXXMemberCallExpr>(SimpleExpr)) {
        OOK = MCE->getMethodDecl()
                  ->getNameInfo()
                  .getName()
                  .getCXXOverloadedOperator();
        OOLoc = MCE->getCallee()->getExprLoc();
        LHS = MCE->getImplicitObjectArgument()->IgnoreParenImpCasts();
        RHS = MCE->getArg(/*Arg=*/0)->IgnoreParenImpCasts();
      }
      SourceLocation ELoc;
      SourceRange ERange;
      auto Res = getPrivateItem(SemaRef, LHS, ELoc, ERange);
      if (Res.second) {
        // It will be analyzed later.
        Vars.push_back(RefExpr);
      }
      ValueDecl *D = Res.first;
      if (!D)
        continue;

      if (OOK != OO_Plus && OOK != OO_Minus && (RHS || OOK != OO_None)) {
        SemaRef.Diag(OOLoc, diag::err_omp_depend_sink_expected_plus_minus);
        continue;
      }
      if (RHS) {
        ExprResult RHSRes =
            SemaRef.OpenMP().VerifyPositiveIntegerConstantInClause(
                RHS, OMPC_depend, /*StrictlyPositive=*/false);
        if (RHSRes.isInvalid())
          continue;
      }
      if (!SemaRef.CurContext->isDependentContext() &&
          Stack->getParentOrderedRegionParam().first &&
          DepCounter != Stack->isParentLoopControlVariable(D).first) {
        const ValueDecl *VD =
            Stack->getParentLoopControlVariable(DepCounter.getZExtValue());
        if (VD)
          SemaRef.Diag(ELoc, diag::err_omp_depend_sink_expected_loop_iteration)
              << 1 << VD;
        else
          SemaRef.Diag(ELoc, diag::err_omp_depend_sink_expected_loop_iteration)
              << 0;
        continue;
      }
      OpsOffs.emplace_back(RHS, OOK);
    }
    Vars.push_back(RefExpr->IgnoreParenImpCasts());
  }
  if (!SemaRef.CurContext->isDependentContext() && !IsSource &&
      TotalDepCount > VarList.size() &&
      Stack->getParentOrderedRegionParam().first &&
      Stack->getParentLoopControlVariable(VarList.size() + 1)) {
    SemaRef.Diag(EndLoc, diag::err_omp_depend_sink_expected_loop_iteration)
        << 1 << Stack->getParentLoopControlVariable(VarList.size() + 1);
  }
  return {Vars, OpsOffs, TotalDepCount};
}

OMPClause *SemaOpenMP::ActOnOpenMPDependClause(
    const OMPDependClause::DependDataTy &Data, Expr *DepModifier,
    ArrayRef<Expr *> VarList, SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation EndLoc) {
  OpenMPDependClauseKind DepKind = Data.DepKind;
  SourceLocation DepLoc = Data.DepLoc;
  if (DSAStack->getCurrentDirective() == OMPD_ordered &&
      DepKind != OMPC_DEPEND_source && DepKind != OMPC_DEPEND_sink) {
    Diag(DepLoc, diag::err_omp_unexpected_clause_value)
        << "'source' or 'sink'" << getOpenMPClauseName(OMPC_depend);
    return nullptr;
  }
  if (DSAStack->getCurrentDirective() == OMPD_taskwait &&
      DepKind == OMPC_DEPEND_mutexinoutset) {
    Diag(DepLoc, diag::err_omp_taskwait_depend_mutexinoutset_not_allowed);
    return nullptr;
  }
  if ((DSAStack->getCurrentDirective() != OMPD_ordered ||
       DSAStack->getCurrentDirective() == OMPD_depobj) &&
      (DepKind == OMPC_DEPEND_unknown || DepKind == OMPC_DEPEND_source ||
       DepKind == OMPC_DEPEND_sink ||
       ((getLangOpts().OpenMP < 50 ||
         DSAStack->getCurrentDirective() == OMPD_depobj) &&
        DepKind == OMPC_DEPEND_depobj))) {
    SmallVector<unsigned, 6> Except = {OMPC_DEPEND_source, OMPC_DEPEND_sink,
                                       OMPC_DEPEND_outallmemory,
                                       OMPC_DEPEND_inoutallmemory};
    if (getLangOpts().OpenMP < 50 ||
        DSAStack->getCurrentDirective() == OMPD_depobj)
      Except.push_back(OMPC_DEPEND_depobj);
    if (getLangOpts().OpenMP < 51)
      Except.push_back(OMPC_DEPEND_inoutset);
    std::string Expected = (getLangOpts().OpenMP >= 50 && !DepModifier)
                               ? "depend modifier(iterator) or "
                               : "";
    Diag(DepLoc, diag::err_omp_unexpected_clause_value)
        << Expected + getListOfPossibleValues(OMPC_depend, /*First=*/0,
                                              /*Last=*/OMPC_DEPEND_unknown,
                                              Except)
        << getOpenMPClauseName(OMPC_depend);
    return nullptr;
  }
  if (DepModifier &&
      (DepKind == OMPC_DEPEND_source || DepKind == OMPC_DEPEND_sink)) {
    Diag(DepModifier->getExprLoc(),
         diag::err_omp_depend_sink_source_with_modifier);
    return nullptr;
  }
  if (DepModifier &&
      !DepModifier->getType()->isSpecificBuiltinType(BuiltinType::OMPIterator))
    Diag(DepModifier->getExprLoc(), diag::err_omp_depend_modifier_not_iterator);

  SmallVector<Expr *, 8> Vars;
  DSAStackTy::OperatorOffsetTy OpsOffs;
  llvm::APSInt TotalDepCount(/*BitWidth=*/32);

  if (DepKind == OMPC_DEPEND_sink || DepKind == OMPC_DEPEND_source) {
    DoacrossDataInfoTy VarOffset = ProcessOpenMPDoacrossClauseCommon(
        SemaRef, DepKind == OMPC_DEPEND_source, VarList, DSAStack, EndLoc);
    Vars = VarOffset.Vars;
    OpsOffs = VarOffset.OpsOffs;
    TotalDepCount = VarOffset.TotalDepCount;
  } else {
    for (Expr *RefExpr : VarList) {
      assert(RefExpr && "NULL expr in OpenMP shared clause.");
      if (isa<DependentScopeDeclRefExpr>(RefExpr)) {
        // It will be analyzed later.
        Vars.push_back(RefExpr);
        continue;
      }

      SourceLocation ELoc = RefExpr->getExprLoc();
      Expr *SimpleExpr = RefExpr->IgnoreParenCasts();
      if (DepKind != OMPC_DEPEND_sink && DepKind != OMPC_DEPEND_source) {
        bool OMPDependTFound = getLangOpts().OpenMP >= 50;
        if (OMPDependTFound)
          OMPDependTFound = findOMPDependT(SemaRef, StartLoc, DSAStack,
                                           DepKind == OMPC_DEPEND_depobj);
        if (DepKind == OMPC_DEPEND_depobj) {
          // OpenMP 5.0, 2.17.11 depend Clause, Restrictions, C/C++
          // List items used in depend clauses with the depobj dependence type
          // must be expressions of the omp_depend_t type.
          if (!RefExpr->isValueDependent() && !RefExpr->isTypeDependent() &&
              !RefExpr->isInstantiationDependent() &&
              !RefExpr->containsUnexpandedParameterPack() &&
              (OMPDependTFound &&
               !getASTContext().hasSameUnqualifiedType(
                   DSAStack->getOMPDependT(), RefExpr->getType()))) {
            Diag(ELoc, diag::err_omp_expected_omp_depend_t_lvalue)
                << 0 << RefExpr->getType() << RefExpr->getSourceRange();
            continue;
          }
          if (!RefExpr->isLValue()) {
            Diag(ELoc, diag::err_omp_expected_omp_depend_t_lvalue)
                << 1 << RefExpr->getType() << RefExpr->getSourceRange();
            continue;
          }
        } else {
          // OpenMP 5.0 [2.17.11, Restrictions]
          // List items used in depend clauses cannot be zero-length array
          // sections.
          QualType ExprTy = RefExpr->getType().getNonReferenceType();
          const auto *OASE = dyn_cast<ArraySectionExpr>(SimpleExpr);
          if (OASE) {
            QualType BaseType =
                ArraySectionExpr::getBaseOriginalType(OASE->getBase());
            if (BaseType.isNull())
              return nullptr;
            if (const auto *ATy = BaseType->getAsArrayTypeUnsafe())
              ExprTy = ATy->getElementType();
            else
              ExprTy = BaseType->getPointeeType();
            if (BaseType.isNull() || ExprTy.isNull())
              return nullptr;
            ExprTy = ExprTy.getNonReferenceType();
            const Expr *Length = OASE->getLength();
            Expr::EvalResult Result;
            if (Length && !Length->isValueDependent() &&
                Length->EvaluateAsInt(Result, getASTContext()) &&
                Result.Val.getInt().isZero()) {
              Diag(ELoc,
                   diag::err_omp_depend_zero_length_array_section_not_allowed)
                  << SimpleExpr->getSourceRange();
              continue;
            }
          }

          // OpenMP 5.0, 2.17.11 depend Clause, Restrictions, C/C++
          // List items used in depend clauses with the in, out, inout,
          // inoutset, or mutexinoutset dependence types cannot be
          // expressions of the omp_depend_t type.
          if (!RefExpr->isValueDependent() && !RefExpr->isTypeDependent() &&
              !RefExpr->isInstantiationDependent() &&
              !RefExpr->containsUnexpandedParameterPack() &&
              (!RefExpr->IgnoreParenImpCasts()->isLValue() ||
               (OMPDependTFound && DSAStack->getOMPDependT().getTypePtr() ==
                                       ExprTy.getTypePtr()))) {
            Diag(ELoc, diag::err_omp_expected_addressable_lvalue_or_array_item)
                << (getLangOpts().OpenMP >= 50 ? 1 : 0)
                << (getLangOpts().OpenMP >= 50 ? 1 : 0)
                << RefExpr->getSourceRange();
            continue;
          }

          auto *ASE = dyn_cast<ArraySubscriptExpr>(SimpleExpr);
          if (ASE && !ASE->getBase()->isTypeDependent() &&
              !ASE->getBase()
                   ->getType()
                   .getNonReferenceType()
                   ->isPointerType() &&
              !ASE->getBase()->getType().getNonReferenceType()->isArrayType()) {
            Diag(ELoc, diag::err_omp_expected_addressable_lvalue_or_array_item)
                << (getLangOpts().OpenMP >= 50 ? 1 : 0)
                << (getLangOpts().OpenMP >= 50 ? 1 : 0)
                << RefExpr->getSourceRange();
            continue;
          }

          ExprResult Res;
          {
            Sema::TentativeAnalysisScope Trap(SemaRef);
            Res = SemaRef.CreateBuiltinUnaryOp(ELoc, UO_AddrOf,
                                               RefExpr->IgnoreParenImpCasts());
          }
          if (!Res.isUsable() && !isa<ArraySectionExpr>(SimpleExpr) &&
              !isa<OMPArrayShapingExpr>(SimpleExpr)) {
            Diag(ELoc, diag::err_omp_expected_addressable_lvalue_or_array_item)
                << (getLangOpts().OpenMP >= 50 ? 1 : 0)
                << (getLangOpts().OpenMP >= 50 ? 1 : 0)
                << RefExpr->getSourceRange();
            continue;
          }
        }
      }
      Vars.push_back(RefExpr->IgnoreParenImpCasts());
    }
  }

  if (DepKind != OMPC_DEPEND_source && DepKind != OMPC_DEPEND_sink &&
      DepKind != OMPC_DEPEND_outallmemory &&
      DepKind != OMPC_DEPEND_inoutallmemory && Vars.empty())
    return nullptr;

  auto *C = OMPDependClause::Create(
      getASTContext(), StartLoc, LParenLoc, EndLoc,
      {DepKind, DepLoc, Data.ColonLoc, Data.OmpAllMemoryLoc}, DepModifier, Vars,
      TotalDepCount.getZExtValue());
  if ((DepKind == OMPC_DEPEND_sink || DepKind == OMPC_DEPEND_source) &&
      DSAStack->isParentOrderedRegion())
    DSAStack->addDoacrossDependClause(C, OpsOffs);
  return C;
}

OMPClause *SemaOpenMP::ActOnOpenMPDeviceClause(
    OpenMPDeviceClauseModifier Modifier, Expr *Device, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation ModifierLoc,
    SourceLocation EndLoc) {
  assert((ModifierLoc.isInvalid() || getLangOpts().OpenMP >= 50) &&
         "Unexpected device modifier in OpenMP < 50.");

  bool ErrorFound = false;
  if (ModifierLoc.isValid() && Modifier == OMPC_DEVICE_unknown) {
    std::string Values =
        getListOfPossibleValues(OMPC_device, /*First=*/0, OMPC_DEVICE_unknown);
    Diag(ModifierLoc, diag::err_omp_unexpected_clause_value)
        << Values << getOpenMPClauseName(OMPC_device);
    ErrorFound = true;
  }

  Expr *ValExpr = Device;
  Stmt *HelperValStmt = nullptr;

  // OpenMP [2.9.1, Restrictions]
  // The device expression must evaluate to a non-negative integer value.
  ErrorFound = !isNonNegativeIntegerValue(ValExpr, SemaRef, OMPC_device,
                                          /*StrictlyPositive=*/false) ||
               ErrorFound;
  if (ErrorFound)
    return nullptr;

  // OpenMP 5.0 [2.12.5, Restrictions]
  // In case of ancestor device-modifier, a requires directive with
  // the reverse_offload clause must be specified.
  if (Modifier == OMPC_DEVICE_ancestor) {
    if (!DSAStack->hasRequiresDeclWithClause<OMPReverseOffloadClause>()) {
      SemaRef.targetDiag(
          StartLoc,
          diag::err_omp_device_ancestor_without_requires_reverse_offload);
      ErrorFound = true;
    }
  }

  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  OpenMPDirectiveKind CaptureRegion =
      getOpenMPCaptureRegionForClause(DKind, OMPC_device, getLangOpts().OpenMP);
  if (CaptureRegion != OMPD_unknown &&
      !SemaRef.CurContext->isDependentContext()) {
    ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
    llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
    ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
    HelperValStmt = buildPreInits(getASTContext(), Captures);
  }

  return new (getASTContext())
      OMPDeviceClause(Modifier, ValExpr, HelperValStmt, CaptureRegion, StartLoc,
                      LParenLoc, ModifierLoc, EndLoc);
}

static bool checkTypeMappable(SourceLocation SL, SourceRange SR, Sema &SemaRef,
                              DSAStackTy *Stack, QualType QTy,
                              bool FullCheck = true) {
  if (SemaRef.RequireCompleteType(SL, QTy, diag::err_incomplete_type))
    return false;
  if (FullCheck && !SemaRef.CurContext->isDependentContext() &&
      !QTy.isTriviallyCopyableType(SemaRef.Context))
    SemaRef.Diag(SL, diag::warn_omp_non_trivial_type_mapped) << QTy << SR;
  return true;
}

/// Return true if it can be proven that the provided array expression
/// (array section or array subscript) does NOT specify the whole size of the
/// array whose base type is \a BaseQTy.
static bool checkArrayExpressionDoesNotReferToWholeSize(Sema &SemaRef,
                                                        const Expr *E,
                                                        QualType BaseQTy) {
  const auto *OASE = dyn_cast<ArraySectionExpr>(E);

  // If this is an array subscript, it refers to the whole size if the size of
  // the dimension is constant and equals 1. Also, an array section assumes the
  // format of an array subscript if no colon is used.
  if (isa<ArraySubscriptExpr>(E) ||
      (OASE && OASE->getColonLocFirst().isInvalid())) {
    if (const auto *ATy = dyn_cast<ConstantArrayType>(BaseQTy.getTypePtr()))
      return ATy->getSExtSize() != 1;
    // Size can't be evaluated statically.
    return false;
  }

  assert(OASE && "Expecting array section if not an array subscript.");
  const Expr *LowerBound = OASE->getLowerBound();
  const Expr *Length = OASE->getLength();

  // If there is a lower bound that does not evaluates to zero, we are not
  // covering the whole dimension.
  if (LowerBound) {
    Expr::EvalResult Result;
    if (!LowerBound->EvaluateAsInt(Result, SemaRef.getASTContext()))
      return false; // Can't get the integer value as a constant.

    llvm::APSInt ConstLowerBound = Result.Val.getInt();
    if (ConstLowerBound.getSExtValue())
      return true;
  }

  // If we don't have a length we covering the whole dimension.
  if (!Length)
    return false;

  // If the base is a pointer, we don't have a way to get the size of the
  // pointee.
  if (BaseQTy->isPointerType())
    return false;

  // We can only check if the length is the same as the size of the dimension
  // if we have a constant array.
  const auto *CATy = dyn_cast<ConstantArrayType>(BaseQTy.getTypePtr());
  if (!CATy)
    return false;

  Expr::EvalResult Result;
  if (!Length->EvaluateAsInt(Result, SemaRef.getASTContext()))
    return false; // Can't get the integer value as a constant.

  llvm::APSInt ConstLength = Result.Val.getInt();
  return CATy->getSExtSize() != ConstLength.getSExtValue();
}

// Return true if it can be proven that the provided array expression (array
// section or array subscript) does NOT specify a single element of the array
// whose base type is \a BaseQTy.
static bool checkArrayExpressionDoesNotReferToUnitySize(Sema &SemaRef,
                                                        const Expr *E,
                                                        QualType BaseQTy) {
  const auto *OASE = dyn_cast<ArraySectionExpr>(E);

  // An array subscript always refer to a single element. Also, an array section
  // assumes the format of an array subscript if no colon is used.
  if (isa<ArraySubscriptExpr>(E) ||
      (OASE && OASE->getColonLocFirst().isInvalid()))
    return false;

  assert(OASE && "Expecting array section if not an array subscript.");
  const Expr *Length = OASE->getLength();

  // If we don't have a length we have to check if the array has unitary size
  // for this dimension. Also, we should always expect a length if the base type
  // is pointer.
  if (!Length) {
    if (const auto *ATy = dyn_cast<ConstantArrayType>(BaseQTy.getTypePtr()))
      return ATy->getSExtSize() != 1;
    // We cannot assume anything.
    return false;
  }

  // Check if the length evaluates to 1.
  Expr::EvalResult Result;
  if (!Length->EvaluateAsInt(Result, SemaRef.getASTContext()))
    return false; // Can't get the integer value as a constant.

  llvm::APSInt ConstLength = Result.Val.getInt();
  return ConstLength.getSExtValue() != 1;
}

// The base of elements of list in a map clause have to be either:
//  - a reference to variable or field.
//  - a member expression.
//  - an array expression.
//
// E.g. if we have the expression 'r.S.Arr[:12]', we want to retrieve the
// reference to 'r'.
//
// If we have:
//
// struct SS {
//   Bla S;
//   foo() {
//     #pragma omp target map (S.Arr[:12]);
//   }
// }
//
// We want to retrieve the member expression 'this->S';

// OpenMP 5.0 [2.19.7.1, map Clause, Restrictions, p.2]
//  If a list item is an array section, it must specify contiguous storage.
//
// For this restriction it is sufficient that we make sure only references
// to variables or fields and array expressions, and that no array sections
// exist except in the rightmost expression (unless they cover the whole
// dimension of the array). E.g. these would be invalid:
//
//   r.ArrS[3:5].Arr[6:7]
//
//   r.ArrS[3:5].x
//
// but these would be valid:
//   r.ArrS[3].Arr[6:7]
//
//   r.ArrS[3].x
namespace {
class MapBaseChecker final : public StmtVisitor<MapBaseChecker, bool> {
  Sema &SemaRef;
  OpenMPClauseKind CKind = OMPC_unknown;
  OpenMPDirectiveKind DKind = OMPD_unknown;
  OMPClauseMappableExprCommon::MappableExprComponentList &Components;
  bool IsNonContiguous = false;
  bool NoDiagnose = false;
  const Expr *RelevantExpr = nullptr;
  bool AllowUnitySizeArraySection = true;
  bool AllowWholeSizeArraySection = true;
  bool AllowAnotherPtr = true;
  SourceLocation ELoc;
  SourceRange ERange;

  void emitErrorMsg() {
    // If nothing else worked, this is not a valid map clause expression.
    if (SemaRef.getLangOpts().OpenMP < 50) {
      SemaRef.Diag(ELoc,
                   diag::err_omp_expected_named_var_member_or_array_expression)
          << ERange;
    } else {
      SemaRef.Diag(ELoc, diag::err_omp_non_lvalue_in_map_or_motion_clauses)
          << getOpenMPClauseName(CKind) << ERange;
    }
  }

public:
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (!isa<VarDecl>(DRE->getDecl())) {
      emitErrorMsg();
      return false;
    }
    assert(!RelevantExpr && "RelevantExpr is expected to be nullptr");
    RelevantExpr = DRE;
    // Record the component.
    Components.emplace_back(DRE, DRE->getDecl(), IsNonContiguous);
    return true;
  }

  bool VisitMemberExpr(MemberExpr *ME) {
    Expr *E = ME;
    Expr *BaseE = ME->getBase()->IgnoreParenCasts();

    if (isa<CXXThisExpr>(BaseE)) {
      assert(!RelevantExpr && "RelevantExpr is expected to be nullptr");
      // We found a base expression: this->Val.
      RelevantExpr = ME;
    } else {
      E = BaseE;
    }

    if (!isa<FieldDecl>(ME->getMemberDecl())) {
      if (!NoDiagnose) {
        SemaRef.Diag(ELoc, diag::err_omp_expected_access_to_data_field)
            << ME->getSourceRange();
        return false;
      }
      if (RelevantExpr)
        return false;
      return Visit(E);
    }

    auto *FD = cast<FieldDecl>(ME->getMemberDecl());

    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C/C++, p.3]
    //  A bit-field cannot appear in a map clause.
    //
    if (FD->isBitField()) {
      if (!NoDiagnose) {
        SemaRef.Diag(ELoc, diag::err_omp_bit_fields_forbidden_in_clause)
            << ME->getSourceRange() << getOpenMPClauseName(CKind);
        return false;
      }
      if (RelevantExpr)
        return false;
      return Visit(E);
    }

    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C++, p.1]
    //  If the type of a list item is a reference to a type T then the type
    //  will be considered to be T for all purposes of this clause.
    QualType CurType = BaseE->getType().getNonReferenceType();

    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C/C++, p.2]
    //  A list item cannot be a variable that is a member of a structure with
    //  a union type.
    //
    if (CurType->isUnionType()) {
      if (!NoDiagnose) {
        SemaRef.Diag(ELoc, diag::err_omp_union_type_not_allowed)
            << ME->getSourceRange();
        return false;
      }
      return RelevantExpr || Visit(E);
    }

    // If we got a member expression, we should not expect any array section
    // before that:
    //
    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.7]
    //  If a list item is an element of a structure, only the rightmost symbol
    //  of the variable reference can be an array section.
    //
    AllowUnitySizeArraySection = false;
    AllowWholeSizeArraySection = false;

    // Record the component.
    Components.emplace_back(ME, FD, IsNonContiguous);
    return RelevantExpr || Visit(E);
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *AE) {
    Expr *E = AE->getBase()->IgnoreParenImpCasts();

    if (!E->getType()->isAnyPointerType() && !E->getType()->isArrayType()) {
      if (!NoDiagnose) {
        SemaRef.Diag(ELoc, diag::err_omp_expected_base_var_name)
            << 0 << AE->getSourceRange();
        return false;
      }
      return RelevantExpr || Visit(E);
    }

    // If we got an array subscript that express the whole dimension we
    // can have any array expressions before. If it only expressing part of
    // the dimension, we can only have unitary-size array expressions.
    if (checkArrayExpressionDoesNotReferToWholeSize(SemaRef, AE, E->getType()))
      AllowWholeSizeArraySection = false;

    if (const auto *TE = dyn_cast<CXXThisExpr>(E->IgnoreParenCasts())) {
      Expr::EvalResult Result;
      if (!AE->getIdx()->isValueDependent() &&
          AE->getIdx()->EvaluateAsInt(Result, SemaRef.getASTContext()) &&
          !Result.Val.getInt().isZero()) {
        SemaRef.Diag(AE->getIdx()->getExprLoc(),
                     diag::err_omp_invalid_map_this_expr);
        SemaRef.Diag(AE->getIdx()->getExprLoc(),
                     diag::note_omp_invalid_subscript_on_this_ptr_map);
      }
      assert(!RelevantExpr && "RelevantExpr is expected to be nullptr");
      RelevantExpr = TE;
    }

    // Record the component - we don't have any declaration associated.
    Components.emplace_back(AE, nullptr, IsNonContiguous);

    return RelevantExpr || Visit(E);
  }

  bool VisitArraySectionExpr(ArraySectionExpr *OASE) {
    // After OMP 5.0  Array section in reduction clause will be implicitly
    // mapped
    assert(!(SemaRef.getLangOpts().OpenMP < 50 && NoDiagnose) &&
           "Array sections cannot be implicitly mapped.");
    Expr *E = OASE->getBase()->IgnoreParenImpCasts();
    QualType CurType =
        ArraySectionExpr::getBaseOriginalType(E).getCanonicalType();

    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C++, p.1]
    //  If the type of a list item is a reference to a type T then the type
    //  will be considered to be T for all purposes of this clause.
    if (CurType->isReferenceType())
      CurType = CurType->getPointeeType();

    bool IsPointer = CurType->isAnyPointerType();

    if (!IsPointer && !CurType->isArrayType()) {
      SemaRef.Diag(ELoc, diag::err_omp_expected_base_var_name)
          << 0 << OASE->getSourceRange();
      return false;
    }

    bool NotWhole =
        checkArrayExpressionDoesNotReferToWholeSize(SemaRef, OASE, CurType);
    bool NotUnity =
        checkArrayExpressionDoesNotReferToUnitySize(SemaRef, OASE, CurType);

    if (AllowWholeSizeArraySection) {
      // Any array section is currently allowed. Allowing a whole size array
      // section implies allowing a unity array section as well.
      //
      // If this array section refers to the whole dimension we can still
      // accept other array sections before this one, except if the base is a
      // pointer. Otherwise, only unitary sections are accepted.
      if (NotWhole || IsPointer)
        AllowWholeSizeArraySection = false;
    } else if (DKind == OMPD_target_update &&
               SemaRef.getLangOpts().OpenMP >= 50) {
      if (IsPointer && !AllowAnotherPtr)
        SemaRef.Diag(ELoc, diag::err_omp_section_length_undefined)
            << /*array of unknown bound */ 1;
      else
        IsNonContiguous = true;
    } else if (AllowUnitySizeArraySection && NotUnity) {
      // A unity or whole array section is not allowed and that is not
      // compatible with the properties of the current array section.
      if (NoDiagnose)
        return false;
      SemaRef.Diag(ELoc,
                   diag::err_array_section_does_not_specify_contiguous_storage)
          << OASE->getSourceRange();
      return false;
    }

    if (IsPointer)
      AllowAnotherPtr = false;

    if (const auto *TE = dyn_cast<CXXThisExpr>(E)) {
      Expr::EvalResult ResultR;
      Expr::EvalResult ResultL;
      if (!OASE->getLength()->isValueDependent() &&
          OASE->getLength()->EvaluateAsInt(ResultR, SemaRef.getASTContext()) &&
          !ResultR.Val.getInt().isOne()) {
        SemaRef.Diag(OASE->getLength()->getExprLoc(),
                     diag::err_omp_invalid_map_this_expr);
        SemaRef.Diag(OASE->getLength()->getExprLoc(),
                     diag::note_omp_invalid_length_on_this_ptr_mapping);
      }
      if (OASE->getLowerBound() && !OASE->getLowerBound()->isValueDependent() &&
          OASE->getLowerBound()->EvaluateAsInt(ResultL,
                                               SemaRef.getASTContext()) &&
          !ResultL.Val.getInt().isZero()) {
        SemaRef.Diag(OASE->getLowerBound()->getExprLoc(),
                     diag::err_omp_invalid_map_this_expr);
        SemaRef.Diag(OASE->getLowerBound()->getExprLoc(),
                     diag::note_omp_invalid_lower_bound_on_this_ptr_mapping);
      }
      assert(!RelevantExpr && "RelevantExpr is expected to be nullptr");
      RelevantExpr = TE;
    }

    // Record the component - we don't have any declaration associated.
    Components.emplace_back(OASE, nullptr, /*IsNonContiguous=*/false);
    return RelevantExpr || Visit(E);
  }
  bool VisitOMPArrayShapingExpr(OMPArrayShapingExpr *E) {
    Expr *Base = E->getBase();

    // Record the component - we don't have any declaration associated.
    Components.emplace_back(E, nullptr, IsNonContiguous);

    return Visit(Base->IgnoreParenImpCasts());
  }

  bool VisitUnaryOperator(UnaryOperator *UO) {
    if (SemaRef.getLangOpts().OpenMP < 50 || !UO->isLValue() ||
        UO->getOpcode() != UO_Deref) {
      emitErrorMsg();
      return false;
    }
    if (!RelevantExpr) {
      // Record the component if haven't found base decl.
      Components.emplace_back(UO, nullptr, /*IsNonContiguous=*/false);
    }
    return RelevantExpr || Visit(UO->getSubExpr()->IgnoreParenImpCasts());
  }
  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (SemaRef.getLangOpts().OpenMP < 50 || !BO->getType()->isPointerType()) {
      emitErrorMsg();
      return false;
    }

    // Pointer arithmetic is the only thing we expect to happen here so after we
    // make sure the binary operator is a pointer type, the only thing we need
    // to do is to visit the subtree that has the same type as root (so that we
    // know the other subtree is just an offset)
    Expr *LE = BO->getLHS()->IgnoreParenImpCasts();
    Expr *RE = BO->getRHS()->IgnoreParenImpCasts();
    Components.emplace_back(BO, nullptr, false);
    assert((LE->getType().getTypePtr() == BO->getType().getTypePtr() ||
            RE->getType().getTypePtr() == BO->getType().getTypePtr()) &&
           "Either LHS or RHS have base decl inside");
    if (BO->getType().getTypePtr() == LE->getType().getTypePtr())
      return RelevantExpr || Visit(LE);
    return RelevantExpr || Visit(RE);
  }
  bool VisitCXXThisExpr(CXXThisExpr *CTE) {
    assert(!RelevantExpr && "RelevantExpr is expected to be nullptr");
    RelevantExpr = CTE;
    Components.emplace_back(CTE, nullptr, IsNonContiguous);
    return true;
  }
  bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *COCE) {
    assert(!RelevantExpr && "RelevantExpr is expected to be nullptr");
    Components.emplace_back(COCE, nullptr, IsNonContiguous);
    return true;
  }
  bool VisitOpaqueValueExpr(OpaqueValueExpr *E) {
    Expr *Source = E->getSourceExpr();
    if (!Source) {
      emitErrorMsg();
      return false;
    }
    return Visit(Source);
  }
  bool VisitStmt(Stmt *) {
    emitErrorMsg();
    return false;
  }
  const Expr *getFoundBase() const { return RelevantExpr; }
  explicit MapBaseChecker(
      Sema &SemaRef, OpenMPClauseKind CKind, OpenMPDirectiveKind DKind,
      OMPClauseMappableExprCommon::MappableExprComponentList &Components,
      bool NoDiagnose, SourceLocation &ELoc, SourceRange &ERange)
      : SemaRef(SemaRef), CKind(CKind), DKind(DKind), Components(Components),
        NoDiagnose(NoDiagnose), ELoc(ELoc), ERange(ERange) {}
};
} // namespace

/// Return the expression of the base of the mappable expression or null if it
/// cannot be determined and do all the necessary checks to see if the
/// expression is valid as a standalone mappable expression. In the process,
/// record all the components of the expression.
static const Expr *checkMapClauseExpressionBase(
    Sema &SemaRef, Expr *E,
    OMPClauseMappableExprCommon::MappableExprComponentList &CurComponents,
    OpenMPClauseKind CKind, OpenMPDirectiveKind DKind, bool NoDiagnose) {
  SourceLocation ELoc = E->getExprLoc();
  SourceRange ERange = E->getSourceRange();
  MapBaseChecker Checker(SemaRef, CKind, DKind, CurComponents, NoDiagnose, ELoc,
                         ERange);
  if (Checker.Visit(E->IgnoreParens())) {
    // Check if the highest dimension array section has length specified
    if (SemaRef.getLangOpts().OpenMP >= 50 && !CurComponents.empty() &&
        (CKind == OMPC_to || CKind == OMPC_from)) {
      auto CI = CurComponents.rbegin();
      auto CE = CurComponents.rend();
      for (; CI != CE; ++CI) {
        const auto *OASE =
            dyn_cast<ArraySectionExpr>(CI->getAssociatedExpression());
        if (!OASE)
          continue;
        if (OASE && OASE->getLength())
          break;
        SemaRef.Diag(ELoc, diag::err_array_section_does_not_specify_length)
            << ERange;
      }
    }
    return Checker.getFoundBase();
  }
  return nullptr;
}

// Return true if expression E associated with value VD has conflicts with other
// map information.
static bool checkMapConflicts(
    Sema &SemaRef, DSAStackTy *DSAS, const ValueDecl *VD, const Expr *E,
    bool CurrentRegionOnly,
    OMPClauseMappableExprCommon::MappableExprComponentListRef CurComponents,
    OpenMPClauseKind CKind) {
  assert(VD && E);
  SourceLocation ELoc = E->getExprLoc();
  SourceRange ERange = E->getSourceRange();

  // In order to easily check the conflicts we need to match each component of
  // the expression under test with the components of the expressions that are
  // already in the stack.

  assert(!CurComponents.empty() && "Map clause expression with no components!");
  assert(CurComponents.back().getAssociatedDeclaration() == VD &&
         "Map clause expression with unexpected base!");

  // Variables to help detecting enclosing problems in data environment nests.
  bool IsEnclosedByDataEnvironmentExpr = false;
  const Expr *EnclosingExpr = nullptr;

  bool FoundError = DSAS->checkMappableExprComponentListsForDecl(
      VD, CurrentRegionOnly,
      [&IsEnclosedByDataEnvironmentExpr, &SemaRef, VD, CurrentRegionOnly, ELoc,
       ERange, CKind, &EnclosingExpr,
       CurComponents](OMPClauseMappableExprCommon::MappableExprComponentListRef
                          StackComponents,
                      OpenMPClauseKind Kind) {
        if (CKind == Kind && SemaRef.LangOpts.OpenMP >= 50)
          return false;
        assert(!StackComponents.empty() &&
               "Map clause expression with no components!");
        assert(StackComponents.back().getAssociatedDeclaration() == VD &&
               "Map clause expression with unexpected base!");
        (void)VD;

        // The whole expression in the stack.
        const Expr *RE = StackComponents.front().getAssociatedExpression();

        // Expressions must start from the same base. Here we detect at which
        // point both expressions diverge from each other and see if we can
        // detect if the memory referred to both expressions is contiguous and
        // do not overlap.
        auto CI = CurComponents.rbegin();
        auto CE = CurComponents.rend();
        auto SI = StackComponents.rbegin();
        auto SE = StackComponents.rend();
        for (; CI != CE && SI != SE; ++CI, ++SI) {

          // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.3]
          //  At most one list item can be an array item derived from a given
          //  variable in map clauses of the same construct.
          if (CurrentRegionOnly &&
              (isa<ArraySubscriptExpr>(CI->getAssociatedExpression()) ||
               isa<ArraySectionExpr>(CI->getAssociatedExpression()) ||
               isa<OMPArrayShapingExpr>(CI->getAssociatedExpression())) &&
              (isa<ArraySubscriptExpr>(SI->getAssociatedExpression()) ||
               isa<ArraySectionExpr>(SI->getAssociatedExpression()) ||
               isa<OMPArrayShapingExpr>(SI->getAssociatedExpression()))) {
            SemaRef.Diag(CI->getAssociatedExpression()->getExprLoc(),
                         diag::err_omp_multiple_array_items_in_map_clause)
                << CI->getAssociatedExpression()->getSourceRange();
            SemaRef.Diag(SI->getAssociatedExpression()->getExprLoc(),
                         diag::note_used_here)
                << SI->getAssociatedExpression()->getSourceRange();
            return true;
          }

          // Do both expressions have the same kind?
          if (CI->getAssociatedExpression()->getStmtClass() !=
              SI->getAssociatedExpression()->getStmtClass())
            break;

          // Are we dealing with different variables/fields?
          if (CI->getAssociatedDeclaration() != SI->getAssociatedDeclaration())
            break;
        }
        // Check if the extra components of the expressions in the enclosing
        // data environment are redundant for the current base declaration.
        // If they are, the maps completely overlap, which is legal.
        for (; SI != SE; ++SI) {
          QualType Type;
          if (const auto *ASE =
                  dyn_cast<ArraySubscriptExpr>(SI->getAssociatedExpression())) {
            Type = ASE->getBase()->IgnoreParenImpCasts()->getType();
          } else if (const auto *OASE = dyn_cast<ArraySectionExpr>(
                         SI->getAssociatedExpression())) {
            const Expr *E = OASE->getBase()->IgnoreParenImpCasts();
            Type = ArraySectionExpr::getBaseOriginalType(E).getCanonicalType();
          } else if (const auto *OASE = dyn_cast<OMPArrayShapingExpr>(
                         SI->getAssociatedExpression())) {
            Type = OASE->getBase()->getType()->getPointeeType();
          }
          if (Type.isNull() || Type->isAnyPointerType() ||
              checkArrayExpressionDoesNotReferToWholeSize(
                  SemaRef, SI->getAssociatedExpression(), Type))
            break;
        }

        // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.4]
        //  List items of map clauses in the same construct must not share
        //  original storage.
        //
        // If the expressions are exactly the same or one is a subset of the
        // other, it means they are sharing storage.
        if (CI == CE && SI == SE) {
          if (CurrentRegionOnly) {
            if (CKind == OMPC_map) {
              SemaRef.Diag(ELoc, diag::err_omp_map_shared_storage) << ERange;
            } else {
              assert(CKind == OMPC_to || CKind == OMPC_from);
              SemaRef.Diag(ELoc, diag::err_omp_once_referenced_in_target_update)
                  << ERange;
            }
            SemaRef.Diag(RE->getExprLoc(), diag::note_used_here)
                << RE->getSourceRange();
            return true;
          }
          // If we find the same expression in the enclosing data environment,
          // that is legal.
          IsEnclosedByDataEnvironmentExpr = true;
          return false;
        }

        QualType DerivedType =
            std::prev(CI)->getAssociatedDeclaration()->getType();
        SourceLocation DerivedLoc =
            std::prev(CI)->getAssociatedExpression()->getExprLoc();

        // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C++, p.1]
        //  If the type of a list item is a reference to a type T then the type
        //  will be considered to be T for all purposes of this clause.
        DerivedType = DerivedType.getNonReferenceType();

        // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C/C++, p.1]
        //  A variable for which the type is pointer and an array section
        //  derived from that variable must not appear as list items of map
        //  clauses of the same construct.
        //
        // Also, cover one of the cases in:
        // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.5]
        //  If any part of the original storage of a list item has corresponding
        //  storage in the device data environment, all of the original storage
        //  must have corresponding storage in the device data environment.
        //
        if (DerivedType->isAnyPointerType()) {
          if (CI == CE || SI == SE) {
            SemaRef.Diag(
                DerivedLoc,
                diag::err_omp_pointer_mapped_along_with_derived_section)
                << DerivedLoc;
            SemaRef.Diag(RE->getExprLoc(), diag::note_used_here)
                << RE->getSourceRange();
            return true;
          }
          if (CI->getAssociatedExpression()->getStmtClass() !=
                  SI->getAssociatedExpression()->getStmtClass() ||
              CI->getAssociatedDeclaration()->getCanonicalDecl() ==
                  SI->getAssociatedDeclaration()->getCanonicalDecl()) {
            assert(CI != CE && SI != SE);
            SemaRef.Diag(DerivedLoc, diag::err_omp_same_pointer_dereferenced)
                << DerivedLoc;
            SemaRef.Diag(RE->getExprLoc(), diag::note_used_here)
                << RE->getSourceRange();
            return true;
          }
        }

        // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.4]
        //  List items of map clauses in the same construct must not share
        //  original storage.
        //
        // An expression is a subset of the other.
        if (CurrentRegionOnly && (CI == CE || SI == SE)) {
          if (CKind == OMPC_map) {
            if (CI != CE || SI != SE) {
              // Allow constructs like this: map(s, s.ptr[0:1]), where s.ptr is
              // a pointer.
              auto Begin =
                  CI != CE ? CurComponents.begin() : StackComponents.begin();
              auto End = CI != CE ? CurComponents.end() : StackComponents.end();
              auto It = Begin;
              while (It != End && !It->getAssociatedDeclaration())
                std::advance(It, 1);
              assert(It != End &&
                     "Expected at least one component with the declaration.");
              if (It != Begin && It->getAssociatedDeclaration()
                                     ->getType()
                                     .getCanonicalType()
                                     ->isAnyPointerType()) {
                IsEnclosedByDataEnvironmentExpr = false;
                EnclosingExpr = nullptr;
                return false;
              }
            }
            SemaRef.Diag(ELoc, diag::err_omp_map_shared_storage) << ERange;
          } else {
            assert(CKind == OMPC_to || CKind == OMPC_from);
            SemaRef.Diag(ELoc, diag::err_omp_once_referenced_in_target_update)
                << ERange;
          }
          SemaRef.Diag(RE->getExprLoc(), diag::note_used_here)
              << RE->getSourceRange();
          return true;
        }

        // The current expression uses the same base as other expression in the
        // data environment but does not contain it completely.
        if (!CurrentRegionOnly && SI != SE)
          EnclosingExpr = RE;

        // The current expression is a subset of the expression in the data
        // environment.
        IsEnclosedByDataEnvironmentExpr |=
            (!CurrentRegionOnly && CI != CE && SI == SE);

        return false;
      });

  if (CurrentRegionOnly)
    return FoundError;

  // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.5]
  //  If any part of the original storage of a list item has corresponding
  //  storage in the device data environment, all of the original storage must
  //  have corresponding storage in the device data environment.
  // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.6]
  //  If a list item is an element of a structure, and a different element of
  //  the structure has a corresponding list item in the device data environment
  //  prior to a task encountering the construct associated with the map clause,
  //  then the list item must also have a corresponding list item in the device
  //  data environment prior to the task encountering the construct.
  //
  if (EnclosingExpr && !IsEnclosedByDataEnvironmentExpr) {
    SemaRef.Diag(ELoc,
                 diag::err_omp_original_storage_is_shared_and_does_not_contain)
        << ERange;
    SemaRef.Diag(EnclosingExpr->getExprLoc(), diag::note_used_here)
        << EnclosingExpr->getSourceRange();
    return true;
  }

  return FoundError;
}

// Look up the user-defined mapper given the mapper name and mapped type, and
// build a reference to it.
static ExprResult buildUserDefinedMapperRef(Sema &SemaRef, Scope *S,
                                            CXXScopeSpec &MapperIdScopeSpec,
                                            const DeclarationNameInfo &MapperId,
                                            QualType Type,
                                            Expr *UnresolvedMapper) {
  if (MapperIdScopeSpec.isInvalid())
    return ExprError();
  // Get the actual type for the array type.
  if (Type->isArrayType()) {
    assert(Type->getAsArrayTypeUnsafe() && "Expect to get a valid array type");
    Type = Type->getAsArrayTypeUnsafe()->getElementType().getCanonicalType();
  }
  // Find all user-defined mappers with the given MapperId.
  SmallVector<UnresolvedSet<8>, 4> Lookups;
  LookupResult Lookup(SemaRef, MapperId, Sema::LookupOMPMapperName);
  Lookup.suppressDiagnostics();
  if (S) {
    while (S && SemaRef.LookupParsedName(Lookup, S, &MapperIdScopeSpec,
                                         /*ObjectType=*/QualType())) {
      NamedDecl *D = Lookup.getRepresentativeDecl();
      while (S && !S->isDeclScope(D))
        S = S->getParent();
      if (S)
        S = S->getParent();
      Lookups.emplace_back();
      Lookups.back().append(Lookup.begin(), Lookup.end());
      Lookup.clear();
    }
  } else if (auto *ULE = cast_or_null<UnresolvedLookupExpr>(UnresolvedMapper)) {
    // Extract the user-defined mappers with the given MapperId.
    Lookups.push_back(UnresolvedSet<8>());
    for (NamedDecl *D : ULE->decls()) {
      auto *DMD = cast<OMPDeclareMapperDecl>(D);
      assert(DMD && "Expect valid OMPDeclareMapperDecl during instantiation.");
      Lookups.back().addDecl(DMD);
    }
  }
  // Defer the lookup for dependent types. The results will be passed through
  // UnresolvedMapper on instantiation.
  if (SemaRef.CurContext->isDependentContext() || Type->isDependentType() ||
      Type->isInstantiationDependentType() ||
      Type->containsUnexpandedParameterPack() ||
      filterLookupForUDReductionAndMapper<bool>(Lookups, [](ValueDecl *D) {
        return !D->isInvalidDecl() &&
               (D->getType()->isDependentType() ||
                D->getType()->isInstantiationDependentType() ||
                D->getType()->containsUnexpandedParameterPack());
      })) {
    UnresolvedSet<8> URS;
    for (const UnresolvedSet<8> &Set : Lookups) {
      if (Set.empty())
        continue;
      URS.append(Set.begin(), Set.end());
    }
    return UnresolvedLookupExpr::Create(
        SemaRef.Context, /*NamingClass=*/nullptr,
        MapperIdScopeSpec.getWithLocInContext(SemaRef.Context), MapperId,
        /*ADL=*/false, URS.begin(), URS.end(), /*KnownDependent=*/false,
        /*KnownInstantiationDependent=*/false);
  }
  SourceLocation Loc = MapperId.getLoc();
  // [OpenMP 5.0], 2.19.7.3 declare mapper Directive, Restrictions
  //  The type must be of struct, union or class type in C and C++
  if (!Type->isStructureOrClassType() && !Type->isUnionType() &&
      (MapperIdScopeSpec.isSet() || MapperId.getAsString() != "default")) {
    SemaRef.Diag(Loc, diag::err_omp_mapper_wrong_type);
    return ExprError();
  }
  // Perform argument dependent lookup.
  if (SemaRef.getLangOpts().CPlusPlus && !MapperIdScopeSpec.isSet())
    argumentDependentLookup(SemaRef, MapperId, Loc, Type, Lookups);
  // Return the first user-defined mapper with the desired type.
  if (auto *VD = filterLookupForUDReductionAndMapper<ValueDecl *>(
          Lookups, [&SemaRef, Type](ValueDecl *D) -> ValueDecl * {
            if (!D->isInvalidDecl() &&
                SemaRef.Context.hasSameType(D->getType(), Type))
              return D;
            return nullptr;
          }))
    return SemaRef.BuildDeclRefExpr(VD, Type, VK_LValue, Loc);
  // Find the first user-defined mapper with a type derived from the desired
  // type.
  if (auto *VD = filterLookupForUDReductionAndMapper<ValueDecl *>(
          Lookups, [&SemaRef, Type, Loc](ValueDecl *D) -> ValueDecl * {
            if (!D->isInvalidDecl() &&
                SemaRef.IsDerivedFrom(Loc, Type, D->getType()) &&
                !Type.isMoreQualifiedThan(D->getType()))
              return D;
            return nullptr;
          })) {
    CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                       /*DetectVirtual=*/false);
    if (SemaRef.IsDerivedFrom(Loc, Type, VD->getType(), Paths)) {
      if (!Paths.isAmbiguous(SemaRef.Context.getCanonicalType(
              VD->getType().getUnqualifiedType()))) {
        if (SemaRef.CheckBaseClassAccess(
                Loc, VD->getType(), Type, Paths.front(),
                /*DiagID=*/0) != Sema::AR_inaccessible) {
          return SemaRef.BuildDeclRefExpr(VD, Type, VK_LValue, Loc);
        }
      }
    }
  }
  // Report error if a mapper is specified, but cannot be found.
  if (MapperIdScopeSpec.isSet() || MapperId.getAsString() != "default") {
    SemaRef.Diag(Loc, diag::err_omp_invalid_mapper)
        << Type << MapperId.getName();
    return ExprError();
  }
  return ExprEmpty();
}

namespace {
// Utility struct that gathers all the related lists associated with a mappable
// expression.
struct MappableVarListInfo {
  // The list of expressions.
  ArrayRef<Expr *> VarList;
  // The list of processed expressions.
  SmallVector<Expr *, 16> ProcessedVarList;
  // The mappble components for each expression.
  OMPClauseMappableExprCommon::MappableExprComponentLists VarComponents;
  // The base declaration of the variable.
  SmallVector<ValueDecl *, 16> VarBaseDeclarations;
  // The reference to the user-defined mapper associated with every expression.
  SmallVector<Expr *, 16> UDMapperList;

  MappableVarListInfo(ArrayRef<Expr *> VarList) : VarList(VarList) {
    // We have a list of components and base declarations for each entry in the
    // variable list.
    VarComponents.reserve(VarList.size());
    VarBaseDeclarations.reserve(VarList.size());
  }
};
} // namespace

// Check the validity of the provided variable list for the provided clause kind
// \a CKind. In the check process the valid expressions, mappable expression
// components, variables, and user-defined mappers are extracted and used to
// fill \a ProcessedVarList, \a VarComponents, \a VarBaseDeclarations, and \a
// UDMapperList in MVLI. \a MapType, \a IsMapTypeImplicit, \a MapperIdScopeSpec,
// and \a MapperId are expected to be valid if the clause kind is 'map'.
static void checkMappableExpressionList(
    Sema &SemaRef, DSAStackTy *DSAS, OpenMPClauseKind CKind,
    MappableVarListInfo &MVLI, SourceLocation StartLoc,
    CXXScopeSpec &MapperIdScopeSpec, DeclarationNameInfo MapperId,
    ArrayRef<Expr *> UnresolvedMappers,
    OpenMPMapClauseKind MapType = OMPC_MAP_unknown,
    ArrayRef<OpenMPMapModifierKind> Modifiers = std::nullopt,
    bool IsMapTypeImplicit = false, bool NoDiagnose = false) {
  // We only expect mappable expressions in 'to', 'from', and 'map' clauses.
  assert((CKind == OMPC_map || CKind == OMPC_to || CKind == OMPC_from) &&
         "Unexpected clause kind with mappable expressions!");

  // If the identifier of user-defined mapper is not specified, it is "default".
  // We do not change the actual name in this clause to distinguish whether a
  // mapper is specified explicitly, i.e., it is not explicitly specified when
  // MapperId.getName() is empty.
  if (!MapperId.getName() || MapperId.getName().isEmpty()) {
    auto &DeclNames = SemaRef.getASTContext().DeclarationNames;
    MapperId.setName(DeclNames.getIdentifier(
        &SemaRef.getASTContext().Idents.get("default")));
    MapperId.setLoc(StartLoc);
  }

  // Iterators to find the current unresolved mapper expression.
  auto UMIt = UnresolvedMappers.begin(), UMEnd = UnresolvedMappers.end();
  bool UpdateUMIt = false;
  Expr *UnresolvedMapper = nullptr;

  bool HasHoldModifier =
      llvm::is_contained(Modifiers, OMPC_MAP_MODIFIER_ompx_hold);

  // Keep track of the mappable components and base declarations in this clause.
  // Each entry in the list is going to have a list of components associated. We
  // record each set of the components so that we can build the clause later on.
  // In the end we should have the same amount of declarations and component
  // lists.

  for (Expr *RE : MVLI.VarList) {
    assert(RE && "Null expr in omp to/from/map clause");
    SourceLocation ELoc = RE->getExprLoc();

    // Find the current unresolved mapper expression.
    if (UpdateUMIt && UMIt != UMEnd) {
      UMIt++;
      assert(
          UMIt != UMEnd &&
          "Expect the size of UnresolvedMappers to match with that of VarList");
    }
    UpdateUMIt = true;
    if (UMIt != UMEnd)
      UnresolvedMapper = *UMIt;

    const Expr *VE = RE->IgnoreParenLValueCasts();

    if (VE->isValueDependent() || VE->isTypeDependent() ||
        VE->isInstantiationDependent() ||
        VE->containsUnexpandedParameterPack()) {
      // Try to find the associated user-defined mapper.
      ExprResult ER = buildUserDefinedMapperRef(
          SemaRef, DSAS->getCurScope(), MapperIdScopeSpec, MapperId,
          VE->getType().getCanonicalType(), UnresolvedMapper);
      if (ER.isInvalid())
        continue;
      MVLI.UDMapperList.push_back(ER.get());
      // We can only analyze this information once the missing information is
      // resolved.
      MVLI.ProcessedVarList.push_back(RE);
      continue;
    }

    Expr *SimpleExpr = RE->IgnoreParenCasts();

    if (!RE->isLValue()) {
      if (SemaRef.getLangOpts().OpenMP < 50) {
        SemaRef.Diag(
            ELoc, diag::err_omp_expected_named_var_member_or_array_expression)
            << RE->getSourceRange();
      } else {
        SemaRef.Diag(ELoc, diag::err_omp_non_lvalue_in_map_or_motion_clauses)
            << getOpenMPClauseName(CKind) << RE->getSourceRange();
      }
      continue;
    }

    OMPClauseMappableExprCommon::MappableExprComponentList CurComponents;
    ValueDecl *CurDeclaration = nullptr;

    // Obtain the array or member expression bases if required. Also, fill the
    // components array with all the components identified in the process.
    const Expr *BE =
        checkMapClauseExpressionBase(SemaRef, SimpleExpr, CurComponents, CKind,
                                     DSAS->getCurrentDirective(), NoDiagnose);
    if (!BE)
      continue;

    assert(!CurComponents.empty() &&
           "Invalid mappable expression information.");

    if (const auto *TE = dyn_cast<CXXThisExpr>(BE)) {
      // Add store "this" pointer to class in DSAStackTy for future checking
      DSAS->addMappedClassesQualTypes(TE->getType());
      // Try to find the associated user-defined mapper.
      ExprResult ER = buildUserDefinedMapperRef(
          SemaRef, DSAS->getCurScope(), MapperIdScopeSpec, MapperId,
          VE->getType().getCanonicalType(), UnresolvedMapper);
      if (ER.isInvalid())
        continue;
      MVLI.UDMapperList.push_back(ER.get());
      // Skip restriction checking for variable or field declarations
      MVLI.ProcessedVarList.push_back(RE);
      MVLI.VarComponents.resize(MVLI.VarComponents.size() + 1);
      MVLI.VarComponents.back().append(CurComponents.begin(),
                                       CurComponents.end());
      MVLI.VarBaseDeclarations.push_back(nullptr);
      continue;
    }

    // For the following checks, we rely on the base declaration which is
    // expected to be associated with the last component. The declaration is
    // expected to be a variable or a field (if 'this' is being mapped).
    CurDeclaration = CurComponents.back().getAssociatedDeclaration();
    assert(CurDeclaration && "Null decl on map clause.");
    assert(
        CurDeclaration->isCanonicalDecl() &&
        "Expecting components to have associated only canonical declarations.");

    auto *VD = dyn_cast<VarDecl>(CurDeclaration);
    const auto *FD = dyn_cast<FieldDecl>(CurDeclaration);

    assert((VD || FD) && "Only variables or fields are expected here!");
    (void)FD;

    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.10]
    // threadprivate variables cannot appear in a map clause.
    // OpenMP 4.5 [2.10.5, target update Construct]
    // threadprivate variables cannot appear in a from clause.
    if (VD && DSAS->isThreadPrivate(VD)) {
      if (NoDiagnose)
        continue;
      DSAStackTy::DSAVarData DVar = DSAS->getTopDSA(VD, /*FromParent=*/false);
      SemaRef.Diag(ELoc, diag::err_omp_threadprivate_in_clause)
          << getOpenMPClauseName(CKind);
      reportOriginalDsa(SemaRef, DSAS, VD, DVar);
      continue;
    }

    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.9]
    //  A list item cannot appear in both a map clause and a data-sharing
    //  attribute clause on the same construct.

    // Check conflicts with other map clause expressions. We check the conflicts
    // with the current construct separately from the enclosing data
    // environment, because the restrictions are different. We only have to
    // check conflicts across regions for the map clauses.
    if (checkMapConflicts(SemaRef, DSAS, CurDeclaration, SimpleExpr,
                          /*CurrentRegionOnly=*/true, CurComponents, CKind))
      break;
    if (CKind == OMPC_map &&
        (SemaRef.getLangOpts().OpenMP <= 45 || StartLoc.isValid()) &&
        checkMapConflicts(SemaRef, DSAS, CurDeclaration, SimpleExpr,
                          /*CurrentRegionOnly=*/false, CurComponents, CKind))
      break;

    // OpenMP 4.5 [2.10.5, target update Construct]
    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, C++, p.1]
    //  If the type of a list item is a reference to a type T then the type will
    //  be considered to be T for all purposes of this clause.
    auto I = llvm::find_if(
        CurComponents,
        [](const OMPClauseMappableExprCommon::MappableComponent &MC) {
          return MC.getAssociatedDeclaration();
        });
    assert(I != CurComponents.end() && "Null decl on map clause.");
    (void)I;
    QualType Type;
    auto *ASE = dyn_cast<ArraySubscriptExpr>(VE->IgnoreParens());
    auto *OASE = dyn_cast<ArraySectionExpr>(VE->IgnoreParens());
    auto *OAShE = dyn_cast<OMPArrayShapingExpr>(VE->IgnoreParens());
    if (ASE) {
      Type = ASE->getType().getNonReferenceType();
    } else if (OASE) {
      QualType BaseType =
          ArraySectionExpr::getBaseOriginalType(OASE->getBase());
      if (const auto *ATy = BaseType->getAsArrayTypeUnsafe())
        Type = ATy->getElementType();
      else
        Type = BaseType->getPointeeType();
      Type = Type.getNonReferenceType();
    } else if (OAShE) {
      Type = OAShE->getBase()->getType()->getPointeeType();
    } else {
      Type = VE->getType();
    }

    // OpenMP 4.5 [2.10.5, target update Construct, Restrictions, p.4]
    // A list item in a to or from clause must have a mappable type.
    // OpenMP 4.5 [2.15.5.1, map Clause, Restrictions, p.9]
    //  A list item must have a mappable type.
    if (!checkTypeMappable(VE->getExprLoc(), VE->getSourceRange(), SemaRef,
                           DSAS, Type, /*FullCheck=*/true))
      continue;

    if (CKind == OMPC_map) {
      // target enter data
      // OpenMP [2.10.2, Restrictions, p. 99]
      // A map-type must be specified in all map clauses and must be either
      // to or alloc. Starting with OpenMP 5.2 the default map type is `to` if
      // no map type is present.
      OpenMPDirectiveKind DKind = DSAS->getCurrentDirective();
      if (DKind == OMPD_target_enter_data &&
          !(MapType == OMPC_MAP_to || MapType == OMPC_MAP_alloc ||
            SemaRef.getLangOpts().OpenMP >= 52)) {
        SemaRef.Diag(StartLoc, diag::err_omp_invalid_map_type_for_directive)
            << (IsMapTypeImplicit ? 1 : 0)
            << getOpenMPSimpleClauseTypeName(OMPC_map, MapType)
            << getOpenMPDirectiveName(DKind);
        continue;
      }

      // target exit_data
      // OpenMP [2.10.3, Restrictions, p. 102]
      // A map-type must be specified in all map clauses and must be either
      // from, release, or delete. Starting with OpenMP 5.2 the default map
      // type is `from` if no map type is present.
      if (DKind == OMPD_target_exit_data &&
          !(MapType == OMPC_MAP_from || MapType == OMPC_MAP_release ||
            MapType == OMPC_MAP_delete || SemaRef.getLangOpts().OpenMP >= 52)) {
        SemaRef.Diag(StartLoc, diag::err_omp_invalid_map_type_for_directive)
            << (IsMapTypeImplicit ? 1 : 0)
            << getOpenMPSimpleClauseTypeName(OMPC_map, MapType)
            << getOpenMPDirectiveName(DKind);
        continue;
      }

      // The 'ompx_hold' modifier is specifically intended to be used on a
      // 'target' or 'target data' directive to prevent data from being unmapped
      // during the associated statement.  It is not permitted on a 'target
      // enter data' or 'target exit data' directive, which have no associated
      // statement.
      if ((DKind == OMPD_target_enter_data || DKind == OMPD_target_exit_data) &&
          HasHoldModifier) {
        SemaRef.Diag(StartLoc,
                     diag::err_omp_invalid_map_type_modifier_for_directive)
            << getOpenMPSimpleClauseTypeName(OMPC_map,
                                             OMPC_MAP_MODIFIER_ompx_hold)
            << getOpenMPDirectiveName(DKind);
        continue;
      }

      // target, target data
      // OpenMP 5.0 [2.12.2, Restrictions, p. 163]
      // OpenMP 5.0 [2.12.5, Restrictions, p. 174]
      // A map-type in a map clause must be to, from, tofrom or alloc
      if ((DKind == OMPD_target_data ||
           isOpenMPTargetExecutionDirective(DKind)) &&
          !(MapType == OMPC_MAP_to || MapType == OMPC_MAP_from ||
            MapType == OMPC_MAP_tofrom || MapType == OMPC_MAP_alloc)) {
        SemaRef.Diag(StartLoc, diag::err_omp_invalid_map_type_for_directive)
            << (IsMapTypeImplicit ? 1 : 0)
            << getOpenMPSimpleClauseTypeName(OMPC_map, MapType)
            << getOpenMPDirectiveName(DKind);
        continue;
      }

      // OpenMP 4.5 [2.15.5.1, Restrictions, p.3]
      // A list item cannot appear in both a map clause and a data-sharing
      // attribute clause on the same construct
      //
      // OpenMP 5.0 [2.19.7.1, Restrictions, p.7]
      // A list item cannot appear in both a map clause and a data-sharing
      // attribute clause on the same construct unless the construct is a
      // combined construct.
      if (VD && ((SemaRef.LangOpts.OpenMP <= 45 &&
                  isOpenMPTargetExecutionDirective(DKind)) ||
                 DKind == OMPD_target)) {
        DSAStackTy::DSAVarData DVar = DSAS->getTopDSA(VD, /*FromParent=*/false);
        if (isOpenMPPrivate(DVar.CKind)) {
          SemaRef.Diag(ELoc, diag::err_omp_variable_in_given_clause_and_dsa)
              << getOpenMPClauseName(DVar.CKind)
              << getOpenMPClauseName(OMPC_map)
              << getOpenMPDirectiveName(DSAS->getCurrentDirective());
          reportOriginalDsa(SemaRef, DSAS, CurDeclaration, DVar);
          continue;
        }
      }
    }

    // Try to find the associated user-defined mapper.
    ExprResult ER = buildUserDefinedMapperRef(
        SemaRef, DSAS->getCurScope(), MapperIdScopeSpec, MapperId,
        Type.getCanonicalType(), UnresolvedMapper);
    if (ER.isInvalid())
      continue;
    MVLI.UDMapperList.push_back(ER.get());

    // Save the current expression.
    MVLI.ProcessedVarList.push_back(RE);

    // Store the components in the stack so that they can be used to check
    // against other clauses later on.
    DSAS->addMappableExpressionComponents(CurDeclaration, CurComponents,
                                          /*WhereFoundClauseKind=*/OMPC_map);

    // Save the components and declaration to create the clause. For purposes of
    // the clause creation, any component list that has base 'this' uses
    // null as base declaration.
    MVLI.VarComponents.resize(MVLI.VarComponents.size() + 1);
    MVLI.VarComponents.back().append(CurComponents.begin(),
                                     CurComponents.end());
    MVLI.VarBaseDeclarations.push_back(isa<MemberExpr>(BE) ? nullptr
                                                           : CurDeclaration);
  }
}

OMPClause *SemaOpenMP::ActOnOpenMPMapClause(
    Expr *IteratorModifier, ArrayRef<OpenMPMapModifierKind> MapTypeModifiers,
    ArrayRef<SourceLocation> MapTypeModifiersLoc,
    CXXScopeSpec &MapperIdScopeSpec, DeclarationNameInfo &MapperId,
    OpenMPMapClauseKind MapType, bool IsMapTypeImplicit, SourceLocation MapLoc,
    SourceLocation ColonLoc, ArrayRef<Expr *> VarList,
    const OMPVarListLocTy &Locs, bool NoDiagnose,
    ArrayRef<Expr *> UnresolvedMappers) {
  OpenMPMapModifierKind Modifiers[] = {
      OMPC_MAP_MODIFIER_unknown, OMPC_MAP_MODIFIER_unknown,
      OMPC_MAP_MODIFIER_unknown, OMPC_MAP_MODIFIER_unknown,
      OMPC_MAP_MODIFIER_unknown, OMPC_MAP_MODIFIER_unknown};
  SourceLocation ModifiersLoc[NumberOfOMPMapClauseModifiers];

  if (IteratorModifier && !IteratorModifier->getType()->isSpecificBuiltinType(
                              BuiltinType::OMPIterator))
    Diag(IteratorModifier->getExprLoc(),
         diag::err_omp_map_modifier_not_iterator);

  // Process map-type-modifiers, flag errors for duplicate modifiers.
  unsigned Count = 0;
  for (unsigned I = 0, E = MapTypeModifiers.size(); I < E; ++I) {
    if (MapTypeModifiers[I] != OMPC_MAP_MODIFIER_unknown &&
        llvm::is_contained(Modifiers, MapTypeModifiers[I])) {
      Diag(MapTypeModifiersLoc[I], diag::err_omp_duplicate_map_type_modifier);
      continue;
    }
    assert(Count < NumberOfOMPMapClauseModifiers &&
           "Modifiers exceed the allowed number of map type modifiers");
    Modifiers[Count] = MapTypeModifiers[I];
    ModifiersLoc[Count] = MapTypeModifiersLoc[I];
    ++Count;
  }

  MappableVarListInfo MVLI(VarList);
  checkMappableExpressionList(SemaRef, DSAStack, OMPC_map, MVLI, Locs.StartLoc,
                              MapperIdScopeSpec, MapperId, UnresolvedMappers,
                              MapType, Modifiers, IsMapTypeImplicit,
                              NoDiagnose);

  // We need to produce a map clause even if we don't have variables so that
  // other diagnostics related with non-existing map clauses are accurate.
  return OMPMapClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, MVLI.VarBaseDeclarations,
      MVLI.VarComponents, MVLI.UDMapperList, IteratorModifier, Modifiers,
      ModifiersLoc, MapperIdScopeSpec.getWithLocInContext(getASTContext()),
      MapperId, MapType, IsMapTypeImplicit, MapLoc);
}

QualType SemaOpenMP::ActOnOpenMPDeclareReductionType(SourceLocation TyLoc,
                                                     TypeResult ParsedType) {
  assert(ParsedType.isUsable());

  QualType ReductionType = SemaRef.GetTypeFromParser(ParsedType.get());
  if (ReductionType.isNull())
    return QualType();

  // [OpenMP 4.0], 2.15 declare reduction Directive, Restrictions, C\C++
  // A type name in a declare reduction directive cannot be a function type, an
  // array type, a reference type, or a type qualified with const, volatile or
  // restrict.
  if (ReductionType.hasQualifiers()) {
    Diag(TyLoc, diag::err_omp_reduction_wrong_type) << 0;
    return QualType();
  }

  if (ReductionType->isFunctionType()) {
    Diag(TyLoc, diag::err_omp_reduction_wrong_type) << 1;
    return QualType();
  }
  if (ReductionType->isReferenceType()) {
    Diag(TyLoc, diag::err_omp_reduction_wrong_type) << 2;
    return QualType();
  }
  if (ReductionType->isArrayType()) {
    Diag(TyLoc, diag::err_omp_reduction_wrong_type) << 3;
    return QualType();
  }
  return ReductionType;
}

SemaOpenMP::DeclGroupPtrTy
SemaOpenMP::ActOnOpenMPDeclareReductionDirectiveStart(
    Scope *S, DeclContext *DC, DeclarationName Name,
    ArrayRef<std::pair<QualType, SourceLocation>> ReductionTypes,
    AccessSpecifier AS, Decl *PrevDeclInScope) {
  SmallVector<Decl *, 8> Decls;
  Decls.reserve(ReductionTypes.size());

  LookupResult Lookup(SemaRef, Name, SourceLocation(),
                      Sema::LookupOMPReductionName,
                      SemaRef.forRedeclarationInCurContext());
  // [OpenMP 4.0], 2.15 declare reduction Directive, Restrictions
  // A reduction-identifier may not be re-declared in the current scope for the
  // same type or for a type that is compatible according to the base language
  // rules.
  llvm::DenseMap<QualType, SourceLocation> PreviousRedeclTypes;
  OMPDeclareReductionDecl *PrevDRD = nullptr;
  bool InCompoundScope = true;
  if (S != nullptr) {
    // Find previous declaration with the same name not referenced in other
    // declarations.
    FunctionScopeInfo *ParentFn = SemaRef.getEnclosingFunction();
    InCompoundScope =
        (ParentFn != nullptr) && !ParentFn->CompoundScopes.empty();
    SemaRef.LookupName(Lookup, S);
    SemaRef.FilterLookupForScope(Lookup, DC, S, /*ConsiderLinkage=*/false,
                                 /*AllowInlineNamespace=*/false);
    llvm::DenseMap<OMPDeclareReductionDecl *, bool> UsedAsPrevious;
    LookupResult::Filter Filter = Lookup.makeFilter();
    while (Filter.hasNext()) {
      auto *PrevDecl = cast<OMPDeclareReductionDecl>(Filter.next());
      if (InCompoundScope) {
        auto I = UsedAsPrevious.find(PrevDecl);
        if (I == UsedAsPrevious.end())
          UsedAsPrevious[PrevDecl] = false;
        if (OMPDeclareReductionDecl *D = PrevDecl->getPrevDeclInScope())
          UsedAsPrevious[D] = true;
      }
      PreviousRedeclTypes[PrevDecl->getType().getCanonicalType()] =
          PrevDecl->getLocation();
    }
    Filter.done();
    if (InCompoundScope) {
      for (const auto &PrevData : UsedAsPrevious) {
        if (!PrevData.second) {
          PrevDRD = PrevData.first;
          break;
        }
      }
    }
  } else if (PrevDeclInScope != nullptr) {
    auto *PrevDRDInScope = PrevDRD =
        cast<OMPDeclareReductionDecl>(PrevDeclInScope);
    do {
      PreviousRedeclTypes[PrevDRDInScope->getType().getCanonicalType()] =
          PrevDRDInScope->getLocation();
      PrevDRDInScope = PrevDRDInScope->getPrevDeclInScope();
    } while (PrevDRDInScope != nullptr);
  }
  for (const auto &TyData : ReductionTypes) {
    const auto I = PreviousRedeclTypes.find(TyData.first.getCanonicalType());
    bool Invalid = false;
    if (I != PreviousRedeclTypes.end()) {
      Diag(TyData.second, diag::err_omp_declare_reduction_redefinition)
          << TyData.first;
      Diag(I->second, diag::note_previous_definition);
      Invalid = true;
    }
    PreviousRedeclTypes[TyData.first.getCanonicalType()] = TyData.second;
    auto *DRD = OMPDeclareReductionDecl::Create(
        getASTContext(), DC, TyData.second, Name, TyData.first, PrevDRD);
    DC->addDecl(DRD);
    DRD->setAccess(AS);
    Decls.push_back(DRD);
    if (Invalid)
      DRD->setInvalidDecl();
    else
      PrevDRD = DRD;
  }

  return DeclGroupPtrTy::make(
      DeclGroupRef::Create(getASTContext(), Decls.begin(), Decls.size()));
}

void SemaOpenMP::ActOnOpenMPDeclareReductionCombinerStart(Scope *S, Decl *D) {
  auto *DRD = cast<OMPDeclareReductionDecl>(D);

  // Enter new function scope.
  SemaRef.PushFunctionScope();
  SemaRef.setFunctionHasBranchProtectedScope();
  SemaRef.getCurFunction()->setHasOMPDeclareReductionCombiner();

  if (S != nullptr)
    SemaRef.PushDeclContext(S, DRD);
  else
    SemaRef.CurContext = DRD;

  SemaRef.PushExpressionEvaluationContext(
      Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

  QualType ReductionType = DRD->getType();
  // Create 'T* omp_parm;T omp_in;'. All references to 'omp_in' will
  // be replaced by '*omp_parm' during codegen. This required because 'omp_in'
  // uses semantics of argument handles by value, but it should be passed by
  // reference. C lang does not support references, so pass all parameters as
  // pointers.
  // Create 'T omp_in;' variable.
  VarDecl *OmpInParm =
      buildVarDecl(SemaRef, D->getLocation(), ReductionType, "omp_in");
  // Create 'T* omp_parm;T omp_out;'. All references to 'omp_out' will
  // be replaced by '*omp_parm' during codegen. This required because 'omp_out'
  // uses semantics of argument handles by value, but it should be passed by
  // reference. C lang does not support references, so pass all parameters as
  // pointers.
  // Create 'T omp_out;' variable.
  VarDecl *OmpOutParm =
      buildVarDecl(SemaRef, D->getLocation(), ReductionType, "omp_out");
  if (S != nullptr) {
    SemaRef.PushOnScopeChains(OmpInParm, S);
    SemaRef.PushOnScopeChains(OmpOutParm, S);
  } else {
    DRD->addDecl(OmpInParm);
    DRD->addDecl(OmpOutParm);
  }
  Expr *InE =
      ::buildDeclRefExpr(SemaRef, OmpInParm, ReductionType, D->getLocation());
  Expr *OutE =
      ::buildDeclRefExpr(SemaRef, OmpOutParm, ReductionType, D->getLocation());
  DRD->setCombinerData(InE, OutE);
}

void SemaOpenMP::ActOnOpenMPDeclareReductionCombinerEnd(Decl *D,
                                                        Expr *Combiner) {
  auto *DRD = cast<OMPDeclareReductionDecl>(D);
  SemaRef.DiscardCleanupsInEvaluationContext();
  SemaRef.PopExpressionEvaluationContext();

  SemaRef.PopDeclContext();
  SemaRef.PopFunctionScopeInfo();

  if (Combiner != nullptr)
    DRD->setCombiner(Combiner);
  else
    DRD->setInvalidDecl();
}

VarDecl *SemaOpenMP::ActOnOpenMPDeclareReductionInitializerStart(Scope *S,
                                                                 Decl *D) {
  auto *DRD = cast<OMPDeclareReductionDecl>(D);

  // Enter new function scope.
  SemaRef.PushFunctionScope();
  SemaRef.setFunctionHasBranchProtectedScope();

  if (S != nullptr)
    SemaRef.PushDeclContext(S, DRD);
  else
    SemaRef.CurContext = DRD;

  SemaRef.PushExpressionEvaluationContext(
      Sema::ExpressionEvaluationContext::PotentiallyEvaluated);

  QualType ReductionType = DRD->getType();
  // Create 'T* omp_parm;T omp_priv;'. All references to 'omp_priv' will
  // be replaced by '*omp_parm' during codegen. This required because 'omp_priv'
  // uses semantics of argument handles by value, but it should be passed by
  // reference. C lang does not support references, so pass all parameters as
  // pointers.
  // Create 'T omp_priv;' variable.
  VarDecl *OmpPrivParm =
      buildVarDecl(SemaRef, D->getLocation(), ReductionType, "omp_priv");
  // Create 'T* omp_parm;T omp_orig;'. All references to 'omp_orig' will
  // be replaced by '*omp_parm' during codegen. This required because 'omp_orig'
  // uses semantics of argument handles by value, but it should be passed by
  // reference. C lang does not support references, so pass all parameters as
  // pointers.
  // Create 'T omp_orig;' variable.
  VarDecl *OmpOrigParm =
      buildVarDecl(SemaRef, D->getLocation(), ReductionType, "omp_orig");
  if (S != nullptr) {
    SemaRef.PushOnScopeChains(OmpPrivParm, S);
    SemaRef.PushOnScopeChains(OmpOrigParm, S);
  } else {
    DRD->addDecl(OmpPrivParm);
    DRD->addDecl(OmpOrigParm);
  }
  Expr *OrigE =
      ::buildDeclRefExpr(SemaRef, OmpOrigParm, ReductionType, D->getLocation());
  Expr *PrivE =
      ::buildDeclRefExpr(SemaRef, OmpPrivParm, ReductionType, D->getLocation());
  DRD->setInitializerData(OrigE, PrivE);
  return OmpPrivParm;
}

void SemaOpenMP::ActOnOpenMPDeclareReductionInitializerEnd(
    Decl *D, Expr *Initializer, VarDecl *OmpPrivParm) {
  auto *DRD = cast<OMPDeclareReductionDecl>(D);
  SemaRef.DiscardCleanupsInEvaluationContext();
  SemaRef.PopExpressionEvaluationContext();

  SemaRef.PopDeclContext();
  SemaRef.PopFunctionScopeInfo();

  if (Initializer != nullptr) {
    DRD->setInitializer(Initializer, OMPDeclareReductionInitKind::Call);
  } else if (OmpPrivParm->hasInit()) {
    DRD->setInitializer(OmpPrivParm->getInit(),
                        OmpPrivParm->isDirectInit()
                            ? OMPDeclareReductionInitKind::Direct
                            : OMPDeclareReductionInitKind::Copy);
  } else {
    DRD->setInvalidDecl();
  }
}

SemaOpenMP::DeclGroupPtrTy SemaOpenMP::ActOnOpenMPDeclareReductionDirectiveEnd(
    Scope *S, DeclGroupPtrTy DeclReductions, bool IsValid) {
  for (Decl *D : DeclReductions.get()) {
    if (IsValid) {
      if (S)
        SemaRef.PushOnScopeChains(cast<OMPDeclareReductionDecl>(D), S,
                                  /*AddToContext=*/false);
    } else {
      D->setInvalidDecl();
    }
  }
  return DeclReductions;
}

TypeResult SemaOpenMP::ActOnOpenMPDeclareMapperVarDecl(Scope *S,
                                                       Declarator &D) {
  TypeSourceInfo *TInfo = SemaRef.GetTypeForDeclarator(D);
  QualType T = TInfo->getType();
  if (D.isInvalidType())
    return true;

  if (getLangOpts().CPlusPlus) {
    // Check that there are no default arguments (C++ only).
    SemaRef.CheckExtraCXXDefaultArguments(D);
  }

  return SemaRef.CreateParsedType(T, TInfo);
}

QualType SemaOpenMP::ActOnOpenMPDeclareMapperType(SourceLocation TyLoc,
                                                  TypeResult ParsedType) {
  assert(ParsedType.isUsable() && "Expect usable parsed mapper type");

  QualType MapperType = SemaRef.GetTypeFromParser(ParsedType.get());
  assert(!MapperType.isNull() && "Expect valid mapper type");

  // [OpenMP 5.0], 2.19.7.3 declare mapper Directive, Restrictions
  //  The type must be of struct, union or class type in C and C++
  if (!MapperType->isStructureOrClassType() && !MapperType->isUnionType()) {
    Diag(TyLoc, diag::err_omp_mapper_wrong_type);
    return QualType();
  }
  return MapperType;
}

SemaOpenMP::DeclGroupPtrTy SemaOpenMP::ActOnOpenMPDeclareMapperDirective(
    Scope *S, DeclContext *DC, DeclarationName Name, QualType MapperType,
    SourceLocation StartLoc, DeclarationName VN, AccessSpecifier AS,
    Expr *MapperVarRef, ArrayRef<OMPClause *> Clauses, Decl *PrevDeclInScope) {
  LookupResult Lookup(SemaRef, Name, SourceLocation(),
                      Sema::LookupOMPMapperName,
                      SemaRef.forRedeclarationInCurContext());
  // [OpenMP 5.0], 2.19.7.3 declare mapper Directive, Restrictions
  //  A mapper-identifier may not be redeclared in the current scope for the
  //  same type or for a type that is compatible according to the base language
  //  rules.
  llvm::DenseMap<QualType, SourceLocation> PreviousRedeclTypes;
  OMPDeclareMapperDecl *PrevDMD = nullptr;
  bool InCompoundScope = true;
  if (S != nullptr) {
    // Find previous declaration with the same name not referenced in other
    // declarations.
    FunctionScopeInfo *ParentFn = SemaRef.getEnclosingFunction();
    InCompoundScope =
        (ParentFn != nullptr) && !ParentFn->CompoundScopes.empty();
    SemaRef.LookupName(Lookup, S);
    SemaRef.FilterLookupForScope(Lookup, DC, S, /*ConsiderLinkage=*/false,
                                 /*AllowInlineNamespace=*/false);
    llvm::DenseMap<OMPDeclareMapperDecl *, bool> UsedAsPrevious;
    LookupResult::Filter Filter = Lookup.makeFilter();
    while (Filter.hasNext()) {
      auto *PrevDecl = cast<OMPDeclareMapperDecl>(Filter.next());
      if (InCompoundScope) {
        auto I = UsedAsPrevious.find(PrevDecl);
        if (I == UsedAsPrevious.end())
          UsedAsPrevious[PrevDecl] = false;
        if (OMPDeclareMapperDecl *D = PrevDecl->getPrevDeclInScope())
          UsedAsPrevious[D] = true;
      }
      PreviousRedeclTypes[PrevDecl->getType().getCanonicalType()] =
          PrevDecl->getLocation();
    }
    Filter.done();
    if (InCompoundScope) {
      for (const auto &PrevData : UsedAsPrevious) {
        if (!PrevData.second) {
          PrevDMD = PrevData.first;
          break;
        }
      }
    }
  } else if (PrevDeclInScope) {
    auto *PrevDMDInScope = PrevDMD =
        cast<OMPDeclareMapperDecl>(PrevDeclInScope);
    do {
      PreviousRedeclTypes[PrevDMDInScope->getType().getCanonicalType()] =
          PrevDMDInScope->getLocation();
      PrevDMDInScope = PrevDMDInScope->getPrevDeclInScope();
    } while (PrevDMDInScope != nullptr);
  }
  const auto I = PreviousRedeclTypes.find(MapperType.getCanonicalType());
  bool Invalid = false;
  if (I != PreviousRedeclTypes.end()) {
    Diag(StartLoc, diag::err_omp_declare_mapper_redefinition)
        << MapperType << Name;
    Diag(I->second, diag::note_previous_definition);
    Invalid = true;
  }
  // Build expressions for implicit maps of data members with 'default'
  // mappers.
  SmallVector<OMPClause *, 4> ClausesWithImplicit(Clauses.begin(),
                                                  Clauses.end());
  if (getLangOpts().OpenMP >= 50)
    processImplicitMapsWithDefaultMappers(SemaRef, DSAStack,
                                          ClausesWithImplicit);
  auto *DMD = OMPDeclareMapperDecl::Create(getASTContext(), DC, StartLoc, Name,
                                           MapperType, VN, ClausesWithImplicit,
                                           PrevDMD);
  if (S)
    SemaRef.PushOnScopeChains(DMD, S);
  else
    DC->addDecl(DMD);
  DMD->setAccess(AS);
  if (Invalid)
    DMD->setInvalidDecl();

  auto *VD = cast<DeclRefExpr>(MapperVarRef)->getDecl();
  VD->setDeclContext(DMD);
  VD->setLexicalDeclContext(DMD);
  DMD->addDecl(VD);
  DMD->setMapperVarRef(MapperVarRef);

  return DeclGroupPtrTy::make(DeclGroupRef(DMD));
}

ExprResult SemaOpenMP::ActOnOpenMPDeclareMapperDirectiveVarDecl(
    Scope *S, QualType MapperType, SourceLocation StartLoc,
    DeclarationName VN) {
  TypeSourceInfo *TInfo =
      getASTContext().getTrivialTypeSourceInfo(MapperType, StartLoc);
  auto *VD = VarDecl::Create(
      getASTContext(), getASTContext().getTranslationUnitDecl(), StartLoc,
      StartLoc, VN.getAsIdentifierInfo(), MapperType, TInfo, SC_None);
  if (S)
    SemaRef.PushOnScopeChains(VD, S, /*AddToContext=*/false);
  Expr *E = buildDeclRefExpr(SemaRef, VD, MapperType, StartLoc);
  DSAStack->addDeclareMapperVarRef(E);
  return E;
}

void SemaOpenMP::ActOnOpenMPIteratorVarDecl(VarDecl *VD) {
  if (DSAStack->getDeclareMapperVarRef())
    DSAStack->addIteratorVarDecl(VD);
}

bool SemaOpenMP::isOpenMPDeclareMapperVarDeclAllowed(const VarDecl *VD) const {
  assert(getLangOpts().OpenMP && "Expected OpenMP mode.");
  const Expr *Ref = DSAStack->getDeclareMapperVarRef();
  if (const auto *DRE = cast_or_null<DeclRefExpr>(Ref)) {
    if (VD->getCanonicalDecl() == DRE->getDecl()->getCanonicalDecl())
      return true;
    if (VD->isUsableInConstantExpressions(getASTContext()))
      return true;
    if (getLangOpts().OpenMP >= 52 && DSAStack->isIteratorVarDecl(VD))
      return true;
    return false;
  }
  return true;
}

const ValueDecl *SemaOpenMP::getOpenMPDeclareMapperVarName() const {
  assert(getLangOpts().OpenMP && "Expected OpenMP mode.");
  return cast<DeclRefExpr>(DSAStack->getDeclareMapperVarRef())->getDecl();
}

OMPClause *SemaOpenMP::ActOnOpenMPNumTeamsClause(Expr *NumTeams,
                                                 SourceLocation StartLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation EndLoc) {
  Expr *ValExpr = NumTeams;
  Stmt *HelperValStmt = nullptr;

  // OpenMP [teams Constrcut, Restrictions]
  // The num_teams expression must evaluate to a positive integer value.
  if (!isNonNegativeIntegerValue(ValExpr, SemaRef, OMPC_num_teams,
                                 /*StrictlyPositive=*/true))
    return nullptr;

  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  OpenMPDirectiveKind CaptureRegion = getOpenMPCaptureRegionForClause(
      DKind, OMPC_num_teams, getLangOpts().OpenMP);
  if (CaptureRegion != OMPD_unknown &&
      !SemaRef.CurContext->isDependentContext()) {
    ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
    llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
    ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
    HelperValStmt = buildPreInits(getASTContext(), Captures);
  }

  return new (getASTContext()) OMPNumTeamsClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPThreadLimitClause(Expr *ThreadLimit,
                                                    SourceLocation StartLoc,
                                                    SourceLocation LParenLoc,
                                                    SourceLocation EndLoc) {
  Expr *ValExpr = ThreadLimit;
  Stmt *HelperValStmt = nullptr;

  // OpenMP [teams Constrcut, Restrictions]
  // The thread_limit expression must evaluate to a positive integer value.
  if (!isNonNegativeIntegerValue(ValExpr, SemaRef, OMPC_thread_limit,
                                 /*StrictlyPositive=*/true))
    return nullptr;

  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  OpenMPDirectiveKind CaptureRegion = getOpenMPCaptureRegionForClause(
      DKind, OMPC_thread_limit, getLangOpts().OpenMP);
  if (CaptureRegion != OMPD_unknown &&
      !SemaRef.CurContext->isDependentContext()) {
    ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
    llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
    ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
    HelperValStmt = buildPreInits(getASTContext(), Captures);
  }

  return new (getASTContext()) OMPThreadLimitClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPPriorityClause(Expr *Priority,
                                                 SourceLocation StartLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation EndLoc) {
  Expr *ValExpr = Priority;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;

  // OpenMP [2.9.1, task Constrcut]
  // The priority-value is a non-negative numerical scalar expression.
  if (!isNonNegativeIntegerValue(
          ValExpr, SemaRef, OMPC_priority,
          /*StrictlyPositive=*/false, /*BuildCapture=*/true,
          DSAStack->getCurrentDirective(), &CaptureRegion, &HelperValStmt))
    return nullptr;

  return new (getASTContext()) OMPPriorityClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPGrainsizeClause(
    OpenMPGrainsizeClauseModifier Modifier, Expr *Grainsize,
    SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ModifierLoc, SourceLocation EndLoc) {
  assert((ModifierLoc.isInvalid() || getLangOpts().OpenMP >= 51) &&
         "Unexpected grainsize modifier in OpenMP < 51.");

  if (ModifierLoc.isValid() && Modifier == OMPC_GRAINSIZE_unknown) {
    std::string Values = getListOfPossibleValues(OMPC_grainsize, /*First=*/0,
                                                 OMPC_GRAINSIZE_unknown);
    Diag(ModifierLoc, diag::err_omp_unexpected_clause_value)
        << Values << getOpenMPClauseName(OMPC_grainsize);
    return nullptr;
  }

  Expr *ValExpr = Grainsize;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;

  // OpenMP [2.9.2, taskloop Constrcut]
  // The parameter of the grainsize clause must be a positive integer
  // expression.
  if (!isNonNegativeIntegerValue(ValExpr, SemaRef, OMPC_grainsize,
                                 /*StrictlyPositive=*/true,
                                 /*BuildCapture=*/true,
                                 DSAStack->getCurrentDirective(),
                                 &CaptureRegion, &HelperValStmt))
    return nullptr;

  return new (getASTContext())
      OMPGrainsizeClause(Modifier, ValExpr, HelperValStmt, CaptureRegion,
                         StartLoc, LParenLoc, ModifierLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPNumTasksClause(
    OpenMPNumTasksClauseModifier Modifier, Expr *NumTasks,
    SourceLocation StartLoc, SourceLocation LParenLoc,
    SourceLocation ModifierLoc, SourceLocation EndLoc) {
  assert((ModifierLoc.isInvalid() || getLangOpts().OpenMP >= 51) &&
         "Unexpected num_tasks modifier in OpenMP < 51.");

  if (ModifierLoc.isValid() && Modifier == OMPC_NUMTASKS_unknown) {
    std::string Values = getListOfPossibleValues(OMPC_num_tasks, /*First=*/0,
                                                 OMPC_NUMTASKS_unknown);
    Diag(ModifierLoc, diag::err_omp_unexpected_clause_value)
        << Values << getOpenMPClauseName(OMPC_num_tasks);
    return nullptr;
  }

  Expr *ValExpr = NumTasks;
  Stmt *HelperValStmt = nullptr;
  OpenMPDirectiveKind CaptureRegion = OMPD_unknown;

  // OpenMP [2.9.2, taskloop Constrcut]
  // The parameter of the num_tasks clause must be a positive integer
  // expression.
  if (!isNonNegativeIntegerValue(
          ValExpr, SemaRef, OMPC_num_tasks,
          /*StrictlyPositive=*/true, /*BuildCapture=*/true,
          DSAStack->getCurrentDirective(), &CaptureRegion, &HelperValStmt))
    return nullptr;

  return new (getASTContext())
      OMPNumTasksClause(Modifier, ValExpr, HelperValStmt, CaptureRegion,
                        StartLoc, LParenLoc, ModifierLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPHintClause(Expr *Hint,
                                             SourceLocation StartLoc,
                                             SourceLocation LParenLoc,
                                             SourceLocation EndLoc) {
  // OpenMP [2.13.2, critical construct, Description]
  // ... where hint-expression is an integer constant expression that evaluates
  // to a valid lock hint.
  ExprResult HintExpr =
      VerifyPositiveIntegerConstantInClause(Hint, OMPC_hint, false);
  if (HintExpr.isInvalid())
    return nullptr;
  return new (getASTContext())
      OMPHintClause(HintExpr.get(), StartLoc, LParenLoc, EndLoc);
}

/// Tries to find omp_event_handle_t type.
static bool findOMPEventHandleT(Sema &S, SourceLocation Loc,
                                DSAStackTy *Stack) {
  QualType OMPEventHandleT = Stack->getOMPEventHandleT();
  if (!OMPEventHandleT.isNull())
    return true;
  IdentifierInfo *II = &S.PP.getIdentifierTable().get("omp_event_handle_t");
  ParsedType PT = S.getTypeName(*II, Loc, S.getCurScope());
  if (!PT.getAsOpaquePtr() || PT.get().isNull()) {
    S.Diag(Loc, diag::err_omp_implied_type_not_found) << "omp_event_handle_t";
    return false;
  }
  Stack->setOMPEventHandleT(PT.get());
  return true;
}

OMPClause *SemaOpenMP::ActOnOpenMPDetachClause(Expr *Evt,
                                               SourceLocation StartLoc,
                                               SourceLocation LParenLoc,
                                               SourceLocation EndLoc) {
  if (!Evt->isValueDependent() && !Evt->isTypeDependent() &&
      !Evt->isInstantiationDependent() &&
      !Evt->containsUnexpandedParameterPack()) {
    if (!findOMPEventHandleT(SemaRef, Evt->getExprLoc(), DSAStack))
      return nullptr;
    // OpenMP 5.0, 2.10.1 task Construct.
    // event-handle is a variable of the omp_event_handle_t type.
    auto *Ref = dyn_cast<DeclRefExpr>(Evt->IgnoreParenImpCasts());
    if (!Ref) {
      Diag(Evt->getExprLoc(), diag::err_omp_var_expected)
          << "omp_event_handle_t" << 0 << Evt->getSourceRange();
      return nullptr;
    }
    auto *VD = dyn_cast_or_null<VarDecl>(Ref->getDecl());
    if (!VD) {
      Diag(Evt->getExprLoc(), diag::err_omp_var_expected)
          << "omp_event_handle_t" << 0 << Evt->getSourceRange();
      return nullptr;
    }
    if (!getASTContext().hasSameUnqualifiedType(DSAStack->getOMPEventHandleT(),
                                                VD->getType()) ||
        VD->getType().isConstant(getASTContext())) {
      Diag(Evt->getExprLoc(), diag::err_omp_var_expected)
          << "omp_event_handle_t" << 1 << VD->getType()
          << Evt->getSourceRange();
      return nullptr;
    }
    // OpenMP 5.0, 2.10.1 task Construct
    // [detach clause]... The event-handle will be considered as if it was
    // specified on a firstprivate clause.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(VD, /*FromParent=*/false);
    if (DVar.CKind != OMPC_unknown && DVar.CKind != OMPC_firstprivate &&
        DVar.RefExpr) {
      Diag(Evt->getExprLoc(), diag::err_omp_wrong_dsa)
          << getOpenMPClauseName(DVar.CKind)
          << getOpenMPClauseName(OMPC_firstprivate);
      reportOriginalDsa(SemaRef, DSAStack, VD, DVar);
      return nullptr;
    }
  }

  return new (getASTContext())
      OMPDetachClause(Evt, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPDistScheduleClause(
    OpenMPDistScheduleClauseKind Kind, Expr *ChunkSize, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation KindLoc, SourceLocation CommaLoc,
    SourceLocation EndLoc) {
  if (Kind == OMPC_DIST_SCHEDULE_unknown) {
    std::string Values;
    Values += "'";
    Values += getOpenMPSimpleClauseTypeName(OMPC_dist_schedule, 0);
    Values += "'";
    Diag(KindLoc, diag::err_omp_unexpected_clause_value)
        << Values << getOpenMPClauseName(OMPC_dist_schedule);
    return nullptr;
  }
  Expr *ValExpr = ChunkSize;
  Stmt *HelperValStmt = nullptr;
  if (ChunkSize) {
    if (!ChunkSize->isValueDependent() && !ChunkSize->isTypeDependent() &&
        !ChunkSize->isInstantiationDependent() &&
        !ChunkSize->containsUnexpandedParameterPack()) {
      SourceLocation ChunkSizeLoc = ChunkSize->getBeginLoc();
      ExprResult Val =
          PerformOpenMPImplicitIntegerConversion(ChunkSizeLoc, ChunkSize);
      if (Val.isInvalid())
        return nullptr;

      ValExpr = Val.get();

      // OpenMP [2.7.1, Restrictions]
      //  chunk_size must be a loop invariant integer expression with a positive
      //  value.
      if (std::optional<llvm::APSInt> Result =
              ValExpr->getIntegerConstantExpr(getASTContext())) {
        if (Result->isSigned() && !Result->isStrictlyPositive()) {
          Diag(ChunkSizeLoc, diag::err_omp_negative_expression_in_clause)
              << "dist_schedule" << ChunkSize->getSourceRange();
          return nullptr;
        }
      } else if (getOpenMPCaptureRegionForClause(
                     DSAStack->getCurrentDirective(), OMPC_dist_schedule,
                     getLangOpts().OpenMP) != OMPD_unknown &&
                 !SemaRef.CurContext->isDependentContext()) {
        ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
        llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
        ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
        HelperValStmt = buildPreInits(getASTContext(), Captures);
      }
    }
  }

  return new (getASTContext())
      OMPDistScheduleClause(StartLoc, LParenLoc, KindLoc, CommaLoc, EndLoc,
                            Kind, ValExpr, HelperValStmt);
}

OMPClause *SemaOpenMP::ActOnOpenMPDefaultmapClause(
    OpenMPDefaultmapClauseModifier M, OpenMPDefaultmapClauseKind Kind,
    SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation MLoc,
    SourceLocation KindLoc, SourceLocation EndLoc) {
  if (getLangOpts().OpenMP < 50) {
    if (M != OMPC_DEFAULTMAP_MODIFIER_tofrom ||
        Kind != OMPC_DEFAULTMAP_scalar) {
      std::string Value;
      SourceLocation Loc;
      Value += "'";
      if (M != OMPC_DEFAULTMAP_MODIFIER_tofrom) {
        Value += getOpenMPSimpleClauseTypeName(OMPC_defaultmap,
                                               OMPC_DEFAULTMAP_MODIFIER_tofrom);
        Loc = MLoc;
      } else {
        Value += getOpenMPSimpleClauseTypeName(OMPC_defaultmap,
                                               OMPC_DEFAULTMAP_scalar);
        Loc = KindLoc;
      }
      Value += "'";
      Diag(Loc, diag::err_omp_unexpected_clause_value)
          << Value << getOpenMPClauseName(OMPC_defaultmap);
      return nullptr;
    }
  } else {
    bool isDefaultmapModifier = (M != OMPC_DEFAULTMAP_MODIFIER_unknown);
    bool isDefaultmapKind = (Kind != OMPC_DEFAULTMAP_unknown) ||
                            (getLangOpts().OpenMP >= 50 && KindLoc.isInvalid());
    if (!isDefaultmapKind || !isDefaultmapModifier) {
      StringRef KindValue = "'scalar', 'aggregate', 'pointer'";
      if (getLangOpts().OpenMP == 50) {
        StringRef ModifierValue = "'alloc', 'from', 'to', 'tofrom', "
                                  "'firstprivate', 'none', 'default'";
        if (!isDefaultmapKind && isDefaultmapModifier) {
          Diag(KindLoc, diag::err_omp_unexpected_clause_value)
              << KindValue << getOpenMPClauseName(OMPC_defaultmap);
        } else if (isDefaultmapKind && !isDefaultmapModifier) {
          Diag(MLoc, diag::err_omp_unexpected_clause_value)
              << ModifierValue << getOpenMPClauseName(OMPC_defaultmap);
        } else {
          Diag(MLoc, diag::err_omp_unexpected_clause_value)
              << ModifierValue << getOpenMPClauseName(OMPC_defaultmap);
          Diag(KindLoc, diag::err_omp_unexpected_clause_value)
              << KindValue << getOpenMPClauseName(OMPC_defaultmap);
        }
      } else {
        StringRef ModifierValue =
            "'alloc', 'from', 'to', 'tofrom', "
            "'firstprivate', 'none', 'default', 'present'";
        if (!isDefaultmapKind && isDefaultmapModifier) {
          Diag(KindLoc, diag::err_omp_unexpected_clause_value)
              << KindValue << getOpenMPClauseName(OMPC_defaultmap);
        } else if (isDefaultmapKind && !isDefaultmapModifier) {
          Diag(MLoc, diag::err_omp_unexpected_clause_value)
              << ModifierValue << getOpenMPClauseName(OMPC_defaultmap);
        } else {
          Diag(MLoc, diag::err_omp_unexpected_clause_value)
              << ModifierValue << getOpenMPClauseName(OMPC_defaultmap);
          Diag(KindLoc, diag::err_omp_unexpected_clause_value)
              << KindValue << getOpenMPClauseName(OMPC_defaultmap);
        }
      }
      return nullptr;
    }

    // OpenMP [5.0, 2.12.5, Restrictions, p. 174]
    //  At most one defaultmap clause for each category can appear on the
    //  directive.
    if (DSAStack->checkDefaultmapCategory(Kind)) {
      Diag(StartLoc, diag::err_omp_one_defaultmap_each_category);
      return nullptr;
    }
  }
  if (Kind == OMPC_DEFAULTMAP_unknown) {
    // Variable category is not specified - mark all categories.
    DSAStack->setDefaultDMAAttr(M, OMPC_DEFAULTMAP_aggregate, StartLoc);
    DSAStack->setDefaultDMAAttr(M, OMPC_DEFAULTMAP_scalar, StartLoc);
    DSAStack->setDefaultDMAAttr(M, OMPC_DEFAULTMAP_pointer, StartLoc);
  } else {
    DSAStack->setDefaultDMAAttr(M, Kind, StartLoc);
  }

  return new (getASTContext())
      OMPDefaultmapClause(StartLoc, LParenLoc, MLoc, KindLoc, EndLoc, Kind, M);
}

bool SemaOpenMP::ActOnStartOpenMPDeclareTargetContext(
    DeclareTargetContextInfo &DTCI) {
  DeclContext *CurLexicalContext = SemaRef.getCurLexicalContext();
  if (!CurLexicalContext->isFileContext() &&
      !CurLexicalContext->isExternCContext() &&
      !CurLexicalContext->isExternCXXContext() &&
      !isa<CXXRecordDecl>(CurLexicalContext) &&
      !isa<ClassTemplateDecl>(CurLexicalContext) &&
      !isa<ClassTemplatePartialSpecializationDecl>(CurLexicalContext) &&
      !isa<ClassTemplateSpecializationDecl>(CurLexicalContext)) {
    Diag(DTCI.Loc, diag::err_omp_region_not_file_context);
    return false;
  }

  // Report affected OpenMP target offloading behavior when in HIP lang-mode.
  if (getLangOpts().HIP)
    Diag(DTCI.Loc, diag::warn_hip_omp_target_directives);

  DeclareTargetNesting.push_back(DTCI);
  return true;
}

const SemaOpenMP::DeclareTargetContextInfo
SemaOpenMP::ActOnOpenMPEndDeclareTargetDirective() {
  assert(!DeclareTargetNesting.empty() &&
         "check isInOpenMPDeclareTargetContext() first!");
  return DeclareTargetNesting.pop_back_val();
}

void SemaOpenMP::ActOnFinishedOpenMPDeclareTargetContext(
    DeclareTargetContextInfo &DTCI) {
  for (auto &It : DTCI.ExplicitlyMapped)
    ActOnOpenMPDeclareTargetName(It.first, It.second.Loc, It.second.MT, DTCI);
}

void SemaOpenMP::DiagnoseUnterminatedOpenMPDeclareTarget() {
  if (DeclareTargetNesting.empty())
    return;
  DeclareTargetContextInfo &DTCI = DeclareTargetNesting.back();
  Diag(DTCI.Loc, diag::warn_omp_unterminated_declare_target)
      << getOpenMPDirectiveName(DTCI.Kind);
}

NamedDecl *SemaOpenMP::lookupOpenMPDeclareTargetName(
    Scope *CurScope, CXXScopeSpec &ScopeSpec, const DeclarationNameInfo &Id) {
  LookupResult Lookup(SemaRef, Id, Sema::LookupOrdinaryName);
  SemaRef.LookupParsedName(Lookup, CurScope, &ScopeSpec,
                           /*ObjectType=*/QualType(),
                           /*AllowBuiltinCreation=*/true);

  if (Lookup.isAmbiguous())
    return nullptr;
  Lookup.suppressDiagnostics();

  if (!Lookup.isSingleResult()) {
    VarOrFuncDeclFilterCCC CCC(SemaRef);
    if (TypoCorrection Corrected =
            SemaRef.CorrectTypo(Id, Sema::LookupOrdinaryName, CurScope, nullptr,
                                CCC, Sema::CTK_ErrorRecovery)) {
      SemaRef.diagnoseTypo(Corrected,
                           PDiag(diag::err_undeclared_var_use_suggest)
                               << Id.getName());
      checkDeclIsAllowedInOpenMPTarget(nullptr, Corrected.getCorrectionDecl());
      return nullptr;
    }

    Diag(Id.getLoc(), diag::err_undeclared_var_use) << Id.getName();
    return nullptr;
  }

  NamedDecl *ND = Lookup.getAsSingle<NamedDecl>();
  if (!isa<VarDecl>(ND) && !isa<FunctionDecl>(ND) &&
      !isa<FunctionTemplateDecl>(ND)) {
    Diag(Id.getLoc(), diag::err_omp_invalid_target_decl) << Id.getName();
    return nullptr;
  }
  return ND;
}

void SemaOpenMP::ActOnOpenMPDeclareTargetName(
    NamedDecl *ND, SourceLocation Loc, OMPDeclareTargetDeclAttr::MapTypeTy MT,
    DeclareTargetContextInfo &DTCI) {
  assert((isa<VarDecl>(ND) || isa<FunctionDecl>(ND) ||
          isa<FunctionTemplateDecl>(ND)) &&
         "Expected variable, function or function template.");

  if (auto *VD = dyn_cast<VarDecl>(ND)) {
    // Only global variables can be marked as declare target.
    if (!VD->isFileVarDecl() && !VD->isStaticLocal() &&
        !VD->isStaticDataMember()) {
      Diag(Loc, diag::err_omp_declare_target_has_local_vars)
          << VD->getNameAsString();
      return;
    }
  }
  // Diagnose marking after use as it may lead to incorrect diagnosis and
  // codegen.
  if (getLangOpts().OpenMP >= 50 &&
      (ND->isUsed(/*CheckUsedAttr=*/false) || ND->isReferenced()))
    Diag(Loc, diag::warn_omp_declare_target_after_first_use);

  // Report affected OpenMP target offloading behavior when in HIP lang-mode.
  if (getLangOpts().HIP)
    Diag(Loc, diag::warn_hip_omp_target_directives);

  // Explicit declare target lists have precedence.
  const unsigned Level = -1;

  auto *VD = cast<ValueDecl>(ND);
  std::optional<OMPDeclareTargetDeclAttr *> ActiveAttr =
      OMPDeclareTargetDeclAttr::getActiveAttr(VD);
  if (ActiveAttr && (*ActiveAttr)->getDevType() != DTCI.DT &&
      (*ActiveAttr)->getLevel() == Level) {
    Diag(Loc, diag::err_omp_device_type_mismatch)
        << OMPDeclareTargetDeclAttr::ConvertDevTypeTyToStr(DTCI.DT)
        << OMPDeclareTargetDeclAttr::ConvertDevTypeTyToStr(
               (*ActiveAttr)->getDevType());
    return;
  }
  if (ActiveAttr && (*ActiveAttr)->getMapType() != MT &&
      (*ActiveAttr)->getLevel() == Level) {
    Diag(Loc, diag::err_omp_declare_target_to_and_link) << ND;
    return;
  }

  if (ActiveAttr && (*ActiveAttr)->getLevel() == Level)
    return;

  Expr *IndirectE = nullptr;
  bool IsIndirect = false;
  if (DTCI.Indirect) {
    IndirectE = *DTCI.Indirect;
    if (!IndirectE)
      IsIndirect = true;
  }
  auto *A = OMPDeclareTargetDeclAttr::CreateImplicit(
      getASTContext(), MT, DTCI.DT, IndirectE, IsIndirect, Level,
      SourceRange(Loc, Loc));
  ND->addAttr(A);
  if (ASTMutationListener *ML = getASTContext().getASTMutationListener())
    ML->DeclarationMarkedOpenMPDeclareTarget(ND, A);
  checkDeclIsAllowedInOpenMPTarget(nullptr, ND, Loc);
  if (auto *VD = dyn_cast<VarDecl>(ND);
      getLangOpts().OpenMP && VD && VD->hasAttr<OMPDeclareTargetDeclAttr>() &&
      VD->hasGlobalStorage())
    ActOnOpenMPDeclareTargetInitializer(ND);
}

static void checkDeclInTargetContext(SourceLocation SL, SourceRange SR,
                                     Sema &SemaRef, Decl *D) {
  if (!D || !isa<VarDecl>(D))
    return;
  auto *VD = cast<VarDecl>(D);
  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> MapTy =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
  if (SemaRef.LangOpts.OpenMP >= 50 &&
      (SemaRef.getCurLambda(/*IgnoreNonLambdaCapturingScope=*/true) ||
       SemaRef.getCurBlock() || SemaRef.getCurCapturedRegion()) &&
      VD->hasGlobalStorage()) {
    if (!MapTy || (*MapTy != OMPDeclareTargetDeclAttr::MT_To &&
                   *MapTy != OMPDeclareTargetDeclAttr::MT_Enter)) {
      // OpenMP 5.0, 2.12.7 declare target Directive, Restrictions
      // If a lambda declaration and definition appears between a
      // declare target directive and the matching end declare target
      // directive, all variables that are captured by the lambda
      // expression must also appear in a to clause.
      SemaRef.Diag(VD->getLocation(),
                   diag::err_omp_lambda_capture_in_declare_target_not_to);
      SemaRef.Diag(SL, diag::note_var_explicitly_captured_here)
          << VD << 0 << SR;
      return;
    }
  }
  if (MapTy)
    return;
  SemaRef.Diag(VD->getLocation(), diag::warn_omp_not_in_target_context);
  SemaRef.Diag(SL, diag::note_used_here) << SR;
}

static bool checkValueDeclInTarget(SourceLocation SL, SourceRange SR,
                                   Sema &SemaRef, DSAStackTy *Stack,
                                   ValueDecl *VD) {
  return OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD) ||
         checkTypeMappable(SL, SR, SemaRef, Stack, VD->getType(),
                           /*FullCheck=*/false);
}

void SemaOpenMP::checkDeclIsAllowedInOpenMPTarget(Expr *E, Decl *D,
                                                  SourceLocation IdLoc) {
  if (!D || D->isInvalidDecl())
    return;
  SourceRange SR = E ? E->getSourceRange() : D->getSourceRange();
  SourceLocation SL = E ? E->getBeginLoc() : D->getLocation();
  if (auto *VD = dyn_cast<VarDecl>(D)) {
    // Only global variables can be marked as declare target.
    if (!VD->isFileVarDecl() && !VD->isStaticLocal() &&
        !VD->isStaticDataMember())
      return;
    // 2.10.6: threadprivate variable cannot appear in a declare target
    // directive.
    if (DSAStack->isThreadPrivate(VD)) {
      Diag(SL, diag::err_omp_threadprivate_in_target);
      reportOriginalDsa(SemaRef, DSAStack, VD, DSAStack->getTopDSA(VD, false));
      return;
    }
  }
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
    D = FTD->getTemplatedDecl();
  if (auto *FD = dyn_cast<FunctionDecl>(D)) {
    std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
        OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(FD);
    if (IdLoc.isValid() && Res && *Res == OMPDeclareTargetDeclAttr::MT_Link) {
      Diag(IdLoc, diag::err_omp_function_in_link_clause);
      Diag(FD->getLocation(), diag::note_defined_here) << FD;
      return;
    }
  }
  if (auto *VD = dyn_cast<ValueDecl>(D)) {
    // Problem if any with var declared with incomplete type will be reported
    // as normal, so no need to check it here.
    if ((E || !VD->getType()->isIncompleteType()) &&
        !checkValueDeclInTarget(SL, SR, SemaRef, DSAStack, VD))
      return;
    if (!E && isInOpenMPDeclareTargetContext()) {
      // Checking declaration inside declare target region.
      if (isa<VarDecl>(D) || isa<FunctionDecl>(D) ||
          isa<FunctionTemplateDecl>(D)) {
        std::optional<OMPDeclareTargetDeclAttr *> ActiveAttr =
            OMPDeclareTargetDeclAttr::getActiveAttr(VD);
        unsigned Level = DeclareTargetNesting.size();
        if (ActiveAttr && (*ActiveAttr)->getLevel() >= Level)
          return;
        DeclareTargetContextInfo &DTCI = DeclareTargetNesting.back();
        Expr *IndirectE = nullptr;
        bool IsIndirect = false;
        if (DTCI.Indirect) {
          IndirectE = *DTCI.Indirect;
          if (!IndirectE)
            IsIndirect = true;
        }
        auto *A = OMPDeclareTargetDeclAttr::CreateImplicit(
            getASTContext(),
            getLangOpts().OpenMP >= 52 ? OMPDeclareTargetDeclAttr::MT_Enter
                                       : OMPDeclareTargetDeclAttr::MT_To,
            DTCI.DT, IndirectE, IsIndirect, Level,
            SourceRange(DTCI.Loc, DTCI.Loc));
        D->addAttr(A);
        if (ASTMutationListener *ML = getASTContext().getASTMutationListener())
          ML->DeclarationMarkedOpenMPDeclareTarget(D, A);
      }
      return;
    }
  }
  if (!E)
    return;
  checkDeclInTargetContext(E->getExprLoc(), E->getSourceRange(), SemaRef, D);
}

/// This class visits every VarDecl that the initializer references and adds
/// OMPDeclareTargetDeclAttr to each of them.
class GlobalDeclRefChecker final : public StmtVisitor<GlobalDeclRefChecker> {
  SmallVector<VarDecl *> DeclVector;
  Attr *A;

public:
  /// A StmtVisitor class function that visits all DeclRefExpr and adds
  /// OMPDeclareTargetDeclAttr to them.
  void VisitDeclRefExpr(DeclRefExpr *Node) {
    if (auto *VD = dyn_cast<VarDecl>(Node->getDecl())) {
      VD->addAttr(A);
      DeclVector.push_back(VD);
    }
  }
  /// A function that iterates across each of the Expr's children.
  void VisitExpr(Expr *Ex) {
    for (auto *Child : Ex->children()) {
      Visit(Child);
    }
  }
  /// A function that keeps a record of all the Decls that are variables, has
  /// OMPDeclareTargetDeclAttr, and has global storage in the DeclVector. Pop
  /// each Decl one at a time and use the inherited 'visit' functions to look
  /// for DeclRefExpr.
  void declareTargetInitializer(Decl *TD) {
    A = TD->getAttr<OMPDeclareTargetDeclAttr>();
    DeclVector.push_back(cast<VarDecl>(TD));
    while (!DeclVector.empty()) {
      VarDecl *TargetVarDecl = DeclVector.pop_back_val();
      if (TargetVarDecl->hasAttr<OMPDeclareTargetDeclAttr>() &&
          TargetVarDecl->hasInit() && TargetVarDecl->hasGlobalStorage()) {
        if (Expr *Ex = TargetVarDecl->getInit())
          Visit(Ex);
      }
    }
  }
};

/// Adding OMPDeclareTargetDeclAttr to variables with static storage
/// duration that are referenced in the initializer expression list of
/// variables with static storage duration in declare target directive.
void SemaOpenMP::ActOnOpenMPDeclareTargetInitializer(Decl *TargetDecl) {
  GlobalDeclRefChecker Checker;
  if (isa<VarDecl>(TargetDecl))
    Checker.declareTargetInitializer(TargetDecl);
}

OMPClause *SemaOpenMP::ActOnOpenMPToClause(
    ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
    ArrayRef<SourceLocation> MotionModifiersLoc,
    CXXScopeSpec &MapperIdScopeSpec, DeclarationNameInfo &MapperId,
    SourceLocation ColonLoc, ArrayRef<Expr *> VarList,
    const OMPVarListLocTy &Locs, ArrayRef<Expr *> UnresolvedMappers) {
  OpenMPMotionModifierKind Modifiers[] = {OMPC_MOTION_MODIFIER_unknown,
                                          OMPC_MOTION_MODIFIER_unknown};
  SourceLocation ModifiersLoc[NumberOfOMPMotionModifiers];

  // Process motion-modifiers, flag errors for duplicate modifiers.
  unsigned Count = 0;
  for (unsigned I = 0, E = MotionModifiers.size(); I < E; ++I) {
    if (MotionModifiers[I] != OMPC_MOTION_MODIFIER_unknown &&
        llvm::is_contained(Modifiers, MotionModifiers[I])) {
      Diag(MotionModifiersLoc[I], diag::err_omp_duplicate_motion_modifier);
      continue;
    }
    assert(Count < NumberOfOMPMotionModifiers &&
           "Modifiers exceed the allowed number of motion modifiers");
    Modifiers[Count] = MotionModifiers[I];
    ModifiersLoc[Count] = MotionModifiersLoc[I];
    ++Count;
  }

  MappableVarListInfo MVLI(VarList);
  checkMappableExpressionList(SemaRef, DSAStack, OMPC_to, MVLI, Locs.StartLoc,
                              MapperIdScopeSpec, MapperId, UnresolvedMappers);
  if (MVLI.ProcessedVarList.empty())
    return nullptr;

  return OMPToClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, MVLI.VarBaseDeclarations,
      MVLI.VarComponents, MVLI.UDMapperList, Modifiers, ModifiersLoc,
      MapperIdScopeSpec.getWithLocInContext(getASTContext()), MapperId);
}

OMPClause *SemaOpenMP::ActOnOpenMPFromClause(
    ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
    ArrayRef<SourceLocation> MotionModifiersLoc,
    CXXScopeSpec &MapperIdScopeSpec, DeclarationNameInfo &MapperId,
    SourceLocation ColonLoc, ArrayRef<Expr *> VarList,
    const OMPVarListLocTy &Locs, ArrayRef<Expr *> UnresolvedMappers) {
  OpenMPMotionModifierKind Modifiers[] = {OMPC_MOTION_MODIFIER_unknown,
                                          OMPC_MOTION_MODIFIER_unknown};
  SourceLocation ModifiersLoc[NumberOfOMPMotionModifiers];

  // Process motion-modifiers, flag errors for duplicate modifiers.
  unsigned Count = 0;
  for (unsigned I = 0, E = MotionModifiers.size(); I < E; ++I) {
    if (MotionModifiers[I] != OMPC_MOTION_MODIFIER_unknown &&
        llvm::is_contained(Modifiers, MotionModifiers[I])) {
      Diag(MotionModifiersLoc[I], diag::err_omp_duplicate_motion_modifier);
      continue;
    }
    assert(Count < NumberOfOMPMotionModifiers &&
           "Modifiers exceed the allowed number of motion modifiers");
    Modifiers[Count] = MotionModifiers[I];
    ModifiersLoc[Count] = MotionModifiersLoc[I];
    ++Count;
  }

  MappableVarListInfo MVLI(VarList);
  checkMappableExpressionList(SemaRef, DSAStack, OMPC_from, MVLI, Locs.StartLoc,
                              MapperIdScopeSpec, MapperId, UnresolvedMappers);
  if (MVLI.ProcessedVarList.empty())
    return nullptr;

  return OMPFromClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, MVLI.VarBaseDeclarations,
      MVLI.VarComponents, MVLI.UDMapperList, Modifiers, ModifiersLoc,
      MapperIdScopeSpec.getWithLocInContext(getASTContext()), MapperId);
}

OMPClause *
SemaOpenMP::ActOnOpenMPUseDevicePtrClause(ArrayRef<Expr *> VarList,
                                          const OMPVarListLocTy &Locs) {
  MappableVarListInfo MVLI(VarList);
  SmallVector<Expr *, 8> PrivateCopies;
  SmallVector<Expr *, 8> Inits;

  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP use_device_ptr clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      MVLI.ProcessedVarList.push_back(RefExpr);
      PrivateCopies.push_back(nullptr);
      Inits.push_back(nullptr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType Type = D->getType();
    Type = Type.getNonReferenceType().getUnqualifiedType();

    auto *VD = dyn_cast<VarDecl>(D);

    // Item should be a pointer or reference to pointer.
    if (!Type->isPointerType()) {
      Diag(ELoc, diag::err_omp_usedeviceptr_not_a_pointer)
          << 0 << RefExpr->getSourceRange();
      continue;
    }

    // Build the private variable and the expression that refers to it.
    auto VDPrivate =
        buildVarDecl(SemaRef, ELoc, Type, D->getName(),
                     D->hasAttrs() ? &D->getAttrs() : nullptr,
                     VD ? cast<DeclRefExpr>(SimpleRefExpr) : nullptr);
    if (VDPrivate->isInvalidDecl())
      continue;

    SemaRef.CurContext->addDecl(VDPrivate);
    DeclRefExpr *VDPrivateRefExpr = buildDeclRefExpr(
        SemaRef, VDPrivate, RefExpr->getType().getUnqualifiedType(), ELoc);

    // Add temporary variable to initialize the private copy of the pointer.
    VarDecl *VDInit =
        buildVarDecl(SemaRef, RefExpr->getExprLoc(), Type, ".devptr.temp");
    DeclRefExpr *VDInitRefExpr = buildDeclRefExpr(
        SemaRef, VDInit, RefExpr->getType(), RefExpr->getExprLoc());
    SemaRef.AddInitializerToDecl(
        VDPrivate, SemaRef.DefaultLvalueConversion(VDInitRefExpr).get(),
        /*DirectInit=*/false);

    // If required, build a capture to implement the privatization initialized
    // with the current list item value.
    DeclRefExpr *Ref = nullptr;
    if (!VD)
      Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/true);
    MVLI.ProcessedVarList.push_back(VD ? RefExpr->IgnoreParens() : Ref);
    PrivateCopies.push_back(VDPrivateRefExpr);
    Inits.push_back(VDInitRefExpr);

    // We need to add a data sharing attribute for this variable to make sure it
    // is correctly captured. A variable that shows up in a use_device_ptr has
    // similar properties of a first private variable.
    DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_firstprivate, Ref);

    // Create a mappable component for the list item. List items in this clause
    // only need a component.
    MVLI.VarBaseDeclarations.push_back(D);
    MVLI.VarComponents.resize(MVLI.VarComponents.size() + 1);
    MVLI.VarComponents.back().emplace_back(SimpleRefExpr, D,
                                           /*IsNonContiguous=*/false);
  }

  if (MVLI.ProcessedVarList.empty())
    return nullptr;

  return OMPUseDevicePtrClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, PrivateCopies, Inits,
      MVLI.VarBaseDeclarations, MVLI.VarComponents);
}

OMPClause *
SemaOpenMP::ActOnOpenMPUseDeviceAddrClause(ArrayRef<Expr *> VarList,
                                           const OMPVarListLocTy &Locs) {
  MappableVarListInfo MVLI(VarList);

  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP use_device_addr clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange,
                              /*AllowArraySection=*/true);
    if (Res.second) {
      // It will be analyzed later.
      MVLI.ProcessedVarList.push_back(RefExpr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;
    auto *VD = dyn_cast<VarDecl>(D);

    // If required, build a capture to implement the privatization initialized
    // with the current list item value.
    DeclRefExpr *Ref = nullptr;
    if (!VD)
      Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/true);
    MVLI.ProcessedVarList.push_back(VD ? RefExpr->IgnoreParens() : Ref);

    // We need to add a data sharing attribute for this variable to make sure it
    // is correctly captured. A variable that shows up in a use_device_addr has
    // similar properties of a first private variable.
    DSAStack->addDSA(D, RefExpr->IgnoreParens(), OMPC_firstprivate, Ref);

    // Create a mappable component for the list item. List items in this clause
    // only need a component.
    MVLI.VarBaseDeclarations.push_back(D);
    MVLI.VarComponents.emplace_back();
    Expr *Component = SimpleRefExpr;
    if (VD && (isa<ArraySectionExpr>(RefExpr->IgnoreParenImpCasts()) ||
               isa<ArraySubscriptExpr>(RefExpr->IgnoreParenImpCasts())))
      Component =
          SemaRef.DefaultFunctionArrayLvalueConversion(SimpleRefExpr).get();
    MVLI.VarComponents.back().emplace_back(Component, D,
                                           /*IsNonContiguous=*/false);
  }

  if (MVLI.ProcessedVarList.empty())
    return nullptr;

  return OMPUseDeviceAddrClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, MVLI.VarBaseDeclarations,
      MVLI.VarComponents);
}

OMPClause *
SemaOpenMP::ActOnOpenMPIsDevicePtrClause(ArrayRef<Expr *> VarList,
                                         const OMPVarListLocTy &Locs) {
  MappableVarListInfo MVLI(VarList);
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP is_device_ptr clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      MVLI.ProcessedVarList.push_back(RefExpr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    QualType Type = D->getType();
    // item should be a pointer or array or reference to pointer or array
    if (!Type.getNonReferenceType()->isPointerType() &&
        !Type.getNonReferenceType()->isArrayType()) {
      Diag(ELoc, diag::err_omp_argument_type_isdeviceptr)
          << 0 << RefExpr->getSourceRange();
      continue;
    }

    // Check if the declaration in the clause does not show up in any data
    // sharing attribute.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(D, /*FromParent=*/false);
    if (isOpenMPPrivate(DVar.CKind)) {
      Diag(ELoc, diag::err_omp_variable_in_given_clause_and_dsa)
          << getOpenMPClauseName(DVar.CKind)
          << getOpenMPClauseName(OMPC_is_device_ptr)
          << getOpenMPDirectiveName(DSAStack->getCurrentDirective());
      reportOriginalDsa(SemaRef, DSAStack, D, DVar);
      continue;
    }

    const Expr *ConflictExpr;
    if (DSAStack->checkMappableExprComponentListsForDecl(
            D, /*CurrentRegionOnly=*/true,
            [&ConflictExpr](
                OMPClauseMappableExprCommon::MappableExprComponentListRef R,
                OpenMPClauseKind) -> bool {
              ConflictExpr = R.front().getAssociatedExpression();
              return true;
            })) {
      Diag(ELoc, diag::err_omp_map_shared_storage) << RefExpr->getSourceRange();
      Diag(ConflictExpr->getExprLoc(), diag::note_used_here)
          << ConflictExpr->getSourceRange();
      continue;
    }

    // Store the components in the stack so that they can be used to check
    // against other clauses later on.
    OMPClauseMappableExprCommon::MappableComponent MC(
        SimpleRefExpr, D, /*IsNonContiguous=*/false);
    DSAStack->addMappableExpressionComponents(
        D, MC, /*WhereFoundClauseKind=*/OMPC_is_device_ptr);

    // Record the expression we've just processed.
    MVLI.ProcessedVarList.push_back(SimpleRefExpr);

    // Create a mappable component for the list item. List items in this clause
    // only need a component. We use a null declaration to signal fields in
    // 'this'.
    assert((isa<DeclRefExpr>(SimpleRefExpr) ||
            isa<CXXThisExpr>(cast<MemberExpr>(SimpleRefExpr)->getBase())) &&
           "Unexpected device pointer expression!");
    MVLI.VarBaseDeclarations.push_back(
        isa<DeclRefExpr>(SimpleRefExpr) ? D : nullptr);
    MVLI.VarComponents.resize(MVLI.VarComponents.size() + 1);
    MVLI.VarComponents.back().push_back(MC);
  }

  if (MVLI.ProcessedVarList.empty())
    return nullptr;

  return OMPIsDevicePtrClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, MVLI.VarBaseDeclarations,
      MVLI.VarComponents);
}

OMPClause *
SemaOpenMP::ActOnOpenMPHasDeviceAddrClause(ArrayRef<Expr *> VarList,
                                           const OMPVarListLocTy &Locs) {
  MappableVarListInfo MVLI(VarList);
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP has_device_addr clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange,
                              /*AllowArraySection=*/true);
    if (Res.second) {
      // It will be analyzed later.
      MVLI.ProcessedVarList.push_back(RefExpr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    // Check if the declaration in the clause does not show up in any data
    // sharing attribute.
    DSAStackTy::DSAVarData DVar = DSAStack->getTopDSA(D, /*FromParent=*/false);
    if (isOpenMPPrivate(DVar.CKind)) {
      Diag(ELoc, diag::err_omp_variable_in_given_clause_and_dsa)
          << getOpenMPClauseName(DVar.CKind)
          << getOpenMPClauseName(OMPC_has_device_addr)
          << getOpenMPDirectiveName(DSAStack->getCurrentDirective());
      reportOriginalDsa(SemaRef, DSAStack, D, DVar);
      continue;
    }

    const Expr *ConflictExpr;
    if (DSAStack->checkMappableExprComponentListsForDecl(
            D, /*CurrentRegionOnly=*/true,
            [&ConflictExpr](
                OMPClauseMappableExprCommon::MappableExprComponentListRef R,
                OpenMPClauseKind) -> bool {
              ConflictExpr = R.front().getAssociatedExpression();
              return true;
            })) {
      Diag(ELoc, diag::err_omp_map_shared_storage) << RefExpr->getSourceRange();
      Diag(ConflictExpr->getExprLoc(), diag::note_used_here)
          << ConflictExpr->getSourceRange();
      continue;
    }

    // Store the components in the stack so that they can be used to check
    // against other clauses later on.
    Expr *Component = SimpleRefExpr;
    auto *VD = dyn_cast<VarDecl>(D);
    if (VD && (isa<ArraySectionExpr>(RefExpr->IgnoreParenImpCasts()) ||
               isa<ArraySubscriptExpr>(RefExpr->IgnoreParenImpCasts())))
      Component =
          SemaRef.DefaultFunctionArrayLvalueConversion(SimpleRefExpr).get();
    OMPClauseMappableExprCommon::MappableComponent MC(
        Component, D, /*IsNonContiguous=*/false);
    DSAStack->addMappableExpressionComponents(
        D, MC, /*WhereFoundClauseKind=*/OMPC_has_device_addr);

    // Record the expression we've just processed.
    if (!VD && !SemaRef.CurContext->isDependentContext()) {
      DeclRefExpr *Ref =
          buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/true);
      assert(Ref && "has_device_addr capture failed");
      MVLI.ProcessedVarList.push_back(Ref);
    } else
      MVLI.ProcessedVarList.push_back(RefExpr->IgnoreParens());

    // Create a mappable component for the list item. List items in this clause
    // only need a component. We use a null declaration to signal fields in
    // 'this'.
    assert((isa<DeclRefExpr>(SimpleRefExpr) ||
            isa<CXXThisExpr>(cast<MemberExpr>(SimpleRefExpr)->getBase())) &&
           "Unexpected device pointer expression!");
    MVLI.VarBaseDeclarations.push_back(
        isa<DeclRefExpr>(SimpleRefExpr) ? D : nullptr);
    MVLI.VarComponents.resize(MVLI.VarComponents.size() + 1);
    MVLI.VarComponents.back().push_back(MC);
  }

  if (MVLI.ProcessedVarList.empty())
    return nullptr;

  return OMPHasDeviceAddrClause::Create(
      getASTContext(), Locs, MVLI.ProcessedVarList, MVLI.VarBaseDeclarations,
      MVLI.VarComponents);
}

OMPClause *SemaOpenMP::ActOnOpenMPAllocateClause(
    Expr *Allocator, ArrayRef<Expr *> VarList, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc) {
  if (Allocator) {
    // OpenMP [2.11.4 allocate Clause, Description]
    // allocator is an expression of omp_allocator_handle_t type.
    if (!findOMPAllocatorHandleT(SemaRef, Allocator->getExprLoc(), DSAStack))
      return nullptr;

    ExprResult AllocatorRes = SemaRef.DefaultLvalueConversion(Allocator);
    if (AllocatorRes.isInvalid())
      return nullptr;
    AllocatorRes = SemaRef.PerformImplicitConversion(
        AllocatorRes.get(), DSAStack->getOMPAllocatorHandleT(),
        Sema::AA_Initializing,
        /*AllowExplicit=*/true);
    if (AllocatorRes.isInvalid())
      return nullptr;
    Allocator = AllocatorRes.get();
  } else {
    // OpenMP 5.0, 2.11.4 allocate Clause, Restrictions.
    // allocate clauses that appear on a target construct or on constructs in a
    // target region must specify an allocator expression unless a requires
    // directive with the dynamic_allocators clause is present in the same
    // compilation unit.
    if (getLangOpts().OpenMPIsTargetDevice &&
        !DSAStack->hasRequiresDeclWithClause<OMPDynamicAllocatorsClause>())
      SemaRef.targetDiag(StartLoc, diag::err_expected_allocator_expression);
  }
  // Analyze and build list of variables.
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP private clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
    }
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    auto *VD = dyn_cast<VarDecl>(D);
    DeclRefExpr *Ref = nullptr;
    if (!VD && !SemaRef.CurContext->isDependentContext())
      Ref = buildCapture(SemaRef, D, SimpleRefExpr, /*WithInit=*/false);
    Vars.push_back((VD || SemaRef.CurContext->isDependentContext())
                       ? RefExpr->IgnoreParens()
                       : Ref);
  }

  if (Vars.empty())
    return nullptr;

  if (Allocator)
    DSAStack->addInnerAllocatorExpr(Allocator);
  return OMPAllocateClause::Create(getASTContext(), StartLoc, LParenLoc,
                                   Allocator, ColonLoc, EndLoc, Vars);
}

OMPClause *SemaOpenMP::ActOnOpenMPNontemporalClause(ArrayRef<Expr *> VarList,
                                                    SourceLocation StartLoc,
                                                    SourceLocation LParenLoc,
                                                    SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP nontemporal clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange);
    if (Res.second)
      // It will be analyzed later.
      Vars.push_back(RefExpr);
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    // OpenMP 5.0, 2.9.3.1 simd Construct, Restrictions.
    // A list-item cannot appear in more than one nontemporal clause.
    if (const Expr *PrevRef =
            DSAStack->addUniqueNontemporal(D, SimpleRefExpr)) {
      Diag(ELoc, diag::err_omp_used_in_clause_twice)
          << 0 << getOpenMPClauseName(OMPC_nontemporal) << ERange;
      Diag(PrevRef->getExprLoc(), diag::note_omp_explicit_dsa)
          << getOpenMPClauseName(OMPC_nontemporal);
      continue;
    }

    Vars.push_back(RefExpr);
  }

  if (Vars.empty())
    return nullptr;

  return OMPNontemporalClause::Create(getASTContext(), StartLoc, LParenLoc,
                                      EndLoc, Vars);
}

StmtResult SemaOpenMP::ActOnOpenMPScopeDirective(ArrayRef<OMPClause *> Clauses,
                                                 Stmt *AStmt,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc) {
  if (!AStmt)
    return StmtError();

  SemaRef.setFunctionHasBranchProtectedScope();

  return OMPScopeDirective::Create(getASTContext(), StartLoc, EndLoc, Clauses,
                                   AStmt);
}

OMPClause *SemaOpenMP::ActOnOpenMPInclusiveClause(ArrayRef<Expr *> VarList,
                                                  SourceLocation StartLoc,
                                                  SourceLocation LParenLoc,
                                                  SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP nontemporal clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange,
                              /*AllowArraySection=*/true);
    if (Res.second)
      // It will be analyzed later.
      Vars.push_back(RefExpr);
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    const DSAStackTy::DSAVarData DVar =
        DSAStack->getTopDSA(D, /*FromParent=*/true);
    // OpenMP 5.0, 2.9.6, scan Directive, Restrictions.
    // A list item that appears in the inclusive or exclusive clause must appear
    // in a reduction clause with the inscan modifier on the enclosing
    // worksharing-loop, worksharing-loop SIMD, or simd construct.
    if (DVar.CKind != OMPC_reduction || DVar.Modifier != OMPC_REDUCTION_inscan)
      Diag(ELoc, diag::err_omp_inclusive_exclusive_not_reduction)
          << RefExpr->getSourceRange();

    if (DSAStack->getParentDirective() != OMPD_unknown)
      DSAStack->markDeclAsUsedInScanDirective(D);
    Vars.push_back(RefExpr);
  }

  if (Vars.empty())
    return nullptr;

  return OMPInclusiveClause::Create(getASTContext(), StartLoc, LParenLoc,
                                    EndLoc, Vars);
}

OMPClause *SemaOpenMP::ActOnOpenMPExclusiveClause(ArrayRef<Expr *> VarList,
                                                  SourceLocation StartLoc,
                                                  SourceLocation LParenLoc,
                                                  SourceLocation EndLoc) {
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : VarList) {
    assert(RefExpr && "NULL expr in OpenMP nontemporal clause.");
    SourceLocation ELoc;
    SourceRange ERange;
    Expr *SimpleRefExpr = RefExpr;
    auto Res = getPrivateItem(SemaRef, SimpleRefExpr, ELoc, ERange,
                              /*AllowArraySection=*/true);
    if (Res.second)
      // It will be analyzed later.
      Vars.push_back(RefExpr);
    ValueDecl *D = Res.first;
    if (!D)
      continue;

    OpenMPDirectiveKind ParentDirective = DSAStack->getParentDirective();
    DSAStackTy::DSAVarData DVar;
    if (ParentDirective != OMPD_unknown)
      DVar = DSAStack->getTopDSA(D, /*FromParent=*/true);
    // OpenMP 5.0, 2.9.6, scan Directive, Restrictions.
    // A list item that appears in the inclusive or exclusive clause must appear
    // in a reduction clause with the inscan modifier on the enclosing
    // worksharing-loop, worksharing-loop SIMD, or simd construct.
    if (ParentDirective == OMPD_unknown || DVar.CKind != OMPC_reduction ||
        DVar.Modifier != OMPC_REDUCTION_inscan) {
      Diag(ELoc, diag::err_omp_inclusive_exclusive_not_reduction)
          << RefExpr->getSourceRange();
    } else {
      DSAStack->markDeclAsUsedInScanDirective(D);
    }
    Vars.push_back(RefExpr);
  }

  if (Vars.empty())
    return nullptr;

  return OMPExclusiveClause::Create(getASTContext(), StartLoc, LParenLoc,
                                    EndLoc, Vars);
}

/// Tries to find omp_alloctrait_t type.
static bool findOMPAlloctraitT(Sema &S, SourceLocation Loc, DSAStackTy *Stack) {
  QualType OMPAlloctraitT = Stack->getOMPAlloctraitT();
  if (!OMPAlloctraitT.isNull())
    return true;
  IdentifierInfo &II = S.PP.getIdentifierTable().get("omp_alloctrait_t");
  ParsedType PT = S.getTypeName(II, Loc, S.getCurScope());
  if (!PT.getAsOpaquePtr() || PT.get().isNull()) {
    S.Diag(Loc, diag::err_omp_implied_type_not_found) << "omp_alloctrait_t";
    return false;
  }
  Stack->setOMPAlloctraitT(PT.get());
  return true;
}

OMPClause *SemaOpenMP::ActOnOpenMPUsesAllocatorClause(
    SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc,
    ArrayRef<UsesAllocatorsData> Data) {
  ASTContext &Context = getASTContext();
  // OpenMP [2.12.5, target Construct]
  // allocator is an identifier of omp_allocator_handle_t type.
  if (!findOMPAllocatorHandleT(SemaRef, StartLoc, DSAStack))
    return nullptr;
  // OpenMP [2.12.5, target Construct]
  // allocator-traits-array is an identifier of const omp_alloctrait_t * type.
  if (llvm::any_of(
          Data,
          [](const UsesAllocatorsData &D) { return D.AllocatorTraits; }) &&
      !findOMPAlloctraitT(SemaRef, StartLoc, DSAStack))
    return nullptr;
  llvm::SmallPtrSet<CanonicalDeclPtr<Decl>, 4> PredefinedAllocators;
  for (int I = 0; I < OMPAllocateDeclAttr::OMPUserDefinedMemAlloc; ++I) {
    auto AllocatorKind = static_cast<OMPAllocateDeclAttr::AllocatorTypeTy>(I);
    StringRef Allocator =
        OMPAllocateDeclAttr::ConvertAllocatorTypeTyToStr(AllocatorKind);
    DeclarationName AllocatorName = &Context.Idents.get(Allocator);
    PredefinedAllocators.insert(SemaRef.LookupSingleName(
        SemaRef.TUScope, AllocatorName, StartLoc, Sema::LookupAnyName));
  }

  SmallVector<OMPUsesAllocatorsClause::Data, 4> NewData;
  for (const UsesAllocatorsData &D : Data) {
    Expr *AllocatorExpr = nullptr;
    // Check allocator expression.
    if (D.Allocator->isTypeDependent()) {
      AllocatorExpr = D.Allocator;
    } else {
      // Traits were specified - need to assign new allocator to the specified
      // allocator, so it must be an lvalue.
      AllocatorExpr = D.Allocator->IgnoreParenImpCasts();
      auto *DRE = dyn_cast<DeclRefExpr>(AllocatorExpr);
      bool IsPredefinedAllocator = false;
      if (DRE) {
        OMPAllocateDeclAttr::AllocatorTypeTy AllocatorTy =
            getAllocatorKind(SemaRef, DSAStack, AllocatorExpr);
        IsPredefinedAllocator =
            AllocatorTy !=
            OMPAllocateDeclAttr::AllocatorTypeTy::OMPUserDefinedMemAlloc;
      }
      QualType OMPAllocatorHandleT = DSAStack->getOMPAllocatorHandleT();
      QualType AllocatorExprType = AllocatorExpr->getType();
      bool IsTypeCompatible = IsPredefinedAllocator;
      IsTypeCompatible = IsTypeCompatible ||
                         Context.hasSameUnqualifiedType(AllocatorExprType,
                                                        OMPAllocatorHandleT);
      IsTypeCompatible =
          IsTypeCompatible ||
          Context.typesAreCompatible(AllocatorExprType, OMPAllocatorHandleT);
      bool IsNonConstantLValue =
          !AllocatorExprType.isConstant(Context) && AllocatorExpr->isLValue();
      if (!DRE || !IsTypeCompatible ||
          (!IsPredefinedAllocator && !IsNonConstantLValue)) {
        Diag(D.Allocator->getExprLoc(), diag::err_omp_var_expected)
            << "omp_allocator_handle_t" << (DRE ? 1 : 0)
            << AllocatorExpr->getType() << D.Allocator->getSourceRange();
        continue;
      }
      // OpenMP [2.12.5, target Construct]
      // Predefined allocators appearing in a uses_allocators clause cannot have
      // traits specified.
      if (IsPredefinedAllocator && D.AllocatorTraits) {
        Diag(D.AllocatorTraits->getExprLoc(),
             diag::err_omp_predefined_allocator_with_traits)
            << D.AllocatorTraits->getSourceRange();
        Diag(D.Allocator->getExprLoc(), diag::note_omp_predefined_allocator)
            << cast<NamedDecl>(DRE->getDecl())->getName()
            << D.Allocator->getSourceRange();
        continue;
      }
      // OpenMP [2.12.5, target Construct]
      // Non-predefined allocators appearing in a uses_allocators clause must
      // have traits specified.
      if (!IsPredefinedAllocator && !D.AllocatorTraits) {
        Diag(D.Allocator->getExprLoc(),
             diag::err_omp_nonpredefined_allocator_without_traits);
        continue;
      }
      // No allocator traits - just convert it to rvalue.
      if (!D.AllocatorTraits)
        AllocatorExpr = SemaRef.DefaultLvalueConversion(AllocatorExpr).get();
      DSAStack->addUsesAllocatorsDecl(
          DRE->getDecl(),
          IsPredefinedAllocator
              ? DSAStackTy::UsesAllocatorsDeclKind::PredefinedAllocator
              : DSAStackTy::UsesAllocatorsDeclKind::UserDefinedAllocator);
    }
    Expr *AllocatorTraitsExpr = nullptr;
    if (D.AllocatorTraits) {
      if (D.AllocatorTraits->isTypeDependent()) {
        AllocatorTraitsExpr = D.AllocatorTraits;
      } else {
        // OpenMP [2.12.5, target Construct]
        // Arrays that contain allocator traits that appear in a uses_allocators
        // clause must be constant arrays, have constant values and be defined
        // in the same scope as the construct in which the clause appears.
        AllocatorTraitsExpr = D.AllocatorTraits->IgnoreParenImpCasts();
        // Check that traits expr is a constant array.
        QualType TraitTy;
        if (const ArrayType *Ty =
                AllocatorTraitsExpr->getType()->getAsArrayTypeUnsafe())
          if (const auto *ConstArrayTy = dyn_cast<ConstantArrayType>(Ty))
            TraitTy = ConstArrayTy->getElementType();
        if (TraitTy.isNull() ||
            !(Context.hasSameUnqualifiedType(TraitTy,
                                             DSAStack->getOMPAlloctraitT()) ||
              Context.typesAreCompatible(TraitTy, DSAStack->getOMPAlloctraitT(),
                                         /*CompareUnqualified=*/true))) {
          Diag(D.AllocatorTraits->getExprLoc(),
               diag::err_omp_expected_array_alloctraits)
              << AllocatorTraitsExpr->getType();
          continue;
        }
        // Do not map by default allocator traits if it is a standalone
        // variable.
        if (auto *DRE = dyn_cast<DeclRefExpr>(AllocatorTraitsExpr))
          DSAStack->addUsesAllocatorsDecl(
              DRE->getDecl(),
              DSAStackTy::UsesAllocatorsDeclKind::AllocatorTrait);
      }
    }
    OMPUsesAllocatorsClause::Data &NewD = NewData.emplace_back();
    NewD.Allocator = AllocatorExpr;
    NewD.AllocatorTraits = AllocatorTraitsExpr;
    NewD.LParenLoc = D.LParenLoc;
    NewD.RParenLoc = D.RParenLoc;
  }
  return OMPUsesAllocatorsClause::Create(getASTContext(), StartLoc, LParenLoc,
                                         EndLoc, NewData);
}

OMPClause *SemaOpenMP::ActOnOpenMPAffinityClause(
    SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation ColonLoc,
    SourceLocation EndLoc, Expr *Modifier, ArrayRef<Expr *> Locators) {
  SmallVector<Expr *, 8> Vars;
  for (Expr *RefExpr : Locators) {
    assert(RefExpr && "NULL expr in OpenMP shared clause.");
    if (isa<DependentScopeDeclRefExpr>(RefExpr) || RefExpr->isTypeDependent()) {
      // It will be analyzed later.
      Vars.push_back(RefExpr);
      continue;
    }

    SourceLocation ELoc = RefExpr->getExprLoc();
    Expr *SimpleExpr = RefExpr->IgnoreParenImpCasts();

    if (!SimpleExpr->isLValue()) {
      Diag(ELoc, diag::err_omp_expected_addressable_lvalue_or_array_item)
          << 1 << 0 << RefExpr->getSourceRange();
      continue;
    }

    ExprResult Res;
    {
      Sema::TentativeAnalysisScope Trap(SemaRef);
      Res = SemaRef.CreateBuiltinUnaryOp(ELoc, UO_AddrOf, SimpleExpr);
    }
    if (!Res.isUsable() && !isa<ArraySectionExpr>(SimpleExpr) &&
        !isa<OMPArrayShapingExpr>(SimpleExpr)) {
      Diag(ELoc, diag::err_omp_expected_addressable_lvalue_or_array_item)
          << 1 << 0 << RefExpr->getSourceRange();
      continue;
    }
    Vars.push_back(SimpleExpr);
  }

  return OMPAffinityClause::Create(getASTContext(), StartLoc, LParenLoc,
                                   ColonLoc, EndLoc, Modifier, Vars);
}

OMPClause *SemaOpenMP::ActOnOpenMPBindClause(OpenMPBindClauseKind Kind,
                                             SourceLocation KindLoc,
                                             SourceLocation StartLoc,
                                             SourceLocation LParenLoc,
                                             SourceLocation EndLoc) {
  if (Kind == OMPC_BIND_unknown) {
    Diag(KindLoc, diag::err_omp_unexpected_clause_value)
        << getListOfPossibleValues(OMPC_bind, /*First=*/0,
                                   /*Last=*/unsigned(OMPC_BIND_unknown))
        << getOpenMPClauseName(OMPC_bind);
    return nullptr;
  }

  return OMPBindClause::Create(getASTContext(), Kind, KindLoc, StartLoc,
                               LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPXDynCGroupMemClause(Expr *Size,
                                                      SourceLocation StartLoc,
                                                      SourceLocation LParenLoc,
                                                      SourceLocation EndLoc) {
  Expr *ValExpr = Size;
  Stmt *HelperValStmt = nullptr;

  // OpenMP [2.5, Restrictions]
  //  The ompx_dyn_cgroup_mem expression must evaluate to a positive integer
  //  value.
  if (!isNonNegativeIntegerValue(ValExpr, SemaRef, OMPC_ompx_dyn_cgroup_mem,
                                 /*StrictlyPositive=*/false))
    return nullptr;

  OpenMPDirectiveKind DKind = DSAStack->getCurrentDirective();
  OpenMPDirectiveKind CaptureRegion = getOpenMPCaptureRegionForClause(
      DKind, OMPC_ompx_dyn_cgroup_mem, getLangOpts().OpenMP);
  if (CaptureRegion != OMPD_unknown &&
      !SemaRef.CurContext->isDependentContext()) {
    ValExpr = SemaRef.MakeFullExpr(ValExpr).get();
    llvm::MapVector<const Expr *, DeclRefExpr *> Captures;
    ValExpr = tryBuildCapture(SemaRef, ValExpr, Captures).get();
    HelperValStmt = buildPreInits(getASTContext(), Captures);
  }

  return new (getASTContext()) OMPXDynCGroupMemClause(
      ValExpr, HelperValStmt, CaptureRegion, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPDoacrossClause(
    OpenMPDoacrossClauseModifier DepType, SourceLocation DepLoc,
    SourceLocation ColonLoc, ArrayRef<Expr *> VarList, SourceLocation StartLoc,
    SourceLocation LParenLoc, SourceLocation EndLoc) {

  if (DSAStack->getCurrentDirective() == OMPD_ordered &&
      DepType != OMPC_DOACROSS_source && DepType != OMPC_DOACROSS_sink &&
      DepType != OMPC_DOACROSS_sink_omp_cur_iteration &&
      DepType != OMPC_DOACROSS_source_omp_cur_iteration &&
      DepType != OMPC_DOACROSS_source) {
    Diag(DepLoc, diag::err_omp_unexpected_clause_value)
        << "'source' or 'sink'" << getOpenMPClauseName(OMPC_doacross);
    return nullptr;
  }

  SmallVector<Expr *, 8> Vars;
  DSAStackTy::OperatorOffsetTy OpsOffs;
  llvm::APSInt TotalDepCount(/*BitWidth=*/32);
  DoacrossDataInfoTy VarOffset = ProcessOpenMPDoacrossClauseCommon(
      SemaRef,
      DepType == OMPC_DOACROSS_source ||
          DepType == OMPC_DOACROSS_source_omp_cur_iteration ||
          DepType == OMPC_DOACROSS_sink_omp_cur_iteration,
      VarList, DSAStack, EndLoc);
  Vars = VarOffset.Vars;
  OpsOffs = VarOffset.OpsOffs;
  TotalDepCount = VarOffset.TotalDepCount;
  auto *C = OMPDoacrossClause::Create(getASTContext(), StartLoc, LParenLoc,
                                      EndLoc, DepType, DepLoc, ColonLoc, Vars,
                                      TotalDepCount.getZExtValue());
  if (DSAStack->isParentOrderedRegion())
    DSAStack->addDoacrossDependClause(C, OpsOffs);
  return C;
}

OMPClause *SemaOpenMP::ActOnOpenMPXAttributeClause(ArrayRef<const Attr *> Attrs,
                                                   SourceLocation StartLoc,
                                                   SourceLocation LParenLoc,
                                                   SourceLocation EndLoc) {
  return new (getASTContext())
      OMPXAttributeClause(Attrs, StartLoc, LParenLoc, EndLoc);
}

OMPClause *SemaOpenMP::ActOnOpenMPXBareClause(SourceLocation StartLoc,
                                              SourceLocation EndLoc) {
  return new (getASTContext()) OMPXBareClause(StartLoc, EndLoc);
}

ExprResult SemaOpenMP::ActOnOMPArraySectionExpr(
    Expr *Base, SourceLocation LBLoc, Expr *LowerBound,
    SourceLocation ColonLocFirst, SourceLocation ColonLocSecond, Expr *Length,
    Expr *Stride, SourceLocation RBLoc) {
  ASTContext &Context = getASTContext();
  if (Base->hasPlaceholderType() &&
      !Base->hasPlaceholderType(BuiltinType::ArraySection)) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Base);
    if (Result.isInvalid())
      return ExprError();
    Base = Result.get();
  }
  if (LowerBound && LowerBound->getType()->isNonOverloadPlaceholderType()) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(LowerBound);
    if (Result.isInvalid())
      return ExprError();
    Result = SemaRef.DefaultLvalueConversion(Result.get());
    if (Result.isInvalid())
      return ExprError();
    LowerBound = Result.get();
  }
  if (Length && Length->getType()->isNonOverloadPlaceholderType()) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Length);
    if (Result.isInvalid())
      return ExprError();
    Result = SemaRef.DefaultLvalueConversion(Result.get());
    if (Result.isInvalid())
      return ExprError();
    Length = Result.get();
  }
  if (Stride && Stride->getType()->isNonOverloadPlaceholderType()) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Stride);
    if (Result.isInvalid())
      return ExprError();
    Result = SemaRef.DefaultLvalueConversion(Result.get());
    if (Result.isInvalid())
      return ExprError();
    Stride = Result.get();
  }

  // Build an unanalyzed expression if either operand is type-dependent.
  if (Base->isTypeDependent() ||
      (LowerBound &&
       (LowerBound->isTypeDependent() || LowerBound->isValueDependent())) ||
      (Length && (Length->isTypeDependent() || Length->isValueDependent())) ||
      (Stride && (Stride->isTypeDependent() || Stride->isValueDependent()))) {
    return new (Context) ArraySectionExpr(
        Base, LowerBound, Length, Stride, Context.DependentTy, VK_LValue,
        OK_Ordinary, ColonLocFirst, ColonLocSecond, RBLoc);
  }

  // Perform default conversions.
  QualType OriginalTy = ArraySectionExpr::getBaseOriginalType(Base);
  QualType ResultTy;
  if (OriginalTy->isAnyPointerType()) {
    ResultTy = OriginalTy->getPointeeType();
  } else if (OriginalTy->isArrayType()) {
    ResultTy = OriginalTy->getAsArrayTypeUnsafe()->getElementType();
  } else {
    return ExprError(
        Diag(Base->getExprLoc(), diag::err_omp_typecheck_section_value)
        << Base->getSourceRange());
  }
  // C99 6.5.2.1p1
  if (LowerBound) {
    auto Res = PerformOpenMPImplicitIntegerConversion(LowerBound->getExprLoc(),
                                                      LowerBound);
    if (Res.isInvalid())
      return ExprError(Diag(LowerBound->getExprLoc(),
                            diag::err_omp_typecheck_section_not_integer)
                       << 0 << LowerBound->getSourceRange());
    LowerBound = Res.get();

    if (LowerBound->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
        LowerBound->getType()->isSpecificBuiltinType(BuiltinType::Char_U))
      Diag(LowerBound->getExprLoc(), diag::warn_omp_section_is_char)
          << 0 << LowerBound->getSourceRange();
  }
  if (Length) {
    auto Res =
        PerformOpenMPImplicitIntegerConversion(Length->getExprLoc(), Length);
    if (Res.isInvalid())
      return ExprError(Diag(Length->getExprLoc(),
                            diag::err_omp_typecheck_section_not_integer)
                       << 1 << Length->getSourceRange());
    Length = Res.get();

    if (Length->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
        Length->getType()->isSpecificBuiltinType(BuiltinType::Char_U))
      Diag(Length->getExprLoc(), diag::warn_omp_section_is_char)
          << 1 << Length->getSourceRange();
  }
  if (Stride) {
    ExprResult Res =
        PerformOpenMPImplicitIntegerConversion(Stride->getExprLoc(), Stride);
    if (Res.isInvalid())
      return ExprError(Diag(Stride->getExprLoc(),
                            diag::err_omp_typecheck_section_not_integer)
                       << 1 << Stride->getSourceRange());
    Stride = Res.get();

    if (Stride->getType()->isSpecificBuiltinType(BuiltinType::Char_S) ||
        Stride->getType()->isSpecificBuiltinType(BuiltinType::Char_U))
      Diag(Stride->getExprLoc(), diag::warn_omp_section_is_char)
          << 1 << Stride->getSourceRange();
  }

  // C99 6.5.2.1p1: "shall have type "pointer to *object* type". Similarly,
  // C++ [expr.sub]p1: The type "T" shall be a completely-defined object
  // type. Note that functions are not objects, and that (in C99 parlance)
  // incomplete types are not object types.
  if (ResultTy->isFunctionType()) {
    Diag(Base->getExprLoc(), diag::err_omp_section_function_type)
        << ResultTy << Base->getSourceRange();
    return ExprError();
  }

  if (SemaRef.RequireCompleteType(Base->getExprLoc(), ResultTy,
                                  diag::err_omp_section_incomplete_type, Base))
    return ExprError();

  if (LowerBound && !OriginalTy->isAnyPointerType()) {
    Expr::EvalResult Result;
    if (LowerBound->EvaluateAsInt(Result, Context)) {
      // OpenMP 5.0, [2.1.5 Array Sections]
      // The array section must be a subset of the original array.
      llvm::APSInt LowerBoundValue = Result.Val.getInt();
      if (LowerBoundValue.isNegative()) {
        Diag(LowerBound->getExprLoc(),
             diag::err_omp_section_not_subset_of_array)
            << LowerBound->getSourceRange();
        return ExprError();
      }
    }
  }

  if (Length) {
    Expr::EvalResult Result;
    if (Length->EvaluateAsInt(Result, Context)) {
      // OpenMP 5.0, [2.1.5 Array Sections]
      // The length must evaluate to non-negative integers.
      llvm::APSInt LengthValue = Result.Val.getInt();
      if (LengthValue.isNegative()) {
        Diag(Length->getExprLoc(), diag::err_omp_section_length_negative)
            << toString(LengthValue, /*Radix=*/10, /*Signed=*/true)
            << Length->getSourceRange();
        return ExprError();
      }
    }
  } else if (ColonLocFirst.isValid() &&
             (OriginalTy.isNull() || (!OriginalTy->isConstantArrayType() &&
                                      !OriginalTy->isVariableArrayType()))) {
    // OpenMP 5.0, [2.1.5 Array Sections]
    // When the size of the array dimension is not known, the length must be
    // specified explicitly.
    Diag(ColonLocFirst, diag::err_omp_section_length_undefined)
        << (!OriginalTy.isNull() && OriginalTy->isArrayType());
    return ExprError();
  }

  if (Stride) {
    Expr::EvalResult Result;
    if (Stride->EvaluateAsInt(Result, Context)) {
      // OpenMP 5.0, [2.1.5 Array Sections]
      // The stride must evaluate to a positive integer.
      llvm::APSInt StrideValue = Result.Val.getInt();
      if (!StrideValue.isStrictlyPositive()) {
        Diag(Stride->getExprLoc(), diag::err_omp_section_stride_non_positive)
            << toString(StrideValue, /*Radix=*/10, /*Signed=*/true)
            << Stride->getSourceRange();
        return ExprError();
      }
    }
  }

  if (!Base->hasPlaceholderType(BuiltinType::ArraySection)) {
    ExprResult Result = SemaRef.DefaultFunctionArrayLvalueConversion(Base);
    if (Result.isInvalid())
      return ExprError();
    Base = Result.get();
  }
  return new (Context) ArraySectionExpr(
      Base, LowerBound, Length, Stride, Context.ArraySectionTy, VK_LValue,
      OK_Ordinary, ColonLocFirst, ColonLocSecond, RBLoc);
}

ExprResult SemaOpenMP::ActOnOMPArrayShapingExpr(
    Expr *Base, SourceLocation LParenLoc, SourceLocation RParenLoc,
    ArrayRef<Expr *> Dims, ArrayRef<SourceRange> Brackets) {
  ASTContext &Context = getASTContext();
  if (Base->hasPlaceholderType()) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Base);
    if (Result.isInvalid())
      return ExprError();
    Result = SemaRef.DefaultLvalueConversion(Result.get());
    if (Result.isInvalid())
      return ExprError();
    Base = Result.get();
  }
  QualType BaseTy = Base->getType();
  // Delay analysis of the types/expressions if instantiation/specialization is
  // required.
  if (!BaseTy->isPointerType() && Base->isTypeDependent())
    return OMPArrayShapingExpr::Create(Context, Context.DependentTy, Base,
                                       LParenLoc, RParenLoc, Dims, Brackets);
  if (!BaseTy->isPointerType() ||
      (!Base->isTypeDependent() &&
       BaseTy->getPointeeType()->isIncompleteType()))
    return ExprError(Diag(Base->getExprLoc(),
                          diag::err_omp_non_pointer_type_array_shaping_base)
                     << Base->getSourceRange());

  SmallVector<Expr *, 4> NewDims;
  bool ErrorFound = false;
  for (Expr *Dim : Dims) {
    if (Dim->hasPlaceholderType()) {
      ExprResult Result = SemaRef.CheckPlaceholderExpr(Dim);
      if (Result.isInvalid()) {
        ErrorFound = true;
        continue;
      }
      Result = SemaRef.DefaultLvalueConversion(Result.get());
      if (Result.isInvalid()) {
        ErrorFound = true;
        continue;
      }
      Dim = Result.get();
    }
    if (!Dim->isTypeDependent()) {
      ExprResult Result =
          PerformOpenMPImplicitIntegerConversion(Dim->getExprLoc(), Dim);
      if (Result.isInvalid()) {
        ErrorFound = true;
        Diag(Dim->getExprLoc(), diag::err_omp_typecheck_shaping_not_integer)
            << Dim->getSourceRange();
        continue;
      }
      Dim = Result.get();
      Expr::EvalResult EvResult;
      if (!Dim->isValueDependent() && Dim->EvaluateAsInt(EvResult, Context)) {
        // OpenMP 5.0, [2.1.4 Array Shaping]
        // Each si is an integral type expression that must evaluate to a
        // positive integer.
        llvm::APSInt Value = EvResult.Val.getInt();
        if (!Value.isStrictlyPositive()) {
          Diag(Dim->getExprLoc(), diag::err_omp_shaping_dimension_not_positive)
              << toString(Value, /*Radix=*/10, /*Signed=*/true)
              << Dim->getSourceRange();
          ErrorFound = true;
          continue;
        }
      }
    }
    NewDims.push_back(Dim);
  }
  if (ErrorFound)
    return ExprError();
  return OMPArrayShapingExpr::Create(Context, Context.OMPArrayShapingTy, Base,
                                     LParenLoc, RParenLoc, NewDims, Brackets);
}

ExprResult SemaOpenMP::ActOnOMPIteratorExpr(Scope *S,
                                            SourceLocation IteratorKwLoc,
                                            SourceLocation LLoc,
                                            SourceLocation RLoc,
                                            ArrayRef<OMPIteratorData> Data) {
  ASTContext &Context = getASTContext();
  SmallVector<OMPIteratorExpr::IteratorDefinition, 4> ID;
  bool IsCorrect = true;
  for (const OMPIteratorData &D : Data) {
    TypeSourceInfo *TInfo = nullptr;
    SourceLocation StartLoc;
    QualType DeclTy;
    if (!D.Type.getAsOpaquePtr()) {
      // OpenMP 5.0, 2.1.6 Iterators
      // In an iterator-specifier, if the iterator-type is not specified then
      // the type of that iterator is of int type.
      DeclTy = Context.IntTy;
      StartLoc = D.DeclIdentLoc;
    } else {
      DeclTy = Sema::GetTypeFromParser(D.Type, &TInfo);
      StartLoc = TInfo->getTypeLoc().getBeginLoc();
    }

    bool IsDeclTyDependent = DeclTy->isDependentType() ||
                             DeclTy->containsUnexpandedParameterPack() ||
                             DeclTy->isInstantiationDependentType();
    if (!IsDeclTyDependent) {
      if (!DeclTy->isIntegralType(Context) && !DeclTy->isAnyPointerType()) {
        // OpenMP 5.0, 2.1.6 Iterators, Restrictions, C/C++
        // The iterator-type must be an integral or pointer type.
        Diag(StartLoc, diag::err_omp_iterator_not_integral_or_pointer)
            << DeclTy;
        IsCorrect = false;
        continue;
      }
      if (DeclTy.isConstant(Context)) {
        // OpenMP 5.0, 2.1.6 Iterators, Restrictions, C/C++
        // The iterator-type must not be const qualified.
        Diag(StartLoc, diag::err_omp_iterator_not_integral_or_pointer)
            << DeclTy;
        IsCorrect = false;
        continue;
      }
    }

    // Iterator declaration.
    assert(D.DeclIdent && "Identifier expected.");
    // Always try to create iterator declarator to avoid extra error messages
    // about unknown declarations use.
    auto *VD =
        VarDecl::Create(Context, SemaRef.CurContext, StartLoc, D.DeclIdentLoc,
                        D.DeclIdent, DeclTy, TInfo, SC_None);
    VD->setImplicit();
    if (S) {
      // Check for conflicting previous declaration.
      DeclarationNameInfo NameInfo(VD->getDeclName(), D.DeclIdentLoc);
      LookupResult Previous(SemaRef, NameInfo, Sema::LookupOrdinaryName,
                            RedeclarationKind::ForVisibleRedeclaration);
      Previous.suppressDiagnostics();
      SemaRef.LookupName(Previous, S);

      SemaRef.FilterLookupForScope(Previous, SemaRef.CurContext, S,
                                   /*ConsiderLinkage=*/false,
                                   /*AllowInlineNamespace=*/false);
      if (!Previous.empty()) {
        NamedDecl *Old = Previous.getRepresentativeDecl();
        Diag(D.DeclIdentLoc, diag::err_redefinition) << VD->getDeclName();
        Diag(Old->getLocation(), diag::note_previous_definition);
      } else {
        SemaRef.PushOnScopeChains(VD, S);
      }
    } else {
      SemaRef.CurContext->addDecl(VD);
    }

    /// Act on the iterator variable declaration.
    ActOnOpenMPIteratorVarDecl(VD);

    Expr *Begin = D.Range.Begin;
    if (!IsDeclTyDependent && Begin && !Begin->isTypeDependent()) {
      ExprResult BeginRes =
          SemaRef.PerformImplicitConversion(Begin, DeclTy, Sema::AA_Converting);
      Begin = BeginRes.get();
    }
    Expr *End = D.Range.End;
    if (!IsDeclTyDependent && End && !End->isTypeDependent()) {
      ExprResult EndRes =
          SemaRef.PerformImplicitConversion(End, DeclTy, Sema::AA_Converting);
      End = EndRes.get();
    }
    Expr *Step = D.Range.Step;
    if (!IsDeclTyDependent && Step && !Step->isTypeDependent()) {
      if (!Step->getType()->isIntegralType(Context)) {
        Diag(Step->getExprLoc(), diag::err_omp_iterator_step_not_integral)
            << Step << Step->getSourceRange();
        IsCorrect = false;
        continue;
      }
      std::optional<llvm::APSInt> Result =
          Step->getIntegerConstantExpr(Context);
      // OpenMP 5.0, 2.1.6 Iterators, Restrictions
      // If the step expression of a range-specification equals zero, the
      // behavior is unspecified.
      if (Result && Result->isZero()) {
        Diag(Step->getExprLoc(), diag::err_omp_iterator_step_constant_zero)
            << Step << Step->getSourceRange();
        IsCorrect = false;
        continue;
      }
    }
    if (!Begin || !End || !IsCorrect) {
      IsCorrect = false;
      continue;
    }
    OMPIteratorExpr::IteratorDefinition &IDElem = ID.emplace_back();
    IDElem.IteratorDecl = VD;
    IDElem.AssignmentLoc = D.AssignLoc;
    IDElem.Range.Begin = Begin;
    IDElem.Range.End = End;
    IDElem.Range.Step = Step;
    IDElem.ColonLoc = D.ColonLoc;
    IDElem.SecondColonLoc = D.SecColonLoc;
  }
  if (!IsCorrect) {
    // Invalidate all created iterator declarations if error is found.
    for (const OMPIteratorExpr::IteratorDefinition &D : ID) {
      if (Decl *ID = D.IteratorDecl)
        ID->setInvalidDecl();
    }
    return ExprError();
  }
  SmallVector<OMPIteratorHelperData, 4> Helpers;
  if (!SemaRef.CurContext->isDependentContext()) {
    // Build number of ityeration for each iteration range.
    // Ni = ((Stepi > 0) ? ((Endi + Stepi -1 - Begini)/Stepi) :
    // ((Begini-Stepi-1-Endi) / -Stepi);
    for (OMPIteratorExpr::IteratorDefinition &D : ID) {
      // (Endi - Begini)
      ExprResult Res = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Sub,
                                                  D.Range.End, D.Range.Begin);
      if (!Res.isUsable()) {
        IsCorrect = false;
        continue;
      }
      ExprResult St, St1;
      if (D.Range.Step) {
        St = D.Range.Step;
        // (Endi - Begini) + Stepi
        Res = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Add, Res.get(),
                                         St.get());
        if (!Res.isUsable()) {
          IsCorrect = false;
          continue;
        }
        // (Endi - Begini) + Stepi - 1
        Res = SemaRef.CreateBuiltinBinOp(
            D.AssignmentLoc, BO_Sub, Res.get(),
            SemaRef.ActOnIntegerConstant(D.AssignmentLoc, 1).get());
        if (!Res.isUsable()) {
          IsCorrect = false;
          continue;
        }
        // ((Endi - Begini) + Stepi - 1) / Stepi
        Res = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Div, Res.get(),
                                         St.get());
        if (!Res.isUsable()) {
          IsCorrect = false;
          continue;
        }
        St1 = SemaRef.CreateBuiltinUnaryOp(D.AssignmentLoc, UO_Minus,
                                           D.Range.Step);
        // (Begini - Endi)
        ExprResult Res1 = SemaRef.CreateBuiltinBinOp(
            D.AssignmentLoc, BO_Sub, D.Range.Begin, D.Range.End);
        if (!Res1.isUsable()) {
          IsCorrect = false;
          continue;
        }
        // (Begini - Endi) - Stepi
        Res1 = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Add, Res1.get(),
                                          St1.get());
        if (!Res1.isUsable()) {
          IsCorrect = false;
          continue;
        }
        // (Begini - Endi) - Stepi - 1
        Res1 = SemaRef.CreateBuiltinBinOp(
            D.AssignmentLoc, BO_Sub, Res1.get(),
            SemaRef.ActOnIntegerConstant(D.AssignmentLoc, 1).get());
        if (!Res1.isUsable()) {
          IsCorrect = false;
          continue;
        }
        // ((Begini - Endi) - Stepi - 1) / (-Stepi)
        Res1 = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Div, Res1.get(),
                                          St1.get());
        if (!Res1.isUsable()) {
          IsCorrect = false;
          continue;
        }
        // Stepi > 0.
        ExprResult CmpRes = SemaRef.CreateBuiltinBinOp(
            D.AssignmentLoc, BO_GT, D.Range.Step,
            SemaRef.ActOnIntegerConstant(D.AssignmentLoc, 0).get());
        if (!CmpRes.isUsable()) {
          IsCorrect = false;
          continue;
        }
        Res = SemaRef.ActOnConditionalOp(D.AssignmentLoc, D.AssignmentLoc,
                                         CmpRes.get(), Res.get(), Res1.get());
        if (!Res.isUsable()) {
          IsCorrect = false;
          continue;
        }
      }
      Res = SemaRef.ActOnFinishFullExpr(Res.get(), /*DiscardedValue=*/false);
      if (!Res.isUsable()) {
        IsCorrect = false;
        continue;
      }

      // Build counter update.
      // Build counter.
      auto *CounterVD = VarDecl::Create(Context, SemaRef.CurContext,
                                        D.IteratorDecl->getBeginLoc(),
                                        D.IteratorDecl->getBeginLoc(), nullptr,
                                        Res.get()->getType(), nullptr, SC_None);
      CounterVD->setImplicit();
      ExprResult RefRes =
          SemaRef.BuildDeclRefExpr(CounterVD, CounterVD->getType(), VK_LValue,
                                   D.IteratorDecl->getBeginLoc());
      // Build counter update.
      // I = Begini + counter * Stepi;
      ExprResult UpdateRes;
      if (D.Range.Step) {
        UpdateRes = SemaRef.CreateBuiltinBinOp(
            D.AssignmentLoc, BO_Mul,
            SemaRef.DefaultLvalueConversion(RefRes.get()).get(), St.get());
      } else {
        UpdateRes = SemaRef.DefaultLvalueConversion(RefRes.get());
      }
      if (!UpdateRes.isUsable()) {
        IsCorrect = false;
        continue;
      }
      UpdateRes = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Add,
                                             D.Range.Begin, UpdateRes.get());
      if (!UpdateRes.isUsable()) {
        IsCorrect = false;
        continue;
      }
      ExprResult VDRes =
          SemaRef.BuildDeclRefExpr(cast<VarDecl>(D.IteratorDecl),
                                   cast<VarDecl>(D.IteratorDecl)->getType(),
                                   VK_LValue, D.IteratorDecl->getBeginLoc());
      UpdateRes = SemaRef.CreateBuiltinBinOp(D.AssignmentLoc, BO_Assign,
                                             VDRes.get(), UpdateRes.get());
      if (!UpdateRes.isUsable()) {
        IsCorrect = false;
        continue;
      }
      UpdateRes =
          SemaRef.ActOnFinishFullExpr(UpdateRes.get(), /*DiscardedValue=*/true);
      if (!UpdateRes.isUsable()) {
        IsCorrect = false;
        continue;
      }
      ExprResult CounterUpdateRes = SemaRef.CreateBuiltinUnaryOp(
          D.AssignmentLoc, UO_PreInc, RefRes.get());
      if (!CounterUpdateRes.isUsable()) {
        IsCorrect = false;
        continue;
      }
      CounterUpdateRes = SemaRef.ActOnFinishFullExpr(CounterUpdateRes.get(),
                                                     /*DiscardedValue=*/true);
      if (!CounterUpdateRes.isUsable()) {
        IsCorrect = false;
        continue;
      }
      OMPIteratorHelperData &HD = Helpers.emplace_back();
      HD.CounterVD = CounterVD;
      HD.Upper = Res.get();
      HD.Update = UpdateRes.get();
      HD.CounterUpdate = CounterUpdateRes.get();
    }
  } else {
    Helpers.assign(ID.size(), {});
  }
  if (!IsCorrect) {
    // Invalidate all created iterator declarations if error is found.
    for (const OMPIteratorExpr::IteratorDefinition &D : ID) {
      if (Decl *ID = D.IteratorDecl)
        ID->setInvalidDecl();
    }
    return ExprError();
  }
  return OMPIteratorExpr::Create(Context, Context.OMPIteratorTy, IteratorKwLoc,
                                 LLoc, RLoc, ID, Helpers);
}

/// Check if \p AssumptionStr is a known assumption and warn if not.
static void checkOMPAssumeAttr(Sema &S, SourceLocation Loc,
                               StringRef AssumptionStr) {
  if (llvm::KnownAssumptionStrings.count(AssumptionStr))
    return;

  unsigned BestEditDistance = 3;
  StringRef Suggestion;
  for (const auto &KnownAssumptionIt : llvm::KnownAssumptionStrings) {
    unsigned EditDistance =
        AssumptionStr.edit_distance(KnownAssumptionIt.getKey());
    if (EditDistance < BestEditDistance) {
      Suggestion = KnownAssumptionIt.getKey();
      BestEditDistance = EditDistance;
    }
  }

  if (!Suggestion.empty())
    S.Diag(Loc, diag::warn_omp_assume_attribute_string_unknown_suggested)
        << AssumptionStr << Suggestion;
  else
    S.Diag(Loc, diag::warn_omp_assume_attribute_string_unknown)
        << AssumptionStr;
}

void SemaOpenMP::handleOMPAssumeAttr(Decl *D, const ParsedAttr &AL) {
  // Handle the case where the attribute has a text message.
  StringRef Str;
  SourceLocation AttrStrLoc;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &AttrStrLoc))
    return;

  checkOMPAssumeAttr(SemaRef, AttrStrLoc, Str);

  D->addAttr(::new (getASTContext()) OMPAssumeAttr(getASTContext(), AL, Str));
}

SemaOpenMP::SemaOpenMP(Sema &S)
    : SemaBase(S), VarDataSharingAttributesStack(nullptr) {}
