//===- XCOFFReader.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_XCOFF_XCOFFREADER_H
#define LLVM_LIB_OBJCOPY_XCOFF_XCOFFREADER_H

#include "XCOFFObject.h"

namespace llvm {
namespace objcopy {
namespace xcoff {

using namespace object;

class XCOFFReader {
public:
  explicit XCOFFReader(const XCOFFObjectFile &O) : XCOFFObj(O) {}
  Expected<std::unique_ptr<Object>> create() const;

private:
  const XCOFFObjectFile &XCOFFObj;
  Error readSections(Object &Obj) const;
  Error readSymbols(Object &Obj) const;
};

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_XCOFF_XCOFFREADER_H
