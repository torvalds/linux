//===- CXStoredDiagnostic.cpp - Diagnostics C Interface -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements part of the diagnostic functions of the Clang C interface.
//
//===----------------------------------------------------------------------===//

#include "CIndexDiagnostic.h"
#include "CIndexer.h"
#include "CXTranslationUnit.h"
#include "CXSourceLocation.h"
#include "CXString.h"

#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Frontend/ASTUnit.h"
#include "llvm/ADT/Twine.h"

using namespace clang;
using namespace clang::cxloc;

CXDiagnosticSeverity CXStoredDiagnostic::getSeverity() const {
  switch (Diag.getLevel()) {
    case DiagnosticsEngine::Ignored: return CXDiagnostic_Ignored;
    case DiagnosticsEngine::Note:    return CXDiagnostic_Note;
    case DiagnosticsEngine::Remark:
    // The 'Remark' level isn't represented in the stable API.
    case DiagnosticsEngine::Warning: return CXDiagnostic_Warning;
    case DiagnosticsEngine::Error:   return CXDiagnostic_Error;
    case DiagnosticsEngine::Fatal:   return CXDiagnostic_Fatal;
  }
  
  llvm_unreachable("Invalid diagnostic level");
}

CXSourceLocation CXStoredDiagnostic::getLocation() const {
  if (Diag.getLocation().isInvalid())
    return clang_getNullLocation();
  
  return translateSourceLocation(Diag.getLocation().getManager(),
                                 LangOpts, Diag.getLocation());
}

CXString CXStoredDiagnostic::getSpelling() const {
  return cxstring::createRef(Diag.getMessage());
}

CXString CXStoredDiagnostic::getDiagnosticOption(CXString *Disable) const {
  unsigned ID = Diag.getID();
  StringRef Option = DiagnosticIDs::getWarningOptionForDiag(ID);
  if (!Option.empty()) {
    if (Disable)
      *Disable = cxstring::createDup((Twine("-Wno-") + Option).str());
    return cxstring::createDup((Twine("-W") + Option).str());
  }
  
  if (ID == diag::fatal_too_many_errors) {
    if (Disable)
      *Disable = cxstring::createRef("-ferror-limit=0");
    return cxstring::createRef("-ferror-limit=");
  }

  return cxstring::createEmpty();
}

unsigned CXStoredDiagnostic::getCategory() const {
  return DiagnosticIDs::getCategoryNumberForDiag(Diag.getID());
}

CXString CXStoredDiagnostic::getCategoryText() const {
  unsigned catID = DiagnosticIDs::getCategoryNumberForDiag(Diag.getID());
  return cxstring::createRef(DiagnosticIDs::getCategoryNameFromID(catID));
}

unsigned CXStoredDiagnostic::getNumRanges() const {
  if (Diag.getLocation().isInvalid())
    return 0;
  
  return Diag.range_size();
}

CXSourceRange CXStoredDiagnostic::getRange(unsigned int Range) const {
  assert(Diag.getLocation().isValid());
  return translateSourceRange(Diag.getLocation().getManager(),
                              LangOpts,
                              Diag.range_begin()[Range]);
}

unsigned CXStoredDiagnostic::getNumFixIts() const {
  if (Diag.getLocation().isInvalid())
    return 0;    
  return Diag.fixit_size();
}

CXString CXStoredDiagnostic::getFixIt(unsigned FixIt,
                                      CXSourceRange *ReplacementRange) const {  
  const FixItHint &Hint = Diag.fixit_begin()[FixIt];
  if (ReplacementRange) {
    // Create a range that covers the entire replacement (or
    // removal) range, adjusting the end of the range to point to
    // the end of the token.
    *ReplacementRange = translateSourceRange(Diag.getLocation().getManager(),
                                             LangOpts, Hint.RemoveRange);
  }
  return cxstring::createDup(Hint.CodeToInsert);
}

