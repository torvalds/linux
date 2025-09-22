//===- DeclLookups.h - Low-level interface to all names in a DC -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines DeclContext::all_lookups_iterator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLLOOKUPS_H
#define LLVM_CLANG_AST_DECLLOOKUPS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExternalASTSource.h"
#include <cstddef>
#include <iterator>

namespace clang {

/// all_lookups_iterator - An iterator that provides a view over the results
/// of looking up every possible name.
class DeclContext::all_lookups_iterator {
  StoredDeclsMap::iterator It, End;

public:
  using value_type = lookup_result;
  using reference = lookup_result;
  using pointer = lookup_result;
  using iterator_category = std::forward_iterator_tag;
  using difference_type = std::ptrdiff_t;

  all_lookups_iterator() = default;
  all_lookups_iterator(StoredDeclsMap::iterator It,
                       StoredDeclsMap::iterator End)
      : It(It), End(End) {}

  DeclarationName getLookupName() const { return It->first; }

  reference operator*() const { return It->second.getLookupResult(); }
  pointer operator->() const { return It->second.getLookupResult(); }

  all_lookups_iterator& operator++() {
    // Filter out using directives. They don't belong as results from name
    // lookup anyways, except as an implementation detail. Users of the API
    // should not expect to get them (or worse, rely on it).
    do {
      ++It;
    } while (It != End &&
             It->first == DeclarationName::getUsingDirectiveName());

    return *this;
  }

  all_lookups_iterator operator++(int) {
    all_lookups_iterator tmp(*this);
    ++(*this);
    return tmp;
  }

  friend bool operator==(all_lookups_iterator x, all_lookups_iterator y) {
    return x.It == y.It;
  }

  friend bool operator!=(all_lookups_iterator x, all_lookups_iterator y) {
    return x.It != y.It;
  }
};

inline DeclContext::lookups_range DeclContext::lookups() const {
  DeclContext *Primary = const_cast<DeclContext*>(this)->getPrimaryContext();
  if (Primary->hasExternalVisibleStorage())
    getParentASTContext().getExternalSource()->completeVisibleDeclsMap(Primary);
  if (StoredDeclsMap *Map = Primary->buildLookup())
    return lookups_range(all_lookups_iterator(Map->begin(), Map->end()),
                         all_lookups_iterator(Map->end(), Map->end()));

  // Synthesize an empty range. This requires that two default constructed
  // versions of these iterators form a valid empty range.
  return lookups_range(all_lookups_iterator(), all_lookups_iterator());
}

inline DeclContext::lookups_range
DeclContext::noload_lookups(bool PreserveInternalState) const {
  DeclContext *Primary = const_cast<DeclContext*>(this)->getPrimaryContext();
  if (!PreserveInternalState)
    Primary->loadLazyLocalLexicalLookups();
  if (StoredDeclsMap *Map = Primary->getLookupPtr())
    return lookups_range(all_lookups_iterator(Map->begin(), Map->end()),
                         all_lookups_iterator(Map->end(), Map->end()));

  // Synthesize an empty range. This requires that two default constructed
  // versions of these iterators form a valid empty range.
  return lookups_range(all_lookups_iterator(), all_lookups_iterator());
}

} // namespace clang

#endif // LLVM_CLANG_AST_DECLLOOKUPS_H
