//===- RewriteRope.cpp - Rope specialized for rewriter --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the RewriteRope class, which is a powerful string.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Core/RewriteRope.h"
#include "clang/Basic/LLVM.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <cstring>

using namespace clang;

/// RewriteRope is a "strong" string class, designed to make insertions and
/// deletions in the middle of the string nearly constant time (really, they are
/// O(log N), but with a very low constant factor).
///
/// The implementation of this datastructure is a conceptual linear sequence of
/// RopePiece elements.  Each RopePiece represents a view on a separately
/// allocated and reference counted string.  This means that splitting a very
/// long string can be done in constant time by splitting a RopePiece that
/// references the whole string into two rope pieces that reference each half.
/// Once split, another string can be inserted in between the two halves by
/// inserting a RopePiece in between the two others.  All of this is very
/// inexpensive: it takes time proportional to the number of RopePieces, not the
/// length of the strings they represent.
///
/// While a linear sequences of RopePieces is the conceptual model, the actual
/// implementation captures them in an adapted B+ Tree.  Using a B+ tree (which
/// is a tree that keeps the values in the leaves and has where each node
/// contains a reasonable number of pointers to children/values) allows us to
/// maintain efficient operation when the RewriteRope contains a *huge* number
/// of RopePieces.  The basic idea of the B+ Tree is that it allows us to find
/// the RopePiece corresponding to some offset very efficiently, and it
/// automatically balances itself on insertions of RopePieces (which can happen
/// for both insertions and erases of string ranges).
///
/// The one wrinkle on the theory is that we don't attempt to keep the tree
/// properly balanced when erases happen.  Erases of string data can both insert
/// new RopePieces (e.g. when the middle of some other rope piece is deleted,
/// which results in two rope pieces, which is just like an insert) or it can
/// reduce the number of RopePieces maintained by the B+Tree.  In the case when
/// the number of RopePieces is reduced, we don't attempt to maintain the
/// standard 'invariant' that each node in the tree contains at least
/// 'WidthFactor' children/values.  For our use cases, this doesn't seem to
/// matter.
///
/// The implementation below is primarily implemented in terms of three classes:
///   RopePieceBTreeNode - Common base class for:
///
///     RopePieceBTreeLeaf - Directly manages up to '2*WidthFactor' RopePiece
///          nodes.  This directly represents a chunk of the string with those
///          RopePieces concatenated.
///     RopePieceBTreeInterior - An interior node in the B+ Tree, which manages
///          up to '2*WidthFactor' other nodes in the tree.

namespace {

//===----------------------------------------------------------------------===//
// RopePieceBTreeNode Class
//===----------------------------------------------------------------------===//

  /// RopePieceBTreeNode - Common base class of RopePieceBTreeLeaf and
  /// RopePieceBTreeInterior.  This provides some 'virtual' dispatching methods
  /// and a flag that determines which subclass the instance is.  Also
  /// important, this node knows the full extend of the node, including any
  /// children that it has.  This allows efficient skipping over entire subtrees
  /// when looking for an offset in the BTree.
  class RopePieceBTreeNode {
  protected:
    /// WidthFactor - This controls the number of K/V slots held in the BTree:
    /// how wide it is.  Each level of the BTree is guaranteed to have at least
    /// 'WidthFactor' elements in it (either ropepieces or children), (except
    /// the root, which may have less) and may have at most 2*WidthFactor
    /// elements.
    enum { WidthFactor = 8 };

    /// Size - This is the number of bytes of file this node (including any
    /// potential children) covers.
    unsigned Size = 0;

    /// IsLeaf - True if this is an instance of RopePieceBTreeLeaf, false if it
    /// is an instance of RopePieceBTreeInterior.
    bool IsLeaf;

    RopePieceBTreeNode(bool isLeaf) : IsLeaf(isLeaf) {}
    ~RopePieceBTreeNode() = default;

  public:
    bool isLeaf() const { return IsLeaf; }
    unsigned size() const { return Size; }

    void Destroy();

    /// split - Split the range containing the specified offset so that we are
    /// guaranteed that there is a place to do an insertion at the specified
    /// offset.  The offset is relative, so "0" is the start of the node.
    ///
    /// If there is no space in this subtree for the extra piece, the extra tree
    /// node is returned and must be inserted into a parent.
    RopePieceBTreeNode *split(unsigned Offset);

    /// insert - Insert the specified ropepiece into this tree node at the
    /// specified offset.  The offset is relative, so "0" is the start of the
    /// node.
    ///
    /// If there is no space in this subtree for the extra piece, the extra tree
    /// node is returned and must be inserted into a parent.
    RopePieceBTreeNode *insert(unsigned Offset, const RopePiece &R);

    /// erase - Remove NumBytes from this node at the specified offset.  We are
    /// guaranteed that there is a split at Offset.
    void erase(unsigned Offset, unsigned NumBytes);
  };

//===----------------------------------------------------------------------===//
// RopePieceBTreeLeaf Class
//===----------------------------------------------------------------------===//

  /// RopePieceBTreeLeaf - Directly manages up to '2*WidthFactor' RopePiece
  /// nodes.  This directly represents a chunk of the string with those
  /// RopePieces concatenated.  Since this is a B+Tree, all values (in this case
  /// instances of RopePiece) are stored in leaves like this.  To make iteration
  /// over the leaves efficient, they maintain a singly linked list through the
  /// NextLeaf field.  This allows the B+Tree forward iterator to be constant
  /// time for all increments.
  class RopePieceBTreeLeaf : public RopePieceBTreeNode {
    /// NumPieces - This holds the number of rope pieces currently active in the
    /// Pieces array.
    unsigned char NumPieces = 0;

    /// Pieces - This tracks the file chunks currently in this leaf.
    RopePiece Pieces[2*WidthFactor];

    /// NextLeaf - This is a pointer to the next leaf in the tree, allowing
    /// efficient in-order forward iteration of the tree without traversal.
    RopePieceBTreeLeaf **PrevLeaf = nullptr;
    RopePieceBTreeLeaf *NextLeaf = nullptr;

  public:
    RopePieceBTreeLeaf() : RopePieceBTreeNode(true) {}

    ~RopePieceBTreeLeaf() {
      if (PrevLeaf || NextLeaf)
        removeFromLeafInOrder();
      clear();
    }

    bool isFull() const { return NumPieces == 2*WidthFactor; }

    /// clear - Remove all rope pieces from this leaf.
    void clear() {
      while (NumPieces)
        Pieces[--NumPieces] = RopePiece();
      Size = 0;
    }

    unsigned getNumPieces() const { return NumPieces; }

    const RopePiece &getPiece(unsigned i) const {
      assert(i < getNumPieces() && "Invalid piece ID");
      return Pieces[i];
    }

    const RopePieceBTreeLeaf *getNextLeafInOrder() const { return NextLeaf; }

    void insertAfterLeafInOrder(RopePieceBTreeLeaf *Node) {
      assert(!PrevLeaf && !NextLeaf && "Already in ordering");

      NextLeaf = Node->NextLeaf;
      if (NextLeaf)
        NextLeaf->PrevLeaf = &NextLeaf;
      PrevLeaf = &Node->NextLeaf;
      Node->NextLeaf = this;
    }

    void removeFromLeafInOrder() {
      if (PrevLeaf) {
        *PrevLeaf = NextLeaf;
        if (NextLeaf)
          NextLeaf->PrevLeaf = PrevLeaf;
      } else if (NextLeaf) {
        NextLeaf->PrevLeaf = nullptr;
      }
    }

    /// FullRecomputeSizeLocally - This method recomputes the 'Size' field by
    /// summing the size of all RopePieces.
    void FullRecomputeSizeLocally() {
      Size = 0;
      for (unsigned i = 0, e = getNumPieces(); i != e; ++i)
        Size += getPiece(i).size();
    }

    /// split - Split the range containing the specified offset so that we are
    /// guaranteed that there is a place to do an insertion at the specified
    /// offset.  The offset is relative, so "0" is the start of the node.
    ///
    /// If there is no space in this subtree for the extra piece, the extra tree
    /// node is returned and must be inserted into a parent.
    RopePieceBTreeNode *split(unsigned Offset);

    /// insert - Insert the specified ropepiece into this tree node at the
    /// specified offset.  The offset is relative, so "0" is the start of the
    /// node.
    ///
    /// If there is no space in this subtree for the extra piece, the extra tree
    /// node is returned and must be inserted into a parent.
    RopePieceBTreeNode *insert(unsigned Offset, const RopePiece &R);

    /// erase - Remove NumBytes from this node at the specified offset.  We are
    /// guaranteed that there is a split at Offset.
    void erase(unsigned Offset, unsigned NumBytes);

    static bool classof(const RopePieceBTreeNode *N) {
      return N->isLeaf();
    }
  };

} // namespace

/// split - Split the range containing the specified offset so that we are
/// guaranteed that there is a place to do an insertion at the specified
/// offset.  The offset is relative, so "0" is the start of the node.
///
/// If there is no space in this subtree for the extra piece, the extra tree
/// node is returned and must be inserted into a parent.
RopePieceBTreeNode *RopePieceBTreeLeaf::split(unsigned Offset) {
  // Find the insertion point.  We are guaranteed that there is a split at the
  // specified offset so find it.
  if (Offset == 0 || Offset == size()) {
    // Fastpath for a common case.  There is already a splitpoint at the end.
    return nullptr;
  }

  // Find the piece that this offset lands in.
  unsigned PieceOffs = 0;
  unsigned i = 0;
  while (Offset >= PieceOffs+Pieces[i].size()) {
    PieceOffs += Pieces[i].size();
    ++i;
  }

  // If there is already a split point at the specified offset, just return
  // success.
  if (PieceOffs == Offset)
    return nullptr;

  // Otherwise, we need to split piece 'i' at Offset-PieceOffs.  Convert Offset
  // to being Piece relative.
  unsigned IntraPieceOffset = Offset-PieceOffs;

  // We do this by shrinking the RopePiece and then doing an insert of the tail.
  RopePiece Tail(Pieces[i].StrData, Pieces[i].StartOffs+IntraPieceOffset,
                 Pieces[i].EndOffs);
  Size -= Pieces[i].size();
  Pieces[i].EndOffs = Pieces[i].StartOffs+IntraPieceOffset;
  Size += Pieces[i].size();

  return insert(Offset, Tail);
}

/// insert - Insert the specified RopePiece into this tree node at the
/// specified offset.  The offset is relative, so "0" is the start of the node.
///
/// If there is no space in this subtree for the extra piece, the extra tree
/// node is returned and must be inserted into a parent.
RopePieceBTreeNode *RopePieceBTreeLeaf::insert(unsigned Offset,
                                               const RopePiece &R) {
  // If this node is not full, insert the piece.
  if (!isFull()) {
    // Find the insertion point.  We are guaranteed that there is a split at the
    // specified offset so find it.
    unsigned i = 0, e = getNumPieces();
    if (Offset == size()) {
      // Fastpath for a common case.
      i = e;
    } else {
      unsigned SlotOffs = 0;
      for (; Offset > SlotOffs; ++i)
        SlotOffs += getPiece(i).size();
      assert(SlotOffs == Offset && "Split didn't occur before insertion!");
    }

    // For an insertion into a non-full leaf node, just insert the value in
    // its sorted position.  This requires moving later values over.
    for (; i != e; --e)
      Pieces[e] = Pieces[e-1];
    Pieces[i] = R;
    ++NumPieces;
    Size += R.size();
    return nullptr;
  }

  // Otherwise, if this is leaf is full, split it in two halves.  Since this
  // node is full, it contains 2*WidthFactor values.  We move the first
  // 'WidthFactor' values to the LHS child (which we leave in this node) and
  // move the last 'WidthFactor' values into the RHS child.

  // Create the new node.
  RopePieceBTreeLeaf *NewNode = new RopePieceBTreeLeaf();

  // Move over the last 'WidthFactor' values from here to NewNode.
  std::copy(&Pieces[WidthFactor], &Pieces[2*WidthFactor],
            &NewNode->Pieces[0]);
  // Replace old pieces with null RopePieces to drop refcounts.
  std::fill(&Pieces[WidthFactor], &Pieces[2*WidthFactor], RopePiece());

  // Decrease the number of values in the two nodes.
  NewNode->NumPieces = NumPieces = WidthFactor;

  // Recompute the two nodes' size.
  NewNode->FullRecomputeSizeLocally();
  FullRecomputeSizeLocally();

  // Update the list of leaves.
  NewNode->insertAfterLeafInOrder(this);

  // These insertions can't fail.
  if (this->size() >= Offset)
    this->insert(Offset, R);
  else
    NewNode->insert(Offset - this->size(), R);
  return NewNode;
}

/// erase - Remove NumBytes from this node at the specified offset.  We are
/// guaranteed that there is a split at Offset.
void RopePieceBTreeLeaf::erase(unsigned Offset, unsigned NumBytes) {
  // Since we are guaranteed that there is a split at Offset, we start by
  // finding the Piece that starts there.
  unsigned PieceOffs = 0;
  unsigned i = 0;
  for (; Offset > PieceOffs; ++i)
    PieceOffs += getPiece(i).size();
  assert(PieceOffs == Offset && "Split didn't occur before erase!");

  unsigned StartPiece = i;

  // Figure out how many pieces completely cover 'NumBytes'.  We want to remove
  // all of them.
  for (; Offset+NumBytes > PieceOffs+getPiece(i).size(); ++i)
    PieceOffs += getPiece(i).size();

  // If we exactly include the last one, include it in the region to delete.
  if (Offset+NumBytes == PieceOffs+getPiece(i).size()) {
    PieceOffs += getPiece(i).size();
    ++i;
  }

  // If we completely cover some RopePieces, erase them now.
  if (i != StartPiece) {
    unsigned NumDeleted = i-StartPiece;
    for (; i != getNumPieces(); ++i)
      Pieces[i-NumDeleted] = Pieces[i];

    // Drop references to dead rope pieces.
    std::fill(&Pieces[getNumPieces()-NumDeleted], &Pieces[getNumPieces()],
              RopePiece());
    NumPieces -= NumDeleted;

    unsigned CoverBytes = PieceOffs-Offset;
    NumBytes -= CoverBytes;
    Size -= CoverBytes;
  }

  // If we completely removed some stuff, we could be done.
  if (NumBytes == 0) return;

  // Okay, now might be erasing part of some Piece.  If this is the case, then
  // move the start point of the piece.
  assert(getPiece(StartPiece).size() > NumBytes);
  Pieces[StartPiece].StartOffs += NumBytes;

  // The size of this node just shrunk by NumBytes.
  Size -= NumBytes;
}

//===----------------------------------------------------------------------===//
// RopePieceBTreeInterior Class
//===----------------------------------------------------------------------===//

namespace {

  /// RopePieceBTreeInterior - This represents an interior node in the B+Tree,
  /// which holds up to 2*WidthFactor pointers to child nodes.
  class RopePieceBTreeInterior : public RopePieceBTreeNode {
    /// NumChildren - This holds the number of children currently active in the
    /// Children array.
    unsigned char NumChildren = 0;

    RopePieceBTreeNode *Children[2*WidthFactor];

  public:
    RopePieceBTreeInterior() : RopePieceBTreeNode(false) {}

    RopePieceBTreeInterior(RopePieceBTreeNode *LHS, RopePieceBTreeNode *RHS)
        : RopePieceBTreeNode(false) {
      Children[0] = LHS;
      Children[1] = RHS;
      NumChildren = 2;
      Size = LHS->size() + RHS->size();
    }

    ~RopePieceBTreeInterior() {
      for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
        Children[i]->Destroy();
    }

    bool isFull() const { return NumChildren == 2*WidthFactor; }

    unsigned getNumChildren() const { return NumChildren; }

    const RopePieceBTreeNode *getChild(unsigned i) const {
      assert(i < NumChildren && "invalid child #");
      return Children[i];
    }

    RopePieceBTreeNode *getChild(unsigned i) {
      assert(i < NumChildren && "invalid child #");
      return Children[i];
    }

    /// FullRecomputeSizeLocally - Recompute the Size field of this node by
    /// summing up the sizes of the child nodes.
    void FullRecomputeSizeLocally() {
      Size = 0;
      for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
        Size += getChild(i)->size();
    }

    /// split - Split the range containing the specified offset so that we are
    /// guaranteed that there is a place to do an insertion at the specified
    /// offset.  The offset is relative, so "0" is the start of the node.
    ///
    /// If there is no space in this subtree for the extra piece, the extra tree
    /// node is returned and must be inserted into a parent.
    RopePieceBTreeNode *split(unsigned Offset);

    /// insert - Insert the specified ropepiece into this tree node at the
    /// specified offset.  The offset is relative, so "0" is the start of the
    /// node.
    ///
    /// If there is no space in this subtree for the extra piece, the extra tree
    /// node is returned and must be inserted into a parent.
    RopePieceBTreeNode *insert(unsigned Offset, const RopePiece &R);

    /// HandleChildPiece - A child propagated an insertion result up to us.
    /// Insert the new child, and/or propagate the result further up the tree.
    RopePieceBTreeNode *HandleChildPiece(unsigned i, RopePieceBTreeNode *RHS);

    /// erase - Remove NumBytes from this node at the specified offset.  We are
    /// guaranteed that there is a split at Offset.
    void erase(unsigned Offset, unsigned NumBytes);

    static bool classof(const RopePieceBTreeNode *N) {
      return !N->isLeaf();
    }
  };

} // namespace

/// split - Split the range containing the specified offset so that we are
/// guaranteed that there is a place to do an insertion at the specified
/// offset.  The offset is relative, so "0" is the start of the node.
///
/// If there is no space in this subtree for the extra piece, the extra tree
/// node is returned and must be inserted into a parent.
RopePieceBTreeNode *RopePieceBTreeInterior::split(unsigned Offset) {
  // Figure out which child to split.
  if (Offset == 0 || Offset == size())
    return nullptr; // If we have an exact offset, we're already split.

  unsigned ChildOffset = 0;
  unsigned i = 0;
  for (; Offset >= ChildOffset+getChild(i)->size(); ++i)
    ChildOffset += getChild(i)->size();

  // If already split there, we're done.
  if (ChildOffset == Offset)
    return nullptr;

  // Otherwise, recursively split the child.
  if (RopePieceBTreeNode *RHS = getChild(i)->split(Offset-ChildOffset))
    return HandleChildPiece(i, RHS);
  return nullptr; // Done!
}

/// insert - Insert the specified ropepiece into this tree node at the
/// specified offset.  The offset is relative, so "0" is the start of the
/// node.
///
/// If there is no space in this subtree for the extra piece, the extra tree
/// node is returned and must be inserted into a parent.
RopePieceBTreeNode *RopePieceBTreeInterior::insert(unsigned Offset,
                                                   const RopePiece &R) {
  // Find the insertion point.  We are guaranteed that there is a split at the
  // specified offset so find it.
  unsigned i = 0, e = getNumChildren();

  unsigned ChildOffs = 0;
  if (Offset == size()) {
    // Fastpath for a common case.  Insert at end of last child.
    i = e-1;
    ChildOffs = size()-getChild(i)->size();
  } else {
    for (; Offset > ChildOffs+getChild(i)->size(); ++i)
      ChildOffs += getChild(i)->size();
  }

  Size += R.size();

  // Insert at the end of this child.
  if (RopePieceBTreeNode *RHS = getChild(i)->insert(Offset-ChildOffs, R))
    return HandleChildPiece(i, RHS);

  return nullptr;
}

/// HandleChildPiece - A child propagated an insertion result up to us.
/// Insert the new child, and/or propagate the result further up the tree.
RopePieceBTreeNode *
RopePieceBTreeInterior::HandleChildPiece(unsigned i, RopePieceBTreeNode *RHS) {
  // Otherwise the child propagated a subtree up to us as a new child.  See if
  // we have space for it here.
  if (!isFull()) {
    // Insert RHS after child 'i'.
    if (i + 1 != getNumChildren())
      memmove(&Children[i+2], &Children[i+1],
              (getNumChildren()-i-1)*sizeof(Children[0]));
    Children[i+1] = RHS;
    ++NumChildren;
    return nullptr;
  }

  // Okay, this node is full.  Split it in half, moving WidthFactor children to
  // a newly allocated interior node.

  // Create the new node.
  RopePieceBTreeInterior *NewNode = new RopePieceBTreeInterior();

  // Move over the last 'WidthFactor' values from here to NewNode.
  memcpy(&NewNode->Children[0], &Children[WidthFactor],
         WidthFactor*sizeof(Children[0]));

  // Decrease the number of values in the two nodes.
  NewNode->NumChildren = NumChildren = WidthFactor;

  // Finally, insert the two new children in the side the can (now) hold them.
  // These insertions can't fail.
  if (i < WidthFactor)
    this->HandleChildPiece(i, RHS);
  else
    NewNode->HandleChildPiece(i-WidthFactor, RHS);

  // Recompute the two nodes' size.
  NewNode->FullRecomputeSizeLocally();
  FullRecomputeSizeLocally();
  return NewNode;
}

/// erase - Remove NumBytes from this node at the specified offset.  We are
/// guaranteed that there is a split at Offset.
void RopePieceBTreeInterior::erase(unsigned Offset, unsigned NumBytes) {
  // This will shrink this node by NumBytes.
  Size -= NumBytes;

  // Find the first child that overlaps with Offset.
  unsigned i = 0;
  for (; Offset >= getChild(i)->size(); ++i)
    Offset -= getChild(i)->size();

  // Propagate the delete request into overlapping children, or completely
  // delete the children as appropriate.
  while (NumBytes) {
    RopePieceBTreeNode *CurChild = getChild(i);

    // If we are deleting something contained entirely in the child, pass on the
    // request.
    if (Offset+NumBytes < CurChild->size()) {
      CurChild->erase(Offset, NumBytes);
      return;
    }

    // If this deletion request starts somewhere in the middle of the child, it
    // must be deleting to the end of the child.
    if (Offset) {
      unsigned BytesFromChild = CurChild->size()-Offset;
      CurChild->erase(Offset, BytesFromChild);
      NumBytes -= BytesFromChild;
      // Start at the beginning of the next child.
      Offset = 0;
      ++i;
      continue;
    }

    // If the deletion request completely covers the child, delete it and move
    // the rest down.
    NumBytes -= CurChild->size();
    CurChild->Destroy();
    --NumChildren;
    if (i != getNumChildren())
      memmove(&Children[i], &Children[i+1],
              (getNumChildren()-i)*sizeof(Children[0]));
  }
}

//===----------------------------------------------------------------------===//
// RopePieceBTreeNode Implementation
//===----------------------------------------------------------------------===//

void RopePieceBTreeNode::Destroy() {
  if (auto *Leaf = dyn_cast<RopePieceBTreeLeaf>(this))
    delete Leaf;
  else
    delete cast<RopePieceBTreeInterior>(this);
}

/// split - Split the range containing the specified offset so that we are
/// guaranteed that there is a place to do an insertion at the specified
/// offset.  The offset is relative, so "0" is the start of the node.
///
/// If there is no space in this subtree for the extra piece, the extra tree
/// node is returned and must be inserted into a parent.
RopePieceBTreeNode *RopePieceBTreeNode::split(unsigned Offset) {
  assert(Offset <= size() && "Invalid offset to split!");
  if (auto *Leaf = dyn_cast<RopePieceBTreeLeaf>(this))
    return Leaf->split(Offset);
  return cast<RopePieceBTreeInterior>(this)->split(Offset);
}

/// insert - Insert the specified ropepiece into this tree node at the
/// specified offset.  The offset is relative, so "0" is the start of the
/// node.
///
/// If there is no space in this subtree for the extra piece, the extra tree
/// node is returned and must be inserted into a parent.
RopePieceBTreeNode *RopePieceBTreeNode::insert(unsigned Offset,
                                               const RopePiece &R) {
  assert(Offset <= size() && "Invalid offset to insert!");
  if (auto *Leaf = dyn_cast<RopePieceBTreeLeaf>(this))
    return Leaf->insert(Offset, R);
  return cast<RopePieceBTreeInterior>(this)->insert(Offset, R);
}

/// erase - Remove NumBytes from this node at the specified offset.  We are
/// guaranteed that there is a split at Offset.
void RopePieceBTreeNode::erase(unsigned Offset, unsigned NumBytes) {
  assert(Offset+NumBytes <= size() && "Invalid offset to erase!");
  if (auto *Leaf = dyn_cast<RopePieceBTreeLeaf>(this))
    return Leaf->erase(Offset, NumBytes);
  return cast<RopePieceBTreeInterior>(this)->erase(Offset, NumBytes);
}

//===----------------------------------------------------------------------===//
// RopePieceBTreeIterator Implementation
//===----------------------------------------------------------------------===//

static const RopePieceBTreeLeaf *getCN(const void *P) {
  return static_cast<const RopePieceBTreeLeaf*>(P);
}

// begin iterator.
RopePieceBTreeIterator::RopePieceBTreeIterator(const void *n) {
  const auto *N = static_cast<const RopePieceBTreeNode *>(n);

  // Walk down the left side of the tree until we get to a leaf.
  while (const auto *IN = dyn_cast<RopePieceBTreeInterior>(N))
    N = IN->getChild(0);

  // We must have at least one leaf.
  CurNode = cast<RopePieceBTreeLeaf>(N);

  // If we found a leaf that happens to be empty, skip over it until we get
  // to something full.
  while (CurNode && getCN(CurNode)->getNumPieces() == 0)
    CurNode = getCN(CurNode)->getNextLeafInOrder();

  if (CurNode)
    CurPiece = &getCN(CurNode)->getPiece(0);
  else  // Empty tree, this is an end() iterator.
    CurPiece = nullptr;
  CurChar = 0;
}

void RopePieceBTreeIterator::MoveToNextPiece() {
  if (CurPiece != &getCN(CurNode)->getPiece(getCN(CurNode)->getNumPieces()-1)) {
    CurChar = 0;
    ++CurPiece;
    return;
  }

  // Find the next non-empty leaf node.
  do
    CurNode = getCN(CurNode)->getNextLeafInOrder();
  while (CurNode && getCN(CurNode)->getNumPieces() == 0);

  if (CurNode)
    CurPiece = &getCN(CurNode)->getPiece(0);
  else // Hit end().
    CurPiece = nullptr;
  CurChar = 0;
}

//===----------------------------------------------------------------------===//
// RopePieceBTree Implementation
//===----------------------------------------------------------------------===//

static RopePieceBTreeNode *getRoot(void *P) {
  return static_cast<RopePieceBTreeNode*>(P);
}

RopePieceBTree::RopePieceBTree() {
  Root = new RopePieceBTreeLeaf();
}

RopePieceBTree::RopePieceBTree(const RopePieceBTree &RHS) {
  assert(RHS.empty() && "Can't copy non-empty tree yet");
  Root = new RopePieceBTreeLeaf();
}

RopePieceBTree::~RopePieceBTree() {
  getRoot(Root)->Destroy();
}

unsigned RopePieceBTree::size() const {
  return getRoot(Root)->size();
}

void RopePieceBTree::clear() {
  if (auto *Leaf = dyn_cast<RopePieceBTreeLeaf>(getRoot(Root)))
    Leaf->clear();
  else {
    getRoot(Root)->Destroy();
    Root = new RopePieceBTreeLeaf();
  }
}

void RopePieceBTree::insert(unsigned Offset, const RopePiece &R) {
  // #1. Split at Offset.
  if (RopePieceBTreeNode *RHS = getRoot(Root)->split(Offset))
    Root = new RopePieceBTreeInterior(getRoot(Root), RHS);

  // #2. Do the insertion.
  if (RopePieceBTreeNode *RHS = getRoot(Root)->insert(Offset, R))
    Root = new RopePieceBTreeInterior(getRoot(Root), RHS);
}

void RopePieceBTree::erase(unsigned Offset, unsigned NumBytes) {
  // #1. Split at Offset.
  if (RopePieceBTreeNode *RHS = getRoot(Root)->split(Offset))
    Root = new RopePieceBTreeInterior(getRoot(Root), RHS);

  // #2. Do the erasing.
  getRoot(Root)->erase(Offset, NumBytes);
}

//===----------------------------------------------------------------------===//
// RewriteRope Implementation
//===----------------------------------------------------------------------===//

/// MakeRopeString - This copies the specified byte range into some instance of
/// RopeRefCountString, and return a RopePiece that represents it.  This uses
/// the AllocBuffer object to aggregate requests for small strings into one
/// allocation instead of doing tons of tiny allocations.
RopePiece RewriteRope::MakeRopeString(const char *Start, const char *End) {
  unsigned Len = End-Start;
  assert(Len && "Zero length RopePiece is invalid!");

  // If we have space for this string in the current alloc buffer, use it.
  if (AllocOffs+Len <= AllocChunkSize) {
    memcpy(AllocBuffer->Data+AllocOffs, Start, Len);
    AllocOffs += Len;
    return RopePiece(AllocBuffer, AllocOffs-Len, AllocOffs);
  }

  // If we don't have enough room because this specific allocation is huge,
  // just allocate a new rope piece for it alone.
  if (Len > AllocChunkSize) {
    unsigned Size = End-Start+sizeof(RopeRefCountString)-1;
    auto *Res = reinterpret_cast<RopeRefCountString *>(new char[Size]);
    Res->RefCount = 0;
    memcpy(Res->Data, Start, End-Start);
    return RopePiece(Res, 0, End-Start);
  }

  // Otherwise, this was a small request but we just don't have space for it
  // Make a new chunk and share it with later allocations.

  unsigned AllocSize = offsetof(RopeRefCountString, Data) + AllocChunkSize;
  auto *Res = reinterpret_cast<RopeRefCountString *>(new char[AllocSize]);
  Res->RefCount = 0;
  memcpy(Res->Data, Start, Len);
  AllocBuffer = Res;
  AllocOffs = Len;

  return RopePiece(AllocBuffer, 0, Len);
}
