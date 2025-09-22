//===- XCOFFObjcopy.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/XCOFF/XCOFFConfig.h"
#include "llvm/ObjCopy/XCOFF/XCOFFObjcopy.h"
#include "llvm/Support/Errc.h"
#include "XCOFFObject.h"
#include "XCOFFReader.h"
#include "XCOFFWriter.h"

namespace llvm {
namespace objcopy {
namespace xcoff {

using namespace object;

static Error handleArgs(const CommonConfig &Config, Object &Obj) {
  return Error::success();
}

Error executeObjcopyOnBinary(const CommonConfig &Config, const XCOFFConfig &,
                             XCOFFObjectFile &In, raw_ostream &Out) {
  XCOFFReader Reader(In);
  Expected<std::unique_ptr<Object>> ObjOrErr = Reader.create();
  if (!ObjOrErr)
    return createFileError(Config.InputFilename, ObjOrErr.takeError());
  Object *Obj = ObjOrErr->get();
  assert(Obj && "Unable to deserialize XCOFF object");
  if (Error E = handleArgs(Config, *Obj))
    return createFileError(Config.InputFilename, std::move(E));
  XCOFFWriter Writer(*Obj, Out);
  if (Error E = Writer.write())
    return createFileError(Config.OutputFilename, std::move(E));
  return Error::success();
}

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm
