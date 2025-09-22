//===- CallDescription.cpp - function/method call matching     --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file defines a generic mechanism for matching for function and
/// method calls of C, C++, and Objective-C languages. Instances of these
/// classes are frequently used together with the CallEvent classes.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/AST/Decl.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/ArrayRef.h"
#include <iterator>
#include <optional>

using namespace llvm;
using namespace clang;

using MaybeCount = std::optional<unsigned>;

// A constructor helper.
static MaybeCount readRequiredParams(MaybeCount RequiredArgs,
                                     MaybeCount RequiredParams) {
  if (RequiredParams)
    return RequiredParams;
  if (RequiredArgs)
    return RequiredArgs;
  return std::nullopt;
}

ento::CallDescription::CallDescription(Mode MatchAs,
                                       ArrayRef<StringRef> QualifiedName,
                                       MaybeCount RequiredArgs /*= None*/,
                                       MaybeCount RequiredParams /*= None*/)
    : RequiredArgs(RequiredArgs),
      RequiredParams(readRequiredParams(RequiredArgs, RequiredParams)),
      MatchAs(MatchAs) {
  assert(!QualifiedName.empty());
  this->QualifiedName.reserve(QualifiedName.size());
  llvm::transform(QualifiedName, std::back_inserter(this->QualifiedName),
                  [](StringRef From) { return From.str(); });
}

bool ento::CallDescription::matches(const CallEvent &Call) const {
  // FIXME: Add ObjC Message support.
  if (Call.getKind() == CE_ObjCMessage)
    return false;

  const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return false;

  return matchesImpl(FD, Call.getNumArgs(), Call.parameters().size());
}

bool ento::CallDescription::matchesAsWritten(const CallExpr &CE) const {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(CE.getCalleeDecl());
  if (!FD)
    return false;

  return matchesImpl(FD, CE.getNumArgs(), FD->param_size());
}

bool ento::CallDescription::matchNameOnly(const NamedDecl *ND) const {
  DeclarationName Name = ND->getDeclName();
  if (const auto *NameII = Name.getAsIdentifierInfo()) {
    if (!II)
      II = &ND->getASTContext().Idents.get(getFunctionName());

    return NameII == *II; // Fast case.
  }

  // Fallback to the slow stringification and comparison for:
  // C++ overloaded operators, constructors, destructors, etc.
  // FIXME This comparison is way SLOWER than comparing pointers.
  // At some point in the future, we should compare FunctionDecl pointers.
  return Name.getAsString() == getFunctionName();
}

bool ento::CallDescription::matchQualifiedNameParts(const Decl *D) const {
  const auto FindNextNamespaceOrRecord =
      [](const DeclContext *Ctx) -> const DeclContext * {
    while (Ctx && !isa<NamespaceDecl, RecordDecl>(Ctx))
      Ctx = Ctx->getParent();
    return Ctx;
  };

  auto QualifierPartsIt = begin_qualified_name_parts();
  const auto QualifierPartsEndIt = end_qualified_name_parts();

  // Match namespace and record names. Skip unrelated names if they don't
  // match.
  const DeclContext *Ctx = FindNextNamespaceOrRecord(D->getDeclContext());
  for (; Ctx && QualifierPartsIt != QualifierPartsEndIt;
       Ctx = FindNextNamespaceOrRecord(Ctx->getParent())) {
    // If not matched just continue and try matching for the next one.
    if (cast<NamedDecl>(Ctx)->getName() != *QualifierPartsIt)
      continue;
    ++QualifierPartsIt;
  }

  // We matched if we consumed all expected qualifier segments.
  return QualifierPartsIt == QualifierPartsEndIt;
}

bool ento::CallDescription::matchesImpl(const FunctionDecl *FD, size_t ArgCount,
                                        size_t ParamCount) const {
  if (!FD)
    return false;

  const bool isMethod = isa<CXXMethodDecl>(FD);

  if (MatchAs == Mode::SimpleFunc && isMethod)
    return false;

  if (MatchAs == Mode::CXXMethod && !isMethod)
    return false;

  if (MatchAs == Mode::CLibraryMaybeHardened) {
    // In addition to accepting FOO() with CLibrary rules, we also want to
    // accept calls to __FOO_chk() and __builtin___FOO_chk().
    if (CheckerContext::isCLibraryFunction(FD) &&
        CheckerContext::isHardenedVariantOf(FD, getFunctionName())) {
      // Check that the actual argument/parameter counts are greater or equal
      // to the required counts. (Setting a requirement to std::nullopt matches
      // anything, so in that case value_or ensures that the value is compared
      // with itself.)
      return (RequiredArgs.value_or(ArgCount) <= ArgCount &&
              RequiredParams.value_or(ParamCount) <= ParamCount);
    }
  }

  if (RequiredArgs.value_or(ArgCount) != ArgCount ||
      RequiredParams.value_or(ParamCount) != ParamCount)
    return false;

  if (MatchAs == Mode::CLibrary || MatchAs == Mode::CLibraryMaybeHardened)
    return CheckerContext::isCLibraryFunction(FD, getFunctionName());

  if (!matchNameOnly(FD))
    return false;

  if (!hasQualifiedNameParts())
    return true;

  return matchQualifiedNameParts(FD);
}

ento::CallDescriptionSet::CallDescriptionSet(
    std::initializer_list<CallDescription> &&List) {
  Impl.LinearMap.reserve(List.size());
  for (const CallDescription &CD : List)
    Impl.LinearMap.push_back({CD, /*unused*/ true});
}

bool ento::CallDescriptionSet::contains(const CallEvent &Call) const {
  return static_cast<bool>(Impl.lookup(Call));
}

bool ento::CallDescriptionSet::containsAsWritten(const CallExpr &CE) const {
  return static_cast<bool>(Impl.lookupAsWritten(CE));
}
