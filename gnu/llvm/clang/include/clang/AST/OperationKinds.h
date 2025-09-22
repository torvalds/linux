//===- OperationKinds.h - Operation enums -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file enumerates the different kinds of operations that can be
// performed by various expressions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_OPERATIONKINDS_H
#define LLVM_CLANG_AST_OPERATIONKINDS_H

namespace clang {

/// CastKind - The kind of operation required for a conversion.
enum CastKind {
#define CAST_OPERATION(Name) CK_##Name,
#include "clang/AST/OperationKinds.def"
};

enum BinaryOperatorKind {
#define BINARY_OPERATION(Name, Spelling) BO_##Name,
#include "clang/AST/OperationKinds.def"
};

enum UnaryOperatorKind {
#define UNARY_OPERATION(Name, Spelling) UO_##Name,
#include "clang/AST/OperationKinds.def"
};

/// The kind of bridging performed by the Objective-C bridge cast.
enum ObjCBridgeCastKind {
  /// Bridging via __bridge, which does nothing but reinterpret
  /// the bits.
  OBC_Bridge,
  /// Bridging via __bridge_transfer, which transfers ownership of an
  /// Objective-C pointer into ARC.
  OBC_BridgeTransfer,
  /// Bridging via __bridge_retain, which makes an ARC object available
  /// as a +1 C pointer.
  OBC_BridgeRetained
};

}  // end namespace clang

#endif
