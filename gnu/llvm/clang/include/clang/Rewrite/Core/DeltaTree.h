//===- DeltaTree.h - B-Tree for Rewrite Delta tracking ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the DeltaTree class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_CORE_DELTATREE_H
#define LLVM_CLANG_REWRITE_CORE_DELTATREE_H

namespace clang {

  /// DeltaTree - a multiway search tree (BTree) structure with some fancy
  /// features.  B-Trees are generally more memory and cache efficient than
  /// binary trees, because they store multiple keys/values in each node.  This
  /// implements a key/value mapping from index to delta, and allows fast lookup
  /// on index.  However, an added (important) bonus is that it can also
  /// efficiently tell us the full accumulated delta for a specific file offset
  /// as well, without traversing the whole tree.
  class DeltaTree {
    void *Root;    // "DeltaTreeNode *"

  public:
    DeltaTree();

    // Note: Currently we only support copying when the RHS is empty.
    DeltaTree(const DeltaTree &RHS);

    DeltaTree &operator=(const DeltaTree &) = delete;
    ~DeltaTree();

    /// getDeltaAt - Return the accumulated delta at the specified file offset.
    /// This includes all insertions or delections that occurred *before* the
    /// specified file index.
    int getDeltaAt(unsigned FileIndex) const;

    /// AddDelta - When a change is made that shifts around the text buffer,
    /// this method is used to record that info.  It inserts a delta of 'Delta'
    /// into the current DeltaTree at offset FileIndex.
    void AddDelta(unsigned FileIndex, int Delta);
  };

} // namespace clang

#endif // LLVM_CLANG_REWRITE_CORE_DELTATREE_H
