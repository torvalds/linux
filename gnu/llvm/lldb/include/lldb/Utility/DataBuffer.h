//===-- DataBuffer.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_DATABUFFER_H
#define LLDB_UTILITY_DATABUFFER_H

#include <cstdint>
#include <cstring>

#include "lldb/lldb-types.h"

#include "llvm/ADT/ArrayRef.h"

namespace lldb_private {

/// \class DataBuffer DataBuffer.h "lldb/Core/DataBuffer.h"
/// A pure virtual protocol class for abstracted read only data buffers.
///
/// DataBuffer is an abstract class that gets packaged into a shared
/// pointer that can use to implement various ways to store data (on the heap,
/// memory mapped, cached inferior memory). It gets used by DataExtractor so
/// many DataExtractor objects can share the same data and sub-ranges of that
/// shared data, and the last object that contains a reference to the shared
/// data will free it.
///
/// Subclasses can implement as many different constructors or member
/// functions that allow data to be stored in the object's buffer prior to
/// handing the shared data to clients that use these buffers.
///
/// All subclasses must override all of the pure virtual functions as they are
/// used by clients to access the data. Having a common interface allows
/// different ways of storing data, yet using it in one common way.
///
/// This class currently expects all data to be available without any extra
/// calls being made, but we can modify it to optionally get data on demand
/// with some extra function calls to load the data before it gets accessed.
class DataBuffer {
public:
  virtual ~DataBuffer() = default;

  /// Get the number of bytes in the data buffer.
  ///
  /// \return
  ///     The number of bytes this object currently contains.
  virtual lldb::offset_t GetByteSize() const = 0;

  /// Get a const pointer to the data.
  ///
  /// \return
  ///     A const pointer to the bytes owned by this object, or NULL
  ///     if the object contains no bytes.
  const uint8_t *GetBytes() const { return GetBytesImpl(); }

  llvm::ArrayRef<uint8_t> GetData() const {
    return llvm::ArrayRef<uint8_t>(GetBytes(), GetByteSize());
  }

  /// LLVM RTTI support.
  /// {
  static char ID;
  virtual bool isA(const void *ClassID) const { return ClassID == &ID; }
  static bool classof(const DataBuffer *data_buffer) {
    return data_buffer->isA(&ID);
  }
  /// }

protected:
  /// Get a const pointer to the data.
  ///
  /// \return
  ///     A const pointer to the bytes owned by this object, or NULL
  ///     if the object contains no bytes.
  virtual const uint8_t *GetBytesImpl() const = 0;
};

/// \class DataBuffer DataBuffer.h "lldb/Core/DataBuffer.h"
/// A pure virtual protocol class for abstracted writable data buffers.
///
/// DataBuffer is an abstract class that gets packaged into a shared pointer
/// that can use to implement various ways to store data (on the heap, memory
/// mapped, cached inferior memory). It gets used by DataExtractor so many
/// DataExtractor objects can share the same data and sub-ranges of that
/// shared data, and the last object that contains a reference to the shared
/// data will free it.
class WritableDataBuffer : public DataBuffer {
public:
  /// Destructor
  ///
  /// The destructor is virtual as other classes will inherit from this class
  /// and be downcast to the DataBuffer pure virtual interface. The virtual
  /// destructor ensures that destructing the base class will destruct the
  /// class that inherited from it correctly.
  ~WritableDataBuffer() override = default;

  using DataBuffer::GetBytes;
  using DataBuffer::GetData;

  /// Get a pointer to the data.
  ///
  /// \return
  ///     A pointer to the bytes owned by this object, or NULL if the
  ///     object contains no bytes.
  uint8_t *GetBytes() { return const_cast<uint8_t *>(GetBytesImpl()); }

  llvm::MutableArrayRef<uint8_t> GetData() {
    return llvm::MutableArrayRef<uint8_t>(GetBytes(), GetByteSize());
  }

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
};

class DataBufferUnowned : public WritableDataBuffer {
public:
  DataBufferUnowned(uint8_t *bytes, lldb::offset_t size)
      : m_bytes(bytes), m_size(size) {}

  const uint8_t *GetBytesImpl() const override { return m_bytes; }
  lldb::offset_t GetByteSize() const override { return m_size; }

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
private:
  uint8_t *m_bytes;
  lldb::offset_t m_size;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_DATABUFFER_H
