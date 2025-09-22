//===- MemoryBufferRef.h - Memory Buffer Reference --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the MemoryBuffer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORYBUFFERREF_H
#define LLVM_SUPPORT_MEMORYBUFFERREF_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

class MemoryBuffer;

class MemoryBufferRef {
  StringRef Buffer;
  StringRef Identifier;

public:
  MemoryBufferRef() = default;
  MemoryBufferRef(const MemoryBuffer &Buffer);
  MemoryBufferRef(StringRef Buffer, StringRef Identifier)
      : Buffer(Buffer), Identifier(Identifier) {}

  StringRef getBuffer() const { return Buffer; }
  StringRef getBufferIdentifier() const { return Identifier; }

  const char *getBufferStart() const { return Buffer.begin(); }
  const char *getBufferEnd() const { return Buffer.end(); }
  size_t getBufferSize() const { return Buffer.size(); }

  /// Check pointer identity (not value) of identifier and data.
  friend bool operator==(const MemoryBufferRef &LHS,
                         const MemoryBufferRef &RHS) {
    return LHS.Buffer.begin() == RHS.Buffer.begin() &&
           LHS.Buffer.end() == RHS.Buffer.end() &&
           LHS.Identifier.begin() == RHS.Identifier.begin() &&
           LHS.Identifier.end() == RHS.Identifier.end();
  }

  friend bool operator!=(const MemoryBufferRef &LHS,
                         const MemoryBufferRef &RHS) {
    return !(LHS == RHS);
  }
};

} // namespace llvm

#endif // LLVM_SUPPORT_MEMORYBUFFERREF_H
