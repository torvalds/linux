//==- ProgramPoint.cpp - Program Points for Path-Sensitive Analysis -*- C++ -*-/
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface ProgramPoint, which identifies a
//  distinct location in a function.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/ProgramPoint.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/JsonSupport.h"

using namespace clang;

ProgramPointTag::~ProgramPointTag() {}

ProgramPoint ProgramPoint::getProgramPoint(const Stmt *S, ProgramPoint::Kind K,
                                           const LocationContext *LC,
                                           const ProgramPointTag *tag){
  switch (K) {
    default:
      llvm_unreachable("Unhandled ProgramPoint kind");
    case ProgramPoint::PreStmtKind:
      return PreStmt(S, LC, tag);
    case ProgramPoint::PostStmtKind:
      return PostStmt(S, LC, tag);
    case ProgramPoint::PreLoadKind:
      return PreLoad(S, LC, tag);
    case ProgramPoint::PostLoadKind:
      return PostLoad(S, LC, tag);
    case ProgramPoint::PreStoreKind:
      return PreStore(S, LC, tag);
    case ProgramPoint::PostLValueKind:
      return PostLValue(S, LC, tag);
    case ProgramPoint::PostStmtPurgeDeadSymbolsKind:
      return PostStmtPurgeDeadSymbols(S, LC, tag);
    case ProgramPoint::PreStmtPurgeDeadSymbolsKind:
      return PreStmtPurgeDeadSymbols(S, LC, tag);
  }
}

LLVM_DUMP_METHOD void ProgramPoint::dump() const {
  return printJson(llvm::errs());
}

void ProgramPoint::printJson(llvm::raw_ostream &Out, const char *NL) const {
  const ASTContext &Context =
      getLocationContext()->getAnalysisDeclContext()->getASTContext();
  const SourceManager &SM = Context.getSourceManager();
  const PrintingPolicy &PP = Context.getPrintingPolicy();
  const bool AddQuotes = true;

  Out << "\"kind\": \"";
  switch (getKind()) {
  case ProgramPoint::BlockEntranceKind:
    Out << "BlockEntrance\""
        << ", \"block_id\": "
        << castAs<BlockEntrance>().getBlock()->getBlockID();
    break;

  case ProgramPoint::FunctionExitKind: {
    auto FEP = getAs<FunctionExitPoint>();
    Out << "FunctionExit\""
        << ", \"block_id\": " << FEP->getBlock()->getBlockID()
        << ", \"stmt_id\": ";

    if (const ReturnStmt *RS = FEP->getStmt()) {
      Out << RS->getID(Context) << ", \"stmt\": ";
      RS->printJson(Out, nullptr, PP, AddQuotes);
    } else {
      Out << "null, \"stmt\": null";
    }
    break;
  }
  case ProgramPoint::BlockExitKind:
    llvm_unreachable("BlockExitKind");
    break;
  case ProgramPoint::CallEnterKind:
    Out << "CallEnter\"";
    break;
  case ProgramPoint::CallExitBeginKind:
    Out << "CallExitBegin\"";
    break;
  case ProgramPoint::CallExitEndKind:
    Out << "CallExitEnd\"";
    break;
  case ProgramPoint::EpsilonKind:
    Out << "EpsilonPoint\"";
    break;

  case ProgramPoint::LoopExitKind:
    Out << "LoopExit\", \"stmt\": \""
        << castAs<LoopExit>().getLoopStmt()->getStmtClassName() << '\"';
    break;

  case ProgramPoint::PreImplicitCallKind: {
    ImplicitCallPoint PC = castAs<ImplicitCallPoint>();
    Out << "PreCall\", \"decl\": \""
        << PC.getDecl()->getAsFunction()->getQualifiedNameAsString()
        << "\", \"location\": ";
    printSourceLocationAsJson(Out, PC.getLocation(), SM);
    break;
  }

  case ProgramPoint::PostImplicitCallKind: {
    ImplicitCallPoint PC = castAs<ImplicitCallPoint>();
    Out << "PostCall\", \"decl\": \""
        << PC.getDecl()->getAsFunction()->getQualifiedNameAsString()
        << "\", \"location\": ";
    printSourceLocationAsJson(Out, PC.getLocation(), SM);
    break;
  }

  case ProgramPoint::PostInitializerKind: {
    Out << "PostInitializer\", ";
    const CXXCtorInitializer *Init = castAs<PostInitializer>().getInitializer();
    if (const FieldDecl *FD = Init->getAnyMember()) {
      Out << "\"field_decl\": \"" << *FD << '\"';
    } else {
      Out << "\"type\": \"";
      QualType Ty = Init->getTypeSourceInfo()->getType();
      Ty = Ty.getLocalUnqualifiedType();
      Ty.print(Out, Context.getLangOpts());
      Out << '\"';
    }
    break;
  }

  case ProgramPoint::BlockEdgeKind: {
    const BlockEdge &E = castAs<BlockEdge>();
    const Stmt *T = E.getSrc()->getTerminatorStmt();
    Out << "Edge\", \"src_id\": " << E.getSrc()->getBlockID()
        << ", \"dst_id\": " << E.getDst()->getBlockID() << ", \"terminator\": ";

    if (!T) {
      Out << "null, \"term_kind\": null";
      break;
    }

    E.getSrc()->printTerminatorJson(Out, Context.getLangOpts(),
                                    /*AddQuotes=*/true);
    Out << ", \"location\": ";
    printSourceLocationAsJson(Out, T->getBeginLoc(), SM);

    Out << ", \"term_kind\": \"";
    if (isa<SwitchStmt>(T)) {
      Out << "SwitchStmt\", \"case\": ";
      if (const Stmt *Label = E.getDst()->getLabel()) {
        if (const auto *C = dyn_cast<CaseStmt>(Label)) {
          Out << "{ \"lhs\": ";
          if (const Stmt *LHS = C->getLHS()) {
            LHS->printJson(Out, nullptr, PP, AddQuotes);
          } else {
            Out << "null";
	  }

          Out << ", \"rhs\": ";
          if (const Stmt *RHS = C->getRHS()) {
            RHS->printJson(Out, nullptr, PP, AddQuotes);
          } else {
            Out << "null";
          }
          Out << " }";
        } else {
          assert(isa<DefaultStmt>(Label));
          Out << "\"default\"";
        }
      } else {
        Out << "\"implicit default\"";
      }
    } else if (isa<IndirectGotoStmt>(T)) {
      // FIXME: More info.
      Out << "IndirectGotoStmt\"";
    } else {
      Out << "Condition\", \"value\": "
          << (*E.getSrc()->succ_begin() == E.getDst() ? "true" : "false");
    }
    break;
  }

  default: {
    const Stmt *S = castAs<StmtPoint>().getStmt();
    assert(S != nullptr && "Expecting non-null Stmt");

    Out << "Statement\", \"stmt_kind\": \"" << S->getStmtClassName()
        << "\", \"stmt_id\": " << S->getID(Context)
        << ", \"pointer\": \"" << (const void *)S << "\", ";
    if (const auto *CS = dyn_cast<CastExpr>(S))
      Out << "\"cast_kind\": \"" << CS->getCastKindName() << "\", ";

    Out << "\"pretty\": ";

    S->printJson(Out, nullptr, PP, AddQuotes);

    Out << ", \"location\": ";
    printSourceLocationAsJson(Out, S->getBeginLoc(), SM);

    Out << ", \"stmt_point_kind\": \"";
    if (getAs<PreLoad>())
      Out << "PreLoad";
    else if (getAs<PreStore>())
      Out << "PreStore";
    else if (getAs<PostAllocatorCall>())
      Out << "PostAllocatorCall";
    else if (getAs<PostCondition>())
      Out << "PostCondition";
    else if (getAs<PostLoad>())
      Out << "PostLoad";
    else if (getAs<PostLValue>())
      Out << "PostLValue";
    else if (getAs<PostStore>())
      Out << "PostStore";
    else if (getAs<PostStmt>())
      Out << "PostStmt";
    else if (getAs<PostStmtPurgeDeadSymbols>())
      Out << "PostStmtPurgeDeadSymbols";
    else if (getAs<PreStmtPurgeDeadSymbols>())
      Out << "PreStmtPurgeDeadSymbols";
    else if (getAs<PreStmt>())
      Out << "PreStmt";
    else {
      Out << "\nKind: '" << getKind();
      llvm_unreachable("' is unhandled StmtPoint kind!");
    }

    Out << '\"';
    break;
  }
  }
}

SimpleProgramPointTag::SimpleProgramPointTag(StringRef MsgProvider,
                                             StringRef Msg)
  : Desc((MsgProvider + " : " + Msg).str()) {}

StringRef SimpleProgramPointTag::getTagDescription() const {
  return Desc;
}
