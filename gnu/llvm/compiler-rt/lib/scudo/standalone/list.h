//===-- list.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_LIST_H_
#define SCUDO_LIST_H_

#include "internal_defs.h"

namespace scudo {

// Intrusive POD singly and doubly linked list.
// An object with all zero fields should represent a valid empty list. clear()
// should be called on all non-zero-initialized objects before using.

template <class T> class IteratorBase {
public:
  explicit IteratorBase(T *CurrentT) : Current(CurrentT) {}
  IteratorBase &operator++() {
    Current = Current->Next;
    return *this;
  }
  bool operator!=(IteratorBase Other) const { return Current != Other.Current; }
  T &operator*() { return *Current; }

private:
  T *Current;
};

template <class T> struct IntrusiveList {
  bool empty() const { return Size == 0; }
  uptr size() const { return Size; }

  T *front() { return First; }
  const T *front() const { return First; }
  T *back() { return Last; }
  const T *back() const { return Last; }

  void clear() {
    First = Last = nullptr;
    Size = 0;
  }

  typedef IteratorBase<T> Iterator;
  typedef IteratorBase<const T> ConstIterator;

  Iterator begin() { return Iterator(First); }
  Iterator end() { return Iterator(nullptr); }

  ConstIterator begin() const { return ConstIterator(First); }
  ConstIterator end() const { return ConstIterator(nullptr); }

  void checkConsistency() const;

protected:
  uptr Size = 0;
  T *First = nullptr;
  T *Last = nullptr;
};

template <class T> void IntrusiveList<T>::checkConsistency() const {
  if (Size == 0) {
    CHECK_EQ(First, nullptr);
    CHECK_EQ(Last, nullptr);
  } else {
    uptr Count = 0;
    for (T *I = First;; I = I->Next) {
      Count++;
      if (I == Last)
        break;
    }
    CHECK_EQ(this->size(), Count);
    CHECK_EQ(Last->Next, nullptr);
  }
}

template <class T> struct SinglyLinkedList : public IntrusiveList<T> {
  using IntrusiveList<T>::First;
  using IntrusiveList<T>::Last;
  using IntrusiveList<T>::Size;
  using IntrusiveList<T>::empty;

  void push_back(T *X) {
    X->Next = nullptr;
    if (empty())
      First = X;
    else
      Last->Next = X;
    Last = X;
    Size++;
  }

  void push_front(T *X) {
    if (empty())
      Last = X;
    X->Next = First;
    First = X;
    Size++;
  }

  void pop_front() {
    DCHECK(!empty());
    First = First->Next;
    if (!First)
      Last = nullptr;
    Size--;
  }

  // Insert X next to Prev
  void insert(T *Prev, T *X) {
    DCHECK(!empty());
    DCHECK_NE(Prev, nullptr);
    DCHECK_NE(X, nullptr);
    X->Next = Prev->Next;
    Prev->Next = X;
    if (Last == Prev)
      Last = X;
    ++Size;
  }

  void extract(T *Prev, T *X) {
    DCHECK(!empty());
    DCHECK_NE(Prev, nullptr);
    DCHECK_NE(X, nullptr);
    DCHECK_EQ(Prev->Next, X);
    Prev->Next = X->Next;
    if (Last == X)
      Last = Prev;
    Size--;
  }

  void append_back(SinglyLinkedList<T> *L) {
    DCHECK_NE(this, L);
    if (L->empty())
      return;
    if (empty()) {
      *this = *L;
    } else {
      Last->Next = L->First;
      Last = L->Last;
      Size += L->size();
    }
    L->clear();
  }
};

template <class T> struct DoublyLinkedList : IntrusiveList<T> {
  using IntrusiveList<T>::First;
  using IntrusiveList<T>::Last;
  using IntrusiveList<T>::Size;
  using IntrusiveList<T>::empty;

  void push_front(T *X) {
    X->Prev = nullptr;
    if (empty()) {
      Last = X;
    } else {
      DCHECK_EQ(First->Prev, nullptr);
      First->Prev = X;
    }
    X->Next = First;
    First = X;
    Size++;
  }

  // Inserts X before Y.
  void insert(T *X, T *Y) {
    if (Y == First)
      return push_front(X);
    T *Prev = Y->Prev;
    // This is a hard CHECK to ensure consistency in the event of an intentional
    // corruption of Y->Prev, to prevent a potential write-{4,8}.
    CHECK_EQ(Prev->Next, Y);
    Prev->Next = X;
    X->Prev = Prev;
    X->Next = Y;
    Y->Prev = X;
    Size++;
  }

  void push_back(T *X) {
    X->Next = nullptr;
    if (empty()) {
      First = X;
    } else {
      DCHECK_EQ(Last->Next, nullptr);
      Last->Next = X;
    }
    X->Prev = Last;
    Last = X;
    Size++;
  }

  void pop_front() {
    DCHECK(!empty());
    First = First->Next;
    if (!First)
      Last = nullptr;
    else
      First->Prev = nullptr;
    Size--;
  }

  // The consistency of the adjacent links is aggressively checked in order to
  // catch potential corruption attempts, that could yield a mirrored
  // write-{4,8} primitive. nullptr checks are deemed less vital.
  void remove(T *X) {
    T *Prev = X->Prev;
    T *Next = X->Next;
    if (Prev) {
      CHECK_EQ(Prev->Next, X);
      Prev->Next = Next;
    }
    if (Next) {
      CHECK_EQ(Next->Prev, X);
      Next->Prev = Prev;
    }
    if (First == X) {
      DCHECK_EQ(Prev, nullptr);
      First = Next;
    } else {
      DCHECK_NE(Prev, nullptr);
    }
    if (Last == X) {
      DCHECK_EQ(Next, nullptr);
      Last = Prev;
    } else {
      DCHECK_NE(Next, nullptr);
    }
    Size--;
  }
};

} // namespace scudo

#endif // SCUDO_LIST_H_
