//===--- ASTWriterStmt.cpp - Statement and Expression Serialization -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implements serialization for Statements and Expressions.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Lex/Token.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTRecordWriter.h"
#include "llvm/Bitstream/BitstreamWriter.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// Statement/expression serialization
//===----------------------------------------------------------------------===//

namespace clang {

  class ASTStmtWriter : public StmtVisitor<ASTStmtWriter, void> {
    ASTWriter &Writer;
    ASTRecordWriter Record;

    serialization::StmtCode Code;
    unsigned AbbrevToUse;

    /// A helper that can help us to write a packed bit across function
    /// calls. For example, we may write separate bits in separate functions:
    ///
    ///  void VisitA(A* a) {
    ///     Record.push_back(a->isSomething());
    ///  }
    ///
    ///  void Visitb(B *b) {
    ///     VisitA(b);
    ///     Record.push_back(b->isAnother());
    ///  }
    ///
    /// In such cases, it'll be better if we can pack these 2 bits. We achieve
    /// this by writing a zero value in `VisitA` and recorded that first and add
    /// the new bit to the recorded value.
    class PakedBitsWriter {
    public:
      PakedBitsWriter(ASTRecordWriter &Record) : RecordRef(Record) {}
      ~PakedBitsWriter() { assert(!CurrentIndex); }

      void addBit(bool Value) {
        assert(CurrentIndex && "Writing Bits without recording first!");
        PackingBits.addBit(Value);
      }
      void addBits(uint32_t Value, uint32_t BitsWidth) {
        assert(CurrentIndex && "Writing Bits without recording first!");
        PackingBits.addBits(Value, BitsWidth);
      }

      void writeBits() {
        if (!CurrentIndex)
          return;

        RecordRef[*CurrentIndex] = (uint32_t)PackingBits;
        CurrentIndex = std::nullopt;
        PackingBits.reset(0);
      }

      void updateBits() {
        writeBits();

        CurrentIndex = RecordRef.size();
        RecordRef.push_back(0);
      }

    private:
      BitsPacker PackingBits;
      ASTRecordWriter &RecordRef;
      std::optional<unsigned> CurrentIndex;
    };

    PakedBitsWriter CurrentPackingBits;

  public:
    ASTStmtWriter(ASTWriter &Writer, ASTWriter::RecordData &Record)
        : Writer(Writer), Record(Writer, Record),
          Code(serialization::STMT_NULL_PTR), AbbrevToUse(0),
          CurrentPackingBits(this->Record) {}

    ASTStmtWriter(const ASTStmtWriter&) = delete;
    ASTStmtWriter &operator=(const ASTStmtWriter &) = delete;

    uint64_t Emit() {
      CurrentPackingBits.writeBits();
      assert(Code != serialization::STMT_NULL_PTR &&
             "unhandled sub-statement writing AST file");
      return Record.EmitStmt(Code, AbbrevToUse);
    }

    void AddTemplateKWAndArgsInfo(const ASTTemplateKWAndArgsInfo &ArgInfo,
                                  const TemplateArgumentLoc *Args);

    void VisitStmt(Stmt *S);
#define STMT(Type, Base) \
    void Visit##Type(Type *);
#include "clang/AST/StmtNodes.inc"
  };
}

void ASTStmtWriter::AddTemplateKWAndArgsInfo(
    const ASTTemplateKWAndArgsInfo &ArgInfo, const TemplateArgumentLoc *Args) {
  Record.AddSourceLocation(ArgInfo.TemplateKWLoc);
  Record.AddSourceLocation(ArgInfo.LAngleLoc);
  Record.AddSourceLocation(ArgInfo.RAngleLoc);
  for (unsigned i = 0; i != ArgInfo.NumTemplateArgs; ++i)
    Record.AddTemplateArgumentLoc(Args[i]);
}

void ASTStmtWriter::VisitStmt(Stmt *S) {
}

void ASTStmtWriter::VisitNullStmt(NullStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getSemiLoc());
  Record.push_back(S->NullStmtBits.HasLeadingEmptyMacro);
  Code = serialization::STMT_NULL;
}

void ASTStmtWriter::VisitCompoundStmt(CompoundStmt *S) {
  VisitStmt(S);

  Record.push_back(S->size());
  Record.push_back(S->hasStoredFPFeatures());

  for (auto *CS : S->body())
    Record.AddStmt(CS);
  if (S->hasStoredFPFeatures())
    Record.push_back(S->getStoredFPFeatures().getAsOpaqueInt());
  Record.AddSourceLocation(S->getLBracLoc());
  Record.AddSourceLocation(S->getRBracLoc());

  if (!S->hasStoredFPFeatures())
    AbbrevToUse = Writer.getCompoundStmtAbbrev();

  Code = serialization::STMT_COMPOUND;
}

void ASTStmtWriter::VisitSwitchCase(SwitchCase *S) {
  VisitStmt(S);
  Record.push_back(Writer.getSwitchCaseID(S));
  Record.AddSourceLocation(S->getKeywordLoc());
  Record.AddSourceLocation(S->getColonLoc());
}

void ASTStmtWriter::VisitCaseStmt(CaseStmt *S) {
  VisitSwitchCase(S);
  Record.push_back(S->caseStmtIsGNURange());
  Record.AddStmt(S->getLHS());
  Record.AddStmt(S->getSubStmt());
  if (S->caseStmtIsGNURange()) {
    Record.AddStmt(S->getRHS());
    Record.AddSourceLocation(S->getEllipsisLoc());
  }
  Code = serialization::STMT_CASE;
}

void ASTStmtWriter::VisitDefaultStmt(DefaultStmt *S) {
  VisitSwitchCase(S);
  Record.AddStmt(S->getSubStmt());
  Code = serialization::STMT_DEFAULT;
}

void ASTStmtWriter::VisitLabelStmt(LabelStmt *S) {
  VisitStmt(S);
  Record.push_back(S->isSideEntry());
  Record.AddDeclRef(S->getDecl());
  Record.AddStmt(S->getSubStmt());
  Record.AddSourceLocation(S->getIdentLoc());
  Code = serialization::STMT_LABEL;
}

void ASTStmtWriter::VisitAttributedStmt(AttributedStmt *S) {
  VisitStmt(S);
  Record.push_back(S->getAttrs().size());
  Record.AddAttributes(S->getAttrs());
  Record.AddStmt(S->getSubStmt());
  Record.AddSourceLocation(S->getAttrLoc());
  Code = serialization::STMT_ATTRIBUTED;
}

void ASTStmtWriter::VisitIfStmt(IfStmt *S) {
  VisitStmt(S);

  bool HasElse = S->getElse() != nullptr;
  bool HasVar = S->getConditionVariableDeclStmt() != nullptr;
  bool HasInit = S->getInit() != nullptr;

  CurrentPackingBits.updateBits();

  CurrentPackingBits.addBit(HasElse);
  CurrentPackingBits.addBit(HasVar);
  CurrentPackingBits.addBit(HasInit);
  Record.push_back(static_cast<uint64_t>(S->getStatementKind()));
  Record.AddStmt(S->getCond());
  Record.AddStmt(S->getThen());
  if (HasElse)
    Record.AddStmt(S->getElse());
  if (HasVar)
    Record.AddStmt(S->getConditionVariableDeclStmt());
  if (HasInit)
    Record.AddStmt(S->getInit());

  Record.AddSourceLocation(S->getIfLoc());
  Record.AddSourceLocation(S->getLParenLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  if (HasElse)
    Record.AddSourceLocation(S->getElseLoc());

  Code = serialization::STMT_IF;
}

void ASTStmtWriter::VisitSwitchStmt(SwitchStmt *S) {
  VisitStmt(S);

  bool HasInit = S->getInit() != nullptr;
  bool HasVar = S->getConditionVariableDeclStmt() != nullptr;
  Record.push_back(HasInit);
  Record.push_back(HasVar);
  Record.push_back(S->isAllEnumCasesCovered());

  Record.AddStmt(S->getCond());
  Record.AddStmt(S->getBody());
  if (HasInit)
    Record.AddStmt(S->getInit());
  if (HasVar)
    Record.AddStmt(S->getConditionVariableDeclStmt());

  Record.AddSourceLocation(S->getSwitchLoc());
  Record.AddSourceLocation(S->getLParenLoc());
  Record.AddSourceLocation(S->getRParenLoc());

  for (SwitchCase *SC = S->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase())
    Record.push_back(Writer.RecordSwitchCaseID(SC));
  Code = serialization::STMT_SWITCH;
}

void ASTStmtWriter::VisitWhileStmt(WhileStmt *S) {
  VisitStmt(S);

  bool HasVar = S->getConditionVariableDeclStmt() != nullptr;
  Record.push_back(HasVar);

  Record.AddStmt(S->getCond());
  Record.AddStmt(S->getBody());
  if (HasVar)
    Record.AddStmt(S->getConditionVariableDeclStmt());

  Record.AddSourceLocation(S->getWhileLoc());
  Record.AddSourceLocation(S->getLParenLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  Code = serialization::STMT_WHILE;
}

void ASTStmtWriter::VisitDoStmt(DoStmt *S) {
  VisitStmt(S);
  Record.AddStmt(S->getCond());
  Record.AddStmt(S->getBody());
  Record.AddSourceLocation(S->getDoLoc());
  Record.AddSourceLocation(S->getWhileLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  Code = serialization::STMT_DO;
}

void ASTStmtWriter::VisitForStmt(ForStmt *S) {
  VisitStmt(S);
  Record.AddStmt(S->getInit());
  Record.AddStmt(S->getCond());
  Record.AddStmt(S->getConditionVariableDeclStmt());
  Record.AddStmt(S->getInc());
  Record.AddStmt(S->getBody());
  Record.AddSourceLocation(S->getForLoc());
  Record.AddSourceLocation(S->getLParenLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  Code = serialization::STMT_FOR;
}

void ASTStmtWriter::VisitGotoStmt(GotoStmt *S) {
  VisitStmt(S);
  Record.AddDeclRef(S->getLabel());
  Record.AddSourceLocation(S->getGotoLoc());
  Record.AddSourceLocation(S->getLabelLoc());
  Code = serialization::STMT_GOTO;
}

void ASTStmtWriter::VisitIndirectGotoStmt(IndirectGotoStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getGotoLoc());
  Record.AddSourceLocation(S->getStarLoc());
  Record.AddStmt(S->getTarget());
  Code = serialization::STMT_INDIRECT_GOTO;
}

void ASTStmtWriter::VisitContinueStmt(ContinueStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getContinueLoc());
  Code = serialization::STMT_CONTINUE;
}

void ASTStmtWriter::VisitBreakStmt(BreakStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getBreakLoc());
  Code = serialization::STMT_BREAK;
}

void ASTStmtWriter::VisitReturnStmt(ReturnStmt *S) {
  VisitStmt(S);

  bool HasNRVOCandidate = S->getNRVOCandidate() != nullptr;
  Record.push_back(HasNRVOCandidate);

  Record.AddStmt(S->getRetValue());
  if (HasNRVOCandidate)
    Record.AddDeclRef(S->getNRVOCandidate());

  Record.AddSourceLocation(S->getReturnLoc());
  Code = serialization::STMT_RETURN;
}

void ASTStmtWriter::VisitDeclStmt(DeclStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getBeginLoc());
  Record.AddSourceLocation(S->getEndLoc());
  DeclGroupRef DG = S->getDeclGroup();
  for (DeclGroupRef::iterator D = DG.begin(), DEnd = DG.end(); D != DEnd; ++D)
    Record.AddDeclRef(*D);
  Code = serialization::STMT_DECL;
}

void ASTStmtWriter::VisitAsmStmt(AsmStmt *S) {
  VisitStmt(S);
  Record.push_back(S->getNumOutputs());
  Record.push_back(S->getNumInputs());
  Record.push_back(S->getNumClobbers());
  Record.AddSourceLocation(S->getAsmLoc());
  Record.push_back(S->isVolatile());
  Record.push_back(S->isSimple());
}

void ASTStmtWriter::VisitGCCAsmStmt(GCCAsmStmt *S) {
  VisitAsmStmt(S);
  Record.push_back(S->getNumLabels());
  Record.AddSourceLocation(S->getRParenLoc());
  Record.AddStmt(S->getAsmString());

  // Outputs
  for (unsigned I = 0, N = S->getNumOutputs(); I != N; ++I) {
    Record.AddIdentifierRef(S->getOutputIdentifier(I));
    Record.AddStmt(S->getOutputConstraintLiteral(I));
    Record.AddStmt(S->getOutputExpr(I));
  }

  // Inputs
  for (unsigned I = 0, N = S->getNumInputs(); I != N; ++I) {
    Record.AddIdentifierRef(S->getInputIdentifier(I));
    Record.AddStmt(S->getInputConstraintLiteral(I));
    Record.AddStmt(S->getInputExpr(I));
  }

  // Clobbers
  for (unsigned I = 0, N = S->getNumClobbers(); I != N; ++I)
    Record.AddStmt(S->getClobberStringLiteral(I));

  // Labels
  for (unsigned I = 0, N = S->getNumLabels(); I != N; ++I) {
    Record.AddIdentifierRef(S->getLabelIdentifier(I));
    Record.AddStmt(S->getLabelExpr(I));
  }

  Code = serialization::STMT_GCCASM;
}

void ASTStmtWriter::VisitMSAsmStmt(MSAsmStmt *S) {
  VisitAsmStmt(S);
  Record.AddSourceLocation(S->getLBraceLoc());
  Record.AddSourceLocation(S->getEndLoc());
  Record.push_back(S->getNumAsmToks());
  Record.AddString(S->getAsmString());

  // Tokens
  for (unsigned I = 0, N = S->getNumAsmToks(); I != N; ++I) {
    // FIXME: Move this to ASTRecordWriter?
    Writer.AddToken(S->getAsmToks()[I], Record.getRecordData());
  }

  // Clobbers
  for (unsigned I = 0, N = S->getNumClobbers(); I != N; ++I) {
    Record.AddString(S->getClobber(I));
  }

  // Outputs
  for (unsigned I = 0, N = S->getNumOutputs(); I != N; ++I) {
    Record.AddStmt(S->getOutputExpr(I));
    Record.AddString(S->getOutputConstraint(I));
  }

  // Inputs
  for (unsigned I = 0, N = S->getNumInputs(); I != N; ++I) {
    Record.AddStmt(S->getInputExpr(I));
    Record.AddString(S->getInputConstraint(I));
  }

  Code = serialization::STMT_MSASM;
}

void ASTStmtWriter::VisitCoroutineBodyStmt(CoroutineBodyStmt *CoroStmt) {
  VisitStmt(CoroStmt);
  Record.push_back(CoroStmt->getParamMoves().size());
  for (Stmt *S : CoroStmt->children())
    Record.AddStmt(S);
  Code = serialization::STMT_COROUTINE_BODY;
}

void ASTStmtWriter::VisitCoreturnStmt(CoreturnStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getKeywordLoc());
  Record.AddStmt(S->getOperand());
  Record.AddStmt(S->getPromiseCall());
  Record.push_back(S->isImplicit());
  Code = serialization::STMT_CORETURN;
}

void ASTStmtWriter::VisitCoroutineSuspendExpr(CoroutineSuspendExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getKeywordLoc());
  for (Stmt *S : E->children())
    Record.AddStmt(S);
  Record.AddStmt(E->getOpaqueValue());
}

void ASTStmtWriter::VisitCoawaitExpr(CoawaitExpr *E) {
  VisitCoroutineSuspendExpr(E);
  Record.push_back(E->isImplicit());
  Code = serialization::EXPR_COAWAIT;
}

void ASTStmtWriter::VisitCoyieldExpr(CoyieldExpr *E) {
  VisitCoroutineSuspendExpr(E);
  Code = serialization::EXPR_COYIELD;
}

void ASTStmtWriter::VisitDependentCoawaitExpr(DependentCoawaitExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getKeywordLoc());
  for (Stmt *S : E->children())
    Record.AddStmt(S);
  Code = serialization::EXPR_DEPENDENT_COAWAIT;
}

static void
addConstraintSatisfaction(ASTRecordWriter &Record,
                          const ASTConstraintSatisfaction &Satisfaction) {
  Record.push_back(Satisfaction.IsSatisfied);
  Record.push_back(Satisfaction.ContainsErrors);
  if (!Satisfaction.IsSatisfied) {
    Record.push_back(Satisfaction.NumRecords);
    for (const auto &DetailRecord : Satisfaction) {
      auto *E = DetailRecord.dyn_cast<Expr *>();
      Record.push_back(/* IsDiagnostic */ E == nullptr);
      if (E)
        Record.AddStmt(E);
      else {
        auto *Diag = DetailRecord.get<std::pair<SourceLocation, StringRef> *>();
        Record.AddSourceLocation(Diag->first);
        Record.AddString(Diag->second);
      }
    }
  }
}

static void
addSubstitutionDiagnostic(
    ASTRecordWriter &Record,
    const concepts::Requirement::SubstitutionDiagnostic *D) {
  Record.AddString(D->SubstitutedEntity);
  Record.AddSourceLocation(D->DiagLoc);
  Record.AddString(D->DiagMessage);
}

void ASTStmtWriter::VisitConceptSpecializationExpr(
        ConceptSpecializationExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getSpecializationDecl());
  const ConceptReference *CR = E->getConceptReference();
  Record.push_back(CR != nullptr);
  if (CR)
    Record.AddConceptReference(CR);
  if (!E->isValueDependent())
    addConstraintSatisfaction(Record, E->getSatisfaction());

  Code = serialization::EXPR_CONCEPT_SPECIALIZATION;
}

void ASTStmtWriter::VisitRequiresExpr(RequiresExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getLocalParameters().size());
  Record.push_back(E->getRequirements().size());
  Record.AddSourceLocation(E->RequiresExprBits.RequiresKWLoc);
  Record.push_back(E->RequiresExprBits.IsSatisfied);
  Record.AddDeclRef(E->getBody());
  for (ParmVarDecl *P : E->getLocalParameters())
    Record.AddDeclRef(P);
  for (concepts::Requirement *R : E->getRequirements()) {
    if (auto *TypeReq = dyn_cast<concepts::TypeRequirement>(R)) {
      Record.push_back(concepts::Requirement::RK_Type);
      Record.push_back(TypeReq->Status);
      if (TypeReq->Status == concepts::TypeRequirement::SS_SubstitutionFailure)
        addSubstitutionDiagnostic(Record, TypeReq->getSubstitutionDiagnostic());
      else
        Record.AddTypeSourceInfo(TypeReq->getType());
    } else if (auto *ExprReq = dyn_cast<concepts::ExprRequirement>(R)) {
      Record.push_back(ExprReq->getKind());
      Record.push_back(ExprReq->Status);
      if (ExprReq->isExprSubstitutionFailure()) {
        addSubstitutionDiagnostic(Record,
         ExprReq->Value.get<concepts::Requirement::SubstitutionDiagnostic *>());
      } else
        Record.AddStmt(ExprReq->Value.get<Expr *>());
      if (ExprReq->getKind() == concepts::Requirement::RK_Compound) {
        Record.AddSourceLocation(ExprReq->NoexceptLoc);
        const auto &RetReq = ExprReq->getReturnTypeRequirement();
        if (RetReq.isSubstitutionFailure()) {
          Record.push_back(2);
          addSubstitutionDiagnostic(Record, RetReq.getSubstitutionDiagnostic());
        } else if (RetReq.isTypeConstraint()) {
          Record.push_back(1);
          Record.AddTemplateParameterList(
              RetReq.getTypeConstraintTemplateParameterList());
          if (ExprReq->Status >=
              concepts::ExprRequirement::SS_ConstraintsNotSatisfied)
            Record.AddStmt(
                ExprReq->getReturnTypeRequirementSubstitutedConstraintExpr());
        } else {
          assert(RetReq.isEmpty());
          Record.push_back(0);
        }
      }
    } else {
      auto *NestedReq = cast<concepts::NestedRequirement>(R);
      Record.push_back(concepts::Requirement::RK_Nested);
      Record.push_back(NestedReq->hasInvalidConstraint());
      if (NestedReq->hasInvalidConstraint()) {
        Record.AddString(NestedReq->getInvalidConstraintEntity());
        addConstraintSatisfaction(Record, *NestedReq->Satisfaction);
      } else {
        Record.AddStmt(NestedReq->getConstraintExpr());
        if (!NestedReq->isDependent())
          addConstraintSatisfaction(Record, *NestedReq->Satisfaction);
      }
    }
  }
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.AddSourceLocation(E->getEndLoc());

  Code = serialization::EXPR_REQUIRES;
}


void ASTStmtWriter::VisitCapturedStmt(CapturedStmt *S) {
  VisitStmt(S);
  // NumCaptures
  Record.push_back(std::distance(S->capture_begin(), S->capture_end()));

  // CapturedDecl and captured region kind
  Record.AddDeclRef(S->getCapturedDecl());
  Record.push_back(S->getCapturedRegionKind());

  Record.AddDeclRef(S->getCapturedRecordDecl());

  // Capture inits
  for (auto *I : S->capture_inits())
    Record.AddStmt(I);

  // Body
  Record.AddStmt(S->getCapturedStmt());

  // Captures
  for (const auto &I : S->captures()) {
    if (I.capturesThis() || I.capturesVariableArrayType())
      Record.AddDeclRef(nullptr);
    else
      Record.AddDeclRef(I.getCapturedVar());
    Record.push_back(I.getCaptureKind());
    Record.AddSourceLocation(I.getLocation());
  }

  Code = serialization::STMT_CAPTURED;
}

void ASTStmtWriter::VisitExpr(Expr *E) {
  VisitStmt(E);

  CurrentPackingBits.updateBits();
  CurrentPackingBits.addBits(E->getDependence(), /*BitsWidth=*/5);
  CurrentPackingBits.addBits(E->getValueKind(), /*BitsWidth=*/2);
  CurrentPackingBits.addBits(E->getObjectKind(), /*BitsWidth=*/3);

  Record.AddTypeRef(E->getType());
}

void ASTStmtWriter::VisitConstantExpr(ConstantExpr *E) {
  VisitExpr(E);
  Record.push_back(E->ConstantExprBits.ResultKind);

  Record.push_back(E->ConstantExprBits.APValueKind);
  Record.push_back(E->ConstantExprBits.IsUnsigned);
  Record.push_back(E->ConstantExprBits.BitWidth);
  // HasCleanup not serialized since we can just query the APValue.
  Record.push_back(E->ConstantExprBits.IsImmediateInvocation);

  switch (E->getResultStorageKind()) {
  case ConstantResultStorageKind::None:
    break;
  case ConstantResultStorageKind::Int64:
    Record.push_back(E->Int64Result());
    break;
  case ConstantResultStorageKind::APValue:
    Record.AddAPValue(E->APValueResult());
    break;
  }

  Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_CONSTANT;
}

void ASTStmtWriter::VisitSYCLUniqueStableNameExpr(SYCLUniqueStableNameExpr *E) {
  VisitExpr(E);

  Record.AddSourceLocation(E->getLocation());
  Record.AddSourceLocation(E->getLParenLocation());
  Record.AddSourceLocation(E->getRParenLocation());
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());

  Code = serialization::EXPR_SYCL_UNIQUE_STABLE_NAME;
}

void ASTStmtWriter::VisitPredefinedExpr(PredefinedExpr *E) {
  VisitExpr(E);

  bool HasFunctionName = E->getFunctionName() != nullptr;
  Record.push_back(HasFunctionName);
  Record.push_back(
      llvm::to_underlying(E->getIdentKind())); // FIXME: stable encoding
  Record.push_back(E->isTransparent());
  Record.AddSourceLocation(E->getLocation());
  if (HasFunctionName)
    Record.AddStmt(E->getFunctionName());
  Code = serialization::EXPR_PREDEFINED;
}

void ASTStmtWriter::VisitDeclRefExpr(DeclRefExpr *E) {
  VisitExpr(E);

  CurrentPackingBits.updateBits();

  CurrentPackingBits.addBit(E->hadMultipleCandidates());
  CurrentPackingBits.addBit(E->refersToEnclosingVariableOrCapture());
  CurrentPackingBits.addBits(E->isNonOdrUse(), /*Width=*/2);
  CurrentPackingBits.addBit(E->isImmediateEscalating());
  CurrentPackingBits.addBit(E->getDecl() != E->getFoundDecl());
  CurrentPackingBits.addBit(E->hasQualifier());
  CurrentPackingBits.addBit(E->hasTemplateKWAndArgsInfo());

  if (E->hasTemplateKWAndArgsInfo()) {
    unsigned NumTemplateArgs = E->getNumTemplateArgs();
    Record.push_back(NumTemplateArgs);
  }

  DeclarationName::NameKind nk = (E->getDecl()->getDeclName().getNameKind());

  if ((!E->hasTemplateKWAndArgsInfo()) && (!E->hasQualifier()) &&
      (E->getDecl() == E->getFoundDecl()) &&
      nk == DeclarationName::Identifier && E->getObjectKind() == OK_Ordinary) {
    AbbrevToUse = Writer.getDeclRefExprAbbrev();
  }

  if (E->hasQualifier())
    Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());

  if (E->getDecl() != E->getFoundDecl())
    Record.AddDeclRef(E->getFoundDecl());

  if (E->hasTemplateKWAndArgsInfo())
    AddTemplateKWAndArgsInfo(*E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
                             E->getTrailingObjects<TemplateArgumentLoc>());

  Record.AddDeclRef(E->getDecl());
  Record.AddSourceLocation(E->getLocation());
  Record.AddDeclarationNameLoc(E->DNLoc, E->getDecl()->getDeclName());
  Code = serialization::EXPR_DECL_REF;
}

void ASTStmtWriter::VisitIntegerLiteral(IntegerLiteral *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getLocation());
  Record.AddAPInt(E->getValue());

  if (E->getValue().getBitWidth() == 32) {
    AbbrevToUse = Writer.getIntegerLiteralAbbrev();
  }

  Code = serialization::EXPR_INTEGER_LITERAL;
}

void ASTStmtWriter::VisitFixedPointLiteral(FixedPointLiteral *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getLocation());
  Record.push_back(E->getScale());
  Record.AddAPInt(E->getValue());
  Code = serialization::EXPR_FIXEDPOINT_LITERAL;
}

void ASTStmtWriter::VisitFloatingLiteral(FloatingLiteral *E) {
  VisitExpr(E);
  Record.push_back(E->getRawSemantics());
  Record.push_back(E->isExact());
  Record.AddAPFloat(E->getValue());
  Record.AddSourceLocation(E->getLocation());
  Code = serialization::EXPR_FLOATING_LITERAL;
}

void ASTStmtWriter::VisitImaginaryLiteral(ImaginaryLiteral *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_IMAGINARY_LITERAL;
}

void ASTStmtWriter::VisitStringLiteral(StringLiteral *E) {
  VisitExpr(E);

  // Store the various bits of data of StringLiteral.
  Record.push_back(E->getNumConcatenated());
  Record.push_back(E->getLength());
  Record.push_back(E->getCharByteWidth());
  Record.push_back(llvm::to_underlying(E->getKind()));
  Record.push_back(E->isPascal());

  // Store the trailing array of SourceLocation.
  for (unsigned I = 0, N = E->getNumConcatenated(); I != N; ++I)
    Record.AddSourceLocation(E->getStrTokenLoc(I));

  // Store the trailing array of char holding the string data.
  StringRef StrData = E->getBytes();
  for (unsigned I = 0, N = E->getByteLength(); I != N; ++I)
    Record.push_back(StrData[I]);

  Code = serialization::EXPR_STRING_LITERAL;
}

void ASTStmtWriter::VisitCharacterLiteral(CharacterLiteral *E) {
  VisitExpr(E);
  Record.push_back(E->getValue());
  Record.AddSourceLocation(E->getLocation());
  Record.push_back(llvm::to_underlying(E->getKind()));

  AbbrevToUse = Writer.getCharacterLiteralAbbrev();

  Code = serialization::EXPR_CHARACTER_LITERAL;
}

void ASTStmtWriter::VisitParenExpr(ParenExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getLParen());
  Record.AddSourceLocation(E->getRParen());
  Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_PAREN;
}

void ASTStmtWriter::VisitParenListExpr(ParenListExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumExprs());
  for (auto *SubStmt : E->exprs())
    Record.AddStmt(SubStmt);
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_PAREN_LIST;
}

void ASTStmtWriter::VisitUnaryOperator(UnaryOperator *E) {
  VisitExpr(E);
  bool HasFPFeatures = E->hasStoredFPFeatures();
  // Write this first for easy access when deserializing, as they affect the
  // size of the UnaryOperator.
  CurrentPackingBits.addBit(HasFPFeatures);
  Record.AddStmt(E->getSubExpr());
  CurrentPackingBits.addBits(E->getOpcode(),
                             /*Width=*/5); // FIXME: stable encoding
  Record.AddSourceLocation(E->getOperatorLoc());
  CurrentPackingBits.addBit(E->canOverflow());

  if (HasFPFeatures)
    Record.push_back(E->getStoredFPFeatures().getAsOpaqueInt());
  Code = serialization::EXPR_UNARY_OPERATOR;
}

void ASTStmtWriter::VisitOffsetOfExpr(OffsetOfExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumComponents());
  Record.push_back(E->getNumExpressions());
  Record.AddSourceLocation(E->getOperatorLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());
  for (unsigned I = 0, N = E->getNumComponents(); I != N; ++I) {
    const OffsetOfNode &ON = E->getComponent(I);
    Record.push_back(ON.getKind()); // FIXME: Stable encoding
    Record.AddSourceLocation(ON.getSourceRange().getBegin());
    Record.AddSourceLocation(ON.getSourceRange().getEnd());
    switch (ON.getKind()) {
    case OffsetOfNode::Array:
      Record.push_back(ON.getArrayExprIndex());
      break;

    case OffsetOfNode::Field:
      Record.AddDeclRef(ON.getField());
      break;

    case OffsetOfNode::Identifier:
      Record.AddIdentifierRef(ON.getFieldName());
      break;

    case OffsetOfNode::Base:
      Record.AddCXXBaseSpecifier(*ON.getBase());
      break;
    }
  }
  for (unsigned I = 0, N = E->getNumExpressions(); I != N; ++I)
    Record.AddStmt(E->getIndexExpr(I));
  Code = serialization::EXPR_OFFSETOF;
}

void ASTStmtWriter::VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getKind());
  if (E->isArgumentType())
    Record.AddTypeSourceInfo(E->getArgumentTypeInfo());
  else {
    Record.push_back(0);
    Record.AddStmt(E->getArgumentExpr());
  }
  Record.AddSourceLocation(E->getOperatorLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_SIZEOF_ALIGN_OF;
}

void ASTStmtWriter::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getLHS());
  Record.AddStmt(E->getRHS());
  Record.AddSourceLocation(E->getRBracketLoc());
  Code = serialization::EXPR_ARRAY_SUBSCRIPT;
}

void ASTStmtWriter::VisitMatrixSubscriptExpr(MatrixSubscriptExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getBase());
  Record.AddStmt(E->getRowIdx());
  Record.AddStmt(E->getColumnIdx());
  Record.AddSourceLocation(E->getRBracketLoc());
  Code = serialization::EXPR_ARRAY_SUBSCRIPT;
}

void ASTStmtWriter::VisitArraySectionExpr(ArraySectionExpr *E) {
  VisitExpr(E);
  Record.writeEnum(E->ASType);
  Record.AddStmt(E->getBase());
  Record.AddStmt(E->getLowerBound());
  Record.AddStmt(E->getLength());
  if (E->isOMPArraySection())
    Record.AddStmt(E->getStride());
  Record.AddSourceLocation(E->getColonLocFirst());

  if (E->isOMPArraySection())
    Record.AddSourceLocation(E->getColonLocSecond());

  Record.AddSourceLocation(E->getRBracketLoc());
  Code = serialization::EXPR_ARRAY_SECTION;
}

void ASTStmtWriter::VisitOMPArrayShapingExpr(OMPArrayShapingExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getDimensions().size());
  Record.AddStmt(E->getBase());
  for (Expr *Dim : E->getDimensions())
    Record.AddStmt(Dim);
  for (SourceRange SR : E->getBracketsRanges())
    Record.AddSourceRange(SR);
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_OMP_ARRAY_SHAPING;
}

void ASTStmtWriter::VisitOMPIteratorExpr(OMPIteratorExpr *E) {
  VisitExpr(E);
  Record.push_back(E->numOfIterators());
  Record.AddSourceLocation(E->getIteratorKwLoc());
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  for (unsigned I = 0, End = E->numOfIterators(); I < End; ++I) {
    Record.AddDeclRef(E->getIteratorDecl(I));
    Record.AddSourceLocation(E->getAssignLoc(I));
    OMPIteratorExpr::IteratorRange Range = E->getIteratorRange(I);
    Record.AddStmt(Range.Begin);
    Record.AddStmt(Range.End);
    Record.AddStmt(Range.Step);
    Record.AddSourceLocation(E->getColonLoc(I));
    if (Range.Step)
      Record.AddSourceLocation(E->getSecondColonLoc(I));
    // Serialize helpers
    OMPIteratorHelperData &HD = E->getHelper(I);
    Record.AddDeclRef(HD.CounterVD);
    Record.AddStmt(HD.Upper);
    Record.AddStmt(HD.Update);
    Record.AddStmt(HD.CounterUpdate);
  }
  Code = serialization::EXPR_OMP_ITERATOR;
}

void ASTStmtWriter::VisitCallExpr(CallExpr *E) {
  VisitExpr(E);

  Record.push_back(E->getNumArgs());
  CurrentPackingBits.updateBits();
  CurrentPackingBits.addBit(static_cast<bool>(E->getADLCallKind()));
  CurrentPackingBits.addBit(E->hasStoredFPFeatures());

  Record.AddSourceLocation(E->getRParenLoc());
  Record.AddStmt(E->getCallee());
  for (CallExpr::arg_iterator Arg = E->arg_begin(), ArgEnd = E->arg_end();
       Arg != ArgEnd; ++Arg)
    Record.AddStmt(*Arg);

  if (E->hasStoredFPFeatures())
    Record.push_back(E->getFPFeatures().getAsOpaqueInt());

  if (!E->hasStoredFPFeatures() && !static_cast<bool>(E->getADLCallKind()) &&
      E->getStmtClass() == Stmt::CallExprClass)
    AbbrevToUse = Writer.getCallExprAbbrev();

  Code = serialization::EXPR_CALL;
}

void ASTStmtWriter::VisitRecoveryExpr(RecoveryExpr *E) {
  VisitExpr(E);
  Record.push_back(std::distance(E->children().begin(), E->children().end()));
  Record.AddSourceLocation(E->getBeginLoc());
  Record.AddSourceLocation(E->getEndLoc());
  for (Stmt *Child : E->children())
    Record.AddStmt(Child);
  Code = serialization::EXPR_RECOVERY;
}

void ASTStmtWriter::VisitMemberExpr(MemberExpr *E) {
  VisitExpr(E);

  bool HasQualifier = E->hasQualifier();
  bool HasFoundDecl = E->hasFoundDecl();
  bool HasTemplateInfo = E->hasTemplateKWAndArgsInfo();
  unsigned NumTemplateArgs = E->getNumTemplateArgs();

  // Write these first for easy access when deserializing, as they affect the
  // size of the MemberExpr.
  CurrentPackingBits.updateBits();
  CurrentPackingBits.addBit(HasQualifier);
  CurrentPackingBits.addBit(HasFoundDecl);
  CurrentPackingBits.addBit(HasTemplateInfo);
  Record.push_back(NumTemplateArgs);

  Record.AddStmt(E->getBase());
  Record.AddDeclRef(E->getMemberDecl());
  Record.AddDeclarationNameLoc(E->MemberDNLoc,
                               E->getMemberDecl()->getDeclName());
  Record.AddSourceLocation(E->getMemberLoc());
  CurrentPackingBits.addBit(E->isArrow());
  CurrentPackingBits.addBit(E->hadMultipleCandidates());
  CurrentPackingBits.addBits(E->isNonOdrUse(), /*Width=*/2);
  Record.AddSourceLocation(E->getOperatorLoc());

  if (HasQualifier)
    Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());

  if (HasFoundDecl) {
    DeclAccessPair FoundDecl = E->getFoundDecl();
    Record.AddDeclRef(FoundDecl.getDecl());
    CurrentPackingBits.addBits(FoundDecl.getAccess(), /*BitWidth=*/2);
  }

  if (HasTemplateInfo)
    AddTemplateKWAndArgsInfo(*E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
                             E->getTrailingObjects<TemplateArgumentLoc>());

  Code = serialization::EXPR_MEMBER;
}

void ASTStmtWriter::VisitObjCIsaExpr(ObjCIsaExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getBase());
  Record.AddSourceLocation(E->getIsaMemberLoc());
  Record.AddSourceLocation(E->getOpLoc());
  Record.push_back(E->isArrow());
  Code = serialization::EXPR_OBJC_ISA;
}

void ASTStmtWriter::
VisitObjCIndirectCopyRestoreExpr(ObjCIndirectCopyRestoreExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSubExpr());
  Record.push_back(E->shouldCopy());
  Code = serialization::EXPR_OBJC_INDIRECT_COPY_RESTORE;
}

void ASTStmtWriter::VisitObjCBridgedCastExpr(ObjCBridgedCastExpr *E) {
  VisitExplicitCastExpr(E);
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getBridgeKeywordLoc());
  Record.push_back(E->getBridgeKind()); // FIXME: Stable encoding
  Code = serialization::EXPR_OBJC_BRIDGED_CAST;
}

void ASTStmtWriter::VisitCastExpr(CastExpr *E) {
  VisitExpr(E);

  Record.push_back(E->path_size());
  CurrentPackingBits.updateBits();
  // 7 bits should be enough to store the casting kinds.
  CurrentPackingBits.addBits(E->getCastKind(), /*Width=*/7);
  CurrentPackingBits.addBit(E->hasStoredFPFeatures());
  Record.AddStmt(E->getSubExpr());

  for (CastExpr::path_iterator
         PI = E->path_begin(), PE = E->path_end(); PI != PE; ++PI)
    Record.AddCXXBaseSpecifier(**PI);

  if (E->hasStoredFPFeatures())
    Record.push_back(E->getFPFeatures().getAsOpaqueInt());
}

void ASTStmtWriter::VisitBinaryOperator(BinaryOperator *E) {
  VisitExpr(E);

  // Write this first for easy access when deserializing, as they affect the
  // size of the UnaryOperator.
  CurrentPackingBits.updateBits();
  CurrentPackingBits.addBits(E->getOpcode(), /*Width=*/6);
  bool HasFPFeatures = E->hasStoredFPFeatures();
  CurrentPackingBits.addBit(HasFPFeatures);
  Record.AddStmt(E->getLHS());
  Record.AddStmt(E->getRHS());
  Record.AddSourceLocation(E->getOperatorLoc());
  if (HasFPFeatures)
    Record.push_back(E->getStoredFPFeatures().getAsOpaqueInt());

  if (!HasFPFeatures && E->getValueKind() == VK_PRValue &&
      E->getObjectKind() == OK_Ordinary)
    AbbrevToUse = Writer.getBinaryOperatorAbbrev();

  Code = serialization::EXPR_BINARY_OPERATOR;
}

void ASTStmtWriter::VisitCompoundAssignOperator(CompoundAssignOperator *E) {
  VisitBinaryOperator(E);
  Record.AddTypeRef(E->getComputationLHSType());
  Record.AddTypeRef(E->getComputationResultType());

  if (!E->hasStoredFPFeatures() && E->getValueKind() == VK_PRValue &&
      E->getObjectKind() == OK_Ordinary)
    AbbrevToUse = Writer.getCompoundAssignOperatorAbbrev();

  Code = serialization::EXPR_COMPOUND_ASSIGN_OPERATOR;
}

void ASTStmtWriter::VisitConditionalOperator(ConditionalOperator *E) {
  VisitExpr(E);
  Record.AddStmt(E->getCond());
  Record.AddStmt(E->getLHS());
  Record.AddStmt(E->getRHS());
  Record.AddSourceLocation(E->getQuestionLoc());
  Record.AddSourceLocation(E->getColonLoc());
  Code = serialization::EXPR_CONDITIONAL_OPERATOR;
}

void
ASTStmtWriter::VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
  VisitExpr(E);
  Record.AddStmt(E->getOpaqueValue());
  Record.AddStmt(E->getCommon());
  Record.AddStmt(E->getCond());
  Record.AddStmt(E->getTrueExpr());
  Record.AddStmt(E->getFalseExpr());
  Record.AddSourceLocation(E->getQuestionLoc());
  Record.AddSourceLocation(E->getColonLoc());
  Code = serialization::EXPR_BINARY_CONDITIONAL_OPERATOR;
}

void ASTStmtWriter::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  VisitCastExpr(E);
  CurrentPackingBits.addBit(E->isPartOfExplicitCast());

  if (E->path_size() == 0 && !E->hasStoredFPFeatures())
    AbbrevToUse = Writer.getExprImplicitCastAbbrev();

  Code = serialization::EXPR_IMPLICIT_CAST;
}

void ASTStmtWriter::VisitExplicitCastExpr(ExplicitCastExpr *E) {
  VisitCastExpr(E);
  Record.AddTypeSourceInfo(E->getTypeInfoAsWritten());
}

void ASTStmtWriter::VisitCStyleCastExpr(CStyleCastExpr *E) {
  VisitExplicitCastExpr(E);
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_CSTYLE_CAST;
}

void ASTStmtWriter::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());
  Record.AddStmt(E->getInitializer());
  Record.push_back(E->isFileScope());
  Code = serialization::EXPR_COMPOUND_LITERAL;
}

void ASTStmtWriter::VisitExtVectorElementExpr(ExtVectorElementExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getBase());
  Record.AddIdentifierRef(&E->getAccessor());
  Record.AddSourceLocation(E->getAccessorLoc());
  Code = serialization::EXPR_EXT_VECTOR_ELEMENT;
}

void ASTStmtWriter::VisitInitListExpr(InitListExpr *E) {
  VisitExpr(E);
  // NOTE: only add the (possibly null) syntactic form.
  // No need to serialize the isSemanticForm flag and the semantic form.
  Record.AddStmt(E->getSyntacticForm());
  Record.AddSourceLocation(E->getLBraceLoc());
  Record.AddSourceLocation(E->getRBraceLoc());
  bool isArrayFiller = E->ArrayFillerOrUnionFieldInit.is<Expr*>();
  Record.push_back(isArrayFiller);
  if (isArrayFiller)
    Record.AddStmt(E->getArrayFiller());
  else
    Record.AddDeclRef(E->getInitializedFieldInUnion());
  Record.push_back(E->hadArrayRangeDesignator());
  Record.push_back(E->getNumInits());
  if (isArrayFiller) {
    // ArrayFiller may have filled "holes" due to designated initializer.
    // Replace them by 0 to indicate that the filler goes in that place.
    Expr *filler = E->getArrayFiller();
    for (unsigned I = 0, N = E->getNumInits(); I != N; ++I)
      Record.AddStmt(E->getInit(I) != filler ? E->getInit(I) : nullptr);
  } else {
    for (unsigned I = 0, N = E->getNumInits(); I != N; ++I)
      Record.AddStmt(E->getInit(I));
  }
  Code = serialization::EXPR_INIT_LIST;
}

void ASTStmtWriter::VisitDesignatedInitExpr(DesignatedInitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumSubExprs());
  for (unsigned I = 0, N = E->getNumSubExprs(); I != N; ++I)
    Record.AddStmt(E->getSubExpr(I));
  Record.AddSourceLocation(E->getEqualOrColonLoc());
  Record.push_back(E->usesGNUSyntax());
  for (const DesignatedInitExpr::Designator &D : E->designators()) {
    if (D.isFieldDesignator()) {
      if (FieldDecl *Field = D.getFieldDecl()) {
        Record.push_back(serialization::DESIG_FIELD_DECL);
        Record.AddDeclRef(Field);
      } else {
        Record.push_back(serialization::DESIG_FIELD_NAME);
        Record.AddIdentifierRef(D.getFieldName());
      }
      Record.AddSourceLocation(D.getDotLoc());
      Record.AddSourceLocation(D.getFieldLoc());
    } else if (D.isArrayDesignator()) {
      Record.push_back(serialization::DESIG_ARRAY);
      Record.push_back(D.getArrayIndex());
      Record.AddSourceLocation(D.getLBracketLoc());
      Record.AddSourceLocation(D.getRBracketLoc());
    } else {
      assert(D.isArrayRangeDesignator() && "Unknown designator");
      Record.push_back(serialization::DESIG_ARRAY_RANGE);
      Record.push_back(D.getArrayIndex());
      Record.AddSourceLocation(D.getLBracketLoc());
      Record.AddSourceLocation(D.getEllipsisLoc());
      Record.AddSourceLocation(D.getRBracketLoc());
    }
  }
  Code = serialization::EXPR_DESIGNATED_INIT;
}

void ASTStmtWriter::VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getBase());
  Record.AddStmt(E->getUpdater());
  Code = serialization::EXPR_DESIGNATED_INIT_UPDATE;
}

void ASTStmtWriter::VisitNoInitExpr(NoInitExpr *E) {
  VisitExpr(E);
  Code = serialization::EXPR_NO_INIT;
}

void ASTStmtWriter::VisitArrayInitLoopExpr(ArrayInitLoopExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->SubExprs[0]);
  Record.AddStmt(E->SubExprs[1]);
  Code = serialization::EXPR_ARRAY_INIT_LOOP;
}

void ASTStmtWriter::VisitArrayInitIndexExpr(ArrayInitIndexExpr *E) {
  VisitExpr(E);
  Code = serialization::EXPR_ARRAY_INIT_INDEX;
}

void ASTStmtWriter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  VisitExpr(E);
  Code = serialization::EXPR_IMPLICIT_VALUE_INIT;
}

void ASTStmtWriter::VisitVAArgExpr(VAArgExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSubExpr());
  Record.AddTypeSourceInfo(E->getWrittenTypeInfo());
  Record.AddSourceLocation(E->getBuiltinLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.push_back(E->isMicrosoftABI());
  Code = serialization::EXPR_VA_ARG;
}

void ASTStmtWriter::VisitSourceLocExpr(SourceLocExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(cast_or_null<Decl>(E->getParentContext()));
  Record.AddSourceLocation(E->getBeginLoc());
  Record.AddSourceLocation(E->getEndLoc());
  Record.push_back(llvm::to_underlying(E->getIdentKind()));
  Code = serialization::EXPR_SOURCE_LOC;
}

void ASTStmtWriter::VisitEmbedExpr(EmbedExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getBeginLoc());
  Record.AddSourceLocation(E->getEndLoc());
  Record.AddStmt(E->getDataStringLiteral());
  Record.writeUInt32(E->getStartingElementPos());
  Record.writeUInt32(E->getDataElementCount());
  Code = serialization::EXPR_BUILTIN_PP_EMBED;
}

void ASTStmtWriter::VisitAddrLabelExpr(AddrLabelExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getAmpAmpLoc());
  Record.AddSourceLocation(E->getLabelLoc());
  Record.AddDeclRef(E->getLabel());
  Code = serialization::EXPR_ADDR_LABEL;
}

void ASTStmtWriter::VisitStmtExpr(StmtExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSubStmt());
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.push_back(E->getTemplateDepth());
  Code = serialization::EXPR_STMT;
}

void ASTStmtWriter::VisitChooseExpr(ChooseExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getCond());
  Record.AddStmt(E->getLHS());
  Record.AddStmt(E->getRHS());
  Record.AddSourceLocation(E->getBuiltinLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.push_back(E->isConditionDependent() ? false : E->isConditionTrue());
  Code = serialization::EXPR_CHOOSE;
}

void ASTStmtWriter::VisitGNUNullExpr(GNUNullExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getTokenLocation());
  Code = serialization::EXPR_GNU_NULL;
}

void ASTStmtWriter::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumSubExprs());
  for (unsigned I = 0, N = E->getNumSubExprs(); I != N; ++I)
    Record.AddStmt(E->getExpr(I));
  Record.AddSourceLocation(E->getBuiltinLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_SHUFFLE_VECTOR;
}

void ASTStmtWriter::VisitConvertVectorExpr(ConvertVectorExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getBuiltinLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());
  Record.AddStmt(E->getSrcExpr());
  Code = serialization::EXPR_CONVERT_VECTOR;
}

void ASTStmtWriter::VisitBlockExpr(BlockExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getBlockDecl());
  Code = serialization::EXPR_BLOCK;
}

void ASTStmtWriter::VisitGenericSelectionExpr(GenericSelectionExpr *E) {
  VisitExpr(E);

  Record.push_back(E->getNumAssocs());
  Record.push_back(E->isExprPredicate());
  Record.push_back(E->ResultIndex);
  Record.AddSourceLocation(E->getGenericLoc());
  Record.AddSourceLocation(E->getDefaultLoc());
  Record.AddSourceLocation(E->getRParenLoc());

  Stmt **Stmts = E->getTrailingObjects<Stmt *>();
  // Add 1 to account for the controlling expression which is the first
  // expression in the trailing array of Stmt *. This is not needed for
  // the trailing array of TypeSourceInfo *.
  for (unsigned I = 0, N = E->getNumAssocs() + 1; I < N; ++I)
    Record.AddStmt(Stmts[I]);

  TypeSourceInfo **TSIs = E->getTrailingObjects<TypeSourceInfo *>();
  for (unsigned I = 0, N = E->getNumAssocs(); I < N; ++I)
    Record.AddTypeSourceInfo(TSIs[I]);

  Code = serialization::EXPR_GENERIC_SELECTION;
}

void ASTStmtWriter::VisitPseudoObjectExpr(PseudoObjectExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumSemanticExprs());

  // Push the result index.  Currently, this needs to exactly match
  // the encoding used internally for ResultIndex.
  unsigned result = E->getResultExprIndex();
  result = (result == PseudoObjectExpr::NoResult ? 0 : result + 1);
  Record.push_back(result);

  Record.AddStmt(E->getSyntacticForm());
  for (PseudoObjectExpr::semantics_iterator
         i = E->semantics_begin(), e = E->semantics_end(); i != e; ++i) {
    Record.AddStmt(*i);
  }
  Code = serialization::EXPR_PSEUDO_OBJECT;
}

void ASTStmtWriter::VisitAtomicExpr(AtomicExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getOp());
  for (unsigned I = 0, N = E->getNumSubExprs(); I != N; ++I)
    Record.AddStmt(E->getSubExprs()[I]);
  Record.AddSourceLocation(E->getBuiltinLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_ATOMIC;
}

//===----------------------------------------------------------------------===//
// Objective-C Expressions and Statements.
//===----------------------------------------------------------------------===//

void ASTStmtWriter::VisitObjCStringLiteral(ObjCStringLiteral *E) {
  VisitExpr(E);
  Record.AddStmt(E->getString());
  Record.AddSourceLocation(E->getAtLoc());
  Code = serialization::EXPR_OBJC_STRING_LITERAL;
}

void ASTStmtWriter::VisitObjCBoxedExpr(ObjCBoxedExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSubExpr());
  Record.AddDeclRef(E->getBoxingMethod());
  Record.AddSourceRange(E->getSourceRange());
  Code = serialization::EXPR_OBJC_BOXED_EXPRESSION;
}

void ASTStmtWriter::VisitObjCArrayLiteral(ObjCArrayLiteral *E) {
  VisitExpr(E);
  Record.push_back(E->getNumElements());
  for (unsigned i = 0; i < E->getNumElements(); i++)
    Record.AddStmt(E->getElement(i));
  Record.AddDeclRef(E->getArrayWithObjectsMethod());
  Record.AddSourceRange(E->getSourceRange());
  Code = serialization::EXPR_OBJC_ARRAY_LITERAL;
}

void ASTStmtWriter::VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
  VisitExpr(E);
  Record.push_back(E->getNumElements());
  Record.push_back(E->HasPackExpansions);
  for (unsigned i = 0; i < E->getNumElements(); i++) {
    ObjCDictionaryElement Element = E->getKeyValueElement(i);
    Record.AddStmt(Element.Key);
    Record.AddStmt(Element.Value);
    if (E->HasPackExpansions) {
      Record.AddSourceLocation(Element.EllipsisLoc);
      unsigned NumExpansions = 0;
      if (Element.NumExpansions)
        NumExpansions = *Element.NumExpansions + 1;
      Record.push_back(NumExpansions);
    }
  }

  Record.AddDeclRef(E->getDictWithObjectsMethod());
  Record.AddSourceRange(E->getSourceRange());
  Code = serialization::EXPR_OBJC_DICTIONARY_LITERAL;
}

void ASTStmtWriter::VisitObjCEncodeExpr(ObjCEncodeExpr *E) {
  VisitExpr(E);
  Record.AddTypeSourceInfo(E->getEncodedTypeSourceInfo());
  Record.AddSourceLocation(E->getAtLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_OBJC_ENCODE;
}

void ASTStmtWriter::VisitObjCSelectorExpr(ObjCSelectorExpr *E) {
  VisitExpr(E);
  Record.AddSelectorRef(E->getSelector());
  Record.AddSourceLocation(E->getAtLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_OBJC_SELECTOR_EXPR;
}

void ASTStmtWriter::VisitObjCProtocolExpr(ObjCProtocolExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getProtocol());
  Record.AddSourceLocation(E->getAtLoc());
  Record.AddSourceLocation(E->ProtoLoc);
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_OBJC_PROTOCOL_EXPR;
}

void ASTStmtWriter::VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getDecl());
  Record.AddSourceLocation(E->getLocation());
  Record.AddSourceLocation(E->getOpLoc());
  Record.AddStmt(E->getBase());
  Record.push_back(E->isArrow());
  Record.push_back(E->isFreeIvar());
  Code = serialization::EXPR_OBJC_IVAR_REF_EXPR;
}

void ASTStmtWriter::VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
  VisitExpr(E);
  Record.push_back(E->SetterAndMethodRefFlags.getInt());
  Record.push_back(E->isImplicitProperty());
  if (E->isImplicitProperty()) {
    Record.AddDeclRef(E->getImplicitPropertyGetter());
    Record.AddDeclRef(E->getImplicitPropertySetter());
  } else {
    Record.AddDeclRef(E->getExplicitProperty());
  }
  Record.AddSourceLocation(E->getLocation());
  Record.AddSourceLocation(E->getReceiverLocation());
  if (E->isObjectReceiver()) {
    Record.push_back(0);
    Record.AddStmt(E->getBase());
  } else if (E->isSuperReceiver()) {
    Record.push_back(1);
    Record.AddTypeRef(E->getSuperReceiverType());
  } else {
    Record.push_back(2);
    Record.AddDeclRef(E->getClassReceiver());
  }

  Code = serialization::EXPR_OBJC_PROPERTY_REF_EXPR;
}

void ASTStmtWriter::VisitObjCSubscriptRefExpr(ObjCSubscriptRefExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getRBracket());
  Record.AddStmt(E->getBaseExpr());
  Record.AddStmt(E->getKeyExpr());
  Record.AddDeclRef(E->getAtIndexMethodDecl());
  Record.AddDeclRef(E->setAtIndexMethodDecl());

  Code = serialization::EXPR_OBJC_SUBSCRIPT_REF_EXPR;
}

void ASTStmtWriter::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumArgs());
  Record.push_back(E->getNumStoredSelLocs());
  Record.push_back(E->SelLocsKind);
  Record.push_back(E->isDelegateInitCall());
  Record.push_back(E->IsImplicit);
  Record.push_back((unsigned)E->getReceiverKind()); // FIXME: stable encoding
  switch (E->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
    Record.AddStmt(E->getInstanceReceiver());
    break;

  case ObjCMessageExpr::Class:
    Record.AddTypeSourceInfo(E->getClassReceiverTypeInfo());
    break;

  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance:
    Record.AddTypeRef(E->getSuperType());
    Record.AddSourceLocation(E->getSuperLoc());
    break;
  }

  if (E->getMethodDecl()) {
    Record.push_back(1);
    Record.AddDeclRef(E->getMethodDecl());
  } else {
    Record.push_back(0);
    Record.AddSelectorRef(E->getSelector());
  }

  Record.AddSourceLocation(E->getLeftLoc());
  Record.AddSourceLocation(E->getRightLoc());

  for (CallExpr::arg_iterator Arg = E->arg_begin(), ArgEnd = E->arg_end();
       Arg != ArgEnd; ++Arg)
    Record.AddStmt(*Arg);

  SourceLocation *Locs = E->getStoredSelLocs();
  for (unsigned i = 0, e = E->getNumStoredSelLocs(); i != e; ++i)
    Record.AddSourceLocation(Locs[i]);

  Code = serialization::EXPR_OBJC_MESSAGE_EXPR;
}

void ASTStmtWriter::VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  VisitStmt(S);
  Record.AddStmt(S->getElement());
  Record.AddStmt(S->getCollection());
  Record.AddStmt(S->getBody());
  Record.AddSourceLocation(S->getForLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  Code = serialization::STMT_OBJC_FOR_COLLECTION;
}

void ASTStmtWriter::VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {
  VisitStmt(S);
  Record.AddStmt(S->getCatchBody());
  Record.AddDeclRef(S->getCatchParamDecl());
  Record.AddSourceLocation(S->getAtCatchLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  Code = serialization::STMT_OBJC_CATCH;
}

void ASTStmtWriter::VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
  VisitStmt(S);
  Record.AddStmt(S->getFinallyBody());
  Record.AddSourceLocation(S->getAtFinallyLoc());
  Code = serialization::STMT_OBJC_FINALLY;
}

void ASTStmtWriter::VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S) {
  VisitStmt(S); // FIXME: no test coverage.
  Record.AddStmt(S->getSubStmt());
  Record.AddSourceLocation(S->getAtLoc());
  Code = serialization::STMT_OBJC_AUTORELEASE_POOL;
}

void ASTStmtWriter::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
  VisitStmt(S);
  Record.push_back(S->getNumCatchStmts());
  Record.push_back(S->getFinallyStmt() != nullptr);
  Record.AddStmt(S->getTryBody());
  for (ObjCAtCatchStmt *C : S->catch_stmts())
    Record.AddStmt(C);
  if (S->getFinallyStmt())
    Record.AddStmt(S->getFinallyStmt());
  Record.AddSourceLocation(S->getAtTryLoc());
  Code = serialization::STMT_OBJC_AT_TRY;
}

void ASTStmtWriter::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
  VisitStmt(S); // FIXME: no test coverage.
  Record.AddStmt(S->getSynchExpr());
  Record.AddStmt(S->getSynchBody());
  Record.AddSourceLocation(S->getAtSynchronizedLoc());
  Code = serialization::STMT_OBJC_AT_SYNCHRONIZED;
}

void ASTStmtWriter::VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  VisitStmt(S); // FIXME: no test coverage.
  Record.AddStmt(S->getThrowExpr());
  Record.AddSourceLocation(S->getThrowLoc());
  Code = serialization::STMT_OBJC_AT_THROW;
}

void ASTStmtWriter::VisitObjCBoolLiteralExpr(ObjCBoolLiteralExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getValue());
  Record.AddSourceLocation(E->getLocation());
  Code = serialization::EXPR_OBJC_BOOL_LITERAL;
}

void ASTStmtWriter::VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *E) {
  VisitExpr(E);
  Record.AddSourceRange(E->getSourceRange());
  Record.AddVersionTuple(E->getVersion());
  Code = serialization::EXPR_OBJC_AVAILABILITY_CHECK;
}

//===----------------------------------------------------------------------===//
// C++ Expressions and Statements.
//===----------------------------------------------------------------------===//

void ASTStmtWriter::VisitCXXCatchStmt(CXXCatchStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getCatchLoc());
  Record.AddDeclRef(S->getExceptionDecl());
  Record.AddStmt(S->getHandlerBlock());
  Code = serialization::STMT_CXX_CATCH;
}

void ASTStmtWriter::VisitCXXTryStmt(CXXTryStmt *S) {
  VisitStmt(S);
  Record.push_back(S->getNumHandlers());
  Record.AddSourceLocation(S->getTryLoc());
  Record.AddStmt(S->getTryBlock());
  for (unsigned i = 0, e = S->getNumHandlers(); i != e; ++i)
    Record.AddStmt(S->getHandler(i));
  Code = serialization::STMT_CXX_TRY;
}

void ASTStmtWriter::VisitCXXForRangeStmt(CXXForRangeStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getForLoc());
  Record.AddSourceLocation(S->getCoawaitLoc());
  Record.AddSourceLocation(S->getColonLoc());
  Record.AddSourceLocation(S->getRParenLoc());
  Record.AddStmt(S->getInit());
  Record.AddStmt(S->getRangeStmt());
  Record.AddStmt(S->getBeginStmt());
  Record.AddStmt(S->getEndStmt());
  Record.AddStmt(S->getCond());
  Record.AddStmt(S->getInc());
  Record.AddStmt(S->getLoopVarStmt());
  Record.AddStmt(S->getBody());
  Code = serialization::STMT_CXX_FOR_RANGE;
}

void ASTStmtWriter::VisitMSDependentExistsStmt(MSDependentExistsStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getKeywordLoc());
  Record.push_back(S->isIfExists());
  Record.AddNestedNameSpecifierLoc(S->getQualifierLoc());
  Record.AddDeclarationNameInfo(S->getNameInfo());
  Record.AddStmt(S->getSubStmt());
  Code = serialization::STMT_MS_DEPENDENT_EXISTS;
}

void ASTStmtWriter::VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
  VisitCallExpr(E);
  Record.push_back(E->getOperator());
  Record.AddSourceRange(E->Range);

  if (!E->hasStoredFPFeatures() && !static_cast<bool>(E->getADLCallKind()))
    AbbrevToUse = Writer.getCXXOperatorCallExprAbbrev();

  Code = serialization::EXPR_CXX_OPERATOR_CALL;
}

void ASTStmtWriter::VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
  VisitCallExpr(E);

  if (!E->hasStoredFPFeatures() && !static_cast<bool>(E->getADLCallKind()))
    AbbrevToUse = Writer.getCXXMemberCallExprAbbrev();

  Code = serialization::EXPR_CXX_MEMBER_CALL;
}

void ASTStmtWriter::VisitCXXRewrittenBinaryOperator(
    CXXRewrittenBinaryOperator *E) {
  VisitExpr(E);
  Record.push_back(E->isReversed());
  Record.AddStmt(E->getSemanticForm());
  Code = serialization::EXPR_CXX_REWRITTEN_BINARY_OPERATOR;
}

void ASTStmtWriter::VisitCXXConstructExpr(CXXConstructExpr *E) {
  VisitExpr(E);

  Record.push_back(E->getNumArgs());
  Record.push_back(E->isElidable());
  Record.push_back(E->hadMultipleCandidates());
  Record.push_back(E->isListInitialization());
  Record.push_back(E->isStdInitListInitialization());
  Record.push_back(E->requiresZeroInitialization());
  Record.push_back(
      llvm::to_underlying(E->getConstructionKind())); // FIXME: stable encoding
  Record.push_back(E->isImmediateEscalating());
  Record.AddSourceLocation(E->getLocation());
  Record.AddDeclRef(E->getConstructor());
  Record.AddSourceRange(E->getParenOrBraceRange());

  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    Record.AddStmt(E->getArg(I));

  Code = serialization::EXPR_CXX_CONSTRUCT;
}

void ASTStmtWriter::VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getConstructor());
  Record.AddSourceLocation(E->getLocation());
  Record.push_back(E->constructsVBase());
  Record.push_back(E->inheritedFromVBase());
  Code = serialization::EXPR_CXX_INHERITED_CTOR_INIT;
}

void ASTStmtWriter::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E) {
  VisitCXXConstructExpr(E);
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());
  Code = serialization::EXPR_CXX_TEMPORARY_OBJECT;
}

void ASTStmtWriter::VisitLambdaExpr(LambdaExpr *E) {
  VisitExpr(E);
  Record.push_back(E->LambdaExprBits.NumCaptures);
  Record.AddSourceRange(E->IntroducerRange);
  Record.push_back(E->LambdaExprBits.CaptureDefault); // FIXME: stable encoding
  Record.AddSourceLocation(E->CaptureDefaultLoc);
  Record.push_back(E->LambdaExprBits.ExplicitParams);
  Record.push_back(E->LambdaExprBits.ExplicitResultType);
  Record.AddSourceLocation(E->ClosingBrace);

  // Add capture initializers.
  for (LambdaExpr::capture_init_iterator C = E->capture_init_begin(),
                                      CEnd = E->capture_init_end();
       C != CEnd; ++C) {
    Record.AddStmt(*C);
  }

  // Don't serialize the body. It belongs to the call operator declaration.
  // LambdaExpr only stores a copy of the Stmt *.

  Code = serialization::EXPR_LAMBDA;
}

void ASTStmtWriter::VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_CXX_STD_INITIALIZER_LIST;
}

void ASTStmtWriter::VisitCXXNamedCastExpr(CXXNamedCastExpr *E) {
  VisitExplicitCastExpr(E);
  Record.AddSourceRange(SourceRange(E->getOperatorLoc(), E->getRParenLoc()));
  CurrentPackingBits.addBit(E->getAngleBrackets().isValid());
  if (E->getAngleBrackets().isValid())
    Record.AddSourceRange(E->getAngleBrackets());
}

void ASTStmtWriter::VisitCXXStaticCastExpr(CXXStaticCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = serialization::EXPR_CXX_STATIC_CAST;
}

void ASTStmtWriter::VisitCXXDynamicCastExpr(CXXDynamicCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = serialization::EXPR_CXX_DYNAMIC_CAST;
}

void ASTStmtWriter::VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = serialization::EXPR_CXX_REINTERPRET_CAST;
}

void ASTStmtWriter::VisitCXXConstCastExpr(CXXConstCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = serialization::EXPR_CXX_CONST_CAST;
}

void ASTStmtWriter::VisitCXXAddrspaceCastExpr(CXXAddrspaceCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = serialization::EXPR_CXX_ADDRSPACE_CAST;
}

void ASTStmtWriter::VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E) {
  VisitExplicitCastExpr(E);
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_CXX_FUNCTIONAL_CAST;
}

void ASTStmtWriter::VisitBuiltinBitCastExpr(BuiltinBitCastExpr *E) {
  VisitExplicitCastExpr(E);
  Record.AddSourceLocation(E->getBeginLoc());
  Record.AddSourceLocation(E->getEndLoc());
  Code = serialization::EXPR_BUILTIN_BIT_CAST;
}

void ASTStmtWriter::VisitUserDefinedLiteral(UserDefinedLiteral *E) {
  VisitCallExpr(E);
  Record.AddSourceLocation(E->UDSuffixLoc);
  Code = serialization::EXPR_USER_DEFINED_LITERAL;
}

void ASTStmtWriter::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getValue());
  Record.AddSourceLocation(E->getLocation());
  Code = serialization::EXPR_CXX_BOOL_LITERAL;
}

void ASTStmtWriter::VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getLocation());
  Code = serialization::EXPR_CXX_NULL_PTR_LITERAL;
}

void ASTStmtWriter::VisitCXXTypeidExpr(CXXTypeidExpr *E) {
  VisitExpr(E);
  Record.AddSourceRange(E->getSourceRange());
  if (E->isTypeOperand()) {
    Record.AddTypeSourceInfo(E->getTypeOperandSourceInfo());
    Code = serialization::EXPR_CXX_TYPEID_TYPE;
  } else {
    Record.AddStmt(E->getExprOperand());
    Code = serialization::EXPR_CXX_TYPEID_EXPR;
  }
}

void ASTStmtWriter::VisitCXXThisExpr(CXXThisExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getLocation());
  Record.push_back(E->isImplicit());
  Record.push_back(E->isCapturedByCopyInLambdaWithExplicitObjectParameter());

  Code = serialization::EXPR_CXX_THIS;
}

void ASTStmtWriter::VisitCXXThrowExpr(CXXThrowExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getThrowLoc());
  Record.AddStmt(E->getSubExpr());
  Record.push_back(E->isThrownVariableInScope());
  Code = serialization::EXPR_CXX_THROW;
}

void ASTStmtWriter::VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getParam());
  Record.AddDeclRef(cast_or_null<Decl>(E->getUsedContext()));
  Record.AddSourceLocation(E->getUsedLocation());
  Record.push_back(E->hasRewrittenInit());
  if (E->hasRewrittenInit())
    Record.AddStmt(E->getRewrittenExpr());
  Code = serialization::EXPR_CXX_DEFAULT_ARG;
}

void ASTStmtWriter::VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->hasRewrittenInit());
  Record.AddDeclRef(E->getField());
  Record.AddDeclRef(cast_or_null<Decl>(E->getUsedContext()));
  Record.AddSourceLocation(E->getExprLoc());
  if (E->hasRewrittenInit())
    Record.AddStmt(E->getRewrittenExpr());
  Code = serialization::EXPR_CXX_DEFAULT_INIT;
}

void ASTStmtWriter::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  VisitExpr(E);
  Record.AddCXXTemporary(E->getTemporary());
  Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_CXX_BIND_TEMPORARY;
}

void ASTStmtWriter::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E) {
  VisitExpr(E);
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());
  Record.AddSourceLocation(E->getRParenLoc());
  Code = serialization::EXPR_CXX_SCALAR_VALUE_INIT;
}

void ASTStmtWriter::VisitCXXNewExpr(CXXNewExpr *E) {
  VisitExpr(E);

  Record.push_back(E->isArray());
  Record.push_back(E->hasInitializer());
  Record.push_back(E->getNumPlacementArgs());
  Record.push_back(E->isParenTypeId());

  Record.push_back(E->isGlobalNew());
  Record.push_back(E->passAlignment());
  Record.push_back(E->doesUsualArrayDeleteWantSize());
  Record.push_back(E->CXXNewExprBits.HasInitializer);
  Record.push_back(E->CXXNewExprBits.StoredInitializationStyle);

  Record.AddDeclRef(E->getOperatorNew());
  Record.AddDeclRef(E->getOperatorDelete());
  Record.AddTypeSourceInfo(E->getAllocatedTypeSourceInfo());
  if (E->isParenTypeId())
    Record.AddSourceRange(E->getTypeIdParens());
  Record.AddSourceRange(E->getSourceRange());
  Record.AddSourceRange(E->getDirectInitRange());

  for (CXXNewExpr::arg_iterator I = E->raw_arg_begin(), N = E->raw_arg_end();
       I != N; ++I)
    Record.AddStmt(*I);

  Code = serialization::EXPR_CXX_NEW;
}

void ASTStmtWriter::VisitCXXDeleteExpr(CXXDeleteExpr *E) {
  VisitExpr(E);
  Record.push_back(E->isGlobalDelete());
  Record.push_back(E->isArrayForm());
  Record.push_back(E->isArrayFormAsWritten());
  Record.push_back(E->doesUsualArrayDeleteWantSize());
  Record.AddDeclRef(E->getOperatorDelete());
  Record.AddStmt(E->getArgument());
  Record.AddSourceLocation(E->getBeginLoc());

  Code = serialization::EXPR_CXX_DELETE;
}

void ASTStmtWriter::VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E) {
  VisitExpr(E);

  Record.AddStmt(E->getBase());
  Record.push_back(E->isArrow());
  Record.AddSourceLocation(E->getOperatorLoc());
  Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());
  Record.AddTypeSourceInfo(E->getScopeTypeInfo());
  Record.AddSourceLocation(E->getColonColonLoc());
  Record.AddSourceLocation(E->getTildeLoc());

  // PseudoDestructorTypeStorage.
  Record.AddIdentifierRef(E->getDestroyedTypeIdentifier());
  if (E->getDestroyedTypeIdentifier())
    Record.AddSourceLocation(E->getDestroyedTypeLoc());
  else
    Record.AddTypeSourceInfo(E->getDestroyedTypeInfo());

  Code = serialization::EXPR_CXX_PSEUDO_DESTRUCTOR;
}

void ASTStmtWriter::VisitExprWithCleanups(ExprWithCleanups *E) {
  VisitExpr(E);
  Record.push_back(E->getNumObjects());
  for (auto &Obj : E->getObjects()) {
    if (auto *BD = Obj.dyn_cast<BlockDecl *>()) {
      Record.push_back(serialization::COK_Block);
      Record.AddDeclRef(BD);
    } else if (auto *CLE = Obj.dyn_cast<CompoundLiteralExpr *>()) {
      Record.push_back(serialization::COK_CompoundLiteral);
      Record.AddStmt(CLE);
    }
  }

  Record.push_back(E->cleanupsHaveSideEffects());
  Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_EXPR_WITH_CLEANUPS;
}

void ASTStmtWriter::VisitCXXDependentScopeMemberExpr(
    CXXDependentScopeMemberExpr *E) {
  VisitExpr(E);

  // Don't emit anything here (or if you do you will have to update
  // the corresponding deserialization function).
  Record.push_back(E->getNumTemplateArgs());
  CurrentPackingBits.updateBits();
  CurrentPackingBits.addBit(E->hasTemplateKWAndArgsInfo());
  CurrentPackingBits.addBit(E->hasFirstQualifierFoundInScope());

  if (E->hasTemplateKWAndArgsInfo()) {
    const ASTTemplateKWAndArgsInfo &ArgInfo =
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>();
    AddTemplateKWAndArgsInfo(ArgInfo,
                             E->getTrailingObjects<TemplateArgumentLoc>());
  }

  CurrentPackingBits.addBit(E->isArrow());

  Record.AddTypeRef(E->getBaseType());
  Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());
  CurrentPackingBits.addBit(!E->isImplicitAccess());
  if (!E->isImplicitAccess())
    Record.AddStmt(E->getBase());

  Record.AddSourceLocation(E->getOperatorLoc());

  if (E->hasFirstQualifierFoundInScope())
    Record.AddDeclRef(E->getFirstQualifierFoundInScope());

  Record.AddDeclarationNameInfo(E->MemberNameInfo);
  Code = serialization::EXPR_CXX_DEPENDENT_SCOPE_MEMBER;
}

void
ASTStmtWriter::VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
  VisitExpr(E);

  // Don't emit anything here, HasTemplateKWAndArgsInfo must be
  // emitted first.
  CurrentPackingBits.addBit(
      E->DependentScopeDeclRefExprBits.HasTemplateKWAndArgsInfo);

  if (E->DependentScopeDeclRefExprBits.HasTemplateKWAndArgsInfo) {
    const ASTTemplateKWAndArgsInfo &ArgInfo =
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>();
    // 16 bits should be enought to store the number of args
    CurrentPackingBits.addBits(ArgInfo.NumTemplateArgs, /*Width=*/16);
    AddTemplateKWAndArgsInfo(ArgInfo,
                             E->getTrailingObjects<TemplateArgumentLoc>());
  }

  Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());
  Record.AddDeclarationNameInfo(E->NameInfo);
  Code = serialization::EXPR_CXX_DEPENDENT_SCOPE_DECL_REF;
}

void
ASTStmtWriter::VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumArgs());
  for (CXXUnresolvedConstructExpr::arg_iterator
         ArgI = E->arg_begin(), ArgE = E->arg_end(); ArgI != ArgE; ++ArgI)
    Record.AddStmt(*ArgI);
  Record.AddTypeSourceInfo(E->getTypeSourceInfo());
  Record.AddSourceLocation(E->getLParenLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.push_back(E->isListInitialization());
  Code = serialization::EXPR_CXX_UNRESOLVED_CONSTRUCT;
}

void ASTStmtWriter::VisitOverloadExpr(OverloadExpr *E) {
  VisitExpr(E);

  Record.push_back(E->getNumDecls());

  CurrentPackingBits.updateBits();
  CurrentPackingBits.addBit(E->hasTemplateKWAndArgsInfo());
  if (E->hasTemplateKWAndArgsInfo()) {
    const ASTTemplateKWAndArgsInfo &ArgInfo =
        *E->getTrailingASTTemplateKWAndArgsInfo();
    Record.push_back(ArgInfo.NumTemplateArgs);
    AddTemplateKWAndArgsInfo(ArgInfo, E->getTrailingTemplateArgumentLoc());
  }

  for (OverloadExpr::decls_iterator OvI = E->decls_begin(),
                                    OvE = E->decls_end();
       OvI != OvE; ++OvI) {
    Record.AddDeclRef(OvI.getDecl());
    Record.push_back(OvI.getAccess());
  }

  Record.AddDeclarationNameInfo(E->getNameInfo());
  Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());
}

void ASTStmtWriter::VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
  VisitOverloadExpr(E);
  CurrentPackingBits.addBit(E->isArrow());
  CurrentPackingBits.addBit(E->hasUnresolvedUsing());
  CurrentPackingBits.addBit(!E->isImplicitAccess());
  if (!E->isImplicitAccess())
    Record.AddStmt(E->getBase());

  Record.AddSourceLocation(E->getOperatorLoc());

  Record.AddTypeRef(E->getBaseType());
  Code = serialization::EXPR_CXX_UNRESOLVED_MEMBER;
}

void ASTStmtWriter::VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
  VisitOverloadExpr(E);
  CurrentPackingBits.addBit(E->requiresADL());
  Record.AddDeclRef(E->getNamingClass());
  Code = serialization::EXPR_CXX_UNRESOLVED_LOOKUP;

  if (Writer.isWritingStdCXXNamedModules() && Writer.getChain()) {
    // Referencing all the possible declarations to make sure the change get
    // propagted.
    DeclarationName Name = E->getName();
    for (auto *Found :
         Writer.getASTContext().getTranslationUnitDecl()->lookup(Name))
      if (Found->isFromASTFile())
        Writer.GetDeclRef(Found);

    llvm::SmallVector<NamespaceDecl *> ExternalNSs;
    Writer.getChain()->ReadKnownNamespaces(ExternalNSs);
    for (auto *NS : ExternalNSs)
      for (auto *Found : NS->lookup(Name))
        Writer.GetDeclRef(Found);
  }
}

void ASTStmtWriter::VisitTypeTraitExpr(TypeTraitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->TypeTraitExprBits.NumArgs);
  Record.push_back(E->TypeTraitExprBits.Kind); // FIXME: Stable encoding
  Record.push_back(E->TypeTraitExprBits.Value);
  Record.AddSourceRange(E->getSourceRange());
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    Record.AddTypeSourceInfo(E->getArg(I));
  Code = serialization::EXPR_TYPE_TRAIT;
}

void ASTStmtWriter::VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getTrait());
  Record.push_back(E->getValue());
  Record.AddSourceRange(E->getSourceRange());
  Record.AddTypeSourceInfo(E->getQueriedTypeSourceInfo());
  Record.AddStmt(E->getDimensionExpression());
  Code = serialization::EXPR_ARRAY_TYPE_TRAIT;
}

void ASTStmtWriter::VisitExpressionTraitExpr(ExpressionTraitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getTrait());
  Record.push_back(E->getValue());
  Record.AddSourceRange(E->getSourceRange());
  Record.AddStmt(E->getQueriedExpression());
  Code = serialization::EXPR_CXX_EXPRESSION_TRAIT;
}

void ASTStmtWriter::VisitCXXNoexceptExpr(CXXNoexceptExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getValue());
  Record.AddSourceRange(E->getSourceRange());
  Record.AddStmt(E->getOperand());
  Code = serialization::EXPR_CXX_NOEXCEPT;
}

void ASTStmtWriter::VisitPackExpansionExpr(PackExpansionExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getEllipsisLoc());
  Record.push_back(E->NumExpansions);
  Record.AddStmt(E->getPattern());
  Code = serialization::EXPR_PACK_EXPANSION;
}

void ASTStmtWriter::VisitSizeOfPackExpr(SizeOfPackExpr *E) {
  VisitExpr(E);
  Record.push_back(E->isPartiallySubstituted() ? E->getPartialArguments().size()
                                               : 0);
  Record.AddSourceLocation(E->OperatorLoc);
  Record.AddSourceLocation(E->PackLoc);
  Record.AddSourceLocation(E->RParenLoc);
  Record.AddDeclRef(E->Pack);
  if (E->isPartiallySubstituted()) {
    for (const auto &TA : E->getPartialArguments())
      Record.AddTemplateArgument(TA);
  } else if (!E->isValueDependent()) {
    Record.push_back(E->getPackLength());
  }
  Code = serialization::EXPR_SIZEOF_PACK;
}

void ASTStmtWriter::VisitPackIndexingExpr(PackIndexingExpr *E) {
  VisitExpr(E);
  Record.push_back(E->TransformedExpressions);
  Record.push_back(E->ExpandedToEmptyPack);
  Record.AddSourceLocation(E->getEllipsisLoc());
  Record.AddSourceLocation(E->getRSquareLoc());
  Record.AddStmt(E->getPackIdExpression());
  Record.AddStmt(E->getIndexExpr());
  for (Expr *Sub : E->getExpressions())
    Record.AddStmt(Sub);
  Code = serialization::EXPR_PACK_INDEXING;
}

void ASTStmtWriter::VisitSubstNonTypeTemplateParmExpr(
                                              SubstNonTypeTemplateParmExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getAssociatedDecl());
  CurrentPackingBits.addBit(E->isReferenceParameter());
  CurrentPackingBits.addBits(E->getIndex(), /*Width=*/12);
  CurrentPackingBits.addBit((bool)E->getPackIndex());
  if (auto PackIndex = E->getPackIndex())
    Record.push_back(*PackIndex + 1);

  Record.AddSourceLocation(E->getNameLoc());
  Record.AddStmt(E->getReplacement());
  Code = serialization::EXPR_SUBST_NON_TYPE_TEMPLATE_PARM;
}

void ASTStmtWriter::VisitSubstNonTypeTemplateParmPackExpr(
                                          SubstNonTypeTemplateParmPackExpr *E) {
  VisitExpr(E);
  Record.AddDeclRef(E->getAssociatedDecl());
  Record.push_back(E->getIndex());
  Record.AddTemplateArgument(E->getArgumentPack());
  Record.AddSourceLocation(E->getParameterPackLocation());
  Code = serialization::EXPR_SUBST_NON_TYPE_TEMPLATE_PARM_PACK;
}

void ASTStmtWriter::VisitFunctionParmPackExpr(FunctionParmPackExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumExpansions());
  Record.AddDeclRef(E->getParameterPack());
  Record.AddSourceLocation(E->getParameterPackLocation());
  for (FunctionParmPackExpr::iterator I = E->begin(), End = E->end();
       I != End; ++I)
    Record.AddDeclRef(*I);
  Code = serialization::EXPR_FUNCTION_PARM_PACK;
}

void ASTStmtWriter::VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E) {
  VisitExpr(E);
  Record.push_back(static_cast<bool>(E->getLifetimeExtendedTemporaryDecl()));
  if (E->getLifetimeExtendedTemporaryDecl())
    Record.AddDeclRef(E->getLifetimeExtendedTemporaryDecl());
  else
    Record.AddStmt(E->getSubExpr());
  Code = serialization::EXPR_MATERIALIZE_TEMPORARY;
}

void ASTStmtWriter::VisitCXXFoldExpr(CXXFoldExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->LParenLoc);
  Record.AddSourceLocation(E->EllipsisLoc);
  Record.AddSourceLocation(E->RParenLoc);
  Record.push_back(E->NumExpansions);
  Record.AddStmt(E->SubExprs[0]);
  Record.AddStmt(E->SubExprs[1]);
  Record.AddStmt(E->SubExprs[2]);
  Record.push_back(E->Opcode);
  Code = serialization::EXPR_CXX_FOLD;
}

void ASTStmtWriter::VisitCXXParenListInitExpr(CXXParenListInitExpr *E) {
  VisitExpr(E);
  ArrayRef<Expr *> InitExprs = E->getInitExprs();
  Record.push_back(InitExprs.size());
  Record.push_back(E->getUserSpecifiedInitExprs().size());
  Record.AddSourceLocation(E->getInitLoc());
  Record.AddSourceLocation(E->getBeginLoc());
  Record.AddSourceLocation(E->getEndLoc());
  for (Expr *InitExpr : E->getInitExprs())
    Record.AddStmt(InitExpr);
  Expr *ArrayFiller = E->getArrayFiller();
  FieldDecl *UnionField = E->getInitializedFieldInUnion();
  bool HasArrayFillerOrUnionDecl = ArrayFiller || UnionField;
  Record.push_back(HasArrayFillerOrUnionDecl);
  if (HasArrayFillerOrUnionDecl) {
    Record.push_back(static_cast<bool>(ArrayFiller));
    if (ArrayFiller)
      Record.AddStmt(ArrayFiller);
    else
      Record.AddDeclRef(UnionField);
  }
  Code = serialization::EXPR_CXX_PAREN_LIST_INIT;
}

void ASTStmtWriter::VisitOpaqueValueExpr(OpaqueValueExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getSourceExpr());
  Record.AddSourceLocation(E->getLocation());
  Record.push_back(E->isUnique());
  Code = serialization::EXPR_OPAQUE_VALUE;
}

void ASTStmtWriter::VisitTypoExpr(TypoExpr *E) {
  VisitExpr(E);
  // TODO: Figure out sane writer behavior for a TypoExpr, if necessary
  llvm_unreachable("Cannot write TypoExpr nodes");
}

//===----------------------------------------------------------------------===//
// CUDA Expressions and Statements.
//===----------------------------------------------------------------------===//

void ASTStmtWriter::VisitCUDAKernelCallExpr(CUDAKernelCallExpr *E) {
  VisitCallExpr(E);
  Record.AddStmt(E->getConfig());
  Code = serialization::EXPR_CUDA_KERNEL_CALL;
}

//===----------------------------------------------------------------------===//
// OpenCL Expressions and Statements.
//===----------------------------------------------------------------------===//
void ASTStmtWriter::VisitAsTypeExpr(AsTypeExpr *E) {
  VisitExpr(E);
  Record.AddSourceLocation(E->getBuiltinLoc());
  Record.AddSourceLocation(E->getRParenLoc());
  Record.AddStmt(E->getSrcExpr());
  Code = serialization::EXPR_ASTYPE;
}

//===----------------------------------------------------------------------===//
// Microsoft Expressions and Statements.
//===----------------------------------------------------------------------===//
void ASTStmtWriter::VisitMSPropertyRefExpr(MSPropertyRefExpr *E) {
  VisitExpr(E);
  Record.push_back(E->isArrow());
  Record.AddStmt(E->getBaseExpr());
  Record.AddNestedNameSpecifierLoc(E->getQualifierLoc());
  Record.AddSourceLocation(E->getMemberLoc());
  Record.AddDeclRef(E->getPropertyDecl());
  Code = serialization::EXPR_CXX_PROPERTY_REF_EXPR;
}

void ASTStmtWriter::VisitMSPropertySubscriptExpr(MSPropertySubscriptExpr *E) {
  VisitExpr(E);
  Record.AddStmt(E->getBase());
  Record.AddStmt(E->getIdx());
  Record.AddSourceLocation(E->getRBracketLoc());
  Code = serialization::EXPR_CXX_PROPERTY_SUBSCRIPT_EXPR;
}

void ASTStmtWriter::VisitCXXUuidofExpr(CXXUuidofExpr *E) {
  VisitExpr(E);
  Record.AddSourceRange(E->getSourceRange());
  Record.AddDeclRef(E->getGuidDecl());
  if (E->isTypeOperand()) {
    Record.AddTypeSourceInfo(E->getTypeOperandSourceInfo());
    Code = serialization::EXPR_CXX_UUIDOF_TYPE;
  } else {
    Record.AddStmt(E->getExprOperand());
    Code = serialization::EXPR_CXX_UUIDOF_EXPR;
  }
}

void ASTStmtWriter::VisitSEHExceptStmt(SEHExceptStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getExceptLoc());
  Record.AddStmt(S->getFilterExpr());
  Record.AddStmt(S->getBlock());
  Code = serialization::STMT_SEH_EXCEPT;
}

void ASTStmtWriter::VisitSEHFinallyStmt(SEHFinallyStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getFinallyLoc());
  Record.AddStmt(S->getBlock());
  Code = serialization::STMT_SEH_FINALLY;
}

void ASTStmtWriter::VisitSEHTryStmt(SEHTryStmt *S) {
  VisitStmt(S);
  Record.push_back(S->getIsCXXTry());
  Record.AddSourceLocation(S->getTryLoc());
  Record.AddStmt(S->getTryBlock());
  Record.AddStmt(S->getHandler());
  Code = serialization::STMT_SEH_TRY;
}

void ASTStmtWriter::VisitSEHLeaveStmt(SEHLeaveStmt *S) {
  VisitStmt(S);
  Record.AddSourceLocation(S->getLeaveLoc());
  Code = serialization::STMT_SEH_LEAVE;
}

//===----------------------------------------------------------------------===//
// OpenMP Directives.
//===----------------------------------------------------------------------===//

void ASTStmtWriter::VisitOMPCanonicalLoop(OMPCanonicalLoop *S) {
  VisitStmt(S);
  for (Stmt *SubStmt : S->SubStmts)
    Record.AddStmt(SubStmt);
  Code = serialization::STMT_OMP_CANONICAL_LOOP;
}

void ASTStmtWriter::VisitOMPExecutableDirective(OMPExecutableDirective *E) {
  Record.writeOMPChildren(E->Data);
  Record.AddSourceLocation(E->getBeginLoc());
  Record.AddSourceLocation(E->getEndLoc());
  Record.writeEnum(E->getMappedDirective());
}

void ASTStmtWriter::VisitOMPLoopBasedDirective(OMPLoopBasedDirective *D) {
  VisitStmt(D);
  Record.writeUInt32(D->getLoopsNumber());
  VisitOMPExecutableDirective(D);
}

void ASTStmtWriter::VisitOMPLoopDirective(OMPLoopDirective *D) {
  VisitOMPLoopBasedDirective(D);
}

void ASTStmtWriter::VisitOMPMetaDirective(OMPMetaDirective *D) {
  VisitStmt(D);
  Record.push_back(D->getNumClauses());
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_META_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelDirective(OMPParallelDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_PARALLEL_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPSimdDirective(OMPSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPLoopTransformationDirective(
    OMPLoopTransformationDirective *D) {
  VisitOMPLoopBasedDirective(D);
  Record.writeUInt32(D->getNumGeneratedLoops());
}

void ASTStmtWriter::VisitOMPTileDirective(OMPTileDirective *D) {
  VisitOMPLoopTransformationDirective(D);
  Code = serialization::STMT_OMP_TILE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPUnrollDirective(OMPUnrollDirective *D) {
  VisitOMPLoopTransformationDirective(D);
  Code = serialization::STMT_OMP_UNROLL_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPReverseDirective(OMPReverseDirective *D) {
  VisitOMPLoopTransformationDirective(D);
  Code = serialization::STMT_OMP_REVERSE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPInterchangeDirective(OMPInterchangeDirective *D) {
  VisitOMPLoopTransformationDirective(D);
  Code = serialization::STMT_OMP_INTERCHANGE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPForDirective(OMPForDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_FOR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPForSimdDirective(OMPForSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_FOR_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPSectionsDirective(OMPSectionsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_SECTIONS_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPSectionDirective(OMPSectionDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_SECTION_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPScopeDirective(OMPScopeDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_SCOPE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPSingleDirective(OMPSingleDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_SINGLE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPMasterDirective(OMPMasterDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_MASTER_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPCriticalDirective(OMPCriticalDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.AddDeclarationNameInfo(D->getDirectiveName());
  Code = serialization::STMT_OMP_CRITICAL_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelForDirective(OMPParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_PARALLEL_FOR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelForSimdDirective(
    OMPParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_PARALLEL_FOR_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelMasterDirective(
    OMPParallelMasterDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_PARALLEL_MASTER_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelMaskedDirective(
    OMPParallelMaskedDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_PARALLEL_MASKED_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelSectionsDirective(
    OMPParallelSectionsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_PARALLEL_SECTIONS_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTaskDirective(OMPTaskDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_TASK_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPAtomicDirective(OMPAtomicDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->isXLHSInRHSPart());
  Record.writeBool(D->isPostfixUpdate());
  Record.writeBool(D->isFailOnly());
  Code = serialization::STMT_OMP_ATOMIC_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetDirective(OMPTargetDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TARGET_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetDataDirective(OMPTargetDataDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TARGET_DATA_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetEnterDataDirective(
    OMPTargetEnterDataDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TARGET_ENTER_DATA_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetExitDataDirective(
    OMPTargetExitDataDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TARGET_EXIT_DATA_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetParallelDirective(
    OMPTargetParallelDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_TARGET_PARALLEL_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetParallelForDirective(
    OMPTargetParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_TARGET_PARALLEL_FOR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTaskyieldDirective(OMPTaskyieldDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TASKYIELD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPBarrierDirective(OMPBarrierDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_BARRIER_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTaskwaitDirective(OMPTaskwaitDirective *D) {
  VisitStmt(D);
  Record.push_back(D->getNumClauses());
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TASKWAIT_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPErrorDirective(OMPErrorDirective *D) {
  VisitStmt(D);
  Record.push_back(D->getNumClauses());
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_ERROR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTaskgroupDirective(OMPTaskgroupDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TASKGROUP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPFlushDirective(OMPFlushDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_FLUSH_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPDepobjDirective(OMPDepobjDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_DEPOBJ_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPScanDirective(OMPScanDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_SCAN_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPOrderedDirective(OMPOrderedDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_ORDERED_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTeamsDirective(OMPTeamsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TEAMS_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPCancellationPointDirective(
    OMPCancellationPointDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeEnum(D->getCancelRegion());
  Code = serialization::STMT_OMP_CANCELLATION_POINT_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPCancelDirective(OMPCancelDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.writeEnum(D->getCancelRegion());
  Code = serialization::STMT_OMP_CANCEL_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTaskLoopDirective(OMPTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_TASKLOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTaskLoopSimdDirective(OMPTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TASKLOOP_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPMasterTaskLoopDirective(
    OMPMasterTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_MASTER_TASKLOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPMaskedTaskLoopDirective(
    OMPMaskedTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_MASKED_TASKLOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPMasterTaskLoopSimdDirective(
    OMPMasterTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_MASTER_TASKLOOP_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPMaskedTaskLoopSimdDirective(
    OMPMaskedTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_MASKED_TASKLOOP_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelMasterTaskLoopDirective(
    OMPParallelMasterTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_PARALLEL_MASTER_TASKLOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelMaskedTaskLoopDirective(
    OMPParallelMaskedTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_PARALLEL_MASKED_TASKLOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelMasterTaskLoopSimdDirective(
    OMPParallelMasterTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_PARALLEL_MASTER_TASKLOOP_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelMaskedTaskLoopSimdDirective(
    OMPParallelMaskedTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_PARALLEL_MASKED_TASKLOOP_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPDistributeDirective(OMPDistributeDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_DISTRIBUTE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetUpdateDirective(OMPTargetUpdateDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TARGET_UPDATE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPDistributeParallelForDirective(
    OMPDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPDistributeParallelForSimdDirective(
    OMPDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPDistributeSimdDirective(
    OMPDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_DISTRIBUTE_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetParallelForSimdDirective(
    OMPTargetParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TARGET_PARALLEL_FOR_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetSimdDirective(OMPTargetSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TARGET_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTeamsDistributeDirective(
    OMPTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TEAMS_DISTRIBUTE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTeamsDistributeSimdDirective(
    OMPTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTeamsDistributeParallelForSimdDirective(
    OMPTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTeamsDistributeParallelForDirective(
    OMPTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetTeamsDirective(OMPTargetTeamsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_TARGET_TEAMS_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetTeamsDistributeDirective(
    OMPTargetTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TARGET_TEAMS_DISTRIBUTE_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetTeamsDistributeParallelForDirective(
    OMPTargetTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->hasCancel());
  Code = serialization::STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetTeamsDistributeParallelForSimdDirective(
    OMPTargetTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::
      STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetTeamsDistributeSimdDirective(
    OMPTargetTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TARGET_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPInteropDirective(OMPInteropDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_INTEROP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPDispatchDirective(OMPDispatchDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Record.AddSourceLocation(D->getTargetCallLoc());
  Code = serialization::STMT_OMP_DISPATCH_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPMaskedDirective(OMPMaskedDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  Code = serialization::STMT_OMP_MASKED_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPGenericLoopDirective(OMPGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_GENERIC_LOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTeamsGenericLoopDirective(
    OMPTeamsGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TEAMS_GENERIC_LOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetTeamsGenericLoopDirective(
    OMPTargetTeamsGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Record.writeBool(D->canBeParallelFor());
  Code = serialization::STMT_OMP_TARGET_TEAMS_GENERIC_LOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPParallelGenericLoopDirective(
    OMPParallelGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_PARALLEL_GENERIC_LOOP_DIRECTIVE;
}

void ASTStmtWriter::VisitOMPTargetParallelGenericLoopDirective(
    OMPTargetParallelGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
  Code = serialization::STMT_OMP_TARGET_PARALLEL_GENERIC_LOOP_DIRECTIVE;
}

//===----------------------------------------------------------------------===//
// OpenACC Constructs/Directives.
//===----------------------------------------------------------------------===//
void ASTStmtWriter::VisitOpenACCConstructStmt(OpenACCConstructStmt *S) {
  Record.push_back(S->clauses().size());
  Record.writeEnum(S->Kind);
  Record.AddSourceRange(S->Range);
  Record.AddSourceLocation(S->DirectiveLoc);
  Record.writeOpenACCClauseList(S->clauses());
}

void ASTStmtWriter::VisitOpenACCAssociatedStmtConstruct(
    OpenACCAssociatedStmtConstruct *S) {
  VisitOpenACCConstructStmt(S);
  Record.AddStmt(S->getAssociatedStmt());
}

void ASTStmtWriter::VisitOpenACCComputeConstruct(OpenACCComputeConstruct *S) {
  VisitStmt(S);
  VisitOpenACCAssociatedStmtConstruct(S);
  Code = serialization::STMT_OPENACC_COMPUTE_CONSTRUCT;
}

void ASTStmtWriter::VisitOpenACCLoopConstruct(OpenACCLoopConstruct *S) {
  VisitStmt(S);
  VisitOpenACCAssociatedStmtConstruct(S);
  Code = serialization::STMT_OPENACC_LOOP_CONSTRUCT;
}

//===----------------------------------------------------------------------===//
// ASTWriter Implementation
//===----------------------------------------------------------------------===//

unsigned ASTWriter::RecordSwitchCaseID(SwitchCase *S) {
  assert(!SwitchCaseIDs.contains(S) && "SwitchCase recorded twice");
  unsigned NextID = SwitchCaseIDs.size();
  SwitchCaseIDs[S] = NextID;
  return NextID;
}

unsigned ASTWriter::getSwitchCaseID(SwitchCase *S) {
  assert(SwitchCaseIDs.contains(S) && "SwitchCase hasn't been seen yet");
  return SwitchCaseIDs[S];
}

void ASTWriter::ClearSwitchCaseIDs() {
  SwitchCaseIDs.clear();
}

/// Write the given substatement or subexpression to the
/// bitstream.
void ASTWriter::WriteSubStmt(Stmt *S) {
  RecordData Record;
  ASTStmtWriter Writer(*this, Record);
  ++NumStatements;

  if (!S) {
    Stream.EmitRecord(serialization::STMT_NULL_PTR, Record);
    return;
  }

  llvm::DenseMap<Stmt *, uint64_t>::iterator I = SubStmtEntries.find(S);
  if (I != SubStmtEntries.end()) {
    Record.push_back(I->second);
    Stream.EmitRecord(serialization::STMT_REF_PTR, Record);
    return;
  }

#ifndef NDEBUG
  assert(!ParentStmts.count(S) && "There is a Stmt cycle!");

  struct ParentStmtInserterRAII {
    Stmt *S;
    llvm::DenseSet<Stmt *> &ParentStmts;

    ParentStmtInserterRAII(Stmt *S, llvm::DenseSet<Stmt *> &ParentStmts)
      : S(S), ParentStmts(ParentStmts) {
      ParentStmts.insert(S);
    }
    ~ParentStmtInserterRAII() {
      ParentStmts.erase(S);
    }
  };

  ParentStmtInserterRAII ParentStmtInserter(S, ParentStmts);
#endif

  Writer.Visit(S);

  uint64_t Offset = Writer.Emit();
  SubStmtEntries[S] = Offset;
}

/// Flush all of the statements that have been added to the
/// queue via AddStmt().
void ASTRecordWriter::FlushStmts() {
  // We expect to be the only consumer of the two temporary statement maps,
  // assert that they are empty.
  assert(Writer->SubStmtEntries.empty() && "unexpected entries in sub-stmt map");
  assert(Writer->ParentStmts.empty() && "unexpected entries in parent stmt map");

  for (unsigned I = 0, N = StmtsToEmit.size(); I != N; ++I) {
    Writer->WriteSubStmt(StmtsToEmit[I]);

    assert(N == StmtsToEmit.size() && "record modified while being written!");

    // Note that we are at the end of a full expression. Any
    // expression records that follow this one are part of a different
    // expression.
    Writer->Stream.EmitRecord(serialization::STMT_STOP, ArrayRef<uint32_t>());

    Writer->SubStmtEntries.clear();
    Writer->ParentStmts.clear();
  }

  StmtsToEmit.clear();
}

void ASTRecordWriter::FlushSubStmts() {
  // For a nested statement, write out the substatements in reverse order (so
  // that a simple stack machine can be used when loading), and don't emit a
  // STMT_STOP after each one.
  for (unsigned I = 0, N = StmtsToEmit.size(); I != N; ++I) {
    Writer->WriteSubStmt(StmtsToEmit[N - I - 1]);
    assert(N == StmtsToEmit.size() && "record modified while being written!");
  }

  StmtsToEmit.clear();
}
