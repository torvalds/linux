//===- CXTypes.h - Routines for manipulating CXTypes ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for manipulating CXCursors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CXTYPE_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CXTYPE_H

#include "clang-c/Index.h"
#include "clang/AST/Type.h"

namespace clang {
namespace cxtype {
  
CXType MakeCXType(QualType T, CXTranslationUnit TU);
  
}} // end namespace clang::cxtype
#endif
