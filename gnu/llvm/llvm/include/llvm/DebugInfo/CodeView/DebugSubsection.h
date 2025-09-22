//===- DebugSubsection.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTION_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Error.h"

#include <cstdint>

namespace llvm {
class BinaryStreamWriter;
namespace codeview {

class DebugSubsectionRef {
public:
  explicit DebugSubsectionRef(DebugSubsectionKind Kind) : Kind(Kind) {}
  virtual ~DebugSubsectionRef();

  static bool classof(const DebugSubsectionRef *S) { return true; }

  DebugSubsectionKind kind() const { return Kind; }

protected:
  DebugSubsectionKind Kind;
};

class DebugSubsection {
public:
  explicit DebugSubsection(DebugSubsectionKind Kind) : Kind(Kind) {}
  virtual ~DebugSubsection();

  static bool classof(const DebugSubsection *S) { return true; }

  DebugSubsectionKind kind() const { return Kind; }

  virtual Error commit(BinaryStreamWriter &Writer) const = 0;
  virtual uint32_t calculateSerializedSize() const = 0;

protected:
  DebugSubsectionKind Kind;
};

} // namespace codeview
} // namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTION_H
