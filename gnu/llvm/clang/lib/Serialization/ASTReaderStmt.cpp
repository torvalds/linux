//===- ASTReaderStmt.cpp - Stmt/Expr Deserialization ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Statement/expression deserialization.  This implements the
// ASTReader::ReadStmt method.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/AttrIterator.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/DependenceFlags.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/CapturedStmt.h"
#include "clang/Basic/ExpressionTraits.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Lex/Token.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTRecordReader.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

using namespace clang;
using namespace serialization;

namespace clang {

  class ASTStmtReader : public StmtVisitor<ASTStmtReader> {
    ASTRecordReader &Record;
    llvm::BitstreamCursor &DeclsCursor;

    std::optional<BitsUnpacker> CurrentUnpackingBits;

    SourceLocation readSourceLocation() {
      return Record.readSourceLocation();
    }

    SourceRange readSourceRange() {
      return Record.readSourceRange();
    }

    std::string readString() {
      return Record.readString();
    }

    TypeSourceInfo *readTypeSourceInfo() {
      return Record.readTypeSourceInfo();
    }

    Decl *readDecl() {
      return Record.readDecl();
    }

    template<typename T>
    T *readDeclAs() {
      return Record.readDeclAs<T>();
    }

  public:
    ASTStmtReader(ASTRecordReader &Record, llvm::BitstreamCursor &Cursor)
        : Record(Record), DeclsCursor(Cursor) {}

    /// The number of record fields required for the Stmt class
    /// itself.
    static const unsigned NumStmtFields = 0;

    /// The number of record fields required for the Expr class
    /// itself.
    static const unsigned NumExprFields = NumStmtFields + 2;

    /// The number of bits required for the packing bits for the Expr class.
    static const unsigned NumExprBits = 10;

    /// Read and initialize a ExplicitTemplateArgumentList structure.
    void ReadTemplateKWAndArgsInfo(ASTTemplateKWAndArgsInfo &Args,
                                   TemplateArgumentLoc *ArgsLocArray,
                                   unsigned NumTemplateArgs);

    void VisitStmt(Stmt *S);
#define STMT(Type, Base) \
    void Visit##Type(Type *);
#include "clang/AST/StmtNodes.inc"
  };

} // namespace clang

void ASTStmtReader::ReadTemplateKWAndArgsInfo(ASTTemplateKWAndArgsInfo &Args,
                                              TemplateArgumentLoc *ArgsLocArray,
                                              unsigned NumTemplateArgs) {
  SourceLocation TemplateKWLoc = readSourceLocation();
  TemplateArgumentListInfo ArgInfo;
  ArgInfo.setLAngleLoc(readSourceLocation());
  ArgInfo.setRAngleLoc(readSourceLocation());
  for (unsigned i = 0; i != NumTemplateArgs; ++i)
    ArgInfo.addArgument(Record.readTemplateArgumentLoc());
  Args.initializeFrom(TemplateKWLoc, ArgInfo, ArgsLocArray);
}

void ASTStmtReader::VisitStmt(Stmt *S) {
  assert(Record.getIdx() == NumStmtFields && "Incorrect statement field count");
}

void ASTStmtReader::VisitNullStmt(NullStmt *S) {
  VisitStmt(S);
  S->setSemiLoc(readSourceLocation());
  S->NullStmtBits.HasLeadingEmptyMacro = Record.readInt();
}

void ASTStmtReader::VisitCompoundStmt(CompoundStmt *S) {
  VisitStmt(S);
  SmallVector<Stmt *, 16> Stmts;
  unsigned NumStmts = Record.readInt();
  unsigned HasFPFeatures = Record.readInt();
  assert(S->hasStoredFPFeatures() == HasFPFeatures);
  while (NumStmts--)
    Stmts.push_back(Record.readSubStmt());
  S->setStmts(Stmts);
  if (HasFPFeatures)
    S->setStoredFPFeatures(
        FPOptionsOverride::getFromOpaqueInt(Record.readInt()));
  S->LBraceLoc = readSourceLocation();
  S->RBraceLoc = readSourceLocation();
}

void ASTStmtReader::VisitSwitchCase(SwitchCase *S) {
  VisitStmt(S);
  Record.recordSwitchCaseID(S, Record.readInt());
  S->setKeywordLoc(readSourceLocation());
  S->setColonLoc(readSourceLocation());
}

void ASTStmtReader::VisitCaseStmt(CaseStmt *S) {
  VisitSwitchCase(S);
  bool CaseStmtIsGNURange = Record.readInt();
  S->setLHS(Record.readSubExpr());
  S->setSubStmt(Record.readSubStmt());
  if (CaseStmtIsGNURange) {
    S->setRHS(Record.readSubExpr());
    S->setEllipsisLoc(readSourceLocation());
  }
}

void ASTStmtReader::VisitDefaultStmt(DefaultStmt *S) {
  VisitSwitchCase(S);
  S->setSubStmt(Record.readSubStmt());
}

void ASTStmtReader::VisitLabelStmt(LabelStmt *S) {
  VisitStmt(S);
  bool IsSideEntry = Record.readInt();
  auto *LD = readDeclAs<LabelDecl>();
  LD->setStmt(S);
  S->setDecl(LD);
  S->setSubStmt(Record.readSubStmt());
  S->setIdentLoc(readSourceLocation());
  S->setSideEntry(IsSideEntry);
}

void ASTStmtReader::VisitAttributedStmt(AttributedStmt *S) {
  VisitStmt(S);
  // NumAttrs in AttributedStmt is set when creating an empty
  // AttributedStmt in AttributedStmt::CreateEmpty, since it is needed
  // to allocate the right amount of space for the trailing Attr *.
  uint64_t NumAttrs = Record.readInt();
  AttrVec Attrs;
  Record.readAttributes(Attrs);
  (void)NumAttrs;
  assert(NumAttrs == S->AttributedStmtBits.NumAttrs);
  assert(NumAttrs == Attrs.size());
  std::copy(Attrs.begin(), Attrs.end(), S->getAttrArrayPtr());
  S->SubStmt = Record.readSubStmt();
  S->AttributedStmtBits.AttrLoc = readSourceLocation();
}

void ASTStmtReader::VisitIfStmt(IfStmt *S) {
  VisitStmt(S);

  CurrentUnpackingBits.emplace(Record.readInt());

  bool HasElse = CurrentUnpackingBits->getNextBit();
  bool HasVar = CurrentUnpackingBits->getNextBit();
  bool HasInit = CurrentUnpackingBits->getNextBit();

  S->setStatementKind(static_cast<IfStatementKind>(Record.readInt()));
  S->setCond(Record.readSubExpr());
  S->setThen(Record.readSubStmt());
  if (HasElse)
    S->setElse(Record.readSubStmt());
  if (HasVar)
    S->setConditionVariableDeclStmt(cast<DeclStmt>(Record.readSubStmt()));
  if (HasInit)
    S->setInit(Record.readSubStmt());

  S->setIfLoc(readSourceLocation());
  S->setLParenLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());
  if (HasElse)
    S->setElseLoc(readSourceLocation());
}

void ASTStmtReader::VisitSwitchStmt(SwitchStmt *S) {
  VisitStmt(S);

  bool HasInit = Record.readInt();
  bool HasVar = Record.readInt();
  bool AllEnumCasesCovered = Record.readInt();
  if (AllEnumCasesCovered)
    S->setAllEnumCasesCovered();

  S->setCond(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  if (HasInit)
    S->setInit(Record.readSubStmt());
  if (HasVar)
    S->setConditionVariableDeclStmt(cast<DeclStmt>(Record.readSubStmt()));

  S->setSwitchLoc(readSourceLocation());
  S->setLParenLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());

  SwitchCase *PrevSC = nullptr;
  for (auto E = Record.size(); Record.getIdx() != E; ) {
    SwitchCase *SC = Record.getSwitchCaseWithID(Record.readInt());
    if (PrevSC)
      PrevSC->setNextSwitchCase(SC);
    else
      S->setSwitchCaseList(SC);

    PrevSC = SC;
  }
}

void ASTStmtReader::VisitWhileStmt(WhileStmt *S) {
  VisitStmt(S);

  bool HasVar = Record.readInt();

  S->setCond(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  if (HasVar)
    S->setConditionVariableDeclStmt(cast<DeclStmt>(Record.readSubStmt()));

  S->setWhileLoc(readSourceLocation());
  S->setLParenLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitDoStmt(DoStmt *S) {
  VisitStmt(S);
  S->setCond(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  S->setDoLoc(readSourceLocation());
  S->setWhileLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitForStmt(ForStmt *S) {
  VisitStmt(S);
  S->setInit(Record.readSubStmt());
  S->setCond(Record.readSubExpr());
  S->setConditionVariableDeclStmt(cast_or_null<DeclStmt>(Record.readSubStmt()));
  S->setInc(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  S->setForLoc(readSourceLocation());
  S->setLParenLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitGotoStmt(GotoStmt *S) {
  VisitStmt(S);
  S->setLabel(readDeclAs<LabelDecl>());
  S->setGotoLoc(readSourceLocation());
  S->setLabelLoc(readSourceLocation());
}

void ASTStmtReader::VisitIndirectGotoStmt(IndirectGotoStmt *S) {
  VisitStmt(S);
  S->setGotoLoc(readSourceLocation());
  S->setStarLoc(readSourceLocation());
  S->setTarget(Record.readSubExpr());
}

void ASTStmtReader::VisitContinueStmt(ContinueStmt *S) {
  VisitStmt(S);
  S->setContinueLoc(readSourceLocation());
}

void ASTStmtReader::VisitBreakStmt(BreakStmt *S) {
  VisitStmt(S);
  S->setBreakLoc(readSourceLocation());
}

void ASTStmtReader::VisitReturnStmt(ReturnStmt *S) {
  VisitStmt(S);

  bool HasNRVOCandidate = Record.readInt();

  S->setRetValue(Record.readSubExpr());
  if (HasNRVOCandidate)
    S->setNRVOCandidate(readDeclAs<VarDecl>());

  S->setReturnLoc(readSourceLocation());
}

void ASTStmtReader::VisitDeclStmt(DeclStmt *S) {
  VisitStmt(S);
  S->setStartLoc(readSourceLocation());
  S->setEndLoc(readSourceLocation());

  if (Record.size() - Record.getIdx() == 1) {
    // Single declaration
    S->setDeclGroup(DeclGroupRef(readDecl()));
  } else {
    SmallVector<Decl *, 16> Decls;
    int N = Record.size() - Record.getIdx();
    Decls.reserve(N);
    for (int I = 0; I < N; ++I)
      Decls.push_back(readDecl());
    S->setDeclGroup(DeclGroupRef(DeclGroup::Create(Record.getContext(),
                                                   Decls.data(),
                                                   Decls.size())));
  }
}

void ASTStmtReader::VisitAsmStmt(AsmStmt *S) {
  VisitStmt(S);
  S->NumOutputs = Record.readInt();
  S->NumInputs = Record.readInt();
  S->NumClobbers = Record.readInt();
  S->setAsmLoc(readSourceLocation());
  S->setVolatile(Record.readInt());
  S->setSimple(Record.readInt());
}

void ASTStmtReader::VisitGCCAsmStmt(GCCAsmStmt *S) {
  VisitAsmStmt(S);
  S->NumLabels = Record.readInt();
  S->setRParenLoc(readSourceLocation());
  S->setAsmString(cast_or_null<StringLiteral>(Record.readSubStmt()));

  unsigned NumOutputs = S->getNumOutputs();
  unsigned NumInputs = S->getNumInputs();
  unsigned NumClobbers = S->getNumClobbers();
  unsigned NumLabels = S->getNumLabels();

  // Outputs and inputs
  SmallVector<IdentifierInfo *, 16> Names;
  SmallVector<StringLiteral*, 16> Constraints;
  SmallVector<Stmt*, 16> Exprs;
  for (unsigned I = 0, N = NumOutputs + NumInputs; I != N; ++I) {
    Names.push_back(Record.readIdentifier());
    Constraints.push_back(cast_or_null<StringLiteral>(Record.readSubStmt()));
    Exprs.push_back(Record.readSubStmt());
  }

  // Constraints
  SmallVector<StringLiteral*, 16> Clobbers;
  for (unsigned I = 0; I != NumClobbers; ++I)
    Clobbers.push_back(cast_or_null<StringLiteral>(Record.readSubStmt()));

  // Labels
  for (unsigned I = 0, N = NumLabels; I != N; ++I) {
    Names.push_back(Record.readIdentifier());
    Exprs.push_back(Record.readSubStmt());
  }

  S->setOutputsAndInputsAndClobbers(Record.getContext(),
                                    Names.data(), Constraints.data(),
                                    Exprs.data(), NumOutputs, NumInputs,
                                    NumLabels,
                                    Clobbers.data(), NumClobbers);
}

void ASTStmtReader::VisitMSAsmStmt(MSAsmStmt *S) {
  VisitAsmStmt(S);
  S->LBraceLoc = readSourceLocation();
  S->EndLoc = readSourceLocation();
  S->NumAsmToks = Record.readInt();
  std::string AsmStr = readString();

  // Read the tokens.
  SmallVector<Token, 16> AsmToks;
  AsmToks.reserve(S->NumAsmToks);
  for (unsigned i = 0, e = S->NumAsmToks; i != e; ++i) {
    AsmToks.push_back(Record.readToken());
  }

  // The calls to reserve() for the FooData vectors are mandatory to
  // prevent dead StringRefs in the Foo vectors.

  // Read the clobbers.
  SmallVector<std::string, 16> ClobbersData;
  SmallVector<StringRef, 16> Clobbers;
  ClobbersData.reserve(S->NumClobbers);
  Clobbers.reserve(S->NumClobbers);
  for (unsigned i = 0, e = S->NumClobbers; i != e; ++i) {
    ClobbersData.push_back(readString());
    Clobbers.push_back(ClobbersData.back());
  }

  // Read the operands.
  unsigned NumOperands = S->NumOutputs + S->NumInputs;
  SmallVector<Expr*, 16> Exprs;
  SmallVector<std::string, 16> ConstraintsData;
  SmallVector<StringRef, 16> Constraints;
  Exprs.reserve(NumOperands);
  ConstraintsData.reserve(NumOperands);
  Constraints.reserve(NumOperands);
  for (unsigned i = 0; i != NumOperands; ++i) {
    Exprs.push_back(cast<Expr>(Record.readSubStmt()));
    ConstraintsData.push_back(readString());
    Constraints.push_back(ConstraintsData.back());
  }

  S->initialize(Record.getContext(), AsmStr, AsmToks,
                Constraints, Exprs, Clobbers);
}

void ASTStmtReader::VisitCoroutineBodyStmt(CoroutineBodyStmt *S) {
  VisitStmt(S);
  assert(Record.peekInt() == S->NumParams);
  Record.skipInts(1);
  auto *StoredStmts = S->getStoredStmts();
  for (unsigned i = 0;
       i < CoroutineBodyStmt::SubStmt::FirstParamMove + S->NumParams; ++i)
    StoredStmts[i] = Record.readSubStmt();
}

void ASTStmtReader::VisitCoreturnStmt(CoreturnStmt *S) {
  VisitStmt(S);
  S->CoreturnLoc = Record.readSourceLocation();
  for (auto &SubStmt: S->SubStmts)
    SubStmt = Record.readSubStmt();
  S->IsImplicit = Record.readInt() != 0;
}

void ASTStmtReader::VisitCoawaitExpr(CoawaitExpr *E) {
  VisitExpr(E);
  E->KeywordLoc = readSourceLocation();
  for (auto &SubExpr: E->SubExprs)
    SubExpr = Record.readSubStmt();
  E->OpaqueValue = cast_or_null<OpaqueValueExpr>(Record.readSubStmt());
  E->setIsImplicit(Record.readInt() != 0);
}

void ASTStmtReader::VisitCoyieldExpr(CoyieldExpr *E) {
  VisitExpr(E);
  E->KeywordLoc = readSourceLocation();
  for (auto &SubExpr: E->SubExprs)
    SubExpr = Record.readSubStmt();
  E->OpaqueValue = cast_or_null<OpaqueValueExpr>(Record.readSubStmt());
}

void ASTStmtReader::VisitDependentCoawaitExpr(DependentCoawaitExpr *E) {
  VisitExpr(E);
  E->KeywordLoc = readSourceLocation();
  for (auto &SubExpr: E->SubExprs)
    SubExpr = Record.readSubStmt();
}

void ASTStmtReader::VisitCapturedStmt(CapturedStmt *S) {
  VisitStmt(S);
  Record.skipInts(1);
  S->setCapturedDecl(readDeclAs<CapturedDecl>());
  S->setCapturedRegionKind(static_cast<CapturedRegionKind>(Record.readInt()));
  S->setCapturedRecordDecl(readDeclAs<RecordDecl>());

  // Capture inits
  for (CapturedStmt::capture_init_iterator I = S->capture_init_begin(),
                                           E = S->capture_init_end();
       I != E; ++I)
    *I = Record.readSubExpr();

  // Body
  S->setCapturedStmt(Record.readSubStmt());
  S->getCapturedDecl()->setBody(S->getCapturedStmt());

  // Captures
  for (auto &I : S->captures()) {
    I.VarAndKind.setPointer(readDeclAs<VarDecl>());
    I.VarAndKind.setInt(
        static_cast<CapturedStmt::VariableCaptureKind>(Record.readInt()));
    I.Loc = readSourceLocation();
  }
}

void ASTStmtReader::VisitExpr(Expr *E) {
  VisitStmt(E);
  CurrentUnpackingBits.emplace(Record.readInt());
  E->setDependence(static_cast<ExprDependence>(
      CurrentUnpackingBits->getNextBits(/*Width=*/5)));
  E->setValueKind(static_cast<ExprValueKind>(
      CurrentUnpackingBits->getNextBits(/*Width=*/2)));
  E->setObjectKind(static_cast<ExprObjectKind>(
      CurrentUnpackingBits->getNextBits(/*Width=*/3)));

  E->setType(Record.readType());
  assert(Record.getIdx() == NumExprFields &&
         "Incorrect expression field count");
}

void ASTStmtReader::VisitConstantExpr(ConstantExpr *E) {
  VisitExpr(E);

  auto StorageKind = static_cast<ConstantResultStorageKind>(Record.readInt());
  assert(E->getResultStorageKind() == StorageKind && "Wrong ResultKind!");

  E->ConstantExprBits.APValueKind = Record.readInt();
  E->ConstantExprBits.IsUnsigned = Record.readInt();
  E->ConstantExprBits.BitWidth = Record.readInt();
  E->ConstantExprBits.HasCleanup = false; // Not serialized, see below.
  E->ConstantExprBits.IsImmediateInvocation = Record.readInt();

  switch (StorageKind) {
  case ConstantResultStorageKind::None:
    break;

  case ConstantResultStorageKind::Int64:
    E->Int64Result() = Record.readInt();
    break;

  case ConstantResultStorageKind::APValue:
    E->APValueResult() = Record.readAPValue();
    if (E->APValueResult().needsCleanup()) {
      E->ConstantExprBits.HasCleanup = true;
      Record.getContext().addDestruction(&E->APValueResult());
    }
    break;
  }

  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitSYCLUniqueStableNameExpr(SYCLUniqueStableNameExpr *E) {
  VisitExpr(E);

  E->setLocation(readSourceLocation());
  E->setLParenLocation(readSourceLocation());
  E->setRParenLocation(readSourceLocation());

  E->setTypeSourceInfo(Record.readTypeSourceInfo());
}

void ASTStmtReader::VisitPredefinedExpr(PredefinedExpr *E) {
  VisitExpr(E);
  bool HasFunctionName = Record.readInt();
  E->PredefinedExprBits.HasFunctionName = HasFunctionName;
  E->PredefinedExprBits.Kind = Record.readInt();
  E->PredefinedExprBits.IsTransparent = Record.readInt();
  E->setLocation(readSourceLocation());
  if (HasFunctionName)
    E->setFunctionName(cast<StringLiteral>(Record.readSubExpr()));
}

void ASTStmtReader::VisitDeclRefExpr(DeclRefExpr *E) {
  VisitExpr(E);

  CurrentUnpackingBits.emplace(Record.readInt());
  E->DeclRefExprBits.HadMultipleCandidates = CurrentUnpackingBits->getNextBit();
  E->DeclRefExprBits.RefersToEnclosingVariableOrCapture =
      CurrentUnpackingBits->getNextBit();
  E->DeclRefExprBits.NonOdrUseReason =
      CurrentUnpackingBits->getNextBits(/*Width=*/2);
  E->DeclRefExprBits.IsImmediateEscalating = CurrentUnpackingBits->getNextBit();
  E->DeclRefExprBits.HasFoundDecl = CurrentUnpackingBits->getNextBit();
  E->DeclRefExprBits.HasQualifier = CurrentUnpackingBits->getNextBit();
  E->DeclRefExprBits.HasTemplateKWAndArgsInfo =
      CurrentUnpackingBits->getNextBit();
  E->DeclRefExprBits.CapturedByCopyInLambdaWithExplicitObjectParameter = false;
  unsigned NumTemplateArgs = 0;
  if (E->hasTemplateKWAndArgsInfo())
    NumTemplateArgs = Record.readInt();

  if (E->hasQualifier())
    new (E->getTrailingObjects<NestedNameSpecifierLoc>())
        NestedNameSpecifierLoc(Record.readNestedNameSpecifierLoc());

  if (E->hasFoundDecl())
    *E->getTrailingObjects<NamedDecl *>() = readDeclAs<NamedDecl>();

  if (E->hasTemplateKWAndArgsInfo())
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(), NumTemplateArgs);

  E->D = readDeclAs<ValueDecl>();
  E->setLocation(readSourceLocation());
  E->DNLoc = Record.readDeclarationNameLoc(E->getDecl()->getDeclName());
}

void ASTStmtReader::VisitIntegerLiteral(IntegerLiteral *E) {
  VisitExpr(E);
  E->setLocation(readSourceLocation());
  E->setValue(Record.getContext(), Record.readAPInt());
}

void ASTStmtReader::VisitFixedPointLiteral(FixedPointLiteral *E) {
  VisitExpr(E);
  E->setLocation(readSourceLocation());
  E->setScale(Record.readInt());
  E->setValue(Record.getContext(), Record.readAPInt());
}

void ASTStmtReader::VisitFloatingLiteral(FloatingLiteral *E) {
  VisitExpr(E);
  E->setRawSemantics(
      static_cast<llvm::APFloatBase::Semantics>(Record.readInt()));
  E->setExact(Record.readInt());
  E->setValue(Record.getContext(), Record.readAPFloat(E->getSemantics()));
  E->setLocation(readSourceLocation());
}

void ASTStmtReader::VisitImaginaryLiteral(ImaginaryLiteral *E) {
  VisitExpr(E);
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitStringLiteral(StringLiteral *E) {
  VisitExpr(E);

  // NumConcatenated, Length and CharByteWidth are set by the empty
  // ctor since they are needed to allocate storage for the trailing objects.
  unsigned NumConcatenated = Record.readInt();
  unsigned Length = Record.readInt();
  unsigned CharByteWidth = Record.readInt();
  assert((NumConcatenated == E->getNumConcatenated()) &&
         "Wrong number of concatenated tokens!");
  assert((Length == E->getLength()) && "Wrong Length!");
  assert((CharByteWidth == E->getCharByteWidth()) && "Wrong character width!");
  E->StringLiteralBits.Kind = Record.readInt();
  E->StringLiteralBits.IsPascal = Record.readInt();

  // The character width is originally computed via mapCharByteWidth.
  // Check that the deserialized character width is consistant with the result
  // of calling mapCharByteWidth.
  assert((CharByteWidth ==
          StringLiteral::mapCharByteWidth(Record.getContext().getTargetInfo(),
                                          E->getKind())) &&
         "Wrong character width!");

  // Deserialize the trailing array of SourceLocation.
  for (unsigned I = 0; I < NumConcatenated; ++I)
    E->setStrTokenLoc(I, readSourceLocation());

  // Deserialize the trailing array of char holding the string data.
  char *StrData = E->getStrDataAsChar();
  for (unsigned I = 0; I < Length * CharByteWidth; ++I)
    StrData[I] = Record.readInt();
}

void ASTStmtReader::VisitCharacterLiteral(CharacterLiteral *E) {
  VisitExpr(E);
  E->setValue(Record.readInt());
  E->setLocation(readSourceLocation());
  E->setKind(static_cast<CharacterLiteralKind>(Record.readInt()));
}

void ASTStmtReader::VisitParenExpr(ParenExpr *E) {
  VisitExpr(E);
  E->setLParen(readSourceLocation());
  E->setRParen(readSourceLocation());
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitParenListExpr(ParenListExpr *E) {
  VisitExpr(E);
  unsigned NumExprs = Record.readInt();
  assert((NumExprs == E->getNumExprs()) && "Wrong NumExprs!");
  for (unsigned I = 0; I != NumExprs; ++I)
    E->getTrailingObjects<Stmt *>()[I] = Record.readSubStmt();
  E->LParenLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
}

void ASTStmtReader::VisitUnaryOperator(UnaryOperator *E) {
  VisitExpr(E);
  bool hasFP_Features = CurrentUnpackingBits->getNextBit();
  assert(hasFP_Features == E->hasStoredFPFeatures());
  E->setSubExpr(Record.readSubExpr());
  E->setOpcode(
      (UnaryOperator::Opcode)CurrentUnpackingBits->getNextBits(/*Width=*/5));
  E->setOperatorLoc(readSourceLocation());
  E->setCanOverflow(CurrentUnpackingBits->getNextBit());
  if (hasFP_Features)
    E->setStoredFPFeatures(
        FPOptionsOverride::getFromOpaqueInt(Record.readInt()));
}

void ASTStmtReader::VisitOffsetOfExpr(OffsetOfExpr *E) {
  VisitExpr(E);
  assert(E->getNumComponents() == Record.peekInt());
  Record.skipInts(1);
  assert(E->getNumExpressions() == Record.peekInt());
  Record.skipInts(1);
  E->setOperatorLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
  E->setTypeSourceInfo(readTypeSourceInfo());
  for (unsigned I = 0, N = E->getNumComponents(); I != N; ++I) {
    auto Kind = static_cast<OffsetOfNode::Kind>(Record.readInt());
    SourceLocation Start = readSourceLocation();
    SourceLocation End = readSourceLocation();
    switch (Kind) {
    case OffsetOfNode::Array:
      E->setComponent(I, OffsetOfNode(Start, Record.readInt(), End));
      break;

    case OffsetOfNode::Field:
      E->setComponent(
          I, OffsetOfNode(Start, readDeclAs<FieldDecl>(), End));
      break;

    case OffsetOfNode::Identifier:
      E->setComponent(
          I,
          OffsetOfNode(Start, Record.readIdentifier(), End));
      break;

    case OffsetOfNode::Base: {
      auto *Base = new (Record.getContext()) CXXBaseSpecifier();
      *Base = Record.readCXXBaseSpecifier();
      E->setComponent(I, OffsetOfNode(Base));
      break;
    }
    }
  }

  for (unsigned I = 0, N = E->getNumExpressions(); I != N; ++I)
    E->setIndexExpr(I, Record.readSubExpr());
}

void ASTStmtReader::VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E) {
  VisitExpr(E);
  E->setKind(static_cast<UnaryExprOrTypeTrait>(Record.readInt()));
  if (Record.peekInt() == 0) {
    E->setArgument(Record.readSubExpr());
    Record.skipInts(1);
  } else {
    E->setArgument(readTypeSourceInfo());
  }
  E->setOperatorLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

static ConstraintSatisfaction
readConstraintSatisfaction(ASTRecordReader &Record) {
  ConstraintSatisfaction Satisfaction;
  Satisfaction.IsSatisfied = Record.readInt();
  Satisfaction.ContainsErrors = Record.readInt();
  const ASTContext &C = Record.getContext();
  if (!Satisfaction.IsSatisfied) {
    unsigned NumDetailRecords = Record.readInt();
    for (unsigned i = 0; i != NumDetailRecords; ++i) {
      if (/* IsDiagnostic */Record.readInt()) {
        SourceLocation DiagLocation = Record.readSourceLocation();
        StringRef DiagMessage = C.backupStr(Record.readString());

        Satisfaction.Details.emplace_back(
            new (C) ConstraintSatisfaction::SubstitutionDiagnostic(
                DiagLocation, DiagMessage));
      } else
        Satisfaction.Details.emplace_back(Record.readExpr());
    }
  }
  return Satisfaction;
}

void ASTStmtReader::VisitConceptSpecializationExpr(
        ConceptSpecializationExpr *E) {
  VisitExpr(E);
  E->SpecDecl = Record.readDeclAs<ImplicitConceptSpecializationDecl>();
  if (Record.readBool())
    E->ConceptRef = Record.readConceptReference();
  E->Satisfaction = E->isValueDependent() ? nullptr :
      ASTConstraintSatisfaction::Create(Record.getContext(),
                                        readConstraintSatisfaction(Record));
}

static concepts::Requirement::SubstitutionDiagnostic *
readSubstitutionDiagnostic(ASTRecordReader &Record) {
  const ASTContext &C = Record.getContext();
  StringRef SubstitutedEntity = C.backupStr(Record.readString());
  SourceLocation DiagLoc = Record.readSourceLocation();
  StringRef DiagMessage = C.backupStr(Record.readString());

  return new (Record.getContext())
      concepts::Requirement::SubstitutionDiagnostic{SubstitutedEntity, DiagLoc,
                                                    DiagMessage};
}

void ASTStmtReader::VisitRequiresExpr(RequiresExpr *E) {
  VisitExpr(E);
  unsigned NumLocalParameters = Record.readInt();
  unsigned NumRequirements = Record.readInt();
  E->RequiresExprBits.RequiresKWLoc = Record.readSourceLocation();
  E->RequiresExprBits.IsSatisfied = Record.readInt();
  E->Body = Record.readDeclAs<RequiresExprBodyDecl>();
  llvm::SmallVector<ParmVarDecl *, 4> LocalParameters;
  for (unsigned i = 0; i < NumLocalParameters; ++i)
    LocalParameters.push_back(cast<ParmVarDecl>(Record.readDecl()));
  std::copy(LocalParameters.begin(), LocalParameters.end(),
            E->getTrailingObjects<ParmVarDecl *>());
  llvm::SmallVector<concepts::Requirement *, 4> Requirements;
  for (unsigned i = 0; i < NumRequirements; ++i) {
    auto RK =
        static_cast<concepts::Requirement::RequirementKind>(Record.readInt());
    concepts::Requirement *R = nullptr;
    switch (RK) {
      case concepts::Requirement::RK_Type: {
        auto Status =
            static_cast<concepts::TypeRequirement::SatisfactionStatus>(
                Record.readInt());
        if (Status == concepts::TypeRequirement::SS_SubstitutionFailure)
          R = new (Record.getContext())
              concepts::TypeRequirement(readSubstitutionDiagnostic(Record));
        else
          R = new (Record.getContext())
              concepts::TypeRequirement(Record.readTypeSourceInfo());
      } break;
      case concepts::Requirement::RK_Simple:
      case concepts::Requirement::RK_Compound: {
        auto Status =
            static_cast<concepts::ExprRequirement::SatisfactionStatus>(
                Record.readInt());
        llvm::PointerUnion<concepts::Requirement::SubstitutionDiagnostic *,
                           Expr *> E;
        if (Status == concepts::ExprRequirement::SS_ExprSubstitutionFailure) {
          E = readSubstitutionDiagnostic(Record);
        } else
          E = Record.readExpr();

        std::optional<concepts::ExprRequirement::ReturnTypeRequirement> Req;
        ConceptSpecializationExpr *SubstitutedConstraintExpr = nullptr;
        SourceLocation NoexceptLoc;
        if (RK == concepts::Requirement::RK_Simple) {
          Req.emplace();
        } else {
          NoexceptLoc = Record.readSourceLocation();
          switch (/* returnTypeRequirementKind */Record.readInt()) {
            case 0:
              // No return type requirement.
              Req.emplace();
              break;
            case 1: {
              // type-constraint
              TemplateParameterList *TPL = Record.readTemplateParameterList();
              if (Status >=
                  concepts::ExprRequirement::SS_ConstraintsNotSatisfied)
                SubstitutedConstraintExpr =
                    cast<ConceptSpecializationExpr>(Record.readExpr());
              Req.emplace(TPL);
            } break;
            case 2:
              // Substitution failure
              Req.emplace(readSubstitutionDiagnostic(Record));
              break;
          }
        }
        if (Expr *Ex = E.dyn_cast<Expr *>())
          R = new (Record.getContext()) concepts::ExprRequirement(
                  Ex, RK == concepts::Requirement::RK_Simple, NoexceptLoc,
                  std::move(*Req), Status, SubstitutedConstraintExpr);
        else
          R = new (Record.getContext()) concepts::ExprRequirement(
                  E.get<concepts::Requirement::SubstitutionDiagnostic *>(),
                  RK == concepts::Requirement::RK_Simple, NoexceptLoc,
                  std::move(*Req));
      } break;
      case concepts::Requirement::RK_Nested: {
        ASTContext &C = Record.getContext();
        bool HasInvalidConstraint = Record.readInt();
        if (HasInvalidConstraint) {
          StringRef InvalidConstraint = C.backupStr(Record.readString());
          R = new (C) concepts::NestedRequirement(
              Record.getContext(), InvalidConstraint,
              readConstraintSatisfaction(Record));
          break;
        }
        Expr *E = Record.readExpr();
        if (E->isInstantiationDependent())
          R = new (C) concepts::NestedRequirement(E);
        else
          R = new (C) concepts::NestedRequirement(
              C, E, readConstraintSatisfaction(Record));
      } break;
    }
    if (!R)
      continue;
    Requirements.push_back(R);
  }
  std::copy(Requirements.begin(), Requirements.end(),
            E->getTrailingObjects<concepts::Requirement *>());
  E->LParenLoc = Record.readSourceLocation();
  E->RParenLoc = Record.readSourceLocation();
  E->RBraceLoc = Record.readSourceLocation();
}

void ASTStmtReader::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  VisitExpr(E);
  E->setLHS(Record.readSubExpr());
  E->setRHS(Record.readSubExpr());
  E->setRBracketLoc(readSourceLocation());
}

void ASTStmtReader::VisitMatrixSubscriptExpr(MatrixSubscriptExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setRowIdx(Record.readSubExpr());
  E->setColumnIdx(Record.readSubExpr());
  E->setRBracketLoc(readSourceLocation());
}

void ASTStmtReader::VisitArraySectionExpr(ArraySectionExpr *E) {
  VisitExpr(E);
  E->ASType = Record.readEnum<ArraySectionExpr::ArraySectionType>();

  E->setBase(Record.readSubExpr());
  E->setLowerBound(Record.readSubExpr());
  E->setLength(Record.readSubExpr());

  if (E->isOMPArraySection())
    E->setStride(Record.readSubExpr());

  E->setColonLocFirst(readSourceLocation());

  if (E->isOMPArraySection())
    E->setColonLocSecond(readSourceLocation());

  E->setRBracketLoc(readSourceLocation());
}

void ASTStmtReader::VisitOMPArrayShapingExpr(OMPArrayShapingExpr *E) {
  VisitExpr(E);
  unsigned NumDims = Record.readInt();
  E->setBase(Record.readSubExpr());
  SmallVector<Expr *, 4> Dims(NumDims);
  for (unsigned I = 0; I < NumDims; ++I)
    Dims[I] = Record.readSubExpr();
  E->setDimensions(Dims);
  SmallVector<SourceRange, 4> SRs(NumDims);
  for (unsigned I = 0; I < NumDims; ++I)
    SRs[I] = readSourceRange();
  E->setBracketsRanges(SRs);
  E->setLParenLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitOMPIteratorExpr(OMPIteratorExpr *E) {
  VisitExpr(E);
  unsigned NumIters = Record.readInt();
  E->setIteratorKwLoc(readSourceLocation());
  E->setLParenLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
  for (unsigned I = 0; I < NumIters; ++I) {
    E->setIteratorDeclaration(I, Record.readDeclRef());
    E->setAssignmentLoc(I, readSourceLocation());
    Expr *Begin = Record.readSubExpr();
    Expr *End = Record.readSubExpr();
    Expr *Step = Record.readSubExpr();
    SourceLocation ColonLoc = readSourceLocation();
    SourceLocation SecColonLoc;
    if (Step)
      SecColonLoc = readSourceLocation();
    E->setIteratorRange(I, Begin, ColonLoc, End, SecColonLoc, Step);
    // Deserialize helpers
    OMPIteratorHelperData HD;
    HD.CounterVD = cast_or_null<VarDecl>(Record.readDeclRef());
    HD.Upper = Record.readSubExpr();
    HD.Update = Record.readSubExpr();
    HD.CounterUpdate = Record.readSubExpr();
    E->setHelper(I, HD);
  }
}

void ASTStmtReader::VisitCallExpr(CallExpr *E) {
  VisitExpr(E);

  unsigned NumArgs = Record.readInt();
  CurrentUnpackingBits.emplace(Record.readInt());
  E->setADLCallKind(
      static_cast<CallExpr::ADLCallKind>(CurrentUnpackingBits->getNextBit()));
  bool HasFPFeatures = CurrentUnpackingBits->getNextBit();
  assert((NumArgs == E->getNumArgs()) && "Wrong NumArgs!");
  E->setRParenLoc(readSourceLocation());
  E->setCallee(Record.readSubExpr());
  for (unsigned I = 0; I != NumArgs; ++I)
    E->setArg(I, Record.readSubExpr());

  if (HasFPFeatures)
    E->setStoredFPFeatures(
        FPOptionsOverride::getFromOpaqueInt(Record.readInt()));
}

void ASTStmtReader::VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
  VisitCallExpr(E);
}

void ASTStmtReader::VisitMemberExpr(MemberExpr *E) {
  VisitExpr(E);

  CurrentUnpackingBits.emplace(Record.readInt());
  bool HasQualifier = CurrentUnpackingBits->getNextBit();
  bool HasFoundDecl = CurrentUnpackingBits->getNextBit();
  bool HasTemplateInfo = CurrentUnpackingBits->getNextBit();
  unsigned NumTemplateArgs = Record.readInt();

  E->Base = Record.readSubExpr();
  E->MemberDecl = Record.readDeclAs<ValueDecl>();
  E->MemberDNLoc = Record.readDeclarationNameLoc(E->MemberDecl->getDeclName());
  E->MemberLoc = Record.readSourceLocation();
  E->MemberExprBits.IsArrow = CurrentUnpackingBits->getNextBit();
  E->MemberExprBits.HasQualifier = HasQualifier;
  E->MemberExprBits.HasFoundDecl = HasFoundDecl;
  E->MemberExprBits.HasTemplateKWAndArgsInfo = HasTemplateInfo;
  E->MemberExprBits.HadMultipleCandidates = CurrentUnpackingBits->getNextBit();
  E->MemberExprBits.NonOdrUseReason =
      CurrentUnpackingBits->getNextBits(/*Width=*/2);
  E->MemberExprBits.OperatorLoc = Record.readSourceLocation();

  if (HasQualifier)
    new (E->getTrailingObjects<NestedNameSpecifierLoc>())
        NestedNameSpecifierLoc(Record.readNestedNameSpecifierLoc());

  if (HasFoundDecl) {
    auto *FoundD = Record.readDeclAs<NamedDecl>();
    auto AS = (AccessSpecifier)CurrentUnpackingBits->getNextBits(/*Width=*/2);
    *E->getTrailingObjects<DeclAccessPair>() = DeclAccessPair::make(FoundD, AS);
  }

  if (HasTemplateInfo)
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(), NumTemplateArgs);
}

void ASTStmtReader::VisitObjCIsaExpr(ObjCIsaExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setIsaMemberLoc(readSourceLocation());
  E->setOpLoc(readSourceLocation());
  E->setArrow(Record.readInt());
}

void ASTStmtReader::
VisitObjCIndirectCopyRestoreExpr(ObjCIndirectCopyRestoreExpr *E) {
  VisitExpr(E);
  E->Operand = Record.readSubExpr();
  E->setShouldCopy(Record.readInt());
}

void ASTStmtReader::VisitObjCBridgedCastExpr(ObjCBridgedCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->LParenLoc = readSourceLocation();
  E->BridgeKeywordLoc = readSourceLocation();
  E->Kind = Record.readInt();
}

void ASTStmtReader::VisitCastExpr(CastExpr *E) {
  VisitExpr(E);
  unsigned NumBaseSpecs = Record.readInt();
  assert(NumBaseSpecs == E->path_size());

  CurrentUnpackingBits.emplace(Record.readInt());
  E->setCastKind((CastKind)CurrentUnpackingBits->getNextBits(/*Width=*/7));
  unsigned HasFPFeatures = CurrentUnpackingBits->getNextBit();
  assert(E->hasStoredFPFeatures() == HasFPFeatures);

  E->setSubExpr(Record.readSubExpr());

  CastExpr::path_iterator BaseI = E->path_begin();
  while (NumBaseSpecs--) {
    auto *BaseSpec = new (Record.getContext()) CXXBaseSpecifier;
    *BaseSpec = Record.readCXXBaseSpecifier();
    *BaseI++ = BaseSpec;
  }
  if (HasFPFeatures)
    *E->getTrailingFPFeatures() =
        FPOptionsOverride::getFromOpaqueInt(Record.readInt());
}

void ASTStmtReader::VisitBinaryOperator(BinaryOperator *E) {
  VisitExpr(E);
  CurrentUnpackingBits.emplace(Record.readInt());
  E->setOpcode(
      (BinaryOperator::Opcode)CurrentUnpackingBits->getNextBits(/*Width=*/6));
  bool hasFP_Features = CurrentUnpackingBits->getNextBit();
  E->setHasStoredFPFeatures(hasFP_Features);
  E->setLHS(Record.readSubExpr());
  E->setRHS(Record.readSubExpr());
  E->setOperatorLoc(readSourceLocation());
  if (hasFP_Features)
    E->setStoredFPFeatures(
        FPOptionsOverride::getFromOpaqueInt(Record.readInt()));
}

void ASTStmtReader::VisitCompoundAssignOperator(CompoundAssignOperator *E) {
  VisitBinaryOperator(E);
  E->setComputationLHSType(Record.readType());
  E->setComputationResultType(Record.readType());
}

void ASTStmtReader::VisitConditionalOperator(ConditionalOperator *E) {
  VisitExpr(E);
  E->SubExprs[ConditionalOperator::COND] = Record.readSubExpr();
  E->SubExprs[ConditionalOperator::LHS] = Record.readSubExpr();
  E->SubExprs[ConditionalOperator::RHS] = Record.readSubExpr();
  E->QuestionLoc = readSourceLocation();
  E->ColonLoc = readSourceLocation();
}

void
ASTStmtReader::VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
  VisitExpr(E);
  E->OpaqueValue = cast<OpaqueValueExpr>(Record.readSubExpr());
  E->SubExprs[BinaryConditionalOperator::COMMON] = Record.readSubExpr();
  E->SubExprs[BinaryConditionalOperator::COND] = Record.readSubExpr();
  E->SubExprs[BinaryConditionalOperator::LHS] = Record.readSubExpr();
  E->SubExprs[BinaryConditionalOperator::RHS] = Record.readSubExpr();
  E->QuestionLoc = readSourceLocation();
  E->ColonLoc = readSourceLocation();
}

void ASTStmtReader::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  VisitCastExpr(E);
  E->setIsPartOfExplicitCast(CurrentUnpackingBits->getNextBit());
}

void ASTStmtReader::VisitExplicitCastExpr(ExplicitCastExpr *E) {
  VisitCastExpr(E);
  E->setTypeInfoAsWritten(readTypeSourceInfo());
}

void ASTStmtReader::VisitCStyleCastExpr(CStyleCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->setLParenLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
  VisitExpr(E);
  E->setLParenLoc(readSourceLocation());
  E->setTypeSourceInfo(readTypeSourceInfo());
  E->setInitializer(Record.readSubExpr());
  E->setFileScope(Record.readInt());
}

void ASTStmtReader::VisitExtVectorElementExpr(ExtVectorElementExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setAccessor(Record.readIdentifier());
  E->setAccessorLoc(readSourceLocation());
}

void ASTStmtReader::VisitInitListExpr(InitListExpr *E) {
  VisitExpr(E);
  if (auto *SyntForm = cast_or_null<InitListExpr>(Record.readSubStmt()))
    E->setSyntacticForm(SyntForm);
  E->setLBraceLoc(readSourceLocation());
  E->setRBraceLoc(readSourceLocation());
  bool isArrayFiller = Record.readInt();
  Expr *filler = nullptr;
  if (isArrayFiller) {
    filler = Record.readSubExpr();
    E->ArrayFillerOrUnionFieldInit = filler;
  } else
    E->ArrayFillerOrUnionFieldInit = readDeclAs<FieldDecl>();
  E->sawArrayRangeDesignator(Record.readInt());
  unsigned NumInits = Record.readInt();
  E->reserveInits(Record.getContext(), NumInits);
  if (isArrayFiller) {
    for (unsigned I = 0; I != NumInits; ++I) {
      Expr *init = Record.readSubExpr();
      E->updateInit(Record.getContext(), I, init ? init : filler);
    }
  } else {
    for (unsigned I = 0; I != NumInits; ++I)
      E->updateInit(Record.getContext(), I, Record.readSubExpr());
  }
}

void ASTStmtReader::VisitDesignatedInitExpr(DesignatedInitExpr *E) {
  using Designator = DesignatedInitExpr::Designator;

  VisitExpr(E);
  unsigned NumSubExprs = Record.readInt();
  assert(NumSubExprs == E->getNumSubExprs() && "Wrong number of subexprs");
  for (unsigned I = 0; I != NumSubExprs; ++I)
    E->setSubExpr(I, Record.readSubExpr());
  E->setEqualOrColonLoc(readSourceLocation());
  E->setGNUSyntax(Record.readInt());

  SmallVector<Designator, 4> Designators;
  while (Record.getIdx() < Record.size()) {
    switch ((DesignatorTypes)Record.readInt()) {
    case DESIG_FIELD_DECL: {
      auto *Field = readDeclAs<FieldDecl>();
      SourceLocation DotLoc = readSourceLocation();
      SourceLocation FieldLoc = readSourceLocation();
      Designators.push_back(Designator::CreateFieldDesignator(
          Field->getIdentifier(), DotLoc, FieldLoc));
      Designators.back().setFieldDecl(Field);
      break;
    }

    case DESIG_FIELD_NAME: {
      const IdentifierInfo *Name = Record.readIdentifier();
      SourceLocation DotLoc = readSourceLocation();
      SourceLocation FieldLoc = readSourceLocation();
      Designators.push_back(Designator::CreateFieldDesignator(Name, DotLoc,
                                                              FieldLoc));
      break;
    }

    case DESIG_ARRAY: {
      unsigned Index = Record.readInt();
      SourceLocation LBracketLoc = readSourceLocation();
      SourceLocation RBracketLoc = readSourceLocation();
      Designators.push_back(Designator::CreateArrayDesignator(Index,
                                                              LBracketLoc,
                                                              RBracketLoc));
      break;
    }

    case DESIG_ARRAY_RANGE: {
      unsigned Index = Record.readInt();
      SourceLocation LBracketLoc = readSourceLocation();
      SourceLocation EllipsisLoc = readSourceLocation();
      SourceLocation RBracketLoc = readSourceLocation();
      Designators.push_back(Designator::CreateArrayRangeDesignator(
          Index, LBracketLoc, EllipsisLoc, RBracketLoc));
      break;
    }
    }
  }
  E->setDesignators(Record.getContext(),
                    Designators.data(), Designators.size());
}

void ASTStmtReader::VisitDesignatedInitUpdateExpr(DesignatedInitUpdateExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setUpdater(Record.readSubExpr());
}

void ASTStmtReader::VisitNoInitExpr(NoInitExpr *E) {
  VisitExpr(E);
}

void ASTStmtReader::VisitArrayInitLoopExpr(ArrayInitLoopExpr *E) {
  VisitExpr(E);
  E->SubExprs[0] = Record.readSubExpr();
  E->SubExprs[1] = Record.readSubExpr();
}

void ASTStmtReader::VisitArrayInitIndexExpr(ArrayInitIndexExpr *E) {
  VisitExpr(E);
}

void ASTStmtReader::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  VisitExpr(E);
}

void ASTStmtReader::VisitVAArgExpr(VAArgExpr *E) {
  VisitExpr(E);
  E->setSubExpr(Record.readSubExpr());
  E->setWrittenTypeInfo(readTypeSourceInfo());
  E->setBuiltinLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
  E->setIsMicrosoftABI(Record.readInt());
}

void ASTStmtReader::VisitSourceLocExpr(SourceLocExpr *E) {
  VisitExpr(E);
  E->ParentContext = readDeclAs<DeclContext>();
  E->BuiltinLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
  E->SourceLocExprBits.Kind = Record.readInt();
}

void ASTStmtReader::VisitEmbedExpr(EmbedExpr *E) {
  VisitExpr(E);
  E->EmbedKeywordLoc = readSourceLocation();
  EmbedDataStorage *Data = new (Record.getContext()) EmbedDataStorage;
  Data->BinaryData = cast<StringLiteral>(Record.readSubStmt());
  E->Data = Data;
  E->Begin = Record.readInt();
  E->NumOfElements = Record.readInt();
}

void ASTStmtReader::VisitAddrLabelExpr(AddrLabelExpr *E) {
  VisitExpr(E);
  E->setAmpAmpLoc(readSourceLocation());
  E->setLabelLoc(readSourceLocation());
  E->setLabel(readDeclAs<LabelDecl>());
}

void ASTStmtReader::VisitStmtExpr(StmtExpr *E) {
  VisitExpr(E);
  E->setLParenLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
  E->setSubStmt(cast_or_null<CompoundStmt>(Record.readSubStmt()));
  E->StmtExprBits.TemplateDepth = Record.readInt();
}

void ASTStmtReader::VisitChooseExpr(ChooseExpr *E) {
  VisitExpr(E);
  E->setCond(Record.readSubExpr());
  E->setLHS(Record.readSubExpr());
  E->setRHS(Record.readSubExpr());
  E->setBuiltinLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
  E->setIsConditionTrue(Record.readInt());
}

void ASTStmtReader::VisitGNUNullExpr(GNUNullExpr *E) {
  VisitExpr(E);
  E->setTokenLocation(readSourceLocation());
}

void ASTStmtReader::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  VisitExpr(E);
  SmallVector<Expr *, 16> Exprs;
  unsigned NumExprs = Record.readInt();
  while (NumExprs--)
    Exprs.push_back(Record.readSubExpr());
  E->setExprs(Record.getContext(), Exprs);
  E->setBuiltinLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitConvertVectorExpr(ConvertVectorExpr *E) {
  VisitExpr(E);
  E->BuiltinLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
  E->TInfo = readTypeSourceInfo();
  E->SrcExpr = Record.readSubExpr();
}

void ASTStmtReader::VisitBlockExpr(BlockExpr *E) {
  VisitExpr(E);
  E->setBlockDecl(readDeclAs<BlockDecl>());
}

void ASTStmtReader::VisitGenericSelectionExpr(GenericSelectionExpr *E) {
  VisitExpr(E);

  unsigned NumAssocs = Record.readInt();
  assert(NumAssocs == E->getNumAssocs() && "Wrong NumAssocs!");
  E->IsExprPredicate = Record.readInt();
  E->ResultIndex = Record.readInt();
  E->GenericSelectionExprBits.GenericLoc = readSourceLocation();
  E->DefaultLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();

  Stmt **Stmts = E->getTrailingObjects<Stmt *>();
  // Add 1 to account for the controlling expression which is the first
  // expression in the trailing array of Stmt *. This is not needed for
  // the trailing array of TypeSourceInfo *.
  for (unsigned I = 0, N = NumAssocs + 1; I < N; ++I)
    Stmts[I] = Record.readSubExpr();

  TypeSourceInfo **TSIs = E->getTrailingObjects<TypeSourceInfo *>();
  for (unsigned I = 0, N = NumAssocs; I < N; ++I)
    TSIs[I] = readTypeSourceInfo();
}

void ASTStmtReader::VisitPseudoObjectExpr(PseudoObjectExpr *E) {
  VisitExpr(E);
  unsigned numSemanticExprs = Record.readInt();
  assert(numSemanticExprs + 1 == E->PseudoObjectExprBits.NumSubExprs);
  E->PseudoObjectExprBits.ResultIndex = Record.readInt();

  // Read the syntactic expression.
  E->getSubExprsBuffer()[0] = Record.readSubExpr();

  // Read all the semantic expressions.
  for (unsigned i = 0; i != numSemanticExprs; ++i) {
    Expr *subExpr = Record.readSubExpr();
    E->getSubExprsBuffer()[i+1] = subExpr;
  }
}

void ASTStmtReader::VisitAtomicExpr(AtomicExpr *E) {
  VisitExpr(E);
  E->Op = AtomicExpr::AtomicOp(Record.readInt());
  E->NumSubExprs = AtomicExpr::getNumSubExprs(E->Op);
  for (unsigned I = 0; I != E->NumSubExprs; ++I)
    E->SubExprs[I] = Record.readSubExpr();
  E->BuiltinLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
}

//===----------------------------------------------------------------------===//
// Objective-C Expressions and Statements

void ASTStmtReader::VisitObjCStringLiteral(ObjCStringLiteral *E) {
  VisitExpr(E);
  E->setString(cast<StringLiteral>(Record.readSubStmt()));
  E->setAtLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCBoxedExpr(ObjCBoxedExpr *E) {
  VisitExpr(E);
  // could be one of several IntegerLiteral, FloatLiteral, etc.
  E->SubExpr = Record.readSubStmt();
  E->BoxingMethod = readDeclAs<ObjCMethodDecl>();
  E->Range = readSourceRange();
}

void ASTStmtReader::VisitObjCArrayLiteral(ObjCArrayLiteral *E) {
  VisitExpr(E);
  unsigned NumElements = Record.readInt();
  assert(NumElements == E->getNumElements() && "Wrong number of elements");
  Expr **Elements = E->getElements();
  for (unsigned I = 0, N = NumElements; I != N; ++I)
    Elements[I] = Record.readSubExpr();
  E->ArrayWithObjectsMethod = readDeclAs<ObjCMethodDecl>();
  E->Range = readSourceRange();
}

void ASTStmtReader::VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
  VisitExpr(E);
  unsigned NumElements = Record.readInt();
  assert(NumElements == E->getNumElements() && "Wrong number of elements");
  bool HasPackExpansions = Record.readInt();
  assert(HasPackExpansions == E->HasPackExpansions &&"Pack expansion mismatch");
  auto *KeyValues =
      E->getTrailingObjects<ObjCDictionaryLiteral::KeyValuePair>();
  auto *Expansions =
      E->getTrailingObjects<ObjCDictionaryLiteral::ExpansionData>();
  for (unsigned I = 0; I != NumElements; ++I) {
    KeyValues[I].Key = Record.readSubExpr();
    KeyValues[I].Value = Record.readSubExpr();
    if (HasPackExpansions) {
      Expansions[I].EllipsisLoc = readSourceLocation();
      Expansions[I].NumExpansionsPlusOne = Record.readInt();
    }
  }
  E->DictWithObjectsMethod = readDeclAs<ObjCMethodDecl>();
  E->Range = readSourceRange();
}

void ASTStmtReader::VisitObjCEncodeExpr(ObjCEncodeExpr *E) {
  VisitExpr(E);
  E->setEncodedTypeSourceInfo(readTypeSourceInfo());
  E->setAtLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCSelectorExpr(ObjCSelectorExpr *E) {
  VisitExpr(E);
  E->setSelector(Record.readSelector());
  E->setAtLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCProtocolExpr(ObjCProtocolExpr *E) {
  VisitExpr(E);
  E->setProtocol(readDeclAs<ObjCProtocolDecl>());
  E->setAtLoc(readSourceLocation());
  E->ProtoLoc = readSourceLocation();
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
  VisitExpr(E);
  E->setDecl(readDeclAs<ObjCIvarDecl>());
  E->setLocation(readSourceLocation());
  E->setOpLoc(readSourceLocation());
  E->setBase(Record.readSubExpr());
  E->setIsArrow(Record.readInt());
  E->setIsFreeIvar(Record.readInt());
}

void ASTStmtReader::VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
  VisitExpr(E);
  unsigned MethodRefFlags = Record.readInt();
  bool Implicit = Record.readInt() != 0;
  if (Implicit) {
    auto *Getter = readDeclAs<ObjCMethodDecl>();
    auto *Setter = readDeclAs<ObjCMethodDecl>();
    E->setImplicitProperty(Getter, Setter, MethodRefFlags);
  } else {
    E->setExplicitProperty(readDeclAs<ObjCPropertyDecl>(), MethodRefFlags);
  }
  E->setLocation(readSourceLocation());
  E->setReceiverLocation(readSourceLocation());
  switch (Record.readInt()) {
  case 0:
    E->setBase(Record.readSubExpr());
    break;
  case 1:
    E->setSuperReceiver(Record.readType());
    break;
  case 2:
    E->setClassReceiver(readDeclAs<ObjCInterfaceDecl>());
    break;
  }
}

void ASTStmtReader::VisitObjCSubscriptRefExpr(ObjCSubscriptRefExpr *E) {
  VisitExpr(E);
  E->setRBracket(readSourceLocation());
  E->setBaseExpr(Record.readSubExpr());
  E->setKeyExpr(Record.readSubExpr());
  E->GetAtIndexMethodDecl = readDeclAs<ObjCMethodDecl>();
  E->SetAtIndexMethodDecl = readDeclAs<ObjCMethodDecl>();
}

void ASTStmtReader::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  VisitExpr(E);
  assert(Record.peekInt() == E->getNumArgs());
  Record.skipInts(1);
  unsigned NumStoredSelLocs = Record.readInt();
  E->SelLocsKind = Record.readInt();
  E->setDelegateInitCall(Record.readInt());
  E->IsImplicit = Record.readInt();
  auto Kind = static_cast<ObjCMessageExpr::ReceiverKind>(Record.readInt());
  switch (Kind) {
  case ObjCMessageExpr::Instance:
    E->setInstanceReceiver(Record.readSubExpr());
    break;

  case ObjCMessageExpr::Class:
    E->setClassReceiver(readTypeSourceInfo());
    break;

  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance: {
    QualType T = Record.readType();
    SourceLocation SuperLoc = readSourceLocation();
    E->setSuper(SuperLoc, T, Kind == ObjCMessageExpr::SuperInstance);
    break;
  }
  }

  assert(Kind == E->getReceiverKind());

  if (Record.readInt())
    E->setMethodDecl(readDeclAs<ObjCMethodDecl>());
  else
    E->setSelector(Record.readSelector());

  E->LBracLoc = readSourceLocation();
  E->RBracLoc = readSourceLocation();

  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    E->setArg(I, Record.readSubExpr());

  SourceLocation *Locs = E->getStoredSelLocs();
  for (unsigned I = 0; I != NumStoredSelLocs; ++I)
    Locs[I] = readSourceLocation();
}

void ASTStmtReader::VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  VisitStmt(S);
  S->setElement(Record.readSubStmt());
  S->setCollection(Record.readSubExpr());
  S->setBody(Record.readSubStmt());
  S->setForLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {
  VisitStmt(S);
  S->setCatchBody(Record.readSubStmt());
  S->setCatchParamDecl(readDeclAs<VarDecl>());
  S->setAtCatchLoc(readSourceLocation());
  S->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
  VisitStmt(S);
  S->setFinallyBody(Record.readSubStmt());
  S->setAtFinallyLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S) {
  VisitStmt(S); // FIXME: no test coverage.
  S->setSubStmt(Record.readSubStmt());
  S->setAtLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
  VisitStmt(S);
  assert(Record.peekInt() == S->getNumCatchStmts());
  Record.skipInts(1);
  bool HasFinally = Record.readInt();
  S->setTryBody(Record.readSubStmt());
  for (unsigned I = 0, N = S->getNumCatchStmts(); I != N; ++I)
    S->setCatchStmt(I, cast_or_null<ObjCAtCatchStmt>(Record.readSubStmt()));

  if (HasFinally)
    S->setFinallyStmt(Record.readSubStmt());
  S->setAtTryLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
  VisitStmt(S); // FIXME: no test coverage.
  S->setSynchExpr(Record.readSubStmt());
  S->setSynchBody(Record.readSubStmt());
  S->setAtSynchronizedLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  VisitStmt(S); // FIXME: no test coverage.
  S->setThrowExpr(Record.readSubStmt());
  S->setThrowLoc(readSourceLocation());
}

void ASTStmtReader::VisitObjCBoolLiteralExpr(ObjCBoolLiteralExpr *E) {
  VisitExpr(E);
  E->setValue(Record.readInt());
  E->setLocation(readSourceLocation());
}

void ASTStmtReader::VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *E) {
  VisitExpr(E);
  SourceRange R = Record.readSourceRange();
  E->AtLoc = R.getBegin();
  E->RParen = R.getEnd();
  E->VersionToCheck = Record.readVersionTuple();
}

//===----------------------------------------------------------------------===//
// C++ Expressions and Statements
//===----------------------------------------------------------------------===//

void ASTStmtReader::VisitCXXCatchStmt(CXXCatchStmt *S) {
  VisitStmt(S);
  S->CatchLoc = readSourceLocation();
  S->ExceptionDecl = readDeclAs<VarDecl>();
  S->HandlerBlock = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXTryStmt(CXXTryStmt *S) {
  VisitStmt(S);
  assert(Record.peekInt() == S->getNumHandlers() && "NumStmtFields is wrong ?");
  Record.skipInts(1);
  S->TryLoc = readSourceLocation();
  S->getStmts()[0] = Record.readSubStmt();
  for (unsigned i = 0, e = S->getNumHandlers(); i != e; ++i)
    S->getStmts()[i + 1] = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXForRangeStmt(CXXForRangeStmt *S) {
  VisitStmt(S);
  S->ForLoc = readSourceLocation();
  S->CoawaitLoc = readSourceLocation();
  S->ColonLoc = readSourceLocation();
  S->RParenLoc = readSourceLocation();
  S->setInit(Record.readSubStmt());
  S->setRangeStmt(Record.readSubStmt());
  S->setBeginStmt(Record.readSubStmt());
  S->setEndStmt(Record.readSubStmt());
  S->setCond(Record.readSubExpr());
  S->setInc(Record.readSubExpr());
  S->setLoopVarStmt(Record.readSubStmt());
  S->setBody(Record.readSubStmt());
}

void ASTStmtReader::VisitMSDependentExistsStmt(MSDependentExistsStmt *S) {
  VisitStmt(S);
  S->KeywordLoc = readSourceLocation();
  S->IsIfExists = Record.readInt();
  S->QualifierLoc = Record.readNestedNameSpecifierLoc();
  S->NameInfo = Record.readDeclarationNameInfo();
  S->SubStmt = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
  VisitCallExpr(E);
  E->CXXOperatorCallExprBits.OperatorKind = Record.readInt();
  E->Range = Record.readSourceRange();
}

void ASTStmtReader::VisitCXXRewrittenBinaryOperator(
    CXXRewrittenBinaryOperator *E) {
  VisitExpr(E);
  E->CXXRewrittenBinaryOperatorBits.IsReversed = Record.readInt();
  E->SemanticForm = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXConstructExpr(CXXConstructExpr *E) {
  VisitExpr(E);

  unsigned NumArgs = Record.readInt();
  assert((NumArgs == E->getNumArgs()) && "Wrong NumArgs!");

  E->CXXConstructExprBits.Elidable = Record.readInt();
  E->CXXConstructExprBits.HadMultipleCandidates = Record.readInt();
  E->CXXConstructExprBits.ListInitialization = Record.readInt();
  E->CXXConstructExprBits.StdInitListInitialization = Record.readInt();
  E->CXXConstructExprBits.ZeroInitialization = Record.readInt();
  E->CXXConstructExprBits.ConstructionKind = Record.readInt();
  E->CXXConstructExprBits.IsImmediateEscalating = Record.readInt();
  E->CXXConstructExprBits.Loc = readSourceLocation();
  E->Constructor = readDeclAs<CXXConstructorDecl>();
  E->ParenOrBraceRange = readSourceRange();

  for (unsigned I = 0; I != NumArgs; ++I)
    E->setArg(I, Record.readSubExpr());
}

void ASTStmtReader::VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *E) {
  VisitExpr(E);
  E->Constructor = readDeclAs<CXXConstructorDecl>();
  E->Loc = readSourceLocation();
  E->ConstructsVirtualBase = Record.readInt();
  E->InheritedFromVirtualBase = Record.readInt();
}

void ASTStmtReader::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *E) {
  VisitCXXConstructExpr(E);
  E->TSI = readTypeSourceInfo();
}

void ASTStmtReader::VisitLambdaExpr(LambdaExpr *E) {
  VisitExpr(E);
  unsigned NumCaptures = Record.readInt();
  (void)NumCaptures;
  assert(NumCaptures == E->LambdaExprBits.NumCaptures);
  E->IntroducerRange = readSourceRange();
  E->LambdaExprBits.CaptureDefault = Record.readInt();
  E->CaptureDefaultLoc = readSourceLocation();
  E->LambdaExprBits.ExplicitParams = Record.readInt();
  E->LambdaExprBits.ExplicitResultType = Record.readInt();
  E->ClosingBrace = readSourceLocation();

  // Read capture initializers.
  for (LambdaExpr::capture_init_iterator C = E->capture_init_begin(),
                                         CEnd = E->capture_init_end();
       C != CEnd; ++C)
    *C = Record.readSubExpr();

  // The body will be lazily deserialized when needed from the call operator
  // declaration.
}

void
ASTStmtReader::VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E) {
  VisitExpr(E);
  E->SubExpr = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXNamedCastExpr(CXXNamedCastExpr *E) {
  VisitExplicitCastExpr(E);
  SourceRange R = readSourceRange();
  E->Loc = R.getBegin();
  E->RParenLoc = R.getEnd();
  if (CurrentUnpackingBits->getNextBit())
    E->AngleBrackets = readSourceRange();
}

void ASTStmtReader::VisitCXXStaticCastExpr(CXXStaticCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXDynamicCastExpr(CXXDynamicCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXAddrspaceCastExpr(CXXAddrspaceCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXConstCastExpr(CXXConstCastExpr *E) {
  return VisitCXXNamedCastExpr(E);
}

void ASTStmtReader::VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->setLParenLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
}

void ASTStmtReader::VisitBuiltinBitCastExpr(BuiltinBitCastExpr *E) {
  VisitExplicitCastExpr(E);
  E->KWLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
}

void ASTStmtReader::VisitUserDefinedLiteral(UserDefinedLiteral *E) {
  VisitCallExpr(E);
  E->UDSuffixLoc = readSourceLocation();
}

void ASTStmtReader::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
  VisitExpr(E);
  E->setValue(Record.readInt());
  E->setLocation(readSourceLocation());
}

void ASTStmtReader::VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E) {
  VisitExpr(E);
  E->setLocation(readSourceLocation());
}

void ASTStmtReader::VisitCXXTypeidExpr(CXXTypeidExpr *E) {
  VisitExpr(E);
  E->setSourceRange(readSourceRange());
  if (E->isTypeOperand())
    E->Operand = readTypeSourceInfo();
  else
    E->Operand = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXThisExpr(CXXThisExpr *E) {
  VisitExpr(E);
  E->setLocation(readSourceLocation());
  E->setImplicit(Record.readInt());
  E->setCapturedByCopyInLambdaWithExplicitObjectParameter(Record.readInt());
}

void ASTStmtReader::VisitCXXThrowExpr(CXXThrowExpr *E) {
  VisitExpr(E);
  E->CXXThrowExprBits.ThrowLoc = readSourceLocation();
  E->Operand = Record.readSubExpr();
  E->CXXThrowExprBits.IsThrownVariableInScope = Record.readInt();
}

void ASTStmtReader::VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
  VisitExpr(E);
  E->Param = readDeclAs<ParmVarDecl>();
  E->UsedContext = readDeclAs<DeclContext>();
  E->CXXDefaultArgExprBits.Loc = readSourceLocation();
  E->CXXDefaultArgExprBits.HasRewrittenInit = Record.readInt();
  if (E->CXXDefaultArgExprBits.HasRewrittenInit)
    *E->getTrailingObjects<Expr *>() = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) {
  VisitExpr(E);
  E->CXXDefaultInitExprBits.HasRewrittenInit = Record.readInt();
  E->Field = readDeclAs<FieldDecl>();
  E->UsedContext = readDeclAs<DeclContext>();
  E->CXXDefaultInitExprBits.Loc = readSourceLocation();
  if (E->CXXDefaultInitExprBits.HasRewrittenInit)
    *E->getTrailingObjects<Expr *>() = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  VisitExpr(E);
  E->setTemporary(Record.readCXXTemporary());
  E->setSubExpr(Record.readSubExpr());
}

void ASTStmtReader::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *E) {
  VisitExpr(E);
  E->TypeInfo = readTypeSourceInfo();
  E->CXXScalarValueInitExprBits.RParenLoc = readSourceLocation();
}

void ASTStmtReader::VisitCXXNewExpr(CXXNewExpr *E) {
  VisitExpr(E);

  bool IsArray = Record.readInt();
  bool HasInit = Record.readInt();
  unsigned NumPlacementArgs = Record.readInt();
  bool IsParenTypeId = Record.readInt();

  E->CXXNewExprBits.IsGlobalNew = Record.readInt();
  E->CXXNewExprBits.ShouldPassAlignment = Record.readInt();
  E->CXXNewExprBits.UsualArrayDeleteWantsSize = Record.readInt();
  E->CXXNewExprBits.HasInitializer = Record.readInt();
  E->CXXNewExprBits.StoredInitializationStyle = Record.readInt();

  assert((IsArray == E->isArray()) && "Wrong IsArray!");
  assert((HasInit == E->hasInitializer()) && "Wrong HasInit!");
  assert((NumPlacementArgs == E->getNumPlacementArgs()) &&
         "Wrong NumPlacementArgs!");
  assert((IsParenTypeId == E->isParenTypeId()) && "Wrong IsParenTypeId!");
  (void)IsArray;
  (void)HasInit;
  (void)NumPlacementArgs;

  E->setOperatorNew(readDeclAs<FunctionDecl>());
  E->setOperatorDelete(readDeclAs<FunctionDecl>());
  E->AllocatedTypeInfo = readTypeSourceInfo();
  if (IsParenTypeId)
    E->getTrailingObjects<SourceRange>()[0] = readSourceRange();
  E->Range = readSourceRange();
  E->DirectInitRange = readSourceRange();

  // Install all the subexpressions.
  for (CXXNewExpr::raw_arg_iterator I = E->raw_arg_begin(),
                                    N = E->raw_arg_end();
       I != N; ++I)
    *I = Record.readSubStmt();
}

void ASTStmtReader::VisitCXXDeleteExpr(CXXDeleteExpr *E) {
  VisitExpr(E);
  E->CXXDeleteExprBits.GlobalDelete = Record.readInt();
  E->CXXDeleteExprBits.ArrayForm = Record.readInt();
  E->CXXDeleteExprBits.ArrayFormAsWritten = Record.readInt();
  E->CXXDeleteExprBits.UsualArrayDeleteWantsSize = Record.readInt();
  E->OperatorDelete = readDeclAs<FunctionDecl>();
  E->Argument = Record.readSubExpr();
  E->CXXDeleteExprBits.Loc = readSourceLocation();
}

void ASTStmtReader::VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E) {
  VisitExpr(E);

  E->Base = Record.readSubExpr();
  E->IsArrow = Record.readInt();
  E->OperatorLoc = readSourceLocation();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  E->ScopeType = readTypeSourceInfo();
  E->ColonColonLoc = readSourceLocation();
  E->TildeLoc = readSourceLocation();

  IdentifierInfo *II = Record.readIdentifier();
  if (II)
    E->setDestroyedType(II, readSourceLocation());
  else
    E->setDestroyedType(readTypeSourceInfo());
}

void ASTStmtReader::VisitExprWithCleanups(ExprWithCleanups *E) {
  VisitExpr(E);

  unsigned NumObjects = Record.readInt();
  assert(NumObjects == E->getNumObjects());
  for (unsigned i = 0; i != NumObjects; ++i) {
    unsigned CleanupKind = Record.readInt();
    ExprWithCleanups::CleanupObject Obj;
    if (CleanupKind == COK_Block)
      Obj = readDeclAs<BlockDecl>();
    else if (CleanupKind == COK_CompoundLiteral)
      Obj = cast<CompoundLiteralExpr>(Record.readSubExpr());
    else
      llvm_unreachable("unexpected cleanup object type");
    E->getTrailingObjects<ExprWithCleanups::CleanupObject>()[i] = Obj;
  }

  E->ExprWithCleanupsBits.CleanupsHaveSideEffects = Record.readInt();
  E->SubExpr = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXDependentScopeMemberExpr(
    CXXDependentScopeMemberExpr *E) {
  VisitExpr(E);

  unsigned NumTemplateArgs = Record.readInt();
  CurrentUnpackingBits.emplace(Record.readInt());
  bool HasTemplateKWAndArgsInfo = CurrentUnpackingBits->getNextBit();
  bool HasFirstQualifierFoundInScope = CurrentUnpackingBits->getNextBit();

  assert((HasTemplateKWAndArgsInfo == E->hasTemplateKWAndArgsInfo()) &&
         "Wrong HasTemplateKWAndArgsInfo!");
  assert(
      (HasFirstQualifierFoundInScope == E->hasFirstQualifierFoundInScope()) &&
      "Wrong HasFirstQualifierFoundInScope!");

  if (HasTemplateKWAndArgsInfo)
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(), NumTemplateArgs);

  assert((NumTemplateArgs == E->getNumTemplateArgs()) &&
         "Wrong NumTemplateArgs!");

  E->CXXDependentScopeMemberExprBits.IsArrow =
      CurrentUnpackingBits->getNextBit();

  E->BaseType = Record.readType();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  // not ImplicitAccess
  if (CurrentUnpackingBits->getNextBit())
    E->Base = Record.readSubExpr();
  else
    E->Base = nullptr;

  E->CXXDependentScopeMemberExprBits.OperatorLoc = readSourceLocation();

  if (HasFirstQualifierFoundInScope)
    *E->getTrailingObjects<NamedDecl *>() = readDeclAs<NamedDecl>();

  E->MemberNameInfo = Record.readDeclarationNameInfo();
}

void
ASTStmtReader::VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
  VisitExpr(E);

  if (CurrentUnpackingBits->getNextBit()) // HasTemplateKWAndArgsInfo
    ReadTemplateKWAndArgsInfo(
        *E->getTrailingObjects<ASTTemplateKWAndArgsInfo>(),
        E->getTrailingObjects<TemplateArgumentLoc>(),
        /*NumTemplateArgs=*/CurrentUnpackingBits->getNextBits(/*Width=*/16));

  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  E->NameInfo = Record.readDeclarationNameInfo();
}

void
ASTStmtReader::VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E) {
  VisitExpr(E);
  assert(Record.peekInt() == E->getNumArgs() &&
         "Read wrong record during creation ?");
  Record.skipInts(1);
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    E->setArg(I, Record.readSubExpr());
  E->TypeAndInitForm.setPointer(readTypeSourceInfo());
  E->setLParenLoc(readSourceLocation());
  E->setRParenLoc(readSourceLocation());
  E->TypeAndInitForm.setInt(Record.readInt());
}

void ASTStmtReader::VisitOverloadExpr(OverloadExpr *E) {
  VisitExpr(E);

  unsigned NumResults = Record.readInt();
  CurrentUnpackingBits.emplace(Record.readInt());
  bool HasTemplateKWAndArgsInfo = CurrentUnpackingBits->getNextBit();
  assert((E->getNumDecls() == NumResults) && "Wrong NumResults!");
  assert((E->hasTemplateKWAndArgsInfo() == HasTemplateKWAndArgsInfo) &&
         "Wrong HasTemplateKWAndArgsInfo!");

  if (HasTemplateKWAndArgsInfo) {
    unsigned NumTemplateArgs = Record.readInt();
    ReadTemplateKWAndArgsInfo(*E->getTrailingASTTemplateKWAndArgsInfo(),
                              E->getTrailingTemplateArgumentLoc(),
                              NumTemplateArgs);
    assert((E->getNumTemplateArgs() == NumTemplateArgs) &&
           "Wrong NumTemplateArgs!");
  }

  UnresolvedSet<8> Decls;
  for (unsigned I = 0; I != NumResults; ++I) {
    auto *D = readDeclAs<NamedDecl>();
    auto AS = (AccessSpecifier)Record.readInt();
    Decls.addDecl(D, AS);
  }

  DeclAccessPair *Results = E->getTrailingResults();
  UnresolvedSetIterator Iter = Decls.begin();
  for (unsigned I = 0; I != NumResults; ++I) {
    Results[I] = (Iter + I).getPair();
  }

  E->NameInfo = Record.readDeclarationNameInfo();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
}

void ASTStmtReader::VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
  VisitOverloadExpr(E);
  E->UnresolvedMemberExprBits.IsArrow = CurrentUnpackingBits->getNextBit();
  E->UnresolvedMemberExprBits.HasUnresolvedUsing =
      CurrentUnpackingBits->getNextBit();

  if (/*!isImplicitAccess=*/CurrentUnpackingBits->getNextBit())
    E->Base = Record.readSubExpr();
  else
    E->Base = nullptr;

  E->OperatorLoc = readSourceLocation();

  E->BaseType = Record.readType();
}

void ASTStmtReader::VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
  VisitOverloadExpr(E);
  E->UnresolvedLookupExprBits.RequiresADL = CurrentUnpackingBits->getNextBit();
  E->NamingClass = readDeclAs<CXXRecordDecl>();
}

void ASTStmtReader::VisitTypeTraitExpr(TypeTraitExpr *E) {
  VisitExpr(E);
  E->TypeTraitExprBits.NumArgs = Record.readInt();
  E->TypeTraitExprBits.Kind = Record.readInt();
  E->TypeTraitExprBits.Value = Record.readInt();
  SourceRange Range = readSourceRange();
  E->Loc = Range.getBegin();
  E->RParenLoc = Range.getEnd();

  auto **Args = E->getTrailingObjects<TypeSourceInfo *>();
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    Args[I] = readTypeSourceInfo();
}

void ASTStmtReader::VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {
  VisitExpr(E);
  E->ATT = (ArrayTypeTrait)Record.readInt();
  E->Value = (unsigned int)Record.readInt();
  SourceRange Range = readSourceRange();
  E->Loc = Range.getBegin();
  E->RParen = Range.getEnd();
  E->QueriedType = readTypeSourceInfo();
  E->Dimension = Record.readSubExpr();
}

void ASTStmtReader::VisitExpressionTraitExpr(ExpressionTraitExpr *E) {
  VisitExpr(E);
  E->ET = (ExpressionTrait)Record.readInt();
  E->Value = (bool)Record.readInt();
  SourceRange Range = readSourceRange();
  E->QueriedExpression = Record.readSubExpr();
  E->Loc = Range.getBegin();
  E->RParen = Range.getEnd();
}

void ASTStmtReader::VisitCXXNoexceptExpr(CXXNoexceptExpr *E) {
  VisitExpr(E);
  E->CXXNoexceptExprBits.Value = Record.readInt();
  E->Range = readSourceRange();
  E->Operand = Record.readSubExpr();
}

void ASTStmtReader::VisitPackExpansionExpr(PackExpansionExpr *E) {
  VisitExpr(E);
  E->EllipsisLoc = readSourceLocation();
  E->NumExpansions = Record.readInt();
  E->Pattern = Record.readSubExpr();
}

void ASTStmtReader::VisitSizeOfPackExpr(SizeOfPackExpr *E) {
  VisitExpr(E);
  unsigned NumPartialArgs = Record.readInt();
  E->OperatorLoc = readSourceLocation();
  E->PackLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
  E->Pack = Record.readDeclAs<NamedDecl>();
  if (E->isPartiallySubstituted()) {
    assert(E->Length == NumPartialArgs);
    for (auto *I = E->getTrailingObjects<TemplateArgument>(),
              *E = I + NumPartialArgs;
         I != E; ++I)
      new (I) TemplateArgument(Record.readTemplateArgument());
  } else if (!E->isValueDependent()) {
    E->Length = Record.readInt();
  }
}

void ASTStmtReader::VisitPackIndexingExpr(PackIndexingExpr *E) {
  VisitExpr(E);
  E->TransformedExpressions = Record.readInt();
  E->ExpandedToEmptyPack = Record.readInt();
  E->EllipsisLoc = readSourceLocation();
  E->RSquareLoc = readSourceLocation();
  E->SubExprs[0] = Record.readStmt();
  E->SubExprs[1] = Record.readStmt();
  auto **Exprs = E->getTrailingObjects<Expr *>();
  for (unsigned I = 0; I < E->TransformedExpressions; ++I)
    Exprs[I] = Record.readExpr();
}

void ASTStmtReader::VisitSubstNonTypeTemplateParmExpr(
                                              SubstNonTypeTemplateParmExpr *E) {
  VisitExpr(E);
  E->AssociatedDeclAndRef.setPointer(readDeclAs<Decl>());
  E->AssociatedDeclAndRef.setInt(CurrentUnpackingBits->getNextBit());
  E->Index = CurrentUnpackingBits->getNextBits(/*Width=*/12);
  if (CurrentUnpackingBits->getNextBit())
    E->PackIndex = Record.readInt();
  else
    E->PackIndex = 0;
  E->SubstNonTypeTemplateParmExprBits.NameLoc = readSourceLocation();
  E->Replacement = Record.readSubExpr();
}

void ASTStmtReader::VisitSubstNonTypeTemplateParmPackExpr(
                                          SubstNonTypeTemplateParmPackExpr *E) {
  VisitExpr(E);
  E->AssociatedDecl = readDeclAs<Decl>();
  E->Index = Record.readInt();
  TemplateArgument ArgPack = Record.readTemplateArgument();
  if (ArgPack.getKind() != TemplateArgument::Pack)
    return;

  E->Arguments = ArgPack.pack_begin();
  E->NumArguments = ArgPack.pack_size();
  E->NameLoc = readSourceLocation();
}

void ASTStmtReader::VisitFunctionParmPackExpr(FunctionParmPackExpr *E) {
  VisitExpr(E);
  E->NumParameters = Record.readInt();
  E->ParamPack = readDeclAs<ParmVarDecl>();
  E->NameLoc = readSourceLocation();
  auto **Parms = E->getTrailingObjects<VarDecl *>();
  for (unsigned i = 0, n = E->NumParameters; i != n; ++i)
    Parms[i] = readDeclAs<VarDecl>();
}

void ASTStmtReader::VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *E) {
  VisitExpr(E);
  bool HasMaterialzedDecl = Record.readInt();
  if (HasMaterialzedDecl)
    E->State = cast<LifetimeExtendedTemporaryDecl>(Record.readDecl());
  else
    E->State = Record.readSubExpr();
}

void ASTStmtReader::VisitCXXFoldExpr(CXXFoldExpr *E) {
  VisitExpr(E);
  E->LParenLoc = readSourceLocation();
  E->EllipsisLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
  E->NumExpansions = Record.readInt();
  E->SubExprs[0] = Record.readSubExpr();
  E->SubExprs[1] = Record.readSubExpr();
  E->SubExprs[2] = Record.readSubExpr();
  E->Opcode = (BinaryOperatorKind)Record.readInt();
}

void ASTStmtReader::VisitCXXParenListInitExpr(CXXParenListInitExpr *E) {
  VisitExpr(E);
  unsigned ExpectedNumExprs = Record.readInt();
  assert(E->NumExprs == ExpectedNumExprs &&
         "expected number of expressions does not equal the actual number of "
         "serialized expressions.");
  E->NumUserSpecifiedExprs = Record.readInt();
  E->InitLoc = readSourceLocation();
  E->LParenLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
  for (unsigned I = 0; I < ExpectedNumExprs; I++)
    E->getTrailingObjects<Expr *>()[I] = Record.readSubExpr();

  bool HasArrayFillerOrUnionDecl = Record.readBool();
  if (HasArrayFillerOrUnionDecl) {
    bool HasArrayFiller = Record.readBool();
    if (HasArrayFiller) {
      E->setArrayFiller(Record.readSubExpr());
    } else {
      E->setInitializedFieldInUnion(readDeclAs<FieldDecl>());
    }
  }
  E->updateDependence();
}

void ASTStmtReader::VisitOpaqueValueExpr(OpaqueValueExpr *E) {
  VisitExpr(E);
  E->SourceExpr = Record.readSubExpr();
  E->OpaqueValueExprBits.Loc = readSourceLocation();
  E->setIsUnique(Record.readInt());
}

void ASTStmtReader::VisitTypoExpr(TypoExpr *E) {
  llvm_unreachable("Cannot read TypoExpr nodes");
}

void ASTStmtReader::VisitRecoveryExpr(RecoveryExpr *E) {
  VisitExpr(E);
  unsigned NumArgs = Record.readInt();
  E->BeginLoc = readSourceLocation();
  E->EndLoc = readSourceLocation();
  assert((NumArgs + 0LL ==
          std::distance(E->children().begin(), E->children().end())) &&
         "Wrong NumArgs!");
  (void)NumArgs;
  for (Stmt *&Child : E->children())
    Child = Record.readSubStmt();
}

//===----------------------------------------------------------------------===//
// Microsoft Expressions and Statements
//===----------------------------------------------------------------------===//
void ASTStmtReader::VisitMSPropertyRefExpr(MSPropertyRefExpr *E) {
  VisitExpr(E);
  E->IsArrow = (Record.readInt() != 0);
  E->BaseExpr = Record.readSubExpr();
  E->QualifierLoc = Record.readNestedNameSpecifierLoc();
  E->MemberLoc = readSourceLocation();
  E->TheDecl = readDeclAs<MSPropertyDecl>();
}

void ASTStmtReader::VisitMSPropertySubscriptExpr(MSPropertySubscriptExpr *E) {
  VisitExpr(E);
  E->setBase(Record.readSubExpr());
  E->setIdx(Record.readSubExpr());
  E->setRBracketLoc(readSourceLocation());
}

void ASTStmtReader::VisitCXXUuidofExpr(CXXUuidofExpr *E) {
  VisitExpr(E);
  E->setSourceRange(readSourceRange());
  E->Guid = readDeclAs<MSGuidDecl>();
  if (E->isTypeOperand())
    E->Operand = readTypeSourceInfo();
  else
    E->Operand = Record.readSubExpr();
}

void ASTStmtReader::VisitSEHLeaveStmt(SEHLeaveStmt *S) {
  VisitStmt(S);
  S->setLeaveLoc(readSourceLocation());
}

void ASTStmtReader::VisitSEHExceptStmt(SEHExceptStmt *S) {
  VisitStmt(S);
  S->Loc = readSourceLocation();
  S->Children[SEHExceptStmt::FILTER_EXPR] = Record.readSubStmt();
  S->Children[SEHExceptStmt::BLOCK] = Record.readSubStmt();
}

void ASTStmtReader::VisitSEHFinallyStmt(SEHFinallyStmt *S) {
  VisitStmt(S);
  S->Loc = readSourceLocation();
  S->Block = Record.readSubStmt();
}

void ASTStmtReader::VisitSEHTryStmt(SEHTryStmt *S) {
  VisitStmt(S);
  S->IsCXXTry = Record.readInt();
  S->TryLoc = readSourceLocation();
  S->Children[SEHTryStmt::TRY] = Record.readSubStmt();
  S->Children[SEHTryStmt::HANDLER] = Record.readSubStmt();
}

//===----------------------------------------------------------------------===//
// CUDA Expressions and Statements
//===----------------------------------------------------------------------===//

void ASTStmtReader::VisitCUDAKernelCallExpr(CUDAKernelCallExpr *E) {
  VisitCallExpr(E);
  E->setPreArg(CUDAKernelCallExpr::CONFIG, Record.readSubExpr());
}

//===----------------------------------------------------------------------===//
// OpenCL Expressions and Statements.
//===----------------------------------------------------------------------===//
void ASTStmtReader::VisitAsTypeExpr(AsTypeExpr *E) {
  VisitExpr(E);
  E->BuiltinLoc = readSourceLocation();
  E->RParenLoc = readSourceLocation();
  E->SrcExpr = Record.readSubExpr();
}

//===----------------------------------------------------------------------===//
// OpenMP Directives.
//===----------------------------------------------------------------------===//

void ASTStmtReader::VisitOMPCanonicalLoop(OMPCanonicalLoop *S) {
  VisitStmt(S);
  for (Stmt *&SubStmt : S->SubStmts)
    SubStmt = Record.readSubStmt();
}

void ASTStmtReader::VisitOMPExecutableDirective(OMPExecutableDirective *E) {
  Record.readOMPChildren(E->Data);
  E->setLocStart(readSourceLocation());
  E->setLocEnd(readSourceLocation());
  E->setMappedDirective(Record.readEnum<OpenMPDirectiveKind>());
}

void ASTStmtReader::VisitOMPLoopBasedDirective(OMPLoopBasedDirective *D) {
  VisitStmt(D);
  // Field CollapsedNum was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPLoopDirective(OMPLoopDirective *D) {
  VisitOMPLoopBasedDirective(D);
}

void ASTStmtReader::VisitOMPMetaDirective(OMPMetaDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPParallelDirective(OMPParallelDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPSimdDirective(OMPSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPLoopTransformationDirective(
    OMPLoopTransformationDirective *D) {
  VisitOMPLoopBasedDirective(D);
  D->setNumGeneratedLoops(Record.readUInt32());
}

void ASTStmtReader::VisitOMPTileDirective(OMPTileDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void ASTStmtReader::VisitOMPUnrollDirective(OMPUnrollDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void ASTStmtReader::VisitOMPReverseDirective(OMPReverseDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void ASTStmtReader::VisitOMPInterchangeDirective(OMPInterchangeDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void ASTStmtReader::VisitOMPForDirective(OMPForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPForSimdDirective(OMPForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPSectionsDirective(OMPSectionsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPSectionDirective(OMPSectionDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPScopeDirective(OMPScopeDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPSingleDirective(OMPSingleDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPMasterDirective(OMPMasterDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPCriticalDirective(OMPCriticalDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->DirName = Record.readDeclarationNameInfo();
}

void ASTStmtReader::VisitOMPParallelForDirective(OMPParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPParallelForSimdDirective(
    OMPParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPParallelMasterDirective(
    OMPParallelMasterDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPParallelMaskedDirective(
    OMPParallelMaskedDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPParallelSectionsDirective(
    OMPParallelSectionsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTaskDirective(OMPTaskDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTaskyieldDirective(OMPTaskyieldDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPBarrierDirective(OMPBarrierDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTaskwaitDirective(OMPTaskwaitDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPErrorDirective(OMPErrorDirective *D) {
  VisitStmt(D);
  // The NumClauses field was read in ReadStmtFromStream.
  Record.skipInts(1);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTaskgroupDirective(OMPTaskgroupDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPFlushDirective(OMPFlushDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPDepobjDirective(OMPDepobjDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPScanDirective(OMPScanDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPOrderedDirective(OMPOrderedDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPAtomicDirective(OMPAtomicDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->Flags.IsXLHSInRHSPart = Record.readBool() ? 1 : 0;
  D->Flags.IsPostfixUpdate = Record.readBool() ? 1 : 0;
  D->Flags.IsFailOnly = Record.readBool() ? 1 : 0;
}

void ASTStmtReader::VisitOMPTargetDirective(OMPTargetDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetDataDirective(OMPTargetDataDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetEnterDataDirective(
    OMPTargetEnterDataDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetExitDataDirective(
    OMPTargetExitDataDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetParallelDirective(
    OMPTargetParallelDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTargetParallelForDirective(
    OMPTargetParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTeamsDirective(OMPTeamsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPCancellationPointDirective(
    OMPCancellationPointDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setCancelRegion(Record.readEnum<OpenMPDirectiveKind>());
}

void ASTStmtReader::VisitOMPCancelDirective(OMPCancelDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setCancelRegion(Record.readEnum<OpenMPDirectiveKind>());
}

void ASTStmtReader::VisitOMPTaskLoopDirective(OMPTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTaskLoopSimdDirective(OMPTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPMasterTaskLoopDirective(
    OMPMasterTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPMaskedTaskLoopDirective(
    OMPMaskedTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPMasterTaskLoopSimdDirective(
    OMPMasterTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPMaskedTaskLoopSimdDirective(
    OMPMaskedTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPParallelMasterTaskLoopDirective(
    OMPParallelMasterTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPParallelMaskedTaskLoopDirective(
    OMPParallelMaskedTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPParallelMasterTaskLoopSimdDirective(
    OMPParallelMasterTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPParallelMaskedTaskLoopSimdDirective(
    OMPParallelMaskedTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPDistributeDirective(OMPDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetUpdateDirective(OMPTargetUpdateDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPDistributeParallelForDirective(
    OMPDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPDistributeParallelForSimdDirective(
    OMPDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPDistributeSimdDirective(
    OMPDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetParallelForSimdDirective(
    OMPTargetParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetSimdDirective(OMPTargetSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeDirective(
    OMPTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeSimdDirective(
    OMPTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeParallelForSimdDirective(
    OMPTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsDistributeParallelForDirective(
    OMPTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTargetTeamsDirective(OMPTargetTeamsDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeDirective(
    OMPTargetTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeParallelForDirective(
    OMPTargetTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
  D->setHasCancel(Record.readBool());
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeParallelForSimdDirective(
    OMPTargetTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsDistributeSimdDirective(
    OMPTargetTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPInteropDirective(OMPInteropDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPDispatchDirective(OMPDispatchDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
  D->setTargetCallLoc(Record.readSourceLocation());
}

void ASTStmtReader::VisitOMPMaskedDirective(OMPMaskedDirective *D) {
  VisitStmt(D);
  VisitOMPExecutableDirective(D);
}

void ASTStmtReader::VisitOMPGenericLoopDirective(OMPGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTeamsGenericLoopDirective(
    OMPTeamsGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetTeamsGenericLoopDirective(
    OMPTargetTeamsGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
  D->setCanBeParallelFor(Record.readBool());
}

void ASTStmtReader::VisitOMPParallelGenericLoopDirective(
    OMPParallelGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void ASTStmtReader::VisitOMPTargetParallelGenericLoopDirective(
    OMPTargetParallelGenericLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

//===----------------------------------------------------------------------===//
// OpenACC Constructs/Directives.
//===----------------------------------------------------------------------===//
void ASTStmtReader::VisitOpenACCConstructStmt(OpenACCConstructStmt *S) {
  (void)Record.readInt();
  S->Kind = Record.readEnum<OpenACCDirectiveKind>();
  S->Range = Record.readSourceRange();
  S->DirectiveLoc = Record.readSourceLocation();
  Record.readOpenACCClauseList(S->Clauses);
}

void ASTStmtReader::VisitOpenACCAssociatedStmtConstruct(
    OpenACCAssociatedStmtConstruct *S) {
  VisitOpenACCConstructStmt(S);
  S->setAssociatedStmt(Record.readSubStmt());
}

void ASTStmtReader::VisitOpenACCComputeConstruct(OpenACCComputeConstruct *S) {
  VisitStmt(S);
  VisitOpenACCAssociatedStmtConstruct(S);
  S->findAndSetChildLoops();
}

void ASTStmtReader::VisitOpenACCLoopConstruct(OpenACCLoopConstruct *S) {
  VisitStmt(S);
  VisitOpenACCAssociatedStmtConstruct(S);
}

//===----------------------------------------------------------------------===//
// ASTReader Implementation
//===----------------------------------------------------------------------===//

Stmt *ASTReader::ReadStmt(ModuleFile &F) {
  switch (ReadingKind) {
  case Read_None:
    llvm_unreachable("should not call this when not reading anything");
  case Read_Decl:
  case Read_Type:
    return ReadStmtFromStream(F);
  case Read_Stmt:
    return ReadSubStmt();
  }

  llvm_unreachable("ReadingKind not set ?");
}

Expr *ASTReader::ReadExpr(ModuleFile &F) {
  return cast_or_null<Expr>(ReadStmt(F));
}

Expr *ASTReader::ReadSubExpr() {
  return cast_or_null<Expr>(ReadSubStmt());
}

// Within the bitstream, expressions are stored in Reverse Polish
// Notation, with each of the subexpressions preceding the
// expression they are stored in. Subexpressions are stored from last to first.
// To evaluate expressions, we continue reading expressions and placing them on
// the stack, with expressions having operands removing those operands from the
// stack. Evaluation terminates when we see a STMT_STOP record, and
// the single remaining expression on the stack is our result.
Stmt *ASTReader::ReadStmtFromStream(ModuleFile &F) {
  ReadingKindTracker ReadingKind(Read_Stmt, *this);
  llvm::BitstreamCursor &Cursor = F.DeclsCursor;

  // Map of offset to previously deserialized stmt. The offset points
  // just after the stmt record.
  llvm::DenseMap<uint64_t, Stmt *> StmtEntries;

#ifndef NDEBUG
  unsigned PrevNumStmts = StmtStack.size();
#endif

  ASTRecordReader Record(*this, F);
  ASTStmtReader Reader(Record, Cursor);
  Stmt::EmptyShell Empty;

  while (true) {
    llvm::Expected<llvm::BitstreamEntry> MaybeEntry =
        Cursor.advanceSkippingSubblocks();
    if (!MaybeEntry) {
      Error(toString(MaybeEntry.takeError()));
      return nullptr;
    }
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      Error("malformed block record in AST file");
      return nullptr;
    case llvm::BitstreamEntry::EndBlock:
      goto Done;
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    ASTContext &Context = getContext();
    Stmt *S = nullptr;
    bool Finished = false;
    bool IsStmtReference = false;
    Expected<unsigned> MaybeStmtCode = Record.readRecord(Cursor, Entry.ID);
    if (!MaybeStmtCode) {
      Error(toString(MaybeStmtCode.takeError()));
      return nullptr;
    }
    switch ((StmtCode)MaybeStmtCode.get()) {
    case STMT_STOP:
      Finished = true;
      break;

    case STMT_REF_PTR:
      IsStmtReference = true;
      assert(StmtEntries.contains(Record[0]) &&
             "No stmt was recorded for this offset reference!");
      S = StmtEntries[Record.readInt()];
      break;

    case STMT_NULL_PTR:
      S = nullptr;
      break;

    case STMT_NULL:
      S = new (Context) NullStmt(Empty);
      break;

    case STMT_COMPOUND: {
      unsigned NumStmts = Record[ASTStmtReader::NumStmtFields];
      bool HasFPFeatures = Record[ASTStmtReader::NumStmtFields + 1];
      S = CompoundStmt::CreateEmpty(Context, NumStmts, HasFPFeatures);
      break;
    }

    case STMT_CASE:
      S = CaseStmt::CreateEmpty(
          Context,
          /*CaseStmtIsGNURange*/ Record[ASTStmtReader::NumStmtFields + 3]);
      break;

    case STMT_DEFAULT:
      S = new (Context) DefaultStmt(Empty);
      break;

    case STMT_LABEL:
      S = new (Context) LabelStmt(Empty);
      break;

    case STMT_ATTRIBUTED:
      S = AttributedStmt::CreateEmpty(
        Context,
        /*NumAttrs*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_IF: {
      BitsUnpacker IfStmtBits(Record[ASTStmtReader::NumStmtFields]);
      bool HasElse = IfStmtBits.getNextBit();
      bool HasVar = IfStmtBits.getNextBit();
      bool HasInit = IfStmtBits.getNextBit();
      S = IfStmt::CreateEmpty(Context, HasElse, HasVar, HasInit);
      break;
    }

    case STMT_SWITCH:
      S = SwitchStmt::CreateEmpty(
          Context,
          /* HasInit=*/Record[ASTStmtReader::NumStmtFields],
          /* HasVar=*/Record[ASTStmtReader::NumStmtFields + 1]);
      break;

    case STMT_WHILE:
      S = WhileStmt::CreateEmpty(
          Context,
          /* HasVar=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_DO:
      S = new (Context) DoStmt(Empty);
      break;

    case STMT_FOR:
      S = new (Context) ForStmt(Empty);
      break;

    case STMT_GOTO:
      S = new (Context) GotoStmt(Empty);
      break;

    case STMT_INDIRECT_GOTO:
      S = new (Context) IndirectGotoStmt(Empty);
      break;

    case STMT_CONTINUE:
      S = new (Context) ContinueStmt(Empty);
      break;

    case STMT_BREAK:
      S = new (Context) BreakStmt(Empty);
      break;

    case STMT_RETURN:
      S = ReturnStmt::CreateEmpty(
          Context, /* HasNRVOCandidate=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_DECL:
      S = new (Context) DeclStmt(Empty);
      break;

    case STMT_GCCASM:
      S = new (Context) GCCAsmStmt(Empty);
      break;

    case STMT_MSASM:
      S = new (Context) MSAsmStmt(Empty);
      break;

    case STMT_CAPTURED:
      S = CapturedStmt::CreateDeserialized(
          Context, Record[ASTStmtReader::NumStmtFields]);
      break;

    case EXPR_CONSTANT:
      S = ConstantExpr::CreateEmpty(
          Context, static_cast<ConstantResultStorageKind>(
                       /*StorageKind=*/Record[ASTStmtReader::NumExprFields]));
      break;

    case EXPR_SYCL_UNIQUE_STABLE_NAME:
      S = SYCLUniqueStableNameExpr::CreateEmpty(Context);
      break;

    case EXPR_PREDEFINED:
      S = PredefinedExpr::CreateEmpty(
          Context,
          /*HasFunctionName*/ Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_DECL_REF: {
      BitsUnpacker DeclRefExprBits(Record[ASTStmtReader::NumExprFields]);
      DeclRefExprBits.advance(5);
      bool HasFoundDecl = DeclRefExprBits.getNextBit();
      bool HasQualifier = DeclRefExprBits.getNextBit();
      bool HasTemplateKWAndArgsInfo = DeclRefExprBits.getNextBit();
      unsigned NumTemplateArgs = HasTemplateKWAndArgsInfo
                                     ? Record[ASTStmtReader::NumExprFields + 1]
                                     : 0;
      S = DeclRefExpr::CreateEmpty(Context, HasQualifier, HasFoundDecl,
                                   HasTemplateKWAndArgsInfo, NumTemplateArgs);
      break;
    }

    case EXPR_INTEGER_LITERAL:
      S = IntegerLiteral::Create(Context, Empty);
      break;

    case EXPR_FIXEDPOINT_LITERAL:
      S = FixedPointLiteral::Create(Context, Empty);
      break;

    case EXPR_FLOATING_LITERAL:
      S = FloatingLiteral::Create(Context, Empty);
      break;

    case EXPR_IMAGINARY_LITERAL:
      S = new (Context) ImaginaryLiteral(Empty);
      break;

    case EXPR_STRING_LITERAL:
      S = StringLiteral::CreateEmpty(
          Context,
          /* NumConcatenated=*/Record[ASTStmtReader::NumExprFields],
          /* Length=*/Record[ASTStmtReader::NumExprFields + 1],
          /* CharByteWidth=*/Record[ASTStmtReader::NumExprFields + 2]);
      break;

    case EXPR_CHARACTER_LITERAL:
      S = new (Context) CharacterLiteral(Empty);
      break;

    case EXPR_PAREN:
      S = new (Context) ParenExpr(Empty);
      break;

    case EXPR_PAREN_LIST:
      S = ParenListExpr::CreateEmpty(
          Context,
          /* NumExprs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_UNARY_OPERATOR: {
      BitsUnpacker UnaryOperatorBits(Record[ASTStmtReader::NumStmtFields]);
      UnaryOperatorBits.advance(ASTStmtReader::NumExprBits);
      bool HasFPFeatures = UnaryOperatorBits.getNextBit();
      S = UnaryOperator::CreateEmpty(Context, HasFPFeatures);
      break;
    }

    case EXPR_OFFSETOF:
      S = OffsetOfExpr::CreateEmpty(Context,
                                    Record[ASTStmtReader::NumExprFields],
                                    Record[ASTStmtReader::NumExprFields + 1]);
      break;

    case EXPR_SIZEOF_ALIGN_OF:
      S = new (Context) UnaryExprOrTypeTraitExpr(Empty);
      break;

    case EXPR_ARRAY_SUBSCRIPT:
      S = new (Context) ArraySubscriptExpr(Empty);
      break;

    case EXPR_MATRIX_SUBSCRIPT:
      S = new (Context) MatrixSubscriptExpr(Empty);
      break;

    case EXPR_ARRAY_SECTION:
      S = new (Context) ArraySectionExpr(Empty);
      break;

    case EXPR_OMP_ARRAY_SHAPING:
      S = OMPArrayShapingExpr::CreateEmpty(
          Context, Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_OMP_ITERATOR:
      S = OMPIteratorExpr::CreateEmpty(Context,
                                       Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CALL: {
      auto NumArgs = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CallExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CallExprBits.advance(1);
      auto HasFPFeatures = CallExprBits.getNextBit();
      S = CallExpr::CreateEmpty(Context, NumArgs, HasFPFeatures, Empty);
      break;
    }

    case EXPR_RECOVERY:
      S = RecoveryExpr::CreateEmpty(
          Context, /*NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_MEMBER: {
      BitsUnpacker ExprMemberBits(Record[ASTStmtReader::NumExprFields]);
      bool HasQualifier = ExprMemberBits.getNextBit();
      bool HasFoundDecl = ExprMemberBits.getNextBit();
      bool HasTemplateInfo = ExprMemberBits.getNextBit();
      unsigned NumTemplateArgs = Record[ASTStmtReader::NumExprFields + 1];
      S = MemberExpr::CreateEmpty(Context, HasQualifier, HasFoundDecl,
                                  HasTemplateInfo, NumTemplateArgs);
      break;
    }

    case EXPR_BINARY_OPERATOR: {
      BitsUnpacker BinaryOperatorBits(Record[ASTStmtReader::NumExprFields]);
      BinaryOperatorBits.advance(/*Size of opcode*/ 6);
      bool HasFPFeatures = BinaryOperatorBits.getNextBit();
      S = BinaryOperator::CreateEmpty(Context, HasFPFeatures);
      break;
    }

    case EXPR_COMPOUND_ASSIGN_OPERATOR: {
      BitsUnpacker BinaryOperatorBits(Record[ASTStmtReader::NumExprFields]);
      BinaryOperatorBits.advance(/*Size of opcode*/ 6);
      bool HasFPFeatures = BinaryOperatorBits.getNextBit();
      S = CompoundAssignOperator::CreateEmpty(Context, HasFPFeatures);
      break;
    }

    case EXPR_CONDITIONAL_OPERATOR:
      S = new (Context) ConditionalOperator(Empty);
      break;

    case EXPR_BINARY_CONDITIONAL_OPERATOR:
      S = new (Context) BinaryConditionalOperator(Empty);
      break;

    case EXPR_IMPLICIT_CAST: {
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CastExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CastExprBits.advance(7);
      bool HasFPFeatures = CastExprBits.getNextBit();
      S = ImplicitCastExpr::CreateEmpty(Context, PathSize, HasFPFeatures);
      break;
    }

    case EXPR_CSTYLE_CAST: {
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CastExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CastExprBits.advance(7);
      bool HasFPFeatures = CastExprBits.getNextBit();
      S = CStyleCastExpr::CreateEmpty(Context, PathSize, HasFPFeatures);
      break;
    }

    case EXPR_COMPOUND_LITERAL:
      S = new (Context) CompoundLiteralExpr(Empty);
      break;

    case EXPR_EXT_VECTOR_ELEMENT:
      S = new (Context) ExtVectorElementExpr(Empty);
      break;

    case EXPR_INIT_LIST:
      S = new (Context) InitListExpr(Empty);
      break;

    case EXPR_DESIGNATED_INIT:
      S = DesignatedInitExpr::CreateEmpty(Context,
                                     Record[ASTStmtReader::NumExprFields] - 1);

      break;

    case EXPR_DESIGNATED_INIT_UPDATE:
      S = new (Context) DesignatedInitUpdateExpr(Empty);
      break;

    case EXPR_IMPLICIT_VALUE_INIT:
      S = new (Context) ImplicitValueInitExpr(Empty);
      break;

    case EXPR_NO_INIT:
      S = new (Context) NoInitExpr(Empty);
      break;

    case EXPR_ARRAY_INIT_LOOP:
      S = new (Context) ArrayInitLoopExpr(Empty);
      break;

    case EXPR_ARRAY_INIT_INDEX:
      S = new (Context) ArrayInitIndexExpr(Empty);
      break;

    case EXPR_VA_ARG:
      S = new (Context) VAArgExpr(Empty);
      break;

    case EXPR_SOURCE_LOC:
      S = new (Context) SourceLocExpr(Empty);
      break;

    case EXPR_BUILTIN_PP_EMBED:
      S = new (Context) EmbedExpr(Empty);
      break;

    case EXPR_ADDR_LABEL:
      S = new (Context) AddrLabelExpr(Empty);
      break;

    case EXPR_STMT:
      S = new (Context) StmtExpr(Empty);
      break;

    case EXPR_CHOOSE:
      S = new (Context) ChooseExpr(Empty);
      break;

    case EXPR_GNU_NULL:
      S = new (Context) GNUNullExpr(Empty);
      break;

    case EXPR_SHUFFLE_VECTOR:
      S = new (Context) ShuffleVectorExpr(Empty);
      break;

    case EXPR_CONVERT_VECTOR:
      S = new (Context) ConvertVectorExpr(Empty);
      break;

    case EXPR_BLOCK:
      S = new (Context) BlockExpr(Empty);
      break;

    case EXPR_GENERIC_SELECTION:
      S = GenericSelectionExpr::CreateEmpty(
          Context,
          /*NumAssocs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_OBJC_STRING_LITERAL:
      S = new (Context) ObjCStringLiteral(Empty);
      break;

    case EXPR_OBJC_BOXED_EXPRESSION:
      S = new (Context) ObjCBoxedExpr(Empty);
      break;

    case EXPR_OBJC_ARRAY_LITERAL:
      S = ObjCArrayLiteral::CreateEmpty(Context,
                                        Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_OBJC_DICTIONARY_LITERAL:
      S = ObjCDictionaryLiteral::CreateEmpty(Context,
            Record[ASTStmtReader::NumExprFields],
            Record[ASTStmtReader::NumExprFields + 1]);
      break;

    case EXPR_OBJC_ENCODE:
      S = new (Context) ObjCEncodeExpr(Empty);
      break;

    case EXPR_OBJC_SELECTOR_EXPR:
      S = new (Context) ObjCSelectorExpr(Empty);
      break;

    case EXPR_OBJC_PROTOCOL_EXPR:
      S = new (Context) ObjCProtocolExpr(Empty);
      break;

    case EXPR_OBJC_IVAR_REF_EXPR:
      S = new (Context) ObjCIvarRefExpr(Empty);
      break;

    case EXPR_OBJC_PROPERTY_REF_EXPR:
      S = new (Context) ObjCPropertyRefExpr(Empty);
      break;

    case EXPR_OBJC_SUBSCRIPT_REF_EXPR:
      S = new (Context) ObjCSubscriptRefExpr(Empty);
      break;

    case EXPR_OBJC_KVC_REF_EXPR:
      llvm_unreachable("mismatching AST file");

    case EXPR_OBJC_MESSAGE_EXPR:
      S = ObjCMessageExpr::CreateEmpty(Context,
                                     Record[ASTStmtReader::NumExprFields],
                                     Record[ASTStmtReader::NumExprFields + 1]);
      break;

    case EXPR_OBJC_ISA:
      S = new (Context) ObjCIsaExpr(Empty);
      break;

    case EXPR_OBJC_INDIRECT_COPY_RESTORE:
      S = new (Context) ObjCIndirectCopyRestoreExpr(Empty);
      break;

    case EXPR_OBJC_BRIDGED_CAST:
      S = new (Context) ObjCBridgedCastExpr(Empty);
      break;

    case STMT_OBJC_FOR_COLLECTION:
      S = new (Context) ObjCForCollectionStmt(Empty);
      break;

    case STMT_OBJC_CATCH:
      S = new (Context) ObjCAtCatchStmt(Empty);
      break;

    case STMT_OBJC_FINALLY:
      S = new (Context) ObjCAtFinallyStmt(Empty);
      break;

    case STMT_OBJC_AT_TRY:
      S = ObjCAtTryStmt::CreateEmpty(Context,
                                     Record[ASTStmtReader::NumStmtFields],
                                     Record[ASTStmtReader::NumStmtFields + 1]);
      break;

    case STMT_OBJC_AT_SYNCHRONIZED:
      S = new (Context) ObjCAtSynchronizedStmt(Empty);
      break;

    case STMT_OBJC_AT_THROW:
      S = new (Context) ObjCAtThrowStmt(Empty);
      break;

    case STMT_OBJC_AUTORELEASE_POOL:
      S = new (Context) ObjCAutoreleasePoolStmt(Empty);
      break;

    case EXPR_OBJC_BOOL_LITERAL:
      S = new (Context) ObjCBoolLiteralExpr(Empty);
      break;

    case EXPR_OBJC_AVAILABILITY_CHECK:
      S = new (Context) ObjCAvailabilityCheckExpr(Empty);
      break;

    case STMT_SEH_LEAVE:
      S = new (Context) SEHLeaveStmt(Empty);
      break;

    case STMT_SEH_EXCEPT:
      S = new (Context) SEHExceptStmt(Empty);
      break;

    case STMT_SEH_FINALLY:
      S = new (Context) SEHFinallyStmt(Empty);
      break;

    case STMT_SEH_TRY:
      S = new (Context) SEHTryStmt(Empty);
      break;

    case STMT_CXX_CATCH:
      S = new (Context) CXXCatchStmt(Empty);
      break;

    case STMT_CXX_TRY:
      S = CXXTryStmt::Create(Context, Empty,
             /*numHandlers=*/Record[ASTStmtReader::NumStmtFields]);
      break;

    case STMT_CXX_FOR_RANGE:
      S = new (Context) CXXForRangeStmt(Empty);
      break;

    case STMT_MS_DEPENDENT_EXISTS:
      S = new (Context) MSDependentExistsStmt(SourceLocation(), true,
                                              NestedNameSpecifierLoc(),
                                              DeclarationNameInfo(),
                                              nullptr);
      break;

    case STMT_OMP_CANONICAL_LOOP:
      S = OMPCanonicalLoop::createEmpty(Context);
      break;

    case STMT_OMP_META_DIRECTIVE:
      S = OMPMetaDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_PARALLEL_DIRECTIVE:
      S =
        OMPParallelDirective::CreateEmpty(Context,
                                          Record[ASTStmtReader::NumStmtFields],
                                          Empty);
      break;

    case STMT_OMP_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPSimdDirective::CreateEmpty(Context, NumClauses,
                                        CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TILE_DIRECTIVE: {
      unsigned NumLoops = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTileDirective::CreateEmpty(Context, NumClauses, NumLoops);
      break;
    }

    case STMT_OMP_UNROLL_DIRECTIVE: {
      assert(Record[ASTStmtReader::NumStmtFields] == 1 && "Unroll directive accepts only a single loop");
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPUnrollDirective::CreateEmpty(Context, NumClauses);
      break;
    }

    case STMT_OMP_REVERSE_DIRECTIVE: {
      assert(Record[ASTStmtReader::NumStmtFields] == 1 &&
             "Reverse directive accepts only a single loop");
      assert(Record[ASTStmtReader::NumStmtFields + 1] == 0 &&
             "Reverse directive has no clauses");
      S = OMPReverseDirective::CreateEmpty(Context);
      break;
    }

    case STMT_OMP_INTERCHANGE_DIRECTIVE: {
      unsigned NumLoops = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPInterchangeDirective::CreateEmpty(Context, NumClauses, NumLoops);
      break;
    }

    case STMT_OMP_FOR_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPForDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                       Empty);
      break;
    }

    case STMT_OMP_FOR_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPForSimdDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                           Empty);
      break;
    }

    case STMT_OMP_SECTIONS_DIRECTIVE:
      S = OMPSectionsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_SECTION_DIRECTIVE:
      S = OMPSectionDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_SCOPE_DIRECTIVE:
      S = OMPScopeDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_SINGLE_DIRECTIVE:
      S = OMPSingleDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_MASTER_DIRECTIVE:
      S = OMPMasterDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_CRITICAL_DIRECTIVE:
      S = OMPCriticalDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_PARALLEL_FOR_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelForDirective::CreateEmpty(Context, NumClauses,
                                               CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelForSimdDirective::CreateEmpty(Context, NumClauses,
                                                   CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_MASTER_DIRECTIVE:
      S = OMPParallelMasterDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_PARALLEL_MASKED_DIRECTIVE:
      S = OMPParallelMaskedDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_PARALLEL_SECTIONS_DIRECTIVE:
      S = OMPParallelSectionsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASK_DIRECTIVE:
      S = OMPTaskDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASKYIELD_DIRECTIVE:
      S = OMPTaskyieldDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_BARRIER_DIRECTIVE:
      S = OMPBarrierDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_TASKWAIT_DIRECTIVE:
      S = OMPTaskwaitDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_ERROR_DIRECTIVE:
      S = OMPErrorDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASKGROUP_DIRECTIVE:
      S = OMPTaskgroupDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_FLUSH_DIRECTIVE:
      S = OMPFlushDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_DEPOBJ_DIRECTIVE:
      S = OMPDepobjDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_SCAN_DIRECTIVE:
      S = OMPScanDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_ORDERED_DIRECTIVE: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      bool HasAssociatedStmt = Record[ASTStmtReader::NumStmtFields + 2];
      S = OMPOrderedDirective::CreateEmpty(Context, NumClauses,
                                           !HasAssociatedStmt, Empty);
      break;
    }

    case STMT_OMP_ATOMIC_DIRECTIVE:
      S = OMPAtomicDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_DIRECTIVE:
      S = OMPTargetDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_DATA_DIRECTIVE:
      S = OMPTargetDataDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_ENTER_DATA_DIRECTIVE:
      S = OMPTargetEnterDataDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_EXIT_DATA_DIRECTIVE:
      S = OMPTargetExitDataDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_PARALLEL_DIRECTIVE:
      S = OMPTargetParallelDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_PARALLEL_FOR_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetParallelForDirective::CreateEmpty(Context, NumClauses,
                                                     CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_UPDATE_DIRECTIVE:
      S = OMPTargetUpdateDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TEAMS_DIRECTIVE:
      S = OMPTeamsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_CANCELLATION_POINT_DIRECTIVE:
      S = OMPCancellationPointDirective::CreateEmpty(Context, Empty);
      break;

    case STMT_OMP_CANCEL_DIRECTIVE:
      S = OMPCancelDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TASKLOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTaskLoopDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                            Empty);
      break;
    }

    case STMT_OMP_TASKLOOP_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTaskLoopSimdDirective::CreateEmpty(Context, NumClauses,
                                                CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_MASTER_TASKLOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPMasterTaskLoopDirective::CreateEmpty(Context, NumClauses,
                                                  CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_MASKED_TASKLOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPMaskedTaskLoopDirective::CreateEmpty(Context, NumClauses,
                                                  CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_MASTER_TASKLOOP_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPMasterTaskLoopSimdDirective::CreateEmpty(Context, NumClauses,
                                                      CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_MASKED_TASKLOOP_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPMaskedTaskLoopSimdDirective::CreateEmpty(Context, NumClauses,
                                                      CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_MASTER_TASKLOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelMasterTaskLoopDirective::CreateEmpty(Context, NumClauses,
                                                          CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_MASKED_TASKLOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelMaskedTaskLoopDirective::CreateEmpty(Context, NumClauses,
                                                          CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_MASTER_TASKLOOP_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelMasterTaskLoopSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_MASKED_TASKLOOP_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelMaskedTaskLoopSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                              Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeParallelForDirective::CreateEmpty(Context, NumClauses,
                                                         CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeParallelForSimdDirective::CreateEmpty(Context, NumClauses,
                                                             CollapsedNum,
                                                             Empty);
      break;
    }

    case STMT_OMP_DISTRIBUTE_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPDistributeSimdDirective::CreateEmpty(Context, NumClauses,
                                                  CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetParallelForSimdDirective::CreateEmpty(Context, NumClauses,
                                                         CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetSimdDirective::CreateEmpty(Context, NumClauses, CollapsedNum,
                                              Empty);
      break;
    }

     case STMT_OMP_TEAMS_DISTRIBUTE_DIRECTIVE: {
       unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
       unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
       S = OMPTeamsDistributeDirective::CreateEmpty(Context, NumClauses,
                                                    CollapsedNum, Empty);
       break;
    }

    case STMT_OMP_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeSimdDirective::CreateEmpty(Context, NumClauses,
                                                       CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeParallelForSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsDistributeParallelForDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DIRECTIVE:
      S = OMPTargetTeamsDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeDirective::CreateEmpty(Context, NumClauses,
                                                         CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeParallelForDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_PARALLEL_FOR_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeParallelForSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsDistributeSimdDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_INTEROP_DIRECTIVE:
      S = OMPInteropDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_DISPATCH_DIRECTIVE:
      S = OMPDispatchDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_MASKED_DIRECTIVE:
      S = OMPMaskedDirective::CreateEmpty(
          Context, Record[ASTStmtReader::NumStmtFields], Empty);
      break;

    case STMT_OMP_GENERIC_LOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPGenericLoopDirective::CreateEmpty(Context, NumClauses,
                                               CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TEAMS_GENERIC_LOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTeamsGenericLoopDirective::CreateEmpty(Context, NumClauses,
                                                    CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_TEAMS_GENERIC_LOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetTeamsGenericLoopDirective::CreateEmpty(Context, NumClauses,
                                                          CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_PARALLEL_GENERIC_LOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPParallelGenericLoopDirective::CreateEmpty(Context, NumClauses,
                                                       CollapsedNum, Empty);
      break;
    }

    case STMT_OMP_TARGET_PARALLEL_GENERIC_LOOP_DIRECTIVE: {
      unsigned CollapsedNum = Record[ASTStmtReader::NumStmtFields];
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields + 1];
      S = OMPTargetParallelGenericLoopDirective::CreateEmpty(
          Context, NumClauses, CollapsedNum, Empty);
      break;
    }

    case EXPR_CXX_OPERATOR_CALL: {
      auto NumArgs = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CallExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CallExprBits.advance(1);
      auto HasFPFeatures = CallExprBits.getNextBit();
      S = CXXOperatorCallExpr::CreateEmpty(Context, NumArgs, HasFPFeatures,
                                           Empty);
      break;
    }

    case EXPR_CXX_MEMBER_CALL: {
      auto NumArgs = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CallExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CallExprBits.advance(1);
      auto HasFPFeatures = CallExprBits.getNextBit();
      S = CXXMemberCallExpr::CreateEmpty(Context, NumArgs, HasFPFeatures,
                                         Empty);
      break;
    }

    case EXPR_CXX_REWRITTEN_BINARY_OPERATOR:
      S = new (Context) CXXRewrittenBinaryOperator(Empty);
      break;

    case EXPR_CXX_CONSTRUCT:
      S = CXXConstructExpr::CreateEmpty(
          Context,
          /* NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_INHERITED_CTOR_INIT:
      S = new (Context) CXXInheritedCtorInitExpr(Empty);
      break;

    case EXPR_CXX_TEMPORARY_OBJECT:
      S = CXXTemporaryObjectExpr::CreateEmpty(
          Context,
          /* NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_STATIC_CAST: {
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CastExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CastExprBits.advance(7);
      bool HasFPFeatures = CastExprBits.getNextBit();
      S = CXXStaticCastExpr::CreateEmpty(Context, PathSize, HasFPFeatures);
      break;
    }

    case EXPR_CXX_DYNAMIC_CAST: {
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      S = CXXDynamicCastExpr::CreateEmpty(Context, PathSize);
      break;
    }

    case EXPR_CXX_REINTERPRET_CAST: {
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      S = CXXReinterpretCastExpr::CreateEmpty(Context, PathSize);
      break;
    }

    case EXPR_CXX_CONST_CAST:
      S = CXXConstCastExpr::CreateEmpty(Context);
      break;

    case EXPR_CXX_ADDRSPACE_CAST:
      S = CXXAddrspaceCastExpr::CreateEmpty(Context);
      break;

    case EXPR_CXX_FUNCTIONAL_CAST: {
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CastExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CastExprBits.advance(7);
      bool HasFPFeatures = CastExprBits.getNextBit();
      S = CXXFunctionalCastExpr::CreateEmpty(Context, PathSize, HasFPFeatures);
      break;
    }

    case EXPR_BUILTIN_BIT_CAST: {
#ifndef NDEBUG
      unsigned PathSize = Record[ASTStmtReader::NumExprFields];
      assert(PathSize == 0 && "Wrong PathSize!");
#endif
      S = new (Context) BuiltinBitCastExpr(Empty);
      break;
    }

    case EXPR_USER_DEFINED_LITERAL: {
      auto NumArgs = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CallExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CallExprBits.advance(1);
      auto HasFPFeatures = CallExprBits.getNextBit();
      S = UserDefinedLiteral::CreateEmpty(Context, NumArgs, HasFPFeatures,
                                          Empty);
      break;
    }

    case EXPR_CXX_STD_INITIALIZER_LIST:
      S = new (Context) CXXStdInitializerListExpr(Empty);
      break;

    case EXPR_CXX_BOOL_LITERAL:
      S = new (Context) CXXBoolLiteralExpr(Empty);
      break;

    case EXPR_CXX_NULL_PTR_LITERAL:
      S = new (Context) CXXNullPtrLiteralExpr(Empty);
      break;

    case EXPR_CXX_TYPEID_EXPR:
      S = new (Context) CXXTypeidExpr(Empty, true);
      break;

    case EXPR_CXX_TYPEID_TYPE:
      S = new (Context) CXXTypeidExpr(Empty, false);
      break;

    case EXPR_CXX_UUIDOF_EXPR:
      S = new (Context) CXXUuidofExpr(Empty, true);
      break;

    case EXPR_CXX_PROPERTY_REF_EXPR:
      S = new (Context) MSPropertyRefExpr(Empty);
      break;

    case EXPR_CXX_PROPERTY_SUBSCRIPT_EXPR:
      S = new (Context) MSPropertySubscriptExpr(Empty);
      break;

    case EXPR_CXX_UUIDOF_TYPE:
      S = new (Context) CXXUuidofExpr(Empty, false);
      break;

    case EXPR_CXX_THIS:
      S = CXXThisExpr::CreateEmpty(Context);
      break;

    case EXPR_CXX_THROW:
      S = new (Context) CXXThrowExpr(Empty);
      break;

    case EXPR_CXX_DEFAULT_ARG:
      S = CXXDefaultArgExpr::CreateEmpty(
          Context, /*HasRewrittenInit=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_DEFAULT_INIT:
      S = CXXDefaultInitExpr::CreateEmpty(
          Context, /*HasRewrittenInit=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_BIND_TEMPORARY:
      S = new (Context) CXXBindTemporaryExpr(Empty);
      break;

    case EXPR_CXX_SCALAR_VALUE_INIT:
      S = new (Context) CXXScalarValueInitExpr(Empty);
      break;

    case EXPR_CXX_NEW:
      S = CXXNewExpr::CreateEmpty(
          Context,
          /*IsArray=*/Record[ASTStmtReader::NumExprFields],
          /*HasInit=*/Record[ASTStmtReader::NumExprFields + 1],
          /*NumPlacementArgs=*/Record[ASTStmtReader::NumExprFields + 2],
          /*IsParenTypeId=*/Record[ASTStmtReader::NumExprFields + 3]);
      break;

    case EXPR_CXX_DELETE:
      S = new (Context) CXXDeleteExpr(Empty);
      break;

    case EXPR_CXX_PSEUDO_DESTRUCTOR:
      S = new (Context) CXXPseudoDestructorExpr(Empty);
      break;

    case EXPR_EXPR_WITH_CLEANUPS:
      S = ExprWithCleanups::Create(Context, Empty,
                                   Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_DEPENDENT_SCOPE_MEMBER: {
      unsigned NumTemplateArgs = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker DependentScopeMemberBits(
          Record[ASTStmtReader::NumExprFields + 1]);
      bool HasTemplateKWAndArgsInfo = DependentScopeMemberBits.getNextBit();

      bool HasFirstQualifierFoundInScope =
          DependentScopeMemberBits.getNextBit();
      S = CXXDependentScopeMemberExpr::CreateEmpty(
          Context, HasTemplateKWAndArgsInfo, NumTemplateArgs,
          HasFirstQualifierFoundInScope);
      break;
    }

    case EXPR_CXX_DEPENDENT_SCOPE_DECL_REF: {
      BitsUnpacker DependentScopeDeclRefBits(
          Record[ASTStmtReader::NumStmtFields]);
      DependentScopeDeclRefBits.advance(ASTStmtReader::NumExprBits);
      bool HasTemplateKWAndArgsInfo = DependentScopeDeclRefBits.getNextBit();
      unsigned NumTemplateArgs =
          HasTemplateKWAndArgsInfo
              ? DependentScopeDeclRefBits.getNextBits(/*Width=*/16)
              : 0;
      S = DependentScopeDeclRefExpr::CreateEmpty(
          Context, HasTemplateKWAndArgsInfo, NumTemplateArgs);
      break;
    }

    case EXPR_CXX_UNRESOLVED_CONSTRUCT:
      S = CXXUnresolvedConstructExpr::CreateEmpty(Context,
                              /*NumArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_CXX_UNRESOLVED_MEMBER: {
      auto NumResults = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker OverloadExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      auto HasTemplateKWAndArgsInfo = OverloadExprBits.getNextBit();
      auto NumTemplateArgs = HasTemplateKWAndArgsInfo
                                 ? Record[ASTStmtReader::NumExprFields + 2]
                                 : 0;
      S = UnresolvedMemberExpr::CreateEmpty(
          Context, NumResults, HasTemplateKWAndArgsInfo, NumTemplateArgs);
      break;
    }

    case EXPR_CXX_UNRESOLVED_LOOKUP: {
      auto NumResults = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker OverloadExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      auto HasTemplateKWAndArgsInfo = OverloadExprBits.getNextBit();
      auto NumTemplateArgs = HasTemplateKWAndArgsInfo
                                 ? Record[ASTStmtReader::NumExprFields + 2]
                                 : 0;
      S = UnresolvedLookupExpr::CreateEmpty(
          Context, NumResults, HasTemplateKWAndArgsInfo, NumTemplateArgs);
      break;
    }

    case EXPR_TYPE_TRAIT:
      S = TypeTraitExpr::CreateDeserialized(Context,
            Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_ARRAY_TYPE_TRAIT:
      S = new (Context) ArrayTypeTraitExpr(Empty);
      break;

    case EXPR_CXX_EXPRESSION_TRAIT:
      S = new (Context) ExpressionTraitExpr(Empty);
      break;

    case EXPR_CXX_NOEXCEPT:
      S = new (Context) CXXNoexceptExpr(Empty);
      break;

    case EXPR_PACK_EXPANSION:
      S = new (Context) PackExpansionExpr(Empty);
      break;

    case EXPR_SIZEOF_PACK:
      S = SizeOfPackExpr::CreateDeserialized(
              Context,
              /*NumPartialArgs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_PACK_INDEXING:
      S = PackIndexingExpr::CreateDeserialized(
          Context,
          /*TransformedExprs=*/Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_SUBST_NON_TYPE_TEMPLATE_PARM:
      S = new (Context) SubstNonTypeTemplateParmExpr(Empty);
      break;

    case EXPR_SUBST_NON_TYPE_TEMPLATE_PARM_PACK:
      S = new (Context) SubstNonTypeTemplateParmPackExpr(Empty);
      break;

    case EXPR_FUNCTION_PARM_PACK:
      S = FunctionParmPackExpr::CreateEmpty(Context,
                                          Record[ASTStmtReader::NumExprFields]);
      break;

    case EXPR_MATERIALIZE_TEMPORARY:
      S = new (Context) MaterializeTemporaryExpr(Empty);
      break;

    case EXPR_CXX_FOLD:
      S = new (Context) CXXFoldExpr(Empty);
      break;

    case EXPR_CXX_PAREN_LIST_INIT:
      S = CXXParenListInitExpr::CreateEmpty(
          Context, /*numExprs=*/Record[ASTStmtReader::NumExprFields], Empty);
      break;

    case EXPR_OPAQUE_VALUE:
      S = new (Context) OpaqueValueExpr(Empty);
      break;

    case EXPR_CUDA_KERNEL_CALL: {
      auto NumArgs = Record[ASTStmtReader::NumExprFields];
      BitsUnpacker CallExprBits(Record[ASTStmtReader::NumExprFields + 1]);
      CallExprBits.advance(1);
      auto HasFPFeatures = CallExprBits.getNextBit();
      S = CUDAKernelCallExpr::CreateEmpty(Context, NumArgs, HasFPFeatures,
                                          Empty);
      break;
    }

    case EXPR_ASTYPE:
      S = new (Context) AsTypeExpr(Empty);
      break;

    case EXPR_PSEUDO_OBJECT: {
      unsigned numSemanticExprs = Record[ASTStmtReader::NumExprFields];
      S = PseudoObjectExpr::Create(Context, Empty, numSemanticExprs);
      break;
    }

    case EXPR_ATOMIC:
      S = new (Context) AtomicExpr(Empty);
      break;

    case EXPR_LAMBDA: {
      unsigned NumCaptures = Record[ASTStmtReader::NumExprFields];
      S = LambdaExpr::CreateDeserialized(Context, NumCaptures);
      break;
    }

    case STMT_COROUTINE_BODY: {
      unsigned NumParams = Record[ASTStmtReader::NumStmtFields];
      S = CoroutineBodyStmt::Create(Context, Empty, NumParams);
      break;
    }

    case STMT_CORETURN:
      S = new (Context) CoreturnStmt(Empty);
      break;

    case EXPR_COAWAIT:
      S = new (Context) CoawaitExpr(Empty);
      break;

    case EXPR_COYIELD:
      S = new (Context) CoyieldExpr(Empty);
      break;

    case EXPR_DEPENDENT_COAWAIT:
      S = new (Context) DependentCoawaitExpr(Empty);
      break;

    case EXPR_CONCEPT_SPECIALIZATION: {
      S = new (Context) ConceptSpecializationExpr(Empty);
      break;
    }
    case STMT_OPENACC_COMPUTE_CONSTRUCT: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      S = OpenACCComputeConstruct::CreateEmpty(Context, NumClauses);
      break;
    }
    case STMT_OPENACC_LOOP_CONSTRUCT: {
      unsigned NumClauses = Record[ASTStmtReader::NumStmtFields];
      S = OpenACCLoopConstruct::CreateEmpty(Context, NumClauses);
      break;
    }
    case EXPR_REQUIRES:
      unsigned numLocalParameters = Record[ASTStmtReader::NumExprFields];
      unsigned numRequirement = Record[ASTStmtReader::NumExprFields + 1];
      S = RequiresExpr::Create(Context, Empty, numLocalParameters,
                               numRequirement);
      break;
    }

    // We hit a STMT_STOP, so we're done with this expression.
    if (Finished)
      break;

    ++NumStatementsRead;

    if (S && !IsStmtReference) {
      Reader.Visit(S);
      StmtEntries[Cursor.GetCurrentBitNo()] = S;
    }

    assert(Record.getIdx() == Record.size() &&
           "Invalid deserialization of statement");
    StmtStack.push_back(S);
  }
Done:
  assert(StmtStack.size() > PrevNumStmts && "Read too many sub-stmts!");
  assert(StmtStack.size() == PrevNumStmts + 1 && "Extra expressions on stack!");
  return StmtStack.pop_back_val();
}
