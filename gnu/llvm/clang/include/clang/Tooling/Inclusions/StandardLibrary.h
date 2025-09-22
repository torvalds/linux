//===--- StandardLibrary.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provides an interface for querying information about C and C++ Standard
/// Library headers and symbols.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_INCLUSIONS_STANDARDLIBRARY_H
#define LLVM_CLANG_TOOLING_INCLUSIONS_STANDARDLIBRARY_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <string>

namespace clang {
class Decl;
class NamespaceDecl;
class DeclContext;
namespace tooling {
namespace stdlib {

class Symbol;
enum class Lang { C = 0, CXX, LastValue = CXX };

// A standard library header, such as <iostream>
// Lightweight class, in fact just an index into a table.
// C++ and C Library compatibility headers are considered different: e.g.
// "<cstdio>" and "<stdio.h>" (and their symbols) are treated differently.
class Header {
public:
  static std::vector<Header> all(Lang L = Lang::CXX);
  // Name should contain the angle brackets, e.g. "<vector>".
  static std::optional<Header> named(llvm::StringRef Name,
                                     Lang Language = Lang::CXX);

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Header &H) {
    return OS << H.name();
  }
  llvm::StringRef name() const;

private:
  Header(unsigned ID, Lang Language) : ID(ID), Language(Language) {}
  unsigned ID;
  Lang Language;

  friend Symbol;
  friend llvm::DenseMapInfo<Header>;
  friend bool operator==(const Header &L, const Header &R) {
    return L.ID == R.ID;
  }
};

// A top-level standard library symbol, such as std::vector
// Lightweight class, in fact just an index into a table.
// C++ and C Standard Library symbols are considered distinct: e.g. std::printf
// and ::printf are not treated as the same symbol.
// The symbols do not contain macros right now, we don't have a reliable index
// for them.
class Symbol {
public:
  static std::vector<Symbol> all(Lang L = Lang::CXX);
  /// \p Scope should have the trailing "::", for example:
  /// named("std::chrono::", "system_clock")
  static std::optional<Symbol>
  named(llvm::StringRef Scope, llvm::StringRef Name, Lang Language = Lang::CXX);

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Symbol &S) {
    return OS << S.qualifiedName();
  }
  llvm::StringRef scope() const;
  llvm::StringRef name() const;
  llvm::StringRef qualifiedName() const;
  // The preferred header for this symbol (e.g. the suggested insertion).
  std::optional<Header> header() const;
  // Some symbols may be provided by multiple headers.
  llvm::SmallVector<Header> headers() const;

private:
  Symbol(unsigned ID, Lang Language) : ID(ID), Language(Language) {}
  unsigned ID;
  Lang Language;

  friend class Recognizer;
  friend llvm::DenseMapInfo<Symbol>;
  friend bool operator==(const Symbol &L, const Symbol &R) {
    return L.ID == R.ID;
  }
};

// A functor to find the stdlib::Symbol associated with a decl.
//
// For non-top-level decls (std::vector<int>::iterator), returns the top-level
// symbol (std::vector).
class Recognizer {
public:
  Recognizer();
  std::optional<Symbol> operator()(const Decl *D);

private:
  using NSSymbolMap = llvm::DenseMap<llvm::StringRef, unsigned>;
  NSSymbolMap *namespaceSymbols(const DeclContext *DC, Lang L);
  llvm::DenseMap<const DeclContext *, NSSymbolMap *> NamespaceCache;
};

} // namespace stdlib
} // namespace tooling
} // namespace clang

namespace llvm {

template <> struct DenseMapInfo<clang::tooling::stdlib::Header> {
  static inline clang::tooling::stdlib::Header getEmptyKey() {
    return clang::tooling::stdlib::Header(-1,
                                          clang::tooling::stdlib::Lang::CXX);
  }
  static inline clang::tooling::stdlib::Header getTombstoneKey() {
    return clang::tooling::stdlib::Header(-2,
                                          clang::tooling::stdlib::Lang::CXX);
  }
  static unsigned getHashValue(const clang::tooling::stdlib::Header &H) {
    return hash_value(H.ID);
  }
  static bool isEqual(const clang::tooling::stdlib::Header &LHS,
                      const clang::tooling::stdlib::Header &RHS) {
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<clang::tooling::stdlib::Symbol> {
  static inline clang::tooling::stdlib::Symbol getEmptyKey() {
    return clang::tooling::stdlib::Symbol(-1,
                                          clang::tooling::stdlib::Lang::CXX);
  }
  static inline clang::tooling::stdlib::Symbol getTombstoneKey() {
    return clang::tooling::stdlib::Symbol(-2,
                                          clang::tooling::stdlib::Lang::CXX);
  }
  static unsigned getHashValue(const clang::tooling::stdlib::Symbol &S) {
    return hash_value(S.ID);
  }
  static bool isEqual(const clang::tooling::stdlib::Symbol &LHS,
                      const clang::tooling::stdlib::Symbol &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif // LLVM_CLANG_TOOLING_INCLUSIONS_STANDARDLIBRARY_H
