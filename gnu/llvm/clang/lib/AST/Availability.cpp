//===- Availability.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Availability information for Decls.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Availability.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/TargetInfo.h"

namespace clang {

AvailabilityInfo AvailabilityInfo::createFromDecl(const Decl *Decl) {
  ASTContext &Context = Decl->getASTContext();
  StringRef PlatformName = Context.getTargetInfo().getPlatformName();
  AvailabilityInfo Availability;

  // Collect availability attributes from all redeclarations.
  for (const auto *RD : Decl->redecls()) {
    for (const auto *A : RD->specific_attrs<AvailabilityAttr>()) {
      if (A->getPlatform()->getName() != PlatformName)
        continue;
      Availability = AvailabilityInfo(
          A->getPlatform()->getName(), A->getIntroduced(), A->getDeprecated(),
          A->getObsoleted(), A->getUnavailable(), false, false);
      break;
    }

    if (const auto *A = RD->getAttr<UnavailableAttr>())
      if (!A->isImplicit())
        Availability.UnconditionallyUnavailable = true;

    if (const auto *A = RD->getAttr<DeprecatedAttr>())
      if (!A->isImplicit())
        Availability.UnconditionallyDeprecated = true;
  }
  return Availability;
}

} // namespace clang
