//===- XCOFFWriter.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_XCOFF_XCOFFWRITER_H
#define LLVM_LIB_OBJCOPY_XCOFF_XCOFFWRITER_H

#include "llvm/Support/MemoryBuffer.h"
#include "XCOFFObject.h"

#include <cstdint>

namespace llvm {
namespace objcopy {
namespace xcoff {

class XCOFFWriter {
public:
  virtual ~XCOFFWriter() {}
  XCOFFWriter(Object &Obj, raw_ostream &Out) : Obj(Obj), Out(Out) {}
  Error write();

private:
  Object &Obj;
  raw_ostream &Out;
  std::unique_ptr<WritableMemoryBuffer> Buf;
  size_t FileSize;

  void finalizeHeaders();
  void finalizeSections();
  void finalizeSymbolStringTable();
  void finalize();

  void writeHeaders();
  void writeSections();
  void writeSymbolStringTable();
};

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_XCOFF_XCOFFWRITER_H
