//===-- ResourceScriptToken.h -----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This declares the .rc script tokens.
// The list of available tokens is located at ResourceScriptTokenList.h.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380599(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_SCRIPTTOKEN_H
#define LLVM_INCLUDE_LLVM_SUPPORT_WINDOWS_RESOURCE_SCRIPTTOKEN_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

// A definition of a single resource script token. Each token has its kind
// (declared in ResourceScriptTokenList) and holds a value - a reference
// representation of the token.
// RCToken does not claim ownership on its value. A memory buffer containing
// the token value should be stored in a safe place and cannot be freed
// nor reallocated.
class RCToken {
public:
  enum class Kind {
#define TOKEN(Name) Name,
#define SHORT_TOKEN(Name, Ch) Name,
#include "ResourceScriptTokenList.h"
#undef TOKEN
#undef SHORT_TOKEN
  };

  RCToken(RCToken::Kind RCTokenKind, StringRef Value);

  // Get an integer value of the integer token.
  uint32_t intValue() const;
  bool isLongInt() const;

  StringRef value() const;
  Kind kind() const;

  // Check if a token describes a binary operator.
  bool isBinaryOp() const;

private:
  Kind TokenKind;
  StringRef TokenValue;
};

} // namespace llvm

#endif
