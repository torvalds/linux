//===- CIndex.cpp - Clang-C Source Indexing Library -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the main API hooks in the Clang-C Source Indexing
// library.
//
//===----------------------------------------------------------------------===//

#include "CIndexDiagnostic.h"
#include "CIndexer.h"
#include "CLog.h"
#include "CXCursor.h"
#include "CXFile.h"
#include "CXSourceLocation.h"
#include "CXString.h"
#include "CXTranslationUnit.h"
#include "CXType.h"
#include "CursorVisitor.h"
#include "clang-c/FatalErrorHandler.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrVisitor.h"
#include "clang/AST/DeclObjCCommon.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/OpenACCClause.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticCategories.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Index/CommentToXML.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"
#include <mutex>
#include <optional>

#if LLVM_ENABLE_THREADS != 0 && defined(__APPLE__)
#define USE_DARWIN_THREADS
#endif

#ifdef USE_DARWIN_THREADS
#include <pthread.h>
#endif

using namespace clang;
using namespace clang::cxcursor;
using namespace clang::cxtu;
using namespace clang::cxindex;

CXTranslationUnit cxtu::MakeCXTranslationUnit(CIndexer *CIdx,
                                              std::unique_ptr<ASTUnit> AU) {
  if (!AU)
    return nullptr;
  assert(CIdx);
  CXTranslationUnit D = new CXTranslationUnitImpl();
  D->CIdx = CIdx;
  D->TheASTUnit = AU.release();
  D->StringPool = new cxstring::CXStringPool();
  D->Diagnostics = nullptr;
  D->OverridenCursorsPool = createOverridenCXCursorsPool();
  D->CommentToXML = nullptr;
  D->ParsingOptions = 0;
  D->Arguments = {};
  return D;
}

bool cxtu::isASTReadError(ASTUnit *AU) {
  for (ASTUnit::stored_diag_iterator D = AU->stored_diag_begin(),
                                     DEnd = AU->stored_diag_end();
       D != DEnd; ++D) {
    if (D->getLevel() >= DiagnosticsEngine::Error &&
        DiagnosticIDs::getCategoryNumberForDiag(D->getID()) ==
            diag::DiagCat_AST_Deserialization_Issue)
      return true;
  }
  return false;
}

cxtu::CXTUOwner::~CXTUOwner() {
  if (TU)
    clang_disposeTranslationUnit(TU);
}

/// Compare two source ranges to determine their relative position in
/// the translation unit.
static RangeComparisonResult RangeCompare(SourceManager &SM, SourceRange R1,
                                          SourceRange R2) {
  assert(R1.isValid() && "First range is invalid?");
  assert(R2.isValid() && "Second range is invalid?");
  if (R1.getEnd() != R2.getBegin() &&
      SM.isBeforeInTranslationUnit(R1.getEnd(), R2.getBegin()))
    return RangeBefore;
  if (R2.getEnd() != R1.getBegin() &&
      SM.isBeforeInTranslationUnit(R2.getEnd(), R1.getBegin()))
    return RangeAfter;
  return RangeOverlap;
}

/// Determine if a source location falls within, before, or after a
///   a given source range.
static RangeComparisonResult LocationCompare(SourceManager &SM,
                                             SourceLocation L, SourceRange R) {
  assert(R.isValid() && "First range is invalid?");
  assert(L.isValid() && "Second range is invalid?");
  if (L == R.getBegin() || L == R.getEnd())
    return RangeOverlap;
  if (SM.isBeforeInTranslationUnit(L, R.getBegin()))
    return RangeBefore;
  if (SM.isBeforeInTranslationUnit(R.getEnd(), L))
    return RangeAfter;
  return RangeOverlap;
}

/// Translate a Clang source range into a CIndex source range.
///
/// Clang internally represents ranges where the end location points to the
/// start of the token at the end. However, for external clients it is more
/// useful to have a CXSourceRange be a proper half-open interval. This routine
/// does the appropriate translation.
CXSourceRange cxloc::translateSourceRange(const SourceManager &SM,
                                          const LangOptions &LangOpts,
                                          const CharSourceRange &R) {
  // We want the last character in this location, so we will adjust the
  // location accordingly.
  SourceLocation EndLoc = R.getEnd();
  bool IsTokenRange = R.isTokenRange();
  if (EndLoc.isValid() && EndLoc.isMacroID() &&
      !SM.isMacroArgExpansion(EndLoc)) {
    CharSourceRange Expansion = SM.getExpansionRange(EndLoc);
    EndLoc = Expansion.getEnd();
    IsTokenRange = Expansion.isTokenRange();
  }
  if (IsTokenRange && EndLoc.isValid()) {
    unsigned Length =
        Lexer::MeasureTokenLength(SM.getSpellingLoc(EndLoc), SM, LangOpts);
    EndLoc = EndLoc.getLocWithOffset(Length);
  }

  CXSourceRange Result = {
      {&SM, &LangOpts}, R.getBegin().getRawEncoding(), EndLoc.getRawEncoding()};
  return Result;
}

CharSourceRange cxloc::translateCXRangeToCharRange(CXSourceRange R) {
  return CharSourceRange::getCharRange(
      SourceLocation::getFromRawEncoding(R.begin_int_data),
      SourceLocation::getFromRawEncoding(R.end_int_data));
}

//===----------------------------------------------------------------------===//
// Cursor visitor.
//===----------------------------------------------------------------------===//

static SourceRange getRawCursorExtent(CXCursor C);
static SourceRange getFullCursorExtent(CXCursor C, SourceManager &SrcMgr);

RangeComparisonResult CursorVisitor::CompareRegionOfInterest(SourceRange R) {
  return RangeCompare(AU->getSourceManager(), R, RegionOfInterest);
}

/// Visit the given cursor and, if requested by the visitor,
/// its children.
///
/// \param Cursor the cursor to visit.
///
/// \param CheckedRegionOfInterest if true, then the caller already checked
/// that this cursor is within the region of interest.
///
/// \returns true if the visitation should be aborted, false if it
/// should continue.
bool CursorVisitor::Visit(CXCursor Cursor, bool CheckedRegionOfInterest) {
  if (clang_isInvalid(Cursor.kind))
    return false;

  if (clang_isDeclaration(Cursor.kind)) {
    const Decl *D = getCursorDecl(Cursor);
    if (!D) {
      assert(0 && "Invalid declaration cursor");
      return true; // abort.
    }

    // Ignore implicit declarations, unless it's an objc method because
    // currently we should report implicit methods for properties when indexing.
    if (D->isImplicit() && !isa<ObjCMethodDecl>(D))
      return false;
  }

  // If we have a range of interest, and this cursor doesn't intersect with it,
  // we're done.
  if (RegionOfInterest.isValid() && !CheckedRegionOfInterest) {
    SourceRange Range = getRawCursorExtent(Cursor);
    if (Range.isInvalid() || CompareRegionOfInterest(Range))
      return false;
  }

  switch (Visitor(Cursor, Parent, ClientData)) {
  case CXChildVisit_Break:
    return true;

  case CXChildVisit_Continue:
    return false;

  case CXChildVisit_Recurse: {
    bool ret = VisitChildren(Cursor);
    if (PostChildrenVisitor)
      if (PostChildrenVisitor(Cursor, ClientData))
        return true;
    return ret;
  }
  }

  llvm_unreachable("Invalid CXChildVisitResult!");
}

static bool visitPreprocessedEntitiesInRange(SourceRange R,
                                             PreprocessingRecord &PPRec,
                                             CursorVisitor &Visitor) {
  SourceManager &SM = Visitor.getASTUnit()->getSourceManager();
  FileID FID;

  if (!Visitor.shouldVisitIncludedEntities()) {
    // If the begin/end of the range lie in the same FileID, do the optimization
    // where we skip preprocessed entities that do not come from the same
    // FileID.
    FID = SM.getFileID(SM.getFileLoc(R.getBegin()));
    if (FID != SM.getFileID(SM.getFileLoc(R.getEnd())))
      FID = FileID();
  }

  const auto &Entities = PPRec.getPreprocessedEntitiesInRange(R);
  return Visitor.visitPreprocessedEntities(Entities.begin(), Entities.end(),
                                           PPRec, FID);
}

bool CursorVisitor::visitFileRegion() {
  if (RegionOfInterest.isInvalid())
    return false;

  ASTUnit *Unit = cxtu::getASTUnit(TU);
  SourceManager &SM = Unit->getSourceManager();

  std::pair<FileID, unsigned> Begin = SM.getDecomposedLoc(
                                  SM.getFileLoc(RegionOfInterest.getBegin())),
                              End = SM.getDecomposedLoc(
                                  SM.getFileLoc(RegionOfInterest.getEnd()));

  if (End.first != Begin.first) {
    // If the end does not reside in the same file, try to recover by
    // picking the end of the file of begin location.
    End.first = Begin.first;
    End.second = SM.getFileIDSize(Begin.first);
  }

  assert(Begin.first == End.first);
  if (Begin.second > End.second)
    return false;

  FileID File = Begin.first;
  unsigned Offset = Begin.second;
  unsigned Length = End.second - Begin.second;

  if (!VisitDeclsOnly && !VisitPreprocessorLast)
    if (visitPreprocessedEntitiesInRegion())
      return true; // visitation break.

  if (visitDeclsFromFileRegion(File, Offset, Length))
    return true; // visitation break.

  if (!VisitDeclsOnly && VisitPreprocessorLast)
    return visitPreprocessedEntitiesInRegion();

  return false;
}

static bool isInLexicalContext(Decl *D, DeclContext *DC) {
  if (!DC)
    return false;

  for (DeclContext *DeclDC = D->getLexicalDeclContext(); DeclDC;
       DeclDC = DeclDC->getLexicalParent()) {
    if (DeclDC == DC)
      return true;
  }
  return false;
}

bool CursorVisitor::visitDeclsFromFileRegion(FileID File, unsigned Offset,
                                             unsigned Length) {
  ASTUnit *Unit = cxtu::getASTUnit(TU);
  SourceManager &SM = Unit->getSourceManager();
  SourceRange Range = RegionOfInterest;

  SmallVector<Decl *, 16> Decls;
  Unit->findFileRegionDecls(File, Offset, Length, Decls);

  // If we didn't find any file level decls for the file, try looking at the
  // file that it was included from.
  while (Decls.empty() || Decls.front()->isTopLevelDeclInObjCContainer()) {
    bool Invalid = false;
    const SrcMgr::SLocEntry &SLEntry = SM.getSLocEntry(File, &Invalid);
    if (Invalid)
      return false;

    SourceLocation Outer;
    if (SLEntry.isFile())
      Outer = SLEntry.getFile().getIncludeLoc();
    else
      Outer = SLEntry.getExpansion().getExpansionLocStart();
    if (Outer.isInvalid())
      return false;

    std::tie(File, Offset) = SM.getDecomposedExpansionLoc(Outer);
    Length = 0;
    Unit->findFileRegionDecls(File, Offset, Length, Decls);
  }

  assert(!Decls.empty());

  bool VisitedAtLeastOnce = false;
  DeclContext *CurDC = nullptr;
  SmallVectorImpl<Decl *>::iterator DIt = Decls.begin();
  for (SmallVectorImpl<Decl *>::iterator DE = Decls.end(); DIt != DE; ++DIt) {
    Decl *D = *DIt;
    if (D->getSourceRange().isInvalid())
      continue;

    if (isInLexicalContext(D, CurDC))
      continue;

    CurDC = dyn_cast<DeclContext>(D);

    if (TagDecl *TD = dyn_cast<TagDecl>(D))
      if (!TD->isFreeStanding())
        continue;

    RangeComparisonResult CompRes =
        RangeCompare(SM, D->getSourceRange(), Range);
    if (CompRes == RangeBefore)
      continue;
    if (CompRes == RangeAfter)
      break;

    assert(CompRes == RangeOverlap);
    VisitedAtLeastOnce = true;

    if (isa<ObjCContainerDecl>(D)) {
      FileDI_current = &DIt;
      FileDE_current = DE;
    } else {
      FileDI_current = nullptr;
    }

    if (Visit(MakeCXCursor(D, TU, Range), /*CheckedRegionOfInterest=*/true))
      return true; // visitation break.
  }

  if (VisitedAtLeastOnce)
    return false;

  // No Decls overlapped with the range. Move up the lexical context until there
  // is a context that contains the range or we reach the translation unit
  // level.
  DeclContext *DC = DIt == Decls.begin()
                        ? (*DIt)->getLexicalDeclContext()
                        : (*(DIt - 1))->getLexicalDeclContext();

  while (DC && !DC->isTranslationUnit()) {
    Decl *D = cast<Decl>(DC);
    SourceRange CurDeclRange = D->getSourceRange();
    if (CurDeclRange.isInvalid())
      break;

    if (RangeCompare(SM, CurDeclRange, Range) == RangeOverlap) {
      if (Visit(MakeCXCursor(D, TU, Range), /*CheckedRegionOfInterest=*/true))
        return true; // visitation break.
    }

    DC = D->getLexicalDeclContext();
  }

  return false;
}

bool CursorVisitor::visitPreprocessedEntitiesInRegion() {
  if (!AU->getPreprocessor().getPreprocessingRecord())
    return false;

  PreprocessingRecord &PPRec = *AU->getPreprocessor().getPreprocessingRecord();
  SourceManager &SM = AU->getSourceManager();

  if (RegionOfInterest.isValid()) {
    SourceRange MappedRange = AU->mapRangeToPreamble(RegionOfInterest);
    SourceLocation B = MappedRange.getBegin();
    SourceLocation E = MappedRange.getEnd();

    if (AU->isInPreambleFileID(B)) {
      if (SM.isLoadedSourceLocation(E))
        return visitPreprocessedEntitiesInRange(SourceRange(B, E), PPRec,
                                                *this);

      // Beginning of range lies in the preamble but it also extends beyond
      // it into the main file. Split the range into 2 parts, one covering
      // the preamble and another covering the main file. This allows subsequent
      // calls to visitPreprocessedEntitiesInRange to accept a source range that
      // lies in the same FileID, allowing it to skip preprocessed entities that
      // do not come from the same FileID.
      bool breaked = visitPreprocessedEntitiesInRange(
          SourceRange(B, AU->getEndOfPreambleFileID()), PPRec, *this);
      if (breaked)
        return true;
      return visitPreprocessedEntitiesInRange(
          SourceRange(AU->getStartOfMainFileID(), E), PPRec, *this);
    }

    return visitPreprocessedEntitiesInRange(SourceRange(B, E), PPRec, *this);
  }

  bool OnlyLocalDecls = !AU->isMainFileAST() && AU->getOnlyLocalDecls();

  if (OnlyLocalDecls)
    return visitPreprocessedEntities(PPRec.local_begin(), PPRec.local_end(),
                                     PPRec);

  return visitPreprocessedEntities(PPRec.begin(), PPRec.end(), PPRec);
}

template <typename InputIterator>
bool CursorVisitor::visitPreprocessedEntities(InputIterator First,
                                              InputIterator Last,
                                              PreprocessingRecord &PPRec,
                                              FileID FID) {
  for (; First != Last; ++First) {
    if (!FID.isInvalid() && !PPRec.isEntityInFileID(First, FID))
      continue;

    PreprocessedEntity *PPE = *First;
    if (!PPE)
      continue;

    if (MacroExpansion *ME = dyn_cast<MacroExpansion>(PPE)) {
      if (Visit(MakeMacroExpansionCursor(ME, TU)))
        return true;

      continue;
    }

    if (MacroDefinitionRecord *MD = dyn_cast<MacroDefinitionRecord>(PPE)) {
      if (Visit(MakeMacroDefinitionCursor(MD, TU)))
        return true;

      continue;
    }

    if (InclusionDirective *ID = dyn_cast<InclusionDirective>(PPE)) {
      if (Visit(MakeInclusionDirectiveCursor(ID, TU)))
        return true;

      continue;
    }
  }

  return false;
}

/// Visit the children of the given cursor.
///
/// \returns true if the visitation should be aborted, false if it
/// should continue.
bool CursorVisitor::VisitChildren(CXCursor Cursor) {
  if (clang_isReference(Cursor.kind) &&
      Cursor.kind != CXCursor_CXXBaseSpecifier) {
    // By definition, references have no children.
    return false;
  }

  // Set the Parent field to Cursor, then back to its old value once we're
  // done.
  SetParentRAII SetParent(Parent, StmtParent, Cursor);

  if (clang_isDeclaration(Cursor.kind)) {
    Decl *D = const_cast<Decl *>(getCursorDecl(Cursor));
    if (!D)
      return false;

    return VisitAttributes(D) || Visit(D);
  }

  if (clang_isStatement(Cursor.kind)) {
    if (const Stmt *S = getCursorStmt(Cursor))
      return Visit(S);

    return false;
  }

  if (clang_isExpression(Cursor.kind)) {
    if (const Expr *E = getCursorExpr(Cursor))
      return Visit(E);

    return false;
  }

  if (clang_isTranslationUnit(Cursor.kind)) {
    CXTranslationUnit TU = getCursorTU(Cursor);
    ASTUnit *CXXUnit = cxtu::getASTUnit(TU);

    int VisitOrder[2] = {VisitPreprocessorLast, !VisitPreprocessorLast};
    for (unsigned I = 0; I != 2; ++I) {
      if (VisitOrder[I]) {
        if (!CXXUnit->isMainFileAST() && CXXUnit->getOnlyLocalDecls() &&
            RegionOfInterest.isInvalid()) {
          for (ASTUnit::top_level_iterator TL = CXXUnit->top_level_begin(),
                                           TLEnd = CXXUnit->top_level_end();
               TL != TLEnd; ++TL) {
            const std::optional<bool> V = handleDeclForVisitation(*TL);
            if (!V)
              continue;
            return *V;
          }
        } else if (VisitDeclContext(
                       CXXUnit->getASTContext().getTranslationUnitDecl()))
          return true;
        continue;
      }

      // Walk the preprocessing record.
      if (CXXUnit->getPreprocessor().getPreprocessingRecord())
        visitPreprocessedEntitiesInRegion();
    }

    return false;
  }

  if (Cursor.kind == CXCursor_CXXBaseSpecifier) {
    if (const CXXBaseSpecifier *Base = getCursorCXXBaseSpecifier(Cursor)) {
      if (TypeSourceInfo *BaseTSInfo = Base->getTypeSourceInfo()) {
        return Visit(BaseTSInfo->getTypeLoc());
      }
    }
  }

  if (Cursor.kind == CXCursor_IBOutletCollectionAttr) {
    const IBOutletCollectionAttr *A =
        cast<IBOutletCollectionAttr>(cxcursor::getCursorAttr(Cursor));
    if (const ObjCObjectType *ObjT = A->getInterface()->getAs<ObjCObjectType>())
      return Visit(cxcursor::MakeCursorObjCClassRef(
          ObjT->getInterface(),
          A->getInterfaceLoc()->getTypeLoc().getBeginLoc(), TU));
  }

  if (clang_isAttribute(Cursor.kind)) {
    if (const Attr *A = getCursorAttr(Cursor))
      return Visit(A);

    return false;
  }

  // If pointing inside a macro definition, check if the token is an identifier
  // that was ever defined as a macro. In such a case, create a "pseudo" macro
  // expansion cursor for that token.
  SourceLocation BeginLoc = RegionOfInterest.getBegin();
  if (Cursor.kind == CXCursor_MacroDefinition &&
      BeginLoc == RegionOfInterest.getEnd()) {
    SourceLocation Loc = AU->mapLocationToPreamble(BeginLoc);
    const MacroInfo *MI =
        getMacroInfo(cxcursor::getCursorMacroDefinition(Cursor), TU);
    if (MacroDefinitionRecord *MacroDef =
            checkForMacroInMacroDefinition(MI, Loc, TU))
      return Visit(cxcursor::MakeMacroExpansionCursor(MacroDef, BeginLoc, TU));
  }

  // Nothing to visit at the moment.
  return false;
}

bool CursorVisitor::VisitBlockDecl(BlockDecl *B) {
  if (TypeSourceInfo *TSInfo = B->getSignatureAsWritten())
    if (Visit(TSInfo->getTypeLoc()))
      return true;

  if (Stmt *Body = B->getBody())
    return Visit(MakeCXCursor(Body, StmtParent, TU, RegionOfInterest));

  return false;
}

std::optional<bool> CursorVisitor::shouldVisitCursor(CXCursor Cursor) {
  if (RegionOfInterest.isValid()) {
    SourceRange Range = getFullCursorExtent(Cursor, AU->getSourceManager());
    if (Range.isInvalid())
      return std::nullopt;

    switch (CompareRegionOfInterest(Range)) {
    case RangeBefore:
      // This declaration comes before the region of interest; skip it.
      return std::nullopt;

    case RangeAfter:
      // This declaration comes after the region of interest; we're done.
      return false;

    case RangeOverlap:
      // This declaration overlaps the region of interest; visit it.
      break;
    }
  }
  return true;
}

bool CursorVisitor::VisitDeclContext(DeclContext *DC) {
  DeclContext::decl_iterator I = DC->decls_begin(), E = DC->decls_end();

  // FIXME: Eventually remove.  This part of a hack to support proper
  // iteration over all Decls contained lexically within an ObjC container.
  SaveAndRestore DI_saved(DI_current, &I);
  SaveAndRestore DE_saved(DE_current, E);

  for (; I != E; ++I) {
    Decl *D = *I;
    if (D->getLexicalDeclContext() != DC)
      continue;
    // Filter out synthesized property accessor redeclarations.
    if (isa<ObjCImplDecl>(DC))
      if (auto *OMD = dyn_cast<ObjCMethodDecl>(D))
        if (OMD->isSynthesizedAccessorStub())
          continue;
    const std::optional<bool> V = handleDeclForVisitation(D);
    if (!V)
      continue;
    return *V;
  }
  return false;
}

std::optional<bool> CursorVisitor::handleDeclForVisitation(const Decl *D) {
  CXCursor Cursor = MakeCXCursor(D, TU, RegionOfInterest);

  // Ignore synthesized ivars here, otherwise if we have something like:
  //   @synthesize prop = _prop;
  // and '_prop' is not declared, we will encounter a '_prop' ivar before
  // encountering the 'prop' synthesize declaration and we will think that
  // we passed the region-of-interest.
  if (auto *ivarD = dyn_cast<ObjCIvarDecl>(D)) {
    if (ivarD->getSynthesize())
      return std::nullopt;
  }

  // FIXME: ObjCClassRef/ObjCProtocolRef for forward class/protocol
  // declarations is a mismatch with the compiler semantics.
  if (Cursor.kind == CXCursor_ObjCInterfaceDecl) {
    auto *ID = cast<ObjCInterfaceDecl>(D);
    if (!ID->isThisDeclarationADefinition())
      Cursor = MakeCursorObjCClassRef(ID, ID->getLocation(), TU);

  } else if (Cursor.kind == CXCursor_ObjCProtocolDecl) {
    auto *PD = cast<ObjCProtocolDecl>(D);
    if (!PD->isThisDeclarationADefinition())
      Cursor = MakeCursorObjCProtocolRef(PD, PD->getLocation(), TU);
  }

  const std::optional<bool> V = shouldVisitCursor(Cursor);
  if (!V)
    return std::nullopt;
  if (!*V)
    return false;
  if (Visit(Cursor, true))
    return true;
  return std::nullopt;
}

bool CursorVisitor::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  llvm_unreachable("Translation units are visited directly by Visit()");
}

bool CursorVisitor::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  if (VisitTemplateParameters(D->getTemplateParameters()))
    return true;

  return Visit(MakeCXCursor(D->getTemplatedDecl(), TU, RegionOfInterest));
}

bool CursorVisitor::VisitTypeAliasDecl(TypeAliasDecl *D) {
  if (TypeSourceInfo *TSInfo = D->getTypeSourceInfo())
    return Visit(TSInfo->getTypeLoc());

  return false;
}

bool CursorVisitor::VisitTypedefDecl(TypedefDecl *D) {
  if (TypeSourceInfo *TSInfo = D->getTypeSourceInfo())
    return Visit(TSInfo->getTypeLoc());

  return false;
}

bool CursorVisitor::VisitTagDecl(TagDecl *D) { return VisitDeclContext(D); }

bool CursorVisitor::VisitClassTemplateSpecializationDecl(
    ClassTemplateSpecializationDecl *D) {
  bool ShouldVisitBody = false;
  switch (D->getSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ImplicitInstantiation:
    // Nothing to visit
    return false;

  case TSK_ExplicitInstantiationDeclaration:
  case TSK_ExplicitInstantiationDefinition:
    break;

  case TSK_ExplicitSpecialization:
    ShouldVisitBody = true;
    break;
  }

  // Visit the template arguments used in the specialization.
  if (const auto *ArgsWritten = D->getTemplateArgsAsWritten()) {
    for (const TemplateArgumentLoc &Arg : ArgsWritten->arguments())
      if (VisitTemplateArgumentLoc(Arg))
        return true;
  }

  return ShouldVisitBody && VisitCXXRecordDecl(D);
}

bool CursorVisitor::VisitClassTemplatePartialSpecializationDecl(
    ClassTemplatePartialSpecializationDecl *D) {
  // FIXME: Visit the "outer" template parameter lists on the TagDecl
  // before visiting these template parameters.
  if (VisitTemplateParameters(D->getTemplateParameters()))
    return true;

  // Visit the partial specialization arguments.
  const ASTTemplateArgumentListInfo *Info = D->getTemplateArgsAsWritten();
  const TemplateArgumentLoc *TemplateArgs = Info->getTemplateArgs();
  for (unsigned I = 0, N = Info->NumTemplateArgs; I != N; ++I)
    if (VisitTemplateArgumentLoc(TemplateArgs[I]))
      return true;

  return VisitCXXRecordDecl(D);
}

bool CursorVisitor::VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D) {
  if (const auto *TC = D->getTypeConstraint()) {
    if (VisitTypeConstraint(*TC))
      return true;
  }

  // Visit the default argument.
  if (D->hasDefaultArgument() && !D->defaultArgumentWasInherited() &&
      VisitTemplateArgumentLoc(D->getDefaultArgument()))
    return true;

  return false;
}

bool CursorVisitor::VisitEnumConstantDecl(EnumConstantDecl *D) {
  if (Expr *Init = D->getInitExpr())
    return Visit(MakeCXCursor(Init, StmtParent, TU, RegionOfInterest));
  return false;
}

bool CursorVisitor::VisitDeclaratorDecl(DeclaratorDecl *DD) {
  unsigned NumParamList = DD->getNumTemplateParameterLists();
  for (unsigned i = 0; i < NumParamList; i++) {
    TemplateParameterList *Params = DD->getTemplateParameterList(i);
    if (VisitTemplateParameters(Params))
      return true;
  }

  if (TypeSourceInfo *TSInfo = DD->getTypeSourceInfo())
    if (Visit(TSInfo->getTypeLoc()))
      return true;

  // Visit the nested-name-specifier, if present.
  if (NestedNameSpecifierLoc QualifierLoc = DD->getQualifierLoc())
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;

  return false;
}

static bool HasTrailingReturnType(FunctionDecl *ND) {
  const QualType Ty = ND->getType();
  if (const FunctionType *AFT = Ty->getAs<FunctionType>()) {
    if (const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(AFT))
      return FT->hasTrailingReturn();
  }

  return false;
}

/// Compare two base or member initializers based on their source order.
static int CompareCXXCtorInitializers(CXXCtorInitializer *const *X,
                                      CXXCtorInitializer *const *Y) {
  return (*X)->getSourceOrder() - (*Y)->getSourceOrder();
}

bool CursorVisitor::VisitFunctionDecl(FunctionDecl *ND) {
  unsigned NumParamList = ND->getNumTemplateParameterLists();
  for (unsigned i = 0; i < NumParamList; i++) {
    TemplateParameterList *Params = ND->getTemplateParameterList(i);
    if (VisitTemplateParameters(Params))
      return true;
  }

  if (TypeSourceInfo *TSInfo = ND->getTypeSourceInfo()) {
    // Visit the function declaration's syntactic components in the order
    // written. This requires a bit of work.
    TypeLoc TL = TSInfo->getTypeLoc().IgnoreParens();
    FunctionTypeLoc FTL = TL.getAs<FunctionTypeLoc>();
    const bool HasTrailingRT = HasTrailingReturnType(ND);

    // If we have a function declared directly (without the use of a typedef),
    // visit just the return type. Otherwise, just visit the function's type
    // now.
    if ((FTL && !isa<CXXConversionDecl>(ND) && !HasTrailingRT &&
         Visit(FTL.getReturnLoc())) ||
        (!FTL && Visit(TL)))
      return true;

    // Visit the nested-name-specifier, if present.
    if (NestedNameSpecifierLoc QualifierLoc = ND->getQualifierLoc())
      if (VisitNestedNameSpecifierLoc(QualifierLoc))
        return true;

    // Visit the declaration name.
    if (!isa<CXXDestructorDecl>(ND))
      if (VisitDeclarationNameInfo(ND->getNameInfo()))
        return true;

    // FIXME: Visit explicitly-specified template arguments!

    // Visit the function parameters, if we have a function type.
    if (FTL && VisitFunctionTypeLoc(FTL, true))
      return true;

    // Visit the function's trailing return type.
    if (FTL && HasTrailingRT && Visit(FTL.getReturnLoc()))
      return true;

    // FIXME: Attributes?
  }

  if (auto *E = ND->getTrailingRequiresClause()) {
    if (Visit(E))
      return true;
  }

  if (ND->doesThisDeclarationHaveABody() && !ND->isLateTemplateParsed()) {
    if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(ND)) {
      // Find the initializers that were written in the source.
      SmallVector<CXXCtorInitializer *, 4> WrittenInits;
      for (auto *I : Constructor->inits()) {
        if (!I->isWritten())
          continue;

        WrittenInits.push_back(I);
      }

      // Sort the initializers in source order
      llvm::array_pod_sort(WrittenInits.begin(), WrittenInits.end(),
                           &CompareCXXCtorInitializers);

      // Visit the initializers in source order
      for (unsigned I = 0, N = WrittenInits.size(); I != N; ++I) {
        CXXCtorInitializer *Init = WrittenInits[I];
        if (Init->isAnyMemberInitializer()) {
          if (Visit(MakeCursorMemberRef(Init->getAnyMember(),
                                        Init->getMemberLocation(), TU)))
            return true;
        } else if (TypeSourceInfo *TInfo = Init->getTypeSourceInfo()) {
          if (Visit(TInfo->getTypeLoc()))
            return true;
        }

        // Visit the initializer value.
        if (Expr *Initializer = Init->getInit())
          if (Visit(MakeCXCursor(Initializer, ND, TU, RegionOfInterest)))
            return true;
      }
    }

    if (Visit(MakeCXCursor(ND->getBody(), StmtParent, TU, RegionOfInterest)))
      return true;
  }

  return false;
}

bool CursorVisitor::VisitFieldDecl(FieldDecl *D) {
  if (VisitDeclaratorDecl(D))
    return true;

  if (Expr *BitWidth = D->getBitWidth())
    return Visit(MakeCXCursor(BitWidth, StmtParent, TU, RegionOfInterest));

  if (Expr *Init = D->getInClassInitializer())
    return Visit(MakeCXCursor(Init, StmtParent, TU, RegionOfInterest));

  return false;
}

bool CursorVisitor::VisitVarDecl(VarDecl *D) {
  if (VisitDeclaratorDecl(D))
    return true;

  if (Expr *Init = D->getInit())
    return Visit(MakeCXCursor(Init, StmtParent, TU, RegionOfInterest));

  return false;
}

bool CursorVisitor::VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {
  if (VisitDeclaratorDecl(D))
    return true;

  if (D->hasDefaultArgument() && !D->defaultArgumentWasInherited())
    if (D->hasDefaultArgument() &&
        VisitTemplateArgumentLoc(D->getDefaultArgument()))
      return true;

  return false;
}

bool CursorVisitor::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  // FIXME: Visit the "outer" template parameter lists on the FunctionDecl
  // before visiting these template parameters.
  if (VisitTemplateParameters(D->getTemplateParameters()))
    return true;

  auto *FD = D->getTemplatedDecl();
  return VisitAttributes(FD) || VisitFunctionDecl(FD);
}

bool CursorVisitor::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  // FIXME: Visit the "outer" template parameter lists on the TagDecl
  // before visiting these template parameters.
  if (VisitTemplateParameters(D->getTemplateParameters()))
    return true;

  auto *CD = D->getTemplatedDecl();
  return VisitAttributes(CD) || VisitCXXRecordDecl(CD);
}

bool CursorVisitor::VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D) {
  if (VisitTemplateParameters(D->getTemplateParameters()))
    return true;

  if (D->hasDefaultArgument() && !D->defaultArgumentWasInherited() &&
      VisitTemplateArgumentLoc(D->getDefaultArgument()))
    return true;

  return false;
}

bool CursorVisitor::VisitObjCTypeParamDecl(ObjCTypeParamDecl *D) {
  // Visit the bound, if it's explicit.
  if (D->hasExplicitBound()) {
    if (auto TInfo = D->getTypeSourceInfo()) {
      if (Visit(TInfo->getTypeLoc()))
        return true;
    }
  }

  return false;
}

bool CursorVisitor::VisitObjCMethodDecl(ObjCMethodDecl *ND) {
  if (TypeSourceInfo *TSInfo = ND->getReturnTypeSourceInfo())
    if (Visit(TSInfo->getTypeLoc()))
      return true;

  for (const auto *P : ND->parameters()) {
    if (Visit(MakeCXCursor(P, TU, RegionOfInterest)))
      return true;
  }

  return ND->isThisDeclarationADefinition() &&
         Visit(MakeCXCursor(ND->getBody(), StmtParent, TU, RegionOfInterest));
}

template <typename DeclIt>
static void addRangedDeclsInContainer(DeclIt *DI_current, DeclIt DE_current,
                                      SourceManager &SM, SourceLocation EndLoc,
                                      SmallVectorImpl<Decl *> &Decls) {
  DeclIt next = *DI_current;
  while (++next != DE_current) {
    Decl *D_next = *next;
    if (!D_next)
      break;
    SourceLocation L = D_next->getBeginLoc();
    if (!L.isValid())
      break;
    if (SM.isBeforeInTranslationUnit(L, EndLoc)) {
      *DI_current = next;
      Decls.push_back(D_next);
      continue;
    }
    break;
  }
}

bool CursorVisitor::VisitObjCContainerDecl(ObjCContainerDecl *D) {
  // FIXME: Eventually convert back to just 'VisitDeclContext()'.  Essentially
  // an @implementation can lexically contain Decls that are not properly
  // nested in the AST.  When we identify such cases, we need to retrofit
  // this nesting here.
  if (!DI_current && !FileDI_current)
    return VisitDeclContext(D);

  // Scan the Decls that immediately come after the container
  // in the current DeclContext.  If any fall within the
  // container's lexical region, stash them into a vector
  // for later processing.
  SmallVector<Decl *, 24> DeclsInContainer;
  SourceLocation EndLoc = D->getSourceRange().getEnd();
  SourceManager &SM = AU->getSourceManager();
  if (EndLoc.isValid()) {
    if (DI_current) {
      addRangedDeclsInContainer(DI_current, DE_current, SM, EndLoc,
                                DeclsInContainer);
    } else {
      addRangedDeclsInContainer(FileDI_current, FileDE_current, SM, EndLoc,
                                DeclsInContainer);
    }
  }

  // The common case.
  if (DeclsInContainer.empty())
    return VisitDeclContext(D);

  // Get all the Decls in the DeclContext, and sort them with the
  // additional ones we've collected.  Then visit them.
  for (auto *SubDecl : D->decls()) {
    if (!SubDecl || SubDecl->getLexicalDeclContext() != D ||
        SubDecl->getBeginLoc().isInvalid())
      continue;
    DeclsInContainer.push_back(SubDecl);
  }

  // Now sort the Decls so that they appear in lexical order.
  llvm::sort(DeclsInContainer, [&SM](Decl *A, Decl *B) {
    SourceLocation L_A = A->getBeginLoc();
    SourceLocation L_B = B->getBeginLoc();
    return L_A != L_B
               ? SM.isBeforeInTranslationUnit(L_A, L_B)
               : SM.isBeforeInTranslationUnit(A->getEndLoc(), B->getEndLoc());
  });

  // Now visit the decls.
  for (SmallVectorImpl<Decl *>::iterator I = DeclsInContainer.begin(),
                                         E = DeclsInContainer.end();
       I != E; ++I) {
    CXCursor Cursor = MakeCXCursor(*I, TU, RegionOfInterest);
    const std::optional<bool> &V = shouldVisitCursor(Cursor);
    if (!V)
      continue;
    if (!*V)
      return false;
    if (Visit(Cursor, true))
      return true;
  }
  return false;
}

bool CursorVisitor::VisitObjCCategoryDecl(ObjCCategoryDecl *ND) {
  if (Visit(MakeCursorObjCClassRef(ND->getClassInterface(), ND->getLocation(),
                                   TU)))
    return true;

  if (VisitObjCTypeParamList(ND->getTypeParamList()))
    return true;

  ObjCCategoryDecl::protocol_loc_iterator PL = ND->protocol_loc_begin();
  for (ObjCCategoryDecl::protocol_iterator I = ND->protocol_begin(),
                                           E = ND->protocol_end();
       I != E; ++I, ++PL)
    if (Visit(MakeCursorObjCProtocolRef(*I, *PL, TU)))
      return true;

  return VisitObjCContainerDecl(ND);
}

bool CursorVisitor::VisitObjCProtocolDecl(ObjCProtocolDecl *PID) {
  if (!PID->isThisDeclarationADefinition())
    return Visit(MakeCursorObjCProtocolRef(PID, PID->getLocation(), TU));

  ObjCProtocolDecl::protocol_loc_iterator PL = PID->protocol_loc_begin();
  for (ObjCProtocolDecl::protocol_iterator I = PID->protocol_begin(),
                                           E = PID->protocol_end();
       I != E; ++I, ++PL)
    if (Visit(MakeCursorObjCProtocolRef(*I, *PL, TU)))
      return true;

  return VisitObjCContainerDecl(PID);
}

bool CursorVisitor::VisitObjCPropertyDecl(ObjCPropertyDecl *PD) {
  if (PD->getTypeSourceInfo() && Visit(PD->getTypeSourceInfo()->getTypeLoc()))
    return true;

  // FIXME: This implements a workaround with @property declarations also being
  // installed in the DeclContext for the @interface.  Eventually this code
  // should be removed.
  ObjCCategoryDecl *CDecl = dyn_cast<ObjCCategoryDecl>(PD->getDeclContext());
  if (!CDecl || !CDecl->IsClassExtension())
    return false;

  ObjCInterfaceDecl *ID = CDecl->getClassInterface();
  if (!ID)
    return false;

  IdentifierInfo *PropertyId = PD->getIdentifier();
  ObjCPropertyDecl *prevDecl = ObjCPropertyDecl::findPropertyDecl(
      cast<DeclContext>(ID), PropertyId, PD->getQueryKind());

  if (!prevDecl)
    return false;

  // Visit synthesized methods since they will be skipped when visiting
  // the @interface.
  if (ObjCMethodDecl *MD = prevDecl->getGetterMethodDecl())
    if (MD->isPropertyAccessor() && MD->getLexicalDeclContext() == CDecl)
      if (Visit(MakeCXCursor(MD, TU, RegionOfInterest)))
        return true;

  if (ObjCMethodDecl *MD = prevDecl->getSetterMethodDecl())
    if (MD->isPropertyAccessor() && MD->getLexicalDeclContext() == CDecl)
      if (Visit(MakeCXCursor(MD, TU, RegionOfInterest)))
        return true;

  return false;
}

bool CursorVisitor::VisitObjCTypeParamList(ObjCTypeParamList *typeParamList) {
  if (!typeParamList)
    return false;

  for (auto *typeParam : *typeParamList) {
    // Visit the type parameter.
    if (Visit(MakeCXCursor(typeParam, TU, RegionOfInterest)))
      return true;
  }

  return false;
}

bool CursorVisitor::VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
  if (!D->isThisDeclarationADefinition()) {
    // Forward declaration is treated like a reference.
    return Visit(MakeCursorObjCClassRef(D, D->getLocation(), TU));
  }

  // Objective-C type parameters.
  if (VisitObjCTypeParamList(D->getTypeParamListAsWritten()))
    return true;

  // Issue callbacks for super class.
  if (D->getSuperClass() && Visit(MakeCursorObjCSuperClassRef(
                                D->getSuperClass(), D->getSuperClassLoc(), TU)))
    return true;

  if (TypeSourceInfo *SuperClassTInfo = D->getSuperClassTInfo())
    if (Visit(SuperClassTInfo->getTypeLoc()))
      return true;

  ObjCInterfaceDecl::protocol_loc_iterator PL = D->protocol_loc_begin();
  for (ObjCInterfaceDecl::protocol_iterator I = D->protocol_begin(),
                                            E = D->protocol_end();
       I != E; ++I, ++PL)
    if (Visit(MakeCursorObjCProtocolRef(*I, *PL, TU)))
      return true;

  return VisitObjCContainerDecl(D);
}

bool CursorVisitor::VisitObjCImplDecl(ObjCImplDecl *D) {
  return VisitObjCContainerDecl(D);
}

bool CursorVisitor::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
  // 'ID' could be null when dealing with invalid code.
  if (ObjCInterfaceDecl *ID = D->getClassInterface())
    if (Visit(MakeCursorObjCClassRef(ID, D->getLocation(), TU)))
      return true;

  return VisitObjCImplDecl(D);
}

bool CursorVisitor::VisitObjCImplementationDecl(ObjCImplementationDecl *D) {
#if 0
  // Issue callbacks for super class.
  // FIXME: No source location information!
  if (D->getSuperClass() &&
      Visit(MakeCursorObjCSuperClassRef(D->getSuperClass(),
                                        D->getSuperClassLoc(),
                                        TU)))
    return true;
#endif

  return VisitObjCImplDecl(D);
}

bool CursorVisitor::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *PD) {
  if (ObjCIvarDecl *Ivar = PD->getPropertyIvarDecl())
    if (PD->isIvarNameSpecified())
      return Visit(MakeCursorMemberRef(Ivar, PD->getPropertyIvarDeclLoc(), TU));

  return false;
}

bool CursorVisitor::VisitNamespaceDecl(NamespaceDecl *D) {
  return VisitDeclContext(D);
}

bool CursorVisitor::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  // Visit nested-name-specifier.
  if (NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc())
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;

  return Visit(MakeCursorNamespaceRef(D->getAliasedNamespace(),
                                      D->getTargetNameLoc(), TU));
}

bool CursorVisitor::VisitUsingDecl(UsingDecl *D) {
  // Visit nested-name-specifier.
  if (NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc()) {
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;
  }

  if (Visit(MakeCursorOverloadedDeclRef(D, D->getLocation(), TU)))
    return true;

  return VisitDeclarationNameInfo(D->getNameInfo());
}

bool CursorVisitor::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  // Visit nested-name-specifier.
  if (NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc())
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;

  return Visit(MakeCursorNamespaceRef(D->getNominatedNamespaceAsWritten(),
                                      D->getIdentLocation(), TU));
}

bool CursorVisitor::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  // Visit nested-name-specifier.
  if (NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc()) {
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;
  }

  return VisitDeclarationNameInfo(D->getNameInfo());
}

bool CursorVisitor::VisitUnresolvedUsingTypenameDecl(
    UnresolvedUsingTypenameDecl *D) {
  // Visit nested-name-specifier.
  if (NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc())
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;

  return false;
}

bool CursorVisitor::VisitStaticAssertDecl(StaticAssertDecl *D) {
  if (Visit(MakeCXCursor(D->getAssertExpr(), StmtParent, TU, RegionOfInterest)))
    return true;
  if (auto *Message = D->getMessage())
    if (Visit(MakeCXCursor(Message, StmtParent, TU, RegionOfInterest)))
      return true;
  return false;
}

bool CursorVisitor::VisitFriendDecl(FriendDecl *D) {
  if (NamedDecl *FriendD = D->getFriendDecl()) {
    if (Visit(MakeCXCursor(FriendD, TU, RegionOfInterest)))
      return true;
  } else if (TypeSourceInfo *TI = D->getFriendType()) {
    if (Visit(TI->getTypeLoc()))
      return true;
  }
  return false;
}

bool CursorVisitor::VisitDecompositionDecl(DecompositionDecl *D) {
  for (auto *B : D->bindings()) {
    if (Visit(MakeCXCursor(B, TU, RegionOfInterest)))
      return true;
  }
  return VisitVarDecl(D);
}

bool CursorVisitor::VisitConceptDecl(ConceptDecl *D) {
  if (VisitTemplateParameters(D->getTemplateParameters()))
    return true;

  if (auto *E = D->getConstraintExpr()) {
    if (Visit(MakeCXCursor(E, D, TU, RegionOfInterest)))
      return true;
  }
  return false;
}

bool CursorVisitor::VisitTypeConstraint(const TypeConstraint &TC) {
  if (TC.getNestedNameSpecifierLoc()) {
    if (VisitNestedNameSpecifierLoc(TC.getNestedNameSpecifierLoc()))
      return true;
  }
  if (TC.getNamedConcept()) {
    if (Visit(MakeCursorTemplateRef(TC.getNamedConcept(),
                                    TC.getConceptNameLoc(), TU)))
      return true;
  }
  if (auto Args = TC.getTemplateArgsAsWritten()) {
    for (const auto &Arg : Args->arguments()) {
      if (VisitTemplateArgumentLoc(Arg))
        return true;
    }
  }
  return false;
}

bool CursorVisitor::VisitConceptRequirement(const concepts::Requirement &R) {
  using namespace concepts;
  switch (R.getKind()) {
  case Requirement::RK_Type: {
    const TypeRequirement &TR = cast<TypeRequirement>(R);
    if (!TR.isSubstitutionFailure()) {
      if (Visit(TR.getType()->getTypeLoc()))
        return true;
    }
    break;
  }
  case Requirement::RK_Simple:
  case Requirement::RK_Compound: {
    const ExprRequirement &ER = cast<ExprRequirement>(R);
    if (!ER.isExprSubstitutionFailure()) {
      if (Visit(ER.getExpr()))
        return true;
    }
    if (ER.getKind() == Requirement::RK_Compound) {
      const auto &RTR = ER.getReturnTypeRequirement();
      if (RTR.isTypeConstraint()) {
        if (const auto *Cons = RTR.getTypeConstraint())
          VisitTypeConstraint(*Cons);
      }
    }
    break;
  }
  case Requirement::RK_Nested: {
    const NestedRequirement &NR = cast<NestedRequirement>(R);
    if (!NR.hasInvalidConstraint()) {
      if (Visit(NR.getConstraintExpr()))
        return true;
    }
    break;
  }
  }
  return false;
}

bool CursorVisitor::VisitDeclarationNameInfo(DeclarationNameInfo Name) {
  switch (Name.getName().getNameKind()) {
  case clang::DeclarationName::Identifier:
  case clang::DeclarationName::CXXLiteralOperatorName:
  case clang::DeclarationName::CXXDeductionGuideName:
  case clang::DeclarationName::CXXOperatorName:
  case clang::DeclarationName::CXXUsingDirective:
    return false;

  case clang::DeclarationName::CXXConstructorName:
  case clang::DeclarationName::CXXDestructorName:
  case clang::DeclarationName::CXXConversionFunctionName:
    if (TypeSourceInfo *TSInfo = Name.getNamedTypeInfo())
      return Visit(TSInfo->getTypeLoc());
    return false;

  case clang::DeclarationName::ObjCZeroArgSelector:
  case clang::DeclarationName::ObjCOneArgSelector:
  case clang::DeclarationName::ObjCMultiArgSelector:
    // FIXME: Per-identifier location info?
    return false;
  }

  llvm_unreachable("Invalid DeclarationName::Kind!");
}

bool CursorVisitor::VisitNestedNameSpecifier(NestedNameSpecifier *NNS,
                                             SourceRange Range) {
  // FIXME: This whole routine is a hack to work around the lack of proper
  // source information in nested-name-specifiers (PR5791). Since we do have
  // a beginning source location, we can visit the first component of the
  // nested-name-specifier, if it's a single-token component.
  if (!NNS)
    return false;

  // Get the first component in the nested-name-specifier.
  while (NestedNameSpecifier *Prefix = NNS->getPrefix())
    NNS = Prefix;

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Namespace:
    return Visit(
        MakeCursorNamespaceRef(NNS->getAsNamespace(), Range.getBegin(), TU));

  case NestedNameSpecifier::NamespaceAlias:
    return Visit(MakeCursorNamespaceRef(NNS->getAsNamespaceAlias(),
                                        Range.getBegin(), TU));

  case NestedNameSpecifier::TypeSpec: {
    // If the type has a form where we know that the beginning of the source
    // range matches up with a reference cursor. Visit the appropriate reference
    // cursor.
    const Type *T = NNS->getAsType();
    if (const TypedefType *Typedef = dyn_cast<TypedefType>(T))
      return Visit(MakeCursorTypeRef(Typedef->getDecl(), Range.getBegin(), TU));
    if (const TagType *Tag = dyn_cast<TagType>(T))
      return Visit(MakeCursorTypeRef(Tag->getDecl(), Range.getBegin(), TU));
    if (const TemplateSpecializationType *TST =
            dyn_cast<TemplateSpecializationType>(T))
      return VisitTemplateName(TST->getTemplateName(), Range.getBegin());
    break;
  }

  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::Super:
    break;
  }

  return false;
}

bool CursorVisitor::VisitNestedNameSpecifierLoc(
    NestedNameSpecifierLoc Qualifier) {
  SmallVector<NestedNameSpecifierLoc, 4> Qualifiers;
  for (; Qualifier; Qualifier = Qualifier.getPrefix())
    Qualifiers.push_back(Qualifier);

  while (!Qualifiers.empty()) {
    NestedNameSpecifierLoc Q = Qualifiers.pop_back_val();
    NestedNameSpecifier *NNS = Q.getNestedNameSpecifier();
    switch (NNS->getKind()) {
    case NestedNameSpecifier::Namespace:
      if (Visit(MakeCursorNamespaceRef(NNS->getAsNamespace(),
                                       Q.getLocalBeginLoc(), TU)))
        return true;

      break;

    case NestedNameSpecifier::NamespaceAlias:
      if (Visit(MakeCursorNamespaceRef(NNS->getAsNamespaceAlias(),
                                       Q.getLocalBeginLoc(), TU)))
        return true;

      break;

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate:
      if (Visit(Q.getTypeLoc()))
        return true;

      break;

    case NestedNameSpecifier::Global:
    case NestedNameSpecifier::Identifier:
    case NestedNameSpecifier::Super:
      break;
    }
  }

  return false;
}

bool CursorVisitor::VisitTemplateParameters(
    const TemplateParameterList *Params) {
  if (!Params)
    return false;

  for (TemplateParameterList::const_iterator P = Params->begin(),
                                             PEnd = Params->end();
       P != PEnd; ++P) {
    if (Visit(MakeCXCursor(*P, TU, RegionOfInterest)))
      return true;
  }

  if (const auto *E = Params->getRequiresClause()) {
    if (Visit(MakeCXCursor(E, nullptr, TU, RegionOfInterest)))
      return true;
  }

  return false;
}

bool CursorVisitor::VisitTemplateName(TemplateName Name, SourceLocation Loc) {
  switch (Name.getKind()) {
  case TemplateName::Template:
  case TemplateName::UsingTemplate:
  case TemplateName::QualifiedTemplate: // FIXME: Visit nested-name-specifier.
    return Visit(MakeCursorTemplateRef(Name.getAsTemplateDecl(), Loc, TU));

  case TemplateName::OverloadedTemplate:
    // Visit the overloaded template set.
    if (Visit(MakeCursorOverloadedDeclRef(Name, Loc, TU)))
      return true;

    return false;

  case TemplateName::AssumedTemplate:
    // FIXME: Visit DeclarationName?
    return false;

  case TemplateName::DependentTemplate:
    // FIXME: Visit nested-name-specifier.
    return false;

  case TemplateName::SubstTemplateTemplateParm:
    return Visit(MakeCursorTemplateRef(
        Name.getAsSubstTemplateTemplateParm()->getParameter(), Loc, TU));

  case TemplateName::SubstTemplateTemplateParmPack:
    return Visit(MakeCursorTemplateRef(
        Name.getAsSubstTemplateTemplateParmPack()->getParameterPack(), Loc,
        TU));
  }

  llvm_unreachable("Invalid TemplateName::Kind!");
}

bool CursorVisitor::VisitTemplateArgumentLoc(const TemplateArgumentLoc &TAL) {
  switch (TAL.getArgument().getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::Integral:
  case TemplateArgument::Pack:
    return false;

  case TemplateArgument::Type:
    if (TypeSourceInfo *TSInfo = TAL.getTypeSourceInfo())
      return Visit(TSInfo->getTypeLoc());
    return false;

  case TemplateArgument::Declaration:
    if (Expr *E = TAL.getSourceDeclExpression())
      return Visit(MakeCXCursor(E, StmtParent, TU, RegionOfInterest));
    return false;

  case TemplateArgument::StructuralValue:
    if (Expr *E = TAL.getSourceStructuralValueExpression())
      return Visit(MakeCXCursor(E, StmtParent, TU, RegionOfInterest));
    return false;

  case TemplateArgument::NullPtr:
    if (Expr *E = TAL.getSourceNullPtrExpression())
      return Visit(MakeCXCursor(E, StmtParent, TU, RegionOfInterest));
    return false;

  case TemplateArgument::Expression:
    if (Expr *E = TAL.getSourceExpression())
      return Visit(MakeCXCursor(E, StmtParent, TU, RegionOfInterest));
    return false;

  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
    if (VisitNestedNameSpecifierLoc(TAL.getTemplateQualifierLoc()))
      return true;

    return VisitTemplateName(TAL.getArgument().getAsTemplateOrTemplatePattern(),
                             TAL.getTemplateNameLoc());
  }

  llvm_unreachable("Invalid TemplateArgument::Kind!");
}

bool CursorVisitor::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  return VisitDeclContext(D);
}

bool CursorVisitor::VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
  return Visit(TL.getUnqualifiedLoc());
}

bool CursorVisitor::VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
  ASTContext &Context = AU->getASTContext();

  // Some builtin types (such as Objective-C's "id", "sel", and
  // "Class") have associated declarations. Create cursors for those.
  QualType VisitType;
  switch (TL.getTypePtr()->getKind()) {

  case BuiltinType::Void:
  case BuiltinType::NullPtr:
  case BuiltinType::Dependent:
#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix)                   \
  case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtTYpe, Id, Ext) case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
  case BuiltinType::OCLSampler:
  case BuiltinType::OCLEvent:
  case BuiltinType::OCLClkEvent:
  case BuiltinType::OCLQueue:
  case BuiltinType::OCLReserveID:
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
#define BUILTIN_TYPE(Id, SingletonId)
#define SIGNED_TYPE(Id, SingletonId) case BuiltinType::Id:
#define UNSIGNED_TYPE(Id, SingletonId) case BuiltinType::Id:
#define FLOATING_TYPE(Id, SingletonId) case BuiltinType::Id:
#define PLACEHOLDER_TYPE(Id, SingletonId) case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
    break;

  case BuiltinType::ObjCId:
    VisitType = Context.getObjCIdType();
    break;

  case BuiltinType::ObjCClass:
    VisitType = Context.getObjCClassType();
    break;

  case BuiltinType::ObjCSel:
    VisitType = Context.getObjCSelType();
    break;
  }

  if (!VisitType.isNull()) {
    if (const TypedefType *Typedef = VisitType->getAs<TypedefType>())
      return Visit(
          MakeCursorTypeRef(Typedef->getDecl(), TL.getBuiltinLoc(), TU));
  }

  return false;
}

bool CursorVisitor::VisitTypedefTypeLoc(TypedefTypeLoc TL) {
  return Visit(MakeCursorTypeRef(TL.getTypedefNameDecl(), TL.getNameLoc(), TU));
}

bool CursorVisitor::VisitUnresolvedUsingTypeLoc(UnresolvedUsingTypeLoc TL) {
  return Visit(MakeCursorTypeRef(TL.getDecl(), TL.getNameLoc(), TU));
}

bool CursorVisitor::VisitTagTypeLoc(TagTypeLoc TL) {
  if (TL.isDefinition())
    return Visit(MakeCXCursor(TL.getDecl(), TU, RegionOfInterest));

  return Visit(MakeCursorTypeRef(TL.getDecl(), TL.getNameLoc(), TU));
}

bool CursorVisitor::VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
  if (const auto *TC = TL.getDecl()->getTypeConstraint()) {
    if (VisitTypeConstraint(*TC))
      return true;
  }

  return Visit(MakeCursorTypeRef(TL.getDecl(), TL.getNameLoc(), TU));
}

bool CursorVisitor::VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
  return Visit(MakeCursorObjCClassRef(TL.getIFaceDecl(), TL.getNameLoc(), TU));
}

bool CursorVisitor::VisitObjCTypeParamTypeLoc(ObjCTypeParamTypeLoc TL) {
  if (Visit(MakeCursorTypeRef(TL.getDecl(), TL.getBeginLoc(), TU)))
    return true;
  for (unsigned I = 0, N = TL.getNumProtocols(); I != N; ++I) {
    if (Visit(MakeCursorObjCProtocolRef(TL.getProtocol(I), TL.getProtocolLoc(I),
                                        TU)))
      return true;
  }

  return false;
}

bool CursorVisitor::VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
  if (TL.hasBaseTypeAsWritten() && Visit(TL.getBaseLoc()))
    return true;

  for (unsigned I = 0, N = TL.getNumTypeArgs(); I != N; ++I) {
    if (Visit(TL.getTypeArgTInfo(I)->getTypeLoc()))
      return true;
  }

  for (unsigned I = 0, N = TL.getNumProtocols(); I != N; ++I) {
    if (Visit(MakeCursorObjCProtocolRef(TL.getProtocol(I), TL.getProtocolLoc(I),
                                        TU)))
      return true;
  }

  return false;
}

bool CursorVisitor::VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
  return Visit(TL.getPointeeLoc());
}

bool CursorVisitor::VisitParenTypeLoc(ParenTypeLoc TL) {
  return Visit(TL.getInnerLoc());
}

bool CursorVisitor::VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
  return Visit(TL.getInnerLoc());
}

bool CursorVisitor::VisitPointerTypeLoc(PointerTypeLoc TL) {
  return Visit(TL.getPointeeLoc());
}

bool CursorVisitor::VisitBlockPointerTypeLoc(BlockPointerTypeLoc TL) {
  return Visit(TL.getPointeeLoc());
}

bool CursorVisitor::VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
  return Visit(TL.getPointeeLoc());
}

bool CursorVisitor::VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc TL) {
  return Visit(TL.getPointeeLoc());
}

bool CursorVisitor::VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc TL) {
  return Visit(TL.getPointeeLoc());
}

bool CursorVisitor::VisitUsingTypeLoc(UsingTypeLoc TL) {
  auto *underlyingDecl = TL.getUnderlyingType()->getAsTagDecl();
  if (underlyingDecl) {
    return Visit(MakeCursorTypeRef(underlyingDecl, TL.getNameLoc(), TU));
  }
  return false;
}

bool CursorVisitor::VisitAttributedTypeLoc(AttributedTypeLoc TL) {
  return Visit(TL.getModifiedLoc());
}

bool CursorVisitor::VisitCountAttributedTypeLoc(CountAttributedTypeLoc TL) {
  return Visit(TL.getInnerLoc());
}

bool CursorVisitor::VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
  return Visit(TL.getWrappedLoc());
}

bool CursorVisitor::VisitFunctionTypeLoc(FunctionTypeLoc TL,
                                         bool SkipResultType) {
  if (!SkipResultType && Visit(TL.getReturnLoc()))
    return true;

  for (unsigned I = 0, N = TL.getNumParams(); I != N; ++I)
    if (Decl *D = TL.getParam(I))
      if (Visit(MakeCXCursor(D, TU, RegionOfInterest)))
        return true;

  return false;
}

bool CursorVisitor::VisitArrayTypeLoc(ArrayTypeLoc TL) {
  if (Visit(TL.getElementLoc()))
    return true;

  if (Expr *Size = TL.getSizeExpr())
    return Visit(MakeCXCursor(Size, StmtParent, TU, RegionOfInterest));

  return false;
}

bool CursorVisitor::VisitDecayedTypeLoc(DecayedTypeLoc TL) {
  return Visit(TL.getOriginalLoc());
}

bool CursorVisitor::VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
  return Visit(TL.getOriginalLoc());
}

bool CursorVisitor::VisitDeducedTemplateSpecializationTypeLoc(
    DeducedTemplateSpecializationTypeLoc TL) {
  if (VisitTemplateName(TL.getTypePtr()->getTemplateName(),
                        TL.getTemplateNameLoc()))
    return true;

  return false;
}

bool CursorVisitor::VisitTemplateSpecializationTypeLoc(
    TemplateSpecializationTypeLoc TL) {
  // Visit the template name.
  if (VisitTemplateName(TL.getTypePtr()->getTemplateName(),
                        TL.getTemplateNameLoc()))
    return true;

  // Visit the template arguments.
  for (unsigned I = 0, N = TL.getNumArgs(); I != N; ++I)
    if (VisitTemplateArgumentLoc(TL.getArgLoc(I)))
      return true;

  return false;
}

bool CursorVisitor::VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
  return Visit(MakeCXCursor(TL.getUnderlyingExpr(), StmtParent, TU));
}

bool CursorVisitor::VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
  if (TypeSourceInfo *TSInfo = TL.getUnmodifiedTInfo())
    return Visit(TSInfo->getTypeLoc());

  return false;
}

bool CursorVisitor::VisitUnaryTransformTypeLoc(UnaryTransformTypeLoc TL) {
  if (TypeSourceInfo *TSInfo = TL.getUnderlyingTInfo())
    return Visit(TSInfo->getTypeLoc());

  return false;
}

bool CursorVisitor::VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
  return VisitNestedNameSpecifierLoc(TL.getQualifierLoc());
}

bool CursorVisitor::VisitDependentTemplateSpecializationTypeLoc(
    DependentTemplateSpecializationTypeLoc TL) {
  // Visit the nested-name-specifier, if there is one.
  if (TL.getQualifierLoc() && VisitNestedNameSpecifierLoc(TL.getQualifierLoc()))
    return true;

  // Visit the template arguments.
  for (unsigned I = 0, N = TL.getNumArgs(); I != N; ++I)
    if (VisitTemplateArgumentLoc(TL.getArgLoc(I)))
      return true;

  return false;
}

bool CursorVisitor::VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
  if (VisitNestedNameSpecifierLoc(TL.getQualifierLoc()))
    return true;

  return Visit(TL.getNamedTypeLoc());
}

bool CursorVisitor::VisitPackExpansionTypeLoc(PackExpansionTypeLoc TL) {
  return Visit(TL.getPatternLoc());
}

bool CursorVisitor::VisitDecltypeTypeLoc(DecltypeTypeLoc TL) {
  if (Expr *E = TL.getUnderlyingExpr())
    return Visit(MakeCXCursor(E, StmtParent, TU));

  return false;
}

bool CursorVisitor::VisitPackIndexingTypeLoc(PackIndexingTypeLoc TL) {
  if (Visit(TL.getPatternLoc()))
    return true;
  return Visit(MakeCXCursor(TL.getIndexExpr(), StmtParent, TU));
}

bool CursorVisitor::VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc TL) {
  return Visit(MakeCursorTypeRef(TL.getDecl(), TL.getNameLoc(), TU));
}

bool CursorVisitor::VisitAtomicTypeLoc(AtomicTypeLoc TL) {
  return Visit(TL.getValueLoc());
}

bool CursorVisitor::VisitPipeTypeLoc(PipeTypeLoc TL) {
  return Visit(TL.getValueLoc());
}

#define DEFAULT_TYPELOC_IMPL(CLASS, PARENT)                                    \
  bool CursorVisitor::Visit##CLASS##TypeLoc(CLASS##TypeLoc TL) {               \
    return Visit##PARENT##Loc(TL);                                             \
  }

DEFAULT_TYPELOC_IMPL(Complex, Type)
DEFAULT_TYPELOC_IMPL(ConstantArray, ArrayType)
DEFAULT_TYPELOC_IMPL(IncompleteArray, ArrayType)
DEFAULT_TYPELOC_IMPL(VariableArray, ArrayType)
DEFAULT_TYPELOC_IMPL(DependentSizedArray, ArrayType)
DEFAULT_TYPELOC_IMPL(ArrayParameter, ConstantArrayType)
DEFAULT_TYPELOC_IMPL(DependentAddressSpace, Type)
DEFAULT_TYPELOC_IMPL(DependentVector, Type)
DEFAULT_TYPELOC_IMPL(DependentSizedExtVector, Type)
DEFAULT_TYPELOC_IMPL(Vector, Type)
DEFAULT_TYPELOC_IMPL(ExtVector, VectorType)
DEFAULT_TYPELOC_IMPL(ConstantMatrix, MatrixType)
DEFAULT_TYPELOC_IMPL(DependentSizedMatrix, MatrixType)
DEFAULT_TYPELOC_IMPL(FunctionProto, FunctionType)
DEFAULT_TYPELOC_IMPL(FunctionNoProto, FunctionType)
DEFAULT_TYPELOC_IMPL(Record, TagType)
DEFAULT_TYPELOC_IMPL(Enum, TagType)
DEFAULT_TYPELOC_IMPL(SubstTemplateTypeParm, Type)
DEFAULT_TYPELOC_IMPL(SubstTemplateTypeParmPack, Type)
DEFAULT_TYPELOC_IMPL(Auto, Type)
DEFAULT_TYPELOC_IMPL(BitInt, Type)
DEFAULT_TYPELOC_IMPL(DependentBitInt, Type)

bool CursorVisitor::VisitCXXRecordDecl(CXXRecordDecl *D) {
  // Visit the nested-name-specifier, if present.
  if (NestedNameSpecifierLoc QualifierLoc = D->getQualifierLoc())
    if (VisitNestedNameSpecifierLoc(QualifierLoc))
      return true;

  if (D->isCompleteDefinition()) {
    for (const auto &I : D->bases()) {
      if (Visit(cxcursor::MakeCursorCXXBaseSpecifier(&I, TU)))
        return true;
    }
  }

  return VisitTagDecl(D);
}

bool CursorVisitor::VisitAttributes(Decl *D) {
  for (const auto *I : D->attrs())
    if ((TU->ParsingOptions & CXTranslationUnit_VisitImplicitAttributes ||
         !I->isImplicit()) &&
        Visit(MakeCXCursor(I, D, TU)))
      return true;

  return false;
}

//===----------------------------------------------------------------------===//
// Data-recursive visitor methods.
//===----------------------------------------------------------------------===//

namespace {
#define DEF_JOB(NAME, DATA, KIND)                                              \
  class NAME : public VisitorJob {                                             \
  public:                                                                      \
    NAME(const DATA *d, CXCursor parent)                                       \
        : VisitorJob(parent, VisitorJob::KIND, d) {}                           \
    static bool classof(const VisitorJob *VJ) {                                \
      return VJ->getKind() == KIND;                                            \
    }                                                                          \
    const DATA *get() const { return static_cast<const DATA *>(data[0]); }     \
  };

DEF_JOB(StmtVisit, Stmt, StmtVisitKind)
DEF_JOB(MemberExprParts, MemberExpr, MemberExprPartsKind)
DEF_JOB(DeclRefExprParts, DeclRefExpr, DeclRefExprPartsKind)
DEF_JOB(OverloadExprParts, OverloadExpr, OverloadExprPartsKind)
DEF_JOB(SizeOfPackExprParts, SizeOfPackExpr, SizeOfPackExprPartsKind)
DEF_JOB(LambdaExprParts, LambdaExpr, LambdaExprPartsKind)
DEF_JOB(ConceptSpecializationExprVisit, ConceptSpecializationExpr,
        ConceptSpecializationExprVisitKind)
DEF_JOB(RequiresExprVisit, RequiresExpr, RequiresExprVisitKind)
DEF_JOB(PostChildrenVisit, void, PostChildrenVisitKind)
#undef DEF_JOB

class ExplicitTemplateArgsVisit : public VisitorJob {
public:
  ExplicitTemplateArgsVisit(const TemplateArgumentLoc *Begin,
                            const TemplateArgumentLoc *End, CXCursor parent)
      : VisitorJob(parent, VisitorJob::ExplicitTemplateArgsVisitKind, Begin,
                   End) {}
  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == ExplicitTemplateArgsVisitKind;
  }
  const TemplateArgumentLoc *begin() const {
    return static_cast<const TemplateArgumentLoc *>(data[0]);
  }
  const TemplateArgumentLoc *end() {
    return static_cast<const TemplateArgumentLoc *>(data[1]);
  }
};
class DeclVisit : public VisitorJob {
public:
  DeclVisit(const Decl *D, CXCursor parent, bool isFirst)
      : VisitorJob(parent, VisitorJob::DeclVisitKind, D,
                   isFirst ? (void *)1 : (void *)nullptr) {}
  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == DeclVisitKind;
  }
  const Decl *get() const { return static_cast<const Decl *>(data[0]); }
  bool isFirst() const { return data[1] != nullptr; }
};
class TypeLocVisit : public VisitorJob {
public:
  TypeLocVisit(TypeLoc tl, CXCursor parent)
      : VisitorJob(parent, VisitorJob::TypeLocVisitKind,
                   tl.getType().getAsOpaquePtr(), tl.getOpaqueData()) {}

  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == TypeLocVisitKind;
  }

  TypeLoc get() const {
    QualType T = QualType::getFromOpaquePtr(data[0]);
    return TypeLoc(T, const_cast<void *>(data[1]));
  }
};

class LabelRefVisit : public VisitorJob {
public:
  LabelRefVisit(LabelDecl *LD, SourceLocation labelLoc, CXCursor parent)
      : VisitorJob(parent, VisitorJob::LabelRefVisitKind, LD,
                   labelLoc.getPtrEncoding()) {}

  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == VisitorJob::LabelRefVisitKind;
  }
  const LabelDecl *get() const {
    return static_cast<const LabelDecl *>(data[0]);
  }
  SourceLocation getLoc() const {
    return SourceLocation::getFromPtrEncoding(data[1]);
  }
};

class NestedNameSpecifierLocVisit : public VisitorJob {
public:
  NestedNameSpecifierLocVisit(NestedNameSpecifierLoc Qualifier, CXCursor parent)
      : VisitorJob(parent, VisitorJob::NestedNameSpecifierLocVisitKind,
                   Qualifier.getNestedNameSpecifier(),
                   Qualifier.getOpaqueData()) {}

  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == VisitorJob::NestedNameSpecifierLocVisitKind;
  }

  NestedNameSpecifierLoc get() const {
    return NestedNameSpecifierLoc(
        const_cast<NestedNameSpecifier *>(
            static_cast<const NestedNameSpecifier *>(data[0])),
        const_cast<void *>(data[1]));
  }
};

class DeclarationNameInfoVisit : public VisitorJob {
public:
  DeclarationNameInfoVisit(const Stmt *S, CXCursor parent)
      : VisitorJob(parent, VisitorJob::DeclarationNameInfoVisitKind, S) {}
  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == VisitorJob::DeclarationNameInfoVisitKind;
  }
  DeclarationNameInfo get() const {
    const Stmt *S = static_cast<const Stmt *>(data[0]);
    switch (S->getStmtClass()) {
    default:
      llvm_unreachable("Unhandled Stmt");
    case clang::Stmt::MSDependentExistsStmtClass:
      return cast<MSDependentExistsStmt>(S)->getNameInfo();
    case Stmt::CXXDependentScopeMemberExprClass:
      return cast<CXXDependentScopeMemberExpr>(S)->getMemberNameInfo();
    case Stmt::DependentScopeDeclRefExprClass:
      return cast<DependentScopeDeclRefExpr>(S)->getNameInfo();
    case Stmt::OMPCriticalDirectiveClass:
      return cast<OMPCriticalDirective>(S)->getDirectiveName();
    }
  }
};
class MemberRefVisit : public VisitorJob {
public:
  MemberRefVisit(const FieldDecl *D, SourceLocation L, CXCursor parent)
      : VisitorJob(parent, VisitorJob::MemberRefVisitKind, D,
                   L.getPtrEncoding()) {}
  static bool classof(const VisitorJob *VJ) {
    return VJ->getKind() == VisitorJob::MemberRefVisitKind;
  }
  const FieldDecl *get() const {
    return static_cast<const FieldDecl *>(data[0]);
  }
  SourceLocation getLoc() const {
    return SourceLocation::getFromRawEncoding(
        (SourceLocation::UIntTy)(uintptr_t)data[1]);
  }
};
class EnqueueVisitor : public ConstStmtVisitor<EnqueueVisitor, void>,
                       public ConstAttrVisitor<EnqueueVisitor, void> {
  friend class OpenACCClauseEnqueue;
  friend class OMPClauseEnqueue;
  VisitorWorkList &WL;
  CXCursor Parent;

public:
  EnqueueVisitor(VisitorWorkList &wl, CXCursor parent)
      : WL(wl), Parent(parent) {}

  void VisitAddrLabelExpr(const AddrLabelExpr *E);
  void VisitBlockExpr(const BlockExpr *B);
  void VisitCompoundLiteralExpr(const CompoundLiteralExpr *E);
  void VisitCompoundStmt(const CompoundStmt *S);
  void VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *E) { /* Do nothing. */
  }
  void VisitMSDependentExistsStmt(const MSDependentExistsStmt *S);
  void VisitCXXDependentScopeMemberExpr(const CXXDependentScopeMemberExpr *E);
  void VisitCXXNewExpr(const CXXNewExpr *E);
  void VisitCXXScalarValueInitExpr(const CXXScalarValueInitExpr *E);
  void VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *E);
  void VisitCXXPseudoDestructorExpr(const CXXPseudoDestructorExpr *E);
  void VisitCXXTemporaryObjectExpr(const CXXTemporaryObjectExpr *E);
  void VisitCXXTypeidExpr(const CXXTypeidExpr *E);
  void VisitCXXUnresolvedConstructExpr(const CXXUnresolvedConstructExpr *E);
  void VisitCXXUuidofExpr(const CXXUuidofExpr *E);
  void VisitCXXCatchStmt(const CXXCatchStmt *S);
  void VisitCXXForRangeStmt(const CXXForRangeStmt *S);
  void VisitDeclRefExpr(const DeclRefExpr *D);
  void VisitDeclStmt(const DeclStmt *S);
  void VisitDependentScopeDeclRefExpr(const DependentScopeDeclRefExpr *E);
  void VisitDesignatedInitExpr(const DesignatedInitExpr *E);
  void VisitExplicitCastExpr(const ExplicitCastExpr *E);
  void VisitForStmt(const ForStmt *FS);
  void VisitGotoStmt(const GotoStmt *GS);
  void VisitIfStmt(const IfStmt *If);
  void VisitInitListExpr(const InitListExpr *IE);
  void VisitMemberExpr(const MemberExpr *M);
  void VisitOffsetOfExpr(const OffsetOfExpr *E);
  void VisitObjCEncodeExpr(const ObjCEncodeExpr *E);
  void VisitObjCMessageExpr(const ObjCMessageExpr *M);
  void VisitOverloadExpr(const OverloadExpr *E);
  void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E);
  void VisitStmt(const Stmt *S);
  void VisitSwitchStmt(const SwitchStmt *S);
  void VisitWhileStmt(const WhileStmt *W);
  void VisitTypeTraitExpr(const TypeTraitExpr *E);
  void VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *E);
  void VisitExpressionTraitExpr(const ExpressionTraitExpr *E);
  void VisitUnresolvedMemberExpr(const UnresolvedMemberExpr *U);
  void VisitVAArgExpr(const VAArgExpr *E);
  void VisitSizeOfPackExpr(const SizeOfPackExpr *E);
  void VisitPseudoObjectExpr(const PseudoObjectExpr *E);
  void VisitOpaqueValueExpr(const OpaqueValueExpr *E);
  void VisitLambdaExpr(const LambdaExpr *E);
  void VisitConceptSpecializationExpr(const ConceptSpecializationExpr *E);
  void VisitRequiresExpr(const RequiresExpr *E);
  void VisitCXXParenListInitExpr(const CXXParenListInitExpr *E);
  void VisitOpenACCComputeConstruct(const OpenACCComputeConstruct *D);
  void VisitOpenACCLoopConstruct(const OpenACCLoopConstruct *D);
  void VisitOMPExecutableDirective(const OMPExecutableDirective *D);
  void VisitOMPLoopBasedDirective(const OMPLoopBasedDirective *D);
  void VisitOMPLoopDirective(const OMPLoopDirective *D);
  void VisitOMPParallelDirective(const OMPParallelDirective *D);
  void VisitOMPSimdDirective(const OMPSimdDirective *D);
  void
  VisitOMPLoopTransformationDirective(const OMPLoopTransformationDirective *D);
  void VisitOMPTileDirective(const OMPTileDirective *D);
  void VisitOMPUnrollDirective(const OMPUnrollDirective *D);
  void VisitOMPReverseDirective(const OMPReverseDirective *D);
  void VisitOMPInterchangeDirective(const OMPInterchangeDirective *D);
  void VisitOMPForDirective(const OMPForDirective *D);
  void VisitOMPForSimdDirective(const OMPForSimdDirective *D);
  void VisitOMPSectionsDirective(const OMPSectionsDirective *D);
  void VisitOMPSectionDirective(const OMPSectionDirective *D);
  void VisitOMPSingleDirective(const OMPSingleDirective *D);
  void VisitOMPMasterDirective(const OMPMasterDirective *D);
  void VisitOMPCriticalDirective(const OMPCriticalDirective *D);
  void VisitOMPParallelForDirective(const OMPParallelForDirective *D);
  void VisitOMPParallelForSimdDirective(const OMPParallelForSimdDirective *D);
  void VisitOMPParallelMasterDirective(const OMPParallelMasterDirective *D);
  void VisitOMPParallelMaskedDirective(const OMPParallelMaskedDirective *D);
  void VisitOMPParallelSectionsDirective(const OMPParallelSectionsDirective *D);
  void VisitOMPTaskDirective(const OMPTaskDirective *D);
  void VisitOMPTaskyieldDirective(const OMPTaskyieldDirective *D);
  void VisitOMPBarrierDirective(const OMPBarrierDirective *D);
  void VisitOMPTaskwaitDirective(const OMPTaskwaitDirective *D);
  void VisitOMPErrorDirective(const OMPErrorDirective *D);
  void VisitOMPTaskgroupDirective(const OMPTaskgroupDirective *D);
  void
  VisitOMPCancellationPointDirective(const OMPCancellationPointDirective *D);
  void VisitOMPCancelDirective(const OMPCancelDirective *D);
  void VisitOMPFlushDirective(const OMPFlushDirective *D);
  void VisitOMPDepobjDirective(const OMPDepobjDirective *D);
  void VisitOMPScanDirective(const OMPScanDirective *D);
  void VisitOMPOrderedDirective(const OMPOrderedDirective *D);
  void VisitOMPAtomicDirective(const OMPAtomicDirective *D);
  void VisitOMPTargetDirective(const OMPTargetDirective *D);
  void VisitOMPTargetDataDirective(const OMPTargetDataDirective *D);
  void VisitOMPTargetEnterDataDirective(const OMPTargetEnterDataDirective *D);
  void VisitOMPTargetExitDataDirective(const OMPTargetExitDataDirective *D);
  void VisitOMPTargetParallelDirective(const OMPTargetParallelDirective *D);
  void
  VisitOMPTargetParallelForDirective(const OMPTargetParallelForDirective *D);
  void VisitOMPTeamsDirective(const OMPTeamsDirective *D);
  void VisitOMPTaskLoopDirective(const OMPTaskLoopDirective *D);
  void VisitOMPTaskLoopSimdDirective(const OMPTaskLoopSimdDirective *D);
  void VisitOMPMasterTaskLoopDirective(const OMPMasterTaskLoopDirective *D);
  void VisitOMPMaskedTaskLoopDirective(const OMPMaskedTaskLoopDirective *D);
  void
  VisitOMPMasterTaskLoopSimdDirective(const OMPMasterTaskLoopSimdDirective *D);
  void VisitOMPMaskedTaskLoopSimdDirective(
      const OMPMaskedTaskLoopSimdDirective *D);
  void VisitOMPParallelMasterTaskLoopDirective(
      const OMPParallelMasterTaskLoopDirective *D);
  void VisitOMPParallelMaskedTaskLoopDirective(
      const OMPParallelMaskedTaskLoopDirective *D);
  void VisitOMPParallelMasterTaskLoopSimdDirective(
      const OMPParallelMasterTaskLoopSimdDirective *D);
  void VisitOMPParallelMaskedTaskLoopSimdDirective(
      const OMPParallelMaskedTaskLoopSimdDirective *D);
  void VisitOMPDistributeDirective(const OMPDistributeDirective *D);
  void VisitOMPDistributeParallelForDirective(
      const OMPDistributeParallelForDirective *D);
  void VisitOMPDistributeParallelForSimdDirective(
      const OMPDistributeParallelForSimdDirective *D);
  void VisitOMPDistributeSimdDirective(const OMPDistributeSimdDirective *D);
  void VisitOMPTargetParallelForSimdDirective(
      const OMPTargetParallelForSimdDirective *D);
  void VisitOMPTargetSimdDirective(const OMPTargetSimdDirective *D);
  void VisitOMPTeamsDistributeDirective(const OMPTeamsDistributeDirective *D);
  void VisitOMPTeamsDistributeSimdDirective(
      const OMPTeamsDistributeSimdDirective *D);
  void VisitOMPTeamsDistributeParallelForSimdDirective(
      const OMPTeamsDistributeParallelForSimdDirective *D);
  void VisitOMPTeamsDistributeParallelForDirective(
      const OMPTeamsDistributeParallelForDirective *D);
  void VisitOMPTargetTeamsDirective(const OMPTargetTeamsDirective *D);
  void VisitOMPTargetTeamsDistributeDirective(
      const OMPTargetTeamsDistributeDirective *D);
  void VisitOMPTargetTeamsDistributeParallelForDirective(
      const OMPTargetTeamsDistributeParallelForDirective *D);
  void VisitOMPTargetTeamsDistributeParallelForSimdDirective(
      const OMPTargetTeamsDistributeParallelForSimdDirective *D);
  void VisitOMPTargetTeamsDistributeSimdDirective(
      const OMPTargetTeamsDistributeSimdDirective *D);

  // Attributes
  void VisitAnnotateAttr(const AnnotateAttr *A);

private:
  void AddDeclarationNameInfo(const Stmt *S);
  void AddNestedNameSpecifierLoc(NestedNameSpecifierLoc Qualifier);
  void AddExplicitTemplateArgs(const TemplateArgumentLoc *A,
                               unsigned NumTemplateArgs);
  void AddMemberRef(const FieldDecl *D, SourceLocation L);
  void AddStmt(const Stmt *S);
  void AddDecl(const Decl *D, bool isFirst = true);
  void AddTypeLoc(TypeSourceInfo *TI);
  void EnqueueChildren(const Stmt *S);
  void EnqueueChildren(const OpenACCClause *S);
  void EnqueueChildren(const OMPClause *S);
  void EnqueueChildren(const AnnotateAttr *A);
};
} // namespace

void EnqueueVisitor::AddDeclarationNameInfo(const Stmt *S) {
  // 'S' should always be non-null, since it comes from the
  // statement we are visiting.
  WL.push_back(DeclarationNameInfoVisit(S, Parent));
}

void EnqueueVisitor::AddNestedNameSpecifierLoc(
    NestedNameSpecifierLoc Qualifier) {
  if (Qualifier)
    WL.push_back(NestedNameSpecifierLocVisit(Qualifier, Parent));
}

void EnqueueVisitor::AddStmt(const Stmt *S) {
  if (S)
    WL.push_back(StmtVisit(S, Parent));
}
void EnqueueVisitor::AddDecl(const Decl *D, bool isFirst) {
  if (D)
    WL.push_back(DeclVisit(D, Parent, isFirst));
}
void EnqueueVisitor::AddExplicitTemplateArgs(const TemplateArgumentLoc *A,
                                             unsigned NumTemplateArgs) {
  WL.push_back(ExplicitTemplateArgsVisit(A, A + NumTemplateArgs, Parent));
}
void EnqueueVisitor::AddMemberRef(const FieldDecl *D, SourceLocation L) {
  if (D)
    WL.push_back(MemberRefVisit(D, L, Parent));
}
void EnqueueVisitor::AddTypeLoc(TypeSourceInfo *TI) {
  if (TI)
    WL.push_back(TypeLocVisit(TI->getTypeLoc(), Parent));
}
void EnqueueVisitor::EnqueueChildren(const Stmt *S) {
  unsigned size = WL.size();
  for (const Stmt *SubStmt : S->children()) {
    AddStmt(SubStmt);
  }
  if (size == WL.size())
    return;
  // Now reverse the entries we just added.  This will match the DFS
  // ordering performed by the worklist.
  VisitorWorkList::iterator I = WL.begin() + size, E = WL.end();
  std::reverse(I, E);
}
namespace {
class OMPClauseEnqueue : public ConstOMPClauseVisitor<OMPClauseEnqueue> {
  EnqueueVisitor *Visitor;
  /// Process clauses with list of variables.
  template <typename T> void VisitOMPClauseList(T *Node);

public:
  OMPClauseEnqueue(EnqueueVisitor *Visitor) : Visitor(Visitor) {}
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) void Visit##Class(const Class *C);
#include "llvm/Frontend/OpenMP/OMP.inc"
  void VisitOMPClauseWithPreInit(const OMPClauseWithPreInit *C);
  void VisitOMPClauseWithPostUpdate(const OMPClauseWithPostUpdate *C);
};

void OMPClauseEnqueue::VisitOMPClauseWithPreInit(
    const OMPClauseWithPreInit *C) {
  Visitor->AddStmt(C->getPreInitStmt());
}

void OMPClauseEnqueue::VisitOMPClauseWithPostUpdate(
    const OMPClauseWithPostUpdate *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getPostUpdateExpr());
}

void OMPClauseEnqueue::VisitOMPIfClause(const OMPIfClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getCondition());
}

void OMPClauseEnqueue::VisitOMPFinalClause(const OMPFinalClause *C) {
  Visitor->AddStmt(C->getCondition());
}

void OMPClauseEnqueue::VisitOMPNumThreadsClause(const OMPNumThreadsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getNumThreads());
}

void OMPClauseEnqueue::VisitOMPSafelenClause(const OMPSafelenClause *C) {
  Visitor->AddStmt(C->getSafelen());
}

void OMPClauseEnqueue::VisitOMPSimdlenClause(const OMPSimdlenClause *C) {
  Visitor->AddStmt(C->getSimdlen());
}

void OMPClauseEnqueue::VisitOMPSizesClause(const OMPSizesClause *C) {
  for (auto E : C->getSizesRefs())
    Visitor->AddStmt(E);
}

void OMPClauseEnqueue::VisitOMPFullClause(const OMPFullClause *C) {}

void OMPClauseEnqueue::VisitOMPPartialClause(const OMPPartialClause *C) {
  Visitor->AddStmt(C->getFactor());
}

void OMPClauseEnqueue::VisitOMPAllocatorClause(const OMPAllocatorClause *C) {
  Visitor->AddStmt(C->getAllocator());
}

void OMPClauseEnqueue::VisitOMPCollapseClause(const OMPCollapseClause *C) {
  Visitor->AddStmt(C->getNumForLoops());
}

void OMPClauseEnqueue::VisitOMPDefaultClause(const OMPDefaultClause *C) {}

void OMPClauseEnqueue::VisitOMPProcBindClause(const OMPProcBindClause *C) {}

void OMPClauseEnqueue::VisitOMPScheduleClause(const OMPScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getChunkSize());
}

void OMPClauseEnqueue::VisitOMPOrderedClause(const OMPOrderedClause *C) {
  Visitor->AddStmt(C->getNumForLoops());
}

void OMPClauseEnqueue::VisitOMPDetachClause(const OMPDetachClause *C) {
  Visitor->AddStmt(C->getEventHandler());
}

void OMPClauseEnqueue::VisitOMPNowaitClause(const OMPNowaitClause *) {}

void OMPClauseEnqueue::VisitOMPUntiedClause(const OMPUntiedClause *) {}

void OMPClauseEnqueue::VisitOMPMergeableClause(const OMPMergeableClause *) {}

void OMPClauseEnqueue::VisitOMPReadClause(const OMPReadClause *) {}

void OMPClauseEnqueue::VisitOMPWriteClause(const OMPWriteClause *) {}

void OMPClauseEnqueue::VisitOMPUpdateClause(const OMPUpdateClause *) {}

void OMPClauseEnqueue::VisitOMPCaptureClause(const OMPCaptureClause *) {}

void OMPClauseEnqueue::VisitOMPCompareClause(const OMPCompareClause *) {}

void OMPClauseEnqueue::VisitOMPFailClause(const OMPFailClause *) {}

void OMPClauseEnqueue::VisitOMPSeqCstClause(const OMPSeqCstClause *) {}

void OMPClauseEnqueue::VisitOMPAcqRelClause(const OMPAcqRelClause *) {}

void OMPClauseEnqueue::VisitOMPAcquireClause(const OMPAcquireClause *) {}

void OMPClauseEnqueue::VisitOMPReleaseClause(const OMPReleaseClause *) {}

void OMPClauseEnqueue::VisitOMPRelaxedClause(const OMPRelaxedClause *) {}

void OMPClauseEnqueue::VisitOMPWeakClause(const OMPWeakClause *) {}

void OMPClauseEnqueue::VisitOMPThreadsClause(const OMPThreadsClause *) {}

void OMPClauseEnqueue::VisitOMPSIMDClause(const OMPSIMDClause *) {}

void OMPClauseEnqueue::VisitOMPNogroupClause(const OMPNogroupClause *) {}

void OMPClauseEnqueue::VisitOMPInitClause(const OMPInitClause *C) {
  VisitOMPClauseList(C);
}

void OMPClauseEnqueue::VisitOMPUseClause(const OMPUseClause *C) {
  Visitor->AddStmt(C->getInteropVar());
}

void OMPClauseEnqueue::VisitOMPDestroyClause(const OMPDestroyClause *C) {
  if (C->getInteropVar())
    Visitor->AddStmt(C->getInteropVar());
}

void OMPClauseEnqueue::VisitOMPNovariantsClause(const OMPNovariantsClause *C) {
  Visitor->AddStmt(C->getCondition());
}

void OMPClauseEnqueue::VisitOMPNocontextClause(const OMPNocontextClause *C) {
  Visitor->AddStmt(C->getCondition());
}

void OMPClauseEnqueue::VisitOMPFilterClause(const OMPFilterClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getThreadID());
}

void OMPClauseEnqueue::VisitOMPAlignClause(const OMPAlignClause *C) {
  Visitor->AddStmt(C->getAlignment());
}

void OMPClauseEnqueue::VisitOMPUnifiedAddressClause(
    const OMPUnifiedAddressClause *) {}

void OMPClauseEnqueue::VisitOMPUnifiedSharedMemoryClause(
    const OMPUnifiedSharedMemoryClause *) {}

void OMPClauseEnqueue::VisitOMPReverseOffloadClause(
    const OMPReverseOffloadClause *) {}

void OMPClauseEnqueue::VisitOMPDynamicAllocatorsClause(
    const OMPDynamicAllocatorsClause *) {}

void OMPClauseEnqueue::VisitOMPAtomicDefaultMemOrderClause(
    const OMPAtomicDefaultMemOrderClause *) {}

void OMPClauseEnqueue::VisitOMPAtClause(const OMPAtClause *) {}

void OMPClauseEnqueue::VisitOMPSeverityClause(const OMPSeverityClause *) {}

void OMPClauseEnqueue::VisitOMPMessageClause(const OMPMessageClause *) {}

void OMPClauseEnqueue::VisitOMPDeviceClause(const OMPDeviceClause *C) {
  Visitor->AddStmt(C->getDevice());
}

void OMPClauseEnqueue::VisitOMPNumTeamsClause(const OMPNumTeamsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getNumTeams());
}

void OMPClauseEnqueue::VisitOMPThreadLimitClause(
    const OMPThreadLimitClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getThreadLimit());
}

void OMPClauseEnqueue::VisitOMPPriorityClause(const OMPPriorityClause *C) {
  Visitor->AddStmt(C->getPriority());
}

void OMPClauseEnqueue::VisitOMPGrainsizeClause(const OMPGrainsizeClause *C) {
  Visitor->AddStmt(C->getGrainsize());
}

void OMPClauseEnqueue::VisitOMPNumTasksClause(const OMPNumTasksClause *C) {
  Visitor->AddStmt(C->getNumTasks());
}

void OMPClauseEnqueue::VisitOMPHintClause(const OMPHintClause *C) {
  Visitor->AddStmt(C->getHint());
}

template <typename T> void OMPClauseEnqueue::VisitOMPClauseList(T *Node) {
  for (const auto *I : Node->varlists()) {
    Visitor->AddStmt(I);
  }
}

void OMPClauseEnqueue::VisitOMPInclusiveClause(const OMPInclusiveClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPExclusiveClause(const OMPExclusiveClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPAllocateClause(const OMPAllocateClause *C) {
  VisitOMPClauseList(C);
  Visitor->AddStmt(C->getAllocator());
}
void OMPClauseEnqueue::VisitOMPPrivateClause(const OMPPrivateClause *C) {
  VisitOMPClauseList(C);
  for (const auto *E : C->private_copies()) {
    Visitor->AddStmt(E);
  }
}
void OMPClauseEnqueue::VisitOMPFirstprivateClause(
    const OMPFirstprivateClause *C) {
  VisitOMPClauseList(C);
  VisitOMPClauseWithPreInit(C);
  for (const auto *E : C->private_copies()) {
    Visitor->AddStmt(E);
  }
  for (const auto *E : C->inits()) {
    Visitor->AddStmt(E);
  }
}
void OMPClauseEnqueue::VisitOMPLastprivateClause(
    const OMPLastprivateClause *C) {
  VisitOMPClauseList(C);
  VisitOMPClauseWithPostUpdate(C);
  for (auto *E : C->private_copies()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->source_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->destination_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->assignment_ops()) {
    Visitor->AddStmt(E);
  }
}
void OMPClauseEnqueue::VisitOMPSharedClause(const OMPSharedClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPReductionClause(const OMPReductionClause *C) {
  VisitOMPClauseList(C);
  VisitOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->lhs_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->rhs_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->reduction_ops()) {
    Visitor->AddStmt(E);
  }
  if (C->getModifier() == clang::OMPC_REDUCTION_inscan) {
    for (auto *E : C->copy_ops()) {
      Visitor->AddStmt(E);
    }
    for (auto *E : C->copy_array_temps()) {
      Visitor->AddStmt(E);
    }
    for (auto *E : C->copy_array_elems()) {
      Visitor->AddStmt(E);
    }
  }
}
void OMPClauseEnqueue::VisitOMPTaskReductionClause(
    const OMPTaskReductionClause *C) {
  VisitOMPClauseList(C);
  VisitOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->lhs_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->rhs_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->reduction_ops()) {
    Visitor->AddStmt(E);
  }
}
void OMPClauseEnqueue::VisitOMPInReductionClause(
    const OMPInReductionClause *C) {
  VisitOMPClauseList(C);
  VisitOMPClauseWithPostUpdate(C);
  for (auto *E : C->privates()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->lhs_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->rhs_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->reduction_ops()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->taskgroup_descriptors())
    Visitor->AddStmt(E);
}
void OMPClauseEnqueue::VisitOMPLinearClause(const OMPLinearClause *C) {
  VisitOMPClauseList(C);
  VisitOMPClauseWithPostUpdate(C);
  for (const auto *E : C->privates()) {
    Visitor->AddStmt(E);
  }
  for (const auto *E : C->inits()) {
    Visitor->AddStmt(E);
  }
  for (const auto *E : C->updates()) {
    Visitor->AddStmt(E);
  }
  for (const auto *E : C->finals()) {
    Visitor->AddStmt(E);
  }
  Visitor->AddStmt(C->getStep());
  Visitor->AddStmt(C->getCalcStep());
}
void OMPClauseEnqueue::VisitOMPAlignedClause(const OMPAlignedClause *C) {
  VisitOMPClauseList(C);
  Visitor->AddStmt(C->getAlignment());
}
void OMPClauseEnqueue::VisitOMPCopyinClause(const OMPCopyinClause *C) {
  VisitOMPClauseList(C);
  for (auto *E : C->source_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->destination_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->assignment_ops()) {
    Visitor->AddStmt(E);
  }
}
void OMPClauseEnqueue::VisitOMPCopyprivateClause(
    const OMPCopyprivateClause *C) {
  VisitOMPClauseList(C);
  for (auto *E : C->source_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->destination_exprs()) {
    Visitor->AddStmt(E);
  }
  for (auto *E : C->assignment_ops()) {
    Visitor->AddStmt(E);
  }
}
void OMPClauseEnqueue::VisitOMPFlushClause(const OMPFlushClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPDepobjClause(const OMPDepobjClause *C) {
  Visitor->AddStmt(C->getDepobj());
}
void OMPClauseEnqueue::VisitOMPDependClause(const OMPDependClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPMapClause(const OMPMapClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPDistScheduleClause(
    const OMPDistScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getChunkSize());
}
void OMPClauseEnqueue::VisitOMPDefaultmapClause(
    const OMPDefaultmapClause * /*C*/) {}
void OMPClauseEnqueue::VisitOMPToClause(const OMPToClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPFromClause(const OMPFromClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPUseDevicePtrClause(
    const OMPUseDevicePtrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPUseDeviceAddrClause(
    const OMPUseDeviceAddrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPIsDevicePtrClause(
    const OMPIsDevicePtrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPHasDeviceAddrClause(
    const OMPHasDeviceAddrClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPNontemporalClause(
    const OMPNontemporalClause *C) {
  VisitOMPClauseList(C);
  for (const auto *E : C->private_refs())
    Visitor->AddStmt(E);
}
void OMPClauseEnqueue::VisitOMPOrderClause(const OMPOrderClause *C) {}
void OMPClauseEnqueue::VisitOMPUsesAllocatorsClause(
    const OMPUsesAllocatorsClause *C) {
  for (unsigned I = 0, E = C->getNumberOfAllocators(); I < E; ++I) {
    const OMPUsesAllocatorsClause::Data &D = C->getAllocatorData(I);
    Visitor->AddStmt(D.Allocator);
    Visitor->AddStmt(D.AllocatorTraits);
  }
}
void OMPClauseEnqueue::VisitOMPAffinityClause(const OMPAffinityClause *C) {
  Visitor->AddStmt(C->getModifier());
  for (const Expr *E : C->varlists())
    Visitor->AddStmt(E);
}
void OMPClauseEnqueue::VisitOMPBindClause(const OMPBindClause *C) {}
void OMPClauseEnqueue::VisitOMPXDynCGroupMemClause(
    const OMPXDynCGroupMemClause *C) {
  VisitOMPClauseWithPreInit(C);
  Visitor->AddStmt(C->getSize());
}
void OMPClauseEnqueue::VisitOMPDoacrossClause(const OMPDoacrossClause *C) {
  VisitOMPClauseList(C);
}
void OMPClauseEnqueue::VisitOMPXAttributeClause(const OMPXAttributeClause *C) {
}
void OMPClauseEnqueue::VisitOMPXBareClause(const OMPXBareClause *C) {}

} // namespace

void EnqueueVisitor::EnqueueChildren(const OMPClause *S) {
  unsigned size = WL.size();
  OMPClauseEnqueue Visitor(this);
  Visitor.Visit(S);
  if (size == WL.size())
    return;
  // Now reverse the entries we just added.  This will match the DFS
  // ordering performed by the worklist.
  VisitorWorkList::iterator I = WL.begin() + size, E = WL.end();
  std::reverse(I, E);
}

namespace {
class OpenACCClauseEnqueue : public OpenACCClauseVisitor<OpenACCClauseEnqueue> {
  EnqueueVisitor &Visitor;

public:
  OpenACCClauseEnqueue(EnqueueVisitor &V) : Visitor(V) {}

  void VisitVarList(const OpenACCClauseWithVarList &C) {
    for (Expr *Var : C.getVarList())
      Visitor.AddStmt(Var);
  }

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  void Visit##CLAUSE_NAME##Clause(const OpenACC##CLAUSE_NAME##Clause &C);
#include "clang/Basic/OpenACCClauses.def"
};

void OpenACCClauseEnqueue::VisitDefaultClause(const OpenACCDefaultClause &C) {}
void OpenACCClauseEnqueue::VisitIfClause(const OpenACCIfClause &C) {
  Visitor.AddStmt(C.getConditionExpr());
}
void OpenACCClauseEnqueue::VisitSelfClause(const OpenACCSelfClause &C) {
  if (C.hasConditionExpr())
    Visitor.AddStmt(C.getConditionExpr());
}
void OpenACCClauseEnqueue::VisitNumWorkersClause(
    const OpenACCNumWorkersClause &C) {
  Visitor.AddStmt(C.getIntExpr());
}
void OpenACCClauseEnqueue::VisitVectorLengthClause(
    const OpenACCVectorLengthClause &C) {
  Visitor.AddStmt(C.getIntExpr());
}
void OpenACCClauseEnqueue::VisitNumGangsClause(const OpenACCNumGangsClause &C) {
  for (Expr *IE : C.getIntExprs())
    Visitor.AddStmt(IE);
}

void OpenACCClauseEnqueue::VisitPrivateClause(const OpenACCPrivateClause &C) {
  VisitVarList(C);
}

void OpenACCClauseEnqueue::VisitFirstPrivateClause(
    const OpenACCFirstPrivateClause &C) {
  VisitVarList(C);
}

void OpenACCClauseEnqueue::VisitPresentClause(const OpenACCPresentClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitNoCreateClause(const OpenACCNoCreateClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitCopyClause(const OpenACCCopyClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitCopyInClause(const OpenACCCopyInClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitCopyOutClause(const OpenACCCopyOutClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitCreateClause(const OpenACCCreateClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitAttachClause(const OpenACCAttachClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitDevicePtrClause(
    const OpenACCDevicePtrClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitAsyncClause(const OpenACCAsyncClause &C) {
  if (C.hasIntExpr())
    Visitor.AddStmt(C.getIntExpr());
}
void OpenACCClauseEnqueue::VisitWaitClause(const OpenACCWaitClause &C) {
  if (const Expr *DevNumExpr = C.getDevNumExpr())
    Visitor.AddStmt(DevNumExpr);
  for (Expr *QE : C.getQueueIdExprs())
    Visitor.AddStmt(QE);
}
void OpenACCClauseEnqueue::VisitDeviceTypeClause(
    const OpenACCDeviceTypeClause &C) {}
void OpenACCClauseEnqueue::VisitReductionClause(
    const OpenACCReductionClause &C) {
  VisitVarList(C);
}
void OpenACCClauseEnqueue::VisitAutoClause(const OpenACCAutoClause &C) {}
void OpenACCClauseEnqueue::VisitIndependentClause(
    const OpenACCIndependentClause &C) {}
void OpenACCClauseEnqueue::VisitSeqClause(const OpenACCSeqClause &C) {}
} // namespace

void EnqueueVisitor::EnqueueChildren(const OpenACCClause *C) {
  unsigned size = WL.size();
  OpenACCClauseEnqueue Visitor(*this);
  Visitor.Visit(C);

  if (size == WL.size())
    return;
  // Now reverse the entries we just added.  This will match the DFS
  // ordering performed by the worklist.
  VisitorWorkList::iterator I = WL.begin() + size, E = WL.end();
  std::reverse(I, E);
}

void EnqueueVisitor::EnqueueChildren(const AnnotateAttr *A) {
  unsigned size = WL.size();
  for (const Expr *Arg : A->args()) {
    VisitStmt(Arg);
  }
  if (size == WL.size())
    return;
  // Now reverse the entries we just added.  This will match the DFS
  // ordering performed by the worklist.
  VisitorWorkList::iterator I = WL.begin() + size, E = WL.end();
  std::reverse(I, E);
}

void EnqueueVisitor::VisitAddrLabelExpr(const AddrLabelExpr *E) {
  WL.push_back(LabelRefVisit(E->getLabel(), E->getLabelLoc(), Parent));
}
void EnqueueVisitor::VisitBlockExpr(const BlockExpr *B) {
  AddDecl(B->getBlockDecl());
}
void EnqueueVisitor::VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
  EnqueueChildren(E);
  AddTypeLoc(E->getTypeSourceInfo());
}
void EnqueueVisitor::VisitCompoundStmt(const CompoundStmt *S) {
  for (auto &I : llvm::reverse(S->body()))
    AddStmt(I);
}
void EnqueueVisitor::VisitMSDependentExistsStmt(
    const MSDependentExistsStmt *S) {
  AddStmt(S->getSubStmt());
  AddDeclarationNameInfo(S);
  if (NestedNameSpecifierLoc QualifierLoc = S->getQualifierLoc())
    AddNestedNameSpecifierLoc(QualifierLoc);
}

void EnqueueVisitor::VisitCXXDependentScopeMemberExpr(
    const CXXDependentScopeMemberExpr *E) {
  if (E->hasExplicitTemplateArgs())
    AddExplicitTemplateArgs(E->getTemplateArgs(), E->getNumTemplateArgs());
  AddDeclarationNameInfo(E);
  if (NestedNameSpecifierLoc QualifierLoc = E->getQualifierLoc())
    AddNestedNameSpecifierLoc(QualifierLoc);
  if (!E->isImplicitAccess())
    AddStmt(E->getBase());
}
void EnqueueVisitor::VisitCXXNewExpr(const CXXNewExpr *E) {
  // Enqueue the initializer , if any.
  AddStmt(E->getInitializer());
  // Enqueue the array size, if any.
  AddStmt(E->getArraySize().value_or(nullptr));
  // Enqueue the allocated type.
  AddTypeLoc(E->getAllocatedTypeSourceInfo());
  // Enqueue the placement arguments.
  for (unsigned I = E->getNumPlacementArgs(); I > 0; --I)
    AddStmt(E->getPlacementArg(I - 1));
}
void EnqueueVisitor::VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *CE) {
  for (unsigned I = CE->getNumArgs(); I > 1 /* Yes, this is 1 */; --I)
    AddStmt(CE->getArg(I - 1));
  AddStmt(CE->getCallee());
  AddStmt(CE->getArg(0));
}
void EnqueueVisitor::VisitCXXPseudoDestructorExpr(
    const CXXPseudoDestructorExpr *E) {
  // Visit the name of the type being destroyed.
  AddTypeLoc(E->getDestroyedTypeInfo());
  // Visit the scope type that looks disturbingly like the nested-name-specifier
  // but isn't.
  AddTypeLoc(E->getScopeTypeInfo());
  // Visit the nested-name-specifier.
  if (NestedNameSpecifierLoc QualifierLoc = E->getQualifierLoc())
    AddNestedNameSpecifierLoc(QualifierLoc);
  // Visit base expression.
  AddStmt(E->getBase());
}
void EnqueueVisitor::VisitCXXScalarValueInitExpr(
    const CXXScalarValueInitExpr *E) {
  AddTypeLoc(E->getTypeSourceInfo());
}
void EnqueueVisitor::VisitCXXTemporaryObjectExpr(
    const CXXTemporaryObjectExpr *E) {
  EnqueueChildren(E);
  AddTypeLoc(E->getTypeSourceInfo());
}
void EnqueueVisitor::VisitCXXTypeidExpr(const CXXTypeidExpr *E) {
  EnqueueChildren(E);
  if (E->isTypeOperand())
    AddTypeLoc(E->getTypeOperandSourceInfo());
}

void EnqueueVisitor::VisitCXXUnresolvedConstructExpr(
    const CXXUnresolvedConstructExpr *E) {
  EnqueueChildren(E);
  AddTypeLoc(E->getTypeSourceInfo());
}
void EnqueueVisitor::VisitCXXUuidofExpr(const CXXUuidofExpr *E) {
  EnqueueChildren(E);
  if (E->isTypeOperand())
    AddTypeLoc(E->getTypeOperandSourceInfo());
}

void EnqueueVisitor::VisitCXXCatchStmt(const CXXCatchStmt *S) {
  EnqueueChildren(S);
  AddDecl(S->getExceptionDecl());
}

void EnqueueVisitor::VisitCXXForRangeStmt(const CXXForRangeStmt *S) {
  AddStmt(S->getBody());
  AddStmt(S->getRangeInit());
  AddDecl(S->getLoopVariable());
}

void EnqueueVisitor::VisitDeclRefExpr(const DeclRefExpr *DR) {
  if (DR->hasExplicitTemplateArgs())
    AddExplicitTemplateArgs(DR->getTemplateArgs(), DR->getNumTemplateArgs());
  WL.push_back(DeclRefExprParts(DR, Parent));
}
void EnqueueVisitor::VisitDependentScopeDeclRefExpr(
    const DependentScopeDeclRefExpr *E) {
  if (E->hasExplicitTemplateArgs())
    AddExplicitTemplateArgs(E->getTemplateArgs(), E->getNumTemplateArgs());
  AddDeclarationNameInfo(E);
  AddNestedNameSpecifierLoc(E->getQualifierLoc());
}
void EnqueueVisitor::VisitDeclStmt(const DeclStmt *S) {
  unsigned size = WL.size();
  bool isFirst = true;
  for (const auto *D : S->decls()) {
    AddDecl(D, isFirst);
    isFirst = false;
  }
  if (size == WL.size())
    return;
  // Now reverse the entries we just added.  This will match the DFS
  // ordering performed by the worklist.
  VisitorWorkList::iterator I = WL.begin() + size, E = WL.end();
  std::reverse(I, E);
}
void EnqueueVisitor::VisitDesignatedInitExpr(const DesignatedInitExpr *E) {
  AddStmt(E->getInit());
  for (const DesignatedInitExpr::Designator &D :
       llvm::reverse(E->designators())) {
    if (D.isFieldDesignator()) {
      if (const FieldDecl *Field = D.getFieldDecl())
        AddMemberRef(Field, D.getFieldLoc());
      continue;
    }
    if (D.isArrayDesignator()) {
      AddStmt(E->getArrayIndex(D));
      continue;
    }
    assert(D.isArrayRangeDesignator() && "Unknown designator kind");
    AddStmt(E->getArrayRangeEnd(D));
    AddStmt(E->getArrayRangeStart(D));
  }
}
void EnqueueVisitor::VisitExplicitCastExpr(const ExplicitCastExpr *E) {
  EnqueueChildren(E);
  AddTypeLoc(E->getTypeInfoAsWritten());
}
void EnqueueVisitor::VisitForStmt(const ForStmt *FS) {
  AddStmt(FS->getBody());
  AddStmt(FS->getInc());
  AddStmt(FS->getCond());
  AddDecl(FS->getConditionVariable());
  AddStmt(FS->getInit());
}
void EnqueueVisitor::VisitGotoStmt(const GotoStmt *GS) {
  WL.push_back(LabelRefVisit(GS->getLabel(), GS->getLabelLoc(), Parent));
}
void EnqueueVisitor::VisitIfStmt(const IfStmt *If) {
  AddStmt(If->getElse());
  AddStmt(If->getThen());
  AddStmt(If->getCond());
  AddStmt(If->getInit());
  AddDecl(If->getConditionVariable());
}
void EnqueueVisitor::VisitInitListExpr(const InitListExpr *IE) {
  // We care about the syntactic form of the initializer list, only.
  if (InitListExpr *Syntactic = IE->getSyntacticForm())
    IE = Syntactic;
  EnqueueChildren(IE);
}
void EnqueueVisitor::VisitMemberExpr(const MemberExpr *M) {
  WL.push_back(MemberExprParts(M, Parent));

  // If the base of the member access expression is an implicit 'this', don't
  // visit it.
  // FIXME: If we ever want to show these implicit accesses, this will be
  // unfortunate. However, clang_getCursor() relies on this behavior.
  if (M->isImplicitAccess())
    return;

  // Ignore base anonymous struct/union fields, otherwise they will shadow the
  // real field that we are interested in.
  if (auto *SubME = dyn_cast<MemberExpr>(M->getBase())) {
    if (auto *FD = dyn_cast_or_null<FieldDecl>(SubME->getMemberDecl())) {
      if (FD->isAnonymousStructOrUnion()) {
        AddStmt(SubME->getBase());
        return;
      }
    }
  }

  AddStmt(M->getBase());
}
void EnqueueVisitor::VisitObjCEncodeExpr(const ObjCEncodeExpr *E) {
  AddTypeLoc(E->getEncodedTypeSourceInfo());
}
void EnqueueVisitor::VisitObjCMessageExpr(const ObjCMessageExpr *M) {
  EnqueueChildren(M);
  AddTypeLoc(M->getClassReceiverTypeInfo());
}
void EnqueueVisitor::VisitOffsetOfExpr(const OffsetOfExpr *E) {
  // Visit the components of the offsetof expression.
  for (unsigned N = E->getNumComponents(), I = N; I > 0; --I) {
    const OffsetOfNode &Node = E->getComponent(I - 1);
    switch (Node.getKind()) {
    case OffsetOfNode::Array:
      AddStmt(E->getIndexExpr(Node.getArrayExprIndex()));
      break;
    case OffsetOfNode::Field:
      AddMemberRef(Node.getField(), Node.getSourceRange().getEnd());
      break;
    case OffsetOfNode::Identifier:
    case OffsetOfNode::Base:
      continue;
    }
  }
  // Visit the type into which we're computing the offset.
  AddTypeLoc(E->getTypeSourceInfo());
}
void EnqueueVisitor::VisitOverloadExpr(const OverloadExpr *E) {
  if (E->hasExplicitTemplateArgs())
    AddExplicitTemplateArgs(E->getTemplateArgs(), E->getNumTemplateArgs());
  WL.push_back(OverloadExprParts(E, Parent));
}
void EnqueueVisitor::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *E) {
  EnqueueChildren(E);
  if (E->isArgumentType())
    AddTypeLoc(E->getArgumentTypeInfo());
}
void EnqueueVisitor::VisitStmt(const Stmt *S) { EnqueueChildren(S); }
void EnqueueVisitor::VisitSwitchStmt(const SwitchStmt *S) {
  AddStmt(S->getBody());
  AddStmt(S->getCond());
  AddDecl(S->getConditionVariable());
}

void EnqueueVisitor::VisitWhileStmt(const WhileStmt *W) {
  AddStmt(W->getBody());
  AddStmt(W->getCond());
  AddDecl(W->getConditionVariable());
}

void EnqueueVisitor::VisitTypeTraitExpr(const TypeTraitExpr *E) {
  for (unsigned I = E->getNumArgs(); I > 0; --I)
    AddTypeLoc(E->getArg(I - 1));
}

void EnqueueVisitor::VisitArrayTypeTraitExpr(const ArrayTypeTraitExpr *E) {
  AddTypeLoc(E->getQueriedTypeSourceInfo());
}

void EnqueueVisitor::VisitExpressionTraitExpr(const ExpressionTraitExpr *E) {
  EnqueueChildren(E);
}

void EnqueueVisitor::VisitUnresolvedMemberExpr(const UnresolvedMemberExpr *U) {
  VisitOverloadExpr(U);
  if (!U->isImplicitAccess())
    AddStmt(U->getBase());
}
void EnqueueVisitor::VisitVAArgExpr(const VAArgExpr *E) {
  AddStmt(E->getSubExpr());
  AddTypeLoc(E->getWrittenTypeInfo());
}
void EnqueueVisitor::VisitSizeOfPackExpr(const SizeOfPackExpr *E) {
  WL.push_back(SizeOfPackExprParts(E, Parent));
}
void EnqueueVisitor::VisitOpaqueValueExpr(const OpaqueValueExpr *E) {
  // If the opaque value has a source expression, just transparently
  // visit that.  This is useful for (e.g.) pseudo-object expressions.
  if (Expr *SourceExpr = E->getSourceExpr())
    return ConstStmtVisitor::Visit(SourceExpr);
}
void EnqueueVisitor::VisitLambdaExpr(const LambdaExpr *E) {
  AddStmt(E->getBody());
  WL.push_back(LambdaExprParts(E, Parent));
}
void EnqueueVisitor::VisitConceptSpecializationExpr(
    const ConceptSpecializationExpr *E) {
  WL.push_back(ConceptSpecializationExprVisit(E, Parent));
}
void EnqueueVisitor::VisitRequiresExpr(const RequiresExpr *E) {
  WL.push_back(RequiresExprVisit(E, Parent));
  for (ParmVarDecl *VD : E->getLocalParameters())
    AddDecl(VD);
}
void EnqueueVisitor::VisitCXXParenListInitExpr(const CXXParenListInitExpr *E) {
  EnqueueChildren(E);
}
void EnqueueVisitor::VisitPseudoObjectExpr(const PseudoObjectExpr *E) {
  // Treat the expression like its syntactic form.
  ConstStmtVisitor::Visit(E->getSyntacticForm());
}

void EnqueueVisitor::VisitOMPExecutableDirective(
    const OMPExecutableDirective *D) {
  EnqueueChildren(D);
  for (ArrayRef<OMPClause *>::iterator I = D->clauses().begin(),
                                       E = D->clauses().end();
       I != E; ++I)
    EnqueueChildren(*I);
}

void EnqueueVisitor::VisitOMPLoopBasedDirective(
    const OMPLoopBasedDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPLoopDirective(const OMPLoopDirective *D) {
  VisitOMPLoopBasedDirective(D);
}

void EnqueueVisitor::VisitOMPParallelDirective(const OMPParallelDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPSimdDirective(const OMPSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPLoopTransformationDirective(
    const OMPLoopTransformationDirective *D) {
  VisitOMPLoopBasedDirective(D);
}

void EnqueueVisitor::VisitOMPTileDirective(const OMPTileDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void EnqueueVisitor::VisitOMPUnrollDirective(const OMPUnrollDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void EnqueueVisitor::VisitOMPReverseDirective(const OMPReverseDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void EnqueueVisitor::VisitOMPInterchangeDirective(
    const OMPInterchangeDirective *D) {
  VisitOMPLoopTransformationDirective(D);
}

void EnqueueVisitor::VisitOMPForDirective(const OMPForDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPForSimdDirective(const OMPForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPSectionsDirective(const OMPSectionsDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPSectionDirective(const OMPSectionDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPSingleDirective(const OMPSingleDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPMasterDirective(const OMPMasterDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPCriticalDirective(const OMPCriticalDirective *D) {
  VisitOMPExecutableDirective(D);
  AddDeclarationNameInfo(D);
}

void EnqueueVisitor::VisitOMPParallelForDirective(
    const OMPParallelForDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPParallelForSimdDirective(
    const OMPParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPParallelMasterDirective(
    const OMPParallelMasterDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPParallelMaskedDirective(
    const OMPParallelMaskedDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPParallelSectionsDirective(
    const OMPParallelSectionsDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTaskDirective(const OMPTaskDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTaskyieldDirective(
    const OMPTaskyieldDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPBarrierDirective(const OMPBarrierDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTaskwaitDirective(const OMPTaskwaitDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPErrorDirective(const OMPErrorDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTaskgroupDirective(
    const OMPTaskgroupDirective *D) {
  VisitOMPExecutableDirective(D);
  if (const Expr *E = D->getReductionRef())
    VisitStmt(E);
}

void EnqueueVisitor::VisitOMPFlushDirective(const OMPFlushDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPDepobjDirective(const OMPDepobjDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPScanDirective(const OMPScanDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPOrderedDirective(const OMPOrderedDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPAtomicDirective(const OMPAtomicDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetDirective(const OMPTargetDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetDataDirective(
    const OMPTargetDataDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetEnterDataDirective(
    const OMPTargetEnterDataDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetExitDataDirective(
    const OMPTargetExitDataDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetParallelDirective(
    const OMPTargetParallelDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetParallelForDirective(
    const OMPTargetParallelForDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTeamsDirective(const OMPTeamsDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPCancellationPointDirective(
    const OMPCancellationPointDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPCancelDirective(const OMPCancelDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTaskLoopDirective(const OMPTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTaskLoopSimdDirective(
    const OMPTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPMasterTaskLoopDirective(
    const OMPMasterTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPMaskedTaskLoopDirective(
    const OMPMaskedTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPMasterTaskLoopSimdDirective(
    const OMPMasterTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPMaskedTaskLoopSimdDirective(
    const OMPMaskedTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPParallelMasterTaskLoopDirective(
    const OMPParallelMasterTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPParallelMaskedTaskLoopDirective(
    const OMPParallelMaskedTaskLoopDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPParallelMasterTaskLoopSimdDirective(
    const OMPParallelMasterTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPParallelMaskedTaskLoopSimdDirective(
    const OMPParallelMaskedTaskLoopSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPDistributeDirective(
    const OMPDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPDistributeParallelForDirective(
    const OMPDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPDistributeParallelForSimdDirective(
    const OMPDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPDistributeSimdDirective(
    const OMPDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTargetParallelForSimdDirective(
    const OMPTargetParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTargetSimdDirective(
    const OMPTargetSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTeamsDistributeDirective(
    const OMPTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTeamsDistributeSimdDirective(
    const OMPTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTeamsDistributeParallelForSimdDirective(
    const OMPTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTeamsDistributeParallelForDirective(
    const OMPTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTargetTeamsDirective(
    const OMPTargetTeamsDirective *D) {
  VisitOMPExecutableDirective(D);
}

void EnqueueVisitor::VisitOMPTargetTeamsDistributeDirective(
    const OMPTargetTeamsDistributeDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTargetTeamsDistributeParallelForDirective(
    const OMPTargetTeamsDistributeParallelForDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTargetTeamsDistributeParallelForSimdDirective(
    const OMPTargetTeamsDistributeParallelForSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOMPTargetTeamsDistributeSimdDirective(
    const OMPTargetTeamsDistributeSimdDirective *D) {
  VisitOMPLoopDirective(D);
}

void EnqueueVisitor::VisitOpenACCComputeConstruct(
    const OpenACCComputeConstruct *C) {
  EnqueueChildren(C);
  for (auto *Clause : C->clauses())
    EnqueueChildren(Clause);
}

void EnqueueVisitor::VisitOpenACCLoopConstruct(const OpenACCLoopConstruct *C) {
  EnqueueChildren(C);
  for (auto *Clause : C->clauses())
    EnqueueChildren(Clause);
}

void EnqueueVisitor::VisitAnnotateAttr(const AnnotateAttr *A) {
  EnqueueChildren(A);
}

void CursorVisitor::EnqueueWorkList(VisitorWorkList &WL, const Stmt *S) {
  EnqueueVisitor(WL, MakeCXCursor(S, StmtParent, TU, RegionOfInterest))
      .ConstStmtVisitor::Visit(S);
}

void CursorVisitor::EnqueueWorkList(VisitorWorkList &WL, const Attr *A) {
  // Parent is the attribute itself when this is indirectly called from
  // VisitChildren. Because we need to make a CXCursor for A, we need *its*
  // parent.
  auto AttrCursor = Parent;

  // Get the attribute's parent as stored in
  // cxcursor::MakeCXCursor(const Attr *A, const Decl *Parent, CXTranslationUnit
  // TU)
  const Decl *AttrParent = static_cast<const Decl *>(AttrCursor.data[1]);

  EnqueueVisitor(WL, MakeCXCursor(A, AttrParent, TU))
      .ConstAttrVisitor::Visit(A);
}

bool CursorVisitor::IsInRegionOfInterest(CXCursor C) {
  if (RegionOfInterest.isValid()) {
    SourceRange Range = getRawCursorExtent(C);
    if (Range.isInvalid() || CompareRegionOfInterest(Range))
      return false;
  }
  return true;
}

bool CursorVisitor::RunVisitorWorkList(VisitorWorkList &WL) {
  while (!WL.empty()) {
    // Dequeue the worklist item.
    VisitorJob LI = WL.pop_back_val();

    // Set the Parent field, then back to its old value once we're done.
    SetParentRAII SetParent(Parent, StmtParent, LI.getParent());

    switch (LI.getKind()) {
    case VisitorJob::DeclVisitKind: {
      const Decl *D = cast<DeclVisit>(&LI)->get();
      if (!D)
        continue;

      // For now, perform default visitation for Decls.
      if (Visit(MakeCXCursor(D, TU, RegionOfInterest,
                             cast<DeclVisit>(&LI)->isFirst())))
        return true;

      continue;
    }
    case VisitorJob::ExplicitTemplateArgsVisitKind: {
      for (const TemplateArgumentLoc &Arg :
           *cast<ExplicitTemplateArgsVisit>(&LI)) {
        if (VisitTemplateArgumentLoc(Arg))
          return true;
      }
      continue;
    }
    case VisitorJob::TypeLocVisitKind: {
      // Perform default visitation for TypeLocs.
      if (Visit(cast<TypeLocVisit>(&LI)->get()))
        return true;
      continue;
    }
    case VisitorJob::LabelRefVisitKind: {
      const LabelDecl *LS = cast<LabelRefVisit>(&LI)->get();
      if (LabelStmt *stmt = LS->getStmt()) {
        if (Visit(MakeCursorLabelRef(stmt, cast<LabelRefVisit>(&LI)->getLoc(),
                                     TU))) {
          return true;
        }
      }
      continue;
    }

    case VisitorJob::NestedNameSpecifierLocVisitKind: {
      NestedNameSpecifierLocVisit *V = cast<NestedNameSpecifierLocVisit>(&LI);
      if (VisitNestedNameSpecifierLoc(V->get()))
        return true;
      continue;
    }

    case VisitorJob::DeclarationNameInfoVisitKind: {
      if (VisitDeclarationNameInfo(cast<DeclarationNameInfoVisit>(&LI)->get()))
        return true;
      continue;
    }
    case VisitorJob::MemberRefVisitKind: {
      MemberRefVisit *V = cast<MemberRefVisit>(&LI);
      if (Visit(MakeCursorMemberRef(V->get(), V->getLoc(), TU)))
        return true;
      continue;
    }
    case VisitorJob::StmtVisitKind: {
      const Stmt *S = cast<StmtVisit>(&LI)->get();
      if (!S)
        continue;

      // Update the current cursor.
      CXCursor Cursor = MakeCXCursor(S, StmtParent, TU, RegionOfInterest);
      if (!IsInRegionOfInterest(Cursor))
        continue;
      switch (Visitor(Cursor, Parent, ClientData)) {
      case CXChildVisit_Break:
        return true;
      case CXChildVisit_Continue:
        break;
      case CXChildVisit_Recurse:
        if (PostChildrenVisitor)
          WL.push_back(PostChildrenVisit(nullptr, Cursor));
        EnqueueWorkList(WL, S);
        break;
      }
      continue;
    }
    case VisitorJob::MemberExprPartsKind: {
      // Handle the other pieces in the MemberExpr besides the base.
      const MemberExpr *M = cast<MemberExprParts>(&LI)->get();

      // Visit the nested-name-specifier
      if (NestedNameSpecifierLoc QualifierLoc = M->getQualifierLoc())
        if (VisitNestedNameSpecifierLoc(QualifierLoc))
          return true;

      // Visit the declaration name.
      if (VisitDeclarationNameInfo(M->getMemberNameInfo()))
        return true;

      // Visit the explicitly-specified template arguments, if any.
      if (M->hasExplicitTemplateArgs()) {
        for (const TemplateArgumentLoc *Arg = M->getTemplateArgs(),
                                       *ArgEnd = Arg + M->getNumTemplateArgs();
             Arg != ArgEnd; ++Arg) {
          if (VisitTemplateArgumentLoc(*Arg))
            return true;
        }
      }
      continue;
    }
    case VisitorJob::DeclRefExprPartsKind: {
      const DeclRefExpr *DR = cast<DeclRefExprParts>(&LI)->get();
      // Visit nested-name-specifier, if present.
      if (NestedNameSpecifierLoc QualifierLoc = DR->getQualifierLoc())
        if (VisitNestedNameSpecifierLoc(QualifierLoc))
          return true;
      // Visit declaration name.
      if (VisitDeclarationNameInfo(DR->getNameInfo()))
        return true;
      continue;
    }
    case VisitorJob::OverloadExprPartsKind: {
      const OverloadExpr *O = cast<OverloadExprParts>(&LI)->get();
      // Visit the nested-name-specifier.
      if (NestedNameSpecifierLoc QualifierLoc = O->getQualifierLoc())
        if (VisitNestedNameSpecifierLoc(QualifierLoc))
          return true;
      // Visit the declaration name.
      if (VisitDeclarationNameInfo(O->getNameInfo()))
        return true;
      // Visit the overloaded declaration reference.
      if (Visit(MakeCursorOverloadedDeclRef(O, TU)))
        return true;
      continue;
    }
    case VisitorJob::SizeOfPackExprPartsKind: {
      const SizeOfPackExpr *E = cast<SizeOfPackExprParts>(&LI)->get();
      NamedDecl *Pack = E->getPack();
      if (isa<TemplateTypeParmDecl>(Pack)) {
        if (Visit(MakeCursorTypeRef(cast<TemplateTypeParmDecl>(Pack),
                                    E->getPackLoc(), TU)))
          return true;

        continue;
      }

      if (isa<TemplateTemplateParmDecl>(Pack)) {
        if (Visit(MakeCursorTemplateRef(cast<TemplateTemplateParmDecl>(Pack),
                                        E->getPackLoc(), TU)))
          return true;

        continue;
      }

      // Non-type template parameter packs and function parameter packs are
      // treated like DeclRefExpr cursors.
      continue;
    }

    case VisitorJob::LambdaExprPartsKind: {
      // Visit non-init captures.
      const LambdaExpr *E = cast<LambdaExprParts>(&LI)->get();
      for (LambdaExpr::capture_iterator C = E->explicit_capture_begin(),
                                        CEnd = E->explicit_capture_end();
           C != CEnd; ++C) {
        if (!C->capturesVariable())
          continue;
        // TODO: handle structured bindings here ?
        if (!isa<VarDecl>(C->getCapturedVar()))
          continue;
        if (Visit(MakeCursorVariableRef(cast<VarDecl>(C->getCapturedVar()),
                                        C->getLocation(), TU)))
          return true;
      }
      // Visit init captures
      for (auto InitExpr : E->capture_inits()) {
        if (InitExpr && Visit(InitExpr))
          return true;
      }

      TypeLoc TL = E->getCallOperator()->getTypeSourceInfo()->getTypeLoc();
      // Visit parameters and return type, if present.
      if (FunctionTypeLoc Proto = TL.getAs<FunctionProtoTypeLoc>()) {
        if (E->hasExplicitParameters()) {
          // Visit parameters.
          for (unsigned I = 0, N = Proto.getNumParams(); I != N; ++I)
            if (Visit(MakeCXCursor(Proto.getParam(I), TU)))
              return true;
        }
        if (E->hasExplicitResultType()) {
          // Visit result type.
          if (Visit(Proto.getReturnLoc()))
            return true;
        }
      }
      break;
    }

    case VisitorJob::ConceptSpecializationExprVisitKind: {
      const ConceptSpecializationExpr *E =
          cast<ConceptSpecializationExprVisit>(&LI)->get();
      if (NestedNameSpecifierLoc QualifierLoc =
              E->getNestedNameSpecifierLoc()) {
        if (VisitNestedNameSpecifierLoc(QualifierLoc))
          return true;
      }

      if (E->getNamedConcept() &&
          Visit(MakeCursorTemplateRef(E->getNamedConcept(),
                                      E->getConceptNameLoc(), TU)))
        return true;

      if (auto Args = E->getTemplateArgsAsWritten()) {
        for (const auto &Arg : Args->arguments()) {
          if (VisitTemplateArgumentLoc(Arg))
            return true;
        }
      }
      break;
    }

    case VisitorJob::RequiresExprVisitKind: {
      const RequiresExpr *E = cast<RequiresExprVisit>(&LI)->get();
      for (const concepts::Requirement *R : E->getRequirements())
        VisitConceptRequirement(*R);
      break;
    }

    case VisitorJob::PostChildrenVisitKind:
      if (PostChildrenVisitor(Parent, ClientData))
        return true;
      break;
    }
  }
  return false;
}

bool CursorVisitor::Visit(const Stmt *S) {
  VisitorWorkList *WL = nullptr;
  if (!WorkListFreeList.empty()) {
    WL = WorkListFreeList.back();
    WL->clear();
    WorkListFreeList.pop_back();
  } else {
    WL = new VisitorWorkList();
    WorkListCache.push_back(WL);
  }
  EnqueueWorkList(*WL, S);
  bool result = RunVisitorWorkList(*WL);
  WorkListFreeList.push_back(WL);
  return result;
}

bool CursorVisitor::Visit(const Attr *A) {
  VisitorWorkList *WL = nullptr;
  if (!WorkListFreeList.empty()) {
    WL = WorkListFreeList.back();
    WL->clear();
    WorkListFreeList.pop_back();
  } else {
    WL = new VisitorWorkList();
    WorkListCache.push_back(WL);
  }
  EnqueueWorkList(*WL, A);
  bool result = RunVisitorWorkList(*WL);
  WorkListFreeList.push_back(WL);
  return result;
}

namespace {
typedef SmallVector<SourceRange, 4> RefNamePieces;
RefNamePieces buildPieces(unsigned NameFlags, bool IsMemberRefExpr,
                          const DeclarationNameInfo &NI, SourceRange QLoc,
                          const SourceRange *TemplateArgsLoc = nullptr) {
  const bool WantQualifier = NameFlags & CXNameRange_WantQualifier;
  const bool WantTemplateArgs = NameFlags & CXNameRange_WantTemplateArgs;
  const bool WantSinglePiece = NameFlags & CXNameRange_WantSinglePiece;

  const DeclarationName::NameKind Kind = NI.getName().getNameKind();

  RefNamePieces Pieces;

  if (WantQualifier && QLoc.isValid())
    Pieces.push_back(QLoc);

  if (Kind != DeclarationName::CXXOperatorName || IsMemberRefExpr)
    Pieces.push_back(NI.getLoc());

  if (WantTemplateArgs && TemplateArgsLoc && TemplateArgsLoc->isValid())
    Pieces.push_back(*TemplateArgsLoc);

  if (Kind == DeclarationName::CXXOperatorName) {
    Pieces.push_back(NI.getInfo().getCXXOperatorNameBeginLoc());
    Pieces.push_back(NI.getInfo().getCXXOperatorNameEndLoc());
  }

  if (WantSinglePiece) {
    SourceRange R(Pieces.front().getBegin(), Pieces.back().getEnd());
    Pieces.clear();
    Pieces.push_back(R);
  }

  return Pieces;
}
} // namespace

//===----------------------------------------------------------------------===//
// Misc. API hooks.
//===----------------------------------------------------------------------===//

namespace {
struct RegisterFatalErrorHandler {
  RegisterFatalErrorHandler() {
    clang_install_aborting_llvm_fatal_error_handler();
  }
};
} // namespace

static llvm::ManagedStatic<RegisterFatalErrorHandler>
    RegisterFatalErrorHandlerOnce;

static CIndexer *clang_createIndex_Impl(
    int excludeDeclarationsFromPCH, int displayDiagnostics,
    unsigned char threadBackgroundPriorityForIndexing = CXChoice_Default,
    unsigned char threadBackgroundPriorityForEditing = CXChoice_Default) {
  // We use crash recovery to make some of our APIs more reliable, implicitly
  // enable it.
  if (!getenv("LIBCLANG_DISABLE_CRASH_RECOVERY"))
    llvm::CrashRecoveryContext::Enable();

  // Look through the managed static to trigger construction of the managed
  // static which registers our fatal error handler. This ensures it is only
  // registered once.
  (void)*RegisterFatalErrorHandlerOnce;

  // Initialize targets for clang module support.
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  CIndexer *CIdxr = new CIndexer();

  if (excludeDeclarationsFromPCH)
    CIdxr->setOnlyLocalDecls();
  if (displayDiagnostics)
    CIdxr->setDisplayDiagnostics();

  unsigned GlobalOptions = CIdxr->getCXGlobalOptFlags();
  const auto updateGlobalOption =
      [&GlobalOptions](unsigned char Policy, CXGlobalOptFlags Flag,
                       const char *EnvironmentVariableName) {
        switch (Policy) {
        case CXChoice_Enabled:
          GlobalOptions |= Flag;
          break;
        case CXChoice_Disabled:
          GlobalOptions &= ~Flag;
          break;
        case CXChoice_Default:
        default: // Fall back to default behavior if Policy is unsupported.
          if (getenv(EnvironmentVariableName))
            GlobalOptions |= Flag;
        }
      };
  updateGlobalOption(threadBackgroundPriorityForIndexing,
                     CXGlobalOpt_ThreadBackgroundPriorityForIndexing,
                     "LIBCLANG_BGPRIO_INDEX");
  updateGlobalOption(threadBackgroundPriorityForEditing,
                     CXGlobalOpt_ThreadBackgroundPriorityForEditing,
                     "LIBCLANG_BGPRIO_EDIT");
  CIdxr->setCXGlobalOptFlags(GlobalOptions);

  return CIdxr;
}

CXIndex clang_createIndex(int excludeDeclarationsFromPCH,
                          int displayDiagnostics) {
  return clang_createIndex_Impl(excludeDeclarationsFromPCH, displayDiagnostics);
}

void clang_disposeIndex(CXIndex CIdx) {
  if (CIdx)
    delete static_cast<CIndexer *>(CIdx);
}

CXIndex clang_createIndexWithOptions(const CXIndexOptions *options) {
  // Adding new options to struct CXIndexOptions:
  // 1. If no other new option has been added in the same libclang version,
  // sizeof(CXIndexOptions) must increase for versioning purposes.
  // 2. Options should be added at the end of the struct in order to seamlessly
  // support older struct versions. If options->Size < sizeof(CXIndexOptions),
  // don't attempt to read the missing options and rely on the default values of
  // recently added options being reasonable. For example:
  // if (options->Size >= offsetof(CXIndexOptions, RecentlyAddedMember))
  //   do_something(options->RecentlyAddedMember);

  // An exception: if a new option is small enough, it can be squeezed into the
  // /*Reserved*/ bits in CXIndexOptions. Since the default value of each option
  // is guaranteed to be 0 and the callers are advised to zero out the struct,
  // programs built against older libclang versions would implicitly set the new
  // options to default values, which should keep the behavior of previous
  // libclang versions and thus be backward-compatible.

  // If options->Size > sizeof(CXIndexOptions), the user may have set an option
  // we can't handle, in which case we return nullptr to report failure.
  // Replace `!=` with `>` here to support older struct versions. `!=` has the
  // advantage of catching more usage bugs and no disadvantages while there is a
  // single supported struct version (the initial version).
  if (options->Size != sizeof(CXIndexOptions))
    return nullptr;
  CIndexer *const CIdxr = clang_createIndex_Impl(
      options->ExcludeDeclarationsFromPCH, options->DisplayDiagnostics,
      options->ThreadBackgroundPriorityForIndexing,
      options->ThreadBackgroundPriorityForEditing);
  CIdxr->setStorePreamblesInMemory(options->StorePreamblesInMemory);
  CIdxr->setPreambleStoragePath(options->PreambleStoragePath);
  CIdxr->setInvocationEmissionPath(options->InvocationEmissionPath);
  return CIdxr;
}

void clang_CXIndex_setGlobalOptions(CXIndex CIdx, unsigned options) {
  if (CIdx)
    static_cast<CIndexer *>(CIdx)->setCXGlobalOptFlags(options);
}

unsigned clang_CXIndex_getGlobalOptions(CXIndex CIdx) {
  if (CIdx)
    return static_cast<CIndexer *>(CIdx)->getCXGlobalOptFlags();
  return 0;
}

void clang_CXIndex_setInvocationEmissionPathOption(CXIndex CIdx,
                                                   const char *Path) {
  if (CIdx)
    static_cast<CIndexer *>(CIdx)->setInvocationEmissionPath(Path ? Path : "");
}

void clang_toggleCrashRecovery(unsigned isEnabled) {
  if (isEnabled)
    llvm::CrashRecoveryContext::Enable();
  else
    llvm::CrashRecoveryContext::Disable();
}

CXTranslationUnit clang_createTranslationUnit(CXIndex CIdx,
                                              const char *ast_filename) {
  CXTranslationUnit TU;
  enum CXErrorCode Result =
      clang_createTranslationUnit2(CIdx, ast_filename, &TU);
  (void)Result;
  assert((TU && Result == CXError_Success) ||
         (!TU && Result != CXError_Success));
  return TU;
}

enum CXErrorCode clang_createTranslationUnit2(CXIndex CIdx,
                                              const char *ast_filename,
                                              CXTranslationUnit *out_TU) {
  if (out_TU)
    *out_TU = nullptr;

  if (!CIdx || !ast_filename || !out_TU)
    return CXError_InvalidArguments;

  LOG_FUNC_SECTION { *Log << ast_filename; }

  CIndexer *CXXIdx = static_cast<CIndexer *>(CIdx);
  FileSystemOptions FileSystemOpts;
  auto HSOpts = std::make_shared<HeaderSearchOptions>();

  IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(new DiagnosticOptions());
  std::unique_ptr<ASTUnit> AU = ASTUnit::LoadFromASTFile(
      ast_filename, CXXIdx->getPCHContainerOperations()->getRawReader(),
      ASTUnit::LoadEverything, Diags, FileSystemOpts, HSOpts,
      /*LangOpts=*/nullptr, CXXIdx->getOnlyLocalDecls(), CaptureDiagsKind::All,
      /*AllowASTWithCompilerErrors=*/true,
      /*UserFilesAreVolatile=*/true);
  *out_TU = MakeCXTranslationUnit(CXXIdx, std::move(AU));
  return *out_TU ? CXError_Success : CXError_Failure;
}

unsigned clang_defaultEditingTranslationUnitOptions() {
  return CXTranslationUnit_PrecompiledPreamble |
         CXTranslationUnit_CacheCompletionResults;
}

CXTranslationUnit clang_createTranslationUnitFromSourceFile(
    CXIndex CIdx, const char *source_filename, int num_command_line_args,
    const char *const *command_line_args, unsigned num_unsaved_files,
    struct CXUnsavedFile *unsaved_files) {
  unsigned Options = CXTranslationUnit_DetailedPreprocessingRecord;
  return clang_parseTranslationUnit(CIdx, source_filename, command_line_args,
                                    num_command_line_args, unsaved_files,
                                    num_unsaved_files, Options);
}

static CXErrorCode
clang_parseTranslationUnit_Impl(CXIndex CIdx, const char *source_filename,
                                const char *const *command_line_args,
                                int num_command_line_args,
                                ArrayRef<CXUnsavedFile> unsaved_files,
                                unsigned options, CXTranslationUnit *out_TU) {
  // Set up the initial return values.
  if (out_TU)
    *out_TU = nullptr;

  // Check arguments.
  if (!CIdx || !out_TU)
    return CXError_InvalidArguments;

  CIndexer *CXXIdx = static_cast<CIndexer *>(CIdx);

  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForIndexing))
    setThreadBackgroundPriority();

  bool PrecompilePreamble = options & CXTranslationUnit_PrecompiledPreamble;
  bool CreatePreambleOnFirstParse =
      options & CXTranslationUnit_CreatePreambleOnFirstParse;
  // FIXME: Add a flag for modules.
  TranslationUnitKind TUKind = (options & (CXTranslationUnit_Incomplete |
                                           CXTranslationUnit_SingleFileParse))
                                   ? TU_Prefix
                                   : TU_Complete;
  bool CacheCodeCompletionResults =
      options & CXTranslationUnit_CacheCompletionResults;
  bool IncludeBriefCommentsInCodeCompletion =
      options & CXTranslationUnit_IncludeBriefCommentsInCodeCompletion;
  bool SingleFileParse = options & CXTranslationUnit_SingleFileParse;
  bool ForSerialization = options & CXTranslationUnit_ForSerialization;
  bool RetainExcludedCB =
      options & CXTranslationUnit_RetainExcludedConditionalBlocks;
  SkipFunctionBodiesScope SkipFunctionBodies = SkipFunctionBodiesScope::None;
  if (options & CXTranslationUnit_SkipFunctionBodies) {
    SkipFunctionBodies =
        (options & CXTranslationUnit_LimitSkipFunctionBodiesToPreamble)
            ? SkipFunctionBodiesScope::Preamble
            : SkipFunctionBodiesScope::PreambleAndMainFile;
  }

  // Configure the diagnostics.
  std::unique_ptr<DiagnosticOptions> DiagOpts = CreateAndPopulateDiagOpts(
      llvm::ArrayRef(command_line_args, num_command_line_args));
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      CompilerInstance::createDiagnostics(DiagOpts.release()));

  if (options & CXTranslationUnit_KeepGoing)
    Diags->setFatalsAsError(true);

  CaptureDiagsKind CaptureDiagnostics = CaptureDiagsKind::All;
  if (options & CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles)
    CaptureDiagnostics = CaptureDiagsKind::AllWithoutNonErrorsFromIncludes;

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<
      DiagnosticsEngine,
      llvm::CrashRecoveryContextReleaseRefCleanup<DiagnosticsEngine>>
      DiagCleanup(Diags.get());

  std::unique_ptr<std::vector<ASTUnit::RemappedFile>> RemappedFiles(
      new std::vector<ASTUnit::RemappedFile>());

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<std::vector<ASTUnit::RemappedFile>>
      RemappedCleanup(RemappedFiles.get());

  for (auto &UF : unsaved_files) {
    std::unique_ptr<llvm::MemoryBuffer> MB =
        llvm::MemoryBuffer::getMemBufferCopy(getContents(UF), UF.Filename);
    RemappedFiles->push_back(std::make_pair(UF.Filename, MB.release()));
  }

  std::unique_ptr<std::vector<const char *>> Args(
      new std::vector<const char *>());

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<std::vector<const char *>>
      ArgsCleanup(Args.get());

  // Since the Clang C library is primarily used by batch tools dealing with
  // (often very broken) source code, where spell-checking can have a
  // significant negative impact on performance (particularly when
  // precompiled headers are involved), we disable it by default.
  // Only do this if we haven't found a spell-checking-related argument.
  bool FoundSpellCheckingArgument = false;
  for (int I = 0; I != num_command_line_args; ++I) {
    if (strcmp(command_line_args[I], "-fno-spell-checking") == 0 ||
        strcmp(command_line_args[I], "-fspell-checking") == 0) {
      FoundSpellCheckingArgument = true;
      break;
    }
  }
  Args->insert(Args->end(), command_line_args,
               command_line_args + num_command_line_args);

  if (!FoundSpellCheckingArgument)
    Args->insert(Args->begin() + 1, "-fno-spell-checking");

  // The 'source_filename' argument is optional.  If the caller does not
  // specify it then it is assumed that the source file is specified
  // in the actual argument list.
  // Put the source file after command_line_args otherwise if '-x' flag is
  // present it will be unused.
  if (source_filename)
    Args->push_back(source_filename);

  // Do we need the detailed preprocessing record?
  if (options & CXTranslationUnit_DetailedPreprocessingRecord) {
    Args->push_back("-Xclang");
    Args->push_back("-detailed-preprocessing-record");
  }

  // Suppress any editor placeholder diagnostics.
  Args->push_back("-fallow-editor-placeholders");

  unsigned NumErrors = Diags->getClient()->getNumErrors();
  std::unique_ptr<ASTUnit> ErrUnit;
  // Unless the user specified that they want the preamble on the first parse
  // set it up to be created on the first reparse. This makes the first parse
  // faster, trading for a slower (first) reparse.
  unsigned PrecompilePreambleAfterNParses =
      !PrecompilePreamble ? 0 : 2 - CreatePreambleOnFirstParse;

  LibclangInvocationReporter InvocationReporter(
      *CXXIdx, LibclangInvocationReporter::OperationKind::ParseOperation,
      options, llvm::ArrayRef(*Args), /*InvocationArgs=*/std::nullopt,
      unsaved_files);
  std::unique_ptr<ASTUnit> Unit = ASTUnit::LoadFromCommandLine(
      Args->data(), Args->data() + Args->size(),
      CXXIdx->getPCHContainerOperations(), Diags,
      CXXIdx->getClangResourcesPath(), CXXIdx->getStorePreamblesInMemory(),
      CXXIdx->getPreambleStoragePath(), CXXIdx->getOnlyLocalDecls(),
      CaptureDiagnostics, *RemappedFiles.get(),
      /*RemappedFilesKeepOriginalName=*/true, PrecompilePreambleAfterNParses,
      TUKind, CacheCodeCompletionResults, IncludeBriefCommentsInCodeCompletion,
      /*AllowPCHWithCompilerErrors=*/true, SkipFunctionBodies, SingleFileParse,
      /*UserFilesAreVolatile=*/true, ForSerialization, RetainExcludedCB,
      CXXIdx->getPCHContainerOperations()->getRawReader().getFormats().front(),
      &ErrUnit);

  // Early failures in LoadFromCommandLine may return with ErrUnit unset.
  if (!Unit && !ErrUnit)
    return CXError_ASTReadError;

  if (NumErrors != Diags->getClient()->getNumErrors()) {
    // Make sure to check that 'Unit' is non-NULL.
    if (CXXIdx->getDisplayDiagnostics())
      printDiagsToStderr(Unit ? Unit.get() : ErrUnit.get());
  }

  if (isASTReadError(Unit ? Unit.get() : ErrUnit.get()))
    return CXError_ASTReadError;

  *out_TU = MakeCXTranslationUnit(CXXIdx, std::move(Unit));
  if (CXTranslationUnitImpl *TU = *out_TU) {
    TU->ParsingOptions = options;
    TU->Arguments.reserve(Args->size());
    for (const char *Arg : *Args)
      TU->Arguments.push_back(Arg);
    return CXError_Success;
  }
  return CXError_Failure;
}

CXTranslationUnit
clang_parseTranslationUnit(CXIndex CIdx, const char *source_filename,
                           const char *const *command_line_args,
                           int num_command_line_args,
                           struct CXUnsavedFile *unsaved_files,
                           unsigned num_unsaved_files, unsigned options) {
  CXTranslationUnit TU;
  enum CXErrorCode Result = clang_parseTranslationUnit2(
      CIdx, source_filename, command_line_args, num_command_line_args,
      unsaved_files, num_unsaved_files, options, &TU);
  (void)Result;
  assert((TU && Result == CXError_Success) ||
         (!TU && Result != CXError_Success));
  return TU;
}

enum CXErrorCode clang_parseTranslationUnit2(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options, CXTranslationUnit *out_TU) {
  noteBottomOfStack();
  SmallVector<const char *, 4> Args;
  Args.push_back("clang");
  Args.append(command_line_args, command_line_args + num_command_line_args);
  return clang_parseTranslationUnit2FullArgv(
      CIdx, source_filename, Args.data(), Args.size(), unsaved_files,
      num_unsaved_files, options, out_TU);
}

enum CXErrorCode clang_parseTranslationUnit2FullArgv(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options, CXTranslationUnit *out_TU) {
  LOG_FUNC_SECTION {
    *Log << source_filename << ": ";
    for (int i = 0; i != num_command_line_args; ++i)
      *Log << command_line_args[i] << " ";
  }

  if (num_unsaved_files && !unsaved_files)
    return CXError_InvalidArguments;

  CXErrorCode result = CXError_Failure;
  auto ParseTranslationUnitImpl = [=, &result] {
    noteBottomOfStack();
    result = clang_parseTranslationUnit_Impl(
        CIdx, source_filename, command_line_args, num_command_line_args,
        llvm::ArrayRef(unsaved_files, num_unsaved_files), options, out_TU);
  };

  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, ParseTranslationUnitImpl)) {
    fprintf(stderr, "libclang: crash detected during parsing: {\n");
    fprintf(stderr, "  'source_filename' : '%s'\n", source_filename);
    fprintf(stderr, "  'command_line_args' : [");
    for (int i = 0; i != num_command_line_args; ++i) {
      if (i)
        fprintf(stderr, ", ");
      fprintf(stderr, "'%s'", command_line_args[i]);
    }
    fprintf(stderr, "],\n");
    fprintf(stderr, "  'unsaved_files' : [");
    for (unsigned i = 0; i != num_unsaved_files; ++i) {
      if (i)
        fprintf(stderr, ", ");
      fprintf(stderr, "('%s', '...', %ld)", unsaved_files[i].Filename,
              unsaved_files[i].Length);
    }
    fprintf(stderr, "],\n");
    fprintf(stderr, "  'options' : %d,\n", options);
    fprintf(stderr, "}\n");

    return CXError_Crashed;
  } else if (getenv("LIBCLANG_RESOURCE_USAGE")) {
    if (CXTranslationUnit *TU = out_TU)
      PrintLibclangResourceUsage(*TU);
  }

  return result;
}

CXString clang_Type_getObjCEncoding(CXType CT) {
  CXTranslationUnit tu = static_cast<CXTranslationUnit>(CT.data[1]);
  ASTContext &Ctx = getASTUnit(tu)->getASTContext();
  std::string encoding;
  Ctx.getObjCEncodingForType(QualType::getFromOpaquePtr(CT.data[0]), encoding);

  return cxstring::createDup(encoding);
}

static const IdentifierInfo *getMacroIdentifier(CXCursor C) {
  if (C.kind == CXCursor_MacroDefinition) {
    if (const MacroDefinitionRecord *MDR = getCursorMacroDefinition(C))
      return MDR->getName();
  } else if (C.kind == CXCursor_MacroExpansion) {
    MacroExpansionCursor ME = getCursorMacroExpansion(C);
    return ME.getName();
  }
  return nullptr;
}

unsigned clang_Cursor_isMacroFunctionLike(CXCursor C) {
  const IdentifierInfo *II = getMacroIdentifier(C);
  if (!II) {
    return false;
  }
  ASTUnit *ASTU = getCursorASTUnit(C);
  Preprocessor &PP = ASTU->getPreprocessor();
  if (const MacroInfo *MI = PP.getMacroInfo(II))
    return MI->isFunctionLike();
  return false;
}

unsigned clang_Cursor_isMacroBuiltin(CXCursor C) {
  const IdentifierInfo *II = getMacroIdentifier(C);
  if (!II) {
    return false;
  }
  ASTUnit *ASTU = getCursorASTUnit(C);
  Preprocessor &PP = ASTU->getPreprocessor();
  if (const MacroInfo *MI = PP.getMacroInfo(II))
    return MI->isBuiltinMacro();
  return false;
}

unsigned clang_Cursor_isFunctionInlined(CXCursor C) {
  const Decl *D = getCursorDecl(C);
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD) {
    return false;
  }
  return FD->isInlined();
}

static StringLiteral *getCFSTR_value(CallExpr *callExpr) {
  if (callExpr->getNumArgs() != 1) {
    return nullptr;
  }

  StringLiteral *S = nullptr;
  auto *arg = callExpr->getArg(0);
  if (arg->getStmtClass() == Stmt::ImplicitCastExprClass) {
    ImplicitCastExpr *I = static_cast<ImplicitCastExpr *>(arg);
    auto *subExpr = I->getSubExprAsWritten();

    if (subExpr->getStmtClass() != Stmt::StringLiteralClass) {
      return nullptr;
    }

    S = static_cast<StringLiteral *>(I->getSubExprAsWritten());
  } else if (arg->getStmtClass() == Stmt::StringLiteralClass) {
    S = static_cast<StringLiteral *>(callExpr->getArg(0));
  } else {
    return nullptr;
  }
  return S;
}

struct ExprEvalResult {
  CXEvalResultKind EvalType;
  union {
    unsigned long long unsignedVal;
    long long intVal;
    double floatVal;
    char *stringVal;
  } EvalData;
  bool IsUnsignedInt;
  ~ExprEvalResult() {
    if (EvalType != CXEval_UnExposed && EvalType != CXEval_Float &&
        EvalType != CXEval_Int) {
      delete[] EvalData.stringVal;
    }
  }
};

void clang_EvalResult_dispose(CXEvalResult E) {
  delete static_cast<ExprEvalResult *>(E);
}

CXEvalResultKind clang_EvalResult_getKind(CXEvalResult E) {
  if (!E) {
    return CXEval_UnExposed;
  }
  return ((ExprEvalResult *)E)->EvalType;
}

int clang_EvalResult_getAsInt(CXEvalResult E) {
  return clang_EvalResult_getAsLongLong(E);
}

long long clang_EvalResult_getAsLongLong(CXEvalResult E) {
  if (!E) {
    return 0;
  }
  ExprEvalResult *Result = (ExprEvalResult *)E;
  if (Result->IsUnsignedInt)
    return Result->EvalData.unsignedVal;
  return Result->EvalData.intVal;
}

unsigned clang_EvalResult_isUnsignedInt(CXEvalResult E) {
  return ((ExprEvalResult *)E)->IsUnsignedInt;
}

unsigned long long clang_EvalResult_getAsUnsigned(CXEvalResult E) {
  if (!E) {
    return 0;
  }

  ExprEvalResult *Result = (ExprEvalResult *)E;
  if (Result->IsUnsignedInt)
    return Result->EvalData.unsignedVal;
  return Result->EvalData.intVal;
}

double clang_EvalResult_getAsDouble(CXEvalResult E) {
  if (!E) {
    return 0;
  }
  return ((ExprEvalResult *)E)->EvalData.floatVal;
}

const char *clang_EvalResult_getAsStr(CXEvalResult E) {
  if (!E) {
    return nullptr;
  }
  return ((ExprEvalResult *)E)->EvalData.stringVal;
}

static const ExprEvalResult *evaluateExpr(Expr *expr, CXCursor C) {
  Expr::EvalResult ER;
  ASTContext &ctx = getCursorContext(C);
  if (!expr)
    return nullptr;

  expr = expr->IgnoreParens();
  if (expr->isValueDependent())
    return nullptr;
  if (!expr->EvaluateAsRValue(ER, ctx))
    return nullptr;

  QualType rettype;
  CallExpr *callExpr;
  auto result = std::make_unique<ExprEvalResult>();
  result->EvalType = CXEval_UnExposed;
  result->IsUnsignedInt = false;

  if (ER.Val.isInt()) {
    result->EvalType = CXEval_Int;

    auto &val = ER.Val.getInt();
    if (val.isUnsigned()) {
      result->IsUnsignedInt = true;
      result->EvalData.unsignedVal = val.getZExtValue();
    } else {
      result->EvalData.intVal = val.getExtValue();
    }

    return result.release();
  }

  if (ER.Val.isFloat()) {
    llvm::SmallVector<char, 100> Buffer;
    ER.Val.getFloat().toString(Buffer);
    std::string floatStr(Buffer.data(), Buffer.size());
    result->EvalType = CXEval_Float;
    bool ignored;
    llvm::APFloat apFloat = ER.Val.getFloat();
    apFloat.convert(llvm::APFloat::IEEEdouble(),
                    llvm::APFloat::rmNearestTiesToEven, &ignored);
    result->EvalData.floatVal = apFloat.convertToDouble();
    return result.release();
  }

  if (expr->getStmtClass() == Stmt::ImplicitCastExprClass) {
    const auto *I = cast<ImplicitCastExpr>(expr);
    auto *subExpr = I->getSubExprAsWritten();
    if (subExpr->getStmtClass() == Stmt::StringLiteralClass ||
        subExpr->getStmtClass() == Stmt::ObjCStringLiteralClass) {
      const StringLiteral *StrE = nullptr;
      const ObjCStringLiteral *ObjCExpr;
      ObjCExpr = dyn_cast<ObjCStringLiteral>(subExpr);

      if (ObjCExpr) {
        StrE = ObjCExpr->getString();
        result->EvalType = CXEval_ObjCStrLiteral;
      } else {
        StrE = cast<StringLiteral>(I->getSubExprAsWritten());
        result->EvalType = CXEval_StrLiteral;
      }

      std::string strRef(StrE->getString().str());
      result->EvalData.stringVal = new char[strRef.size() + 1];
      strncpy((char *)result->EvalData.stringVal, strRef.c_str(),
              strRef.size());
      result->EvalData.stringVal[strRef.size()] = '\0';
      return result.release();
    }
  } else if (expr->getStmtClass() == Stmt::ObjCStringLiteralClass ||
             expr->getStmtClass() == Stmt::StringLiteralClass) {
    const StringLiteral *StrE = nullptr;
    const ObjCStringLiteral *ObjCExpr;
    ObjCExpr = dyn_cast<ObjCStringLiteral>(expr);

    if (ObjCExpr) {
      StrE = ObjCExpr->getString();
      result->EvalType = CXEval_ObjCStrLiteral;
    } else {
      StrE = cast<StringLiteral>(expr);
      result->EvalType = CXEval_StrLiteral;
    }

    std::string strRef(StrE->getString().str());
    result->EvalData.stringVal = new char[strRef.size() + 1];
    strncpy((char *)result->EvalData.stringVal, strRef.c_str(), strRef.size());
    result->EvalData.stringVal[strRef.size()] = '\0';
    return result.release();
  }

  if (expr->getStmtClass() == Stmt::CStyleCastExprClass) {
    CStyleCastExpr *CC = static_cast<CStyleCastExpr *>(expr);

    rettype = CC->getType();
    if (rettype.getAsString() == "CFStringRef" &&
        CC->getSubExpr()->getStmtClass() == Stmt::CallExprClass) {

      callExpr = static_cast<CallExpr *>(CC->getSubExpr());
      StringLiteral *S = getCFSTR_value(callExpr);
      if (S) {
        std::string strLiteral(S->getString().str());
        result->EvalType = CXEval_CFStr;

        result->EvalData.stringVal = new char[strLiteral.size() + 1];
        strncpy((char *)result->EvalData.stringVal, strLiteral.c_str(),
                strLiteral.size());
        result->EvalData.stringVal[strLiteral.size()] = '\0';
        return result.release();
      }
    }

  } else if (expr->getStmtClass() == Stmt::CallExprClass) {
    callExpr = static_cast<CallExpr *>(expr);
    rettype = callExpr->getCallReturnType(ctx);

    if (rettype->isVectorType() || callExpr->getNumArgs() > 1)
      return nullptr;

    if (rettype->isIntegralType(ctx) || rettype->isRealFloatingType()) {
      if (callExpr->getNumArgs() == 1 &&
          !callExpr->getArg(0)->getType()->isIntegralType(ctx))
        return nullptr;
    } else if (rettype.getAsString() == "CFStringRef") {

      StringLiteral *S = getCFSTR_value(callExpr);
      if (S) {
        std::string strLiteral(S->getString().str());
        result->EvalType = CXEval_CFStr;
        result->EvalData.stringVal = new char[strLiteral.size() + 1];
        strncpy((char *)result->EvalData.stringVal, strLiteral.c_str(),
                strLiteral.size());
        result->EvalData.stringVal[strLiteral.size()] = '\0';
        return result.release();
      }
    }
  } else if (expr->getStmtClass() == Stmt::DeclRefExprClass) {
    DeclRefExpr *D = static_cast<DeclRefExpr *>(expr);
    ValueDecl *V = D->getDecl();
    if (V->getKind() == Decl::Function) {
      std::string strName = V->getNameAsString();
      result->EvalType = CXEval_Other;
      result->EvalData.stringVal = new char[strName.size() + 1];
      strncpy(result->EvalData.stringVal, strName.c_str(), strName.size());
      result->EvalData.stringVal[strName.size()] = '\0';
      return result.release();
    }
  }

  return nullptr;
}

static const Expr *evaluateDeclExpr(const Decl *D) {
  if (!D)
    return nullptr;
  if (auto *Var = dyn_cast<VarDecl>(D))
    return Var->getInit();
  else if (auto *Field = dyn_cast<FieldDecl>(D))
    return Field->getInClassInitializer();
  return nullptr;
}

static const Expr *evaluateCompoundStmtExpr(const CompoundStmt *CS) {
  assert(CS && "invalid compound statement");
  for (auto *bodyIterator : CS->body()) {
    if (const auto *E = dyn_cast<Expr>(bodyIterator))
      return E;
  }
  return nullptr;
}

CXEvalResult clang_Cursor_Evaluate(CXCursor C) {
  const Expr *E = nullptr;
  if (clang_getCursorKind(C) == CXCursor_CompoundStmt)
    E = evaluateCompoundStmtExpr(cast<CompoundStmt>(getCursorStmt(C)));
  else if (clang_isDeclaration(C.kind))
    E = evaluateDeclExpr(getCursorDecl(C));
  else if (clang_isExpression(C.kind))
    E = getCursorExpr(C);
  if (E)
    return const_cast<CXEvalResult>(
        reinterpret_cast<const void *>(evaluateExpr(const_cast<Expr *>(E), C)));
  return nullptr;
}

unsigned clang_Cursor_hasAttrs(CXCursor C) {
  const Decl *D = getCursorDecl(C);
  if (!D) {
    return 0;
  }

  if (D->hasAttrs()) {
    return 1;
  }

  return 0;
}
unsigned clang_defaultSaveOptions(CXTranslationUnit TU) {
  return CXSaveTranslationUnit_None;
}

static CXSaveError clang_saveTranslationUnit_Impl(CXTranslationUnit TU,
                                                  const char *FileName,
                                                  unsigned options) {
  CIndexer *CXXIdx = TU->CIdx;
  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForIndexing))
    setThreadBackgroundPriority();

  bool hadError = cxtu::getASTUnit(TU)->Save(FileName);
  return hadError ? CXSaveError_Unknown : CXSaveError_None;
}

int clang_saveTranslationUnit(CXTranslationUnit TU, const char *FileName,
                              unsigned options) {
  LOG_FUNC_SECTION { *Log << TU << ' ' << FileName; }

  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return CXSaveError_InvalidTU;
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  ASTUnit::ConcurrencyCheck Check(*CXXUnit);
  if (!CXXUnit->hasSema())
    return CXSaveError_InvalidTU;

  CXSaveError result;
  auto SaveTranslationUnitImpl = [=, &result]() {
    result = clang_saveTranslationUnit_Impl(TU, FileName, options);
  };

  if (!CXXUnit->getDiagnostics().hasUnrecoverableErrorOccurred()) {
    SaveTranslationUnitImpl();

    if (getenv("LIBCLANG_RESOURCE_USAGE"))
      PrintLibclangResourceUsage(TU);

    return result;
  }

  // We have an AST that has invalid nodes due to compiler errors.
  // Use a crash recovery thread for protection.

  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, SaveTranslationUnitImpl)) {
    fprintf(stderr, "libclang: crash detected during AST saving: {\n");
    fprintf(stderr, "  'filename' : '%s'\n", FileName);
    fprintf(stderr, "  'options' : %d,\n", options);
    fprintf(stderr, "}\n");

    return CXSaveError_Unknown;

  } else if (getenv("LIBCLANG_RESOURCE_USAGE")) {
    PrintLibclangResourceUsage(TU);
  }

  return result;
}

void clang_disposeTranslationUnit(CXTranslationUnit CTUnit) {
  if (CTUnit) {
    // If the translation unit has been marked as unsafe to free, just discard
    // it.
    ASTUnit *Unit = cxtu::getASTUnit(CTUnit);
    if (Unit && Unit->isUnsafeToFree())
      return;

    delete cxtu::getASTUnit(CTUnit);
    delete CTUnit->StringPool;
    delete static_cast<CXDiagnosticSetImpl *>(CTUnit->Diagnostics);
    disposeOverridenCXCursorsPool(CTUnit->OverridenCursorsPool);
    delete CTUnit->CommentToXML;
    delete CTUnit;
  }
}

unsigned clang_suspendTranslationUnit(CXTranslationUnit CTUnit) {
  if (CTUnit) {
    ASTUnit *Unit = cxtu::getASTUnit(CTUnit);

    if (Unit && Unit->isUnsafeToFree())
      return false;

    Unit->ResetForParse();
    return true;
  }

  return false;
}

unsigned clang_defaultReparseOptions(CXTranslationUnit TU) {
  return CXReparse_None;
}

static CXErrorCode
clang_reparseTranslationUnit_Impl(CXTranslationUnit TU,
                                  ArrayRef<CXUnsavedFile> unsaved_files,
                                  unsigned options) {
  // Check arguments.
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return CXError_InvalidArguments;
  }

  // Reset the associated diagnostics.
  delete static_cast<CXDiagnosticSetImpl *>(TU->Diagnostics);
  TU->Diagnostics = nullptr;

  CIndexer *CXXIdx = TU->CIdx;
  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForEditing))
    setThreadBackgroundPriority();

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  std::unique_ptr<std::vector<ASTUnit::RemappedFile>> RemappedFiles(
      new std::vector<ASTUnit::RemappedFile>());

  // Recover resources if we crash before exiting this function.
  llvm::CrashRecoveryContextCleanupRegistrar<std::vector<ASTUnit::RemappedFile>>
      RemappedCleanup(RemappedFiles.get());

  for (auto &UF : unsaved_files) {
    std::unique_ptr<llvm::MemoryBuffer> MB =
        llvm::MemoryBuffer::getMemBufferCopy(getContents(UF), UF.Filename);
    RemappedFiles->push_back(std::make_pair(UF.Filename, MB.release()));
  }

  if (!CXXUnit->Reparse(CXXIdx->getPCHContainerOperations(),
                        *RemappedFiles.get()))
    return CXError_Success;
  if (isASTReadError(CXXUnit))
    return CXError_ASTReadError;
  return CXError_Failure;
}

int clang_reparseTranslationUnit(CXTranslationUnit TU,
                                 unsigned num_unsaved_files,
                                 struct CXUnsavedFile *unsaved_files,
                                 unsigned options) {
  LOG_FUNC_SECTION { *Log << TU; }

  if (num_unsaved_files && !unsaved_files)
    return CXError_InvalidArguments;

  CXErrorCode result;
  auto ReparseTranslationUnitImpl = [=, &result]() {
    result = clang_reparseTranslationUnit_Impl(
        TU, llvm::ArrayRef(unsaved_files, num_unsaved_files), options);
  };

  llvm::CrashRecoveryContext CRC;

  if (!RunSafely(CRC, ReparseTranslationUnitImpl)) {
    fprintf(stderr, "libclang: crash detected during reparsing\n");
    cxtu::getASTUnit(TU)->setUnsafeToFree(true);
    return CXError_Crashed;
  } else if (getenv("LIBCLANG_RESOURCE_USAGE"))
    PrintLibclangResourceUsage(TU);

  return result;
}

CXString clang_getTranslationUnitSpelling(CXTranslationUnit CTUnit) {
  if (isNotUsableTU(CTUnit)) {
    LOG_BAD_TU(CTUnit);
    return cxstring::createEmpty();
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(CTUnit);
  return cxstring::createDup(CXXUnit->getOriginalSourceFileName());
}

CXCursor clang_getTranslationUnitCursor(CXTranslationUnit TU) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return clang_getNullCursor();
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  return MakeCXCursor(CXXUnit->getASTContext().getTranslationUnitDecl(), TU);
}

CXTargetInfo clang_getTranslationUnitTargetInfo(CXTranslationUnit CTUnit) {
  if (isNotUsableTU(CTUnit)) {
    LOG_BAD_TU(CTUnit);
    return nullptr;
  }

  CXTargetInfoImpl *impl = new CXTargetInfoImpl();
  impl->TranslationUnit = CTUnit;
  return impl;
}

CXString clang_TargetInfo_getTriple(CXTargetInfo TargetInfo) {
  if (!TargetInfo)
    return cxstring::createEmpty();

  CXTranslationUnit CTUnit = TargetInfo->TranslationUnit;
  assert(!isNotUsableTU(CTUnit) &&
         "Unexpected unusable translation unit in TargetInfo");

  ASTUnit *CXXUnit = cxtu::getASTUnit(CTUnit);
  std::string Triple =
      CXXUnit->getASTContext().getTargetInfo().getTriple().normalize();
  return cxstring::createDup(Triple);
}

int clang_TargetInfo_getPointerWidth(CXTargetInfo TargetInfo) {
  if (!TargetInfo)
    return -1;

  CXTranslationUnit CTUnit = TargetInfo->TranslationUnit;
  assert(!isNotUsableTU(CTUnit) &&
         "Unexpected unusable translation unit in TargetInfo");

  ASTUnit *CXXUnit = cxtu::getASTUnit(CTUnit);
  return CXXUnit->getASTContext().getTargetInfo().getMaxPointerWidth();
}

void clang_TargetInfo_dispose(CXTargetInfo TargetInfo) {
  if (!TargetInfo)
    return;

  delete TargetInfo;
}

//===----------------------------------------------------------------------===//
// CXFile Operations.
//===----------------------------------------------------------------------===//

CXString clang_getFileName(CXFile SFile) {
  if (!SFile)
    return cxstring::createNull();

  FileEntryRef FEnt = *cxfile::getFileEntryRef(SFile);
  return cxstring::createRef(FEnt.getName());
}

time_t clang_getFileTime(CXFile SFile) {
  if (!SFile)
    return 0;

  FileEntryRef FEnt = *cxfile::getFileEntryRef(SFile);
  return FEnt.getModificationTime();
}

CXFile clang_getFile(CXTranslationUnit TU, const char *file_name) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return nullptr;
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);

  FileManager &FMgr = CXXUnit->getFileManager();
  return cxfile::makeCXFile(FMgr.getOptionalFileRef(file_name));
}

const char *clang_getFileContents(CXTranslationUnit TU, CXFile file,
                                  size_t *size) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return nullptr;
  }

  const SourceManager &SM = cxtu::getASTUnit(TU)->getSourceManager();
  FileID fid = SM.translateFile(*cxfile::getFileEntryRef(file));
  std::optional<llvm::MemoryBufferRef> buf = SM.getBufferOrNone(fid);
  if (!buf) {
    if (size)
      *size = 0;
    return nullptr;
  }
  if (size)
    *size = buf->getBufferSize();
  return buf->getBufferStart();
}

unsigned clang_isFileMultipleIncludeGuarded(CXTranslationUnit TU, CXFile file) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return 0;
  }

  if (!file)
    return 0;

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  FileEntryRef FEnt = *cxfile::getFileEntryRef(file);
  return CXXUnit->getPreprocessor()
      .getHeaderSearchInfo()
      .isFileMultipleIncludeGuarded(FEnt);
}

int clang_getFileUniqueID(CXFile file, CXFileUniqueID *outID) {
  if (!file || !outID)
    return 1;

  FileEntryRef FEnt = *cxfile::getFileEntryRef(file);
  const llvm::sys::fs::UniqueID &ID = FEnt.getUniqueID();
  outID->data[0] = ID.getDevice();
  outID->data[1] = ID.getFile();
  outID->data[2] = FEnt.getModificationTime();
  return 0;
}

int clang_File_isEqual(CXFile file1, CXFile file2) {
  if (file1 == file2)
    return true;

  if (!file1 || !file2)
    return false;

  FileEntryRef FEnt1 = *cxfile::getFileEntryRef(file1);
  FileEntryRef FEnt2 = *cxfile::getFileEntryRef(file2);
  return FEnt1.getUniqueID() == FEnt2.getUniqueID();
}

CXString clang_File_tryGetRealPathName(CXFile SFile) {
  if (!SFile)
    return cxstring::createNull();

  FileEntryRef FEnt = *cxfile::getFileEntryRef(SFile);
  return cxstring::createRef(FEnt.getFileEntry().tryGetRealPathName());
}

//===----------------------------------------------------------------------===//
// CXCursor Operations.
//===----------------------------------------------------------------------===//

static const Decl *getDeclFromExpr(const Stmt *E) {
  if (const ImplicitCastExpr *CE = dyn_cast<ImplicitCastExpr>(E))
    return getDeclFromExpr(CE->getSubExpr());

  if (const DeclRefExpr *RefExpr = dyn_cast<DeclRefExpr>(E))
    return RefExpr->getDecl();
  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  if (const ObjCIvarRefExpr *RE = dyn_cast<ObjCIvarRefExpr>(E))
    return RE->getDecl();
  if (const ObjCPropertyRefExpr *PRE = dyn_cast<ObjCPropertyRefExpr>(E)) {
    if (PRE->isExplicitProperty())
      return PRE->getExplicitProperty();
    // It could be messaging both getter and setter as in:
    // ++myobj.myprop;
    // in which case prefer to associate the setter since it is less obvious
    // from inspecting the source that the setter is going to get called.
    if (PRE->isMessagingSetter())
      return PRE->getImplicitPropertySetter();
    return PRE->getImplicitPropertyGetter();
  }
  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E))
    return getDeclFromExpr(POE->getSyntacticForm());
  if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E))
    if (Expr *Src = OVE->getSourceExpr())
      return getDeclFromExpr(Src);

  if (const CallExpr *CE = dyn_cast<CallExpr>(E))
    return getDeclFromExpr(CE->getCallee());
  if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(E))
    if (!CE->isElidable())
      return CE->getConstructor();
  if (const CXXInheritedCtorInitExpr *CE =
          dyn_cast<CXXInheritedCtorInitExpr>(E))
    return CE->getConstructor();
  if (const ObjCMessageExpr *OME = dyn_cast<ObjCMessageExpr>(E))
    return OME->getMethodDecl();

  if (const ObjCProtocolExpr *PE = dyn_cast<ObjCProtocolExpr>(E))
    return PE->getProtocol();
  if (const SubstNonTypeTemplateParmPackExpr *NTTP =
          dyn_cast<SubstNonTypeTemplateParmPackExpr>(E))
    return NTTP->getParameterPack();
  if (const SizeOfPackExpr *SizeOfPack = dyn_cast<SizeOfPackExpr>(E))
    if (isa<NonTypeTemplateParmDecl>(SizeOfPack->getPack()) ||
        isa<ParmVarDecl>(SizeOfPack->getPack()))
      return SizeOfPack->getPack();

  return nullptr;
}

static SourceLocation getLocationFromExpr(const Expr *E) {
  if (const ImplicitCastExpr *CE = dyn_cast<ImplicitCastExpr>(E))
    return getLocationFromExpr(CE->getSubExpr());

  if (const ObjCMessageExpr *Msg = dyn_cast<ObjCMessageExpr>(E))
    return /*FIXME:*/ Msg->getLeftLoc();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getLocation();
  if (const MemberExpr *Member = dyn_cast<MemberExpr>(E))
    return Member->getMemberLoc();
  if (const ObjCIvarRefExpr *Ivar = dyn_cast<ObjCIvarRefExpr>(E))
    return Ivar->getLocation();
  if (const SizeOfPackExpr *SizeOfPack = dyn_cast<SizeOfPackExpr>(E))
    return SizeOfPack->getPackLoc();
  if (const ObjCPropertyRefExpr *PropRef = dyn_cast<ObjCPropertyRefExpr>(E))
    return PropRef->getLocation();

  return E->getBeginLoc();
}

extern "C" {

unsigned clang_visitChildren(CXCursor parent, CXCursorVisitor visitor,
                             CXClientData client_data) {
  CursorVisitor CursorVis(getCursorTU(parent), visitor, client_data,
                          /*VisitPreprocessorLast=*/false);
  return CursorVis.VisitChildren(parent);
}

#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if __has_feature(blocks)
typedef enum CXChildVisitResult (^CXCursorVisitorBlock)(CXCursor cursor,
                                                        CXCursor parent);

static enum CXChildVisitResult visitWithBlock(CXCursor cursor, CXCursor parent,
                                              CXClientData client_data) {
  CXCursorVisitorBlock block = (CXCursorVisitorBlock)client_data;
  return block(cursor, parent);
}
#else
// If we are compiled with a compiler that doesn't have native blocks support,
// define and call the block manually, so the
typedef struct _CXChildVisitResult {
  void *isa;
  int flags;
  int reserved;
  enum CXChildVisitResult (*invoke)(struct _CXChildVisitResult *, CXCursor,
                                    CXCursor);
} * CXCursorVisitorBlock;

static enum CXChildVisitResult visitWithBlock(CXCursor cursor, CXCursor parent,
                                              CXClientData client_data) {
  CXCursorVisitorBlock block = (CXCursorVisitorBlock)client_data;
  return block->invoke(block, cursor, parent);
}
#endif

unsigned clang_visitChildrenWithBlock(CXCursor parent,
                                      CXCursorVisitorBlock block) {
  return clang_visitChildren(parent, visitWithBlock, block);
}

static CXString getDeclSpelling(const Decl *D) {
  if (!D)
    return cxstring::createEmpty();

  const NamedDecl *ND = dyn_cast<NamedDecl>(D);
  if (!ND) {
    if (const ObjCPropertyImplDecl *PropImpl =
            dyn_cast<ObjCPropertyImplDecl>(D))
      if (ObjCPropertyDecl *Property = PropImpl->getPropertyDecl())
        return cxstring::createDup(Property->getIdentifier()->getName());

    if (const ImportDecl *ImportD = dyn_cast<ImportDecl>(D))
      if (Module *Mod = ImportD->getImportedModule())
        return cxstring::createDup(Mod->getFullModuleName());

    return cxstring::createEmpty();
  }

  if (const ObjCMethodDecl *OMD = dyn_cast<ObjCMethodDecl>(ND))
    return cxstring::createDup(OMD->getSelector().getAsString());

  if (const ObjCCategoryImplDecl *CIMP = dyn_cast<ObjCCategoryImplDecl>(ND))
    // No, this isn't the same as the code below. getIdentifier() is non-virtual
    // and returns different names. NamedDecl returns the class name and
    // ObjCCategoryImplDecl returns the category name.
    return cxstring::createRef(CIMP->getIdentifier()->getNameStart());

  if (isa<UsingDirectiveDecl>(D))
    return cxstring::createEmpty();

  SmallString<1024> S;
  llvm::raw_svector_ostream os(S);
  ND->printName(os);

  return cxstring::createDup(os.str());
}

CXString clang_getCursorSpelling(CXCursor C) {
  if (clang_isTranslationUnit(C.kind))
    return clang_getTranslationUnitSpelling(getCursorTU(C));

  if (clang_isReference(C.kind)) {
    switch (C.kind) {
    case CXCursor_ObjCSuperClassRef: {
      const ObjCInterfaceDecl *Super = getCursorObjCSuperClassRef(C).first;
      return cxstring::createRef(Super->getIdentifier()->getNameStart());
    }
    case CXCursor_ObjCClassRef: {
      const ObjCInterfaceDecl *Class = getCursorObjCClassRef(C).first;
      return cxstring::createRef(Class->getIdentifier()->getNameStart());
    }
    case CXCursor_ObjCProtocolRef: {
      const ObjCProtocolDecl *OID = getCursorObjCProtocolRef(C).first;
      assert(OID && "getCursorSpelling(): Missing protocol decl");
      return cxstring::createRef(OID->getIdentifier()->getNameStart());
    }
    case CXCursor_CXXBaseSpecifier: {
      const CXXBaseSpecifier *B = getCursorCXXBaseSpecifier(C);
      return cxstring::createDup(B->getType().getAsString());
    }
    case CXCursor_TypeRef: {
      const TypeDecl *Type = getCursorTypeRef(C).first;
      assert(Type && "Missing type decl");

      return cxstring::createDup(
          getCursorContext(C).getTypeDeclType(Type).getAsString());
    }
    case CXCursor_TemplateRef: {
      const TemplateDecl *Template = getCursorTemplateRef(C).first;
      assert(Template && "Missing template decl");

      return cxstring::createDup(Template->getNameAsString());
    }

    case CXCursor_NamespaceRef: {
      const NamedDecl *NS = getCursorNamespaceRef(C).first;
      assert(NS && "Missing namespace decl");

      return cxstring::createDup(NS->getNameAsString());
    }

    case CXCursor_MemberRef: {
      const FieldDecl *Field = getCursorMemberRef(C).first;
      assert(Field && "Missing member decl");

      return cxstring::createDup(Field->getNameAsString());
    }

    case CXCursor_LabelRef: {
      const LabelStmt *Label = getCursorLabelRef(C).first;
      assert(Label && "Missing label");

      return cxstring::createRef(Label->getName());
    }

    case CXCursor_OverloadedDeclRef: {
      OverloadedDeclRefStorage Storage = getCursorOverloadedDeclRef(C).first;
      if (const Decl *D = Storage.dyn_cast<const Decl *>()) {
        if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
          return cxstring::createDup(ND->getNameAsString());
        return cxstring::createEmpty();
      }
      if (const OverloadExpr *E = Storage.dyn_cast<const OverloadExpr *>())
        return cxstring::createDup(E->getName().getAsString());
      OverloadedTemplateStorage *Ovl =
          Storage.get<OverloadedTemplateStorage *>();
      if (Ovl->size() == 0)
        return cxstring::createEmpty();
      return cxstring::createDup((*Ovl->begin())->getNameAsString());
    }

    case CXCursor_VariableRef: {
      const VarDecl *Var = getCursorVariableRef(C).first;
      assert(Var && "Missing variable decl");

      return cxstring::createDup(Var->getNameAsString());
    }

    default:
      return cxstring::createRef("<not implemented>");
    }
  }

  if (clang_isExpression(C.kind)) {
    const Expr *E = getCursorExpr(C);

    if (C.kind == CXCursor_ObjCStringLiteral ||
        C.kind == CXCursor_StringLiteral) {
      const StringLiteral *SLit;
      if (const ObjCStringLiteral *OSL = dyn_cast<ObjCStringLiteral>(E)) {
        SLit = OSL->getString();
      } else {
        SLit = cast<StringLiteral>(E);
      }
      SmallString<256> Buf;
      llvm::raw_svector_ostream OS(Buf);
      SLit->outputString(OS);
      return cxstring::createDup(OS.str());
    }

    if (C.kind == CXCursor_BinaryOperator ||
        C.kind == CXCursor_CompoundAssignOperator) {
      return clang_Cursor_getBinaryOpcodeStr(clang_Cursor_getBinaryOpcode(C));
    }

    const Decl *D = getDeclFromExpr(getCursorExpr(C));
    if (D)
      return getDeclSpelling(D);
    return cxstring::createEmpty();
  }

  if (clang_isStatement(C.kind)) {
    const Stmt *S = getCursorStmt(C);
    if (const LabelStmt *Label = dyn_cast_or_null<LabelStmt>(S))
      return cxstring::createRef(Label->getName());

    return cxstring::createEmpty();
  }

  if (C.kind == CXCursor_MacroExpansion)
    return cxstring::createRef(
        getCursorMacroExpansion(C).getName()->getNameStart());

  if (C.kind == CXCursor_MacroDefinition)
    return cxstring::createRef(
        getCursorMacroDefinition(C)->getName()->getNameStart());

  if (C.kind == CXCursor_InclusionDirective)
    return cxstring::createDup(getCursorInclusionDirective(C)->getFileName());

  if (clang_isDeclaration(C.kind))
    return getDeclSpelling(getCursorDecl(C));

  if (C.kind == CXCursor_AnnotateAttr) {
    const AnnotateAttr *AA = cast<AnnotateAttr>(cxcursor::getCursorAttr(C));
    return cxstring::createDup(AA->getAnnotation());
  }

  if (C.kind == CXCursor_AsmLabelAttr) {
    const AsmLabelAttr *AA = cast<AsmLabelAttr>(cxcursor::getCursorAttr(C));
    return cxstring::createDup(AA->getLabel());
  }

  if (C.kind == CXCursor_PackedAttr) {
    return cxstring::createRef("packed");
  }

  if (C.kind == CXCursor_VisibilityAttr) {
    const VisibilityAttr *AA = cast<VisibilityAttr>(cxcursor::getCursorAttr(C));
    switch (AA->getVisibility()) {
    case VisibilityAttr::VisibilityType::Default:
      return cxstring::createRef("default");
    case VisibilityAttr::VisibilityType::Hidden:
      return cxstring::createRef("hidden");
    case VisibilityAttr::VisibilityType::Protected:
      return cxstring::createRef("protected");
    }
    llvm_unreachable("unknown visibility type");
  }

  return cxstring::createEmpty();
}

CXSourceRange clang_Cursor_getSpellingNameRange(CXCursor C, unsigned pieceIndex,
                                                unsigned options) {
  if (clang_Cursor_isNull(C))
    return clang_getNullRange();

  ASTContext &Ctx = getCursorContext(C);

  if (clang_isStatement(C.kind)) {
    const Stmt *S = getCursorStmt(C);
    if (const LabelStmt *Label = dyn_cast_or_null<LabelStmt>(S)) {
      if (pieceIndex > 0)
        return clang_getNullRange();
      return cxloc::translateSourceRange(Ctx, Label->getIdentLoc());
    }

    return clang_getNullRange();
  }

  if (C.kind == CXCursor_ObjCMessageExpr) {
    if (const ObjCMessageExpr *ME =
            dyn_cast_or_null<ObjCMessageExpr>(getCursorExpr(C))) {
      if (pieceIndex >= ME->getNumSelectorLocs())
        return clang_getNullRange();
      return cxloc::translateSourceRange(Ctx, ME->getSelectorLoc(pieceIndex));
    }
  }

  if (C.kind == CXCursor_ObjCInstanceMethodDecl ||
      C.kind == CXCursor_ObjCClassMethodDecl) {
    if (const ObjCMethodDecl *MD =
            dyn_cast_or_null<ObjCMethodDecl>(getCursorDecl(C))) {
      if (pieceIndex >= MD->getNumSelectorLocs())
        return clang_getNullRange();
      return cxloc::translateSourceRange(Ctx, MD->getSelectorLoc(pieceIndex));
    }
  }

  if (C.kind == CXCursor_ObjCCategoryDecl ||
      C.kind == CXCursor_ObjCCategoryImplDecl) {
    if (pieceIndex > 0)
      return clang_getNullRange();
    if (const ObjCCategoryDecl *CD =
            dyn_cast_or_null<ObjCCategoryDecl>(getCursorDecl(C)))
      return cxloc::translateSourceRange(Ctx, CD->getCategoryNameLoc());
    if (const ObjCCategoryImplDecl *CID =
            dyn_cast_or_null<ObjCCategoryImplDecl>(getCursorDecl(C)))
      return cxloc::translateSourceRange(Ctx, CID->getCategoryNameLoc());
  }

  if (C.kind == CXCursor_ModuleImportDecl) {
    if (pieceIndex > 0)
      return clang_getNullRange();
    if (const ImportDecl *ImportD =
            dyn_cast_or_null<ImportDecl>(getCursorDecl(C))) {
      ArrayRef<SourceLocation> Locs = ImportD->getIdentifierLocs();
      if (!Locs.empty())
        return cxloc::translateSourceRange(
            Ctx, SourceRange(Locs.front(), Locs.back()));
    }
    return clang_getNullRange();
  }

  if (C.kind == CXCursor_CXXMethod || C.kind == CXCursor_Destructor ||
      C.kind == CXCursor_ConversionFunction ||
      C.kind == CXCursor_FunctionDecl) {
    if (pieceIndex > 0)
      return clang_getNullRange();
    if (const FunctionDecl *FD =
            dyn_cast_or_null<FunctionDecl>(getCursorDecl(C))) {
      DeclarationNameInfo FunctionName = FD->getNameInfo();
      return cxloc::translateSourceRange(Ctx, FunctionName.getSourceRange());
    }
    return clang_getNullRange();
  }

  // FIXME: A CXCursor_InclusionDirective should give the location of the
  // filename, but we don't keep track of this.

  // FIXME: A CXCursor_AnnotateAttr should give the location of the annotation
  // but we don't keep track of this.

  // FIXME: A CXCursor_AsmLabelAttr should give the location of the label
  // but we don't keep track of this.

  // Default handling, give the location of the cursor.

  if (pieceIndex > 0)
    return clang_getNullRange();

  CXSourceLocation CXLoc = clang_getCursorLocation(C);
  SourceLocation Loc = cxloc::translateSourceLocation(CXLoc);
  return cxloc::translateSourceRange(Ctx, Loc);
}

CXString clang_Cursor_getMangling(CXCursor C) {
  if (clang_isInvalid(C.kind) || !clang_isDeclaration(C.kind))
    return cxstring::createEmpty();

  // Mangling only works for functions and variables.
  const Decl *D = getCursorDecl(C);
  if (!D || !(isa<FunctionDecl>(D) || isa<VarDecl>(D)))
    return cxstring::createEmpty();

  ASTContext &Ctx = D->getASTContext();
  ASTNameGenerator ASTNameGen(Ctx);
  return cxstring::createDup(ASTNameGen.getName(D));
}

CXStringSet *clang_Cursor_getCXXManglings(CXCursor C) {
  if (clang_isInvalid(C.kind) || !clang_isDeclaration(C.kind))
    return nullptr;

  const Decl *D = getCursorDecl(C);
  if (!(isa<CXXRecordDecl>(D) || isa<CXXMethodDecl>(D)))
    return nullptr;

  ASTContext &Ctx = D->getASTContext();
  ASTNameGenerator ASTNameGen(Ctx);
  std::vector<std::string> Manglings = ASTNameGen.getAllManglings(D);
  return cxstring::createSet(Manglings);
}

CXStringSet *clang_Cursor_getObjCManglings(CXCursor C) {
  if (clang_isInvalid(C.kind) || !clang_isDeclaration(C.kind))
    return nullptr;

  const Decl *D = getCursorDecl(C);
  if (!(isa<ObjCInterfaceDecl>(D) || isa<ObjCImplementationDecl>(D)))
    return nullptr;

  ASTContext &Ctx = D->getASTContext();
  ASTNameGenerator ASTNameGen(Ctx);
  std::vector<std::string> Manglings = ASTNameGen.getAllManglings(D);
  return cxstring::createSet(Manglings);
}

CXPrintingPolicy clang_getCursorPrintingPolicy(CXCursor C) {
  if (clang_Cursor_isNull(C))
    return nullptr;
  return new PrintingPolicy(getCursorContext(C).getPrintingPolicy());
}

void clang_PrintingPolicy_dispose(CXPrintingPolicy Policy) {
  if (Policy)
    delete static_cast<PrintingPolicy *>(Policy);
}

unsigned
clang_PrintingPolicy_getProperty(CXPrintingPolicy Policy,
                                 enum CXPrintingPolicyProperty Property) {
  if (!Policy)
    return 0;

  PrintingPolicy *P = static_cast<PrintingPolicy *>(Policy);
  switch (Property) {
  case CXPrintingPolicy_Indentation:
    return P->Indentation;
  case CXPrintingPolicy_SuppressSpecifiers:
    return P->SuppressSpecifiers;
  case CXPrintingPolicy_SuppressTagKeyword:
    return P->SuppressTagKeyword;
  case CXPrintingPolicy_IncludeTagDefinition:
    return P->IncludeTagDefinition;
  case CXPrintingPolicy_SuppressScope:
    return P->SuppressScope;
  case CXPrintingPolicy_SuppressUnwrittenScope:
    return P->SuppressUnwrittenScope;
  case CXPrintingPolicy_SuppressInitializers:
    return P->SuppressInitializers;
  case CXPrintingPolicy_ConstantArraySizeAsWritten:
    return P->ConstantArraySizeAsWritten;
  case CXPrintingPolicy_AnonymousTagLocations:
    return P->AnonymousTagLocations;
  case CXPrintingPolicy_SuppressStrongLifetime:
    return P->SuppressStrongLifetime;
  case CXPrintingPolicy_SuppressLifetimeQualifiers:
    return P->SuppressLifetimeQualifiers;
  case CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors:
    return P->SuppressTemplateArgsInCXXConstructors;
  case CXPrintingPolicy_Bool:
    return P->Bool;
  case CXPrintingPolicy_Restrict:
    return P->Restrict;
  case CXPrintingPolicy_Alignof:
    return P->Alignof;
  case CXPrintingPolicy_UnderscoreAlignof:
    return P->UnderscoreAlignof;
  case CXPrintingPolicy_UseVoidForZeroParams:
    return P->UseVoidForZeroParams;
  case CXPrintingPolicy_TerseOutput:
    return P->TerseOutput;
  case CXPrintingPolicy_PolishForDeclaration:
    return P->PolishForDeclaration;
  case CXPrintingPolicy_Half:
    return P->Half;
  case CXPrintingPolicy_MSWChar:
    return P->MSWChar;
  case CXPrintingPolicy_IncludeNewlines:
    return P->IncludeNewlines;
  case CXPrintingPolicy_MSVCFormatting:
    return P->MSVCFormatting;
  case CXPrintingPolicy_ConstantsAsWritten:
    return P->ConstantsAsWritten;
  case CXPrintingPolicy_SuppressImplicitBase:
    return P->SuppressImplicitBase;
  case CXPrintingPolicy_FullyQualifiedName:
    return P->FullyQualifiedName;
  }

  assert(false && "Invalid CXPrintingPolicyProperty");
  return 0;
}

void clang_PrintingPolicy_setProperty(CXPrintingPolicy Policy,
                                      enum CXPrintingPolicyProperty Property,
                                      unsigned Value) {
  if (!Policy)
    return;

  PrintingPolicy *P = static_cast<PrintingPolicy *>(Policy);
  switch (Property) {
  case CXPrintingPolicy_Indentation:
    P->Indentation = Value;
    return;
  case CXPrintingPolicy_SuppressSpecifiers:
    P->SuppressSpecifiers = Value;
    return;
  case CXPrintingPolicy_SuppressTagKeyword:
    P->SuppressTagKeyword = Value;
    return;
  case CXPrintingPolicy_IncludeTagDefinition:
    P->IncludeTagDefinition = Value;
    return;
  case CXPrintingPolicy_SuppressScope:
    P->SuppressScope = Value;
    return;
  case CXPrintingPolicy_SuppressUnwrittenScope:
    P->SuppressUnwrittenScope = Value;
    return;
  case CXPrintingPolicy_SuppressInitializers:
    P->SuppressInitializers = Value;
    return;
  case CXPrintingPolicy_ConstantArraySizeAsWritten:
    P->ConstantArraySizeAsWritten = Value;
    return;
  case CXPrintingPolicy_AnonymousTagLocations:
    P->AnonymousTagLocations = Value;
    return;
  case CXPrintingPolicy_SuppressStrongLifetime:
    P->SuppressStrongLifetime = Value;
    return;
  case CXPrintingPolicy_SuppressLifetimeQualifiers:
    P->SuppressLifetimeQualifiers = Value;
    return;
  case CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors:
    P->SuppressTemplateArgsInCXXConstructors = Value;
    return;
  case CXPrintingPolicy_Bool:
    P->Bool = Value;
    return;
  case CXPrintingPolicy_Restrict:
    P->Restrict = Value;
    return;
  case CXPrintingPolicy_Alignof:
    P->Alignof = Value;
    return;
  case CXPrintingPolicy_UnderscoreAlignof:
    P->UnderscoreAlignof = Value;
    return;
  case CXPrintingPolicy_UseVoidForZeroParams:
    P->UseVoidForZeroParams = Value;
    return;
  case CXPrintingPolicy_TerseOutput:
    P->TerseOutput = Value;
    return;
  case CXPrintingPolicy_PolishForDeclaration:
    P->PolishForDeclaration = Value;
    return;
  case CXPrintingPolicy_Half:
    P->Half = Value;
    return;
  case CXPrintingPolicy_MSWChar:
    P->MSWChar = Value;
    return;
  case CXPrintingPolicy_IncludeNewlines:
    P->IncludeNewlines = Value;
    return;
  case CXPrintingPolicy_MSVCFormatting:
    P->MSVCFormatting = Value;
    return;
  case CXPrintingPolicy_ConstantsAsWritten:
    P->ConstantsAsWritten = Value;
    return;
  case CXPrintingPolicy_SuppressImplicitBase:
    P->SuppressImplicitBase = Value;
    return;
  case CXPrintingPolicy_FullyQualifiedName:
    P->FullyQualifiedName = Value;
    return;
  }

  assert(false && "Invalid CXPrintingPolicyProperty");
}

CXString clang_getCursorPrettyPrinted(CXCursor C, CXPrintingPolicy cxPolicy) {
  if (clang_Cursor_isNull(C))
    return cxstring::createEmpty();

  if (clang_isDeclaration(C.kind)) {
    const Decl *D = getCursorDecl(C);
    if (!D)
      return cxstring::createEmpty();

    SmallString<128> Str;
    llvm::raw_svector_ostream OS(Str);
    PrintingPolicy *UserPolicy = static_cast<PrintingPolicy *>(cxPolicy);
    D->print(OS, UserPolicy ? *UserPolicy
                            : getCursorContext(C).getPrintingPolicy());

    return cxstring::createDup(OS.str());
  }

  return cxstring::createEmpty();
}

CXString clang_getCursorDisplayName(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return clang_getCursorSpelling(C);

  const Decl *D = getCursorDecl(C);
  if (!D)
    return cxstring::createEmpty();

  PrintingPolicy Policy = getCursorContext(C).getPrintingPolicy();
  if (const FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(D))
    D = FunTmpl->getTemplatedDecl();

  if (const FunctionDecl *Function = dyn_cast<FunctionDecl>(D)) {
    SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << *Function;
    if (Function->getPrimaryTemplate())
      OS << "<>";
    OS << "(";
    for (unsigned I = 0, N = Function->getNumParams(); I != N; ++I) {
      if (I)
        OS << ", ";
      OS << Function->getParamDecl(I)->getType().getAsString(Policy);
    }

    if (Function->isVariadic()) {
      if (Function->getNumParams())
        OS << ", ";
      OS << "...";
    }
    OS << ")";
    return cxstring::createDup(OS.str());
  }

  if (const ClassTemplateDecl *ClassTemplate = dyn_cast<ClassTemplateDecl>(D)) {
    SmallString<64> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << *ClassTemplate;
    OS << "<";
    TemplateParameterList *Params = ClassTemplate->getTemplateParameters();
    for (unsigned I = 0, N = Params->size(); I != N; ++I) {
      if (I)
        OS << ", ";

      NamedDecl *Param = Params->getParam(I);
      if (Param->getIdentifier()) {
        OS << Param->getIdentifier()->getName();
        continue;
      }

      // There is no parameter name, which makes this tricky. Try to come up
      // with something useful that isn't too long.
      if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(Param))
        if (const auto *TC = TTP->getTypeConstraint()) {
          TC->getConceptNameInfo().printName(OS, Policy);
          if (TC->hasExplicitTemplateArgs())
            OS << "<...>";
        } else
          OS << (TTP->wasDeclaredWithTypename() ? "typename" : "class");
      else if (NonTypeTemplateParmDecl *NTTP =
                   dyn_cast<NonTypeTemplateParmDecl>(Param))
        OS << NTTP->getType().getAsString(Policy);
      else
        OS << "template<...> class";
    }

    OS << ">";
    return cxstring::createDup(OS.str());
  }

  if (const ClassTemplateSpecializationDecl *ClassSpec =
          dyn_cast<ClassTemplateSpecializationDecl>(D)) {
    SmallString<128> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << *ClassSpec;
    // If the template arguments were written explicitly, use them..
    if (const auto *ArgsWritten = ClassSpec->getTemplateArgsAsWritten()) {
      printTemplateArgumentList(
          OS, ArgsWritten->arguments(), Policy,
          ClassSpec->getSpecializedTemplate()->getTemplateParameters());
    } else {
      printTemplateArgumentList(
          OS, ClassSpec->getTemplateArgs().asArray(), Policy,
          ClassSpec->getSpecializedTemplate()->getTemplateParameters());
    }
    return cxstring::createDup(OS.str());
  }

  return clang_getCursorSpelling(C);
}

CXString clang_getCursorKindSpelling(enum CXCursorKind Kind) {
  switch (Kind) {
  case CXCursor_FunctionDecl:
    return cxstring::createRef("FunctionDecl");
  case CXCursor_TypedefDecl:
    return cxstring::createRef("TypedefDecl");
  case CXCursor_EnumDecl:
    return cxstring::createRef("EnumDecl");
  case CXCursor_EnumConstantDecl:
    return cxstring::createRef("EnumConstantDecl");
  case CXCursor_StructDecl:
    return cxstring::createRef("StructDecl");
  case CXCursor_UnionDecl:
    return cxstring::createRef("UnionDecl");
  case CXCursor_ClassDecl:
    return cxstring::createRef("ClassDecl");
  case CXCursor_FieldDecl:
    return cxstring::createRef("FieldDecl");
  case CXCursor_VarDecl:
    return cxstring::createRef("VarDecl");
  case CXCursor_ParmDecl:
    return cxstring::createRef("ParmDecl");
  case CXCursor_ObjCInterfaceDecl:
    return cxstring::createRef("ObjCInterfaceDecl");
  case CXCursor_ObjCCategoryDecl:
    return cxstring::createRef("ObjCCategoryDecl");
  case CXCursor_ObjCProtocolDecl:
    return cxstring::createRef("ObjCProtocolDecl");
  case CXCursor_ObjCPropertyDecl:
    return cxstring::createRef("ObjCPropertyDecl");
  case CXCursor_ObjCIvarDecl:
    return cxstring::createRef("ObjCIvarDecl");
  case CXCursor_ObjCInstanceMethodDecl:
    return cxstring::createRef("ObjCInstanceMethodDecl");
  case CXCursor_ObjCClassMethodDecl:
    return cxstring::createRef("ObjCClassMethodDecl");
  case CXCursor_ObjCImplementationDecl:
    return cxstring::createRef("ObjCImplementationDecl");
  case CXCursor_ObjCCategoryImplDecl:
    return cxstring::createRef("ObjCCategoryImplDecl");
  case CXCursor_CXXMethod:
    return cxstring::createRef("CXXMethod");
  case CXCursor_UnexposedDecl:
    return cxstring::createRef("UnexposedDecl");
  case CXCursor_ObjCSuperClassRef:
    return cxstring::createRef("ObjCSuperClassRef");
  case CXCursor_ObjCProtocolRef:
    return cxstring::createRef("ObjCProtocolRef");
  case CXCursor_ObjCClassRef:
    return cxstring::createRef("ObjCClassRef");
  case CXCursor_TypeRef:
    return cxstring::createRef("TypeRef");
  case CXCursor_TemplateRef:
    return cxstring::createRef("TemplateRef");
  case CXCursor_NamespaceRef:
    return cxstring::createRef("NamespaceRef");
  case CXCursor_MemberRef:
    return cxstring::createRef("MemberRef");
  case CXCursor_LabelRef:
    return cxstring::createRef("LabelRef");
  case CXCursor_OverloadedDeclRef:
    return cxstring::createRef("OverloadedDeclRef");
  case CXCursor_VariableRef:
    return cxstring::createRef("VariableRef");
  case CXCursor_IntegerLiteral:
    return cxstring::createRef("IntegerLiteral");
  case CXCursor_FixedPointLiteral:
    return cxstring::createRef("FixedPointLiteral");
  case CXCursor_FloatingLiteral:
    return cxstring::createRef("FloatingLiteral");
  case CXCursor_ImaginaryLiteral:
    return cxstring::createRef("ImaginaryLiteral");
  case CXCursor_StringLiteral:
    return cxstring::createRef("StringLiteral");
  case CXCursor_CharacterLiteral:
    return cxstring::createRef("CharacterLiteral");
  case CXCursor_ParenExpr:
    return cxstring::createRef("ParenExpr");
  case CXCursor_UnaryOperator:
    return cxstring::createRef("UnaryOperator");
  case CXCursor_ArraySubscriptExpr:
    return cxstring::createRef("ArraySubscriptExpr");
  case CXCursor_ArraySectionExpr:
    return cxstring::createRef("ArraySectionExpr");
  case CXCursor_OMPArrayShapingExpr:
    return cxstring::createRef("OMPArrayShapingExpr");
  case CXCursor_OMPIteratorExpr:
    return cxstring::createRef("OMPIteratorExpr");
  case CXCursor_BinaryOperator:
    return cxstring::createRef("BinaryOperator");
  case CXCursor_CompoundAssignOperator:
    return cxstring::createRef("CompoundAssignOperator");
  case CXCursor_ConditionalOperator:
    return cxstring::createRef("ConditionalOperator");
  case CXCursor_CStyleCastExpr:
    return cxstring::createRef("CStyleCastExpr");
  case CXCursor_CompoundLiteralExpr:
    return cxstring::createRef("CompoundLiteralExpr");
  case CXCursor_InitListExpr:
    return cxstring::createRef("InitListExpr");
  case CXCursor_AddrLabelExpr:
    return cxstring::createRef("AddrLabelExpr");
  case CXCursor_StmtExpr:
    return cxstring::createRef("StmtExpr");
  case CXCursor_GenericSelectionExpr:
    return cxstring::createRef("GenericSelectionExpr");
  case CXCursor_GNUNullExpr:
    return cxstring::createRef("GNUNullExpr");
  case CXCursor_CXXStaticCastExpr:
    return cxstring::createRef("CXXStaticCastExpr");
  case CXCursor_CXXDynamicCastExpr:
    return cxstring::createRef("CXXDynamicCastExpr");
  case CXCursor_CXXReinterpretCastExpr:
    return cxstring::createRef("CXXReinterpretCastExpr");
  case CXCursor_CXXConstCastExpr:
    return cxstring::createRef("CXXConstCastExpr");
  case CXCursor_CXXFunctionalCastExpr:
    return cxstring::createRef("CXXFunctionalCastExpr");
  case CXCursor_CXXAddrspaceCastExpr:
    return cxstring::createRef("CXXAddrspaceCastExpr");
  case CXCursor_CXXTypeidExpr:
    return cxstring::createRef("CXXTypeidExpr");
  case CXCursor_CXXBoolLiteralExpr:
    return cxstring::createRef("CXXBoolLiteralExpr");
  case CXCursor_CXXNullPtrLiteralExpr:
    return cxstring::createRef("CXXNullPtrLiteralExpr");
  case CXCursor_CXXThisExpr:
    return cxstring::createRef("CXXThisExpr");
  case CXCursor_CXXThrowExpr:
    return cxstring::createRef("CXXThrowExpr");
  case CXCursor_CXXNewExpr:
    return cxstring::createRef("CXXNewExpr");
  case CXCursor_CXXDeleteExpr:
    return cxstring::createRef("CXXDeleteExpr");
  case CXCursor_UnaryExpr:
    return cxstring::createRef("UnaryExpr");
  case CXCursor_ObjCStringLiteral:
    return cxstring::createRef("ObjCStringLiteral");
  case CXCursor_ObjCBoolLiteralExpr:
    return cxstring::createRef("ObjCBoolLiteralExpr");
  case CXCursor_ObjCAvailabilityCheckExpr:
    return cxstring::createRef("ObjCAvailabilityCheckExpr");
  case CXCursor_ObjCSelfExpr:
    return cxstring::createRef("ObjCSelfExpr");
  case CXCursor_ObjCEncodeExpr:
    return cxstring::createRef("ObjCEncodeExpr");
  case CXCursor_ObjCSelectorExpr:
    return cxstring::createRef("ObjCSelectorExpr");
  case CXCursor_ObjCProtocolExpr:
    return cxstring::createRef("ObjCProtocolExpr");
  case CXCursor_ObjCBridgedCastExpr:
    return cxstring::createRef("ObjCBridgedCastExpr");
  case CXCursor_BlockExpr:
    return cxstring::createRef("BlockExpr");
  case CXCursor_PackExpansionExpr:
    return cxstring::createRef("PackExpansionExpr");
  case CXCursor_SizeOfPackExpr:
    return cxstring::createRef("SizeOfPackExpr");
  case CXCursor_PackIndexingExpr:
    return cxstring::createRef("PackIndexingExpr");
  case CXCursor_LambdaExpr:
    return cxstring::createRef("LambdaExpr");
  case CXCursor_UnexposedExpr:
    return cxstring::createRef("UnexposedExpr");
  case CXCursor_DeclRefExpr:
    return cxstring::createRef("DeclRefExpr");
  case CXCursor_MemberRefExpr:
    return cxstring::createRef("MemberRefExpr");
  case CXCursor_CallExpr:
    return cxstring::createRef("CallExpr");
  case CXCursor_ObjCMessageExpr:
    return cxstring::createRef("ObjCMessageExpr");
  case CXCursor_BuiltinBitCastExpr:
    return cxstring::createRef("BuiltinBitCastExpr");
  case CXCursor_ConceptSpecializationExpr:
    return cxstring::createRef("ConceptSpecializationExpr");
  case CXCursor_RequiresExpr:
    return cxstring::createRef("RequiresExpr");
  case CXCursor_CXXParenListInitExpr:
    return cxstring::createRef("CXXParenListInitExpr");
  case CXCursor_UnexposedStmt:
    return cxstring::createRef("UnexposedStmt");
  case CXCursor_DeclStmt:
    return cxstring::createRef("DeclStmt");
  case CXCursor_LabelStmt:
    return cxstring::createRef("LabelStmt");
  case CXCursor_CompoundStmt:
    return cxstring::createRef("CompoundStmt");
  case CXCursor_CaseStmt:
    return cxstring::createRef("CaseStmt");
  case CXCursor_DefaultStmt:
    return cxstring::createRef("DefaultStmt");
  case CXCursor_IfStmt:
    return cxstring::createRef("IfStmt");
  case CXCursor_SwitchStmt:
    return cxstring::createRef("SwitchStmt");
  case CXCursor_WhileStmt:
    return cxstring::createRef("WhileStmt");
  case CXCursor_DoStmt:
    return cxstring::createRef("DoStmt");
  case CXCursor_ForStmt:
    return cxstring::createRef("ForStmt");
  case CXCursor_GotoStmt:
    return cxstring::createRef("GotoStmt");
  case CXCursor_IndirectGotoStmt:
    return cxstring::createRef("IndirectGotoStmt");
  case CXCursor_ContinueStmt:
    return cxstring::createRef("ContinueStmt");
  case CXCursor_BreakStmt:
    return cxstring::createRef("BreakStmt");
  case CXCursor_ReturnStmt:
    return cxstring::createRef("ReturnStmt");
  case CXCursor_GCCAsmStmt:
    return cxstring::createRef("GCCAsmStmt");
  case CXCursor_MSAsmStmt:
    return cxstring::createRef("MSAsmStmt");
  case CXCursor_ObjCAtTryStmt:
    return cxstring::createRef("ObjCAtTryStmt");
  case CXCursor_ObjCAtCatchStmt:
    return cxstring::createRef("ObjCAtCatchStmt");
  case CXCursor_ObjCAtFinallyStmt:
    return cxstring::createRef("ObjCAtFinallyStmt");
  case CXCursor_ObjCAtThrowStmt:
    return cxstring::createRef("ObjCAtThrowStmt");
  case CXCursor_ObjCAtSynchronizedStmt:
    return cxstring::createRef("ObjCAtSynchronizedStmt");
  case CXCursor_ObjCAutoreleasePoolStmt:
    return cxstring::createRef("ObjCAutoreleasePoolStmt");
  case CXCursor_ObjCForCollectionStmt:
    return cxstring::createRef("ObjCForCollectionStmt");
  case CXCursor_CXXCatchStmt:
    return cxstring::createRef("CXXCatchStmt");
  case CXCursor_CXXTryStmt:
    return cxstring::createRef("CXXTryStmt");
  case CXCursor_CXXForRangeStmt:
    return cxstring::createRef("CXXForRangeStmt");
  case CXCursor_SEHTryStmt:
    return cxstring::createRef("SEHTryStmt");
  case CXCursor_SEHExceptStmt:
    return cxstring::createRef("SEHExceptStmt");
  case CXCursor_SEHFinallyStmt:
    return cxstring::createRef("SEHFinallyStmt");
  case CXCursor_SEHLeaveStmt:
    return cxstring::createRef("SEHLeaveStmt");
  case CXCursor_NullStmt:
    return cxstring::createRef("NullStmt");
  case CXCursor_InvalidFile:
    return cxstring::createRef("InvalidFile");
  case CXCursor_InvalidCode:
    return cxstring::createRef("InvalidCode");
  case CXCursor_NoDeclFound:
    return cxstring::createRef("NoDeclFound");
  case CXCursor_NotImplemented:
    return cxstring::createRef("NotImplemented");
  case CXCursor_TranslationUnit:
    return cxstring::createRef("TranslationUnit");
  case CXCursor_UnexposedAttr:
    return cxstring::createRef("UnexposedAttr");
  case CXCursor_IBActionAttr:
    return cxstring::createRef("attribute(ibaction)");
  case CXCursor_IBOutletAttr:
    return cxstring::createRef("attribute(iboutlet)");
  case CXCursor_IBOutletCollectionAttr:
    return cxstring::createRef("attribute(iboutletcollection)");
  case CXCursor_CXXFinalAttr:
    return cxstring::createRef("attribute(final)");
  case CXCursor_CXXOverrideAttr:
    return cxstring::createRef("attribute(override)");
  case CXCursor_AnnotateAttr:
    return cxstring::createRef("attribute(annotate)");
  case CXCursor_AsmLabelAttr:
    return cxstring::createRef("asm label");
  case CXCursor_PackedAttr:
    return cxstring::createRef("attribute(packed)");
  case CXCursor_PureAttr:
    return cxstring::createRef("attribute(pure)");
  case CXCursor_ConstAttr:
    return cxstring::createRef("attribute(const)");
  case CXCursor_NoDuplicateAttr:
    return cxstring::createRef("attribute(noduplicate)");
  case CXCursor_CUDAConstantAttr:
    return cxstring::createRef("attribute(constant)");
  case CXCursor_CUDADeviceAttr:
    return cxstring::createRef("attribute(device)");
  case CXCursor_CUDAGlobalAttr:
    return cxstring::createRef("attribute(global)");
  case CXCursor_CUDAHostAttr:
    return cxstring::createRef("attribute(host)");
  case CXCursor_CUDASharedAttr:
    return cxstring::createRef("attribute(shared)");
  case CXCursor_VisibilityAttr:
    return cxstring::createRef("attribute(visibility)");
  case CXCursor_DLLExport:
    return cxstring::createRef("attribute(dllexport)");
  case CXCursor_DLLImport:
    return cxstring::createRef("attribute(dllimport)");
  case CXCursor_NSReturnsRetained:
    return cxstring::createRef("attribute(ns_returns_retained)");
  case CXCursor_NSReturnsNotRetained:
    return cxstring::createRef("attribute(ns_returns_not_retained)");
  case CXCursor_NSReturnsAutoreleased:
    return cxstring::createRef("attribute(ns_returns_autoreleased)");
  case CXCursor_NSConsumesSelf:
    return cxstring::createRef("attribute(ns_consumes_self)");
  case CXCursor_NSConsumed:
    return cxstring::createRef("attribute(ns_consumed)");
  case CXCursor_ObjCException:
    return cxstring::createRef("attribute(objc_exception)");
  case CXCursor_ObjCNSObject:
    return cxstring::createRef("attribute(NSObject)");
  case CXCursor_ObjCIndependentClass:
    return cxstring::createRef("attribute(objc_independent_class)");
  case CXCursor_ObjCPreciseLifetime:
    return cxstring::createRef("attribute(objc_precise_lifetime)");
  case CXCursor_ObjCReturnsInnerPointer:
    return cxstring::createRef("attribute(objc_returns_inner_pointer)");
  case CXCursor_ObjCRequiresSuper:
    return cxstring::createRef("attribute(objc_requires_super)");
  case CXCursor_ObjCRootClass:
    return cxstring::createRef("attribute(objc_root_class)");
  case CXCursor_ObjCSubclassingRestricted:
    return cxstring::createRef("attribute(objc_subclassing_restricted)");
  case CXCursor_ObjCExplicitProtocolImpl:
    return cxstring::createRef(
        "attribute(objc_protocol_requires_explicit_implementation)");
  case CXCursor_ObjCDesignatedInitializer:
    return cxstring::createRef("attribute(objc_designated_initializer)");
  case CXCursor_ObjCRuntimeVisible:
    return cxstring::createRef("attribute(objc_runtime_visible)");
  case CXCursor_ObjCBoxable:
    return cxstring::createRef("attribute(objc_boxable)");
  case CXCursor_FlagEnum:
    return cxstring::createRef("attribute(flag_enum)");
  case CXCursor_PreprocessingDirective:
    return cxstring::createRef("preprocessing directive");
  case CXCursor_MacroDefinition:
    return cxstring::createRef("macro definition");
  case CXCursor_MacroExpansion:
    return cxstring::createRef("macro expansion");
  case CXCursor_InclusionDirective:
    return cxstring::createRef("inclusion directive");
  case CXCursor_Namespace:
    return cxstring::createRef("Namespace");
  case CXCursor_LinkageSpec:
    return cxstring::createRef("LinkageSpec");
  case CXCursor_CXXBaseSpecifier:
    return cxstring::createRef("C++ base class specifier");
  case CXCursor_Constructor:
    return cxstring::createRef("CXXConstructor");
  case CXCursor_Destructor:
    return cxstring::createRef("CXXDestructor");
  case CXCursor_ConversionFunction:
    return cxstring::createRef("CXXConversion");
  case CXCursor_TemplateTypeParameter:
    return cxstring::createRef("TemplateTypeParameter");
  case CXCursor_NonTypeTemplateParameter:
    return cxstring::createRef("NonTypeTemplateParameter");
  case CXCursor_TemplateTemplateParameter:
    return cxstring::createRef("TemplateTemplateParameter");
  case CXCursor_FunctionTemplate:
    return cxstring::createRef("FunctionTemplate");
  case CXCursor_ClassTemplate:
    return cxstring::createRef("ClassTemplate");
  case CXCursor_ClassTemplatePartialSpecialization:
    return cxstring::createRef("ClassTemplatePartialSpecialization");
  case CXCursor_NamespaceAlias:
    return cxstring::createRef("NamespaceAlias");
  case CXCursor_UsingDirective:
    return cxstring::createRef("UsingDirective");
  case CXCursor_UsingDeclaration:
    return cxstring::createRef("UsingDeclaration");
  case CXCursor_TypeAliasDecl:
    return cxstring::createRef("TypeAliasDecl");
  case CXCursor_ObjCSynthesizeDecl:
    return cxstring::createRef("ObjCSynthesizeDecl");
  case CXCursor_ObjCDynamicDecl:
    return cxstring::createRef("ObjCDynamicDecl");
  case CXCursor_CXXAccessSpecifier:
    return cxstring::createRef("CXXAccessSpecifier");
  case CXCursor_ModuleImportDecl:
    return cxstring::createRef("ModuleImport");
  case CXCursor_OMPCanonicalLoop:
    return cxstring::createRef("OMPCanonicalLoop");
  case CXCursor_OMPMetaDirective:
    return cxstring::createRef("OMPMetaDirective");
  case CXCursor_OMPParallelDirective:
    return cxstring::createRef("OMPParallelDirective");
  case CXCursor_OMPSimdDirective:
    return cxstring::createRef("OMPSimdDirective");
  case CXCursor_OMPTileDirective:
    return cxstring::createRef("OMPTileDirective");
  case CXCursor_OMPUnrollDirective:
    return cxstring::createRef("OMPUnrollDirective");
  case CXCursor_OMPReverseDirective:
    return cxstring::createRef("OMPReverseDirective");
  case CXCursor_OMPInterchangeDirective:
    return cxstring::createRef("OMPInterchangeDirective");
  case CXCursor_OMPForDirective:
    return cxstring::createRef("OMPForDirective");
  case CXCursor_OMPForSimdDirective:
    return cxstring::createRef("OMPForSimdDirective");
  case CXCursor_OMPSectionsDirective:
    return cxstring::createRef("OMPSectionsDirective");
  case CXCursor_OMPSectionDirective:
    return cxstring::createRef("OMPSectionDirective");
  case CXCursor_OMPScopeDirective:
    return cxstring::createRef("OMPScopeDirective");
  case CXCursor_OMPSingleDirective:
    return cxstring::createRef("OMPSingleDirective");
  case CXCursor_OMPMasterDirective:
    return cxstring::createRef("OMPMasterDirective");
  case CXCursor_OMPCriticalDirective:
    return cxstring::createRef("OMPCriticalDirective");
  case CXCursor_OMPParallelForDirective:
    return cxstring::createRef("OMPParallelForDirective");
  case CXCursor_OMPParallelForSimdDirective:
    return cxstring::createRef("OMPParallelForSimdDirective");
  case CXCursor_OMPParallelMasterDirective:
    return cxstring::createRef("OMPParallelMasterDirective");
  case CXCursor_OMPParallelMaskedDirective:
    return cxstring::createRef("OMPParallelMaskedDirective");
  case CXCursor_OMPParallelSectionsDirective:
    return cxstring::createRef("OMPParallelSectionsDirective");
  case CXCursor_OMPTaskDirective:
    return cxstring::createRef("OMPTaskDirective");
  case CXCursor_OMPTaskyieldDirective:
    return cxstring::createRef("OMPTaskyieldDirective");
  case CXCursor_OMPBarrierDirective:
    return cxstring::createRef("OMPBarrierDirective");
  case CXCursor_OMPTaskwaitDirective:
    return cxstring::createRef("OMPTaskwaitDirective");
  case CXCursor_OMPErrorDirective:
    return cxstring::createRef("OMPErrorDirective");
  case CXCursor_OMPTaskgroupDirective:
    return cxstring::createRef("OMPTaskgroupDirective");
  case CXCursor_OMPFlushDirective:
    return cxstring::createRef("OMPFlushDirective");
  case CXCursor_OMPDepobjDirective:
    return cxstring::createRef("OMPDepobjDirective");
  case CXCursor_OMPScanDirective:
    return cxstring::createRef("OMPScanDirective");
  case CXCursor_OMPOrderedDirective:
    return cxstring::createRef("OMPOrderedDirective");
  case CXCursor_OMPAtomicDirective:
    return cxstring::createRef("OMPAtomicDirective");
  case CXCursor_OMPTargetDirective:
    return cxstring::createRef("OMPTargetDirective");
  case CXCursor_OMPTargetDataDirective:
    return cxstring::createRef("OMPTargetDataDirective");
  case CXCursor_OMPTargetEnterDataDirective:
    return cxstring::createRef("OMPTargetEnterDataDirective");
  case CXCursor_OMPTargetExitDataDirective:
    return cxstring::createRef("OMPTargetExitDataDirective");
  case CXCursor_OMPTargetParallelDirective:
    return cxstring::createRef("OMPTargetParallelDirective");
  case CXCursor_OMPTargetParallelForDirective:
    return cxstring::createRef("OMPTargetParallelForDirective");
  case CXCursor_OMPTargetUpdateDirective:
    return cxstring::createRef("OMPTargetUpdateDirective");
  case CXCursor_OMPTeamsDirective:
    return cxstring::createRef("OMPTeamsDirective");
  case CXCursor_OMPCancellationPointDirective:
    return cxstring::createRef("OMPCancellationPointDirective");
  case CXCursor_OMPCancelDirective:
    return cxstring::createRef("OMPCancelDirective");
  case CXCursor_OMPTaskLoopDirective:
    return cxstring::createRef("OMPTaskLoopDirective");
  case CXCursor_OMPTaskLoopSimdDirective:
    return cxstring::createRef("OMPTaskLoopSimdDirective");
  case CXCursor_OMPMasterTaskLoopDirective:
    return cxstring::createRef("OMPMasterTaskLoopDirective");
  case CXCursor_OMPMaskedTaskLoopDirective:
    return cxstring::createRef("OMPMaskedTaskLoopDirective");
  case CXCursor_OMPMasterTaskLoopSimdDirective:
    return cxstring::createRef("OMPMasterTaskLoopSimdDirective");
  case CXCursor_OMPMaskedTaskLoopSimdDirective:
    return cxstring::createRef("OMPMaskedTaskLoopSimdDirective");
  case CXCursor_OMPParallelMasterTaskLoopDirective:
    return cxstring::createRef("OMPParallelMasterTaskLoopDirective");
  case CXCursor_OMPParallelMaskedTaskLoopDirective:
    return cxstring::createRef("OMPParallelMaskedTaskLoopDirective");
  case CXCursor_OMPParallelMasterTaskLoopSimdDirective:
    return cxstring::createRef("OMPParallelMasterTaskLoopSimdDirective");
  case CXCursor_OMPParallelMaskedTaskLoopSimdDirective:
    return cxstring::createRef("OMPParallelMaskedTaskLoopSimdDirective");
  case CXCursor_OMPDistributeDirective:
    return cxstring::createRef("OMPDistributeDirective");
  case CXCursor_OMPDistributeParallelForDirective:
    return cxstring::createRef("OMPDistributeParallelForDirective");
  case CXCursor_OMPDistributeParallelForSimdDirective:
    return cxstring::createRef("OMPDistributeParallelForSimdDirective");
  case CXCursor_OMPDistributeSimdDirective:
    return cxstring::createRef("OMPDistributeSimdDirective");
  case CXCursor_OMPTargetParallelForSimdDirective:
    return cxstring::createRef("OMPTargetParallelForSimdDirective");
  case CXCursor_OMPTargetSimdDirective:
    return cxstring::createRef("OMPTargetSimdDirective");
  case CXCursor_OMPTeamsDistributeDirective:
    return cxstring::createRef("OMPTeamsDistributeDirective");
  case CXCursor_OMPTeamsDistributeSimdDirective:
    return cxstring::createRef("OMPTeamsDistributeSimdDirective");
  case CXCursor_OMPTeamsDistributeParallelForSimdDirective:
    return cxstring::createRef("OMPTeamsDistributeParallelForSimdDirective");
  case CXCursor_OMPTeamsDistributeParallelForDirective:
    return cxstring::createRef("OMPTeamsDistributeParallelForDirective");
  case CXCursor_OMPTargetTeamsDirective:
    return cxstring::createRef("OMPTargetTeamsDirective");
  case CXCursor_OMPTargetTeamsDistributeDirective:
    return cxstring::createRef("OMPTargetTeamsDistributeDirective");
  case CXCursor_OMPTargetTeamsDistributeParallelForDirective:
    return cxstring::createRef("OMPTargetTeamsDistributeParallelForDirective");
  case CXCursor_OMPTargetTeamsDistributeParallelForSimdDirective:
    return cxstring::createRef(
        "OMPTargetTeamsDistributeParallelForSimdDirective");
  case CXCursor_OMPTargetTeamsDistributeSimdDirective:
    return cxstring::createRef("OMPTargetTeamsDistributeSimdDirective");
  case CXCursor_OMPInteropDirective:
    return cxstring::createRef("OMPInteropDirective");
  case CXCursor_OMPDispatchDirective:
    return cxstring::createRef("OMPDispatchDirective");
  case CXCursor_OMPMaskedDirective:
    return cxstring::createRef("OMPMaskedDirective");
  case CXCursor_OMPGenericLoopDirective:
    return cxstring::createRef("OMPGenericLoopDirective");
  case CXCursor_OMPTeamsGenericLoopDirective:
    return cxstring::createRef("OMPTeamsGenericLoopDirective");
  case CXCursor_OMPTargetTeamsGenericLoopDirective:
    return cxstring::createRef("OMPTargetTeamsGenericLoopDirective");
  case CXCursor_OMPParallelGenericLoopDirective:
    return cxstring::createRef("OMPParallelGenericLoopDirective");
  case CXCursor_OMPTargetParallelGenericLoopDirective:
    return cxstring::createRef("OMPTargetParallelGenericLoopDirective");
  case CXCursor_OverloadCandidate:
    return cxstring::createRef("OverloadCandidate");
  case CXCursor_TypeAliasTemplateDecl:
    return cxstring::createRef("TypeAliasTemplateDecl");
  case CXCursor_StaticAssert:
    return cxstring::createRef("StaticAssert");
  case CXCursor_FriendDecl:
    return cxstring::createRef("FriendDecl");
  case CXCursor_ConvergentAttr:
    return cxstring::createRef("attribute(convergent)");
  case CXCursor_WarnUnusedAttr:
    return cxstring::createRef("attribute(warn_unused)");
  case CXCursor_WarnUnusedResultAttr:
    return cxstring::createRef("attribute(warn_unused_result)");
  case CXCursor_AlignedAttr:
    return cxstring::createRef("attribute(aligned)");
  case CXCursor_ConceptDecl:
    return cxstring::createRef("ConceptDecl");
  case CXCursor_OpenACCComputeConstruct:
    return cxstring::createRef("OpenACCComputeConstruct");
  case CXCursor_OpenACCLoopConstruct:
    return cxstring::createRef("OpenACCLoopConstruct");
  }

  llvm_unreachable("Unhandled CXCursorKind");
}

struct GetCursorData {
  SourceLocation TokenBeginLoc;
  bool PointsAtMacroArgExpansion;
  bool VisitedObjCPropertyImplDecl;
  SourceLocation VisitedDeclaratorDeclStartLoc;
  CXCursor &BestCursor;

  GetCursorData(SourceManager &SM, SourceLocation tokenBegin,
                CXCursor &outputCursor)
      : TokenBeginLoc(tokenBegin), BestCursor(outputCursor) {
    PointsAtMacroArgExpansion = SM.isMacroArgExpansion(tokenBegin);
    VisitedObjCPropertyImplDecl = false;
  }
};

static enum CXChildVisitResult
GetCursorVisitor(CXCursor cursor, CXCursor parent, CXClientData client_data) {
  GetCursorData *Data = static_cast<GetCursorData *>(client_data);
  CXCursor *BestCursor = &Data->BestCursor;

  // If we point inside a macro argument we should provide info of what the
  // token is so use the actual cursor, don't replace it with a macro expansion
  // cursor.
  if (cursor.kind == CXCursor_MacroExpansion && Data->PointsAtMacroArgExpansion)
    return CXChildVisit_Recurse;

  if (clang_isDeclaration(cursor.kind)) {
    // Avoid having the implicit methods override the property decls.
    if (const ObjCMethodDecl *MD =
            dyn_cast_or_null<ObjCMethodDecl>(getCursorDecl(cursor))) {
      if (MD->isImplicit())
        return CXChildVisit_Break;

    } else if (const ObjCInterfaceDecl *ID =
                   dyn_cast_or_null<ObjCInterfaceDecl>(getCursorDecl(cursor))) {
      // Check that when we have multiple @class references in the same line,
      // that later ones do not override the previous ones.
      // If we have:
      // @class Foo, Bar;
      // source ranges for both start at '@', so 'Bar' will end up overriding
      // 'Foo' even though the cursor location was at 'Foo'.
      if (BestCursor->kind == CXCursor_ObjCInterfaceDecl ||
          BestCursor->kind == CXCursor_ObjCClassRef)
        if (const ObjCInterfaceDecl *PrevID =
                dyn_cast_or_null<ObjCInterfaceDecl>(
                    getCursorDecl(*BestCursor))) {
          if (PrevID != ID && !PrevID->isThisDeclarationADefinition() &&
              !ID->isThisDeclarationADefinition())
            return CXChildVisit_Break;
        }

    } else if (const DeclaratorDecl *DD =
                   dyn_cast_or_null<DeclaratorDecl>(getCursorDecl(cursor))) {
      SourceLocation StartLoc = DD->getSourceRange().getBegin();
      // Check that when we have multiple declarators in the same line,
      // that later ones do not override the previous ones.
      // If we have:
      // int Foo, Bar;
      // source ranges for both start at 'int', so 'Bar' will end up overriding
      // 'Foo' even though the cursor location was at 'Foo'.
      if (Data->VisitedDeclaratorDeclStartLoc == StartLoc)
        return CXChildVisit_Break;
      Data->VisitedDeclaratorDeclStartLoc = StartLoc;

    } else if (const ObjCPropertyImplDecl *PropImp =
                   dyn_cast_or_null<ObjCPropertyImplDecl>(
                       getCursorDecl(cursor))) {
      (void)PropImp;
      // Check that when we have multiple @synthesize in the same line,
      // that later ones do not override the previous ones.
      // If we have:
      // @synthesize Foo, Bar;
      // source ranges for both start at '@', so 'Bar' will end up overriding
      // 'Foo' even though the cursor location was at 'Foo'.
      if (Data->VisitedObjCPropertyImplDecl)
        return CXChildVisit_Break;
      Data->VisitedObjCPropertyImplDecl = true;
    }
  }

  if (clang_isExpression(cursor.kind) &&
      clang_isDeclaration(BestCursor->kind)) {
    if (const Decl *D = getCursorDecl(*BestCursor)) {
      // Avoid having the cursor of an expression replace the declaration cursor
      // when the expression source range overlaps the declaration range.
      // This can happen for C++ constructor expressions whose range generally
      // include the variable declaration, e.g.:
      //  MyCXXClass foo; // Make sure pointing at 'foo' returns a VarDecl
      //  cursor.
      if (D->getLocation().isValid() && Data->TokenBeginLoc.isValid() &&
          D->getLocation() == Data->TokenBeginLoc)
        return CXChildVisit_Break;
    }
  }

  // If our current best cursor is the construction of a temporary object,
  // don't replace that cursor with a type reference, because we want
  // clang_getCursor() to point at the constructor.
  if (clang_isExpression(BestCursor->kind) &&
      isa<CXXTemporaryObjectExpr>(getCursorExpr(*BestCursor)) &&
      cursor.kind == CXCursor_TypeRef) {
    // Keep the cursor pointing at CXXTemporaryObjectExpr but also mark it
    // as having the actual point on the type reference.
    *BestCursor = getTypeRefedCallExprCursor(*BestCursor);
    return CXChildVisit_Recurse;
  }

  // If we already have an Objective-C superclass reference, don't
  // update it further.
  if (BestCursor->kind == CXCursor_ObjCSuperClassRef)
    return CXChildVisit_Break;

  *BestCursor = cursor;
  return CXChildVisit_Recurse;
}

CXCursor clang_getCursor(CXTranslationUnit TU, CXSourceLocation Loc) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return clang_getNullCursor();
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  SourceLocation SLoc = cxloc::translateSourceLocation(Loc);
  CXCursor Result = cxcursor::getCursor(TU, SLoc);

  LOG_FUNC_SECTION {
    CXFile SearchFile;
    unsigned SearchLine, SearchColumn;
    CXFile ResultFile;
    unsigned ResultLine, ResultColumn;
    CXString SearchFileName, ResultFileName, KindSpelling, USR;
    const char *IsDef = clang_isCursorDefinition(Result) ? " (Definition)" : "";
    CXSourceLocation ResultLoc = clang_getCursorLocation(Result);

    clang_getFileLocation(Loc, &SearchFile, &SearchLine, &SearchColumn,
                          nullptr);
    clang_getFileLocation(ResultLoc, &ResultFile, &ResultLine, &ResultColumn,
                          nullptr);
    SearchFileName = clang_getFileName(SearchFile);
    ResultFileName = clang_getFileName(ResultFile);
    KindSpelling = clang_getCursorKindSpelling(Result.kind);
    USR = clang_getCursorUSR(Result);
    *Log << llvm::format("(%s:%d:%d) = %s", clang_getCString(SearchFileName),
                         SearchLine, SearchColumn,
                         clang_getCString(KindSpelling))
         << llvm::format("(%s:%d:%d):%s%s", clang_getCString(ResultFileName),
                         ResultLine, ResultColumn, clang_getCString(USR),
                         IsDef);
    clang_disposeString(SearchFileName);
    clang_disposeString(ResultFileName);
    clang_disposeString(KindSpelling);
    clang_disposeString(USR);

    CXCursor Definition = clang_getCursorDefinition(Result);
    if (!clang_equalCursors(Definition, clang_getNullCursor())) {
      CXSourceLocation DefinitionLoc = clang_getCursorLocation(Definition);
      CXString DefinitionKindSpelling =
          clang_getCursorKindSpelling(Definition.kind);
      CXFile DefinitionFile;
      unsigned DefinitionLine, DefinitionColumn;
      clang_getFileLocation(DefinitionLoc, &DefinitionFile, &DefinitionLine,
                            &DefinitionColumn, nullptr);
      CXString DefinitionFileName = clang_getFileName(DefinitionFile);
      *Log << llvm::format("  -> %s(%s:%d:%d)",
                           clang_getCString(DefinitionKindSpelling),
                           clang_getCString(DefinitionFileName), DefinitionLine,
                           DefinitionColumn);
      clang_disposeString(DefinitionFileName);
      clang_disposeString(DefinitionKindSpelling);
    }
  }

  return Result;
}

CXCursor clang_getNullCursor(void) {
  return MakeCXCursorInvalid(CXCursor_InvalidFile);
}

unsigned clang_equalCursors(CXCursor X, CXCursor Y) {
  // Clear out the "FirstInDeclGroup" part in a declaration cursor, since we
  // can't set consistently. For example, when visiting a DeclStmt we will set
  // it but we don't set it on the result of clang_getCursorDefinition for
  // a reference of the same declaration.
  // FIXME: Setting "FirstInDeclGroup" in CXCursors is a hack that only works
  // when visiting a DeclStmt currently, the AST should be enhanced to be able
  // to provide that kind of info.
  if (clang_isDeclaration(X.kind))
    X.data[1] = nullptr;
  if (clang_isDeclaration(Y.kind))
    Y.data[1] = nullptr;

  return X == Y;
}

unsigned clang_hashCursor(CXCursor C) {
  unsigned Index = 0;
  if (clang_isExpression(C.kind) || clang_isStatement(C.kind))
    Index = 1;

  return llvm::DenseMapInfo<std::pair<unsigned, const void *>>::getHashValue(
      std::make_pair(C.kind, C.data[Index]));
}

unsigned clang_isInvalid(enum CXCursorKind K) {
  return K >= CXCursor_FirstInvalid && K <= CXCursor_LastInvalid;
}

unsigned clang_isDeclaration(enum CXCursorKind K) {
  return (K >= CXCursor_FirstDecl && K <= CXCursor_LastDecl) ||
         (K >= CXCursor_FirstExtraDecl && K <= CXCursor_LastExtraDecl);
}

unsigned clang_isInvalidDeclaration(CXCursor C) {
  if (clang_isDeclaration(C.kind)) {
    if (const Decl *D = getCursorDecl(C))
      return D->isInvalidDecl();
  }

  return 0;
}

unsigned clang_isReference(enum CXCursorKind K) {
  return K >= CXCursor_FirstRef && K <= CXCursor_LastRef;
}

unsigned clang_isExpression(enum CXCursorKind K) {
  return K >= CXCursor_FirstExpr && K <= CXCursor_LastExpr;
}

unsigned clang_isStatement(enum CXCursorKind K) {
  return K >= CXCursor_FirstStmt && K <= CXCursor_LastStmt;
}

unsigned clang_isAttribute(enum CXCursorKind K) {
  return K >= CXCursor_FirstAttr && K <= CXCursor_LastAttr;
}

unsigned clang_isTranslationUnit(enum CXCursorKind K) {
  return K == CXCursor_TranslationUnit;
}

unsigned clang_isPreprocessing(enum CXCursorKind K) {
  return K >= CXCursor_FirstPreprocessing && K <= CXCursor_LastPreprocessing;
}

unsigned clang_isUnexposed(enum CXCursorKind K) {
  switch (K) {
  case CXCursor_UnexposedDecl:
  case CXCursor_UnexposedExpr:
  case CXCursor_UnexposedStmt:
  case CXCursor_UnexposedAttr:
    return true;
  default:
    return false;
  }
}

CXCursorKind clang_getCursorKind(CXCursor C) { return C.kind; }

CXSourceLocation clang_getCursorLocation(CXCursor C) {
  if (clang_isReference(C.kind)) {
    switch (C.kind) {
    case CXCursor_ObjCSuperClassRef: {
      std::pair<const ObjCInterfaceDecl *, SourceLocation> P =
          getCursorObjCSuperClassRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_ObjCProtocolRef: {
      std::pair<const ObjCProtocolDecl *, SourceLocation> P =
          getCursorObjCProtocolRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_ObjCClassRef: {
      std::pair<const ObjCInterfaceDecl *, SourceLocation> P =
          getCursorObjCClassRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_TypeRef: {
      std::pair<const TypeDecl *, SourceLocation> P = getCursorTypeRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_TemplateRef: {
      std::pair<const TemplateDecl *, SourceLocation> P =
          getCursorTemplateRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_NamespaceRef: {
      std::pair<const NamedDecl *, SourceLocation> P = getCursorNamespaceRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_MemberRef: {
      std::pair<const FieldDecl *, SourceLocation> P = getCursorMemberRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_VariableRef: {
      std::pair<const VarDecl *, SourceLocation> P = getCursorVariableRef(C);
      return cxloc::translateSourceLocation(P.first->getASTContext(), P.second);
    }

    case CXCursor_CXXBaseSpecifier: {
      const CXXBaseSpecifier *BaseSpec = getCursorCXXBaseSpecifier(C);
      if (!BaseSpec)
        return clang_getNullLocation();

      if (TypeSourceInfo *TSInfo = BaseSpec->getTypeSourceInfo())
        return cxloc::translateSourceLocation(
            getCursorContext(C), TSInfo->getTypeLoc().getBeginLoc());

      return cxloc::translateSourceLocation(getCursorContext(C),
                                            BaseSpec->getBeginLoc());
    }

    case CXCursor_LabelRef: {
      std::pair<const LabelStmt *, SourceLocation> P = getCursorLabelRef(C);
      return cxloc::translateSourceLocation(getCursorContext(C), P.second);
    }

    case CXCursor_OverloadedDeclRef:
      return cxloc::translateSourceLocation(
          getCursorContext(C), getCursorOverloadedDeclRef(C).second);

    default:
      // FIXME: Need a way to enumerate all non-reference cases.
      llvm_unreachable("Missed a reference kind");
    }
  }

  if (clang_isExpression(C.kind))
    return cxloc::translateSourceLocation(
        getCursorContext(C), getLocationFromExpr(getCursorExpr(C)));

  if (clang_isStatement(C.kind))
    return cxloc::translateSourceLocation(getCursorContext(C),
                                          getCursorStmt(C)->getBeginLoc());

  if (C.kind == CXCursor_PreprocessingDirective) {
    SourceLocation L = cxcursor::getCursorPreprocessingDirective(C).getBegin();
    return cxloc::translateSourceLocation(getCursorContext(C), L);
  }

  if (C.kind == CXCursor_MacroExpansion) {
    SourceLocation L =
        cxcursor::getCursorMacroExpansion(C).getSourceRange().getBegin();
    return cxloc::translateSourceLocation(getCursorContext(C), L);
  }

  if (C.kind == CXCursor_MacroDefinition) {
    SourceLocation L = cxcursor::getCursorMacroDefinition(C)->getLocation();
    return cxloc::translateSourceLocation(getCursorContext(C), L);
  }

  if (C.kind == CXCursor_InclusionDirective) {
    SourceLocation L =
        cxcursor::getCursorInclusionDirective(C)->getSourceRange().getBegin();
    return cxloc::translateSourceLocation(getCursorContext(C), L);
  }

  if (clang_isAttribute(C.kind)) {
    SourceLocation L = cxcursor::getCursorAttr(C)->getLocation();
    return cxloc::translateSourceLocation(getCursorContext(C), L);
  }

  if (!clang_isDeclaration(C.kind))
    return clang_getNullLocation();

  const Decl *D = getCursorDecl(C);
  if (!D)
    return clang_getNullLocation();

  SourceLocation Loc = D->getLocation();
  // FIXME: Multiple variables declared in a single declaration
  // currently lack the information needed to correctly determine their
  // ranges when accounting for the type-specifier.  We use context
  // stored in the CXCursor to determine if the VarDecl is in a DeclGroup,
  // and if so, whether it is the first decl.
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    if (!cxcursor::isFirstInDeclGroup(C))
      Loc = VD->getLocation();
  }

  // For ObjC methods, give the start location of the method name.
  if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D))
    Loc = MD->getSelectorStartLoc();

  return cxloc::translateSourceLocation(getCursorContext(C), Loc);
}

} // end extern "C"

CXCursor cxcursor::getCursor(CXTranslationUnit TU, SourceLocation SLoc) {
  assert(TU);

  // Guard against an invalid SourceLocation, or we may assert in one
  // of the following calls.
  if (SLoc.isInvalid())
    return clang_getNullCursor();

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);

  // Translate the given source location to make it point at the beginning of
  // the token under the cursor.
  SLoc = Lexer::GetBeginningOfToken(SLoc, CXXUnit->getSourceManager(),
                                    CXXUnit->getASTContext().getLangOpts());

  CXCursor Result = MakeCXCursorInvalid(CXCursor_NoDeclFound);
  if (SLoc.isValid()) {
    GetCursorData ResultData(CXXUnit->getSourceManager(), SLoc, Result);
    CursorVisitor CursorVis(TU, GetCursorVisitor, &ResultData,
                            /*VisitPreprocessorLast=*/true,
                            /*VisitIncludedEntities=*/false,
                            SourceLocation(SLoc));
    CursorVis.visitFileRegion();
  }

  return Result;
}

static SourceRange getRawCursorExtent(CXCursor C) {
  if (clang_isReference(C.kind)) {
    switch (C.kind) {
    case CXCursor_ObjCSuperClassRef:
      return getCursorObjCSuperClassRef(C).second;

    case CXCursor_ObjCProtocolRef:
      return getCursorObjCProtocolRef(C).second;

    case CXCursor_ObjCClassRef:
      return getCursorObjCClassRef(C).second;

    case CXCursor_TypeRef:
      return getCursorTypeRef(C).second;

    case CXCursor_TemplateRef:
      return getCursorTemplateRef(C).second;

    case CXCursor_NamespaceRef:
      return getCursorNamespaceRef(C).second;

    case CXCursor_MemberRef:
      return getCursorMemberRef(C).second;

    case CXCursor_CXXBaseSpecifier:
      return getCursorCXXBaseSpecifier(C)->getSourceRange();

    case CXCursor_LabelRef:
      return getCursorLabelRef(C).second;

    case CXCursor_OverloadedDeclRef:
      return getCursorOverloadedDeclRef(C).second;

    case CXCursor_VariableRef:
      return getCursorVariableRef(C).second;

    default:
      // FIXME: Need a way to enumerate all non-reference cases.
      llvm_unreachable("Missed a reference kind");
    }
  }

  if (clang_isExpression(C.kind))
    return getCursorExpr(C)->getSourceRange();

  if (clang_isStatement(C.kind))
    return getCursorStmt(C)->getSourceRange();

  if (clang_isAttribute(C.kind))
    return getCursorAttr(C)->getRange();

  if (C.kind == CXCursor_PreprocessingDirective)
    return cxcursor::getCursorPreprocessingDirective(C);

  if (C.kind == CXCursor_MacroExpansion) {
    ASTUnit *TU = getCursorASTUnit(C);
    SourceRange Range = cxcursor::getCursorMacroExpansion(C).getSourceRange();
    return TU->mapRangeFromPreamble(Range);
  }

  if (C.kind == CXCursor_MacroDefinition) {
    ASTUnit *TU = getCursorASTUnit(C);
    SourceRange Range = cxcursor::getCursorMacroDefinition(C)->getSourceRange();
    return TU->mapRangeFromPreamble(Range);
  }

  if (C.kind == CXCursor_InclusionDirective) {
    ASTUnit *TU = getCursorASTUnit(C);
    SourceRange Range =
        cxcursor::getCursorInclusionDirective(C)->getSourceRange();
    return TU->mapRangeFromPreamble(Range);
  }

  if (C.kind == CXCursor_TranslationUnit) {
    ASTUnit *TU = getCursorASTUnit(C);
    FileID MainID = TU->getSourceManager().getMainFileID();
    SourceLocation Start = TU->getSourceManager().getLocForStartOfFile(MainID);
    SourceLocation End = TU->getSourceManager().getLocForEndOfFile(MainID);
    return SourceRange(Start, End);
  }

  if (clang_isDeclaration(C.kind)) {
    const Decl *D = cxcursor::getCursorDecl(C);
    if (!D)
      return SourceRange();

    SourceRange R = D->getSourceRange();
    // FIXME: Multiple variables declared in a single declaration
    // currently lack the information needed to correctly determine their
    // ranges when accounting for the type-specifier.  We use context
    // stored in the CXCursor to determine if the VarDecl is in a DeclGroup,
    // and if so, whether it is the first decl.
    if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
      if (!cxcursor::isFirstInDeclGroup(C))
        R.setBegin(VD->getLocation());
    }
    return R;
  }
  return SourceRange();
}

/// Retrieves the "raw" cursor extent, which is then extended to include
/// the decl-specifier-seq for declarations.
static SourceRange getFullCursorExtent(CXCursor C, SourceManager &SrcMgr) {
  if (clang_isDeclaration(C.kind)) {
    const Decl *D = cxcursor::getCursorDecl(C);
    if (!D)
      return SourceRange();

    SourceRange R = D->getSourceRange();

    // Adjust the start of the location for declarations preceded by
    // declaration specifiers.
    SourceLocation StartLoc;
    if (const DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
      if (TypeSourceInfo *TI = DD->getTypeSourceInfo())
        StartLoc = TI->getTypeLoc().getBeginLoc();
    } else if (const TypedefDecl *Typedef = dyn_cast<TypedefDecl>(D)) {
      if (TypeSourceInfo *TI = Typedef->getTypeSourceInfo())
        StartLoc = TI->getTypeLoc().getBeginLoc();
    }

    if (StartLoc.isValid() && R.getBegin().isValid() &&
        SrcMgr.isBeforeInTranslationUnit(StartLoc, R.getBegin()))
      R.setBegin(StartLoc);

    // FIXME: Multiple variables declared in a single declaration
    // currently lack the information needed to correctly determine their
    // ranges when accounting for the type-specifier.  We use context
    // stored in the CXCursor to determine if the VarDecl is in a DeclGroup,
    // and if so, whether it is the first decl.
    if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
      if (!cxcursor::isFirstInDeclGroup(C))
        R.setBegin(VD->getLocation());
    }

    return R;
  }

  return getRawCursorExtent(C);
}

CXSourceRange clang_getCursorExtent(CXCursor C) {
  SourceRange R = getRawCursorExtent(C);
  if (R.isInvalid())
    return clang_getNullRange();

  return cxloc::translateSourceRange(getCursorContext(C), R);
}

CXCursor clang_getCursorReferenced(CXCursor C) {
  if (clang_isInvalid(C.kind))
    return clang_getNullCursor();

  CXTranslationUnit tu = getCursorTU(C);
  if (clang_isDeclaration(C.kind)) {
    const Decl *D = getCursorDecl(C);
    if (!D)
      return clang_getNullCursor();
    if (const UsingDecl *Using = dyn_cast<UsingDecl>(D))
      return MakeCursorOverloadedDeclRef(Using, D->getLocation(), tu);
    if (const ObjCPropertyImplDecl *PropImpl =
            dyn_cast<ObjCPropertyImplDecl>(D))
      if (ObjCPropertyDecl *Property = PropImpl->getPropertyDecl())
        return MakeCXCursor(Property, tu);

    return C;
  }

  if (clang_isExpression(C.kind)) {
    const Expr *E = getCursorExpr(C);
    const Decl *D = getDeclFromExpr(E);
    if (D) {
      CXCursor declCursor = MakeCXCursor(D, tu);
      declCursor = getSelectorIdentifierCursor(getSelectorIdentifierIndex(C),
                                               declCursor);
      return declCursor;
    }

    if (const OverloadExpr *Ovl = dyn_cast_or_null<OverloadExpr>(E))
      return MakeCursorOverloadedDeclRef(Ovl, tu);

    return clang_getNullCursor();
  }

  if (clang_isStatement(C.kind)) {
    const Stmt *S = getCursorStmt(C);
    if (const GotoStmt *Goto = dyn_cast_or_null<GotoStmt>(S))
      if (LabelDecl *label = Goto->getLabel())
        if (LabelStmt *labelS = label->getStmt())
          return MakeCXCursor(labelS, getCursorDecl(C), tu);

    return clang_getNullCursor();
  }

  if (C.kind == CXCursor_MacroExpansion) {
    if (const MacroDefinitionRecord *Def =
            getCursorMacroExpansion(C).getDefinition())
      return MakeMacroDefinitionCursor(Def, tu);
  }

  if (!clang_isReference(C.kind))
    return clang_getNullCursor();

  switch (C.kind) {
  case CXCursor_ObjCSuperClassRef:
    return MakeCXCursor(getCursorObjCSuperClassRef(C).first, tu);

  case CXCursor_ObjCProtocolRef: {
    const ObjCProtocolDecl *Prot = getCursorObjCProtocolRef(C).first;
    if (const ObjCProtocolDecl *Def = Prot->getDefinition())
      return MakeCXCursor(Def, tu);

    return MakeCXCursor(Prot, tu);
  }

  case CXCursor_ObjCClassRef: {
    const ObjCInterfaceDecl *Class = getCursorObjCClassRef(C).first;
    if (const ObjCInterfaceDecl *Def = Class->getDefinition())
      return MakeCXCursor(Def, tu);

    return MakeCXCursor(Class, tu);
  }

  case CXCursor_TypeRef:
    return MakeCXCursor(getCursorTypeRef(C).first, tu);

  case CXCursor_TemplateRef:
    return MakeCXCursor(getCursorTemplateRef(C).first, tu);

  case CXCursor_NamespaceRef:
    return MakeCXCursor(getCursorNamespaceRef(C).first, tu);

  case CXCursor_MemberRef:
    return MakeCXCursor(getCursorMemberRef(C).first, tu);

  case CXCursor_CXXBaseSpecifier: {
    const CXXBaseSpecifier *B = cxcursor::getCursorCXXBaseSpecifier(C);
    return clang_getTypeDeclaration(cxtype::MakeCXType(B->getType(), tu));
  }

  case CXCursor_LabelRef:
    // FIXME: We end up faking the "parent" declaration here because we
    // don't want to make CXCursor larger.
    return MakeCXCursor(
        getCursorLabelRef(C).first,
        cxtu::getASTUnit(tu)->getASTContext().getTranslationUnitDecl(), tu);

  case CXCursor_OverloadedDeclRef:
    return C;

  case CXCursor_VariableRef:
    return MakeCXCursor(getCursorVariableRef(C).first, tu);

  default:
    // We would prefer to enumerate all non-reference cursor kinds here.
    llvm_unreachable("Unhandled reference cursor kind");
  }
}

CXCursor clang_getCursorDefinition(CXCursor C) {
  if (clang_isInvalid(C.kind))
    return clang_getNullCursor();

  CXTranslationUnit TU = getCursorTU(C);

  bool WasReference = false;
  if (clang_isReference(C.kind) || clang_isExpression(C.kind)) {
    C = clang_getCursorReferenced(C);
    WasReference = true;
  }

  if (C.kind == CXCursor_MacroExpansion)
    return clang_getCursorReferenced(C);

  if (!clang_isDeclaration(C.kind))
    return clang_getNullCursor();

  const Decl *D = getCursorDecl(C);
  if (!D)
    return clang_getNullCursor();

  switch (D->getKind()) {
  // Declaration kinds that don't really separate the notions of
  // declaration and definition.
  case Decl::Namespace:
  case Decl::Typedef:
  case Decl::TypeAlias:
  case Decl::TypeAliasTemplate:
  case Decl::TemplateTypeParm:
  case Decl::EnumConstant:
  case Decl::Field:
  case Decl::Binding:
  case Decl::MSProperty:
  case Decl::MSGuid:
  case Decl::HLSLBuffer:
  case Decl::UnnamedGlobalConstant:
  case Decl::TemplateParamObject:
  case Decl::IndirectField:
  case Decl::ObjCIvar:
  case Decl::ObjCAtDefsField:
  case Decl::ImplicitParam:
  case Decl::ParmVar:
  case Decl::NonTypeTemplateParm:
  case Decl::TemplateTemplateParm:
  case Decl::ObjCCategoryImpl:
  case Decl::ObjCImplementation:
  case Decl::AccessSpec:
  case Decl::LinkageSpec:
  case Decl::Export:
  case Decl::ObjCPropertyImpl:
  case Decl::FileScopeAsm:
  case Decl::TopLevelStmt:
  case Decl::StaticAssert:
  case Decl::Block:
  case Decl::Captured:
  case Decl::OMPCapturedExpr:
  case Decl::Label: // FIXME: Is this right??
  case Decl::CXXDeductionGuide:
  case Decl::Import:
  case Decl::OMPThreadPrivate:
  case Decl::OMPAllocate:
  case Decl::OMPDeclareReduction:
  case Decl::OMPDeclareMapper:
  case Decl::OMPRequires:
  case Decl::ObjCTypeParam:
  case Decl::BuiltinTemplate:
  case Decl::PragmaComment:
  case Decl::PragmaDetectMismatch:
  case Decl::UsingPack:
  case Decl::Concept:
  case Decl::ImplicitConceptSpecialization:
  case Decl::LifetimeExtendedTemporary:
  case Decl::RequiresExprBody:
  case Decl::UnresolvedUsingIfExists:
    return C;

  // Declaration kinds that don't make any sense here, but are
  // nonetheless harmless.
  case Decl::Empty:
  case Decl::TranslationUnit:
  case Decl::ExternCContext:
    break;

  // Declaration kinds for which the definition is not resolvable.
  case Decl::UnresolvedUsingTypename:
  case Decl::UnresolvedUsingValue:
    break;

  case Decl::UsingDirective:
    return MakeCXCursor(cast<UsingDirectiveDecl>(D)->getNominatedNamespace(),
                        TU);

  case Decl::NamespaceAlias:
    return MakeCXCursor(cast<NamespaceAliasDecl>(D)->getNamespace(), TU);

  case Decl::Enum:
  case Decl::Record:
  case Decl::CXXRecord:
  case Decl::ClassTemplateSpecialization:
  case Decl::ClassTemplatePartialSpecialization:
    if (TagDecl *Def = cast<TagDecl>(D)->getDefinition())
      return MakeCXCursor(Def, TU);
    return clang_getNullCursor();

  case Decl::Function:
  case Decl::CXXMethod:
  case Decl::CXXConstructor:
  case Decl::CXXDestructor:
  case Decl::CXXConversion: {
    const FunctionDecl *Def = nullptr;
    if (cast<FunctionDecl>(D)->getBody(Def))
      return MakeCXCursor(Def, TU);
    return clang_getNullCursor();
  }

  case Decl::Var:
  case Decl::VarTemplateSpecialization:
  case Decl::VarTemplatePartialSpecialization:
  case Decl::Decomposition: {
    // Ask the variable if it has a definition.
    if (const VarDecl *Def = cast<VarDecl>(D)->getDefinition())
      return MakeCXCursor(Def, TU);
    return clang_getNullCursor();
  }

  case Decl::FunctionTemplate: {
    const FunctionDecl *Def = nullptr;
    if (cast<FunctionTemplateDecl>(D)->getTemplatedDecl()->getBody(Def))
      return MakeCXCursor(Def->getDescribedFunctionTemplate(), TU);
    return clang_getNullCursor();
  }

  case Decl::ClassTemplate: {
    if (RecordDecl *Def =
            cast<ClassTemplateDecl>(D)->getTemplatedDecl()->getDefinition())
      return MakeCXCursor(cast<CXXRecordDecl>(Def)->getDescribedClassTemplate(),
                          TU);
    return clang_getNullCursor();
  }

  case Decl::VarTemplate: {
    if (VarDecl *Def =
            cast<VarTemplateDecl>(D)->getTemplatedDecl()->getDefinition())
      return MakeCXCursor(cast<VarDecl>(Def)->getDescribedVarTemplate(), TU);
    return clang_getNullCursor();
  }

  case Decl::Using:
  case Decl::UsingEnum:
    return MakeCursorOverloadedDeclRef(cast<BaseUsingDecl>(D), D->getLocation(),
                                       TU);

  case Decl::UsingShadow:
  case Decl::ConstructorUsingShadow:
    return clang_getCursorDefinition(
        MakeCXCursor(cast<UsingShadowDecl>(D)->getTargetDecl(), TU));

  case Decl::ObjCMethod: {
    const ObjCMethodDecl *Method = cast<ObjCMethodDecl>(D);
    if (Method->isThisDeclarationADefinition())
      return C;

    // Dig out the method definition in the associated
    // @implementation, if we have it.
    // FIXME: The ASTs should make finding the definition easier.
    if (const ObjCInterfaceDecl *Class =
            dyn_cast<ObjCInterfaceDecl>(Method->getDeclContext()))
      if (ObjCImplementationDecl *ClassImpl = Class->getImplementation())
        if (ObjCMethodDecl *Def = ClassImpl->getMethod(
                Method->getSelector(), Method->isInstanceMethod()))
          if (Def->isThisDeclarationADefinition())
            return MakeCXCursor(Def, TU);

    return clang_getNullCursor();
  }

  case Decl::ObjCCategory:
    if (ObjCCategoryImplDecl *Impl =
            cast<ObjCCategoryDecl>(D)->getImplementation())
      return MakeCXCursor(Impl, TU);
    return clang_getNullCursor();

  case Decl::ObjCProtocol:
    if (const ObjCProtocolDecl *Def =
            cast<ObjCProtocolDecl>(D)->getDefinition())
      return MakeCXCursor(Def, TU);
    return clang_getNullCursor();

  case Decl::ObjCInterface: {
    // There are two notions of a "definition" for an Objective-C
    // class: the interface and its implementation. When we resolved a
    // reference to an Objective-C class, produce the @interface as
    // the definition; when we were provided with the interface,
    // produce the @implementation as the definition.
    const ObjCInterfaceDecl *IFace = cast<ObjCInterfaceDecl>(D);
    if (WasReference) {
      if (const ObjCInterfaceDecl *Def = IFace->getDefinition())
        return MakeCXCursor(Def, TU);
    } else if (ObjCImplementationDecl *Impl = IFace->getImplementation())
      return MakeCXCursor(Impl, TU);
    return clang_getNullCursor();
  }

  case Decl::ObjCProperty:
    // FIXME: We don't really know where to find the
    // ObjCPropertyImplDecls that implement this property.
    return clang_getNullCursor();

  case Decl::ObjCCompatibleAlias:
    if (const ObjCInterfaceDecl *Class =
            cast<ObjCCompatibleAliasDecl>(D)->getClassInterface())
      if (const ObjCInterfaceDecl *Def = Class->getDefinition())
        return MakeCXCursor(Def, TU);

    return clang_getNullCursor();

  case Decl::Friend:
    if (NamedDecl *Friend = cast<FriendDecl>(D)->getFriendDecl())
      return clang_getCursorDefinition(MakeCXCursor(Friend, TU));
    return clang_getNullCursor();

  case Decl::FriendTemplate:
    if (NamedDecl *Friend = cast<FriendTemplateDecl>(D)->getFriendDecl())
      return clang_getCursorDefinition(MakeCXCursor(Friend, TU));
    return clang_getNullCursor();
  }

  return clang_getNullCursor();
}

unsigned clang_isCursorDefinition(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  return clang_getCursorDefinition(C) == C;
}

CXCursor clang_getCanonicalCursor(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return C;

  if (const Decl *D = getCursorDecl(C)) {
    if (const ObjCCategoryImplDecl *CatImplD =
            dyn_cast<ObjCCategoryImplDecl>(D))
      if (ObjCCategoryDecl *CatD = CatImplD->getCategoryDecl())
        return MakeCXCursor(CatD, getCursorTU(C));

    if (const ObjCImplDecl *ImplD = dyn_cast<ObjCImplDecl>(D))
      if (const ObjCInterfaceDecl *IFD = ImplD->getClassInterface())
        return MakeCXCursor(IFD, getCursorTU(C));

    return MakeCXCursor(D->getCanonicalDecl(), getCursorTU(C));
  }

  return C;
}

int clang_Cursor_getObjCSelectorIndex(CXCursor cursor) {
  return cxcursor::getSelectorIdentifierIndexAndLoc(cursor).first;
}

unsigned clang_getNumOverloadedDecls(CXCursor C) {
  if (C.kind != CXCursor_OverloadedDeclRef)
    return 0;

  OverloadedDeclRefStorage Storage = getCursorOverloadedDeclRef(C).first;
  if (const OverloadExpr *E = Storage.dyn_cast<const OverloadExpr *>())
    return E->getNumDecls();

  if (OverloadedTemplateStorage *S =
          Storage.dyn_cast<OverloadedTemplateStorage *>())
    return S->size();

  const Decl *D = Storage.get<const Decl *>();
  if (const UsingDecl *Using = dyn_cast<UsingDecl>(D))
    return Using->shadow_size();

  return 0;
}

CXCursor clang_getOverloadedDecl(CXCursor cursor, unsigned index) {
  if (cursor.kind != CXCursor_OverloadedDeclRef)
    return clang_getNullCursor();

  if (index >= clang_getNumOverloadedDecls(cursor))
    return clang_getNullCursor();

  CXTranslationUnit TU = getCursorTU(cursor);
  OverloadedDeclRefStorage Storage = getCursorOverloadedDeclRef(cursor).first;
  if (const OverloadExpr *E = Storage.dyn_cast<const OverloadExpr *>())
    return MakeCXCursor(E->decls_begin()[index], TU);

  if (OverloadedTemplateStorage *S =
          Storage.dyn_cast<OverloadedTemplateStorage *>())
    return MakeCXCursor(S->begin()[index], TU);

  const Decl *D = Storage.get<const Decl *>();
  if (const UsingDecl *Using = dyn_cast<UsingDecl>(D)) {
    // FIXME: This is, unfortunately, linear time.
    UsingDecl::shadow_iterator Pos = Using->shadow_begin();
    std::advance(Pos, index);
    return MakeCXCursor(cast<UsingShadowDecl>(*Pos)->getTargetDecl(), TU);
  }

  return clang_getNullCursor();
}

void clang_getDefinitionSpellingAndExtent(
    CXCursor C, const char **startBuf, const char **endBuf, unsigned *startLine,
    unsigned *startColumn, unsigned *endLine, unsigned *endColumn) {
  assert(getCursorDecl(C) && "CXCursor has null decl");
  const auto *FD = cast<FunctionDecl>(getCursorDecl(C));
  const auto *Body = cast<CompoundStmt>(FD->getBody());

  SourceManager &SM = FD->getASTContext().getSourceManager();
  *startBuf = SM.getCharacterData(Body->getLBracLoc());
  *endBuf = SM.getCharacterData(Body->getRBracLoc());
  *startLine = SM.getSpellingLineNumber(Body->getLBracLoc());
  *startColumn = SM.getSpellingColumnNumber(Body->getLBracLoc());
  *endLine = SM.getSpellingLineNumber(Body->getRBracLoc());
  *endColumn = SM.getSpellingColumnNumber(Body->getRBracLoc());
}

CXSourceRange clang_getCursorReferenceNameRange(CXCursor C, unsigned NameFlags,
                                                unsigned PieceIndex) {
  RefNamePieces Pieces;

  switch (C.kind) {
  case CXCursor_MemberRefExpr:
    if (const MemberExpr *E = dyn_cast<MemberExpr>(getCursorExpr(C)))
      Pieces = buildPieces(NameFlags, true, E->getMemberNameInfo(),
                           E->getQualifierLoc().getSourceRange());
    break;

  case CXCursor_DeclRefExpr:
    if (const DeclRefExpr *E = dyn_cast<DeclRefExpr>(getCursorExpr(C))) {
      SourceRange TemplateArgLoc(E->getLAngleLoc(), E->getRAngleLoc());
      Pieces =
          buildPieces(NameFlags, false, E->getNameInfo(),
                      E->getQualifierLoc().getSourceRange(), &TemplateArgLoc);
    }
    break;

  case CXCursor_CallExpr:
    if (const CXXOperatorCallExpr *OCE =
            dyn_cast<CXXOperatorCallExpr>(getCursorExpr(C))) {
      const Expr *Callee = OCE->getCallee();
      if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Callee))
        Callee = ICE->getSubExpr();

      if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Callee))
        Pieces = buildPieces(NameFlags, false, DRE->getNameInfo(),
                             DRE->getQualifierLoc().getSourceRange());
    }
    break;

  default:
    break;
  }

  if (Pieces.empty()) {
    if (PieceIndex == 0)
      return clang_getCursorExtent(C);
  } else if (PieceIndex < Pieces.size()) {
    SourceRange R = Pieces[PieceIndex];
    if (R.isValid())
      return cxloc::translateSourceRange(getCursorContext(C), R);
  }

  return clang_getNullRange();
}

void clang_enableStackTraces(void) {
  // FIXME: Provide an argv0 here so we can find llvm-symbolizer.
  llvm::sys::PrintStackTraceOnErrorSignal(StringRef());
}

void clang_executeOnThread(void (*fn)(void *), void *user_data,
                           unsigned stack_size) {
  llvm::thread Thread(stack_size == 0 ? clang::DesiredStackSize
                                      : std::optional<unsigned>(stack_size),
                      fn, user_data);
  Thread.join();
}

//===----------------------------------------------------------------------===//
// Token-based Operations.
//===----------------------------------------------------------------------===//

/* CXToken layout:
 *   int_data[0]: a CXTokenKind
 *   int_data[1]: starting token location
 *   int_data[2]: token length
 *   int_data[3]: reserved
 *   ptr_data: for identifiers and keywords, an IdentifierInfo*.
 *   otherwise unused.
 */
CXTokenKind clang_getTokenKind(CXToken CXTok) {
  return static_cast<CXTokenKind>(CXTok.int_data[0]);
}

CXString clang_getTokenSpelling(CXTranslationUnit TU, CXToken CXTok) {
  switch (clang_getTokenKind(CXTok)) {
  case CXToken_Identifier:
  case CXToken_Keyword:
    // We know we have an IdentifierInfo*, so use that.
    return cxstring::createRef(
        static_cast<IdentifierInfo *>(CXTok.ptr_data)->getNameStart());

  case CXToken_Literal: {
    // We have stashed the starting pointer in the ptr_data field. Use it.
    const char *Text = static_cast<const char *>(CXTok.ptr_data);
    return cxstring::createDup(StringRef(Text, CXTok.int_data[2]));
  }

  case CXToken_Punctuation:
  case CXToken_Comment:
    break;
  }

  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return cxstring::createEmpty();
  }

  // We have to find the starting buffer pointer the hard way, by
  // deconstructing the source location.
  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit)
    return cxstring::createEmpty();

  SourceLocation Loc = SourceLocation::getFromRawEncoding(CXTok.int_data[1]);
  std::pair<FileID, unsigned> LocInfo =
      CXXUnit->getSourceManager().getDecomposedSpellingLoc(Loc);
  bool Invalid = false;
  StringRef Buffer =
      CXXUnit->getSourceManager().getBufferData(LocInfo.first, &Invalid);
  if (Invalid)
    return cxstring::createEmpty();

  return cxstring::createDup(Buffer.substr(LocInfo.second, CXTok.int_data[2]));
}

CXSourceLocation clang_getTokenLocation(CXTranslationUnit TU, CXToken CXTok) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return clang_getNullLocation();
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit)
    return clang_getNullLocation();

  return cxloc::translateSourceLocation(
      CXXUnit->getASTContext(),
      SourceLocation::getFromRawEncoding(CXTok.int_data[1]));
}

CXSourceRange clang_getTokenExtent(CXTranslationUnit TU, CXToken CXTok) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return clang_getNullRange();
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit)
    return clang_getNullRange();

  return cxloc::translateSourceRange(
      CXXUnit->getASTContext(),
      SourceLocation::getFromRawEncoding(CXTok.int_data[1]));
}

static void getTokens(ASTUnit *CXXUnit, SourceRange Range,
                      SmallVectorImpl<CXToken> &CXTokens) {
  SourceManager &SourceMgr = CXXUnit->getSourceManager();
  std::pair<FileID, unsigned> BeginLocInfo =
      SourceMgr.getDecomposedSpellingLoc(Range.getBegin());
  std::pair<FileID, unsigned> EndLocInfo =
      SourceMgr.getDecomposedSpellingLoc(Range.getEnd());

  // Cannot tokenize across files.
  if (BeginLocInfo.first != EndLocInfo.first)
    return;

  // Create a lexer
  bool Invalid = false;
  StringRef Buffer = SourceMgr.getBufferData(BeginLocInfo.first, &Invalid);
  if (Invalid)
    return;

  Lexer Lex(SourceMgr.getLocForStartOfFile(BeginLocInfo.first),
            CXXUnit->getASTContext().getLangOpts(), Buffer.begin(),
            Buffer.data() + BeginLocInfo.second, Buffer.end());
  Lex.SetCommentRetentionState(true);

  // Lex tokens until we hit the end of the range.
  const char *EffectiveBufferEnd = Buffer.data() + EndLocInfo.second;
  Token Tok;
  bool previousWasAt = false;
  do {
    // Lex the next token
    Lex.LexFromRawLexer(Tok);
    if (Tok.is(tok::eof))
      break;

    // Initialize the CXToken.
    CXToken CXTok;

    //   - Common fields
    CXTok.int_data[1] = Tok.getLocation().getRawEncoding();
    CXTok.int_data[2] = Tok.getLength();
    CXTok.int_data[3] = 0;

    //   - Kind-specific fields
    if (Tok.isLiteral()) {
      CXTok.int_data[0] = CXToken_Literal;
      CXTok.ptr_data = const_cast<char *>(Tok.getLiteralData());
    } else if (Tok.is(tok::raw_identifier)) {
      // Lookup the identifier to determine whether we have a keyword.
      IdentifierInfo *II = CXXUnit->getPreprocessor().LookUpIdentifierInfo(Tok);

      if ((II->getObjCKeywordID() != tok::objc_not_keyword) && previousWasAt) {
        CXTok.int_data[0] = CXToken_Keyword;
      } else {
        CXTok.int_data[0] =
            Tok.is(tok::identifier) ? CXToken_Identifier : CXToken_Keyword;
      }
      CXTok.ptr_data = II;
    } else if (Tok.is(tok::comment)) {
      CXTok.int_data[0] = CXToken_Comment;
      CXTok.ptr_data = nullptr;
    } else {
      CXTok.int_data[0] = CXToken_Punctuation;
      CXTok.ptr_data = nullptr;
    }
    CXTokens.push_back(CXTok);
    previousWasAt = Tok.is(tok::at);
  } while (Lex.getBufferLocation() < EffectiveBufferEnd);
}

CXToken *clang_getToken(CXTranslationUnit TU, CXSourceLocation Location) {
  LOG_FUNC_SECTION { *Log << TU << ' ' << Location; }

  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return nullptr;
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit)
    return nullptr;

  SourceLocation Begin = cxloc::translateSourceLocation(Location);
  if (Begin.isInvalid())
    return nullptr;
  SourceManager &SM = CXXUnit->getSourceManager();
  std::pair<FileID, unsigned> DecomposedEnd = SM.getDecomposedLoc(Begin);
  DecomposedEnd.second +=
      Lexer::MeasureTokenLength(Begin, SM, CXXUnit->getLangOpts());

  SourceLocation End =
      SM.getComposedLoc(DecomposedEnd.first, DecomposedEnd.second);

  SmallVector<CXToken, 32> CXTokens;
  getTokens(CXXUnit, SourceRange(Begin, End), CXTokens);

  if (CXTokens.empty())
    return nullptr;

  CXTokens.resize(1);
  CXToken *Token = static_cast<CXToken *>(llvm::safe_malloc(sizeof(CXToken)));

  memmove(Token, CXTokens.data(), sizeof(CXToken));
  return Token;
}

void clang_tokenize(CXTranslationUnit TU, CXSourceRange Range, CXToken **Tokens,
                    unsigned *NumTokens) {
  LOG_FUNC_SECTION { *Log << TU << ' ' << Range; }

  if (Tokens)
    *Tokens = nullptr;
  if (NumTokens)
    *NumTokens = 0;

  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return;
  }

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit || !Tokens || !NumTokens)
    return;

  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  SourceRange R = cxloc::translateCXSourceRange(Range);
  if (R.isInvalid())
    return;

  SmallVector<CXToken, 32> CXTokens;
  getTokens(CXXUnit, R, CXTokens);

  if (CXTokens.empty())
    return;

  *Tokens = static_cast<CXToken *>(
      llvm::safe_malloc(sizeof(CXToken) * CXTokens.size()));
  memmove(*Tokens, CXTokens.data(), sizeof(CXToken) * CXTokens.size());
  *NumTokens = CXTokens.size();
}

void clang_disposeTokens(CXTranslationUnit TU, CXToken *Tokens,
                         unsigned NumTokens) {
  free(Tokens);
}

//===----------------------------------------------------------------------===//
// Token annotation APIs.
//===----------------------------------------------------------------------===//

static enum CXChildVisitResult AnnotateTokensVisitor(CXCursor cursor,
                                                     CXCursor parent,
                                                     CXClientData client_data);
static bool AnnotateTokensPostChildrenVisitor(CXCursor cursor,
                                              CXClientData client_data);

namespace {
class AnnotateTokensWorker {
  CXToken *Tokens;
  CXCursor *Cursors;
  unsigned NumTokens;
  unsigned TokIdx;
  unsigned PreprocessingTokIdx;
  CursorVisitor AnnotateVis;
  SourceManager &SrcMgr;
  bool HasContextSensitiveKeywords;

  struct PostChildrenAction {
    CXCursor cursor;
    enum Action { Invalid, Ignore, Postpone } action;
  };
  using PostChildrenActions = SmallVector<PostChildrenAction, 0>;

  struct PostChildrenInfo {
    CXCursor Cursor;
    SourceRange CursorRange;
    unsigned BeforeReachingCursorIdx;
    unsigned BeforeChildrenTokenIdx;
    PostChildrenActions ChildActions;
  };
  SmallVector<PostChildrenInfo, 8> PostChildrenInfos;

  CXToken &getTok(unsigned Idx) {
    assert(Idx < NumTokens);
    return Tokens[Idx];
  }
  const CXToken &getTok(unsigned Idx) const {
    assert(Idx < NumTokens);
    return Tokens[Idx];
  }
  bool MoreTokens() const { return TokIdx < NumTokens; }
  unsigned NextToken() const { return TokIdx; }
  void AdvanceToken() { ++TokIdx; }
  SourceLocation GetTokenLoc(unsigned tokI) {
    return SourceLocation::getFromRawEncoding(getTok(tokI).int_data[1]);
  }
  bool isFunctionMacroToken(unsigned tokI) const {
    return getTok(tokI).int_data[3] != 0;
  }
  SourceLocation getFunctionMacroTokenLoc(unsigned tokI) const {
    return SourceLocation::getFromRawEncoding(getTok(tokI).int_data[3]);
  }

  void annotateAndAdvanceTokens(CXCursor, RangeComparisonResult, SourceRange);
  bool annotateAndAdvanceFunctionMacroTokens(CXCursor, RangeComparisonResult,
                                             SourceRange);

public:
  AnnotateTokensWorker(CXToken *tokens, CXCursor *cursors, unsigned numTokens,
                       CXTranslationUnit TU, SourceRange RegionOfInterest)
      : Tokens(tokens), Cursors(cursors), NumTokens(numTokens), TokIdx(0),
        PreprocessingTokIdx(0),
        AnnotateVis(TU, AnnotateTokensVisitor, this,
                    /*VisitPreprocessorLast=*/true,
                    /*VisitIncludedEntities=*/false, RegionOfInterest,
                    /*VisitDeclsOnly=*/false,
                    AnnotateTokensPostChildrenVisitor),
        SrcMgr(cxtu::getASTUnit(TU)->getSourceManager()),
        HasContextSensitiveKeywords(false) {}

  void VisitChildren(CXCursor C) { AnnotateVis.VisitChildren(C); }
  enum CXChildVisitResult Visit(CXCursor cursor, CXCursor parent);
  bool IsIgnoredChildCursor(CXCursor cursor) const;
  PostChildrenActions DetermineChildActions(CXCursor Cursor) const;

  bool postVisitChildren(CXCursor cursor);
  void HandlePostPonedChildCursors(const PostChildrenInfo &Info);
  void HandlePostPonedChildCursor(CXCursor Cursor, unsigned StartTokenIndex);

  void AnnotateTokens();

  /// Determine whether the annotator saw any cursors that have
  /// context-sensitive keywords.
  bool hasContextSensitiveKeywords() const {
    return HasContextSensitiveKeywords;
  }

  ~AnnotateTokensWorker() { assert(PostChildrenInfos.empty()); }
};
} // namespace

void AnnotateTokensWorker::AnnotateTokens() {
  // Walk the AST within the region of interest, annotating tokens
  // along the way.
  AnnotateVis.visitFileRegion();
}

bool AnnotateTokensWorker::IsIgnoredChildCursor(CXCursor cursor) const {
  if (PostChildrenInfos.empty())
    return false;

  for (const auto &ChildAction : PostChildrenInfos.back().ChildActions) {
    if (ChildAction.cursor == cursor &&
        ChildAction.action == PostChildrenAction::Ignore) {
      return true;
    }
  }

  return false;
}

const CXXOperatorCallExpr *GetSubscriptOrCallOperator(CXCursor Cursor) {
  if (!clang_isExpression(Cursor.kind))
    return nullptr;

  const Expr *E = getCursorExpr(Cursor);
  if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
    const OverloadedOperatorKind Kind = OCE->getOperator();
    if (Kind == OO_Call || Kind == OO_Subscript)
      return OCE;
  }

  return nullptr;
}

AnnotateTokensWorker::PostChildrenActions
AnnotateTokensWorker::DetermineChildActions(CXCursor Cursor) const {
  PostChildrenActions actions;

  // The DeclRefExpr of CXXOperatorCallExpr referring to the custom operator is
  // visited before the arguments to the operator call. For the Call and
  // Subscript operator the range of this DeclRefExpr includes the whole call
  // expression, so that all tokens in that range would be mapped to the
  // operator function, including the tokens of the arguments. To avoid that,
  // ensure to visit this DeclRefExpr as last node.
  if (const auto *OCE = GetSubscriptOrCallOperator(Cursor)) {
    const Expr *Callee = OCE->getCallee();
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Callee)) {
      const Expr *SubExpr = ICE->getSubExpr();
      if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        const Decl *parentDecl = getCursorDecl(Cursor);
        CXTranslationUnit TU = clang_Cursor_getTranslationUnit(Cursor);

        // Visit the DeclRefExpr as last.
        CXCursor cxChild = MakeCXCursor(DRE, parentDecl, TU);
        actions.push_back({cxChild, PostChildrenAction::Postpone});

        // The parent of the DeclRefExpr, an ImplicitCastExpr, has an equally
        // wide range as the DeclRefExpr. We can skip visiting this entirely.
        cxChild = MakeCXCursor(ICE, parentDecl, TU);
        actions.push_back({cxChild, PostChildrenAction::Ignore});
      }
    }
  }

  return actions;
}

static inline void updateCursorAnnotation(CXCursor &Cursor,
                                          const CXCursor &updateC) {
  if (clang_isInvalid(updateC.kind) || !clang_isInvalid(Cursor.kind))
    return;
  Cursor = updateC;
}

/// It annotates and advances tokens with a cursor until the comparison
//// between the cursor location and the source range is the same as
/// \arg compResult.
///
/// Pass RangeBefore to annotate tokens with a cursor until a range is reached.
/// Pass RangeOverlap to annotate tokens inside a range.
void AnnotateTokensWorker::annotateAndAdvanceTokens(
    CXCursor updateC, RangeComparisonResult compResult, SourceRange range) {
  while (MoreTokens()) {
    const unsigned I = NextToken();
    if (isFunctionMacroToken(I))
      if (!annotateAndAdvanceFunctionMacroTokens(updateC, compResult, range))
        return;

    SourceLocation TokLoc = GetTokenLoc(I);
    if (LocationCompare(SrcMgr, TokLoc, range) == compResult) {
      updateCursorAnnotation(Cursors[I], updateC);
      AdvanceToken();
      continue;
    }
    break;
  }
}

/// Special annotation handling for macro argument tokens.
/// \returns true if it advanced beyond all macro tokens, false otherwise.
bool AnnotateTokensWorker::annotateAndAdvanceFunctionMacroTokens(
    CXCursor updateC, RangeComparisonResult compResult, SourceRange range) {
  assert(MoreTokens());
  assert(isFunctionMacroToken(NextToken()) &&
         "Should be called only for macro arg tokens");

  // This works differently than annotateAndAdvanceTokens; because expanded
  // macro arguments can have arbitrary translation-unit source order, we do not
  // advance the token index one by one until a token fails the range test.
  // We only advance once past all of the macro arg tokens if all of them
  // pass the range test. If one of them fails we keep the token index pointing
  // at the start of the macro arg tokens so that the failing token will be
  // annotated by a subsequent annotation try.

  bool atLeastOneCompFail = false;

  unsigned I = NextToken();
  for (; I < NumTokens && isFunctionMacroToken(I); ++I) {
    SourceLocation TokLoc = getFunctionMacroTokenLoc(I);
    if (TokLoc.isFileID())
      continue; // not macro arg token, it's parens or comma.
    if (LocationCompare(SrcMgr, TokLoc, range) == compResult) {
      if (clang_isInvalid(clang_getCursorKind(Cursors[I])))
        Cursors[I] = updateC;
    } else
      atLeastOneCompFail = true;
  }

  if (atLeastOneCompFail)
    return false;

  TokIdx = I; // All of the tokens were handled, advance beyond all of them.
  return true;
}

enum CXChildVisitResult AnnotateTokensWorker::Visit(CXCursor cursor,
                                                    CXCursor parent) {
  SourceRange cursorRange = getRawCursorExtent(cursor);
  if (cursorRange.isInvalid())
    return CXChildVisit_Recurse;

  if (IsIgnoredChildCursor(cursor))
    return CXChildVisit_Continue;

  if (!HasContextSensitiveKeywords) {
    // Objective-C properties can have context-sensitive keywords.
    if (cursor.kind == CXCursor_ObjCPropertyDecl) {
      if (const ObjCPropertyDecl *Property =
              dyn_cast_or_null<ObjCPropertyDecl>(getCursorDecl(cursor)))
        HasContextSensitiveKeywords =
            Property->getPropertyAttributesAsWritten() != 0;
    }
    // Objective-C methods can have context-sensitive keywords.
    else if (cursor.kind == CXCursor_ObjCInstanceMethodDecl ||
             cursor.kind == CXCursor_ObjCClassMethodDecl) {
      if (const ObjCMethodDecl *Method =
              dyn_cast_or_null<ObjCMethodDecl>(getCursorDecl(cursor))) {
        if (Method->getObjCDeclQualifier())
          HasContextSensitiveKeywords = true;
        else {
          for (const auto *P : Method->parameters()) {
            if (P->getObjCDeclQualifier()) {
              HasContextSensitiveKeywords = true;
              break;
            }
          }
        }
      }
    }
    // C++ methods can have context-sensitive keywords.
    else if (cursor.kind == CXCursor_CXXMethod) {
      if (const CXXMethodDecl *Method =
              dyn_cast_or_null<CXXMethodDecl>(getCursorDecl(cursor))) {
        if (Method->hasAttr<FinalAttr>() || Method->hasAttr<OverrideAttr>())
          HasContextSensitiveKeywords = true;
      }
    }
    // C++ classes can have context-sensitive keywords.
    else if (cursor.kind == CXCursor_StructDecl ||
             cursor.kind == CXCursor_ClassDecl ||
             cursor.kind == CXCursor_ClassTemplate ||
             cursor.kind == CXCursor_ClassTemplatePartialSpecialization) {
      if (const Decl *D = getCursorDecl(cursor))
        if (D->hasAttr<FinalAttr>())
          HasContextSensitiveKeywords = true;
    }
  }

  // Don't override a property annotation with its getter/setter method.
  if (cursor.kind == CXCursor_ObjCInstanceMethodDecl &&
      parent.kind == CXCursor_ObjCPropertyDecl)
    return CXChildVisit_Continue;

  if (clang_isPreprocessing(cursor.kind)) {
    // Items in the preprocessing record are kept separate from items in
    // declarations, so we keep a separate token index.
    unsigned SavedTokIdx = TokIdx;
    TokIdx = PreprocessingTokIdx;

    // Skip tokens up until we catch up to the beginning of the preprocessing
    // entry.
    while (MoreTokens()) {
      const unsigned I = NextToken();
      SourceLocation TokLoc = GetTokenLoc(I);
      switch (LocationCompare(SrcMgr, TokLoc, cursorRange)) {
      case RangeBefore:
        AdvanceToken();
        continue;
      case RangeAfter:
      case RangeOverlap:
        break;
      }
      break;
    }

    // Look at all of the tokens within this range.
    while (MoreTokens()) {
      const unsigned I = NextToken();
      SourceLocation TokLoc = GetTokenLoc(I);
      switch (LocationCompare(SrcMgr, TokLoc, cursorRange)) {
      case RangeBefore:
        llvm_unreachable("Infeasible");
      case RangeAfter:
        break;
      case RangeOverlap:
        // For macro expansions, just note where the beginning of the macro
        // expansion occurs.
        if (cursor.kind == CXCursor_MacroExpansion) {
          if (TokLoc == cursorRange.getBegin())
            Cursors[I] = cursor;
          AdvanceToken();
          break;
        }
        // We may have already annotated macro names inside macro definitions.
        if (Cursors[I].kind != CXCursor_MacroExpansion)
          Cursors[I] = cursor;
        AdvanceToken();
        continue;
      }
      break;
    }

    // Save the preprocessing token index; restore the non-preprocessing
    // token index.
    PreprocessingTokIdx = TokIdx;
    TokIdx = SavedTokIdx;
    return CXChildVisit_Recurse;
  }

  if (cursorRange.isInvalid())
    return CXChildVisit_Continue;

  unsigned BeforeReachingCursorIdx = NextToken();
  const enum CXCursorKind cursorK = clang_getCursorKind(cursor);
  const enum CXCursorKind K = clang_getCursorKind(parent);
  const CXCursor updateC =
      (clang_isInvalid(K) || K == CXCursor_TranslationUnit ||
       // Attributes are annotated out-of-order, skip tokens until we reach it.
       clang_isAttribute(cursor.kind))
          ? clang_getNullCursor()
          : parent;

  annotateAndAdvanceTokens(updateC, RangeBefore, cursorRange);

  // Avoid having the cursor of an expression "overwrite" the annotation of the
  // variable declaration that it belongs to.
  // This can happen for C++ constructor expressions whose range generally
  // include the variable declaration, e.g.:
  //  MyCXXClass foo; // Make sure we don't annotate 'foo' as a CallExpr cursor.
  if (clang_isExpression(cursorK) && MoreTokens()) {
    const Expr *E = getCursorExpr(cursor);
    if (const Decl *D = getCursorDecl(cursor)) {
      const unsigned I = NextToken();
      if (E->getBeginLoc().isValid() && D->getLocation().isValid() &&
          E->getBeginLoc() == D->getLocation() &&
          E->getBeginLoc() == GetTokenLoc(I)) {
        updateCursorAnnotation(Cursors[I], updateC);
        AdvanceToken();
      }
    }
  }

  // Before recursing into the children keep some state that we are going
  // to use in the AnnotateTokensWorker::postVisitChildren callback to do some
  // extra work after the child nodes are visited.
  // Note that we don't call VisitChildren here to avoid traversing statements
  // code-recursively which can blow the stack.

  PostChildrenInfo Info;
  Info.Cursor = cursor;
  Info.CursorRange = cursorRange;
  Info.BeforeReachingCursorIdx = BeforeReachingCursorIdx;
  Info.BeforeChildrenTokenIdx = NextToken();
  Info.ChildActions = DetermineChildActions(cursor);
  PostChildrenInfos.push_back(Info);

  return CXChildVisit_Recurse;
}

bool AnnotateTokensWorker::postVisitChildren(CXCursor cursor) {
  if (PostChildrenInfos.empty())
    return false;
  const PostChildrenInfo &Info = PostChildrenInfos.back();
  if (!clang_equalCursors(Info.Cursor, cursor))
    return false;

  HandlePostPonedChildCursors(Info);

  const unsigned BeforeChildren = Info.BeforeChildrenTokenIdx;
  const unsigned AfterChildren = NextToken();
  SourceRange cursorRange = Info.CursorRange;

  // Scan the tokens that are at the end of the cursor, but are not captured
  // but the child cursors.
  annotateAndAdvanceTokens(cursor, RangeOverlap, cursorRange);

  // Scan the tokens that are at the beginning of the cursor, but are not
  // capture by the child cursors.
  for (unsigned I = BeforeChildren; I != AfterChildren; ++I) {
    if (!clang_isInvalid(clang_getCursorKind(Cursors[I])))
      break;

    Cursors[I] = cursor;
  }

  // Attributes are annotated out-of-order, rewind TokIdx to when we first
  // encountered the attribute cursor.
  if (clang_isAttribute(cursor.kind))
    TokIdx = Info.BeforeReachingCursorIdx;

  PostChildrenInfos.pop_back();
  return false;
}

void AnnotateTokensWorker::HandlePostPonedChildCursors(
    const PostChildrenInfo &Info) {
  for (const auto &ChildAction : Info.ChildActions) {
    if (ChildAction.action == PostChildrenAction::Postpone) {
      HandlePostPonedChildCursor(ChildAction.cursor,
                                 Info.BeforeChildrenTokenIdx);
    }
  }
}

void AnnotateTokensWorker::HandlePostPonedChildCursor(
    CXCursor Cursor, unsigned StartTokenIndex) {
  unsigned I = StartTokenIndex;

  // The bracket tokens of a Call or Subscript operator are mapped to
  // CallExpr/CXXOperatorCallExpr because we skipped visiting the corresponding
  // DeclRefExpr. Remap these tokens to the DeclRefExpr cursors.
  for (unsigned RefNameRangeNr = 0; I < NumTokens; RefNameRangeNr++) {
    const CXSourceRange CXRefNameRange = clang_getCursorReferenceNameRange(
        Cursor, CXNameRange_WantQualifier, RefNameRangeNr);
    if (clang_Range_isNull(CXRefNameRange))
      break; // All ranges handled.

    SourceRange RefNameRange = cxloc::translateCXSourceRange(CXRefNameRange);
    while (I < NumTokens) {
      const SourceLocation TokenLocation = GetTokenLoc(I);
      if (!TokenLocation.isValid())
        break;

      // Adapt the end range, because LocationCompare() reports
      // RangeOverlap even for the not-inclusive end location.
      const SourceLocation fixedEnd =
          RefNameRange.getEnd().getLocWithOffset(-1);
      RefNameRange = SourceRange(RefNameRange.getBegin(), fixedEnd);

      const RangeComparisonResult ComparisonResult =
          LocationCompare(SrcMgr, TokenLocation, RefNameRange);

      if (ComparisonResult == RangeOverlap) {
        Cursors[I++] = Cursor;
      } else if (ComparisonResult == RangeBefore) {
        ++I; // Not relevant token, check next one.
      } else if (ComparisonResult == RangeAfter) {
        break; // All tokens updated for current range, check next.
      }
    }
  }
}

static enum CXChildVisitResult AnnotateTokensVisitor(CXCursor cursor,
                                                     CXCursor parent,
                                                     CXClientData client_data) {
  return static_cast<AnnotateTokensWorker *>(client_data)
      ->Visit(cursor, parent);
}

static bool AnnotateTokensPostChildrenVisitor(CXCursor cursor,
                                              CXClientData client_data) {
  return static_cast<AnnotateTokensWorker *>(client_data)
      ->postVisitChildren(cursor);
}

namespace {

/// Uses the macro expansions in the preprocessing record to find
/// and mark tokens that are macro arguments. This info is used by the
/// AnnotateTokensWorker.
class MarkMacroArgTokensVisitor {
  SourceManager &SM;
  CXToken *Tokens;
  unsigned NumTokens;
  unsigned CurIdx;

public:
  MarkMacroArgTokensVisitor(SourceManager &SM, CXToken *tokens,
                            unsigned numTokens)
      : SM(SM), Tokens(tokens), NumTokens(numTokens), CurIdx(0) {}

  CXChildVisitResult visit(CXCursor cursor, CXCursor parent) {
    if (cursor.kind != CXCursor_MacroExpansion)
      return CXChildVisit_Continue;

    SourceRange macroRange = getCursorMacroExpansion(cursor).getSourceRange();
    if (macroRange.getBegin() == macroRange.getEnd())
      return CXChildVisit_Continue; // it's not a function macro.

    for (; CurIdx < NumTokens; ++CurIdx) {
      if (!SM.isBeforeInTranslationUnit(getTokenLoc(CurIdx),
                                        macroRange.getBegin()))
        break;
    }

    if (CurIdx == NumTokens)
      return CXChildVisit_Break;

    for (; CurIdx < NumTokens; ++CurIdx) {
      SourceLocation tokLoc = getTokenLoc(CurIdx);
      if (!SM.isBeforeInTranslationUnit(tokLoc, macroRange.getEnd()))
        break;

      setFunctionMacroTokenLoc(CurIdx, SM.getMacroArgExpandedLocation(tokLoc));
    }

    if (CurIdx == NumTokens)
      return CXChildVisit_Break;

    return CXChildVisit_Continue;
  }

private:
  CXToken &getTok(unsigned Idx) {
    assert(Idx < NumTokens);
    return Tokens[Idx];
  }
  const CXToken &getTok(unsigned Idx) const {
    assert(Idx < NumTokens);
    return Tokens[Idx];
  }

  SourceLocation getTokenLoc(unsigned tokI) {
    return SourceLocation::getFromRawEncoding(getTok(tokI).int_data[1]);
  }

  void setFunctionMacroTokenLoc(unsigned tokI, SourceLocation loc) {
    // The third field is reserved and currently not used. Use it here
    // to mark macro arg expanded tokens with their expanded locations.
    getTok(tokI).int_data[3] = loc.getRawEncoding();
  }
};

} // end anonymous namespace

static CXChildVisitResult
MarkMacroArgTokensVisitorDelegate(CXCursor cursor, CXCursor parent,
                                  CXClientData client_data) {
  return static_cast<MarkMacroArgTokensVisitor *>(client_data)
      ->visit(cursor, parent);
}

/// Used by \c annotatePreprocessorTokens.
/// \returns true if lexing was finished, false otherwise.
static bool lexNext(Lexer &Lex, Token &Tok, unsigned &NextIdx,
                    unsigned NumTokens) {
  if (NextIdx >= NumTokens)
    return true;

  ++NextIdx;
  Lex.LexFromRawLexer(Tok);
  return Tok.is(tok::eof);
}

static void annotatePreprocessorTokens(CXTranslationUnit TU,
                                       SourceRange RegionOfInterest,
                                       CXCursor *Cursors, CXToken *Tokens,
                                       unsigned NumTokens) {
  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);

  Preprocessor &PP = CXXUnit->getPreprocessor();
  SourceManager &SourceMgr = CXXUnit->getSourceManager();
  std::pair<FileID, unsigned> BeginLocInfo =
      SourceMgr.getDecomposedSpellingLoc(RegionOfInterest.getBegin());
  std::pair<FileID, unsigned> EndLocInfo =
      SourceMgr.getDecomposedSpellingLoc(RegionOfInterest.getEnd());

  if (BeginLocInfo.first != EndLocInfo.first)
    return;

  StringRef Buffer;
  bool Invalid = false;
  Buffer = SourceMgr.getBufferData(BeginLocInfo.first, &Invalid);
  if (Buffer.empty() || Invalid)
    return;

  Lexer Lex(SourceMgr.getLocForStartOfFile(BeginLocInfo.first),
            CXXUnit->getASTContext().getLangOpts(), Buffer.begin(),
            Buffer.data() + BeginLocInfo.second, Buffer.end());
  Lex.SetCommentRetentionState(true);

  unsigned NextIdx = 0;
  // Lex tokens in raw mode until we hit the end of the range, to avoid
  // entering #includes or expanding macros.
  while (true) {
    Token Tok;
    if (lexNext(Lex, Tok, NextIdx, NumTokens))
      break;
    unsigned TokIdx = NextIdx - 1;
    assert(Tok.getLocation() ==
           SourceLocation::getFromRawEncoding(Tokens[TokIdx].int_data[1]));

  reprocess:
    if (Tok.is(tok::hash) && Tok.isAtStartOfLine()) {
      // We have found a preprocessing directive. Annotate the tokens
      // appropriately.
      //
      // FIXME: Some simple tests here could identify macro definitions and
      // #undefs, to provide specific cursor kinds for those.

      SourceLocation BeginLoc = Tok.getLocation();
      if (lexNext(Lex, Tok, NextIdx, NumTokens))
        break;

      MacroInfo *MI = nullptr;
      if (Tok.is(tok::raw_identifier) && Tok.getRawIdentifier() == "define") {
        if (lexNext(Lex, Tok, NextIdx, NumTokens))
          break;

        if (Tok.is(tok::raw_identifier)) {
          IdentifierInfo &II =
              PP.getIdentifierTable().get(Tok.getRawIdentifier());
          SourceLocation MappedTokLoc =
              CXXUnit->mapLocationToPreamble(Tok.getLocation());
          MI = getMacroInfo(II, MappedTokLoc, TU);
        }
      }

      bool finished = false;
      do {
        if (lexNext(Lex, Tok, NextIdx, NumTokens)) {
          finished = true;
          break;
        }
        // If we are in a macro definition, check if the token was ever a
        // macro name and annotate it if that's the case.
        if (MI) {
          SourceLocation SaveLoc = Tok.getLocation();
          Tok.setLocation(CXXUnit->mapLocationToPreamble(SaveLoc));
          MacroDefinitionRecord *MacroDef =
              checkForMacroInMacroDefinition(MI, Tok, TU);
          Tok.setLocation(SaveLoc);
          if (MacroDef)
            Cursors[NextIdx - 1] =
                MakeMacroExpansionCursor(MacroDef, Tok.getLocation(), TU);
        }
      } while (!Tok.isAtStartOfLine());

      unsigned LastIdx = finished ? NextIdx - 1 : NextIdx - 2;
      assert(TokIdx <= LastIdx);
      SourceLocation EndLoc =
          SourceLocation::getFromRawEncoding(Tokens[LastIdx].int_data[1]);
      CXCursor Cursor =
          MakePreprocessingDirectiveCursor(SourceRange(BeginLoc, EndLoc), TU);

      for (; TokIdx <= LastIdx; ++TokIdx)
        updateCursorAnnotation(Cursors[TokIdx], Cursor);

      if (finished)
        break;
      goto reprocess;
    }
  }
}

// This gets run a separate thread to avoid stack blowout.
static void clang_annotateTokensImpl(CXTranslationUnit TU, ASTUnit *CXXUnit,
                                     CXToken *Tokens, unsigned NumTokens,
                                     CXCursor *Cursors) {
  CIndexer *CXXIdx = TU->CIdx;
  if (CXXIdx->isOptEnabled(CXGlobalOpt_ThreadBackgroundPriorityForEditing))
    setThreadBackgroundPriority();

  // Determine the region of interest, which contains all of the tokens.
  SourceRange RegionOfInterest;
  RegionOfInterest.setBegin(
      cxloc::translateSourceLocation(clang_getTokenLocation(TU, Tokens[0])));
  RegionOfInterest.setEnd(cxloc::translateSourceLocation(
      clang_getTokenLocation(TU, Tokens[NumTokens - 1])));

  // Relex the tokens within the source range to look for preprocessing
  // directives.
  annotatePreprocessorTokens(TU, RegionOfInterest, Cursors, Tokens, NumTokens);

  // If begin location points inside a macro argument, set it to the expansion
  // location so we can have the full context when annotating semantically.
  {
    SourceManager &SM = CXXUnit->getSourceManager();
    SourceLocation Loc =
        SM.getMacroArgExpandedLocation(RegionOfInterest.getBegin());
    if (Loc.isMacroID())
      RegionOfInterest.setBegin(SM.getExpansionLoc(Loc));
  }

  if (CXXUnit->getPreprocessor().getPreprocessingRecord()) {
    // Search and mark tokens that are macro argument expansions.
    MarkMacroArgTokensVisitor Visitor(CXXUnit->getSourceManager(), Tokens,
                                      NumTokens);
    CursorVisitor MacroArgMarker(
        TU, MarkMacroArgTokensVisitorDelegate, &Visitor,
        /*VisitPreprocessorLast=*/true,
        /*VisitIncludedEntities=*/false, RegionOfInterest);
    MacroArgMarker.visitPreprocessedEntitiesInRegion();
  }

  // Annotate all of the source locations in the region of interest that map to
  // a specific cursor.
  AnnotateTokensWorker W(Tokens, Cursors, NumTokens, TU, RegionOfInterest);

  // FIXME: We use a ridiculous stack size here because the data-recursion
  // algorithm uses a large stack frame than the non-data recursive version,
  // and AnnotationTokensWorker currently transforms the data-recursion
  // algorithm back into a traditional recursion by explicitly calling
  // VisitChildren().  We will need to remove this explicit recursive call.
  W.AnnotateTokens();

  // If we ran into any entities that involve context-sensitive keywords,
  // take another pass through the tokens to mark them as such.
  if (W.hasContextSensitiveKeywords()) {
    for (unsigned I = 0; I != NumTokens; ++I) {
      if (clang_getTokenKind(Tokens[I]) != CXToken_Identifier)
        continue;

      if (Cursors[I].kind == CXCursor_ObjCPropertyDecl) {
        IdentifierInfo *II = static_cast<IdentifierInfo *>(Tokens[I].ptr_data);
        if (const ObjCPropertyDecl *Property =
                dyn_cast_or_null<ObjCPropertyDecl>(getCursorDecl(Cursors[I]))) {
          if (Property->getPropertyAttributesAsWritten() != 0 &&
              llvm::StringSwitch<bool>(II->getName())
                  .Case("readonly", true)
                  .Case("assign", true)
                  .Case("unsafe_unretained", true)
                  .Case("readwrite", true)
                  .Case("retain", true)
                  .Case("copy", true)
                  .Case("nonatomic", true)
                  .Case("atomic", true)
                  .Case("getter", true)
                  .Case("setter", true)
                  .Case("strong", true)
                  .Case("weak", true)
                  .Case("class", true)
                  .Default(false))
            Tokens[I].int_data[0] = CXToken_Keyword;
        }
        continue;
      }

      if (Cursors[I].kind == CXCursor_ObjCInstanceMethodDecl ||
          Cursors[I].kind == CXCursor_ObjCClassMethodDecl) {
        IdentifierInfo *II = static_cast<IdentifierInfo *>(Tokens[I].ptr_data);
        if (llvm::StringSwitch<bool>(II->getName())
                .Case("in", true)
                .Case("out", true)
                .Case("inout", true)
                .Case("oneway", true)
                .Case("bycopy", true)
                .Case("byref", true)
                .Default(false))
          Tokens[I].int_data[0] = CXToken_Keyword;
        continue;
      }

      if (Cursors[I].kind == CXCursor_CXXFinalAttr ||
          Cursors[I].kind == CXCursor_CXXOverrideAttr) {
        Tokens[I].int_data[0] = CXToken_Keyword;
        continue;
      }
    }
  }
}

void clang_annotateTokens(CXTranslationUnit TU, CXToken *Tokens,
                          unsigned NumTokens, CXCursor *Cursors) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return;
  }
  if (NumTokens == 0 || !Tokens || !Cursors) {
    LOG_FUNC_SECTION { *Log << "<null input>"; }
    return;
  }

  LOG_FUNC_SECTION {
    *Log << TU << ' ';
    CXSourceLocation bloc = clang_getTokenLocation(TU, Tokens[0]);
    CXSourceLocation eloc = clang_getTokenLocation(TU, Tokens[NumTokens - 1]);
    *Log << clang_getRange(bloc, eloc);
  }

  // Any token we don't specifically annotate will have a NULL cursor.
  CXCursor C = clang_getNullCursor();
  for (unsigned I = 0; I != NumTokens; ++I)
    Cursors[I] = C;

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit)
    return;

  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  auto AnnotateTokensImpl = [=]() {
    clang_annotateTokensImpl(TU, CXXUnit, Tokens, NumTokens, Cursors);
  };
  llvm::CrashRecoveryContext CRC;
  if (!RunSafely(CRC, AnnotateTokensImpl, GetSafetyThreadStackSize() * 2)) {
    fprintf(stderr, "libclang: crash detected while annotating tokens\n");
  }
}

//===----------------------------------------------------------------------===//
// Operations for querying linkage of a cursor.
//===----------------------------------------------------------------------===//

CXLinkageKind clang_getCursorLinkage(CXCursor cursor) {
  if (!clang_isDeclaration(cursor.kind))
    return CXLinkage_Invalid;

  const Decl *D = cxcursor::getCursorDecl(cursor);
  if (const NamedDecl *ND = dyn_cast_or_null<NamedDecl>(D))
    switch (ND->getLinkageInternal()) {
    case Linkage::Invalid:
      return CXLinkage_Invalid;
    case Linkage::None:
    case Linkage::VisibleNone:
      return CXLinkage_NoLinkage;
    case Linkage::Internal:
      return CXLinkage_Internal;
    case Linkage::UniqueExternal:
      return CXLinkage_UniqueExternal;
    case Linkage::Module:
    case Linkage::External:
      return CXLinkage_External;
    };

  return CXLinkage_Invalid;
}

//===----------------------------------------------------------------------===//
// Operations for querying visibility of a cursor.
//===----------------------------------------------------------------------===//

CXVisibilityKind clang_getCursorVisibility(CXCursor cursor) {
  if (!clang_isDeclaration(cursor.kind))
    return CXVisibility_Invalid;

  const Decl *D = cxcursor::getCursorDecl(cursor);
  if (const NamedDecl *ND = dyn_cast_or_null<NamedDecl>(D))
    switch (ND->getVisibility()) {
    case HiddenVisibility:
      return CXVisibility_Hidden;
    case ProtectedVisibility:
      return CXVisibility_Protected;
    case DefaultVisibility:
      return CXVisibility_Default;
    };

  return CXVisibility_Invalid;
}

//===----------------------------------------------------------------------===//
// Operations for querying language of a cursor.
//===----------------------------------------------------------------------===//

static CXLanguageKind getDeclLanguage(const Decl *D) {
  if (!D)
    return CXLanguage_C;

  switch (D->getKind()) {
  default:
    break;
  case Decl::ImplicitParam:
  case Decl::ObjCAtDefsField:
  case Decl::ObjCCategory:
  case Decl::ObjCCategoryImpl:
  case Decl::ObjCCompatibleAlias:
  case Decl::ObjCImplementation:
  case Decl::ObjCInterface:
  case Decl::ObjCIvar:
  case Decl::ObjCMethod:
  case Decl::ObjCProperty:
  case Decl::ObjCPropertyImpl:
  case Decl::ObjCProtocol:
  case Decl::ObjCTypeParam:
    return CXLanguage_ObjC;
  case Decl::CXXConstructor:
  case Decl::CXXConversion:
  case Decl::CXXDestructor:
  case Decl::CXXMethod:
  case Decl::CXXRecord:
  case Decl::ClassTemplate:
  case Decl::ClassTemplatePartialSpecialization:
  case Decl::ClassTemplateSpecialization:
  case Decl::Friend:
  case Decl::FriendTemplate:
  case Decl::FunctionTemplate:
  case Decl::LinkageSpec:
  case Decl::Namespace:
  case Decl::NamespaceAlias:
  case Decl::NonTypeTemplateParm:
  case Decl::StaticAssert:
  case Decl::TemplateTemplateParm:
  case Decl::TemplateTypeParm:
  case Decl::UnresolvedUsingTypename:
  case Decl::UnresolvedUsingValue:
  case Decl::Using:
  case Decl::UsingDirective:
  case Decl::UsingShadow:
    return CXLanguage_CPlusPlus;
  }

  return CXLanguage_C;
}

static CXAvailabilityKind getCursorAvailabilityForDecl(const Decl *D) {
  if (isa<FunctionDecl>(D) && cast<FunctionDecl>(D)->isDeleted())
    return CXAvailability_NotAvailable;

  switch (D->getAvailability()) {
  case AR_Available:
  case AR_NotYetIntroduced:
    if (const EnumConstantDecl *EnumConst = dyn_cast<EnumConstantDecl>(D))
      return getCursorAvailabilityForDecl(
          cast<Decl>(EnumConst->getDeclContext()));
    return CXAvailability_Available;

  case AR_Deprecated:
    return CXAvailability_Deprecated;

  case AR_Unavailable:
    return CXAvailability_NotAvailable;
  }

  llvm_unreachable("Unknown availability kind!");
}

enum CXAvailabilityKind clang_getCursorAvailability(CXCursor cursor) {
  if (clang_isDeclaration(cursor.kind))
    if (const Decl *D = cxcursor::getCursorDecl(cursor))
      return getCursorAvailabilityForDecl(D);

  return CXAvailability_Available;
}

static CXVersion convertVersion(VersionTuple In) {
  CXVersion Out = {-1, -1, -1};
  if (In.empty())
    return Out;

  Out.Major = In.getMajor();

  std::optional<unsigned> Minor = In.getMinor();
  if (Minor)
    Out.Minor = *Minor;
  else
    return Out;

  std::optional<unsigned> Subminor = In.getSubminor();
  if (Subminor)
    Out.Subminor = *Subminor;

  return Out;
}

static void getCursorPlatformAvailabilityForDecl(
    const Decl *D, int *always_deprecated, CXString *deprecated_message,
    int *always_unavailable, CXString *unavailable_message,
    SmallVectorImpl<AvailabilityAttr *> &AvailabilityAttrs) {
  bool HadAvailAttr = false;
  for (auto A : D->attrs()) {
    if (DeprecatedAttr *Deprecated = dyn_cast<DeprecatedAttr>(A)) {
      HadAvailAttr = true;
      if (always_deprecated)
        *always_deprecated = 1;
      if (deprecated_message) {
        clang_disposeString(*deprecated_message);
        *deprecated_message = cxstring::createDup(Deprecated->getMessage());
      }
      continue;
    }

    if (UnavailableAttr *Unavailable = dyn_cast<UnavailableAttr>(A)) {
      HadAvailAttr = true;
      if (always_unavailable)
        *always_unavailable = 1;
      if (unavailable_message) {
        clang_disposeString(*unavailable_message);
        *unavailable_message = cxstring::createDup(Unavailable->getMessage());
      }
      continue;
    }

    if (AvailabilityAttr *Avail = dyn_cast<AvailabilityAttr>(A)) {
      AvailabilityAttrs.push_back(Avail);
      HadAvailAttr = true;
    }
  }

  if (!HadAvailAttr)
    if (const EnumConstantDecl *EnumConst = dyn_cast<EnumConstantDecl>(D))
      return getCursorPlatformAvailabilityForDecl(
          cast<Decl>(EnumConst->getDeclContext()), always_deprecated,
          deprecated_message, always_unavailable, unavailable_message,
          AvailabilityAttrs);

  // If no availability attributes are found, inherit the attribute from the
  // containing decl or the class or category interface decl.
  if (AvailabilityAttrs.empty()) {
    const ObjCContainerDecl *CD = nullptr;
    const DeclContext *DC = D->getDeclContext();

    if (auto *IMD = dyn_cast<ObjCImplementationDecl>(D))
      CD = IMD->getClassInterface();
    else if (auto *CatD = dyn_cast<ObjCCategoryDecl>(D))
      CD = CatD->getClassInterface();
    else if (auto *IMD = dyn_cast<ObjCCategoryImplDecl>(D))
      CD = IMD->getCategoryDecl();
    else if (auto *ID = dyn_cast<ObjCInterfaceDecl>(DC))
      CD = ID;
    else if (auto *CatD = dyn_cast<ObjCCategoryDecl>(DC))
      CD = CatD;
    else if (auto *IMD = dyn_cast<ObjCImplementationDecl>(DC))
      CD = IMD->getClassInterface();
    else if (auto *IMD = dyn_cast<ObjCCategoryImplDecl>(DC))
      CD = IMD->getCategoryDecl();
    else if (auto *PD = dyn_cast<ObjCProtocolDecl>(DC))
      CD = PD;

    if (CD)
      getCursorPlatformAvailabilityForDecl(
          CD, always_deprecated, deprecated_message, always_unavailable,
          unavailable_message, AvailabilityAttrs);
    return;
  }

  llvm::sort(
      AvailabilityAttrs, [](AvailabilityAttr *LHS, AvailabilityAttr *RHS) {
        return LHS->getPlatform()->getName() < RHS->getPlatform()->getName();
      });
  ASTContext &Ctx = D->getASTContext();
  auto It = std::unique(
      AvailabilityAttrs.begin(), AvailabilityAttrs.end(),
      [&Ctx](AvailabilityAttr *LHS, AvailabilityAttr *RHS) {
        if (LHS->getPlatform() != RHS->getPlatform())
          return false;

        if (LHS->getIntroduced() == RHS->getIntroduced() &&
            LHS->getDeprecated() == RHS->getDeprecated() &&
            LHS->getObsoleted() == RHS->getObsoleted() &&
            LHS->getMessage() == RHS->getMessage() &&
            LHS->getReplacement() == RHS->getReplacement())
          return true;

        if ((!LHS->getIntroduced().empty() && !RHS->getIntroduced().empty()) ||
            (!LHS->getDeprecated().empty() && !RHS->getDeprecated().empty()) ||
            (!LHS->getObsoleted().empty() && !RHS->getObsoleted().empty()))
          return false;

        if (LHS->getIntroduced().empty() && !RHS->getIntroduced().empty())
          LHS->setIntroduced(Ctx, RHS->getIntroduced());

        if (LHS->getDeprecated().empty() && !RHS->getDeprecated().empty()) {
          LHS->setDeprecated(Ctx, RHS->getDeprecated());
          if (LHS->getMessage().empty())
            LHS->setMessage(Ctx, RHS->getMessage());
          if (LHS->getReplacement().empty())
            LHS->setReplacement(Ctx, RHS->getReplacement());
        }

        if (LHS->getObsoleted().empty() && !RHS->getObsoleted().empty()) {
          LHS->setObsoleted(Ctx, RHS->getObsoleted());
          if (LHS->getMessage().empty())
            LHS->setMessage(Ctx, RHS->getMessage());
          if (LHS->getReplacement().empty())
            LHS->setReplacement(Ctx, RHS->getReplacement());
        }

        return true;
      });
  AvailabilityAttrs.erase(It, AvailabilityAttrs.end());
}

int clang_getCursorPlatformAvailability(CXCursor cursor, int *always_deprecated,
                                        CXString *deprecated_message,
                                        int *always_unavailable,
                                        CXString *unavailable_message,
                                        CXPlatformAvailability *availability,
                                        int availability_size) {
  if (always_deprecated)
    *always_deprecated = 0;
  if (deprecated_message)
    *deprecated_message = cxstring::createEmpty();
  if (always_unavailable)
    *always_unavailable = 0;
  if (unavailable_message)
    *unavailable_message = cxstring::createEmpty();

  if (!clang_isDeclaration(cursor.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(cursor);
  if (!D)
    return 0;

  SmallVector<AvailabilityAttr *, 8> AvailabilityAttrs;
  getCursorPlatformAvailabilityForDecl(D, always_deprecated, deprecated_message,
                                       always_unavailable, unavailable_message,
                                       AvailabilityAttrs);
  for (const auto &Avail : llvm::enumerate(
           llvm::ArrayRef(AvailabilityAttrs).take_front(availability_size))) {
    availability[Avail.index()].Platform =
        cxstring::createDup(Avail.value()->getPlatform()->getName());
    availability[Avail.index()].Introduced =
        convertVersion(Avail.value()->getIntroduced());
    availability[Avail.index()].Deprecated =
        convertVersion(Avail.value()->getDeprecated());
    availability[Avail.index()].Obsoleted =
        convertVersion(Avail.value()->getObsoleted());
    availability[Avail.index()].Unavailable = Avail.value()->getUnavailable();
    availability[Avail.index()].Message =
        cxstring::createDup(Avail.value()->getMessage());
  }

  return AvailabilityAttrs.size();
}

void clang_disposeCXPlatformAvailability(CXPlatformAvailability *availability) {
  clang_disposeString(availability->Platform);
  clang_disposeString(availability->Message);
}

CXLanguageKind clang_getCursorLanguage(CXCursor cursor) {
  if (clang_isDeclaration(cursor.kind))
    return getDeclLanguage(cxcursor::getCursorDecl(cursor));

  return CXLanguage_Invalid;
}

CXTLSKind clang_getCursorTLSKind(CXCursor cursor) {
  const Decl *D = cxcursor::getCursorDecl(cursor);
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    switch (VD->getTLSKind()) {
    case VarDecl::TLS_None:
      return CXTLS_None;
    case VarDecl::TLS_Dynamic:
      return CXTLS_Dynamic;
    case VarDecl::TLS_Static:
      return CXTLS_Static;
    }
  }

  return CXTLS_None;
}

/// If the given cursor is the "templated" declaration
/// describing a class or function template, return the class or
/// function template.
static const Decl *maybeGetTemplateCursor(const Decl *D) {
  if (!D)
    return nullptr;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    if (FunctionTemplateDecl *FunTmpl = FD->getDescribedFunctionTemplate())
      return FunTmpl;

  if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(D))
    if (ClassTemplateDecl *ClassTmpl = RD->getDescribedClassTemplate())
      return ClassTmpl;

  return D;
}

enum CX_StorageClass clang_Cursor_getStorageClass(CXCursor C) {
  StorageClass sc = SC_None;
  const Decl *D = getCursorDecl(C);
  if (D) {
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      sc = FD->getStorageClass();
    } else if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
      sc = VD->getStorageClass();
    } else {
      return CX_SC_Invalid;
    }
  } else {
    return CX_SC_Invalid;
  }
  switch (sc) {
  case SC_None:
    return CX_SC_None;
  case SC_Extern:
    return CX_SC_Extern;
  case SC_Static:
    return CX_SC_Static;
  case SC_PrivateExtern:
    return CX_SC_PrivateExtern;
  case SC_Auto:
    return CX_SC_Auto;
  case SC_Register:
    return CX_SC_Register;
  }
  llvm_unreachable("Unhandled storage class!");
}

CXCursor clang_getCursorSemanticParent(CXCursor cursor) {
  if (clang_isDeclaration(cursor.kind)) {
    if (const Decl *D = getCursorDecl(cursor)) {
      const DeclContext *DC = D->getDeclContext();
      if (!DC)
        return clang_getNullCursor();

      return MakeCXCursor(maybeGetTemplateCursor(cast<Decl>(DC)),
                          getCursorTU(cursor));
    }
  }

  if (clang_isStatement(cursor.kind) || clang_isExpression(cursor.kind)) {
    if (const Decl *D = getCursorDecl(cursor))
      return MakeCXCursor(D, getCursorTU(cursor));
  }

  return clang_getNullCursor();
}

CXCursor clang_getCursorLexicalParent(CXCursor cursor) {
  if (clang_isDeclaration(cursor.kind)) {
    if (const Decl *D = getCursorDecl(cursor)) {
      const DeclContext *DC = D->getLexicalDeclContext();
      if (!DC)
        return clang_getNullCursor();

      return MakeCXCursor(maybeGetTemplateCursor(cast<Decl>(DC)),
                          getCursorTU(cursor));
    }
  }

  // FIXME: Note that we can't easily compute the lexical context of a
  // statement or expression, so we return nothing.
  return clang_getNullCursor();
}

CXFile clang_getIncludedFile(CXCursor cursor) {
  if (cursor.kind != CXCursor_InclusionDirective)
    return nullptr;

  const InclusionDirective *ID = getCursorInclusionDirective(cursor);
  return cxfile::makeCXFile(ID->getFile());
}

unsigned clang_Cursor_getObjCPropertyAttributes(CXCursor C, unsigned reserved) {
  if (C.kind != CXCursor_ObjCPropertyDecl)
    return CXObjCPropertyAttr_noattr;

  unsigned Result = CXObjCPropertyAttr_noattr;
  const auto *PD = cast<ObjCPropertyDecl>(getCursorDecl(C));
  ObjCPropertyAttribute::Kind Attr = PD->getPropertyAttributesAsWritten();

#define SET_CXOBJCPROP_ATTR(A)                                                 \
  if (Attr & ObjCPropertyAttribute::kind_##A)                                  \
  Result |= CXObjCPropertyAttr_##A
  SET_CXOBJCPROP_ATTR(readonly);
  SET_CXOBJCPROP_ATTR(getter);
  SET_CXOBJCPROP_ATTR(assign);
  SET_CXOBJCPROP_ATTR(readwrite);
  SET_CXOBJCPROP_ATTR(retain);
  SET_CXOBJCPROP_ATTR(copy);
  SET_CXOBJCPROP_ATTR(nonatomic);
  SET_CXOBJCPROP_ATTR(setter);
  SET_CXOBJCPROP_ATTR(atomic);
  SET_CXOBJCPROP_ATTR(weak);
  SET_CXOBJCPROP_ATTR(strong);
  SET_CXOBJCPROP_ATTR(unsafe_unretained);
  SET_CXOBJCPROP_ATTR(class);
#undef SET_CXOBJCPROP_ATTR

  return Result;
}

CXString clang_Cursor_getObjCPropertyGetterName(CXCursor C) {
  if (C.kind != CXCursor_ObjCPropertyDecl)
    return cxstring::createNull();

  const auto *PD = cast<ObjCPropertyDecl>(getCursorDecl(C));
  Selector sel = PD->getGetterName();
  if (sel.isNull())
    return cxstring::createNull();

  return cxstring::createDup(sel.getAsString());
}

CXString clang_Cursor_getObjCPropertySetterName(CXCursor C) {
  if (C.kind != CXCursor_ObjCPropertyDecl)
    return cxstring::createNull();

  const auto *PD = cast<ObjCPropertyDecl>(getCursorDecl(C));
  Selector sel = PD->getSetterName();
  if (sel.isNull())
    return cxstring::createNull();

  return cxstring::createDup(sel.getAsString());
}

unsigned clang_Cursor_getObjCDeclQualifiers(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return CXObjCDeclQualifier_None;

  Decl::ObjCDeclQualifier QT = Decl::OBJC_TQ_None;
  const Decl *D = getCursorDecl(C);
  if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D))
    QT = MD->getObjCDeclQualifier();
  else if (const ParmVarDecl *PD = dyn_cast<ParmVarDecl>(D))
    QT = PD->getObjCDeclQualifier();
  if (QT == Decl::OBJC_TQ_None)
    return CXObjCDeclQualifier_None;

  unsigned Result = CXObjCDeclQualifier_None;
  if (QT & Decl::OBJC_TQ_In)
    Result |= CXObjCDeclQualifier_In;
  if (QT & Decl::OBJC_TQ_Inout)
    Result |= CXObjCDeclQualifier_Inout;
  if (QT & Decl::OBJC_TQ_Out)
    Result |= CXObjCDeclQualifier_Out;
  if (QT & Decl::OBJC_TQ_Bycopy)
    Result |= CXObjCDeclQualifier_Bycopy;
  if (QT & Decl::OBJC_TQ_Byref)
    Result |= CXObjCDeclQualifier_Byref;
  if (QT & Decl::OBJC_TQ_Oneway)
    Result |= CXObjCDeclQualifier_Oneway;

  return Result;
}

unsigned clang_Cursor_isObjCOptional(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = getCursorDecl(C);
  if (const ObjCPropertyDecl *PD = dyn_cast<ObjCPropertyDecl>(D))
    return PD->getPropertyImplementation() == ObjCPropertyDecl::Optional;
  if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->getImplementationControl() ==
           ObjCImplementationControl::Optional;

  return 0;
}

unsigned clang_Cursor_isVariadic(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = getCursorDecl(C);
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    return FD->isVariadic();
  if (const ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->isVariadic();

  return 0;
}

unsigned clang_Cursor_isExternalSymbol(CXCursor C, CXString *language,
                                       CXString *definedIn,
                                       unsigned *isGenerated) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = getCursorDecl(C);

  if (auto *attr = D->getExternalSourceSymbolAttr()) {
    if (language)
      *language = cxstring::createDup(attr->getLanguage());
    if (definedIn)
      *definedIn = cxstring::createDup(attr->getDefinedIn());
    if (isGenerated)
      *isGenerated = attr->getGeneratedDeclaration();
    return 1;
  }
  return 0;
}

enum CX_BinaryOperatorKind clang_Cursor_getBinaryOpcode(CXCursor C) {
  if (C.kind != CXCursor_BinaryOperator &&
      C.kind != CXCursor_CompoundAssignOperator) {
    return CX_BO_Invalid;
  }

  const Expr *D = getCursorExpr(C);
  if (const auto *BinOp = dyn_cast<BinaryOperator>(D)) {
    switch (BinOp->getOpcode()) {
#define BINARY_OPERATION(Name, Spelling)                                       \
  case BO_##Name:                                                              \
    return CX_BO_##Name;
#include "clang/AST/OperationKinds.def"
    }
  }

  return CX_BO_Invalid;
}

CXString clang_Cursor_getBinaryOpcodeStr(enum CX_BinaryOperatorKind Op) {
  if (Op > CX_BO_LAST)
    return cxstring::createEmpty();

  return cxstring::createDup(
      // BinaryOperator::getOpcodeStr has no case for CX_BO_Invalid,
      // so subtract 1
      BinaryOperator::getOpcodeStr(static_cast<BinaryOperatorKind>(Op - 1)));
}

CXSourceRange clang_Cursor_getCommentRange(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return clang_getNullRange();

  const Decl *D = getCursorDecl(C);
  ASTContext &Context = getCursorContext(C);
  const RawComment *RC = Context.getRawCommentForAnyRedecl(D);
  if (!RC)
    return clang_getNullRange();

  return cxloc::translateSourceRange(Context, RC->getSourceRange());
}

CXString clang_Cursor_getRawCommentText(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return cxstring::createNull();

  const Decl *D = getCursorDecl(C);
  ASTContext &Context = getCursorContext(C);
  const RawComment *RC = Context.getRawCommentForAnyRedecl(D);
  StringRef RawText =
      RC ? RC->getRawText(Context.getSourceManager()) : StringRef();

  // Don't duplicate the string because RawText points directly into source
  // code.
  return cxstring::createRef(RawText);
}

CXString clang_Cursor_getBriefCommentText(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return cxstring::createNull();

  const Decl *D = getCursorDecl(C);
  const ASTContext &Context = getCursorContext(C);
  const RawComment *RC = Context.getRawCommentForAnyRedecl(D);

  if (RC) {
    StringRef BriefText = RC->getBriefText(Context);

    // Don't duplicate the string because RawComment ensures that this memory
    // will not go away.
    return cxstring::createRef(BriefText);
  }

  return cxstring::createNull();
}

CXModule clang_Cursor_getModule(CXCursor C) {
  if (C.kind == CXCursor_ModuleImportDecl) {
    if (const ImportDecl *ImportD =
            dyn_cast_or_null<ImportDecl>(getCursorDecl(C)))
      return ImportD->getImportedModule();
  }

  return nullptr;
}

CXModule clang_getModuleForFile(CXTranslationUnit TU, CXFile File) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return nullptr;
  }
  if (!File)
    return nullptr;
  FileEntryRef FE = *cxfile::getFileEntryRef(File);

  ASTUnit &Unit = *cxtu::getASTUnit(TU);
  HeaderSearch &HS = Unit.getPreprocessor().getHeaderSearchInfo();
  ModuleMap::KnownHeader Header = HS.findModuleForHeader(FE);

  return Header.getModule();
}

CXFile clang_Module_getASTFile(CXModule CXMod) {
  if (!CXMod)
    return nullptr;
  Module *Mod = static_cast<Module *>(CXMod);
  return cxfile::makeCXFile(Mod->getASTFile());
}

CXModule clang_Module_getParent(CXModule CXMod) {
  if (!CXMod)
    return nullptr;
  Module *Mod = static_cast<Module *>(CXMod);
  return Mod->Parent;
}

CXString clang_Module_getName(CXModule CXMod) {
  if (!CXMod)
    return cxstring::createEmpty();
  Module *Mod = static_cast<Module *>(CXMod);
  return cxstring::createDup(Mod->Name);
}

CXString clang_Module_getFullName(CXModule CXMod) {
  if (!CXMod)
    return cxstring::createEmpty();
  Module *Mod = static_cast<Module *>(CXMod);
  return cxstring::createDup(Mod->getFullModuleName());
}

int clang_Module_isSystem(CXModule CXMod) {
  if (!CXMod)
    return 0;
  Module *Mod = static_cast<Module *>(CXMod);
  return Mod->IsSystem;
}

unsigned clang_Module_getNumTopLevelHeaders(CXTranslationUnit TU,
                                            CXModule CXMod) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return 0;
  }
  if (!CXMod)
    return 0;
  Module *Mod = static_cast<Module *>(CXMod);
  FileManager &FileMgr = cxtu::getASTUnit(TU)->getFileManager();
  ArrayRef<FileEntryRef> TopHeaders = Mod->getTopHeaders(FileMgr);
  return TopHeaders.size();
}

CXFile clang_Module_getTopLevelHeader(CXTranslationUnit TU, CXModule CXMod,
                                      unsigned Index) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return nullptr;
  }
  if (!CXMod)
    return nullptr;
  Module *Mod = static_cast<Module *>(CXMod);
  FileManager &FileMgr = cxtu::getASTUnit(TU)->getFileManager();

  ArrayRef<FileEntryRef> TopHeaders = Mod->getTopHeaders(FileMgr);
  if (Index < TopHeaders.size())
    return cxfile::makeCXFile(TopHeaders[Index]);

  return nullptr;
}

//===----------------------------------------------------------------------===//
// C++ AST instrospection.
//===----------------------------------------------------------------------===//

unsigned clang_CXXConstructor_isDefaultConstructor(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXConstructorDecl *Constructor =
      D ? dyn_cast_or_null<CXXConstructorDecl>(D->getAsFunction()) : nullptr;
  return (Constructor && Constructor->isDefaultConstructor()) ? 1 : 0;
}

unsigned clang_CXXConstructor_isCopyConstructor(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXConstructorDecl *Constructor =
      D ? dyn_cast_or_null<CXXConstructorDecl>(D->getAsFunction()) : nullptr;
  return (Constructor && Constructor->isCopyConstructor()) ? 1 : 0;
}

unsigned clang_CXXConstructor_isMoveConstructor(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXConstructorDecl *Constructor =
      D ? dyn_cast_or_null<CXXConstructorDecl>(D->getAsFunction()) : nullptr;
  return (Constructor && Constructor->isMoveConstructor()) ? 1 : 0;
}

unsigned clang_CXXConstructor_isConvertingConstructor(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXConstructorDecl *Constructor =
      D ? dyn_cast_or_null<CXXConstructorDecl>(D->getAsFunction()) : nullptr;
  // Passing 'false' excludes constructors marked 'explicit'.
  return (Constructor && Constructor->isConvertingConstructor(false)) ? 1 : 0;
}

unsigned clang_CXXField_isMutable(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  if (const auto D = cxcursor::getCursorDecl(C))
    if (const auto FD = dyn_cast_or_null<FieldDecl>(D))
      return FD->isMutable() ? 1 : 0;
  return 0;
}

unsigned clang_CXXMethod_isPureVirtual(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;
  return (Method && Method->isPureVirtual()) ? 1 : 0;
}

unsigned clang_CXXMethod_isConst(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;
  return (Method && Method->getMethodQualifiers().hasConst()) ? 1 : 0;
}

unsigned clang_CXXMethod_isDefaulted(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;
  return (Method && Method->isDefaulted()) ? 1 : 0;
}

unsigned clang_CXXMethod_isDeleted(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_if_present<CXXMethodDecl>(D->getAsFunction()) : nullptr;
  return (Method && Method->isDeleted()) ? 1 : 0;
}

unsigned clang_CXXMethod_isStatic(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;
  return (Method && Method->isStatic()) ? 1 : 0;
}

unsigned clang_CXXMethod_isVirtual(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;
  return (Method && Method->isVirtual()) ? 1 : 0;
}

unsigned clang_CXXMethod_isCopyAssignmentOperator(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;

  return (Method && Method->isCopyAssignmentOperator()) ? 1 : 0;
}

unsigned clang_CXXMethod_isMoveAssignmentOperator(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const CXXMethodDecl *Method =
      D ? dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()) : nullptr;

  return (Method && Method->isMoveAssignmentOperator()) ? 1 : 0;
}

unsigned clang_CXXMethod_isExplicit(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  const FunctionDecl *FD = D->getAsFunction();

  if (!FD)
    return 0;

  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(FD))
    return Ctor->isExplicit();

  if (const auto *Conv = dyn_cast<CXXConversionDecl>(FD))
    return Conv->isExplicit();

  return 0;
}

unsigned clang_CXXRecord_isAbstract(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const auto *D = cxcursor::getCursorDecl(C);
  const auto *RD = dyn_cast_or_null<CXXRecordDecl>(D);
  if (RD)
    RD = RD->getDefinition();
  return (RD && RD->isAbstract()) ? 1 : 0;
}

unsigned clang_EnumDecl_isScoped(CXCursor C) {
  if (!clang_isDeclaration(C.kind))
    return 0;

  const Decl *D = cxcursor::getCursorDecl(C);
  auto *Enum = dyn_cast_or_null<EnumDecl>(D);
  return (Enum && Enum->isScoped()) ? 1 : 0;
}

//===----------------------------------------------------------------------===//
// Attribute introspection.
//===----------------------------------------------------------------------===//

CXType clang_getIBOutletCollectionType(CXCursor C) {
  if (C.kind != CXCursor_IBOutletCollectionAttr)
    return cxtype::MakeCXType(QualType(), cxcursor::getCursorTU(C));

  const IBOutletCollectionAttr *A =
      cast<IBOutletCollectionAttr>(cxcursor::getCursorAttr(C));

  return cxtype::MakeCXType(A->getInterface(), cxcursor::getCursorTU(C));
}

//===----------------------------------------------------------------------===//
// Inspecting memory usage.
//===----------------------------------------------------------------------===//

typedef std::vector<CXTUResourceUsageEntry> MemUsageEntries;

static inline void createCXTUResourceUsageEntry(MemUsageEntries &entries,
                                                enum CXTUResourceUsageKind k,
                                                unsigned long amount) {
  CXTUResourceUsageEntry entry = {k, amount};
  entries.push_back(entry);
}

const char *clang_getTUResourceUsageName(CXTUResourceUsageKind kind) {
  const char *str = "";
  switch (kind) {
  case CXTUResourceUsage_AST:
    str = "ASTContext: expressions, declarations, and types";
    break;
  case CXTUResourceUsage_Identifiers:
    str = "ASTContext: identifiers";
    break;
  case CXTUResourceUsage_Selectors:
    str = "ASTContext: selectors";
    break;
  case CXTUResourceUsage_GlobalCompletionResults:
    str = "Code completion: cached global results";
    break;
  case CXTUResourceUsage_SourceManagerContentCache:
    str = "SourceManager: content cache allocator";
    break;
  case CXTUResourceUsage_AST_SideTables:
    str = "ASTContext: side tables";
    break;
  case CXTUResourceUsage_SourceManager_Membuffer_Malloc:
    str = "SourceManager: malloc'ed memory buffers";
    break;
  case CXTUResourceUsage_SourceManager_Membuffer_MMap:
    str = "SourceManager: mmap'ed memory buffers";
    break;
  case CXTUResourceUsage_ExternalASTSource_Membuffer_Malloc:
    str = "ExternalASTSource: malloc'ed memory buffers";
    break;
  case CXTUResourceUsage_ExternalASTSource_Membuffer_MMap:
    str = "ExternalASTSource: mmap'ed memory buffers";
    break;
  case CXTUResourceUsage_Preprocessor:
    str = "Preprocessor: malloc'ed memory";
    break;
  case CXTUResourceUsage_PreprocessingRecord:
    str = "Preprocessor: PreprocessingRecord";
    break;
  case CXTUResourceUsage_SourceManager_DataStructures:
    str = "SourceManager: data structures and tables";
    break;
  case CXTUResourceUsage_Preprocessor_HeaderSearch:
    str = "Preprocessor: header search tables";
    break;
  }
  return str;
}

CXTUResourceUsage clang_getCXTUResourceUsage(CXTranslationUnit TU) {
  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    CXTUResourceUsage usage = {(void *)nullptr, 0, nullptr};
    return usage;
  }

  ASTUnit *astUnit = cxtu::getASTUnit(TU);
  std::unique_ptr<MemUsageEntries> entries(new MemUsageEntries());
  ASTContext &astContext = astUnit->getASTContext();

  // How much memory is used by AST nodes and types?
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_AST,
      (unsigned long)astContext.getASTAllocatedMemory());

  // How much memory is used by identifiers?
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_Identifiers,
      (unsigned long)astContext.Idents.getAllocator().getTotalMemory());

  // How much memory is used for selectors?
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_Selectors,
      (unsigned long)astContext.Selectors.getTotalMemory());

  // How much memory is used by ASTContext's side tables?
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_AST_SideTables,
      (unsigned long)astContext.getSideTableAllocatedMemory());

  // How much memory is used for caching global code completion results?
  unsigned long completionBytes = 0;
  if (GlobalCodeCompletionAllocator *completionAllocator =
          astUnit->getCachedCompletionAllocator().get()) {
    completionBytes = completionAllocator->getTotalMemory();
  }
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_GlobalCompletionResults, completionBytes);

  // How much memory is being used by SourceManager's content cache?
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_SourceManagerContentCache,
      (unsigned long)astContext.getSourceManager().getContentCacheSize());

  // How much memory is being used by the MemoryBuffer's in SourceManager?
  const SourceManager::MemoryBufferSizes &srcBufs =
      astUnit->getSourceManager().getMemoryBufferSizes();

  createCXTUResourceUsageEntry(*entries,
                               CXTUResourceUsage_SourceManager_Membuffer_Malloc,
                               (unsigned long)srcBufs.malloc_bytes);
  createCXTUResourceUsageEntry(*entries,
                               CXTUResourceUsage_SourceManager_Membuffer_MMap,
                               (unsigned long)srcBufs.mmap_bytes);
  createCXTUResourceUsageEntry(
      *entries, CXTUResourceUsage_SourceManager_DataStructures,
      (unsigned long)astContext.getSourceManager().getDataStructureSizes());

  // How much memory is being used by the ExternalASTSource?
  if (ExternalASTSource *esrc = astContext.getExternalSource()) {
    const ExternalASTSource::MemoryBufferSizes &sizes =
        esrc->getMemoryBufferSizes();

    createCXTUResourceUsageEntry(
        *entries, CXTUResourceUsage_ExternalASTSource_Membuffer_Malloc,
        (unsigned long)sizes.malloc_bytes);
    createCXTUResourceUsageEntry(
        *entries, CXTUResourceUsage_ExternalASTSource_Membuffer_MMap,
        (unsigned long)sizes.mmap_bytes);
  }

  // How much memory is being used by the Preprocessor?
  Preprocessor &pp = astUnit->getPreprocessor();
  createCXTUResourceUsageEntry(*entries, CXTUResourceUsage_Preprocessor,
                               pp.getTotalMemory());

  if (PreprocessingRecord *pRec = pp.getPreprocessingRecord()) {
    createCXTUResourceUsageEntry(*entries,
                                 CXTUResourceUsage_PreprocessingRecord,
                                 pRec->getTotalMemory());
  }

  createCXTUResourceUsageEntry(*entries,
                               CXTUResourceUsage_Preprocessor_HeaderSearch,
                               pp.getHeaderSearchInfo().getTotalMemory());

  CXTUResourceUsage usage = {(void *)entries.get(), (unsigned)entries->size(),
                             !entries->empty() ? &(*entries)[0] : nullptr};
  (void)entries.release();
  return usage;
}

void clang_disposeCXTUResourceUsage(CXTUResourceUsage usage) {
  if (usage.data)
    delete (MemUsageEntries *)usage.data;
}

CXSourceRangeList *clang_getSkippedRanges(CXTranslationUnit TU, CXFile file) {
  CXSourceRangeList *skipped = new CXSourceRangeList;
  skipped->count = 0;
  skipped->ranges = nullptr;

  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return skipped;
  }

  if (!file)
    return skipped;

  ASTUnit *astUnit = cxtu::getASTUnit(TU);
  PreprocessingRecord *ppRec =
      astUnit->getPreprocessor().getPreprocessingRecord();
  if (!ppRec)
    return skipped;

  ASTContext &Ctx = astUnit->getASTContext();
  SourceManager &sm = Ctx.getSourceManager();
  FileEntryRef fileEntry = *cxfile::getFileEntryRef(file);
  FileID wantedFileID = sm.translateFile(fileEntry);
  bool isMainFile = wantedFileID == sm.getMainFileID();

  const std::vector<SourceRange> &SkippedRanges = ppRec->getSkippedRanges();
  std::vector<SourceRange> wantedRanges;
  for (std::vector<SourceRange>::const_iterator i = SkippedRanges.begin(),
                                                ei = SkippedRanges.end();
       i != ei; ++i) {
    if (sm.getFileID(i->getBegin()) == wantedFileID ||
        sm.getFileID(i->getEnd()) == wantedFileID)
      wantedRanges.push_back(*i);
    else if (isMainFile && (astUnit->isInPreambleFileID(i->getBegin()) ||
                            astUnit->isInPreambleFileID(i->getEnd())))
      wantedRanges.push_back(*i);
  }

  skipped->count = wantedRanges.size();
  skipped->ranges = new CXSourceRange[skipped->count];
  for (unsigned i = 0, ei = skipped->count; i != ei; ++i)
    skipped->ranges[i] = cxloc::translateSourceRange(Ctx, wantedRanges[i]);

  return skipped;
}

CXSourceRangeList *clang_getAllSkippedRanges(CXTranslationUnit TU) {
  CXSourceRangeList *skipped = new CXSourceRangeList;
  skipped->count = 0;
  skipped->ranges = nullptr;

  if (isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return skipped;
  }

  ASTUnit *astUnit = cxtu::getASTUnit(TU);
  PreprocessingRecord *ppRec =
      astUnit->getPreprocessor().getPreprocessingRecord();
  if (!ppRec)
    return skipped;

  ASTContext &Ctx = astUnit->getASTContext();

  const std::vector<SourceRange> &SkippedRanges = ppRec->getSkippedRanges();

  skipped->count = SkippedRanges.size();
  skipped->ranges = new CXSourceRange[skipped->count];
  for (unsigned i = 0, ei = skipped->count; i != ei; ++i)
    skipped->ranges[i] = cxloc::translateSourceRange(Ctx, SkippedRanges[i]);

  return skipped;
}

void clang_disposeSourceRangeList(CXSourceRangeList *ranges) {
  if (ranges) {
    delete[] ranges->ranges;
    delete ranges;
  }
}

void clang::PrintLibclangResourceUsage(CXTranslationUnit TU) {
  CXTUResourceUsage Usage = clang_getCXTUResourceUsage(TU);
  for (unsigned I = 0; I != Usage.numEntries; ++I)
    fprintf(stderr, "  %s: %lu\n",
            clang_getTUResourceUsageName(Usage.entries[I].kind),
            Usage.entries[I].amount);

  clang_disposeCXTUResourceUsage(Usage);
}

CXCursor clang_Cursor_getVarDeclInitializer(CXCursor cursor) {
  const Decl *const D = getCursorDecl(cursor);
  if (!D)
    return clang_getNullCursor();
  const auto *const VD = dyn_cast<VarDecl>(D);
  if (!VD)
    return clang_getNullCursor();
  const Expr *const Init = VD->getInit();
  if (!Init)
    return clang_getNullCursor();

  return cxcursor::MakeCXCursor(Init, VD, cxcursor::getCursorTU(cursor));
}

int clang_Cursor_hasVarDeclGlobalStorage(CXCursor cursor) {
  const Decl *const D = getCursorDecl(cursor);
  if (!D)
    return -1;
  const auto *const VD = dyn_cast<VarDecl>(D);
  if (!VD)
    return -1;

  return VD->hasGlobalStorage();
}

int clang_Cursor_hasVarDeclExternalStorage(CXCursor cursor) {
  const Decl *const D = getCursorDecl(cursor);
  if (!D)
    return -1;
  const auto *const VD = dyn_cast<VarDecl>(D);
  if (!VD)
    return -1;

  return VD->hasExternalStorage();
}

//===----------------------------------------------------------------------===//
// Misc. utility functions.
//===----------------------------------------------------------------------===//

/// Default to using our desired 8 MB stack size on "safety" threads.
static unsigned SafetyStackThreadSize = DesiredStackSize;

namespace clang {

bool RunSafely(llvm::CrashRecoveryContext &CRC, llvm::function_ref<void()> Fn,
               unsigned Size) {
  if (!Size)
    Size = GetSafetyThreadStackSize();
  if (Size && !getenv("LIBCLANG_NOTHREADS"))
    return CRC.RunSafelyOnThread(Fn, Size);
  return CRC.RunSafely(Fn);
}

unsigned GetSafetyThreadStackSize() { return SafetyStackThreadSize; }

void SetSafetyThreadStackSize(unsigned Value) { SafetyStackThreadSize = Value; }

} // namespace clang

void clang::setThreadBackgroundPriority() {
  if (getenv("LIBCLANG_BGPRIO_DISABLE"))
    return;

#if LLVM_ENABLE_THREADS
  // The function name setThreadBackgroundPriority is for historical reasons;
  // Low is more appropriate.
  llvm::set_thread_priority(llvm::ThreadPriority::Low);
#endif
}

void cxindex::printDiagsToStderr(ASTUnit *Unit) {
  if (!Unit)
    return;

  for (ASTUnit::stored_diag_iterator D = Unit->stored_diag_begin(),
                                     DEnd = Unit->stored_diag_end();
       D != DEnd; ++D) {
    CXStoredDiagnostic Diag(*D, Unit->getLangOpts());
    CXString Msg =
        clang_formatDiagnostic(&Diag, clang_defaultDiagnosticDisplayOptions());
    fprintf(stderr, "%s\n", clang_getCString(Msg));
    clang_disposeString(Msg);
  }
#ifdef _WIN32
  // On Windows, force a flush, since there may be multiple copies of
  // stderr and stdout in the file system, all with different buffers
  // but writing to the same device.
  fflush(stderr);
#endif
}

MacroInfo *cxindex::getMacroInfo(const IdentifierInfo &II,
                                 SourceLocation MacroDefLoc,
                                 CXTranslationUnit TU) {
  if (MacroDefLoc.isInvalid() || !TU)
    return nullptr;
  if (!II.hadMacroDefinition())
    return nullptr;

  ASTUnit *Unit = cxtu::getASTUnit(TU);
  Preprocessor &PP = Unit->getPreprocessor();
  MacroDirective *MD = PP.getLocalMacroDirectiveHistory(&II);
  if (MD) {
    for (MacroDirective::DefInfo Def = MD->getDefinition(); Def;
         Def = Def.getPreviousDefinition()) {
      if (MacroDefLoc == Def.getMacroInfo()->getDefinitionLoc())
        return Def.getMacroInfo();
    }
  }

  return nullptr;
}

const MacroInfo *cxindex::getMacroInfo(const MacroDefinitionRecord *MacroDef,
                                       CXTranslationUnit TU) {
  if (!MacroDef || !TU)
    return nullptr;
  const IdentifierInfo *II = MacroDef->getName();
  if (!II)
    return nullptr;

  return getMacroInfo(*II, MacroDef->getLocation(), TU);
}

MacroDefinitionRecord *
cxindex::checkForMacroInMacroDefinition(const MacroInfo *MI, const Token &Tok,
                                        CXTranslationUnit TU) {
  if (!MI || !TU)
    return nullptr;
  if (Tok.isNot(tok::raw_identifier))
    return nullptr;

  if (MI->getNumTokens() == 0)
    return nullptr;
  SourceRange DefRange(MI->getReplacementToken(0).getLocation(),
                       MI->getDefinitionEndLoc());
  ASTUnit *Unit = cxtu::getASTUnit(TU);

  // Check that the token is inside the definition and not its argument list.
  SourceManager &SM = Unit->getSourceManager();
  if (SM.isBeforeInTranslationUnit(Tok.getLocation(), DefRange.getBegin()))
    return nullptr;
  if (SM.isBeforeInTranslationUnit(DefRange.getEnd(), Tok.getLocation()))
    return nullptr;

  Preprocessor &PP = Unit->getPreprocessor();
  PreprocessingRecord *PPRec = PP.getPreprocessingRecord();
  if (!PPRec)
    return nullptr;

  IdentifierInfo &II = PP.getIdentifierTable().get(Tok.getRawIdentifier());
  if (!II.hadMacroDefinition())
    return nullptr;

  // Check that the identifier is not one of the macro arguments.
  if (llvm::is_contained(MI->params(), &II))
    return nullptr;

  MacroDirective *InnerMD = PP.getLocalMacroDirectiveHistory(&II);
  if (!InnerMD)
    return nullptr;

  return PPRec->findMacroDefinition(InnerMD->getMacroInfo());
}

MacroDefinitionRecord *
cxindex::checkForMacroInMacroDefinition(const MacroInfo *MI, SourceLocation Loc,
                                        CXTranslationUnit TU) {
  if (Loc.isInvalid() || !MI || !TU)
    return nullptr;

  if (MI->getNumTokens() == 0)
    return nullptr;
  ASTUnit *Unit = cxtu::getASTUnit(TU);
  Preprocessor &PP = Unit->getPreprocessor();
  if (!PP.getPreprocessingRecord())
    return nullptr;
  Loc = Unit->getSourceManager().getSpellingLoc(Loc);
  Token Tok;
  if (PP.getRawToken(Loc, Tok))
    return nullptr;

  return checkForMacroInMacroDefinition(MI, Tok, TU);
}

CXString clang_getClangVersion() {
  return cxstring::createDup(getClangFullVersion());
}

Logger &cxindex::Logger::operator<<(CXTranslationUnit TU) {
  if (TU) {
    if (ASTUnit *Unit = cxtu::getASTUnit(TU)) {
      LogOS << '<' << Unit->getMainFileName() << '>';
      if (Unit->isMainFileAST())
        LogOS << " (" << Unit->getASTFileName() << ')';
      return *this;
    }
  } else {
    LogOS << "<NULL TU>";
  }
  return *this;
}

Logger &cxindex::Logger::operator<<(FileEntryRef FE) {
  *this << FE.getName();
  return *this;
}

Logger &cxindex::Logger::operator<<(CXCursor cursor) {
  CXString cursorName = clang_getCursorDisplayName(cursor);
  *this << cursorName << "@" << clang_getCursorLocation(cursor);
  clang_disposeString(cursorName);
  return *this;
}

Logger &cxindex::Logger::operator<<(CXSourceLocation Loc) {
  CXFile File;
  unsigned Line, Column;
  clang_getFileLocation(Loc, &File, &Line, &Column, nullptr);
  CXString FileName = clang_getFileName(File);
  *this << llvm::format("(%s:%d:%d)", clang_getCString(FileName), Line, Column);
  clang_disposeString(FileName);
  return *this;
}

Logger &cxindex::Logger::operator<<(CXSourceRange range) {
  CXSourceLocation BLoc = clang_getRangeStart(range);
  CXSourceLocation ELoc = clang_getRangeEnd(range);

  CXFile BFile;
  unsigned BLine, BColumn;
  clang_getFileLocation(BLoc, &BFile, &BLine, &BColumn, nullptr);

  CXFile EFile;
  unsigned ELine, EColumn;
  clang_getFileLocation(ELoc, &EFile, &ELine, &EColumn, nullptr);

  CXString BFileName = clang_getFileName(BFile);
  if (BFile == EFile) {
    *this << llvm::format("[%s %d:%d-%d:%d]", clang_getCString(BFileName),
                          BLine, BColumn, ELine, EColumn);
  } else {
    CXString EFileName = clang_getFileName(EFile);
    *this << llvm::format("[%s:%d:%d - ", clang_getCString(BFileName), BLine,
                          BColumn)
          << llvm::format("%s:%d:%d]", clang_getCString(EFileName), ELine,
                          EColumn);
    clang_disposeString(EFileName);
  }
  clang_disposeString(BFileName);
  return *this;
}

Logger &cxindex::Logger::operator<<(CXString Str) {
  *this << clang_getCString(Str);
  return *this;
}

Logger &cxindex::Logger::operator<<(const llvm::format_object_base &Fmt) {
  LogOS << Fmt;
  return *this;
}

static llvm::ManagedStatic<std::mutex> LoggingMutex;

cxindex::Logger::~Logger() {
  std::lock_guard<std::mutex> L(*LoggingMutex);

  static llvm::TimeRecord sBeginTR = llvm::TimeRecord::getCurrentTime();

  raw_ostream &OS = llvm::errs();
  OS << "[libclang:" << Name << ':';

#ifdef USE_DARWIN_THREADS
  // TODO: Portability.
  mach_port_t tid = pthread_mach_thread_np(pthread_self());
  OS << tid << ':';
#endif

  llvm::TimeRecord TR = llvm::TimeRecord::getCurrentTime();
  OS << llvm::format("%7.4f] ", TR.getWallTime() - sBeginTR.getWallTime());
  OS << Msg << '\n';

  if (Trace) {
    llvm::sys::PrintStackTrace(OS);
    OS << "--------------------------------------------------\n";
  }
}

CXString clang_getBinaryOperatorKindSpelling(enum CXBinaryOperatorKind kind) {
  return cxstring::createRef(
      BinaryOperator::getOpcodeStr(static_cast<BinaryOperatorKind>(kind - 1)));
}

enum CXBinaryOperatorKind clang_getCursorBinaryOperatorKind(CXCursor cursor) {
  if (clang_isExpression(cursor.kind)) {
    const Expr *expr = getCursorExpr(cursor);

    if (const auto *op = dyn_cast<BinaryOperator>(expr))
      return static_cast<CXBinaryOperatorKind>(op->getOpcode() + 1);

    if (const auto *op = dyn_cast<CXXRewrittenBinaryOperator>(expr))
      return static_cast<CXBinaryOperatorKind>(op->getOpcode() + 1);
  }

  return CXBinaryOperator_Invalid;
}

CXString clang_getUnaryOperatorKindSpelling(enum CXUnaryOperatorKind kind) {
  return cxstring::createRef(
      UnaryOperator::getOpcodeStr(static_cast<UnaryOperatorKind>(kind - 1)));
}

enum CXUnaryOperatorKind clang_getCursorUnaryOperatorKind(CXCursor cursor) {
  if (clang_isExpression(cursor.kind)) {
    const Expr *expr = getCursorExpr(cursor);

    if (const auto *op = dyn_cast<UnaryOperator>(expr))
      return static_cast<CXUnaryOperatorKind>(op->getOpcode() + 1);
  }

  return CXUnaryOperator_Invalid;
}
