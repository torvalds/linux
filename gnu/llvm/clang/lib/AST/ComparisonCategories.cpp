//===- ComparisonCategories.cpp - Three Way Comparison Data -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Comparison Category enum and data types, which
//  store the types and expressions needed to support operator<=>
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

using namespace clang;

std::optional<ComparisonCategoryType>
clang::getComparisonCategoryForBuiltinCmp(QualType T) {
  using CCT = ComparisonCategoryType;

  if (T->isIntegralOrEnumerationType())
    return CCT::StrongOrdering;

  if (T->isRealFloatingType())
    return CCT::PartialOrdering;

  // C++2a [expr.spaceship]p8: If the composite pointer type is an object
  // pointer type, p <=> q is of type std::strong_ordering.
  // Note: this assumes neither operand is a null pointer constant.
  if (T->isObjectPointerType())
    return CCT::StrongOrdering;

  // TODO: Extend support for operator<=> to ObjC types.
  return std::nullopt;
}

bool ComparisonCategoryInfo::ValueInfo::hasValidIntValue() const {
  assert(VD && "must have var decl");
  if (!VD->isUsableInConstantExpressions(VD->getASTContext()))
    return false;

  // Before we attempt to get the value of the first field, ensure that we
  // actually have one (and only one) field.
  const auto *Record = VD->getType()->getAsCXXRecordDecl();
  if (std::distance(Record->field_begin(), Record->field_end()) != 1 ||
      !Record->field_begin()->getType()->isIntegralOrEnumerationType())
    return false;

  return true;
}

/// Attempt to determine the integer value used to represent the comparison
/// category result by evaluating the initializer for the specified VarDecl as
/// a constant expression and retrieving the value of the class's first
/// (and only) field.
///
/// Note: The STL types are expected to have the form:
///    struct X { T value; };
/// where T is an integral or enumeration type.
llvm::APSInt ComparisonCategoryInfo::ValueInfo::getIntValue() const {
  assert(hasValidIntValue() && "must have a valid value");
  return VD->evaluateValue()->getStructField(0).getInt();
}

ComparisonCategoryInfo::ValueInfo *ComparisonCategoryInfo::lookupValueInfo(
    ComparisonCategoryResult ValueKind) const {
  // Check if we already have a cache entry for this value.
  auto It = llvm::find_if(
      Objects, [&](ValueInfo const &Info) { return Info.Kind == ValueKind; });
  if (It != Objects.end())
    return &(*It);

  // We don't have a cached result. Lookup the variable declaration and create
  // a new entry representing it.
  DeclContextLookupResult Lookup = Record->getCanonicalDecl()->lookup(
      &Ctx.Idents.get(ComparisonCategories::getResultString(ValueKind)));
  if (Lookup.empty() || !isa<VarDecl>(Lookup.front()))
    return nullptr;
  Objects.emplace_back(ValueKind, cast<VarDecl>(Lookup.front()));
  return &Objects.back();
}

static const NamespaceDecl *lookupStdNamespace(const ASTContext &Ctx,
                                               NamespaceDecl *&StdNS) {
  if (!StdNS) {
    DeclContextLookupResult Lookup =
        Ctx.getTranslationUnitDecl()->lookup(&Ctx.Idents.get("std"));
    if (!Lookup.empty())
      StdNS = dyn_cast<NamespaceDecl>(Lookup.front());
  }
  return StdNS;
}

static const CXXRecordDecl *lookupCXXRecordDecl(const ASTContext &Ctx,
                                                const NamespaceDecl *StdNS,
                                                ComparisonCategoryType Kind) {
  StringRef Name = ComparisonCategories::getCategoryString(Kind);
  DeclContextLookupResult Lookup = StdNS->lookup(&Ctx.Idents.get(Name));
  if (!Lookup.empty())
    if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Lookup.front()))
      return RD;
  return nullptr;
}

const ComparisonCategoryInfo *
ComparisonCategories::lookupInfo(ComparisonCategoryType Kind) const {
  auto It = Data.find(static_cast<char>(Kind));
  if (It != Data.end())
    return &It->second;

  if (const NamespaceDecl *NS = lookupStdNamespace(Ctx, StdNS))
    if (const CXXRecordDecl *RD = lookupCXXRecordDecl(Ctx, NS, Kind))
      return &Data.try_emplace((char)Kind, Ctx, RD, Kind).first->second;

  return nullptr;
}

const ComparisonCategoryInfo *
ComparisonCategories::lookupInfoForType(QualType Ty) const {
  assert(!Ty.isNull() && "type must be non-null");
  using CCT = ComparisonCategoryType;
  const auto *RD = Ty->getAsCXXRecordDecl();
  if (!RD)
    return nullptr;

  // Check to see if we have information for the specified type cached.
  const auto *CanonRD = RD->getCanonicalDecl();
  for (const auto &KV : Data) {
    const ComparisonCategoryInfo &Info = KV.second;
    if (CanonRD == Info.Record->getCanonicalDecl())
      return &Info;
  }

  if (!RD->getEnclosingNamespaceContext()->isStdNamespace())
    return nullptr;

  // If not, check to see if the decl names a type in namespace std with a name
  // matching one of the comparison category types.
  for (unsigned I = static_cast<unsigned>(CCT::First),
                End = static_cast<unsigned>(CCT::Last);
       I <= End; ++I) {
    CCT Kind = static_cast<CCT>(I);

    // We've found the comparison category type. Build a new cache entry for
    // it.
    if (getCategoryString(Kind) == RD->getName())
      return &Data.try_emplace((char)Kind, Ctx, RD, Kind).first->second;
  }

  // We've found nothing. This isn't a comparison category type.
  return nullptr;
}

const ComparisonCategoryInfo &ComparisonCategories::getInfoForType(QualType Ty) const {
  const ComparisonCategoryInfo *Info = lookupInfoForType(Ty);
  assert(Info && "info for comparison category not found");
  return *Info;
}

QualType ComparisonCategoryInfo::getType() const {
  assert(Record);
  return QualType(Record->getTypeForDecl(), 0);
}

StringRef ComparisonCategories::getCategoryString(ComparisonCategoryType Kind) {
  using CCKT = ComparisonCategoryType;
  switch (Kind) {
  case CCKT::PartialOrdering:
    return "partial_ordering";
  case CCKT::WeakOrdering:
    return "weak_ordering";
  case CCKT::StrongOrdering:
    return "strong_ordering";
  }
  llvm_unreachable("unhandled cases in switch");
}

StringRef ComparisonCategories::getResultString(ComparisonCategoryResult Kind) {
  using CCVT = ComparisonCategoryResult;
  switch (Kind) {
  case CCVT::Equal:
    return "equal";
  case CCVT::Equivalent:
    return "equivalent";
  case CCVT::Less:
    return "less";
  case CCVT::Greater:
    return "greater";
  case CCVT::Unordered:
    return "unordered";
  }
  llvm_unreachable("unhandled case in switch");
}

std::vector<ComparisonCategoryResult>
ComparisonCategories::getPossibleResultsForType(ComparisonCategoryType Type) {
  using CCT = ComparisonCategoryType;
  using CCR = ComparisonCategoryResult;
  std::vector<CCR> Values;
  Values.reserve(4);
  bool IsStrong = Type == CCT::StrongOrdering;
  Values.push_back(IsStrong ? CCR::Equal : CCR::Equivalent);
  Values.push_back(CCR::Less);
  Values.push_back(CCR::Greater);
  if (Type == CCT::PartialOrdering)
    Values.push_back(CCR::Unordered);
  return Values;
}
