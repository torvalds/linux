//===- IPDBLineNumber.h - base interface for PDB line no. info ---*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBLINENUMBER_H
#define LLVM_DEBUGINFO_PDB_IPDBLINENUMBER_H

#include <cstdint>

namespace llvm {
namespace pdb {
class IPDBLineNumber {
public:
  virtual ~IPDBLineNumber();

  virtual uint32_t getLineNumber() const = 0;
  virtual uint32_t getLineNumberEnd() const = 0;
  virtual uint32_t getColumnNumber() const = 0;
  virtual uint32_t getColumnNumberEnd() const = 0;
  virtual uint32_t getAddressSection() const = 0;
  virtual uint32_t getAddressOffset() const = 0;
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  virtual uint64_t getVirtualAddress() const = 0;
  virtual uint32_t getLength() const = 0;
  virtual uint32_t getSourceFileId() const = 0;
  virtual uint32_t getCompilandId() const = 0;
  virtual bool isStatement() const = 0;
};
}
}

#endif
