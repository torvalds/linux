//===- TypeSymbolEmitter.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPESYMBOLEMITTER_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPESYMBOLEMITTER_H

namespace llvm {
class StringRef;

namespace codeview {
class TypeIndex;

class TypeSymbolEmitter {
private:
  TypeSymbolEmitter(const TypeSymbolEmitter &) = delete;
  TypeSymbolEmitter &operator=(const TypeSymbolEmitter &) = delete;

protected:
  TypeSymbolEmitter() {}

public:
  virtual ~TypeSymbolEmitter() {}

public:
  virtual void writeUserDefinedType(TypeIndex TI, StringRef Name) = 0;
};
}
}

#endif
