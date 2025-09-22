//===- Pragma.h - Pragma registration and handling --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PragmaHandler and PragmaTable interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_PRAGMA_H
#define LLVM_CLANG_LEX_PRAGMA_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace clang {

class PragmaNamespace;
class Preprocessor;
class Token;

  /**
   * Describes how the pragma was introduced, e.g., with \#pragma,
   * _Pragma, or __pragma.
   */
  enum PragmaIntroducerKind {
    /**
     * The pragma was introduced via \#pragma.
     */
    PIK_HashPragma,

    /**
     * The pragma was introduced via the C99 _Pragma(string-literal).
     */
    PIK__Pragma,

    /**
     * The pragma was introduced via the Microsoft
     * __pragma(token-string).
     */
    PIK___pragma
  };

  /// Describes how and where the pragma was introduced.
  struct PragmaIntroducer {
    PragmaIntroducerKind Kind;
    SourceLocation Loc;
  };

/// PragmaHandler - Instances of this interface defined to handle the various
/// pragmas that the language front-end uses.  Each handler optionally has a
/// name (e.g. "pack") and the HandlePragma method is invoked when a pragma with
/// that identifier is found.  If a handler does not match any of the declared
/// pragmas the handler with a null identifier is invoked, if it exists.
///
/// Note that the PragmaNamespace class can be used to subdivide pragmas, e.g.
/// we treat "\#pragma STDC" and "\#pragma GCC" as namespaces that contain other
/// pragmas.
class PragmaHandler {
  std::string Name;

public:
  PragmaHandler() = default;
  explicit PragmaHandler(StringRef name) : Name(name) {}
  virtual ~PragmaHandler();

  StringRef getName() const { return Name; }
  virtual void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                            Token &FirstToken) = 0;

  /// getIfNamespace - If this is a namespace, return it.  This is equivalent to
  /// using a dynamic_cast, but doesn't require RTTI.
  virtual PragmaNamespace *getIfNamespace() { return nullptr; }
};

/// EmptyPragmaHandler - A pragma handler which takes no action, which can be
/// used to ignore particular pragmas.
class EmptyPragmaHandler : public PragmaHandler {
public:
  explicit EmptyPragmaHandler(StringRef Name = StringRef());

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;
};

/// PragmaNamespace - This PragmaHandler subdivides the namespace of pragmas,
/// allowing hierarchical pragmas to be defined.  Common examples of namespaces
/// are "\#pragma GCC", "\#pragma STDC", and "\#pragma omp", but any namespaces
/// may be (potentially recursively) defined.
class PragmaNamespace : public PragmaHandler {
  /// Handlers - This is a map of the handlers in this namespace with their name
  /// as key.
  llvm::StringMap<std::unique_ptr<PragmaHandler>> Handlers;

public:
  explicit PragmaNamespace(StringRef Name) : PragmaHandler(Name) {}

  /// FindHandler - Check to see if there is already a handler for the
  /// specified name.  If not, return the handler for the null name if it
  /// exists, otherwise return null.  If IgnoreNull is true (the default) then
  /// the null handler isn't returned on failure to match.
  PragmaHandler *FindHandler(StringRef Name,
                             bool IgnoreNull = true) const;

  /// AddPragma - Add a pragma to this namespace.
  void AddPragma(PragmaHandler *Handler);

  /// RemovePragmaHandler - Remove the given handler from the
  /// namespace.
  void RemovePragmaHandler(PragmaHandler *Handler);

  bool IsEmpty() const { return Handlers.empty(); }

  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &Tok) override;

  PragmaNamespace *getIfNamespace() override { return this; }
};

/// Destringize a \c _Pragma("") string according to C11 6.10.9.1:
/// "The string literal is destringized by deleting any encoding prefix,
/// deleting the leading and trailing double-quotes, replacing each escape
/// sequence \" by a double-quote, and replacing each escape sequence \\ by a
/// single backslash."
void prepare_PragmaString(SmallVectorImpl<char> &StrVal);

} // namespace clang

#endif // LLVM_CLANG_LEX_PRAGMA_H
