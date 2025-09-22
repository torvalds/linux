//===-- llvm-dis-fuzzer.cpp - Fuzzer for llvm-dis using lib/Fuzzer --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Fuzzer for LLVM bitcode reading.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  LLVMContext Context;
  auto Buffer = MemoryBuffer::getMemBuffer(
      StringRef(reinterpret_cast<const char *>(Data), Size), "Fuzzer input",
      /*RequiresNullTerminator=*/false);
  consumeError(
      parseBitcodeFile(Buffer->getMemBufferRef(), Context).takeError());
  return 0;
}
