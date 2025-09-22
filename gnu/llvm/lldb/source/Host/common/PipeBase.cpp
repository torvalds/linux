//===-- source/Host/common/PipeBase.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/PipeBase.h"

using namespace lldb_private;

PipeBase::~PipeBase() = default;

Status PipeBase::OpenAsWriter(llvm::StringRef name,
                              bool child_process_inherit) {
  return OpenAsWriterWithTimeout(name, child_process_inherit,
                                 std::chrono::microseconds::zero());
}

Status PipeBase::Read(void *buf, size_t size, size_t &bytes_read) {
  return ReadWithTimeout(buf, size, std::chrono::microseconds::zero(),
                         bytes_read);
}
