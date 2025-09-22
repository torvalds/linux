//===- SimpleTypeSerializer.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SIMPLETYPESERIALIZER_H
#define LLVM_DEBUGINFO_CODEVIEW_SIMPLETYPESERIALIZER_H

#include "llvm/ADT/ArrayRef.h"
#include <vector>

namespace llvm {
namespace codeview {
class FieldListRecord;

class SimpleTypeSerializer {
  std::vector<uint8_t> ScratchBuffer;

public:
  SimpleTypeSerializer();
  ~SimpleTypeSerializer();

  // This template is explicitly instantiated in the implementation file for all
  // supported types.  The method itself is ugly, so inlining it into the header
  // file clutters an otherwise straightforward interface.
  template <typename T> ArrayRef<uint8_t> serialize(T &Record);

  // Don't allow serialization of field list records using this interface.
  ArrayRef<uint8_t> serialize(const FieldListRecord &Record) = delete;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SIMPLETYPESERIALIZER_H
