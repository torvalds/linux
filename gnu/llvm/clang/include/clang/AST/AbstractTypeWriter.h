//==--- AbstractTypeWriter.h - Abstract serialization for types -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ABSTRACTTYPEWRITER_H
#define LLVM_CLANG_AST_ABSTRACTTYPEWRITER_H

#include "clang/AST/Type.h"
#include "clang/AST/AbstractBasicWriter.h"
#include "clang/AST/DeclObjC.h"

namespace clang {
namespace serialization {

// template <class PropertyWriter>
// class AbstractTypeWriter {
// public:
//   AbstractTypeWriter(PropertyWriter &W);
//   void write(QualType type);
// };
//
// The actual class is auto-generated; see ClangASTPropertiesEmitter.cpp.
#include "clang/AST/AbstractTypeWriter.inc"

} // end namespace serialization
} // end namespace clang

#endif
