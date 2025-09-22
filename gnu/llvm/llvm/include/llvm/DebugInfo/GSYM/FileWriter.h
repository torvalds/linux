//===- FileWriter.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_FILEWRITER_H
#define LLVM_DEBUGINFO_GSYM_FILEWRITER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace llvm {
class raw_pwrite_stream;

namespace gsym {

/// A simplified binary data writer class that doesn't require targets, target
/// definitions, architectures, or require any other optional compile time
/// libraries to be enabled via the build process. This class needs the ability
/// to seek to different spots in the binary stream that is produces to fixup
/// offsets and sizes.
class FileWriter {
  llvm::raw_pwrite_stream &OS;
  llvm::endianness ByteOrder;

public:
  FileWriter(llvm::raw_pwrite_stream &S, llvm::endianness B)
      : OS(S), ByteOrder(B) {}
  ~FileWriter();
  /// Write a single uint8_t value into the stream at the current file
  /// position.
  ///
  /// \param   Value The value to write into the stream.
  void writeU8(uint8_t Value);

  /// Write a single uint16_t value into the stream at the current file
  /// position. The value will be byte swapped if needed to match the byte
  /// order specified during construction.
  ///
  /// \param   Value The value to write into the stream.
  void writeU16(uint16_t Value);

  /// Write a single uint32_t value into the stream at the current file
  /// position. The value will be byte swapped if needed to match the byte
  /// order specified during construction.
  ///
  /// \param   Value The value to write into the stream.
  void writeU32(uint32_t Value);

  /// Write a single uint64_t value into the stream at the current file
  /// position. The value will be byte swapped if needed to match the byte
  /// order specified during construction.
  ///
  /// \param   Value The value to write into the stream.
  void writeU64(uint64_t Value);

  /// Write the value into the stream encoded using signed LEB128 at the
  /// current file position.
  ///
  /// \param   Value The value to write into the stream.
  void writeSLEB(int64_t Value);

  /// Write the value into the stream encoded using unsigned LEB128 at the
  /// current file position.
  ///
  /// \param   Value The value to write into the stream.
  void writeULEB(uint64_t Value);

  /// Write an array of uint8_t values into the stream at the current file
  /// position.
  ///
  /// \param   Data An array of values to write into the stream.
  void writeData(llvm::ArrayRef<uint8_t> Data);

  /// Write a NULL terminated C string into the stream at the current file
  /// position. The entire contents of Str will be written into the steam at
  /// the current file position and then an extra NULL termation byte will be
  /// written. It is up to the user to ensure that Str doesn't contain any NULL
  /// characters unless the additional NULL characters are desired.
  ///
  /// \param   Str The value to write into the stream.
  void writeNullTerminated(llvm::StringRef Str);

  /// Fixup a uint32_t value at the specified offset in the stream. This
  /// function will save the current file position, seek to the specified
  /// offset, overwrite the data using Value, and then restore the file
  /// position to the previous file position.
  ///
  /// \param   Value The value to write into the stream.
  /// \param   Offset The offset at which to write the Value within the stream.
  void fixup32(uint32_t Value, uint64_t Offset);

  /// Pad with zeroes at the current file position until the current file
  /// position matches the specified alignment.
  ///
  /// \param  Align An integer speciying the desired alignment. This does not
  ///         need to be a power of two.
  void alignTo(size_t Align);

  /// Return the current offset within the file.
  ///
  /// \return The unsigned offset from the start of the file of the current
  ///         file position.
  uint64_t tell();

  llvm::raw_pwrite_stream &get_stream() {
    return OS;
  }

  llvm::endianness getByteOrder() const { return ByteOrder; }

private:
  FileWriter(const FileWriter &rhs) = delete;
  void operator=(const FileWriter &rhs) = delete;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_FILEWRITER_H
