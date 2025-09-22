//===-- tsan_ilist.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_ILIST_H
#define TSAN_ILIST_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __tsan {

class INode {
 public:
  INode() = default;

 private:
  INode* next_ = nullptr;
  INode* prev_ = nullptr;

  template <typename Base, INode Base::*Node, typename Elem>
  friend class IList;
  INode(const INode&) = delete;
  void operator=(const INode&) = delete;
};

// Intrusive doubly-linked list.
//
// The node class (MyNode) needs to include "INode foo" field,
// then the list can be declared as IList<MyNode, &MyNode::foo>.
// This design allows to link MyNode into multiple lists using
// different INode fields.
// The optional Elem template argument allows to specify node MDT
// (most derived type) if it's different from MyNode.
template <typename Base, INode Base::*Node, typename Elem = Base>
class IList {
 public:
  IList();

  void PushFront(Elem* e);
  void PushBack(Elem* e);
  void Remove(Elem* e);

  Elem* PopFront();
  Elem* PopBack();
  Elem* Front();
  Elem* Back();

  // Prev links point towards front of the queue.
  Elem* Prev(Elem* e);
  // Next links point towards back of the queue.
  Elem* Next(Elem* e);

  uptr Size() const;
  bool Empty() const;
  bool Queued(Elem* e) const;

 private:
  INode node_;
  uptr size_ = 0;

  void Push(Elem* e, INode* after);
  static INode* ToNode(Elem* e);
  static Elem* ToElem(INode* n);

  IList(const IList&) = delete;
  void operator=(const IList&) = delete;
};

template <typename Base, INode Base::*Node, typename Elem>
IList<Base, Node, Elem>::IList() {
  node_.next_ = node_.prev_ = &node_;
}

template <typename Base, INode Base::*Node, typename Elem>
void IList<Base, Node, Elem>::PushFront(Elem* e) {
  Push(e, &node_);
}

template <typename Base, INode Base::*Node, typename Elem>
void IList<Base, Node, Elem>::PushBack(Elem* e) {
  Push(e, node_.prev_);
}

template <typename Base, INode Base::*Node, typename Elem>
void IList<Base, Node, Elem>::Push(Elem* e, INode* after) {
  INode* n = ToNode(e);
  DCHECK_EQ(n->next_, nullptr);
  DCHECK_EQ(n->prev_, nullptr);
  INode* next = after->next_;
  n->next_ = next;
  n->prev_ = after;
  next->prev_ = n;
  after->next_ = n;
  size_++;
}

template <typename Base, INode Base::*Node, typename Elem>
void IList<Base, Node, Elem>::Remove(Elem* e) {
  INode* n = ToNode(e);
  INode* next = n->next_;
  INode* prev = n->prev_;
  DCHECK(next);
  DCHECK(prev);
  DCHECK(size_);
  next->prev_ = prev;
  prev->next_ = next;
  n->prev_ = n->next_ = nullptr;
  size_--;
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::PopFront() {
  Elem* e = Front();
  if (e)
    Remove(e);
  return e;
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::PopBack() {
  Elem* e = Back();
  if (e)
    Remove(e);
  return e;
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::Front() {
  return size_ ? ToElem(node_.next_) : nullptr;
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::Back() {
  return size_ ? ToElem(node_.prev_) : nullptr;
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::Prev(Elem* e) {
  INode* n = ToNode(e);
  DCHECK(n->prev_);
  return n->prev_ != &node_ ? ToElem(n->prev_) : nullptr;
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::Next(Elem* e) {
  INode* n = ToNode(e);
  DCHECK(n->next_);
  return n->next_ != &node_ ? ToElem(n->next_) : nullptr;
}

template <typename Base, INode Base::*Node, typename Elem>
uptr IList<Base, Node, Elem>::Size() const {
  return size_;
}

template <typename Base, INode Base::*Node, typename Elem>
bool IList<Base, Node, Elem>::Empty() const {
  return size_ == 0;
}

template <typename Base, INode Base::*Node, typename Elem>
bool IList<Base, Node, Elem>::Queued(Elem* e) const {
  INode* n = ToNode(e);
  DCHECK_EQ(!n->next_, !n->prev_);
  return n->next_;
}

template <typename Base, INode Base::*Node, typename Elem>
INode* IList<Base, Node, Elem>::ToNode(Elem* e) {
  return &(e->*Node);
}

template <typename Base, INode Base::*Node, typename Elem>
Elem* IList<Base, Node, Elem>::ToElem(INode* n) {
  return static_cast<Elem*>(reinterpret_cast<Base*>(
      reinterpret_cast<uptr>(n) -
      reinterpret_cast<uptr>(&(reinterpret_cast<Elem*>(0)->*Node))));
}

}  // namespace __tsan

#endif
