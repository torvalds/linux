//===--- FileIndexRecord.cpp - Index data per file --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FileIndexRecord.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::index;

ArrayRef<DeclOccurrence>
FileIndexRecord::getDeclOccurrencesSortedByOffset() const {
  if (!IsSorted) {
    llvm::stable_sort(Decls,
                      [](const DeclOccurrence &A, const DeclOccurrence &B) {
                        return A.Offset < B.Offset;
                      });
    IsSorted = true;
  }
  return Decls;
}

void FileIndexRecord::addDeclOccurence(SymbolRoleSet Roles, unsigned Offset,
                                       const Decl *D,
                                       ArrayRef<SymbolRelation> Relations) {
  assert(D->isCanonicalDecl() &&
         "Occurrences should be associated with their canonical decl");
  IsSorted = false;
  Decls.emplace_back(Roles, Offset, D, Relations);
}

void FileIndexRecord::addMacroOccurence(SymbolRoleSet Roles, unsigned Offset,
                                        const IdentifierInfo *Name,
                                        const MacroInfo *MI) {
  IsSorted = false;
  Decls.emplace_back(Roles, Offset, Name, MI);
}

void FileIndexRecord::removeHeaderGuardMacros() {
  llvm::erase_if(Decls, [](const DeclOccurrence &D) {
    if (const auto *MI = D.DeclOrMacro.dyn_cast<const MacroInfo *>())
      return MI->isUsedForHeaderGuard();
    return false;
  });
}

void FileIndexRecord::print(llvm::raw_ostream &OS, SourceManager &SM) const {
  OS << "DECLS BEGIN ---\n";
  for (auto &DclInfo : Decls) {
    if (const auto *D = DclInfo.DeclOrMacro.dyn_cast<const Decl *>()) {
      SourceLocation Loc = SM.getFileLoc(D->getLocation());
      PresumedLoc PLoc = SM.getPresumedLoc(Loc);
      OS << llvm::sys::path::filename(PLoc.getFilename()) << ':'
         << PLoc.getLine() << ':' << PLoc.getColumn();

      if (const auto *ND = dyn_cast<NamedDecl>(D)) {
        OS << ' ' << ND->getDeclName();
      }
    } else {
      const auto *MI = DclInfo.DeclOrMacro.get<const MacroInfo *>();
      SourceLocation Loc = SM.getFileLoc(MI->getDefinitionLoc());
      PresumedLoc PLoc = SM.getPresumedLoc(Loc);
      OS << llvm::sys::path::filename(PLoc.getFilename()) << ':'
         << PLoc.getLine() << ':' << PLoc.getColumn();
      OS << ' ' << DclInfo.MacroName->getName();
    }

    OS << '\n';
  }
  OS << "DECLS END ---\n";
}
