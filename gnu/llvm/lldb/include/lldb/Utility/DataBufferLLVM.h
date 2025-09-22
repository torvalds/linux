//===--- DataBufferLLVM.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_DATABUFFERLLVM_H
#define LLDB_UTILITY_DATABUFFERLLVM_H

#include "lldb/Utility/DataBuffer.h"
#include "lldb/lldb-types.h"

#include <cstdint>
#include <memory>

namespace llvm {
class WritableMemoryBuffer;
class MemoryBuffer;
class Twine;
} // namespace llvm

namespace lldb_private {
class FileSystem;

class DataBufferLLVM : public DataBuffer {
public:
  ~DataBufferLLVM() override;

  const uint8_t *GetBytesImpl() const override;
  lldb::offset_t GetByteSize() const override;

  /// LLVM RTTI support.
  /// {
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || DataBuffer::isA(ClassID);
  }
  static bool classof(const DataBuffer *data_buffer) {
    return data_buffer->isA(&ID);
  }
  /// }

  /// Construct a DataBufferLLVM from \p Buffer.  \p Buffer must be a valid
  /// pointer.
  explicit DataBufferLLVM(std::unique_ptr<llvm::MemoryBuffer> Buffer);

protected:
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
};

class WritableDataBufferLLVM : public WritableDataBuffer {
public:
  ~WritableDataBufferLLVM() override;

  const uint8_t *GetBytesImpl() const override;
  lldb::offset_t GetByteSize() const override;

  /// LLVM RTTI support.
  /// {
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || WritableDataBuffer::isA(ClassID);
  }
  static bool classof(const DataBuffer *data_buffer) {
    return data_buffer->isA(&ID);
  }
  /// }

  /// Construct a DataBufferLLVM from \p Buffer.  \p Buffer must be a valid
  /// pointer.
  explicit WritableDataBufferLLVM(
      std::unique_ptr<llvm::WritableMemoryBuffer> Buffer);

protected:
  std::unique_ptr<llvm::WritableMemoryBuffer> Buffer;
};
} // namespace lldb_private

#endif
