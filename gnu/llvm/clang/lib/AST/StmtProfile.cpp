//===---- StmtProfile.cpp - Profile implementation for Stmt ASTs ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Stmt::Profile method, which builds a unique bit
// representation that identifies a statement/expression.
//
//===----------------------------------------------------------------------===//
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/ODRHash.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/ADT/FoldingSet.h"
using namespace clang;

namespace {
  class StmtProfiler : public ConstStmtVisitor<StmtProfiler> {
  protected:
    llvm::FoldingSetNodeID &ID;
    bool Canonical;
    bool ProfileLambdaExpr;

  public:
    StmtProfiler(llvm::FoldingSetNodeID &ID, bool Canonical,
                 bool ProfileLambdaExpr)
        : ID(ID), Canonical(Canonical), ProfileLambdaExpr(ProfileLambdaExpr) {}

    virtual ~StmtProfiler() {}

    void VisitStmt(const Stmt *S);

    void VisitStmtNoChildren(const Stmt *S) {
      HandleStmtClass(S->getStmtClass());
    }

    virtual void HandleStmtClass(Stmt::StmtClass SC) = 0;

#define STMT(Node, Base) void Visit##Node(const Node *S);
#include "clang/AST/StmtNodes.inc"

    /// Visit a declaration that is referenced within an expression
    /// or statement.
    virtual void VisitDecl(const Decl *D) = 0;

    /// Visit a type that is referenced within an expression or
    /// statement.
    virtual void VisitType(QualType T) = 0;

    /// Visit a name that occurs within an expression or statement.
    virtual void VisitName(DeclarationName Name, bool TreatAsDecl = false) = 0;

    /// Visit identifiers that are not in Decl's or Type's.
    virtual void VisitIdentifierInfo(const IdentifierInfo *II) = 0;

    /// Visit a nested-name-specifier that occurs within an expression
    /// or statement.
    virtual void VisitNestedNameSpecifier(NestedNameSpecifier *NNS) = 0;

    /// Visit a template name that occurs within an expression or
    /// statement.
    virtual void VisitTemplateName(TemplateName Name) = 0;

    /// Visit template arguments that occur within an expression or
    /// statement.
    void VisitTemplateArguments(const TemplateArgumentLoc *Args,
                                unsigned NumArgs);

    /// Visit a single template argument.
    void VisitTemplateArgument(const TemplateArgument &Arg);
  };

  class StmtProfilerWithPointers : public StmtProfiler {
    const ASTContext &Context;

  public:
    StmtProfilerWithPointers(llvm::FoldingSetNodeID &ID,
                             const ASTContext &Context, bool Canonical,
                             bool ProfileLambdaExpr)
        : StmtProfiler(ID, Canonical, ProfileLambdaExpr), Context(Context) {}

  private:
    void HandleStmtClass(Stmt::StmtClass SC) override {
      ID.AddInteger(SC);
    }

    void VisitDecl(const Decl *D) override {
      ID.AddInteger(D ? D->getKind() : 0);

      if (Canonical && D) {
        if (const NonTypeTemplateParmDecl *NTTP =
                dyn_cast<NonTypeTemplateParmDecl>(D)) {
          ID.AddInteger(NTTP->getDepth());
          ID.AddInteger(NTTP->getIndex());
          ID.AddBoolean(NTTP->isParameterPack());
          // C++20 [temp.over.link]p6:
          //   Two template-parameters are equivalent under the following
          //   conditions: [...] if they declare non-type template parameters,
          //   they have equivalent types ignoring the use of type-constraints
          //   for placeholder types
          //
          // TODO: Why do we need to include the type in the profile? It's not
          // part of the mangling.
          VisitType(Context.getUnconstrainedType(NTTP->getType()));
          return;
        }

        if (const ParmVarDecl *Parm = dyn_cast<ParmVarDecl>(D)) {
          // The Itanium C++ ABI uses the type, scope depth, and scope
          // index of a parameter when mangling expressions that involve
          // function parameters, so we will use the parameter's type for
          // establishing function parameter identity. That way, our
          // definition of "equivalent" (per C++ [temp.over.link]) is at
          // least as strong as the definition of "equivalent" used for
          // name mangling.
          //
          // TODO: The Itanium C++ ABI only uses the top-level cv-qualifiers,
          // not the entirety of the type.
          VisitType(Parm->getType());
          ID.AddInteger(Parm->getFunctionScopeDepth());
          ID.AddInteger(Parm->getFunctionScopeIndex());
          return;
        }

        if (const TemplateTypeParmDecl *TTP =
                dyn_cast<TemplateTypeParmDecl>(D)) {
          ID.AddInteger(TTP->getDepth());
          ID.AddInteger(TTP->getIndex());
          ID.AddBoolean(TTP->isParameterPack());
          return;
        }

        if (const TemplateTemplateParmDecl *TTP =
                dyn_cast<TemplateTemplateParmDecl>(D)) {
          ID.AddInteger(TTP->getDepth());
          ID.AddInteger(TTP->getIndex());
          ID.AddBoolean(TTP->isParameterPack());
          return;
        }
      }

      ID.AddPointer(D ? D->getCanonicalDecl() : nullptr);
    }

    void VisitType(QualType T) override {
      if (Canonical && !T.isNull())
        T = Context.getCanonicalType(T);

      ID.AddPointer(T.getAsOpaquePtr());
    }

    void VisitName(DeclarationName Name, bool /*TreatAsDecl*/) override {
      ID.AddPointer(Name.getAsOpaquePtr());
    }

    void VisitIdentifierInfo(const IdentifierInfo *II) override {
      ID.AddPointer(II);
    }

    void VisitNestedNameSpecifier(NestedNameSpecifier *NNS) override {
      if (Canonical)
        NNS = Context.getCanonicalNestedNameSpecifier(NNS);
      ID.AddPointer(NNS);
    }

    void VisitTemplateName(TemplateName Name) override {
      if (Canonical)
        Name = Context.getCanonicalTemplateName(Name);

      Name.Profile(ID);
    }
  };

  class StmtProfilerWithoutPointers : public StmtProfiler {
    ODRHash &Hash;
  public:
    StmtProfilerWithoutPointers(llvm::FoldingSetNodeID &ID, ODRHash &Hash)
        : StmtProfiler(ID, /*Canonical=*/false, /*ProfileLambdaExpr=*/false),
          Hash(Hash) {}

  private:
    void HandleStmtClass(Stmt::StmtClass SC) override {
      if (SC == Stmt::UnresolvedLookupExprClass) {
        // Pretend that the name looked up is a Decl due to how templates
        // handle some Decl lookups.
        ID.AddInteger(Stmt::DeclRefExprClass);
      } else {
        ID.AddInteger(SC);
      }
    }

    void VisitType(QualType T) override {
      Hash.AddQualType(T);
    }

    void VisitName(DeclarationName Name, bool TreatAsDecl) override {
      if (TreatAsDecl) {
        // A Decl can be null, so each Decl is preceded by a boolean to
        // store its nullness.  Add a boolean here to match.
        ID.AddBoolean(true);
      }
      Hash.AddDeclarationName(Name, TreatAsDecl);
    }
    void VisitIdentifierInfo(const IdentifierInfo *II) override {
      ID.AddBoolean(II);
      if (II) {
        Hash.AddIdentifierInfo(II);
      }
    }
    void VisitDecl(const Decl *D) override {
      ID.AddBoolean(D);
      if (D) {
        Hash.AddDecl(D);
      }
    }
    void VisitTemplateName(TemplateName Name) override {
      Hash.AddTemplateName(Name);
    }
    void VisitNestedNameSpecifier(NestedNameSpecifier *NNS) override {
      ID.AddBoolean(NNS);
      if (NNS) {
        Hash.AddNestedNameSpecifier(NNS);
      }
    }
  };
}

void StmtProfiler::VisitStmt(const Stmt *S) {
  assert(S && "Requires non-null Stmt pointer");

  VisitStmtNoChildren(S);

  for (const Stmt *SubStmt : S->children()) {
    if (SubStmt)
      Visit(SubStmt);
    else
      ID.AddInteger(0);
  }
}

void StmtProfiler::VisitDeclStmt(const DeclStmt *S) {
  VisitStmt(S);
  for (const auto *D : S->decls())
    VisitDecl(D);
}

void StmtProfiler::VisitNullStmt(const NullStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitCompoundStmt(const CompoundStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitCaseStmt(const CaseStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitDefaultStmt(const DefaultStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitLabelStmt(const LabelStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getDecl());
}

void StmtProfiler::VisitAttributedStmt(const AttributedStmt *S) {
  VisitStmt(S);
  // TODO: maybe visit attributes?
}

void StmtProfiler::VisitIfStmt(const IfStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getConditionVariable());
}

void StmtProfiler::VisitSwitchStmt(const SwitchStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getConditionVariable());
}

void StmtProfiler::VisitWhileStmt(const WhileStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getConditionVariable());
}

void StmtProfiler::VisitDoStmt(const DoStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitForStmt(const ForStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitGotoStmt(const GotoStmt *S) {
  VisitStmt(S);
  VisitDecl(S->getLabel());
}

void StmtProfiler::VisitIndirectGotoStmt(const IndirectGotoStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitContinueStmt(const ContinueStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitBreakStmt(const BreakStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitReturnStmt(const ReturnStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitGCCAsmStmt(const GCCAsmStmt *S) {
  VisitStmt(S);
  ID.AddBoolean(S->isVolatile());
  ID.AddBoolean(S->isSimple());
  VisitStringLiteral(S->getAsmString());
  ID.AddInteger(S->getNumOutputs());
  for (unsigned I = 0, N = S->getNumOutputs(); I != N; ++I) {
    ID.AddString(S->getOutputName(I));
    VisitStringLiteral(S->getOutputConstraintLiteral(I));
  }
  ID.AddInteger(S->getNumInputs());
  for (unsigned I = 0, N = S->getNumInputs(); I != N; ++I) {
    ID.AddString(S->getInputName(I));
    VisitStringLiteral(S->getInputConstraintLiteral(I));
  }
  ID.AddInteger(S->getNumClobbers());
  for (unsigned I = 0, N = S->getNumClobbers(); I != N; ++I)
    VisitStringLiteral(S->getClobberStringLiteral(I));
  ID.AddInteger(S->getNumLabels());
  for (auto *L : S->labels())
    VisitDecl(L->getLabel());
}

void StmtProfiler::VisitMSAsmStmt(const MSAsmStmt *S) {
  // FIXME: Implement MS style inline asm statement profiler.
  VisitStmt(S);
}

void StmtProfiler::VisitCXXCatchStmt(const CXXCatchStmt *S) {
  VisitStmt(S);
  VisitType(S->getCaughtType());
}

void StmtProfiler::VisitCXXTryStmt(const CXXTryStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitCXXForRangeStmt(const CXXForRangeStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitMSDependentExistsStmt(const MSDependentExistsStmt *S) {
  VisitStmt(S);
  ID.AddBoolean(S->isIfExists());
  VisitNestedNameSpecifier(S->getQualifierLoc().getNestedNameSpecifier());
  VisitName(S->getNameInfo().getName());
}

void StmtProfiler::VisitSEHTryStmt(const SEHTryStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitSEHFinallyStmt(const SEHFinallyStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitSEHExceptStmt(const SEHExceptStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitSEHLeaveStmt(const SEHLeaveStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitCapturedStmt(const CapturedStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitObjCAtCatchStmt(const ObjCAtCatchStmt *S) {
  VisitStmt(S);
  ID.AddBoolean(S->hasEllipsis());
  if (S->getCatchParamDecl())
    VisitType(S->getCatchParamDecl()->getType());
}

void StmtProfiler::VisitObjCAtFinallyStmt(const ObjCAtFinallyStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitObjCAtTryStmt(const ObjCAtTryStmt *S) {
  VisitStmt(S);
}

void
StmtProfiler::VisitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitObjCAtThrowStmt(const ObjCAtThrowStmt *S) {
  VisitStmt(S);
}

void
StmtProfiler::VisitObjCAutoreleasePoolStmt(const ObjCAutoreleasePoolStmt *S) {
  VisitStmt(S);
}

namespace {
class OMPClauseProfiler : public ConstOMPClauseVisitor<OMPClauseProfiler> {
  StmtProfiler *Profiler;
  /// Process clauses with list of variables.
  template <typename T>
  void VisitOMPClauseList(T *Node);

public:
  OMPClauseProfiler(StmtProfiler *P) : Profiler(P) { }
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) void Visit##Class(const Class *C);
#include "llvm/Frontend/OpenMP/OMP.inc"
  void VistOMPClauseWithPreInit(const OMPClauseWithPreInit *C);
  void VistOMPClauseWithPostUpdate(const OMPClauseWithPostUpdate *C);
};

void OMPClauseProfiler::VistOMPClauseWithPreInit(
    const OMPClauseWithPreInit *C) {
  if (auto *S = C->getPreInitStmt())
    Profiler->VisitStmt(S);
}

void OMPClauseProfiler::VistOMPClauseWithPostUpdate(
    const OMPClauseWithPostUpdate *C) {
  VistOMPClauseWithPreInit(C);
  if (auto *E = C->getPostUpdateExpr())
    Profiler->VisitStmt(E);
}

void OMPClauseProfiler::VisitOMPIfClause(const OMPIfClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getCondition())
    Profiler->VisitStmt(C->getCondition());
}

void OMPClauseProfiler::VisitOMPFinalClause(const OMPFinalClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getCondition())
    Profiler->VisitStmt(C->getCondition());
}

void OMPClauseProfiler::VisitOMPNumThreadsClause(const OMPNumThreadsClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getNumThreads())
    Profiler->VisitStmt(C->getNumThreads());
}

void OMPClauseProfiler::VisitOMPAlignClause(const OMPAlignClause *C) {
  if (C->getAlignment())
    Profiler->VisitStmt(C->getAlignment());
}

void OMPClauseProfiler::VisitOMPSafelenClause(const OMPSafelenClause *C) {
  if (C->getSafelen())
    Profiler->VisitStmt(C->getSafelen());
}

void OMPClauseProfiler::VisitOMPSimdlenClause(const OMPSimdlenClause *C) {
  if (C->getSimdlen())
    Profiler->VisitStmt(C->getSimdlen());
}

void OMPClauseProfiler::VisitOMPSizesClause(const OMPSizesClause *C) {
  for (auto *E : C->getSizesRefs())
    if (E)
      Profiler->VisitExpr(E);
}

void OMPClauseProfiler::VisitOMPFullClause(const OMPFullClause *C) {}

void OMPClauseProfiler::VisitOMPPartialClause(const OMPPartialClause *C) {
  if (const Expr *Factor = C->getFactor())
    Profiler->VisitExpr(Factor);
}

void OMPClauseProfiler::VisitOMPAllocatorClause(const OMPAllocatorClause *C) {
  if (C->getAllocator())
    Profiler->VisitStmt(C->getAllocator());
}

void OMPClauseProfiler::VisitOMPCollapseClause(const OMPCollapseClause *C) {
  if (C->getNumForLoops())
    Profiler->VisitStmt(C->getNumForLoops());
}

void OMPClauseProfiler::VisitOMPDetachClause(const OMPDetachClause *C) {
  if (Expr *Evt = C->getEventHandler())
    Profiler->VisitStmt(Evt);
}

void OMPClauseProfiler::VisitOMPNovariantsClause(const OMPNovariantsClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getCondition())
    Profiler->VisitStmt(C->getCondition());
}

void OMPClauseProfiler::VisitOMPNocontextClause(const OMPNocontextClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getCondition())
    Profiler->VisitStmt(C->getCondition());
}

void OMPClauseProfiler::VisitOMPDefaultClause(const OMPDefaultClause *C) { }

void OMPClauseProfiler::VisitOMPProcBindClause(const OMPProcBindClause *C) { }

void OMPClauseProfiler::VisitOMPUnifiedAddressClause(
    const OMPUnifiedAddressClause *C) {}

void OMPClauseProfiler::VisitOMPUnifiedSharedMemoryClause(
    const OMPUnifiedSharedMemoryClause *C) {}

void OMPClauseProfiler::VisitOMPReverseOffloadClause(
    const OMPReverseOffloadClause *C) {}

void OMPClauseProfiler::VisitOMPDynamicAllocatorsClause(
    const OMPDynamicAllocatorsClause *C) {}

void OMPClauseProfiler::VisitOMPAtomicDefaultMemOrderClause(
    const OMPAtomicDefaultMemOrderClause *C) {}

void OMPClauseProfiler::VisitOMPAtClause(const OMPAtClause *C) {}

void OMPClauseProfiler::VisitOMPSeverityClause(const OMPSeverityClause *C) {}

void OMPClauseProfiler::VisitOMPMessageClause(const OMPMessageClause *C) {
  if (C->getMessageString())
    Profiler->VisitStmt(C->getMessageString());
}

void OMPClauseProfiler::VisitOMPScheduleClause(const OMPScheduleClause *C) {
  VistOMPClauseWithPreInit(C);
  if (auto *S = C->getChunkSize())
    Profiler->VisitStmt(S);
}

void OMPClauseProfiler::VisitOMPOrderedClause(const OMPOrderedClause *C) {
  if (auto *Num = C->getNumForLoops())
    Profiler->VisitStmt(Num);
}

void OMPClauseProfiler::VisitOMPNowaitClause(const OMPNowaitClause *) {}

void OMPClauseProfiler::VisitOMPUntiedClause(const OMPUntiedClause *) {}

void OMPClauseProfiler::VisitOMPMergeableClause(const OMPMergeableClause *) {}

void OMPClauseProfiler::VisitOMPReadClause(const OMPReadClause *) {}

void OMPClauseProfiler::VisitOMPWriteClause(const OMPWriteClause *) {}

void OMPClauseProfiler::VisitOMPUpdateClause(const OMPUpdateClause *) {}

void OMPClauseProfiler::VisitOMPCaptureClause(const OMPCaptureClause *) {}

void OMPClauseProfiler::VisitOMPCompareClause(const OMPCompareClause *) {}

void OMPClauseProfiler::VisitOMPFailClause(const OMPFailClause *) {}

void OMPClauseProfiler::VisitOMPSeqCstClause(const OMPSeqCstClause *) {}

void OMPClauseProfiler::VisitOMPAcqRelClause(const OMPAcqRelClause *) {}

void OMPClauseProfiler::VisitOMPAcquireClause(const OMPAcquireClause *) {}

void OMPClauseProfiler::VisitOMPReleaseClause(const OMPReleaseClause *) {}

void OMPClauseProfiler::VisitOMPRelaxedClause(const OMPRelaxedClause *) {}

void OMPClauseProfiler::VisitOMPWeakClause(const OMPWeakClause *) {}

void OMPClauseProfiler::VisitOMPThreadsClause(const OMPThreadsClause *) {}

void OMPClauseProfiler::VisitOMPSIMDClause(const OMPSIMDClause *) {}

void OMPClauseProfiler::VisitOMPNogroupClause(const OMPNogroupClause *) {}

void OMPClauseProfiler::VisitOMPInitClause(const OMPInitClause *C) {
  VisitOMPClauseList(C);
}

void OMPClauseProfiler::VisitOMPUseClause(const OMPUseClause *C) {
  if (C->getInteropVar())
    Profiler->VisitStmt(C->getInteropVar());
}

void OMPClauseProfiler::VisitOMPDestroyClause(const OMPDestroyClause *C) {
  if (C->getInteropVar())
    Profiler->VisitStmt(C->getInteropVar());
}

void OMPClauseProfiler::VisitOMPFilterClause(const OMPFilterClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getThreadID())
    Profiler->VisitStmt(C->getThreadID());
}

template<typename T>
void OMPClauseProfiler::VisitOMPClauseList(T *Node) {
  for (auto *E : Node->varlists()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}

void OMPClauseProfiler::VisitOMPPrivateClause(const OMPPrivateClause *C) {
  VisitOMPClauseList(C);
  for (auto *E : C->private_copies()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void
OMPClauseProfiler::VisitOMPFirstprivateClause(const OMPFirstprivateClause *C) {
  VisitOMPClauseList(C);
  VistOMPClauseWithPreInit(C);
  for (auto *E : C->private_copies()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->inits()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void
OMPClauseProfiler::VisitOMPLastprivateClause(const OMPLastprivateClause *C) {
  VisitOMPClauseList(C);
  VistOMPClauseWithPostUpdate(C);
  for (auto *E : C->source_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->destination_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->assignment_ops()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void OMPClauseProfiler::VisitOMPSharedClause(const OMPSharedClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPReductionClause(
                                         const OMPReductionClause *C) {
  Profiler->VisitNestedNameSpecifier(
      C->getQualifierLoc().getNestedNameSpecifier());
  Profiler->VisitName(C->getNameInfo().getName());
  VisitOMPClauseList(C);
  VistOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->lhs_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->rhs_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->reduction_ops()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  if (C->getModifier() == clang::OMPC_REDUCTION_inscan) {
    for (auto *E : C->copy_ops()) {
      if (E)
        Profiler->VisitStmt(E);
    }
    for (auto *E : C->copy_array_temps()) {
      if (E)
        Profiler->VisitStmt(E);
    }
    for (auto *E : C->copy_array_elems()) {
      if (E)
        Profiler->VisitStmt(E);
    }
  }
}
void OMPClauseProfiler::VisitOMPTaskReductionClause(
    const OMPTaskReductionClause *C) {
  Profiler->VisitNestedNameSpecifier(
      C->getQualifierLoc().getNestedNameSpecifier());
  Profiler->VisitName(C->getNameInfo().getName());
  VisitOMPClauseList(C);
  VistOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->lhs_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->rhs_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->reduction_ops()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void OMPClauseProfiler::VisitOMPInReductionClause(
    const OMPInReductionClause *C) {
  Profiler->VisitNestedNameSpecifier(
      C->getQualifierLoc().getNestedNameSpecifier());
  Profiler->VisitName(C->getNameInfo().getName());
  VisitOMPClauseList(C);
  VistOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->lhs_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->rhs_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->reduction_ops()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->taskgroup_descriptors()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void OMPClauseProfiler::VisitOMPLinearClause(const OMPLinearClause *C) {
  VisitOMPClauseList(C);
  VistOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->inits()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->updates()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->finals()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  if (C->getStep())
    Profiler->VisitStmt(C->getStep());
  if (C->getCalcStep())
    Profiler->VisitStmt(C->getCalcStep());
}
void OMPClauseProfiler::VisitOMPAlignedClause(const OMPAlignedClause *C) {
  VisitOMPClauseList(C);
  if (C->getAlignment())
    Profiler->VisitStmt(C->getAlignment());
}
void OMPClauseProfiler::VisitOMPCopyinClause(const OMPCopyinClause *C) {
  VisitOMPClauseList(C);
  for (auto *E : C->source_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->destination_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->assignment_ops()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void
OMPClauseProfiler::VisitOMPCopyprivateClause(const OMPCopyprivateClause *C) {
  VisitOMPClauseList(C);
  for (auto *E : C->source_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->destination_exprs()) {
    if (E)
      Profiler->VisitStmt(E);
  }
  for (auto *E : C->assignment_ops()) {
    if (E)
      Profiler->VisitStmt(E);
  }
}
void OMPClauseProfiler::VisitOMPFlushClause(const OMPFlushClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPDepobjClause(const OMPDepobjClause *C) {
  if (const Expr *Depobj = C->getDepobj())
    Profiler->VisitStmt(Depobj);
}
void OMPClauseProfiler::VisitOMPDependClause(const OMPDependClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPDeviceClause(const OMPDeviceClause *C) {
  if (C->getDevice())
    Profiler->VisitStmt(C->getDevice());
}
void OMPClauseProfiler::VisitOMPMapClause(const OMPMapClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPAllocateClause(const OMPAllocateClause *C) {
  if (Expr *Allocator = C->getAllocator())
    Profiler->VisitStmt(Allocator);
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPNumTeamsClause(const OMPNumTeamsClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getNumTeams())
    Profiler->VisitStmt(C->getNumTeams());
}
void OMPClauseProfiler::VisitOMPThreadLimitClause(
    const OMPThreadLimitClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getThreadLimit())
    Profiler->VisitStmt(C->getThreadLimit());
}
void OMPClauseProfiler::VisitOMPPriorityClause(const OMPPriorityClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getPriority())
    Profiler->VisitStmt(C->getPriority());
}
void OMPClauseProfiler::VisitOMPGrainsizeClause(const OMPGrainsizeClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getGrainsize())
    Profiler->VisitStmt(C->getGrainsize());
}
void OMPClauseProfiler::VisitOMPNumTasksClause(const OMPNumTasksClause *C) {
  VistOMPClauseWithPreInit(C);
  if (C->getNumTasks())
    Profiler->VisitStmt(C->getNumTasks());
}
void OMPClauseProfiler::VisitOMPHintClause(const OMPHintClause *C) {
  if (C->getHint())
    Profiler->VisitStmt(C->getHint());
}
void OMPClauseProfiler::VisitOMPToClause(const OMPToClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPFromClause(const OMPFromClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPUseDevicePtrClause(
    const OMPUseDevicePtrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPUseDeviceAddrClause(
    const OMPUseDeviceAddrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPIsDevicePtrClause(
    const OMPIsDevicePtrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPHasDeviceAddrClause(
    const OMPHasDeviceAddrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPNontemporalClause(
    const OMPNontemporalClause *C) {
  VisitOMPClauseList(C);
  for (auto *E : C->private_refs())
    Profiler->VisitStmt(E);
}
void OMPClauseProfiler::VisitOMPInclusiveClause(const OMPInclusiveClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPExclusiveClause(const OMPExclusiveClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPUsesAllocatorsClause(
    const OMPUsesAllocatorsClause *C) {
  for (unsigned I = 0, E = C->getNumberOfAllocators(); I < E; ++I) {
    OMPUsesAllocatorsClause::Data D = C->getAllocatorData(I);
    Profiler->VisitStmt(D.Allocator);
    if (D.AllocatorTraits)
      Profiler->VisitStmt(D.AllocatorTraits);
  }
}
void OMPClauseProfiler::VisitOMPAffinityClause(const OMPAffinityClause *C) {
  if (const Expr *Modifier = C->getModifier())
    Profiler->VisitStmt(Modifier);
  for (const Expr *E : C->varlists())
    Profiler->VisitStmt(E);
}
void OMPClauseProfiler::VisitOMPOrderClause(const OMPOrderClause *C) {}
void OMPClauseProfiler::VisitOMPBindClause(const OMPBindClause *C) {}
void OMPClauseProfiler::VisitOMPXDynCGroupMemClause(
    const OMPXDynCGroupMemClause *C) {
  VistOMPClauseWithPreInit(C);
  if (Expr *Size = C->getSize())
    Profiler->VisitStmt(Size);
}
void OMPClauseProfiler::VisitOMPDoacrossClause(const OMPDoacrossClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseProfiler::VisitOMPXAttributeClause(const OMPXAttributeClause *C) {
}
void OMPClauseProfiler::VisitOMPXBareClause(const OMPXBareClause *C) {}
} // namespace

void
StmtProfiler::VisitOMPExecutableDirective(const OMPExecutableDirective *S) {
  VisitStmt(S);
  OMPClauseProfiler P(this);
  ArrayRef<OMPClause *> Clauses = S->clauses();
  for (ArrayRef<OMPClause *>::iterator I = Clauses.begin(), E = Clauses.end();
       I != E; ++I)
    if (*I)
      P.Visit(*I);
}

void StmtProfiler::VisitOMPCanonicalLoop(const OMPCanonicalLoop *L) {
  VisitStmt(L);
}

void StmtProfiler::VisitOMPLoopBasedDirective(const OMPLoopBasedDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPLoopDirective(const OMPLoopDirective *S) {
  VisitOMPLoopBasedDirective(S);
}

void StmtProfiler::VisitOMPMetaDirective(const OMPMetaDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPParallelDirective(const OMPParallelDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPSimdDirective(const OMPSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPLoopTransformationDirective(
    const OMPLoopTransformationDirective *S) {
  VisitOMPLoopBasedDirective(S);
}

void StmtProfiler::VisitOMPTileDirective(const OMPTileDirective *S) {
  VisitOMPLoopTransformationDirective(S);
}

void StmtProfiler::VisitOMPUnrollDirective(const OMPUnrollDirective *S) {
  VisitOMPLoopTransformationDirective(S);
}

void StmtProfiler::VisitOMPReverseDirective(const OMPReverseDirective *S) {
  VisitOMPLoopTransformationDirective(S);
}

void StmtProfiler::VisitOMPInterchangeDirective(
    const OMPInterchangeDirective *S) {
  VisitOMPLoopTransformationDirective(S);
}

void StmtProfiler::VisitOMPForDirective(const OMPForDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPForSimdDirective(const OMPForSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPSectionsDirective(const OMPSectionsDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPSectionDirective(const OMPSectionDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPScopeDirective(const OMPScopeDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPSingleDirective(const OMPSingleDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPMasterDirective(const OMPMasterDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPCriticalDirective(const OMPCriticalDirective *S) {
  VisitOMPExecutableDirective(S);
  VisitName(S->getDirectiveName().getName());
}

void
StmtProfiler::VisitOMPParallelForDirective(const OMPParallelForDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelForSimdDirective(
    const OMPParallelForSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelMasterDirective(
    const OMPParallelMasterDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPParallelMaskedDirective(
    const OMPParallelMaskedDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPParallelSectionsDirective(
    const OMPParallelSectionsDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTaskDirective(const OMPTaskDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTaskyieldDirective(const OMPTaskyieldDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPBarrierDirective(const OMPBarrierDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTaskwaitDirective(const OMPTaskwaitDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPErrorDirective(const OMPErrorDirective *S) {
  VisitOMPExecutableDirective(S);
}
void StmtProfiler::VisitOMPTaskgroupDirective(const OMPTaskgroupDirective *S) {
  VisitOMPExecutableDirective(S);
  if (const Expr *E = S->getReductionRef())
    VisitStmt(E);
}

void StmtProfiler::VisitOMPFlushDirective(const OMPFlushDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPDepobjDirective(const OMPDepobjDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPScanDirective(const OMPScanDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPOrderedDirective(const OMPOrderedDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPAtomicDirective(const OMPAtomicDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetDirective(const OMPTargetDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetDataDirective(const OMPTargetDataDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetEnterDataDirective(
    const OMPTargetEnterDataDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetExitDataDirective(
    const OMPTargetExitDataDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetParallelDirective(
    const OMPTargetParallelDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetParallelForDirective(
    const OMPTargetParallelForDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTeamsDirective(const OMPTeamsDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPCancellationPointDirective(
    const OMPCancellationPointDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPCancelDirective(const OMPCancelDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTaskLoopDirective(const OMPTaskLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTaskLoopSimdDirective(
    const OMPTaskLoopSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPMasterTaskLoopDirective(
    const OMPMasterTaskLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPMaskedTaskLoopDirective(
    const OMPMaskedTaskLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPMasterTaskLoopSimdDirective(
    const OMPMasterTaskLoopSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPMaskedTaskLoopSimdDirective(
    const OMPMaskedTaskLoopSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelMasterTaskLoopDirective(
    const OMPParallelMasterTaskLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelMaskedTaskLoopDirective(
    const OMPParallelMaskedTaskLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelMasterTaskLoopSimdDirective(
    const OMPParallelMasterTaskLoopSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelMaskedTaskLoopSimdDirective(
    const OMPParallelMaskedTaskLoopSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPDistributeDirective(
    const OMPDistributeDirective *S) {
  VisitOMPLoopDirective(S);
}

void OMPClauseProfiler::VisitOMPDistScheduleClause(
    const OMPDistScheduleClause *C) {
  VistOMPClauseWithPreInit(C);
  if (auto *S = C->getChunkSize())
    Profiler->VisitStmt(S);
}

void OMPClauseProfiler::VisitOMPDefaultmapClause(const OMPDefaultmapClause *) {}

void StmtProfiler::VisitOMPTargetUpdateDirective(
    const OMPTargetUpdateDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPDistributeParallelForDirective(
    const OMPDistributeParallelForDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPDistributeParallelForSimdDirective(
    const OMPDistributeParallelForSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPDistributeSimdDirective(
    const OMPDistributeSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetParallelForSimdDirective(
    const OMPTargetParallelForSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetSimdDirective(
    const OMPTargetSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTeamsDistributeDirective(
    const OMPTeamsDistributeDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTeamsDistributeSimdDirective(
    const OMPTeamsDistributeSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTeamsDistributeParallelForSimdDirective(
    const OMPTeamsDistributeParallelForSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTeamsDistributeParallelForDirective(
    const OMPTeamsDistributeParallelForDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetTeamsDirective(
    const OMPTargetTeamsDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPTargetTeamsDistributeDirective(
    const OMPTargetTeamsDistributeDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetTeamsDistributeParallelForDirective(
    const OMPTargetTeamsDistributeParallelForDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetTeamsDistributeParallelForSimdDirective(
    const OMPTargetTeamsDistributeParallelForSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetTeamsDistributeSimdDirective(
    const OMPTargetTeamsDistributeSimdDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPInteropDirective(const OMPInteropDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPDispatchDirective(const OMPDispatchDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPMaskedDirective(const OMPMaskedDirective *S) {
  VisitOMPExecutableDirective(S);
}

void StmtProfiler::VisitOMPGenericLoopDirective(
    const OMPGenericLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTeamsGenericLoopDirective(
    const OMPTeamsGenericLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetTeamsGenericLoopDirective(
    const OMPTargetTeamsGenericLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPParallelGenericLoopDirective(
    const OMPParallelGenericLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitOMPTargetParallelGenericLoopDirective(
    const OMPTargetParallelGenericLoopDirective *S) {
  VisitOMPLoopDirective(S);
}

void StmtProfiler::VisitExpr(const Expr *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitConstantExpr(const ConstantExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitDeclRefExpr(const DeclRefExpr *S) {
  VisitExpr(S);
  if (!Canonical)
    VisitNestedNameSpecifier(S->getQualifier());
  VisitDecl(S->getDecl());
  if (!Canonical) {
    ID.AddBoolean(S->hasExplicitTemplateArgs());
    if (S->hasExplicitTemplateArgs())
      VisitTemplateArguments(S->getTemplateArgs(), S->getNumTemplateArgs());
  }
}

void StmtProfiler::VisitSYCLUniqueStableNameExpr(
    const SYCLUniqueStableNameExpr *S) {
  VisitExpr(S);
  VisitType(S->getTypeSourceInfo()->getType());
}

void StmtProfiler::VisitPredefinedExpr(const PredefinedExpr *S) {
  VisitExpr(S);
  ID.AddInteger(llvm::to_underlying(S->getIdentKind()));
}

void StmtProfiler::VisitIntegerLiteral(const IntegerLiteral *S) {
  VisitExpr(S);
  S->getValue().Profile(ID);

  QualType T = S->getType();
  if (Canonical)
    T = T.getCanonicalType();
  ID.AddInteger(T->getTypeClass());
  if (auto BitIntT = T->getAs<BitIntType>())
    BitIntT->Profile(ID);
  else
    ID.AddInteger(T->castAs<BuiltinType>()->getKind());
}

void StmtProfiler::VisitFixedPointLiteral(const FixedPointLiteral *S) {
  VisitExpr(S);
  S->getValue().Profile(ID);
  ID.AddInteger(S->getType()->castAs<BuiltinType>()->getKind());
}

void StmtProfiler::VisitCharacterLiteral(const CharacterLiteral *S) {
  VisitExpr(S);
  ID.AddInteger(llvm::to_underlying(S->getKind()));
  ID.AddInteger(S->getValue());
}

void StmtProfiler::VisitFloatingLiteral(const FloatingLiteral *S) {
  VisitExpr(S);
  S->getValue().Profile(ID);
  ID.AddBoolean(S->isExact());
  ID.AddInteger(S->getType()->castAs<BuiltinType>()->getKind());
}

void StmtProfiler::VisitImaginaryLiteral(const ImaginaryLiteral *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitStringLiteral(const StringLiteral *S) {
  VisitExpr(S);
  ID.AddString(S->getBytes());
  ID.AddInteger(llvm::to_underlying(S->getKind()));
}

void StmtProfiler::VisitParenExpr(const ParenExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitParenListExpr(const ParenListExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitUnaryOperator(const UnaryOperator *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOpcode());
}

void StmtProfiler::VisitOffsetOfExpr(const OffsetOfExpr *S) {
  VisitType(S->getTypeSourceInfo()->getType());
  unsigned n = S->getNumComponents();
  for (unsigned i = 0; i < n; ++i) {
    const OffsetOfNode &ON = S->getComponent(i);
    ID.AddInteger(ON.getKind());
    switch (ON.getKind()) {
    case OffsetOfNode::Array:
      // Expressions handled below.
      break;

    case OffsetOfNode::Field:
      VisitDecl(ON.getField());
      break;

    case OffsetOfNode::Identifier:
      VisitIdentifierInfo(ON.getFieldName());
      break;

    case OffsetOfNode::Base:
      // These nodes are implicit, and therefore don't need profiling.
      break;
    }
  }

  VisitExpr(S);
}

void
StmtProfiler::VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getKind());
  if (S->isArgumentType())
    VisitType(S->getArgumentType());
}

void StmtProfiler::VisitArraySubscriptExpr(const ArraySubscriptExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitMatrixSubscriptExpr(const MatrixSubscriptExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitArraySectionExpr(const ArraySectionExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitOMPArrayShapingExpr(const OMPArrayShapingExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitOMPIteratorExpr(const OMPIteratorExpr *S) {
  VisitExpr(S);
  for (unsigned I = 0, E = S->numOfIterators(); I < E; ++I)
    VisitDecl(S->getIteratorDecl(I));
}

void StmtProfiler::VisitCallExpr(const CallExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitMemberExpr(const MemberExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getMemberDecl());
  if (!Canonical)
    VisitNestedNameSpecifier(S->getQualifier());
  ID.AddBoolean(S->isArrow());
}

void StmtProfiler::VisitCompoundLiteralExpr(const CompoundLiteralExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->isFileScope());
}

void StmtProfiler::VisitCastExpr(const CastExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitImplicitCastExpr(const ImplicitCastExpr *S) {
  VisitCastExpr(S);
  ID.AddInteger(S->getValueKind());
}

void StmtProfiler::VisitExplicitCastExpr(const ExplicitCastExpr *S) {
  VisitCastExpr(S);
  VisitType(S->getTypeAsWritten());
}

void StmtProfiler::VisitCStyleCastExpr(const CStyleCastExpr *S) {
  VisitExplicitCastExpr(S);
}

void StmtProfiler::VisitBinaryOperator(const BinaryOperator *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOpcode());
}

void
StmtProfiler::VisitCompoundAssignOperator(const CompoundAssignOperator *S) {
  VisitBinaryOperator(S);
}

void StmtProfiler::VisitConditionalOperator(const ConditionalOperator *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitBinaryConditionalOperator(
    const BinaryConditionalOperator *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitAddrLabelExpr(const AddrLabelExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getLabel());
}

void StmtProfiler::VisitStmtExpr(const StmtExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitShuffleVectorExpr(const ShuffleVectorExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitConvertVectorExpr(const ConvertVectorExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitChooseExpr(const ChooseExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitGNUNullExpr(const GNUNullExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitVAArgExpr(const VAArgExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitInitListExpr(const InitListExpr *S) {
  if (S->getSyntacticForm()) {
    VisitInitListExpr(S->getSyntacticForm());
    return;
  }

  VisitExpr(S);
}

void StmtProfiler::VisitDesignatedInitExpr(const DesignatedInitExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->usesGNUSyntax());
  for (const DesignatedInitExpr::Designator &D : S->designators()) {
    if (D.isFieldDesignator()) {
      ID.AddInteger(0);
      VisitName(D.getFieldName());
      continue;
    }

    if (D.isArrayDesignator()) {
      ID.AddInteger(1);
    } else {
      assert(D.isArrayRangeDesignator());
      ID.AddInteger(2);
    }
    ID.AddInteger(D.getArrayIndex());
  }
}

// Seems that if VisitInitListExpr() only works on the syntactic form of an
// InitListExpr, then a DesignatedInitUpdateExpr is not encountered.
void StmtProfiler::VisitDesignatedInitUpdateExpr(
    const DesignatedInitUpdateExpr *S) {
  llvm_unreachable("Unexpected DesignatedInitUpdateExpr in syntactic form of "
                   "initializer");
}

void StmtProfiler::VisitArrayInitLoopExpr(const ArrayInitLoopExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitArrayInitIndexExpr(const ArrayInitIndexExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitNoInitExpr(const NoInitExpr *S) {
  llvm_unreachable("Unexpected NoInitExpr in syntactic form of initializer");
}

void StmtProfiler::VisitImplicitValueInitExpr(const ImplicitValueInitExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitExtVectorElementExpr(const ExtVectorElementExpr *S) {
  VisitExpr(S);
  VisitName(&S->getAccessor());
}

void StmtProfiler::VisitBlockExpr(const BlockExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getBlockDecl());
}

void StmtProfiler::VisitGenericSelectionExpr(const GenericSelectionExpr *S) {
  VisitExpr(S);
  for (const GenericSelectionExpr::ConstAssociation Assoc :
       S->associations()) {
    QualType T = Assoc.getType();
    if (T.isNull())
      ID.AddPointer(nullptr);
    else
      VisitType(T);
    VisitExpr(Assoc.getAssociationExpr());
  }
}

void StmtProfiler::VisitPseudoObjectExpr(const PseudoObjectExpr *S) {
  VisitExpr(S);
  for (PseudoObjectExpr::const_semantics_iterator
         i = S->semantics_begin(), e = S->semantics_end(); i != e; ++i)
    // Normally, we would not profile the source expressions of OVEs.
    if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(*i))
      Visit(OVE->getSourceExpr());
}

void StmtProfiler::VisitAtomicExpr(const AtomicExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOp());
}

void StmtProfiler::VisitConceptSpecializationExpr(
                                           const ConceptSpecializationExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getNamedConcept());
  for (const TemplateArgument &Arg : S->getTemplateArguments())
    VisitTemplateArgument(Arg);
}

void StmtProfiler::VisitRequiresExpr(const RequiresExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getLocalParameters().size());
  for (ParmVarDecl *LocalParam : S->getLocalParameters())
    VisitDecl(LocalParam);
  ID.AddInteger(S->getRequirements().size());
  for (concepts::Requirement *Req : S->getRequirements()) {
    if (auto *TypeReq = dyn_cast<concepts::TypeRequirement>(Req)) {
      ID.AddInteger(concepts::Requirement::RK_Type);
      ID.AddBoolean(TypeReq->isSubstitutionFailure());
      if (!TypeReq->isSubstitutionFailure())
        VisitType(TypeReq->getType()->getType());
    } else if (auto *ExprReq = dyn_cast<concepts::ExprRequirement>(Req)) {
      ID.AddInteger(concepts::Requirement::RK_Compound);
      ID.AddBoolean(ExprReq->isExprSubstitutionFailure());
      if (!ExprReq->isExprSubstitutionFailure())
        Visit(ExprReq->getExpr());
      // C++2a [expr.prim.req.compound]p1 Example:
      //    [...] The compound-requirement in C1 requires that x++ is a valid
      //    expression. It is equivalent to the simple-requirement x++; [...]
      // We therefore do not profile isSimple() here.
      ID.AddBoolean(ExprReq->getNoexceptLoc().isValid());
      const concepts::ExprRequirement::ReturnTypeRequirement &RetReq =
          ExprReq->getReturnTypeRequirement();
      if (RetReq.isEmpty()) {
        ID.AddInteger(0);
      } else if (RetReq.isTypeConstraint()) {
        ID.AddInteger(1);
        Visit(RetReq.getTypeConstraint()->getImmediatelyDeclaredConstraint());
      } else {
        assert(RetReq.isSubstitutionFailure());
        ID.AddInteger(2);
      }
    } else {
      ID.AddInteger(concepts::Requirement::RK_Nested);
      auto *NestedReq = cast<concepts::NestedRequirement>(Req);
      ID.AddBoolean(NestedReq->hasInvalidConstraint());
      if (!NestedReq->hasInvalidConstraint())
        Visit(NestedReq->getConstraintExpr());
    }
  }
}

static Stmt::StmtClass DecodeOperatorCall(const CXXOperatorCallExpr *S,
                                          UnaryOperatorKind &UnaryOp,
                                          BinaryOperatorKind &BinaryOp,
                                          unsigned &NumArgs) {
  switch (S->getOperator()) {
  case OO_None:
  case OO_New:
  case OO_Delete:
  case OO_Array_New:
  case OO_Array_Delete:
  case OO_Arrow:
  case OO_Conditional:
  case NUM_OVERLOADED_OPERATORS:
    llvm_unreachable("Invalid operator call kind");

  case OO_Plus:
    if (NumArgs == 1) {
      UnaryOp = UO_Plus;
      return Stmt::UnaryOperatorClass;
    }

    BinaryOp = BO_Add;
    return Stmt::BinaryOperatorClass;

  case OO_Minus:
    if (NumArgs == 1) {
      UnaryOp = UO_Minus;
      return Stmt::UnaryOperatorClass;
    }

    BinaryOp = BO_Sub;
    return Stmt::BinaryOperatorClass;

  case OO_Star:
    if (NumArgs == 1) {
      UnaryOp = UO_Deref;
      return Stmt::UnaryOperatorClass;
    }

    BinaryOp = BO_Mul;
    return Stmt::BinaryOperatorClass;

  case OO_Slash:
    BinaryOp = BO_Div;
    return Stmt::BinaryOperatorClass;

  case OO_Percent:
    BinaryOp = BO_Rem;
    return Stmt::BinaryOperatorClass;

  case OO_Caret:
    BinaryOp = BO_Xor;
    return Stmt::BinaryOperatorClass;

  case OO_Amp:
    if (NumArgs == 1) {
      UnaryOp = UO_AddrOf;
      return Stmt::UnaryOperatorClass;
    }

    BinaryOp = BO_And;
    return Stmt::BinaryOperatorClass;

  case OO_Pipe:
    BinaryOp = BO_Or;
    return Stmt::BinaryOperatorClass;

  case OO_Tilde:
    UnaryOp = UO_Not;
    return Stmt::UnaryOperatorClass;

  case OO_Exclaim:
    UnaryOp = UO_LNot;
    return Stmt::UnaryOperatorClass;

  case OO_Equal:
    BinaryOp = BO_Assign;
    return Stmt::BinaryOperatorClass;

  case OO_Less:
    BinaryOp = BO_LT;
    return Stmt::BinaryOperatorClass;

  case OO_Greater:
    BinaryOp = BO_GT;
    return Stmt::BinaryOperatorClass;

  case OO_PlusEqual:
    BinaryOp = BO_AddAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_MinusEqual:
    BinaryOp = BO_SubAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_StarEqual:
    BinaryOp = BO_MulAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_SlashEqual:
    BinaryOp = BO_DivAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_PercentEqual:
    BinaryOp = BO_RemAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_CaretEqual:
    BinaryOp = BO_XorAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_AmpEqual:
    BinaryOp = BO_AndAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_PipeEqual:
    BinaryOp = BO_OrAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_LessLess:
    BinaryOp = BO_Shl;
    return Stmt::BinaryOperatorClass;

  case OO_GreaterGreater:
    BinaryOp = BO_Shr;
    return Stmt::BinaryOperatorClass;

  case OO_LessLessEqual:
    BinaryOp = BO_ShlAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_GreaterGreaterEqual:
    BinaryOp = BO_ShrAssign;
    return Stmt::CompoundAssignOperatorClass;

  case OO_EqualEqual:
    BinaryOp = BO_EQ;
    return Stmt::BinaryOperatorClass;

  case OO_ExclaimEqual:
    BinaryOp = BO_NE;
    return Stmt::BinaryOperatorClass;

  case OO_LessEqual:
    BinaryOp = BO_LE;
    return Stmt::BinaryOperatorClass;

  case OO_GreaterEqual:
    BinaryOp = BO_GE;
    return Stmt::BinaryOperatorClass;

  case OO_Spaceship:
    BinaryOp = BO_Cmp;
    return Stmt::BinaryOperatorClass;

  case OO_AmpAmp:
    BinaryOp = BO_LAnd;
    return Stmt::BinaryOperatorClass;

  case OO_PipePipe:
    BinaryOp = BO_LOr;
    return Stmt::BinaryOperatorClass;

  case OO_PlusPlus:
    UnaryOp = NumArgs == 1 ? UO_PreInc : UO_PostInc;
    NumArgs = 1;
    return Stmt::UnaryOperatorClass;

  case OO_MinusMinus:
    UnaryOp = NumArgs == 1 ? UO_PreDec : UO_PostDec;
    NumArgs = 1;
    return Stmt::UnaryOperatorClass;

  case OO_Comma:
    BinaryOp = BO_Comma;
    return Stmt::BinaryOperatorClass;

  case OO_ArrowStar:
    BinaryOp = BO_PtrMemI;
    return Stmt::BinaryOperatorClass;

  case OO_Subscript:
    return Stmt::ArraySubscriptExprClass;

  case OO_Call:
    return Stmt::CallExprClass;

  case OO_Coawait:
    UnaryOp = UO_Coawait;
    return Stmt::UnaryOperatorClass;
  }

  llvm_unreachable("Invalid overloaded operator expression");
}

#if defined(_MSC_VER) && !defined(__clang__)
#if _MSC_VER == 1911
// Work around https://developercommunity.visualstudio.com/content/problem/84002/clang-cl-when-built-with-vc-2017-crashes-cause-vc.html
// MSVC 2017 update 3 miscompiles this function, and a clang built with it
// will crash in stage 2 of a bootstrap build.
#pragma optimize("", off)
#endif
#endif

void StmtProfiler::VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *S) {
  if (S->isTypeDependent()) {
    // Type-dependent operator calls are profiled like their underlying
    // syntactic operator.
    //
    // An operator call to operator-> is always implicit, so just skip it. The
    // enclosing MemberExpr will profile the actual member access.
    if (S->getOperator() == OO_Arrow)
      return Visit(S->getArg(0));

    UnaryOperatorKind UnaryOp = UO_Extension;
    BinaryOperatorKind BinaryOp = BO_Comma;
    unsigned NumArgs = S->getNumArgs();
    Stmt::StmtClass SC = DecodeOperatorCall(S, UnaryOp, BinaryOp, NumArgs);

    ID.AddInteger(SC);
    for (unsigned I = 0; I != NumArgs; ++I)
      Visit(S->getArg(I));
    if (SC == Stmt::UnaryOperatorClass)
      ID.AddInteger(UnaryOp);
    else if (SC == Stmt::BinaryOperatorClass ||
             SC == Stmt::CompoundAssignOperatorClass)
      ID.AddInteger(BinaryOp);
    else
      assert(SC == Stmt::ArraySubscriptExprClass || SC == Stmt::CallExprClass);

    return;
  }

  VisitCallExpr(S);
  ID.AddInteger(S->getOperator());
}

void StmtProfiler::VisitCXXRewrittenBinaryOperator(
    const CXXRewrittenBinaryOperator *S) {
  // If a rewritten operator were ever to be type-dependent, we should profile
  // it following its syntactic operator.
  assert(!S->isTypeDependent() &&
         "resolved rewritten operator should never be type-dependent");
  ID.AddBoolean(S->isReversed());
  VisitExpr(S->getSemanticForm());
}

#if defined(_MSC_VER) && !defined(__clang__)
#if _MSC_VER == 1911
#pragma optimize("", on)
#endif
#endif

void StmtProfiler::VisitCXXMemberCallExpr(const CXXMemberCallExpr *S) {
  VisitCallExpr(S);
}

void StmtProfiler::VisitCUDAKernelCallExpr(const CUDAKernelCallExpr *S) {
  VisitCallExpr(S);
}

void StmtProfiler::VisitAsTypeExpr(const AsTypeExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXNamedCastExpr(const CXXNamedCastExpr *S) {
  VisitExplicitCastExpr(S);
}

void StmtProfiler::VisitCXXStaticCastExpr(const CXXStaticCastExpr *S) {
  VisitCXXNamedCastExpr(S);
}

void StmtProfiler::VisitCXXDynamicCastExpr(const CXXDynamicCastExpr *S) {
  VisitCXXNamedCastExpr(S);
}

void
StmtProfiler::VisitCXXReinterpretCastExpr(const CXXReinterpretCastExpr *S) {
  VisitCXXNamedCastExpr(S);
}

void StmtProfiler::VisitCXXConstCastExpr(const CXXConstCastExpr *S) {
  VisitCXXNamedCastExpr(S);
}

void StmtProfiler::VisitBuiltinBitCastExpr(const BuiltinBitCastExpr *S) {
  VisitExpr(S);
  VisitType(S->getTypeInfoAsWritten()->getType());
}

void StmtProfiler::VisitCXXAddrspaceCastExpr(const CXXAddrspaceCastExpr *S) {
  VisitCXXNamedCastExpr(S);
}

void StmtProfiler::VisitUserDefinedLiteral(const UserDefinedLiteral *S) {
  VisitCallExpr(S);
}

void StmtProfiler::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->getValue());
}

void StmtProfiler::VisitCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXStdInitializerListExpr(
    const CXXStdInitializerListExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXTypeidExpr(const CXXTypeidExpr *S) {
  VisitExpr(S);
  if (S->isTypeOperand())
    VisitType(S->getTypeOperandSourceInfo()->getType());
}

void StmtProfiler::VisitCXXUuidofExpr(const CXXUuidofExpr *S) {
  VisitExpr(S);
  if (S->isTypeOperand())
    VisitType(S->getTypeOperandSourceInfo()->getType());
}

void StmtProfiler::VisitMSPropertyRefExpr(const MSPropertyRefExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getPropertyDecl());
}

void StmtProfiler::VisitMSPropertySubscriptExpr(
    const MSPropertySubscriptExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXThisExpr(const CXXThisExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->isImplicit());
  ID.AddBoolean(S->isCapturedByCopyInLambdaWithExplicitObjectParameter());
}

void StmtProfiler::VisitCXXThrowExpr(const CXXThrowExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getParam());
}

void StmtProfiler::VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getField());
}

void StmtProfiler::VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *S) {
  VisitExpr(S);
  VisitDecl(
         const_cast<CXXDestructorDecl *>(S->getTemporary()->getDestructor()));
}

void StmtProfiler::VisitCXXConstructExpr(const CXXConstructExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getConstructor());
  ID.AddBoolean(S->isElidable());
}

void StmtProfiler::VisitCXXInheritedCtorInitExpr(
    const CXXInheritedCtorInitExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getConstructor());
}

void StmtProfiler::VisitCXXFunctionalCastExpr(const CXXFunctionalCastExpr *S) {
  VisitExplicitCastExpr(S);
}

void
StmtProfiler::VisitCXXTemporaryObjectExpr(const CXXTemporaryObjectExpr *S) {
  VisitCXXConstructExpr(S);
}

void
StmtProfiler::VisitLambdaExpr(const LambdaExpr *S) {
  if (!ProfileLambdaExpr) {
    // Do not recursively visit the children of this expression. Profiling the
    // body would result in unnecessary work, and is not safe to do during
    // deserialization.
    VisitStmtNoChildren(S);

    // C++20 [temp.over.link]p5:
    //   Two lambda-expressions are never considered equivalent.
    VisitDecl(S->getLambdaClass());

    return;
  }

  CXXRecordDecl *Lambda = S->getLambdaClass();
  for (const auto &Capture : Lambda->captures()) {
    ID.AddInteger(Capture.getCaptureKind());
    if (Capture.capturesVariable())
      VisitDecl(Capture.getCapturedVar());
  }

  // Profiling the body of the lambda may be dangerous during deserialization.
  // So we'd like only to profile the signature here.
  ODRHash Hasher;
  // FIXME: We can't get the operator call easily by
  // `CXXRecordDecl::getLambdaCallOperator()` if we're in deserialization.
  // So we have to do something raw here.
  for (auto *SubDecl : Lambda->decls()) {
    FunctionDecl *Call = nullptr;
    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(SubDecl))
      Call = FTD->getTemplatedDecl();
    else if (auto *FD = dyn_cast<FunctionDecl>(SubDecl))
      Call = FD;

    if (!Call)
      continue;

    Hasher.AddFunctionDecl(Call, /*SkipBody=*/true);
  }
  ID.AddInteger(Hasher.CalculateHash());
}

void
StmtProfiler::VisitCXXScalarValueInitExpr(const CXXScalarValueInitExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXDeleteExpr(const CXXDeleteExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->isGlobalDelete());
  ID.AddBoolean(S->isArrayForm());
  VisitDecl(S->getOperatorDelete());
}

void StmtProfiler::VisitCXXNewExpr(const CXXNewExpr *S) {
  VisitExpr(S);
  VisitType(S->getAllocatedType());
  VisitDecl(S->getOperatorNew());
  VisitDecl(S->getOperatorDelete());
  ID.AddBoolean(S->isArray());
  ID.AddInteger(S->getNumPlacementArgs());
  ID.AddBoolean(S->isGlobalNew());
  ID.AddBoolean(S->isParenTypeId());
  ID.AddInteger(llvm::to_underlying(S->getInitializationStyle()));
}

void
StmtProfiler::VisitCXXPseudoDestructorExpr(const CXXPseudoDestructorExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->isArrow());
  VisitNestedNameSpecifier(S->getQualifier());
  ID.AddBoolean(S->getScopeTypeInfo() != nullptr);
  if (S->getScopeTypeInfo())
    VisitType(S->getScopeTypeInfo()->getType());
  ID.AddBoolean(S->getDestroyedTypeInfo() != nullptr);
  if (S->getDestroyedTypeInfo())
    VisitType(S->getDestroyedType());
  else
    VisitIdentifierInfo(S->getDestroyedTypeIdentifier());
}

void StmtProfiler::VisitOverloadExpr(const OverloadExpr *S) {
  VisitExpr(S);
  VisitNestedNameSpecifier(S->getQualifier());
  VisitName(S->getName(), /*TreatAsDecl*/ true);
  ID.AddBoolean(S->hasExplicitTemplateArgs());
  if (S->hasExplicitTemplateArgs())
    VisitTemplateArguments(S->getTemplateArgs(), S->getNumTemplateArgs());
}

void
StmtProfiler::VisitUnresolvedLookupExpr(const UnresolvedLookupExpr *S) {
  VisitOverloadExpr(S);
}

void StmtProfiler::VisitTypeTraitExpr(const TypeTraitExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getTrait());
  ID.AddInteger(S->getNumArgs());
  for (unsigned I = 0, N = S->getNumArgs(); I != N; ++I)
    VisitType(S->getArg(I)->getType());
}

void StmtProfiler::VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getTrait());
  VisitType(S->getQueriedType());
}

void StmtProfiler::VisitExpressionTraitExpr(const ExpressionTraitExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getTrait());
  VisitExpr(S->getQueriedExpression());
}

void StmtProfiler::VisitDependentScopeDeclRefExpr(
    const DependentScopeDeclRefExpr *S) {
  VisitExpr(S);
  VisitName(S->getDeclName());
  VisitNestedNameSpecifier(S->getQualifier());
  ID.AddBoolean(S->hasExplicitTemplateArgs());
  if (S->hasExplicitTemplateArgs())
    VisitTemplateArguments(S->getTemplateArgs(), S->getNumTemplateArgs());
}

void StmtProfiler::VisitExprWithCleanups(const ExprWithCleanups *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXUnresolvedConstructExpr(
    const CXXUnresolvedConstructExpr *S) {
  VisitExpr(S);
  VisitType(S->getTypeAsWritten());
  ID.AddInteger(S->isListInitialization());
}

void StmtProfiler::VisitCXXDependentScopeMemberExpr(
    const CXXDependentScopeMemberExpr *S) {
  ID.AddBoolean(S->isImplicitAccess());
  if (!S->isImplicitAccess()) {
    VisitExpr(S);
    ID.AddBoolean(S->isArrow());
  }
  VisitNestedNameSpecifier(S->getQualifier());
  VisitName(S->getMember());
  ID.AddBoolean(S->hasExplicitTemplateArgs());
  if (S->hasExplicitTemplateArgs())
    VisitTemplateArguments(S->getTemplateArgs(), S->getNumTemplateArgs());
}

void StmtProfiler::VisitUnresolvedMemberExpr(const UnresolvedMemberExpr *S) {
  ID.AddBoolean(S->isImplicitAccess());
  if (!S->isImplicitAccess()) {
    VisitExpr(S);
    ID.AddBoolean(S->isArrow());
  }
  VisitNestedNameSpecifier(S->getQualifier());
  VisitName(S->getMemberName());
  ID.AddBoolean(S->hasExplicitTemplateArgs());
  if (S->hasExplicitTemplateArgs())
    VisitTemplateArguments(S->getTemplateArgs(), S->getNumTemplateArgs());
}

void StmtProfiler::VisitCXXNoexceptExpr(const CXXNoexceptExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitPackExpansionExpr(const PackExpansionExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitSizeOfPackExpr(const SizeOfPackExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getPack());
  if (S->isPartiallySubstituted()) {
    auto Args = S->getPartialArguments();
    ID.AddInteger(Args.size());
    for (const auto &TA : Args)
      VisitTemplateArgument(TA);
  } else {
    ID.AddInteger(0);
  }
}

void StmtProfiler::VisitPackIndexingExpr(const PackIndexingExpr *E) {
  VisitExpr(E);
  VisitExpr(E->getPackIdExpression());
  VisitExpr(E->getIndexExpr());
}

void StmtProfiler::VisitSubstNonTypeTemplateParmPackExpr(
    const SubstNonTypeTemplateParmPackExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getParameterPack());
  VisitTemplateArgument(S->getArgumentPack());
}

void StmtProfiler::VisitSubstNonTypeTemplateParmExpr(
    const SubstNonTypeTemplateParmExpr *E) {
  // Profile exactly as the replacement expression.
  Visit(E->getReplacement());
}

void StmtProfiler::VisitFunctionParmPackExpr(const FunctionParmPackExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getParameterPack());
  ID.AddInteger(S->getNumExpansions());
  for (FunctionParmPackExpr::iterator I = S->begin(), E = S->end(); I != E; ++I)
    VisitDecl(*I);
}

void StmtProfiler::VisitMaterializeTemporaryExpr(
                                           const MaterializeTemporaryExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCXXFoldExpr(const CXXFoldExpr *S) {
  VisitExpr(S);
  ID.AddInteger(S->getOperator());
}

void StmtProfiler::VisitCXXParenListInitExpr(const CXXParenListInitExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCoroutineBodyStmt(const CoroutineBodyStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitCoreturnStmt(const CoreturnStmt *S) {
  VisitStmt(S);
}

void StmtProfiler::VisitCoawaitExpr(const CoawaitExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitDependentCoawaitExpr(const DependentCoawaitExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitCoyieldExpr(const CoyieldExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitOpaqueValueExpr(const OpaqueValueExpr *E) {
  VisitExpr(E);
}

void StmtProfiler::VisitTypoExpr(const TypoExpr *E) {
  VisitExpr(E);
}

void StmtProfiler::VisitSourceLocExpr(const SourceLocExpr *E) {
  VisitExpr(E);
}

void StmtProfiler::VisitEmbedExpr(const EmbedExpr *E) { VisitExpr(E); }

void StmtProfiler::VisitRecoveryExpr(const RecoveryExpr *E) { VisitExpr(E); }

void StmtProfiler::VisitObjCStringLiteral(const ObjCStringLiteral *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitObjCBoxedExpr(const ObjCBoxedExpr *E) {
  VisitExpr(E);
}

void StmtProfiler::VisitObjCArrayLiteral(const ObjCArrayLiteral *E) {
  VisitExpr(E);
}

void StmtProfiler::VisitObjCDictionaryLiteral(const ObjCDictionaryLiteral *E) {
  VisitExpr(E);
}

void StmtProfiler::VisitObjCEncodeExpr(const ObjCEncodeExpr *S) {
  VisitExpr(S);
  VisitType(S->getEncodedType());
}

void StmtProfiler::VisitObjCSelectorExpr(const ObjCSelectorExpr *S) {
  VisitExpr(S);
  VisitName(S->getSelector());
}

void StmtProfiler::VisitObjCProtocolExpr(const ObjCProtocolExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getProtocol());
}

void StmtProfiler::VisitObjCIvarRefExpr(const ObjCIvarRefExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getDecl());
  ID.AddBoolean(S->isArrow());
  ID.AddBoolean(S->isFreeIvar());
}

void StmtProfiler::VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *S) {
  VisitExpr(S);
  if (S->isImplicitProperty()) {
    VisitDecl(S->getImplicitPropertyGetter());
    VisitDecl(S->getImplicitPropertySetter());
  } else {
    VisitDecl(S->getExplicitProperty());
  }
  if (S->isSuperReceiver()) {
    ID.AddBoolean(S->isSuperReceiver());
    VisitType(S->getSuperReceiverType());
  }
}

void StmtProfiler::VisitObjCSubscriptRefExpr(const ObjCSubscriptRefExpr *S) {
  VisitExpr(S);
  VisitDecl(S->getAtIndexMethodDecl());
  VisitDecl(S->setAtIndexMethodDecl());
}

void StmtProfiler::VisitObjCMessageExpr(const ObjCMessageExpr *S) {
  VisitExpr(S);
  VisitName(S->getSelector());
  VisitDecl(S->getMethodDecl());
}

void StmtProfiler::VisitObjCIsaExpr(const ObjCIsaExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->isArrow());
}

void StmtProfiler::VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->getValue());
}

void StmtProfiler::VisitObjCIndirectCopyRestoreExpr(
    const ObjCIndirectCopyRestoreExpr *S) {
  VisitExpr(S);
  ID.AddBoolean(S->shouldCopy());
}

void StmtProfiler::VisitObjCBridgedCastExpr(const ObjCBridgedCastExpr *S) {
  VisitExplicitCastExpr(S);
  ID.AddBoolean(S->getBridgeKind());
}

void StmtProfiler::VisitObjCAvailabilityCheckExpr(
    const ObjCAvailabilityCheckExpr *S) {
  VisitExpr(S);
}

void StmtProfiler::VisitTemplateArguments(const TemplateArgumentLoc *Args,
                                          unsigned NumArgs) {
  ID.AddInteger(NumArgs);
  for (unsigned I = 0; I != NumArgs; ++I)
    VisitTemplateArgument(Args[I].getArgument());
}

void StmtProfiler::VisitTemplateArgument(const TemplateArgument &Arg) {
  // Mostly repetitive with TemplateArgument::Profile!
  ID.AddInteger(Arg.getKind());
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    break;

  case TemplateArgument::Type:
    VisitType(Arg.getAsType());
    break;

  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
    VisitTemplateName(Arg.getAsTemplateOrTemplatePattern());
    break;

  case TemplateArgument::Declaration:
    VisitType(Arg.getParamTypeForDecl());
    // FIXME: Do we need to recursively decompose template parameter objects?
    VisitDecl(Arg.getAsDecl());
    break;

  case TemplateArgument::NullPtr:
    VisitType(Arg.getNullPtrType());
    break;

  case TemplateArgument::Integral:
    VisitType(Arg.getIntegralType());
    Arg.getAsIntegral().Profile(ID);
    break;

  case TemplateArgument::StructuralValue:
    VisitType(Arg.getStructuralValueType());
    // FIXME: Do we need to recursively decompose this ourselves?
    Arg.getAsStructuralValue().Profile(ID);
    break;

  case TemplateArgument::Expression:
    Visit(Arg.getAsExpr());
    break;

  case TemplateArgument::Pack:
    for (const auto &P : Arg.pack_elements())
      VisitTemplateArgument(P);
    break;
  }
}

namespace {
class OpenACCClauseProfiler
    : public OpenACCClauseVisitor<OpenACCClauseProfiler> {
  StmtProfiler &Profiler;

public:
  OpenACCClauseProfiler(StmtProfiler &P) : Profiler(P) {}

  void VisitOpenACCClauseList(ArrayRef<const OpenACCClause *> Clauses) {
    for (const OpenACCClause *Clause : Clauses) {
      // TODO OpenACC: When we have clauses with expressions, we should
      // profile them too.
      Visit(Clause);
    }
  }

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  void Visit##CLAUSE_NAME##Clause(const OpenACC##CLAUSE_NAME##Clause &Clause);

#include "clang/Basic/OpenACCClauses.def"
};

/// Nothing to do here, there are no sub-statements.
void OpenACCClauseProfiler::VisitDefaultClause(
    const OpenACCDefaultClause &Clause) {}

void OpenACCClauseProfiler::VisitIfClause(const OpenACCIfClause &Clause) {
  assert(Clause.hasConditionExpr() &&
         "if clause requires a valid condition expr");
  Profiler.VisitStmt(Clause.getConditionExpr());
}

void OpenACCClauseProfiler::VisitCopyClause(const OpenACCCopyClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}
void OpenACCClauseProfiler::VisitCopyInClause(
    const OpenACCCopyInClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitCopyOutClause(
    const OpenACCCopyOutClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitCreateClause(
    const OpenACCCreateClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitSelfClause(const OpenACCSelfClause &Clause) {
  if (Clause.hasConditionExpr())
    Profiler.VisitStmt(Clause.getConditionExpr());
}

void OpenACCClauseProfiler::VisitNumGangsClause(
    const OpenACCNumGangsClause &Clause) {
  for (auto *E : Clause.getIntExprs())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitNumWorkersClause(
    const OpenACCNumWorkersClause &Clause) {
  assert(Clause.hasIntExpr() && "num_workers clause requires a valid int expr");
  Profiler.VisitStmt(Clause.getIntExpr());
}

void OpenACCClauseProfiler::VisitPrivateClause(
    const OpenACCPrivateClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitFirstPrivateClause(
    const OpenACCFirstPrivateClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitAttachClause(
    const OpenACCAttachClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitDevicePtrClause(
    const OpenACCDevicePtrClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitNoCreateClause(
    const OpenACCNoCreateClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitPresentClause(
    const OpenACCPresentClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}

void OpenACCClauseProfiler::VisitVectorLengthClause(
    const OpenACCVectorLengthClause &Clause) {
  assert(Clause.hasIntExpr() &&
         "vector_length clause requires a valid int expr");
  Profiler.VisitStmt(Clause.getIntExpr());
}

void OpenACCClauseProfiler::VisitAsyncClause(const OpenACCAsyncClause &Clause) {
  if (Clause.hasIntExpr())
    Profiler.VisitStmt(Clause.getIntExpr());
}

void OpenACCClauseProfiler::VisitWaitClause(const OpenACCWaitClause &Clause) {
  if (Clause.hasDevNumExpr())
    Profiler.VisitStmt(Clause.getDevNumExpr());
  for (auto *E : Clause.getQueueIdExprs())
    Profiler.VisitStmt(E);
}
/// Nothing to do here, there are no sub-statements.
void OpenACCClauseProfiler::VisitDeviceTypeClause(
    const OpenACCDeviceTypeClause &Clause) {}

void OpenACCClauseProfiler::VisitAutoClause(const OpenACCAutoClause &Clause) {}

void OpenACCClauseProfiler::VisitIndependentClause(
    const OpenACCIndependentClause &Clause) {}

void OpenACCClauseProfiler::VisitSeqClause(const OpenACCSeqClause &Clause) {}

void OpenACCClauseProfiler::VisitReductionClause(
    const OpenACCReductionClause &Clause) {
  for (auto *E : Clause.getVarList())
    Profiler.VisitStmt(E);
}
} // namespace

void StmtProfiler::VisitOpenACCComputeConstruct(
    const OpenACCComputeConstruct *S) {
  // VisitStmt handles children, so the AssociatedStmt is handled.
  VisitStmt(S);

  OpenACCClauseProfiler P{*this};
  P.VisitOpenACCClauseList(S->clauses());
}

void StmtProfiler::VisitOpenACCLoopConstruct(const OpenACCLoopConstruct *S) {
  // VisitStmt handles children, so the Loop is handled.
  VisitStmt(S);

  OpenACCClauseProfiler P{*this};
  P.VisitOpenACCClauseList(S->clauses());
}

void Stmt::Profile(llvm::FoldingSetNodeID &ID, const ASTContext &Context,
                   bool Canonical, bool ProfileLambdaExpr) const {
  StmtProfilerWithPointers Profiler(ID, Context, Canonical, ProfileLambdaExpr);
  Profiler.Visit(this);
}

void Stmt::ProcessODRHash(llvm::FoldingSetNodeID &ID,
                          class ODRHash &Hash) const {
  StmtProfilerWithoutPointers Profiler(ID, Hash);
  Profiler.Visit(this);
}
