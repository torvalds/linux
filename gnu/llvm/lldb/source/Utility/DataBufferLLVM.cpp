//===-- DataBufferLLVM.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/DataBufferLLVM.h"

#include "llvm/Support/MemoryBuffer.h"

#include <cassert>

using namespace lldb_private;

DataBufferLLVM::DataBufferLLVM(std::unique_ptr<llvm::MemoryBuffer> MemBuffer)
    : Buffer(std::move(MemBuffer)) {
  assert(Buffer != nullptr &&
         "Cannot construct a DataBufferLLVM with a null buffer");
}

DataBufferLLVM::~DataBufferLLVM() = default;

const uint8_t *DataBufferLLVM::GetBytesImpl() const {
  return reinterpret_cast<const uint8_t *>(Buffer->getBufferStart());
}

lldb::offset_t DataBufferLLVM::GetByteSize() const {
  return Buffer->getBufferSize();
}

WritableDataBufferLLVM::WritableDataBufferLLVM(
    std::unique_ptr<llvm::WritableMemoryBuffer> MemBuffer)
    : Buffer(std::move(MemBuffer)) {
  assert(Buffer != nullptr &&
         "Cannot construct a WritableDataBufferLLVM with a null buffer");
}

WritableDataBufferLLVM::~WritableDataBufferLLVM() = default;

const uint8_t *WritableDataBufferLLVM::GetBytesImpl() const {
  return reinterpret_cast<const uint8_t *>(Buffer->getBufferStart());
}

lldb::offset_t WritableDataBufferLLVM::GetByteSize() const {
  return Buffer->getBufferSize();
}

char DataBufferLLVM::ID;
char WritableDataBufferLLVM::ID;
