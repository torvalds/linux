//===- ASTImporterLookupTable.cpp - ASTImporter specific lookup -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporterLookupTable class which implements a
//  lookup procedure for the import mechanism.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTImporterLookupTable.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/FormatVariadic.h"

namespace clang {

namespace {

struct Builder : RecursiveASTVisitor<Builder> {
  ASTImporterLookupTable &LT;
  Builder(ASTImporterLookupTable &LT) : LT(LT) {}

  bool VisitTypedefNameDecl(TypedefNameDecl *D) {
    QualType Ty = D->getUnderlyingType();
    Ty = Ty.getCanonicalType();
    if (const auto *RTy = dyn_cast<RecordType>(Ty)) {
      LT.add(RTy->getAsRecordDecl());
      // iterate over the field decls, adding them
      for (auto *it : RTy->getAsRecordDecl()->fields()) {
        LT.add(it);
      }
    }
    return true;
  }

  bool VisitNamedDecl(NamedDecl *D) {
    LT.add(D);
    return true;
  }
  // In most cases the FriendDecl contains the declaration of the befriended
  // class as a child node, so it is discovered during the recursive
  // visitation. However, there are cases when the befriended class is not a
  // child, thus it must be fetched explicitly from the FriendDecl, and only
  // then can we add it to the lookup table.
  bool VisitFriendDecl(FriendDecl *D) {
    if (D->getFriendType()) {
      QualType Ty = D->getFriendType()->getType();
      if (isa<ElaboratedType>(Ty))
        Ty = cast<ElaboratedType>(Ty)->getNamedType();
      // A FriendDecl with a dependent type (e.g. ClassTemplateSpecialization)
      // always has that decl as child node.
      // However, there are non-dependent cases which does not have the
      // type as a child node. We have to dig up that type now.
      if (!Ty->isDependentType()) {
        if (const auto *RTy = dyn_cast<RecordType>(Ty))
          LT.add(RTy->getAsCXXRecordDecl());
        else if (const auto *SpecTy = dyn_cast<TemplateSpecializationType>(Ty))
          LT.add(SpecTy->getAsCXXRecordDecl());
        else if (const auto *SubstTy =
                     dyn_cast<SubstTemplateTypeParmType>(Ty)) {
          if (SubstTy->getAsCXXRecordDecl())
            LT.add(SubstTy->getAsCXXRecordDecl());
        } else if (isa<TypedefType>(Ty)) {
          // We do not put friend typedefs to the lookup table because
          // ASTImporter does not organize typedefs into redecl chains.
        } else if (isa<UsingType>(Ty)) {
          // Similar to TypedefType, not putting into lookup table.
        } else {
          llvm_unreachable("Unhandled type of friend class");
        }
      }
    }
    return true;
  }

  // Override default settings of base.
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
};

} // anonymous namespace

ASTImporterLookupTable::ASTImporterLookupTable(TranslationUnitDecl &TU) {
  Builder B(*this);
  B.TraverseDecl(&TU);
  // The VaList declaration may be created on demand only or not traversed.
  // To ensure it is present and found during import, add it to the table now.
  if (auto *D =
          dyn_cast_or_null<NamedDecl>(TU.getASTContext().getVaListTagDecl())) {
    // On some platforms (AArch64) the VaList declaration can be inside a 'std'
    // namespace. This is handled specially and not visible by AST traversal.
    // ASTImporter must be able to find this namespace to import the VaList
    // declaration (and the namespace) correctly.
    if (auto *Ns = dyn_cast<NamespaceDecl>(D->getDeclContext()))
      add(&TU, Ns);
    add(D->getDeclContext(), D);
  }
}

void ASTImporterLookupTable::add(DeclContext *DC, NamedDecl *ND) {
  DeclList &Decls = LookupTable[DC][ND->getDeclName()];
  // Inserts if and only if there is no element in the container equal to it.
  Decls.insert(ND);
}

void ASTImporterLookupTable::remove(DeclContext *DC, NamedDecl *ND) {
  const DeclarationName Name = ND->getDeclName();
  DeclList &Decls = LookupTable[DC][Name];
  bool EraseResult = Decls.remove(ND);
  (void)EraseResult;
#ifndef NDEBUG
  if (!EraseResult) {
    std::string Message =
        llvm::formatv("Trying to remove not contained Decl '{0}' of type {1}",
                      Name.getAsString(), DC->getDeclKindName())
            .str();
    llvm_unreachable(Message.c_str());
  }
#endif
}

void ASTImporterLookupTable::add(NamedDecl *ND) {
  assert(ND);
  DeclContext *DC = ND->getDeclContext()->getPrimaryContext();
  add(DC, ND);
  DeclContext *ReDC = DC->getRedeclContext()->getPrimaryContext();
  if (DC != ReDC)
    add(ReDC, ND);
}

void ASTImporterLookupTable::remove(NamedDecl *ND) {
  assert(ND);
  DeclContext *DC = ND->getDeclContext()->getPrimaryContext();
  remove(DC, ND);
  DeclContext *ReDC = DC->getRedeclContext()->getPrimaryContext();
  if (DC != ReDC)
    remove(ReDC, ND);
}

void ASTImporterLookupTable::update(NamedDecl *ND, DeclContext *OldDC) {
  assert(OldDC != ND->getDeclContext() &&
         "DeclContext should be changed before update");
  if (contains(ND->getDeclContext(), ND)) {
    assert(!contains(OldDC, ND) &&
           "Decl should not be found in the old context if already in the new");
    return;
  }

  remove(OldDC, ND);
  add(ND);
}

void ASTImporterLookupTable::updateForced(NamedDecl *ND, DeclContext *OldDC) {
  LookupTable[OldDC][ND->getDeclName()].remove(ND);
  add(ND);
}

ASTImporterLookupTable::LookupResult
ASTImporterLookupTable::lookup(DeclContext *DC, DeclarationName Name) const {
  auto DCI = LookupTable.find(DC->getPrimaryContext());
  if (DCI == LookupTable.end())
    return {};

  const auto &FoundNameMap = DCI->second;
  auto NamesI = FoundNameMap.find(Name);
  if (NamesI == FoundNameMap.end())
    return {};

  return NamesI->second;
}

bool ASTImporterLookupTable::contains(DeclContext *DC, NamedDecl *ND) const {
  return lookup(DC, ND->getDeclName()).contains(ND);
}

void ASTImporterLookupTable::dump(DeclContext *DC) const {
  auto DCI = LookupTable.find(DC->getPrimaryContext());
  if (DCI == LookupTable.end())
    llvm::errs() << "empty\n";
  const auto &FoundNameMap = DCI->second;
  for (const auto &Entry : FoundNameMap) {
    DeclarationName Name = Entry.first;
    llvm::errs() << "==== Name: ";
    Name.dump();
    const DeclList& List = Entry.second;
    for (NamedDecl *ND : List) {
      ND->dump();
    }
  }
}

void ASTImporterLookupTable::dump() const {
  for (const auto &Entry : LookupTable) {
    DeclContext *DC = Entry.first;
    StringRef Primary = DC->getPrimaryContext() ? " primary" : "";
    llvm::errs() << "== DC:" << cast<Decl>(DC) << Primary << "\n";
    dump(DC);
  }
}

} // namespace clang
