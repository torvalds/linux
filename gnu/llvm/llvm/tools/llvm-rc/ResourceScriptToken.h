//===-- ResourceScriptToken.h -----------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This declares the .rc script tokens and defines an interface for tokenizing
// the input data. The list of available tokens is located at
// ResourceScriptTokenList.def.
//
// Note that the tokenizer does not support preprocessor directives. The
// preprocessor should do its work on the .rc file before running llvm-rc.
//
// As for now, it is possible to parse ASCII files only (the behavior on
// UTF files might be undefined). However, it already consumes UTF-8 BOM, if
// there is any. Thus, ASCII-compatible UTF-8 files are tokenized correctly.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/aa380599(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMRC_RESOURCESCRIPTTOKEN_H
#define LLVM_TOOLS_LLVMRC_RESOURCESCRIPTTOKEN_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <map>
#include <vector>

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
#include "ResourceScriptTokenList.def"
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

// Tokenize Input.
// In case no error occurred, the return value contains
//   tokens in order they were in the input file.
// In case of any error, the return value contains
//   a textual representation of error.
//
// Tokens returned by this function hold only references to the parts
// of the Input. Memory buffer containing Input cannot be freed,
// modified or reallocated.
Expected<std::vector<RCToken>> tokenizeRC(StringRef Input);

} // namespace llvm

#endif
