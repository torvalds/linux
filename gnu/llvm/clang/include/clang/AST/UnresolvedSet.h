//===- UnresolvedSet.h - Unresolved sets of declarations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the UnresolvedSet class, which is used to store
//  collections of declarations in the AST.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_UNRESOLVEDSET_H
#define LLVM_CLANG_AST_UNRESOLVEDSET_H

#include "clang/AST/DeclAccessPair.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include <cstddef>
#include <iterator>

namespace clang {

class NamedDecl;

/// The iterator over UnresolvedSets.  Serves as both the const and
/// non-const iterator.
class UnresolvedSetIterator : public llvm::iterator_adaptor_base<
                                  UnresolvedSetIterator, DeclAccessPair *,
                                  std::random_access_iterator_tag, NamedDecl *,
                                  std::ptrdiff_t, NamedDecl *, NamedDecl *> {
  friend class ASTUnresolvedSet;
  friend class OverloadExpr;
  friend class UnresolvedSetImpl;

  explicit UnresolvedSetIterator(DeclAccessPair *Iter)
      : iterator_adaptor_base(Iter) {}
  explicit UnresolvedSetIterator(const DeclAccessPair *Iter)
      : iterator_adaptor_base(const_cast<DeclAccessPair *>(Iter)) {}

public:
  // Work around a bug in MSVC 2013 where explicitly default constructed
  // temporaries with defaulted ctors are not zero initialized.
  UnresolvedSetIterator() : iterator_adaptor_base(nullptr) {}

  uint64_t getDeclID() const { return I->getDeclID(); }
  NamedDecl *getDecl() const { return I->getDecl(); }
  void setDecl(NamedDecl *ND) const { return I->setDecl(ND); }
  AccessSpecifier getAccess() const { return I->getAccess(); }
  void setAccess(AccessSpecifier AS) { I->setAccess(AS); }
  const DeclAccessPair &getPair() const { return *I; }

  NamedDecl *operator*() const { return getDecl(); }
  NamedDecl *operator->() const { return **this; }
};

/// A set of unresolved declarations.
class UnresolvedSetImpl {
  using DeclsTy = SmallVectorImpl<DeclAccessPair>;

  // Don't allow direct construction, and only permit subclassing by
  // UnresolvedSet.
private:
  template <unsigned N> friend class UnresolvedSet;

  UnresolvedSetImpl() = default;
  UnresolvedSetImpl(const UnresolvedSetImpl &) = default;
  UnresolvedSetImpl &operator=(const UnresolvedSetImpl &) = default;

  // FIXME: Switch these to "= default" once MSVC supports generating move ops
  UnresolvedSetImpl(UnresolvedSetImpl &&) {}
  UnresolvedSetImpl &operator=(UnresolvedSetImpl &&) { return *this; }

public:
  // We don't currently support assignment through this iterator, so we might
  // as well use the same implementation twice.
  using iterator = UnresolvedSetIterator;
  using const_iterator = UnresolvedSetIterator;

  iterator begin() { return iterator(decls().begin()); }
  iterator end() { return iterator(decls().end()); }

  const_iterator begin() const { return const_iterator(decls().begin()); }
  const_iterator end() const { return const_iterator(decls().end()); }

  ArrayRef<DeclAccessPair> pairs() const { return decls(); }

  void addDecl(NamedDecl *D) {
    addDecl(D, AS_none);
  }

  void addDecl(NamedDecl *D, AccessSpecifier AS) {
    decls().push_back(DeclAccessPair::make(D, AS));
  }

  /// Replaces the given declaration with the new one, once.
  ///
  /// \return true if the set changed
  bool replace(const NamedDecl* Old, NamedDecl *New) {
    for (DeclsTy::iterator I = decls().begin(), E = decls().end(); I != E; ++I)
      if (I->getDecl() == Old)
        return (I->setDecl(New), true);
    return false;
  }

  /// Replaces the declaration at the given iterator with the new one,
  /// preserving the original access bits.
  void replace(iterator I, NamedDecl *New) { I.I->setDecl(New); }

  void replace(iterator I, NamedDecl *New, AccessSpecifier AS) {
    I.I->set(New, AS);
  }

  void erase(unsigned I) {
    auto val = decls().pop_back_val();
    if (I < size())
      decls()[I] = val;
  }

  void erase(iterator I) {
    auto val = decls().pop_back_val();
    if (I != end())
      *I.I = val;
  }

  void setAccess(iterator I, AccessSpecifier AS) { I.I->setAccess(AS); }

  void clear() { decls().clear(); }
  void truncate(unsigned N) { decls().truncate(N); }

  bool empty() const { return decls().empty(); }
  unsigned size() const { return decls().size(); }

  void append(iterator I, iterator E) { decls().append(I.I, E.I); }

  template<typename Iter> void assign(Iter I, Iter E) { decls().assign(I, E); }

  DeclAccessPair &operator[](unsigned I) { return decls()[I]; }
  const DeclAccessPair &operator[](unsigned I) const { return decls()[I]; }

private:
  // These work because the only permitted subclass is UnresolvedSetImpl

  DeclsTy &decls() {
    return *reinterpret_cast<DeclsTy*>(this);
  }
  const DeclsTy &decls() const {
    return *reinterpret_cast<const DeclsTy*>(this);
  }
};

/// A set of unresolved declarations.
template <unsigned InlineCapacity> class UnresolvedSet :
    public UnresolvedSetImpl {
  SmallVector<DeclAccessPair, InlineCapacity> Decls;
};


} // namespace clang

#endif // LLVM_CLANG_AST_UNRESOLVEDSET_H
