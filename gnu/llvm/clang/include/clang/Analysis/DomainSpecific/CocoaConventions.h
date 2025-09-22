//===- CocoaConventions.h - Special handling of Cocoa conventions -*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements cocoa naming convention analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_DOMAINSPECIFIC_COCOACONVENTIONS_H
#define LLVM_CLANG_ANALYSIS_DOMAINSPECIFIC_COCOACONVENTIONS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
class FunctionDecl;
class QualType;

namespace ento {
namespace cocoa {

  bool isRefType(QualType RetTy, StringRef Prefix,
                 StringRef Name = StringRef());

  bool isCocoaObjectRef(QualType T);

}

namespace coreFoundation {
  bool isCFObjectRef(QualType T);

  bool followsCreateRule(const FunctionDecl *FD);
}

}} // end: "clang:ento"

#endif
