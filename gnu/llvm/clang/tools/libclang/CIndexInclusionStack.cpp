//===- CIndexInclusionStack.cpp - Clang-C Source Indexing Library ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a callback mechanism for clients to get the inclusion
// stack from a translation unit.
//
//===----------------------------------------------------------------------===//

#include "CIndexer.h"
#include "CXFile.h"
#include "CXSourceLocation.h"
#include "CXTranslationUnit.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/Frontend/ASTUnit.h"
using namespace clang;

namespace {
void getInclusions(bool IsLocal, unsigned n, CXTranslationUnit TU,
                   CXInclusionVisitor CB, CXClientData clientData) {
  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  SourceManager &SM = CXXUnit->getSourceManager();
  ASTContext &Ctx = CXXUnit->getASTContext();
  SmallVector<CXSourceLocation, 10> InclusionStack;
  const bool HasPreamble = SM.getPreambleFileID().isValid();

  for (unsigned i = 0 ; i < n ; ++i) {
    bool Invalid = false;
    const SrcMgr::SLocEntry &SL =
        IsLocal ? SM.getLocalSLocEntry(i) : SM.getLoadedSLocEntry(i, &Invalid);
    if (!SL.isFile() || Invalid)
      continue;

    const SrcMgr::FileInfo &FI = SL.getFile();
    if (!FI.getContentCache().OrigEntry)
      continue;

    // If this is the main file, and there is a preamble, skip this SLoc. The
    // inclusions of the preamble already showed it.
    SourceLocation L = FI.getIncludeLoc();
    if (HasPreamble && CXXUnit->isInMainFileID(L))
      continue;

    // Build the inclusion stack.
    InclusionStack.clear();
    while (L.isValid()) {
      PresumedLoc PLoc = SM.getPresumedLoc(L);
      InclusionStack.push_back(cxloc::translateSourceLocation(Ctx, L));
      L = PLoc.isValid()? PLoc.getIncludeLoc() : SourceLocation();
    }

    // If there is a preamble, the last entry is the "inclusion" of that
    // preamble into the main file, which has the bogus entry of main.c:1:1
    if (HasPreamble && !InclusionStack.empty())
      InclusionStack.pop_back();

    // Callback to the client.
    CB(cxfile::makeCXFile(*FI.getContentCache().OrigEntry),
       InclusionStack.data(), InclusionStack.size(), clientData);
  }
}
} // namespace

void clang_getInclusions(CXTranslationUnit TU, CXInclusionVisitor CB,
                         CXClientData clientData) {
  if (cxtu::isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return;
  }

  SourceManager &SM = cxtu::getASTUnit(TU)->getSourceManager();
  const unsigned n =  SM.local_sloc_entry_size();

  // In the case where all the SLocEntries are in an external source, traverse
  // those SLocEntries as well.  This is the case where we are looking
  // at the inclusion stack of an AST/PCH file. Also, if we are not looking at
  // a AST/PCH file, but this file has a pre-compiled preamble, we also need
  // to look in that file.
  if (n == 1 || SM.getPreambleFileID().isValid()) {
    getInclusions(/*IsLocal=*/false, SM.loaded_sloc_entry_size(), TU, CB,
                  clientData);
  }

  // Not a PCH/AST file. Note, if there is a preamble, it could still be that
  // there are #includes in this file (e.g. for any include after the first
  // declaration).
  if (n != 1)
    getInclusions(/*IsLocal=*/true, n, TU, CB, clientData);
}
