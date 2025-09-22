//===- IdentifierResolver.h - Lexical Scope Name lookup ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the IdentifierResolver class, which is used for lexical
// scoped lookup, based on declaration names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_IDENTIFIERRESOLVER_H
#define LLVM_CLANG_SEMA_IDENTIFIERRESOLVER_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace clang {

class Decl;
class DeclarationName;
class DeclContext;
class IdentifierInfo;
class LangOptions;
class NamedDecl;
class Preprocessor;
class Scope;

/// IdentifierResolver - Keeps track of shadowed decls on enclosing
/// scopes.  It manages the shadowing chains of declaration names and
/// implements efficient decl lookup based on a declaration name.
class IdentifierResolver {
  /// IdDeclInfo - Keeps track of information about decls associated
  /// to a particular declaration name. IdDeclInfos are lazily
  /// constructed and assigned to a declaration name the first time a
  /// decl with that declaration name is shadowed in some scope.
  class IdDeclInfo {
  public:
    using DeclsTy = SmallVector<NamedDecl *, 2>;

    DeclsTy::iterator decls_begin() { return Decls.begin(); }
    DeclsTy::iterator decls_end() { return Decls.end(); }

    void AddDecl(NamedDecl *D) { Decls.push_back(D); }

    /// RemoveDecl - Remove the decl from the scope chain.
    /// The decl must already be part of the decl chain.
    void RemoveDecl(NamedDecl *D);

    /// Insert the given declaration at the given position in the list.
    void InsertDecl(DeclsTy::iterator Pos, NamedDecl *D) {
      Decls.insert(Pos, D);
    }

  private:
    DeclsTy Decls;
  };

public:
  /// iterator - Iterate over the decls of a specified declaration name.
  /// It will walk or not the parent declaration contexts depending on how
  /// it was instantiated.
  class iterator {
  public:
    friend class IdentifierResolver;

    using value_type = NamedDecl *;
    using reference = NamedDecl *;
    using pointer = NamedDecl *;
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;

    /// Ptr - There are 2 forms that 'Ptr' represents:
    /// 1) A single NamedDecl. (Ptr & 0x1 == 0)
    /// 2) A IdDeclInfo::DeclsTy::iterator that traverses only the decls of the
    ///    same declaration context. (Ptr & 0x1 == 0x1)
    uintptr_t Ptr = 0;
    using BaseIter = IdDeclInfo::DeclsTy::iterator;

    /// A single NamedDecl. (Ptr & 0x1 == 0)
    iterator(NamedDecl *D) {
      Ptr = reinterpret_cast<uintptr_t>(D);
      assert((Ptr & 0x1) == 0 && "Invalid Ptr!");
    }

    /// A IdDeclInfo::DeclsTy::iterator that walks or not the parent declaration
    /// contexts depending on 'LookInParentCtx'.
    iterator(BaseIter I) {
      Ptr = reinterpret_cast<uintptr_t>(I) | 0x1;
    }

    bool isIterator() const { return (Ptr & 0x1); }

    BaseIter getIterator() const {
      assert(isIterator() && "Ptr not an iterator!");
      return reinterpret_cast<BaseIter>(Ptr & ~0x1);
    }

    void incrementSlowCase();

  public:
    iterator() = default;

    NamedDecl *operator*() const {
      if (isIterator())
        return *getIterator();
      else
        return reinterpret_cast<NamedDecl*>(Ptr);
    }

    bool operator==(const iterator &RHS) const {
      return Ptr == RHS.Ptr;
    }
    bool operator!=(const iterator &RHS) const {
      return Ptr != RHS.Ptr;
    }

    // Preincrement.
    iterator& operator++() {
      if (!isIterator()) // common case.
        Ptr = 0;
      else
        incrementSlowCase();
      return *this;
    }
  };

  explicit IdentifierResolver(Preprocessor &PP);
  ~IdentifierResolver();

  IdentifierResolver(const IdentifierResolver &) = delete;
  IdentifierResolver &operator=(const IdentifierResolver &) = delete;

  /// Returns a range of decls with the name 'Name'.
  llvm::iterator_range<iterator> decls(DeclarationName Name);

  /// Returns an iterator over decls with the name 'Name'.
  iterator begin(DeclarationName Name);

  /// Returns the end iterator.
  iterator end() { return iterator(); }

  /// isDeclInScope - If 'Ctx' is a function/method, isDeclInScope returns true
  /// if 'D' is in Scope 'S', otherwise 'S' is ignored and isDeclInScope returns
  /// true if 'D' belongs to the given declaration context.
  ///
  /// \param AllowInlineNamespace If \c true, we are checking whether a prior
  ///        declaration is in scope in a declaration that requires a prior
  ///        declaration (because it is either explicitly qualified or is a
  ///        template instantiation or specialization). In this case, a
  ///        declaration is in scope if it's in the inline namespace set of the
  ///        context.
  bool isDeclInScope(Decl *D, DeclContext *Ctx, Scope *S = nullptr,
                     bool AllowInlineNamespace = false) const;

  /// AddDecl - Link the decl to its shadowed decl chain.
  void AddDecl(NamedDecl *D);

  /// RemoveDecl - Unlink the decl from its shadowed decl chain.
  /// The decl must already be part of the decl chain.
  void RemoveDecl(NamedDecl *D);

  /// Insert the given declaration after the given iterator
  /// position.
  void InsertDeclAfter(iterator Pos, NamedDecl *D);

  /// Try to add the given declaration to the top level scope, if it
  /// (or a redeclaration of it) hasn't already been added.
  ///
  /// \param D The externally-produced declaration to add.
  ///
  /// \param Name The name of the externally-produced declaration.
  ///
  /// \returns true if the declaration was added, false otherwise.
  bool tryAddTopLevelDecl(NamedDecl *D, DeclarationName Name);

private:
  const LangOptions &LangOpt;
  Preprocessor &PP;

  class IdDeclInfoMap;
  IdDeclInfoMap *IdDeclInfos;

  void updatingIdentifier(IdentifierInfo &II);
  void readingIdentifier(IdentifierInfo &II);

  /// FETokenInfo contains a Decl pointer if lower bit == 0.
  static inline bool isDeclPtr(void *Ptr) {
    return (reinterpret_cast<uintptr_t>(Ptr) & 0x1) == 0;
  }

  /// FETokenInfo contains a IdDeclInfo pointer if lower bit == 1.
  static inline IdDeclInfo *toIdDeclInfo(void *Ptr) {
    assert((reinterpret_cast<uintptr_t>(Ptr) & 0x1) == 1
          && "Ptr not a IdDeclInfo* !");
    return reinterpret_cast<IdDeclInfo*>(
                    reinterpret_cast<uintptr_t>(Ptr) & ~0x1);
  }
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_IDENTIFIERRESOLVER_H
