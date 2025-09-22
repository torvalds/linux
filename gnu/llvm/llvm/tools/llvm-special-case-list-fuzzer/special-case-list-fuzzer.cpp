//===--- special-case-list-fuzzer.cpp - Fuzzer for special case lists -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SpecialCaseList.h"

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::string Payload(reinterpret_cast<const char *>(Data), Size);
  std::unique_ptr<llvm::MemoryBuffer> Buf =
      llvm::MemoryBuffer::getMemBuffer(Payload);

  if (!Buf)
    return 0;

  std::string Error;
  llvm::SpecialCaseList::create(Buf.get(), Error);

  return 0;
}
