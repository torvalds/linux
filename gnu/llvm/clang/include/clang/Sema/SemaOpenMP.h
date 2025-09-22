//===----- SemaOpenMP.h -- Semantic Analysis for OpenMP constructs -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for OpenMP constructs and
/// clauses.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAOPENMP_H
#define LLVM_CLANG_SEMA_SEMAOPENMP_H

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include <optional>
#include <string>
#include <utility>

namespace clang {
class ParsedAttr;

class SemaOpenMP : public SemaBase {
public:
  SemaOpenMP(Sema &S);

  friend class Parser;
  friend class Sema;

  using DeclGroupPtrTy = OpaquePtr<DeclGroupRef>;
  using CapturedParamNameType = std::pair<StringRef, QualType>;

  /// Creates a SemaDiagnosticBuilder that emits the diagnostic if the current
  /// context is "used as device code".
  ///
  /// - If CurContext is a `declare target` function or it is known that the
  /// function is emitted for the device, emits the diagnostics immediately.
  /// - If CurContext is a non-`declare target` function and we are compiling
  ///   for the device, creates a diagnostic which is emitted if and when we
  ///   realize that the function will be codegen'ed.
  ///
  /// Example usage:
  ///
  ///  // Variable-length arrays are not allowed in NVPTX device code.
  ///  if (diagIfOpenMPDeviceCode(Loc, diag::err_vla_unsupported))
  ///    return ExprError();
  ///  // Otherwise, continue parsing as normal.
  SemaDiagnosticBuilder diagIfOpenMPDeviceCode(SourceLocation Loc,
                                               unsigned DiagID,
                                               const FunctionDecl *FD);

  /// Creates a SemaDiagnosticBuilder that emits the diagnostic if the current
  /// context is "used as host code".
  ///
  /// - If CurContext is a `declare target` function or it is known that the
  /// function is emitted for the host, emits the diagnostics immediately.
  /// - If CurContext is a non-host function, just ignore it.
  ///
  /// Example usage:
  ///
  ///  // Variable-length arrays are not allowed in NVPTX device code.
  ///  if (diagIfOpenMPHostode(Loc, diag::err_vla_unsupported))
  ///    return ExprError();
  ///  // Otherwise, continue parsing as normal.
  SemaDiagnosticBuilder diagIfOpenMPHostCode(SourceLocation Loc,
                                             unsigned DiagID,
                                             const FunctionDecl *FD);

  /// The declarator \p D defines a function in the scope \p S which is nested
  /// in an `omp begin/end declare variant` scope. In this method we create a
  /// declaration for \p D and rename \p D according to the OpenMP context
  /// selector of the surrounding scope. Return all base functions in \p Bases.
  void ActOnStartOfFunctionDefinitionInOpenMPDeclareVariantScope(
      Scope *S, Declarator &D, MultiTemplateParamsArg TemplateParameterLists,
      SmallVectorImpl<FunctionDecl *> &Bases);

  /// Register \p D as specialization of all base functions in \p Bases in the
  /// current `omp begin/end declare variant` scope.
  void ActOnFinishedFunctionDefinitionInOpenMPDeclareVariantScope(
      Decl *D, SmallVectorImpl<FunctionDecl *> &Bases);

  /// Act on \p D, a function definition inside of an `omp [begin/end] assumes`.
  void ActOnFinishedFunctionDefinitionInOpenMPAssumeScope(Decl *D);

  /// Can we exit an OpenMP declare variant scope at the moment.
  bool isInOpenMPDeclareVariantScope() const {
    return !OMPDeclareVariantScopes.empty();
  }

  ExprResult
  VerifyPositiveIntegerConstantInClause(Expr *Op, OpenMPClauseKind CKind,
                                        bool StrictlyPositive = true,
                                        bool SuppressExprDiags = false);

  /// Given the potential call expression \p Call, determine if there is a
  /// specialization via the OpenMP declare variant mechanism available. If
  /// there is, return the specialized call expression, otherwise return the
  /// original \p Call.
  ExprResult ActOnOpenMPCall(ExprResult Call, Scope *Scope,
                             SourceLocation LParenLoc, MultiExprArg ArgExprs,
                             SourceLocation RParenLoc, Expr *ExecConfig);

  /// Handle a `omp begin declare variant`.
  void ActOnOpenMPBeginDeclareVariant(SourceLocation Loc, OMPTraitInfo &TI);

  /// Handle a `omp end declare variant`.
  void ActOnOpenMPEndDeclareVariant();

  /// Function tries to capture lambda's captured variables in the OpenMP region
  /// before the original lambda is captured.
  void tryCaptureOpenMPLambdas(ValueDecl *V);

  /// Return true if the provided declaration \a VD should be captured by
  /// reference.
  /// \param Level Relative level of nested OpenMP construct for that the check
  /// is performed.
  /// \param OpenMPCaptureLevel Capture level within an OpenMP construct.
  bool isOpenMPCapturedByRef(const ValueDecl *D, unsigned Level,
                             unsigned OpenMPCaptureLevel) const;

  /// Check if the specified variable is used in one of the private
  /// clauses (private, firstprivate, lastprivate, reduction etc.) in OpenMP
  /// constructs.
  VarDecl *isOpenMPCapturedDecl(ValueDecl *D, bool CheckScopeInfo = false,
                                unsigned StopAt = 0);

  /// The member expression(this->fd) needs to be rebuilt in the template
  /// instantiation to generate private copy for OpenMP when default
  /// clause is used. The function will return true if default
  /// cluse is used.
  bool isOpenMPRebuildMemberExpr(ValueDecl *D);

  ExprResult getOpenMPCapturedExpr(VarDecl *Capture, ExprValueKind VK,
                                   ExprObjectKind OK, SourceLocation Loc);

  /// If the current region is a loop-based region, mark the start of the loop
  /// construct.
  void startOpenMPLoop();

  /// If the current region is a range loop-based region, mark the start of the
  /// loop construct.
  void startOpenMPCXXRangeFor();

  /// Check if the specified variable is used in 'private' clause.
  /// \param Level Relative level of nested OpenMP construct for that the check
  /// is performed.
  OpenMPClauseKind isOpenMPPrivateDecl(ValueDecl *D, unsigned Level,
                                       unsigned CapLevel) const;

  /// Sets OpenMP capture kind (OMPC_private, OMPC_firstprivate, OMPC_map etc.)
  /// for \p FD based on DSA for the provided corresponding captured declaration
  /// \p D.
  void setOpenMPCaptureKind(FieldDecl *FD, const ValueDecl *D, unsigned Level);

  /// Check if the specified variable is captured  by 'target' directive.
  /// \param Level Relative level of nested OpenMP construct for that the check
  /// is performed.
  bool isOpenMPTargetCapturedDecl(const ValueDecl *D, unsigned Level,
                                  unsigned CaptureLevel) const;

  /// Check if the specified global variable must be captured  by outer capture
  /// regions.
  /// \param Level Relative level of nested OpenMP construct for that
  /// the check is performed.
  bool isOpenMPGlobalCapturedDecl(ValueDecl *D, unsigned Level,
                                  unsigned CaptureLevel) const;

  ExprResult PerformOpenMPImplicitIntegerConversion(SourceLocation OpLoc,
                                                    Expr *Op);
  /// Called on start of new data sharing attribute block.
  void StartOpenMPDSABlock(OpenMPDirectiveKind K,
                           const DeclarationNameInfo &DirName, Scope *CurScope,
                           SourceLocation Loc);
  /// Start analysis of clauses.
  void StartOpenMPClause(OpenMPClauseKind K);
  /// End analysis of clauses.
  void EndOpenMPClause();
  /// Called on end of data sharing attribute block.
  void EndOpenMPDSABlock(Stmt *CurDirective);

  /// Check if the current region is an OpenMP loop region and if it is,
  /// mark loop control variable, used in \p Init for loop initialization, as
  /// private by default.
  /// \param Init First part of the for loop.
  void ActOnOpenMPLoopInitialization(SourceLocation ForLoc, Stmt *Init);

  /// Called on well-formed '\#pragma omp metadirective' after parsing
  /// of the  associated statement.
  StmtResult ActOnOpenMPMetaDirective(ArrayRef<OMPClause *> Clauses,
                                      Stmt *AStmt, SourceLocation StartLoc,
                                      SourceLocation EndLoc);

  // OpenMP directives and clauses.
  /// Called on correct id-expression from the '#pragma omp
  /// threadprivate'.
  ExprResult ActOnOpenMPIdExpression(Scope *CurScope, CXXScopeSpec &ScopeSpec,
                                     const DeclarationNameInfo &Id,
                                     OpenMPDirectiveKind Kind);
  /// Called on well-formed '#pragma omp threadprivate'.
  DeclGroupPtrTy ActOnOpenMPThreadprivateDirective(SourceLocation Loc,
                                                   ArrayRef<Expr *> VarList);
  /// Builds a new OpenMPThreadPrivateDecl and checks its correctness.
  OMPThreadPrivateDecl *CheckOMPThreadPrivateDecl(SourceLocation Loc,
                                                  ArrayRef<Expr *> VarList);
  /// Called on well-formed '#pragma omp allocate'.
  DeclGroupPtrTy ActOnOpenMPAllocateDirective(SourceLocation Loc,
                                              ArrayRef<Expr *> VarList,
                                              ArrayRef<OMPClause *> Clauses,
                                              DeclContext *Owner = nullptr);

  /// Called on well-formed '#pragma omp [begin] assume[s]'.
  void ActOnOpenMPAssumesDirective(SourceLocation Loc,
                                   OpenMPDirectiveKind DKind,
                                   ArrayRef<std::string> Assumptions,
                                   bool SkippedClauses);

  /// Check if there is an active global `omp begin assumes` directive.
  bool isInOpenMPAssumeScope() const { return !OMPAssumeScoped.empty(); }

  /// Check if there is an active global `omp assumes` directive.
  bool hasGlobalOpenMPAssumes() const { return !OMPAssumeGlobal.empty(); }

  /// Called on well-formed '#pragma omp end assumes'.
  void ActOnOpenMPEndAssumesDirective();

  /// Called on well-formed '#pragma omp requires'.
  DeclGroupPtrTy ActOnOpenMPRequiresDirective(SourceLocation Loc,
                                              ArrayRef<OMPClause *> ClauseList);
  /// Check restrictions on Requires directive
  OMPRequiresDecl *CheckOMPRequiresDecl(SourceLocation Loc,
                                        ArrayRef<OMPClause *> Clauses);
  /// Check if the specified type is allowed to be used in 'omp declare
  /// reduction' construct.
  QualType ActOnOpenMPDeclareReductionType(SourceLocation TyLoc,
                                           TypeResult ParsedType);
  /// Called on start of '#pragma omp declare reduction'.
  DeclGroupPtrTy ActOnOpenMPDeclareReductionDirectiveStart(
      Scope *S, DeclContext *DC, DeclarationName Name,
      ArrayRef<std::pair<QualType, SourceLocation>> ReductionTypes,
      AccessSpecifier AS, Decl *PrevDeclInScope = nullptr);
  /// Initialize declare reduction construct initializer.
  void ActOnOpenMPDeclareReductionCombinerStart(Scope *S, Decl *D);
  /// Finish current declare reduction construct initializer.
  void ActOnOpenMPDeclareReductionCombinerEnd(Decl *D, Expr *Combiner);
  /// Initialize declare reduction construct initializer.
  /// \return omp_priv variable.
  VarDecl *ActOnOpenMPDeclareReductionInitializerStart(Scope *S, Decl *D);
  /// Finish current declare reduction construct initializer.
  void ActOnOpenMPDeclareReductionInitializerEnd(Decl *D, Expr *Initializer,
                                                 VarDecl *OmpPrivParm);
  /// Called at the end of '#pragma omp declare reduction'.
  DeclGroupPtrTy ActOnOpenMPDeclareReductionDirectiveEnd(
      Scope *S, DeclGroupPtrTy DeclReductions, bool IsValid);

  /// Check variable declaration in 'omp declare mapper' construct.
  TypeResult ActOnOpenMPDeclareMapperVarDecl(Scope *S, Declarator &D);
  /// Check if the specified type is allowed to be used in 'omp declare
  /// mapper' construct.
  QualType ActOnOpenMPDeclareMapperType(SourceLocation TyLoc,
                                        TypeResult ParsedType);
  /// Called on start of '#pragma omp declare mapper'.
  DeclGroupPtrTy ActOnOpenMPDeclareMapperDirective(
      Scope *S, DeclContext *DC, DeclarationName Name, QualType MapperType,
      SourceLocation StartLoc, DeclarationName VN, AccessSpecifier AS,
      Expr *MapperVarRef, ArrayRef<OMPClause *> Clauses,
      Decl *PrevDeclInScope = nullptr);
  /// Build the mapper variable of '#pragma omp declare mapper'.
  ExprResult ActOnOpenMPDeclareMapperDirectiveVarDecl(Scope *S,
                                                      QualType MapperType,
                                                      SourceLocation StartLoc,
                                                      DeclarationName VN);
  void ActOnOpenMPIteratorVarDecl(VarDecl *VD);
  bool isOpenMPDeclareMapperVarDeclAllowed(const VarDecl *VD) const;
  const ValueDecl *getOpenMPDeclareMapperVarName() const;

  struct DeclareTargetContextInfo {
    struct MapInfo {
      OMPDeclareTargetDeclAttr::MapTypeTy MT;
      SourceLocation Loc;
    };
    /// Explicitly listed variables and functions in a 'to' or 'link' clause.
    llvm::DenseMap<NamedDecl *, MapInfo> ExplicitlyMapped;

    /// The 'device_type' as parsed from the clause.
    OMPDeclareTargetDeclAttr::DevTypeTy DT = OMPDeclareTargetDeclAttr::DT_Any;

    /// The directive kind, `begin declare target` or `declare target`.
    OpenMPDirectiveKind Kind;

    /// The directive with indirect clause.
    std::optional<Expr *> Indirect;

    /// The directive location.
    SourceLocation Loc;

    DeclareTargetContextInfo(OpenMPDirectiveKind Kind, SourceLocation Loc)
        : Kind(Kind), Loc(Loc) {}
  };

  /// Called on the start of target region i.e. '#pragma omp declare target'.
  bool ActOnStartOpenMPDeclareTargetContext(DeclareTargetContextInfo &DTCI);

  /// Called at the end of target region i.e. '#pragma omp end declare target'.
  const DeclareTargetContextInfo ActOnOpenMPEndDeclareTargetDirective();

  /// Called once a target context is completed, that can be when a
  /// '#pragma omp end declare target' was encountered or when a
  /// '#pragma omp declare target' without declaration-definition-seq was
  /// encountered.
  void ActOnFinishedOpenMPDeclareTargetContext(DeclareTargetContextInfo &DTCI);

  /// Report unterminated 'omp declare target' or 'omp begin declare target' at
  /// the end of a compilation unit.
  void DiagnoseUnterminatedOpenMPDeclareTarget();

  /// Searches for the provided declaration name for OpenMP declare target
  /// directive.
  NamedDecl *lookupOpenMPDeclareTargetName(Scope *CurScope,
                                           CXXScopeSpec &ScopeSpec,
                                           const DeclarationNameInfo &Id);

  /// Called on correct id-expression from the '#pragma omp declare target'.
  void ActOnOpenMPDeclareTargetName(NamedDecl *ND, SourceLocation Loc,
                                    OMPDeclareTargetDeclAttr::MapTypeTy MT,
                                    DeclareTargetContextInfo &DTCI);

  /// Check declaration inside target region.
  void
  checkDeclIsAllowedInOpenMPTarget(Expr *E, Decl *D,
                                   SourceLocation IdLoc = SourceLocation());

  /// Adds OMPDeclareTargetDeclAttr to referenced variables in declare target
  /// directive.
  void ActOnOpenMPDeclareTargetInitializer(Decl *D);

  /// Finishes analysis of the deferred functions calls that may be declared as
  /// host/nohost during device/host compilation.
  void finalizeOpenMPDelayedAnalysis(const FunctionDecl *Caller,
                                     const FunctionDecl *Callee,
                                     SourceLocation Loc);

  /// Return true if currently in OpenMP task with untied clause context.
  bool isInOpenMPTaskUntiedContext() const;

  /// Return true inside OpenMP declare target region.
  bool isInOpenMPDeclareTargetContext() const {
    return !DeclareTargetNesting.empty();
  }
  /// Return true inside OpenMP target region.
  bool isInOpenMPTargetExecutionDirective() const;

  /// Return the number of captured regions created for an OpenMP directive.
  static int getOpenMPCaptureLevels(OpenMPDirectiveKind Kind);

  /// Initialization of captured region for OpenMP region.
  void ActOnOpenMPRegionStart(OpenMPDirectiveKind DKind, Scope *CurScope);

  /// Called for syntactical loops (ForStmt or CXXForRangeStmt) associated to
  /// an OpenMP loop directive.
  StmtResult ActOnOpenMPCanonicalLoop(Stmt *AStmt);

  /// Process a canonical OpenMP loop nest that can either be a canonical
  /// literal loop (ForStmt or CXXForRangeStmt), or the generated loop of an
  /// OpenMP loop transformation construct.
  StmtResult ActOnOpenMPLoopnest(Stmt *AStmt);

  /// End of OpenMP region.
  ///
  /// \param S Statement associated with the current OpenMP region.
  /// \param Clauses List of clauses for the current OpenMP region.
  ///
  /// \returns Statement for finished OpenMP region.
  StmtResult ActOnOpenMPRegionEnd(StmtResult S, ArrayRef<OMPClause *> Clauses);
  StmtResult ActOnOpenMPExecutableDirective(
      OpenMPDirectiveKind Kind, const DeclarationNameInfo &DirName,
      OpenMPDirectiveKind CancelRegion, ArrayRef<OMPClause *> Clauses,
      Stmt *AStmt, SourceLocation StartLoc, SourceLocation EndLoc,
      OpenMPDirectiveKind PrevMappedDirective = llvm::omp::OMPD_unknown);
  /// Called on well-formed '\#pragma omp parallel' after parsing
  /// of the  associated statement.
  StmtResult ActOnOpenMPParallelDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  using VarsWithInheritedDSAType =
      llvm::SmallDenseMap<const ValueDecl *, const Expr *, 4>;
  /// Called on well-formed '\#pragma omp simd' after parsing
  /// of the associated statement.
  StmtResult
  ActOnOpenMPSimdDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                           SourceLocation StartLoc, SourceLocation EndLoc,
                           VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '#pragma omp tile' after parsing of its clauses and
  /// the associated statement.
  StmtResult ActOnOpenMPTileDirective(ArrayRef<OMPClause *> Clauses,
                                      Stmt *AStmt, SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed '#pragma omp unroll' after parsing of its clauses
  /// and the associated statement.
  StmtResult ActOnOpenMPUnrollDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '#pragma omp reverse'.
  StmtResult ActOnOpenMPReverseDirective(Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '#pragma omp interchange' after parsing of its
  /// clauses and the associated statement.
  StmtResult ActOnOpenMPInterchangeDirective(ArrayRef<OMPClause *> Clauses,
                                             Stmt *AStmt,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp for' after parsing
  /// of the associated statement.
  StmtResult
  ActOnOpenMPForDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                          SourceLocation StartLoc, SourceLocation EndLoc,
                          VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp for simd' after parsing
  /// of the associated statement.
  StmtResult
  ActOnOpenMPForSimdDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                              SourceLocation StartLoc, SourceLocation EndLoc,
                              VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp sections' after parsing
  /// of the associated statement.
  StmtResult ActOnOpenMPSectionsDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp section' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPSectionDirective(Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp scope' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPScopeDirective(ArrayRef<OMPClause *> Clauses,
                                       Stmt *AStmt, SourceLocation StartLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp single' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPSingleDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp master' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPMasterDirective(Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp critical' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPCriticalDirective(const DeclarationNameInfo &DirName,
                                          ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp parallel for' after parsing
  /// of the  associated statement.
  StmtResult ActOnOpenMPParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel for simd' after
  /// parsing of the  associated statement.
  StmtResult ActOnOpenMPParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel master' after
  /// parsing of the  associated statement.
  StmtResult ActOnOpenMPParallelMasterDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp parallel masked' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPParallelMaskedDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp parallel sections' after
  /// parsing of the  associated statement.
  StmtResult ActOnOpenMPParallelSectionsDirective(ArrayRef<OMPClause *> Clauses,
                                                  Stmt *AStmt,
                                                  SourceLocation StartLoc,
                                                  SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp task' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPTaskDirective(ArrayRef<OMPClause *> Clauses,
                                      Stmt *AStmt, SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp taskyield'.
  StmtResult ActOnOpenMPTaskyieldDirective(SourceLocation StartLoc,
                                           SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp error'.
  /// Error direcitive is allowed in both declared and excutable contexts.
  /// Adding InExContext to identify which context is called from.
  StmtResult ActOnOpenMPErrorDirective(ArrayRef<OMPClause *> Clauses,
                                       SourceLocation StartLoc,
                                       SourceLocation EndLoc,
                                       bool InExContext = true);
  /// Called on well-formed '\#pragma omp barrier'.
  StmtResult ActOnOpenMPBarrierDirective(SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp taskwait'.
  StmtResult ActOnOpenMPTaskwaitDirective(ArrayRef<OMPClause *> Clauses,
                                          SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp taskgroup'.
  StmtResult ActOnOpenMPTaskgroupDirective(ArrayRef<OMPClause *> Clauses,
                                           Stmt *AStmt, SourceLocation StartLoc,
                                           SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp flush'.
  StmtResult ActOnOpenMPFlushDirective(ArrayRef<OMPClause *> Clauses,
                                       SourceLocation StartLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp depobj'.
  StmtResult ActOnOpenMPDepobjDirective(ArrayRef<OMPClause *> Clauses,
                                        SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp scan'.
  StmtResult ActOnOpenMPScanDirective(ArrayRef<OMPClause *> Clauses,
                                      SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp ordered' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPOrderedDirective(ArrayRef<OMPClause *> Clauses,
                                         Stmt *AStmt, SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp atomic' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPAtomicDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPTargetDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target data' after parsing of
  /// the associated statement.
  StmtResult ActOnOpenMPTargetDataDirective(ArrayRef<OMPClause *> Clauses,
                                            Stmt *AStmt,
                                            SourceLocation StartLoc,
                                            SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target enter data' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPTargetEnterDataDirective(ArrayRef<OMPClause *> Clauses,
                                                 SourceLocation StartLoc,
                                                 SourceLocation EndLoc,
                                                 Stmt *AStmt);
  /// Called on well-formed '\#pragma omp target exit data' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPTargetExitDataDirective(ArrayRef<OMPClause *> Clauses,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc,
                                                Stmt *AStmt);
  /// Called on well-formed '\#pragma omp target parallel' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPTargetParallelDirective(ArrayRef<OMPClause *> Clauses,
                                                Stmt *AStmt,
                                                SourceLocation StartLoc,
                                                SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target parallel for' after
  /// parsing of the  associated statement.
  StmtResult ActOnOpenMPTargetParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPTeamsDirective(ArrayRef<OMPClause *> Clauses,
                                       Stmt *AStmt, SourceLocation StartLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp teams loop' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPTeamsGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams loop' after parsing of
  /// the associated statement.
  StmtResult ActOnOpenMPTargetTeamsGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel loop' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPParallelGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target parallel loop' after parsing
  /// of the associated statement.
  StmtResult ActOnOpenMPTargetParallelGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp cancellation point'.
  StmtResult
  ActOnOpenMPCancellationPointDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        OpenMPDirectiveKind CancelRegion);
  /// Called on well-formed '\#pragma omp cancel'.
  StmtResult ActOnOpenMPCancelDirective(ArrayRef<OMPClause *> Clauses,
                                        SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        OpenMPDirectiveKind CancelRegion);
  /// Called on well-formed '\#pragma omp taskloop' after parsing of the
  /// associated statement.
  StmtResult
  ActOnOpenMPTaskLoopDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                               SourceLocation StartLoc, SourceLocation EndLoc,
                               VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp taskloop simd' after parsing of
  /// the associated statement.
  StmtResult ActOnOpenMPTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp master taskloop' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPMasterTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp master taskloop simd' after parsing of
  /// the associated statement.
  StmtResult ActOnOpenMPMasterTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel master taskloop' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPParallelMasterTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel master taskloop simd' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPParallelMasterTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp masked taskloop' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPMaskedTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp masked taskloop simd' after parsing of
  /// the associated statement.
  StmtResult ActOnOpenMPMaskedTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel masked taskloop' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPParallelMaskedTaskLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp parallel masked taskloop simd' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPParallelMaskedTaskLoopSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp distribute' after parsing
  /// of the associated statement.
  StmtResult
  ActOnOpenMPDistributeDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                                 SourceLocation StartLoc, SourceLocation EndLoc,
                                 VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target update'.
  StmtResult ActOnOpenMPTargetUpdateDirective(ArrayRef<OMPClause *> Clauses,
                                              SourceLocation StartLoc,
                                              SourceLocation EndLoc,
                                              Stmt *AStmt);
  /// Called on well-formed '\#pragma omp distribute parallel for' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPDistributeParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp distribute parallel for simd'
  /// after parsing of the associated statement.
  StmtResult ActOnOpenMPDistributeParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp distribute simd' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPDistributeSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target parallel for simd' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPTargetParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target simd' after parsing of
  /// the associated statement.
  StmtResult
  ActOnOpenMPTargetSimdDirective(ArrayRef<OMPClause *> Clauses, Stmt *AStmt,
                                 SourceLocation StartLoc, SourceLocation EndLoc,
                                 VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute' after parsing of
  /// the associated statement.
  StmtResult ActOnOpenMPTeamsDistributeDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute simd' after parsing
  /// of the associated statement.
  StmtResult ActOnOpenMPTeamsDistributeSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute parallel for simd'
  /// after parsing of the associated statement.
  StmtResult ActOnOpenMPTeamsDistributeParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp teams distribute parallel for'
  /// after parsing of the associated statement.
  StmtResult ActOnOpenMPTeamsDistributeParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPTargetTeamsDirective(ArrayRef<OMPClause *> Clauses,
                                             Stmt *AStmt,
                                             SourceLocation StartLoc,
                                             SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp target teams distribute' after parsing
  /// of the associated statement.
  StmtResult ActOnOpenMPTargetTeamsDistributeDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams distribute parallel for'
  /// after parsing of the associated statement.
  StmtResult ActOnOpenMPTargetTeamsDistributeParallelForDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams distribute parallel for
  /// simd' after parsing of the associated statement.
  StmtResult ActOnOpenMPTargetTeamsDistributeParallelForSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp target teams distribute simd' after
  /// parsing of the associated statement.
  StmtResult ActOnOpenMPTargetTeamsDistributeSimdDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);
  /// Called on well-formed '\#pragma omp interop'.
  StmtResult ActOnOpenMPInteropDirective(ArrayRef<OMPClause *> Clauses,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp dispatch' after parsing of the
  // /associated statement.
  StmtResult ActOnOpenMPDispatchDirective(ArrayRef<OMPClause *> Clauses,
                                          Stmt *AStmt, SourceLocation StartLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed '\#pragma omp masked' after parsing of the
  // /associated statement.
  StmtResult ActOnOpenMPMaskedDirective(ArrayRef<OMPClause *> Clauses,
                                        Stmt *AStmt, SourceLocation StartLoc,
                                        SourceLocation EndLoc);

  /// Called on well-formed '\#pragma omp loop' after parsing of the
  /// associated statement.
  StmtResult ActOnOpenMPGenericLoopDirective(
      ArrayRef<OMPClause *> Clauses, Stmt *AStmt, SourceLocation StartLoc,
      SourceLocation EndLoc, VarsWithInheritedDSAType &VarsWithImplicitDSA);

  /// Checks correctness of linear modifiers.
  bool CheckOpenMPLinearModifier(OpenMPLinearClauseKind LinKind,
                                 SourceLocation LinLoc);
  /// Checks that the specified declaration matches requirements for the linear
  /// decls.
  bool CheckOpenMPLinearDecl(const ValueDecl *D, SourceLocation ELoc,
                             OpenMPLinearClauseKind LinKind, QualType Type,
                             bool IsDeclareSimd = false);

  /// Called on well-formed '\#pragma omp declare simd' after parsing of
  /// the associated method/function.
  DeclGroupPtrTy ActOnOpenMPDeclareSimdDirective(
      DeclGroupPtrTy DG, OMPDeclareSimdDeclAttr::BranchStateTy BS,
      Expr *Simdlen, ArrayRef<Expr *> Uniforms, ArrayRef<Expr *> Aligneds,
      ArrayRef<Expr *> Alignments, ArrayRef<Expr *> Linears,
      ArrayRef<unsigned> LinModifiers, ArrayRef<Expr *> Steps, SourceRange SR);

  /// Checks '\#pragma omp declare variant' variant function and original
  /// functions after parsing of the associated method/function.
  /// \param DG Function declaration to which declare variant directive is
  /// applied to.
  /// \param VariantRef Expression that references the variant function, which
  /// must be used instead of the original one, specified in \p DG.
  /// \param TI The trait info object representing the match clause.
  /// \param NumAppendArgs The number of omp_interop_t arguments to account for
  /// in checking.
  /// \returns std::nullopt, if the function/variant function are not compatible
  /// with the pragma, pair of original function/variant ref expression
  /// otherwise.
  std::optional<std::pair<FunctionDecl *, Expr *>>
  checkOpenMPDeclareVariantFunction(DeclGroupPtrTy DG, Expr *VariantRef,
                                    OMPTraitInfo &TI, unsigned NumAppendArgs,
                                    SourceRange SR);

  /// Called on well-formed '\#pragma omp declare variant' after parsing of
  /// the associated method/function.
  /// \param FD Function declaration to which declare variant directive is
  /// applied to.
  /// \param VariantRef Expression that references the variant function, which
  /// must be used instead of the original one, specified in \p DG.
  /// \param TI The context traits associated with the function variant.
  /// \param AdjustArgsNothing The list of 'nothing' arguments.
  /// \param AdjustArgsNeedDevicePtr The list of 'need_device_ptr' arguments.
  /// \param AppendArgs The list of 'append_args' arguments.
  /// \param AdjustArgsLoc The Location of an 'adjust_args' clause.
  /// \param AppendArgsLoc The Location of an 'append_args' clause.
  /// \param SR The SourceRange of the 'declare variant' directive.
  void ActOnOpenMPDeclareVariantDirective(
      FunctionDecl *FD, Expr *VariantRef, OMPTraitInfo &TI,
      ArrayRef<Expr *> AdjustArgsNothing,
      ArrayRef<Expr *> AdjustArgsNeedDevicePtr,
      ArrayRef<OMPInteropInfo> AppendArgs, SourceLocation AdjustArgsLoc,
      SourceLocation AppendArgsLoc, SourceRange SR);

  OMPClause *ActOnOpenMPSingleExprClause(OpenMPClauseKind Kind, Expr *Expr,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed 'allocator' clause.
  OMPClause *ActOnOpenMPAllocatorClause(Expr *Allocator,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'if' clause.
  OMPClause *ActOnOpenMPIfClause(OpenMPDirectiveKind NameModifier,
                                 Expr *Condition, SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation NameModifierLoc,
                                 SourceLocation ColonLoc,
                                 SourceLocation EndLoc);
  /// Called on well-formed 'final' clause.
  OMPClause *ActOnOpenMPFinalClause(Expr *Condition, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'num_threads' clause.
  OMPClause *ActOnOpenMPNumThreadsClause(Expr *NumThreads,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed 'align' clause.
  OMPClause *ActOnOpenMPAlignClause(Expr *Alignment, SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'safelen' clause.
  OMPClause *ActOnOpenMPSafelenClause(Expr *Length, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'simdlen' clause.
  OMPClause *ActOnOpenMPSimdlenClause(Expr *Length, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-form 'sizes' clause.
  OMPClause *ActOnOpenMPSizesClause(ArrayRef<Expr *> SizeExprs,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-form 'full' clauses.
  OMPClause *ActOnOpenMPFullClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  /// Called on well-form 'partial' clauses.
  OMPClause *ActOnOpenMPPartialClause(Expr *FactorExpr, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'collapse' clause.
  OMPClause *ActOnOpenMPCollapseClause(Expr *NumForLoops,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'ordered' clause.
  OMPClause *
  ActOnOpenMPOrderedClause(SourceLocation StartLoc, SourceLocation EndLoc,
                           SourceLocation LParenLoc = SourceLocation(),
                           Expr *NumForLoops = nullptr);
  /// Called on well-formed 'grainsize' clause.
  OMPClause *ActOnOpenMPGrainsizeClause(OpenMPGrainsizeClauseModifier Modifier,
                                        Expr *Size, SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation ModifierLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'num_tasks' clause.
  OMPClause *ActOnOpenMPNumTasksClause(OpenMPNumTasksClauseModifier Modifier,
                                       Expr *NumTasks, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation ModifierLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'hint' clause.
  OMPClause *ActOnOpenMPHintClause(Expr *Hint, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'detach' clause.
  OMPClause *ActOnOpenMPDetachClause(Expr *Evt, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);

  OMPClause *ActOnOpenMPSimpleClause(OpenMPClauseKind Kind, unsigned Argument,
                                     SourceLocation ArgumentLoc,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'when' clause.
  OMPClause *ActOnOpenMPWhenClause(OMPTraitInfo &TI, SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'default' clause.
  OMPClause *ActOnOpenMPDefaultClause(llvm::omp::DefaultKind Kind,
                                      SourceLocation KindLoc,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'proc_bind' clause.
  OMPClause *ActOnOpenMPProcBindClause(llvm::omp::ProcBindKind Kind,
                                       SourceLocation KindLoc,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'order' clause.
  OMPClause *ActOnOpenMPOrderClause(OpenMPOrderClauseModifier Modifier,
                                    OpenMPOrderClauseKind Kind,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation MLoc, SourceLocation KindLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'update' clause.
  OMPClause *ActOnOpenMPUpdateClause(OpenMPDependClauseKind Kind,
                                     SourceLocation KindLoc,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);

  OMPClause *ActOnOpenMPSingleExprWithArgClause(
      OpenMPClauseKind Kind, ArrayRef<unsigned> Arguments, Expr *Expr,
      SourceLocation StartLoc, SourceLocation LParenLoc,
      ArrayRef<SourceLocation> ArgumentsLoc, SourceLocation DelimLoc,
      SourceLocation EndLoc);
  /// Called on well-formed 'schedule' clause.
  OMPClause *ActOnOpenMPScheduleClause(
      OpenMPScheduleClauseModifier M1, OpenMPScheduleClauseModifier M2,
      OpenMPScheduleClauseKind Kind, Expr *ChunkSize, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation M1Loc, SourceLocation M2Loc,
      SourceLocation KindLoc, SourceLocation CommaLoc, SourceLocation EndLoc);

  OMPClause *ActOnOpenMPClause(OpenMPClauseKind Kind, SourceLocation StartLoc,
                               SourceLocation EndLoc);
  /// Called on well-formed 'nowait' clause.
  OMPClause *ActOnOpenMPNowaitClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'untied' clause.
  OMPClause *ActOnOpenMPUntiedClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'mergeable' clause.
  OMPClause *ActOnOpenMPMergeableClause(SourceLocation StartLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'read' clause.
  OMPClause *ActOnOpenMPReadClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'write' clause.
  OMPClause *ActOnOpenMPWriteClause(SourceLocation StartLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'update' clause.
  OMPClause *ActOnOpenMPUpdateClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'capture' clause.
  OMPClause *ActOnOpenMPCaptureClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'compare' clause.
  OMPClause *ActOnOpenMPCompareClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'fail' clause.
  OMPClause *ActOnOpenMPFailClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  OMPClause *ActOnOpenMPFailClause(OpenMPClauseKind Kind,
                                   SourceLocation KindLoc,
                                   SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);

  /// Called on well-formed 'seq_cst' clause.
  OMPClause *ActOnOpenMPSeqCstClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'acq_rel' clause.
  OMPClause *ActOnOpenMPAcqRelClause(SourceLocation StartLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'acquire' clause.
  OMPClause *ActOnOpenMPAcquireClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'release' clause.
  OMPClause *ActOnOpenMPReleaseClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'relaxed' clause.
  OMPClause *ActOnOpenMPRelaxedClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'weak' clause.
  OMPClause *ActOnOpenMPWeakClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);

  /// Called on well-formed 'init' clause.
  OMPClause *
  ActOnOpenMPInitClause(Expr *InteropVar, OMPInteropInfo &InteropInfo,
                        SourceLocation StartLoc, SourceLocation LParenLoc,
                        SourceLocation VarLoc, SourceLocation EndLoc);

  /// Called on well-formed 'use' clause.
  OMPClause *ActOnOpenMPUseClause(Expr *InteropVar, SourceLocation StartLoc,
                                  SourceLocation LParenLoc,
                                  SourceLocation VarLoc, SourceLocation EndLoc);

  /// Called on well-formed 'destroy' clause.
  OMPClause *ActOnOpenMPDestroyClause(Expr *InteropVar, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation VarLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'novariants' clause.
  OMPClause *ActOnOpenMPNovariantsClause(Expr *Condition,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);
  /// Called on well-formed 'nocontext' clause.
  OMPClause *ActOnOpenMPNocontextClause(Expr *Condition,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'filter' clause.
  OMPClause *ActOnOpenMPFilterClause(Expr *ThreadID, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'threads' clause.
  OMPClause *ActOnOpenMPThreadsClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'simd' clause.
  OMPClause *ActOnOpenMPSIMDClause(SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  /// Called on well-formed 'nogroup' clause.
  OMPClause *ActOnOpenMPNogroupClause(SourceLocation StartLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'unified_address' clause.
  OMPClause *ActOnOpenMPUnifiedAddressClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc);

  /// Called on well-formed 'unified_address' clause.
  OMPClause *ActOnOpenMPUnifiedSharedMemoryClause(SourceLocation StartLoc,
                                                  SourceLocation EndLoc);

  /// Called on well-formed 'reverse_offload' clause.
  OMPClause *ActOnOpenMPReverseOffloadClause(SourceLocation StartLoc,
                                             SourceLocation EndLoc);

  /// Called on well-formed 'dynamic_allocators' clause.
  OMPClause *ActOnOpenMPDynamicAllocatorsClause(SourceLocation StartLoc,
                                                SourceLocation EndLoc);

  /// Called on well-formed 'atomic_default_mem_order' clause.
  OMPClause *ActOnOpenMPAtomicDefaultMemOrderClause(
      OpenMPAtomicDefaultMemOrderClauseKind Kind, SourceLocation KindLoc,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc);

  /// Called on well-formed 'at' clause.
  OMPClause *ActOnOpenMPAtClause(OpenMPAtClauseKind Kind,
                                 SourceLocation KindLoc,
                                 SourceLocation StartLoc,
                                 SourceLocation LParenLoc,
                                 SourceLocation EndLoc);

  /// Called on well-formed 'severity' clause.
  OMPClause *ActOnOpenMPSeverityClause(OpenMPSeverityClauseKind Kind,
                                       SourceLocation KindLoc,
                                       SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);

  /// Called on well-formed 'message' clause.
  /// passing string for message.
  OMPClause *ActOnOpenMPMessageClause(Expr *MS, SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);

  /// Data used for processing a list of variables in OpenMP clauses.
  struct OpenMPVarListDataTy final {
    Expr *DepModOrTailExpr = nullptr;
    Expr *IteratorExpr = nullptr;
    SourceLocation ColonLoc;
    SourceLocation RLoc;
    CXXScopeSpec ReductionOrMapperIdScopeSpec;
    DeclarationNameInfo ReductionOrMapperId;
    int ExtraModifier = -1; ///< Additional modifier for linear, map, depend or
                            ///< lastprivate clause.
    SmallVector<OpenMPMapModifierKind, NumberOfOMPMapClauseModifiers>
        MapTypeModifiers;
    SmallVector<SourceLocation, NumberOfOMPMapClauseModifiers>
        MapTypeModifiersLoc;
    SmallVector<OpenMPMotionModifierKind, NumberOfOMPMotionModifiers>
        MotionModifiers;
    SmallVector<SourceLocation, NumberOfOMPMotionModifiers> MotionModifiersLoc;
    bool IsMapTypeImplicit = false;
    SourceLocation ExtraModifierLoc;
    SourceLocation OmpAllMemoryLoc;
    SourceLocation
        StepModifierLoc; /// 'step' modifier location for linear clause
  };

  OMPClause *ActOnOpenMPVarListClause(OpenMPClauseKind Kind,
                                      ArrayRef<Expr *> Vars,
                                      const OMPVarListLocTy &Locs,
                                      OpenMPVarListDataTy &Data);
  /// Called on well-formed 'inclusive' clause.
  OMPClause *ActOnOpenMPInclusiveClause(ArrayRef<Expr *> VarList,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'exclusive' clause.
  OMPClause *ActOnOpenMPExclusiveClause(ArrayRef<Expr *> VarList,
                                        SourceLocation StartLoc,
                                        SourceLocation LParenLoc,
                                        SourceLocation EndLoc);
  /// Called on well-formed 'allocate' clause.
  OMPClause *
  ActOnOpenMPAllocateClause(Expr *Allocator, ArrayRef<Expr *> VarList,
                            SourceLocation StartLoc, SourceLocation ColonLoc,
                            SourceLocation LParenLoc, SourceLocation EndLoc);
  /// Called on well-formed 'private' clause.
  OMPClause *ActOnOpenMPPrivateClause(ArrayRef<Expr *> VarList,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'firstprivate' clause.
  OMPClause *ActOnOpenMPFirstprivateClause(ArrayRef<Expr *> VarList,
                                           SourceLocation StartLoc,
                                           SourceLocation LParenLoc,
                                           SourceLocation EndLoc);
  /// Called on well-formed 'lastprivate' clause.
  OMPClause *ActOnOpenMPLastprivateClause(
      ArrayRef<Expr *> VarList, OpenMPLastprivateModifier LPKind,
      SourceLocation LPKindLoc, SourceLocation ColonLoc,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation EndLoc);
  /// Called on well-formed 'shared' clause.
  OMPClause *ActOnOpenMPSharedClause(ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'reduction' clause.
  OMPClause *ActOnOpenMPReductionClause(
      ArrayRef<Expr *> VarList, OpenMPReductionClauseModifier Modifier,
      SourceLocation StartLoc, SourceLocation LParenLoc,
      SourceLocation ModifierLoc, SourceLocation ColonLoc,
      SourceLocation EndLoc, CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions = std::nullopt);
  /// Called on well-formed 'task_reduction' clause.
  OMPClause *ActOnOpenMPTaskReductionClause(
      ArrayRef<Expr *> VarList, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc,
      CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions = std::nullopt);
  /// Called on well-formed 'in_reduction' clause.
  OMPClause *ActOnOpenMPInReductionClause(
      ArrayRef<Expr *> VarList, SourceLocation StartLoc,
      SourceLocation LParenLoc, SourceLocation ColonLoc, SourceLocation EndLoc,
      CXXScopeSpec &ReductionIdScopeSpec,
      const DeclarationNameInfo &ReductionId,
      ArrayRef<Expr *> UnresolvedReductions = std::nullopt);
  /// Called on well-formed 'linear' clause.
  OMPClause *ActOnOpenMPLinearClause(
      ArrayRef<Expr *> VarList, Expr *Step, SourceLocation StartLoc,
      SourceLocation LParenLoc, OpenMPLinearClauseKind LinKind,
      SourceLocation LinLoc, SourceLocation ColonLoc,
      SourceLocation StepModifierLoc, SourceLocation EndLoc);
  /// Called on well-formed 'aligned' clause.
  OMPClause *ActOnOpenMPAlignedClause(ArrayRef<Expr *> VarList, Expr *Alignment,
                                      SourceLocation StartLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation ColonLoc,
                                      SourceLocation EndLoc);
  /// Called on well-formed 'copyin' clause.
  OMPClause *ActOnOpenMPCopyinClause(ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'copyprivate' clause.
  OMPClause *ActOnOpenMPCopyprivateClause(ArrayRef<Expr *> VarList,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed 'flush' pseudo clause.
  OMPClause *ActOnOpenMPFlushClause(ArrayRef<Expr *> VarList,
                                    SourceLocation StartLoc,
                                    SourceLocation LParenLoc,
                                    SourceLocation EndLoc);
  /// Called on well-formed 'depobj' pseudo clause.
  OMPClause *ActOnOpenMPDepobjClause(Expr *Depobj, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'depend' clause.
  OMPClause *ActOnOpenMPDependClause(const OMPDependClause::DependDataTy &Data,
                                     Expr *DepModifier,
                                     ArrayRef<Expr *> VarList,
                                     SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'device' clause.
  OMPClause *ActOnOpenMPDeviceClause(OpenMPDeviceClauseModifier Modifier,
                                     Expr *Device, SourceLocation StartLoc,
                                     SourceLocation LParenLoc,
                                     SourceLocation ModifierLoc,
                                     SourceLocation EndLoc);
  /// Called on well-formed 'map' clause.
  OMPClause *ActOnOpenMPMapClause(
      Expr *IteratorModifier, ArrayRef<OpenMPMapModifierKind> MapTypeModifiers,
      ArrayRef<SourceLocation> MapTypeModifiersLoc,
      CXXScopeSpec &MapperIdScopeSpec, DeclarationNameInfo &MapperId,
      OpenMPMapClauseKind MapType, bool IsMapTypeImplicit,
      SourceLocation MapLoc, SourceLocation ColonLoc, ArrayRef<Expr *> VarList,
      const OMPVarListLocTy &Locs, bool NoDiagnose = false,
      ArrayRef<Expr *> UnresolvedMappers = std::nullopt);
  /// Called on well-formed 'num_teams' clause.
  OMPClause *ActOnOpenMPNumTeamsClause(Expr *NumTeams, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'thread_limit' clause.
  OMPClause *ActOnOpenMPThreadLimitClause(Expr *ThreadLimit,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);
  /// Called on well-formed 'priority' clause.
  OMPClause *ActOnOpenMPPriorityClause(Expr *Priority, SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation EndLoc);
  /// Called on well-formed 'dist_schedule' clause.
  OMPClause *ActOnOpenMPDistScheduleClause(
      OpenMPDistScheduleClauseKind Kind, Expr *ChunkSize,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation KindLoc,
      SourceLocation CommaLoc, SourceLocation EndLoc);
  /// Called on well-formed 'defaultmap' clause.
  OMPClause *ActOnOpenMPDefaultmapClause(
      OpenMPDefaultmapClauseModifier M, OpenMPDefaultmapClauseKind Kind,
      SourceLocation StartLoc, SourceLocation LParenLoc, SourceLocation MLoc,
      SourceLocation KindLoc, SourceLocation EndLoc);
  /// Called on well-formed 'to' clause.
  OMPClause *
  ActOnOpenMPToClause(ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                      ArrayRef<SourceLocation> MotionModifiersLoc,
                      CXXScopeSpec &MapperIdScopeSpec,
                      DeclarationNameInfo &MapperId, SourceLocation ColonLoc,
                      ArrayRef<Expr *> VarList, const OMPVarListLocTy &Locs,
                      ArrayRef<Expr *> UnresolvedMappers = std::nullopt);
  /// Called on well-formed 'from' clause.
  OMPClause *
  ActOnOpenMPFromClause(ArrayRef<OpenMPMotionModifierKind> MotionModifiers,
                        ArrayRef<SourceLocation> MotionModifiersLoc,
                        CXXScopeSpec &MapperIdScopeSpec,
                        DeclarationNameInfo &MapperId, SourceLocation ColonLoc,
                        ArrayRef<Expr *> VarList, const OMPVarListLocTy &Locs,
                        ArrayRef<Expr *> UnresolvedMappers = std::nullopt);
  /// Called on well-formed 'use_device_ptr' clause.
  OMPClause *ActOnOpenMPUseDevicePtrClause(ArrayRef<Expr *> VarList,
                                           const OMPVarListLocTy &Locs);
  /// Called on well-formed 'use_device_addr' clause.
  OMPClause *ActOnOpenMPUseDeviceAddrClause(ArrayRef<Expr *> VarList,
                                            const OMPVarListLocTy &Locs);
  /// Called on well-formed 'is_device_ptr' clause.
  OMPClause *ActOnOpenMPIsDevicePtrClause(ArrayRef<Expr *> VarList,
                                          const OMPVarListLocTy &Locs);
  /// Called on well-formed 'has_device_addr' clause.
  OMPClause *ActOnOpenMPHasDeviceAddrClause(ArrayRef<Expr *> VarList,
                                            const OMPVarListLocTy &Locs);
  /// Called on well-formed 'nontemporal' clause.
  OMPClause *ActOnOpenMPNontemporalClause(ArrayRef<Expr *> VarList,
                                          SourceLocation StartLoc,
                                          SourceLocation LParenLoc,
                                          SourceLocation EndLoc);

  /// Data for list of allocators.
  struct UsesAllocatorsData {
    /// Allocator.
    Expr *Allocator = nullptr;
    /// Allocator traits.
    Expr *AllocatorTraits = nullptr;
    /// Locations of '(' and ')' symbols.
    SourceLocation LParenLoc, RParenLoc;
  };
  /// Called on well-formed 'uses_allocators' clause.
  OMPClause *ActOnOpenMPUsesAllocatorClause(SourceLocation StartLoc,
                                            SourceLocation LParenLoc,
                                            SourceLocation EndLoc,
                                            ArrayRef<UsesAllocatorsData> Data);
  /// Called on well-formed 'affinity' clause.
  OMPClause *ActOnOpenMPAffinityClause(SourceLocation StartLoc,
                                       SourceLocation LParenLoc,
                                       SourceLocation ColonLoc,
                                       SourceLocation EndLoc, Expr *Modifier,
                                       ArrayRef<Expr *> Locators);
  /// Called on a well-formed 'bind' clause.
  OMPClause *ActOnOpenMPBindClause(OpenMPBindClauseKind Kind,
                                   SourceLocation KindLoc,
                                   SourceLocation StartLoc,
                                   SourceLocation LParenLoc,
                                   SourceLocation EndLoc);

  /// Called on a well-formed 'ompx_dyn_cgroup_mem' clause.
  OMPClause *ActOnOpenMPXDynCGroupMemClause(Expr *Size, SourceLocation StartLoc,
                                            SourceLocation LParenLoc,
                                            SourceLocation EndLoc);

  /// Called on well-formed 'doacross' clause.
  OMPClause *
  ActOnOpenMPDoacrossClause(OpenMPDoacrossClauseModifier DepType,
                            SourceLocation DepLoc, SourceLocation ColonLoc,
                            ArrayRef<Expr *> VarList, SourceLocation StartLoc,
                            SourceLocation LParenLoc, SourceLocation EndLoc);

  /// Called on a well-formed 'ompx_attribute' clause.
  OMPClause *ActOnOpenMPXAttributeClause(ArrayRef<const Attr *> Attrs,
                                         SourceLocation StartLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation EndLoc);

  /// Called on a well-formed 'ompx_bare' clause.
  OMPClause *ActOnOpenMPXBareClause(SourceLocation StartLoc,
                                    SourceLocation EndLoc);

  ExprResult ActOnOMPArraySectionExpr(Expr *Base, SourceLocation LBLoc,
                                      Expr *LowerBound,
                                      SourceLocation ColonLocFirst,
                                      SourceLocation ColonLocSecond,
                                      Expr *Length, Expr *Stride,
                                      SourceLocation RBLoc);
  ExprResult ActOnOMPArrayShapingExpr(Expr *Base, SourceLocation LParenLoc,
                                      SourceLocation RParenLoc,
                                      ArrayRef<Expr *> Dims,
                                      ArrayRef<SourceRange> Brackets);

  /// Data structure for iterator expression.
  struct OMPIteratorData {
    IdentifierInfo *DeclIdent = nullptr;
    SourceLocation DeclIdentLoc;
    ParsedType Type;
    OMPIteratorExpr::IteratorRange Range;
    SourceLocation AssignLoc;
    SourceLocation ColonLoc;
    SourceLocation SecColonLoc;
  };

  ExprResult ActOnOMPIteratorExpr(Scope *S, SourceLocation IteratorKwLoc,
                                  SourceLocation LLoc, SourceLocation RLoc,
                                  ArrayRef<OMPIteratorData> Data);

  void handleOMPAssumeAttr(Decl *D, const ParsedAttr &AL);

private:
  void *VarDataSharingAttributesStack;

  /// Number of nested '#pragma omp declare target' directives.
  SmallVector<DeclareTargetContextInfo, 4> DeclareTargetNesting;

  /// Initialization of data-sharing attributes stack.
  void InitDataSharingAttributesStack();
  void DestroyDataSharingAttributesStack();

  /// Returns OpenMP nesting level for current directive.
  unsigned getOpenMPNestingLevel() const;

  /// Adjusts the function scopes index for the target-based regions.
  void adjustOpenMPTargetScopeIndex(unsigned &FunctionScopesIndex,
                                    unsigned Level) const;

  /// Returns the number of scopes associated with the construct on the given
  /// OpenMP level.
  int getNumberOfConstructScopes(unsigned Level) const;

  /// Push new OpenMP function region for non-capturing function.
  void pushOpenMPFunctionRegion();

  /// Pop OpenMP function region for non-capturing function.
  void popOpenMPFunctionRegion(const sema::FunctionScopeInfo *OldFSI);

  /// Analyzes and checks a loop nest for use by a loop transformation.
  ///
  /// \param Kind          The loop transformation directive kind.
  /// \param NumLoops      How many nested loops the directive is expecting.
  /// \param AStmt         Associated statement of the transformation directive.
  /// \param LoopHelpers   [out] The loop analysis result.
  /// \param Body          [out] The body code nested in \p NumLoops loop.
  /// \param OriginalInits [out] Collection of statements and declarations that
  ///                      must have been executed/declared before entering the
  ///                      loop.
  ///
  /// \return Whether there was any error.
  bool checkTransformableLoopNest(
      OpenMPDirectiveKind Kind, Stmt *AStmt, int NumLoops,
      SmallVectorImpl<OMPLoopBasedDirective::HelperExprs> &LoopHelpers,
      Stmt *&Body, SmallVectorImpl<SmallVector<Stmt *, 0>> &OriginalInits);

  /// Helper to keep information about the current `omp begin/end declare
  /// variant` nesting.
  struct OMPDeclareVariantScope {
    /// The associated OpenMP context selector.
    OMPTraitInfo *TI;

    /// The associated OpenMP context selector mangling.
    std::string NameSuffix;

    OMPDeclareVariantScope(OMPTraitInfo &TI);
  };

  /// Return the OMPTraitInfo for the surrounding scope, if any.
  OMPTraitInfo *getOMPTraitInfoForSurroundingScope() {
    return OMPDeclareVariantScopes.empty() ? nullptr
                                           : OMPDeclareVariantScopes.back().TI;
  }

  /// The current `omp begin/end declare variant` scopes.
  SmallVector<OMPDeclareVariantScope, 4> OMPDeclareVariantScopes;

  /// The current `omp begin/end assumes` scopes.
  SmallVector<OMPAssumeAttr *, 4> OMPAssumeScoped;

  /// All `omp assumes` we encountered so far.
  SmallVector<OMPAssumeAttr *, 4> OMPAssumeGlobal;

  /// OMPD_loop is mapped to OMPD_for, OMPD_distribute or OMPD_simd depending
  /// on the parameter of the bind clause. In the methods for the
  /// mapped directives, check the parameters of the lastprivate clause.
  bool checkLastPrivateForMappedDirectives(ArrayRef<OMPClause *> Clauses);
  /// Depending on the bind clause of OMPD_loop map the directive to new
  /// directives.
  ///    1) loop bind(parallel) --> OMPD_for
  ///    2) loop bind(teams) --> OMPD_distribute
  ///    3) loop bind(thread) --> OMPD_simd
  /// This is being handled in Sema instead of Codegen because of the need for
  /// rigorous semantic checking in the new mapped directives.
  bool mapLoopConstruct(llvm::SmallVector<OMPClause *> &ClausesWithoutBind,
                        ArrayRef<OMPClause *> Clauses,
                        OpenMPBindClauseKind &BindKind,
                        OpenMPDirectiveKind &Kind,
                        OpenMPDirectiveKind &PrevMappedDirective,
                        SourceLocation StartLoc, SourceLocation EndLoc,
                        const DeclarationNameInfo &DirName,
                        OpenMPDirectiveKind CancelRegion);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAOPENMP_H
