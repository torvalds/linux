//===--- ASTConcept.cpp - Concepts Related AST Data Structures --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file defines AST data structures related to concepts.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConcept.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;

static void
CreateUnsatisfiedConstraintRecord(const ASTContext &C,
                                  const UnsatisfiedConstraintRecord &Detail,
                                  UnsatisfiedConstraintRecord *TrailingObject) {
  if (Detail.is<Expr *>())
    new (TrailingObject) UnsatisfiedConstraintRecord(Detail.get<Expr *>());
  else {
    auto &SubstitutionDiagnostic =
        *Detail.get<std::pair<SourceLocation, StringRef> *>();
    StringRef Message = C.backupStr(SubstitutionDiagnostic.second);
    auto *NewSubstDiag = new (C) std::pair<SourceLocation, StringRef>(
        SubstitutionDiagnostic.first, Message);
    new (TrailingObject) UnsatisfiedConstraintRecord(NewSubstDiag);
  }
}

ASTConstraintSatisfaction::ASTConstraintSatisfaction(
    const ASTContext &C, const ConstraintSatisfaction &Satisfaction)
    : NumRecords{Satisfaction.Details.size()},
      IsSatisfied{Satisfaction.IsSatisfied}, ContainsErrors{
                                                 Satisfaction.ContainsErrors} {
  for (unsigned I = 0; I < NumRecords; ++I)
    CreateUnsatisfiedConstraintRecord(
        C, Satisfaction.Details[I],
        getTrailingObjects<UnsatisfiedConstraintRecord>() + I);
}

ASTConstraintSatisfaction::ASTConstraintSatisfaction(
    const ASTContext &C, const ASTConstraintSatisfaction &Satisfaction)
    : NumRecords{Satisfaction.NumRecords},
      IsSatisfied{Satisfaction.IsSatisfied},
      ContainsErrors{Satisfaction.ContainsErrors} {
  for (unsigned I = 0; I < NumRecords; ++I)
    CreateUnsatisfiedConstraintRecord(
        C, *(Satisfaction.begin() + I),
        getTrailingObjects<UnsatisfiedConstraintRecord>() + I);
}

ASTConstraintSatisfaction *
ASTConstraintSatisfaction::Create(const ASTContext &C,
                                  const ConstraintSatisfaction &Satisfaction) {
  std::size_t size =
      totalSizeToAlloc<UnsatisfiedConstraintRecord>(
          Satisfaction.Details.size());
  void *Mem = C.Allocate(size, alignof(ASTConstraintSatisfaction));
  return new (Mem) ASTConstraintSatisfaction(C, Satisfaction);
}

ASTConstraintSatisfaction *ASTConstraintSatisfaction::Rebuild(
    const ASTContext &C, const ASTConstraintSatisfaction &Satisfaction) {
  std::size_t size =
      totalSizeToAlloc<UnsatisfiedConstraintRecord>(Satisfaction.NumRecords);
  void *Mem = C.Allocate(size, alignof(ASTConstraintSatisfaction));
  return new (Mem) ASTConstraintSatisfaction(C, Satisfaction);
}

void ConstraintSatisfaction::Profile(
    llvm::FoldingSetNodeID &ID, const ASTContext &C,
    const NamedDecl *ConstraintOwner, ArrayRef<TemplateArgument> TemplateArgs) {
  ID.AddPointer(ConstraintOwner);
  ID.AddInteger(TemplateArgs.size());
  for (auto &Arg : TemplateArgs)
    Arg.Profile(ID, C);
}

ConceptReference *
ConceptReference::Create(const ASTContext &C, NestedNameSpecifierLoc NNS,
                         SourceLocation TemplateKWLoc,
                         DeclarationNameInfo ConceptNameInfo,
                         NamedDecl *FoundDecl, ConceptDecl *NamedConcept,
                         const ASTTemplateArgumentListInfo *ArgsAsWritten) {
  return new (C) ConceptReference(NNS, TemplateKWLoc, ConceptNameInfo,
                                  FoundDecl, NamedConcept, ArgsAsWritten);
}

void ConceptReference::print(llvm::raw_ostream &OS,
                             const PrintingPolicy &Policy) const {
  if (NestedNameSpec)
    NestedNameSpec.getNestedNameSpecifier()->print(OS, Policy);
  ConceptName.printName(OS, Policy);
  if (hasExplicitTemplateArgs()) {
    OS << "<";
    llvm::ListSeparator Sep(", ");
    // FIXME: Find corresponding parameter for argument
    for (auto &ArgLoc : ArgsAsWritten->arguments()) {
      OS << Sep;
      ArgLoc.getArgument().print(Policy, OS, /*IncludeType*/ false);
    }
    OS << ">";
  }
}
