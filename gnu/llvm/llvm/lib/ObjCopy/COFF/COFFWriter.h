//===- COFFWriter.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_COFF_COFFWRITER_H
#define LLVM_LIB_OBJCOPY_COFF_COFFWRITER_H

#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <utility>

namespace llvm {
namespace objcopy {
namespace coff {

struct Object;

class COFFWriter {
  Object &Obj;
  std::unique_ptr<WritableMemoryBuffer> Buf;
  raw_ostream &Out;

  size_t FileSize;
  size_t FileAlignment;
  size_t SizeOfInitializedData;
  StringTableBuilder StrTabBuilder;

  template <class SymbolTy> std::pair<size_t, size_t> finalizeSymbolTable();
  Error finalizeRelocTargets();
  Error finalizeSymbolContents();
  void layoutSections();
  Expected<size_t> finalizeStringTable();

  Error finalize(bool IsBigObj);

  void writeHeaders(bool IsBigObj);
  void writeSections();
  template <class SymbolTy> void writeSymbolStringTables();

  Error write(bool IsBigObj);

  Error patchDebugDirectory();
  Expected<uint32_t> virtualAddressToFileAddress(uint32_t RVA);

public:
  virtual ~COFFWriter() {}
  Error write();

  COFFWriter(Object &Obj, raw_ostream &Out)
      : Obj(Obj), Out(Out), StrTabBuilder(StringTableBuilder::WinCOFF) {}
};

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_COFF_COFFWRITER_H
