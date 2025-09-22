//===- ASTDiffInternal.h --------------------------------------*- C++ -*- -===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_ASTDIFF_ASTDIFFINTERNAL_H
#define LLVM_CLANG_TOOLING_ASTDIFF_ASTDIFFINTERNAL_H

#include "clang/AST/ASTTypeTraits.h"

namespace clang {
namespace diff {

using DynTypedNode = DynTypedNode;

/// Within a tree, this identifies a node by its preorder offset.
struct NodeId {
private:
  static constexpr int InvalidNodeId = -1;

public:
  int Id;

  NodeId() : Id(InvalidNodeId) {}
  NodeId(int Id) : Id(Id) {}

  operator int() const { return Id; }
  NodeId &operator++() { return ++Id, *this; }
  NodeId &operator--() { return --Id, *this; }
  // Support defining iterators on NodeId.
  NodeId &operator*() { return *this; }

  bool isValid() const { return Id != InvalidNodeId; }
  bool isInvalid() const { return Id == InvalidNodeId; }
};

} // end namespace diff
} // end namespace clang
#endif
