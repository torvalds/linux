//===- CIndexHigh.cpp - Higher level API functions ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CursorVisitor.h"
#include "CLog.h"
#include "CXCursor.h"
#include "CXFile.h"
#include "CXSourceLocation.h"
#include "CXTranslationUnit.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Frontend/ASTUnit.h"
#include "llvm/Support/Compiler.h"

using namespace clang;
using namespace cxcursor;
using namespace cxindex;

static void getTopOverriddenMethods(CXTranslationUnit TU,
                                    const Decl *D,
                                    SmallVectorImpl<const Decl *> &Methods) {
  if (!D)
    return;
  if (!isa<ObjCMethodDecl>(D) && !isa<CXXMethodDecl>(D))
    return;

  SmallVector<CXCursor, 8> Overridden;
  cxcursor::getOverriddenCursors(cxcursor::MakeCXCursor(D, TU), Overridden);
  
  if (Overridden.empty()) {
    Methods.push_back(D->getCanonicalDecl());
    return;
  }

  for (SmallVectorImpl<CXCursor>::iterator
         I = Overridden.begin(), E = Overridden.end(); I != E; ++I)
    getTopOverriddenMethods(TU, cxcursor::getCursorDecl(*I), Methods);
}

namespace {

struct FindFileIdRefVisitData {
  CXTranslationUnit TU;
  FileID FID;
  const Decl *Dcl;
  int SelectorIdIdx;
  CXCursorAndRangeVisitor visitor;

  typedef SmallVector<const Decl *, 8> TopMethodsTy;
  TopMethodsTy TopMethods;

  FindFileIdRefVisitData(CXTranslationUnit TU, FileID FID,
                         const Decl *D, int selectorIdIdx,
                         CXCursorAndRangeVisitor visitor)
    : TU(TU), FID(FID), SelectorIdIdx(selectorIdIdx), visitor(visitor) {
    Dcl = getCanonical(D);
    getTopOverriddenMethods(TU, Dcl, TopMethods);
  }

  ASTContext &getASTContext() const {
    return cxtu::getASTUnit(TU)->getASTContext();
  }

  /// We are looking to find all semantically relevant identifiers,
  /// so the definition of "canonical" here is different than in the AST, e.g.
  ///
  /// \code
  ///   class C {
  ///     C() {}
  ///   };
  /// \endcode
  ///
  /// we consider the canonical decl of the constructor decl to be the class
  /// itself, so both 'C' can be highlighted.
  const Decl *getCanonical(const Decl *D) const {
    if (!D)
      return nullptr;

    D = D->getCanonicalDecl();

    if (const ObjCImplDecl *ImplD = dyn_cast<ObjCImplDecl>(D)) {
      if (ImplD->getClassInterface())
        return getCanonical(ImplD->getClassInterface());

    } else if (const CXXConstructorDecl *CXXCtorD =
                   dyn_cast<CXXConstructorDecl>(D)) {
      return getCanonical(CXXCtorD->getParent());
    }
    
    return D;
  }

  bool isHit(const Decl *D) const {
    if (!D)
      return false;

    D = getCanonical(D);
    if (D == Dcl)
      return true;

    if (isa<ObjCMethodDecl>(D) || isa<CXXMethodDecl>(D))
      return isOverriddingMethod(D);

    return false;
  }

private:
  bool isOverriddingMethod(const Decl *D) const {
    if (llvm::is_contained(TopMethods, D))
      return true;

    TopMethodsTy methods;
    getTopOverriddenMethods(TU, D, methods);
    for (TopMethodsTy::iterator
           I = methods.begin(), E = methods.end(); I != E; ++I) {
      if (llvm::is_contained(TopMethods, *I))
        return true;
    }

    return false;
  }
};

} // end anonymous namespace.

/// For a macro \arg Loc, returns the file spelling location and sets
/// to \arg isMacroArg whether the spelling resides inside a macro definition or
/// a macro argument.
static SourceLocation getFileSpellingLoc(SourceManager &SM,
                                         SourceLocation Loc,
                                         bool &isMacroArg) {
  assert(Loc.isMacroID());
  SourceLocation SpellLoc = SM.getImmediateSpellingLoc(Loc);
  if (SpellLoc.isMacroID())
    return getFileSpellingLoc(SM, SpellLoc, isMacroArg);
  
  isMacroArg = SM.isMacroArgExpansion(Loc);
  return SpellLoc;
}

static enum CXChildVisitResult findFileIdRefVisit(CXCursor cursor,
                                                  CXCursor parent,
                                                  CXClientData client_data) {
  CXCursor declCursor = clang_getCursorReferenced(cursor);
  if (!clang_isDeclaration(declCursor.kind))
    return CXChildVisit_Recurse;

  const Decl *D = cxcursor::getCursorDecl(declCursor);
  if (!D)
    return CXChildVisit_Continue;

  FindFileIdRefVisitData *data = (FindFileIdRefVisitData *)client_data;
  if (data->isHit(D)) {
    cursor = cxcursor::getSelectorIdentifierCursor(data->SelectorIdIdx, cursor);

    // We are looking for identifiers to highlight so for objc methods (and
    // not a parameter) we can only highlight the selector identifiers.
    if ((cursor.kind == CXCursor_ObjCClassMethodDecl ||
         cursor.kind == CXCursor_ObjCInstanceMethodDecl) &&
         cxcursor::getSelectorIdentifierIndex(cursor) == -1)
      return CXChildVisit_Recurse;

    if (clang_isExpression(cursor.kind)) {
      if (cursor.kind == CXCursor_DeclRefExpr ||
          cursor.kind == CXCursor_MemberRefExpr) {
        // continue..

      } else if (cursor.kind == CXCursor_ObjCMessageExpr &&
                 cxcursor::getSelectorIdentifierIndex(cursor) != -1) {
        // continue..
                
      } else
        return CXChildVisit_Recurse;
    }

    SourceLocation
      Loc = cxloc::translateSourceLocation(clang_getCursorLocation(cursor));
    SourceLocation SelIdLoc = cxcursor::getSelectorIdentifierLoc(cursor);
    if (SelIdLoc.isValid())
      Loc = SelIdLoc;

    ASTContext &Ctx = data->getASTContext();
    SourceManager &SM = Ctx.getSourceManager();
    bool isInMacroDef = false;
    if (Loc.isMacroID()) {
      bool isMacroArg;
      Loc = getFileSpellingLoc(SM, Loc, isMacroArg);
      isInMacroDef = !isMacroArg;
    }

    // We are looking for identifiers in a specific file.
    std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
    if (LocInfo.first != data->FID)
      return CXChildVisit_Recurse;

    if (isInMacroDef) {
      // FIXME: For a macro definition make sure that all expansions
      // of it expand to the same reference before allowing to point to it.
      return CXChildVisit_Recurse;
    }

    if (data->visitor.visit(data->visitor.context, cursor,
                        cxloc::translateSourceRange(Ctx, Loc)) == CXVisit_Break)
      return CXChildVisit_Break;
  }
  return CXChildVisit_Recurse;
}

static bool findIdRefsInFile(CXTranslationUnit TU, CXCursor declCursor,
                             const FileEntry *File,
                             CXCursorAndRangeVisitor Visitor) {
  assert(clang_isDeclaration(declCursor.kind));
  SourceManager &SM = cxtu::getASTUnit(TU)->getSourceManager();

  FileID FID = SM.translateFile(File);
  const Decl *Dcl = cxcursor::getCursorDecl(declCursor);
  if (!Dcl)
    return false;

  FindFileIdRefVisitData data(TU, FID, Dcl,
                              cxcursor::getSelectorIdentifierIndex(declCursor),
                              Visitor);

  if (const DeclContext *DC = Dcl->getParentFunctionOrMethod()) {
    return clang_visitChildren(cxcursor::MakeCXCursor(cast<Decl>(DC), TU),
                               findFileIdRefVisit, &data);
  }

  SourceRange Range(SM.getLocForStartOfFile(FID), SM.getLocForEndOfFile(FID));
  CursorVisitor FindIdRefsVisitor(TU,
                                  findFileIdRefVisit, &data,
                                  /*VisitPreprocessorLast=*/true,
                                  /*VisitIncludedEntities=*/false,
                                  Range,
                                  /*VisitDeclsOnly=*/true);
  return FindIdRefsVisitor.visitFileRegion();
}

namespace {

struct FindFileMacroRefVisitData {
  ASTUnit &Unit;
  const FileEntry *File;
  const IdentifierInfo *Macro;
  CXCursorAndRangeVisitor visitor;

  FindFileMacroRefVisitData(ASTUnit &Unit, const FileEntry *File,
                            const IdentifierInfo *Macro,
                            CXCursorAndRangeVisitor visitor)
    : Unit(Unit), File(File), Macro(Macro), visitor(visitor) { }

  ASTContext &getASTContext() const {
    return Unit.getASTContext();
  }
};

} // anonymous namespace

static enum CXChildVisitResult findFileMacroRefVisit(CXCursor cursor,
                                                     CXCursor parent,
                                                     CXClientData client_data) {
  const IdentifierInfo *Macro = nullptr;
  if (cursor.kind == CXCursor_MacroDefinition)
    Macro = getCursorMacroDefinition(cursor)->getName();
  else if (cursor.kind == CXCursor_MacroExpansion)
    Macro = getCursorMacroExpansion(cursor).getName();
  if (!Macro)
    return CXChildVisit_Continue;

  FindFileMacroRefVisitData *data = (FindFileMacroRefVisitData *)client_data;
  if (data->Macro != Macro)
    return CXChildVisit_Continue;

  SourceLocation
    Loc = cxloc::translateSourceLocation(clang_getCursorLocation(cursor));

  ASTContext &Ctx = data->getASTContext();
  SourceManager &SM = Ctx.getSourceManager();
  bool isInMacroDef = false;
  if (Loc.isMacroID()) {
    bool isMacroArg;
    Loc = getFileSpellingLoc(SM, Loc, isMacroArg);
    isInMacroDef = !isMacroArg;
  }

  // We are looking for identifiers in a specific file.
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
  if (SM.getFileEntryForID(LocInfo.first) != data->File)
    return CXChildVisit_Continue;

  if (isInMacroDef) {
    // FIXME: For a macro definition make sure that all expansions
    // of it expand to the same reference before allowing to point to it.
    return CXChildVisit_Continue;
  }

  if (data->visitor.visit(data->visitor.context, cursor,
                        cxloc::translateSourceRange(Ctx, Loc)) == CXVisit_Break)
    return CXChildVisit_Break;
  return CXChildVisit_Continue;
}

static bool findMacroRefsInFile(CXTranslationUnit TU, CXCursor Cursor,
                                const FileEntry *File,
                                CXCursorAndRangeVisitor Visitor) {
  if (Cursor.kind != CXCursor_MacroDefinition &&
      Cursor.kind != CXCursor_MacroExpansion)
    return false;

  ASTUnit *Unit = cxtu::getASTUnit(TU);
  SourceManager &SM = Unit->getSourceManager();

  FileID FID = SM.translateFile(File);
  const IdentifierInfo *Macro = nullptr;
  if (Cursor.kind == CXCursor_MacroDefinition)
    Macro = getCursorMacroDefinition(Cursor)->getName();
  else
    Macro = getCursorMacroExpansion(Cursor).getName();
  if (!Macro)
    return false;

  FindFileMacroRefVisitData data(*Unit, File, Macro, Visitor);

  SourceRange Range(SM.getLocForStartOfFile(FID), SM.getLocForEndOfFile(FID));
  CursorVisitor FindMacroRefsVisitor(TU,
                                  findFileMacroRefVisit, &data,
                                  /*VisitPreprocessorLast=*/false,
                                  /*VisitIncludedEntities=*/false,
                                  Range);
  return FindMacroRefsVisitor.visitPreprocessedEntitiesInRegion();
}

namespace {

struct FindFileIncludesVisitor {
  ASTUnit &Unit;
  const FileEntry *File;
  CXCursorAndRangeVisitor visitor;

  FindFileIncludesVisitor(ASTUnit &Unit, const FileEntry *File,
                          CXCursorAndRangeVisitor visitor)
    : Unit(Unit), File(File), visitor(visitor) { }

  ASTContext &getASTContext() const {
    return Unit.getASTContext();
  }

  enum CXChildVisitResult visit(CXCursor cursor, CXCursor parent) {
    if (cursor.kind != CXCursor_InclusionDirective)
      return CXChildVisit_Continue;

    SourceLocation
      Loc = cxloc::translateSourceLocation(clang_getCursorLocation(cursor));

    ASTContext &Ctx = getASTContext();
    SourceManager &SM = Ctx.getSourceManager();

    // We are looking for includes in a specific file.
    std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
    if (SM.getFileEntryForID(LocInfo.first) != File)
      return CXChildVisit_Continue;

    if (visitor.visit(visitor.context, cursor,
                      cxloc::translateSourceRange(Ctx, Loc)) == CXVisit_Break)
      return CXChildVisit_Break;
    return CXChildVisit_Continue;
  }

  static enum CXChildVisitResult visit(CXCursor cursor, CXCursor parent,
                                       CXClientData client_data) {
    return static_cast<FindFileIncludesVisitor*>(client_data)->
                                                          visit(cursor, parent);
  }
};

} // anonymous namespace

static bool findIncludesInFile(CXTranslationUnit TU, const FileEntry *File,
                               CXCursorAndRangeVisitor Visitor) {
  assert(TU && File && Visitor.visit);

  ASTUnit *Unit = cxtu::getASTUnit(TU);
  SourceManager &SM = Unit->getSourceManager();

  FileID FID = SM.translateFile(File);

  FindFileIncludesVisitor IncludesVisitor(*Unit, File, Visitor);

  SourceRange Range(SM.getLocForStartOfFile(FID), SM.getLocForEndOfFile(FID));
  CursorVisitor InclusionCursorsVisitor(TU,
                                        FindFileIncludesVisitor::visit,
                                        &IncludesVisitor,
                                        /*VisitPreprocessorLast=*/false,
                                        /*VisitIncludedEntities=*/false,
                                        Range);
  return InclusionCursorsVisitor.visitPreprocessedEntitiesInRegion();
}


//===----------------------------------------------------------------------===//
// libclang public APIs.
//===----------------------------------------------------------------------===//

extern "C" {

CXResult clang_findReferencesInFile(CXCursor cursor, CXFile file,
                                    CXCursorAndRangeVisitor visitor) {
  LogRef Log = Logger::make(__func__);

  if (clang_Cursor_isNull(cursor)) {
    if (Log)
      *Log << "Null cursor";
    return CXResult_Invalid;
  }
  if (cursor.kind == CXCursor_NoDeclFound) {
    if (Log)
      *Log << "Got CXCursor_NoDeclFound";
    return CXResult_Invalid;
  }
  if (!file) {
    if (Log)
      *Log << "Null file";
    return CXResult_Invalid;
  }
  if (!visitor.visit) {
    if (Log)
      *Log << "Null visitor";
    return CXResult_Invalid;
  }

  if (Log)
    *Log << cursor << " @" << *cxfile::getFileEntryRef(file);

  ASTUnit *CXXUnit = cxcursor::getCursorASTUnit(cursor);
  if (!CXXUnit)
    return CXResult_Invalid;

  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  if (cursor.kind == CXCursor_MacroDefinition ||
      cursor.kind == CXCursor_MacroExpansion) {
    if (findMacroRefsInFile(cxcursor::getCursorTU(cursor),
                            cursor,
                            *cxfile::getFileEntryRef(file),
                            visitor))
      return CXResult_VisitBreak;
    return CXResult_Success;
  }

  // We are interested in semantics of identifiers so for C++ constructor exprs
  // prefer type references, e.g.:
  //
  //  return MyStruct();
  //
  // for 'MyStruct' we'll have a cursor pointing at the constructor decl but
  // we are actually interested in the type declaration.
  cursor = cxcursor::getTypeRefCursor(cursor);

  CXCursor refCursor = clang_getCursorReferenced(cursor);

  if (!clang_isDeclaration(refCursor.kind)) {
    if (Log)
      *Log << "cursor is not referencing a declaration";
    return CXResult_Invalid;
  }

  if (findIdRefsInFile(cxcursor::getCursorTU(cursor),
                       refCursor,
                       *cxfile::getFileEntryRef(file),
                       visitor))
    return CXResult_VisitBreak;
  return CXResult_Success;
}

CXResult clang_findIncludesInFile(CXTranslationUnit TU, CXFile file,
                             CXCursorAndRangeVisitor visitor) {
  if (cxtu::isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return CXResult_Invalid;
  }

  LogRef Log = Logger::make(__func__);
  if (!file) {
    if (Log)
      *Log << "Null file";
    return CXResult_Invalid;
  }
  if (!visitor.visit) {
    if (Log)
      *Log << "Null visitor";
    return CXResult_Invalid;
  }

  if (Log)
    *Log << TU << " @" << *cxfile::getFileEntryRef(file);

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  if (!CXXUnit)
    return CXResult_Invalid;

  ASTUnit::ConcurrencyCheck Check(*CXXUnit);

  if (findIncludesInFile(TU, *cxfile::getFileEntryRef(file), visitor))
    return CXResult_VisitBreak;
  return CXResult_Success;
}

static enum CXVisitorResult _visitCursorAndRange(void *context,
                                                 CXCursor cursor,
                                                 CXSourceRange range) {
  CXCursorAndRangeVisitorBlock block = (CXCursorAndRangeVisitorBlock)context;
  return INVOKE_BLOCK2(block, cursor, range);
}

CXResult clang_findReferencesInFileWithBlock(CXCursor cursor,
                                             CXFile file,
                                           CXCursorAndRangeVisitorBlock block) {
  CXCursorAndRangeVisitor visitor = { block,
                                      block ? _visitCursorAndRange : nullptr };
  return clang_findReferencesInFile(cursor, file, visitor);
}

CXResult clang_findIncludesInFileWithBlock(CXTranslationUnit TU,
                                           CXFile file,
                                           CXCursorAndRangeVisitorBlock block) {
  CXCursorAndRangeVisitor visitor = { block,
                                      block ? _visitCursorAndRange : nullptr };
  return clang_findIncludesInFile(TU, file, visitor);
}

} // end: extern "C"
