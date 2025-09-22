//===-- llvm-dwarfdump-fuzzer.cpp - Fuzz the llvm-dwarfdump tool ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a function that runs llvm-dwarfdump
///  on a single input. This function is then linked into the Fuzzer library.
///
//===----------------------------------------------------------------------===//
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;
using namespace object;

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  std::string Payload(reinterpret_cast<const char *>(data), size);
  std::unique_ptr<MemoryBuffer> Buff = MemoryBuffer::getMemBuffer(Payload);

  Expected<std::unique_ptr<ObjectFile>> ObjOrErr =
      ObjectFile::createObjectFile(Buff->getMemBufferRef());
  if (auto E = ObjOrErr.takeError()) {
    consumeError(std::move(E));
    return 0;
  }
  ObjectFile &Obj = *ObjOrErr.get();
  std::unique_ptr<DIContext> DICtx = DWARFContext::create(Obj);


  DIDumpOptions opts;
  opts.DumpType = DIDT_All;
  DICtx->dump(nulls(), opts);
  return 0;
}
