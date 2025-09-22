//===--- MemoryBuffer.h - Memory Buffer Interface ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the MemoryBuffer interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MEMORYBUFFER_H
#define LLVM_SUPPORT_MEMORYBUFFER_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace llvm {
namespace sys {
namespace fs {
// Duplicated from FileSystem.h to avoid a dependency.
#if defined(_WIN32)
// A Win32 HANDLE is a typedef of void*
using file_t = void *;
#else
using file_t = int;
#endif
} // namespace fs
} // namespace sys

/// This interface provides simple read-only access to a block of memory, and
/// provides simple methods for reading files and standard input into a memory
/// buffer.  In addition to basic access to the characters in the file, this
/// interface guarantees you can read one character past the end of the file,
/// and that this character will read as '\0'.
///
/// The '\0' guarantee is needed to support an optimization -- it's intended to
/// be more efficient for clients which are reading all the data to stop
/// reading when they encounter a '\0' than to continually check the file
/// position to see if it has reached the end of the file.
class MemoryBuffer {
  const char *BufferStart; // Start of the buffer.
  const char *BufferEnd;   // End of the buffer.

protected:
  MemoryBuffer() = default;

  void init(const char *BufStart, const char *BufEnd,
            bool RequiresNullTerminator);

public:
  MemoryBuffer(const MemoryBuffer &) = delete;
  MemoryBuffer &operator=(const MemoryBuffer &) = delete;
  virtual ~MemoryBuffer();

  const char *getBufferStart() const { return BufferStart; }
  const char *getBufferEnd() const   { return BufferEnd; }
  size_t getBufferSize() const { return BufferEnd-BufferStart; }

  StringRef getBuffer() const {
    return StringRef(BufferStart, getBufferSize());
  }

  /// Return an identifier for this buffer, typically the filename it was read
  /// from.
  virtual StringRef getBufferIdentifier() const { return "Unknown buffer"; }

  /// For read-only MemoryBuffer_MMap, mark the buffer as unused in the near
  /// future and the kernel can free resources associated with it. Further
  /// access is supported but may be expensive. This calls
  /// madvise(MADV_DONTNEED) on read-only file mappings on *NIX systems. This
  /// function should not be called on a writable buffer.
  virtual void dontNeedIfMmap() {}

  /// Open the specified file as a MemoryBuffer, returning a new MemoryBuffer
  /// if successful, otherwise returning null.
  ///
  /// \param IsText Set to true to indicate that the file should be read in
  /// text mode.
  ///
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFile(const Twine &Filename, bool IsText = false,
          bool RequiresNullTerminator = true, bool IsVolatile = false,
          std::optional<Align> Alignment = std::nullopt);

  /// Read all of the specified file into a MemoryBuffer as a stream
  /// (i.e. until EOF reached). This is useful for special files that
  /// look like a regular file but have 0 size (e.g. /proc/cpuinfo on Linux).
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileAsStream(const Twine &Filename);

  /// Given an already-open file descriptor, map some slice of it into a
  /// MemoryBuffer. The slice is specified by an \p Offset and \p MapSize.
  /// Since this is in the middle of a file, the buffer is not null terminated.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getOpenFileSlice(sys::fs::file_t FD, const Twine &Filename, uint64_t MapSize,
                   int64_t Offset, bool IsVolatile = false,
                   std::optional<Align> Alignment = std::nullopt);

  /// Given an already-open file descriptor, read the file and return a
  /// MemoryBuffer.
  ///
  /// \param IsVolatile Set to true to indicate that the contents of the file
  /// can change outside the user's control, e.g. when libclang tries to parse
  /// while the user is editing/updating the file or if the file is on an NFS.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getOpenFile(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
              bool RequiresNullTerminator = true, bool IsVolatile = false,
              std::optional<Align> Alignment = std::nullopt);

  /// Open the specified memory range as a MemoryBuffer. Note that InputData
  /// must be null terminated if RequiresNullTerminator is true.
  static std::unique_ptr<MemoryBuffer>
  getMemBuffer(StringRef InputData, StringRef BufferName = "",
               bool RequiresNullTerminator = true);

  static std::unique_ptr<MemoryBuffer>
  getMemBuffer(MemoryBufferRef Ref, bool RequiresNullTerminator = true);

  /// Open the specified memory range as a MemoryBuffer, copying the contents
  /// and taking ownership of it. InputData does not have to be null terminated.
  static std::unique_ptr<MemoryBuffer>
  getMemBufferCopy(StringRef InputData, const Twine &BufferName = "");

  /// Read all of stdin into a file buffer, and return it.
  static ErrorOr<std::unique_ptr<MemoryBuffer>> getSTDIN();

  /// Open the specified file as a MemoryBuffer, or open stdin if the Filename
  /// is "-".
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileOrSTDIN(const Twine &Filename, bool IsText = false,
                 bool RequiresNullTerminator = true,
                 std::optional<Align> Alignment = std::nullopt);

  /// Map a subrange of the specified file as a MemoryBuffer.
  static ErrorOr<std::unique_ptr<MemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
               bool IsVolatile = false,
               std::optional<Align> Alignment = std::nullopt);

  //===--------------------------------------------------------------------===//
  // Provided for performance analysis.
  //===--------------------------------------------------------------------===//

  /// The kind of memory backing used to support the MemoryBuffer.
  enum BufferKind {
    MemoryBuffer_Malloc,
    MemoryBuffer_MMap
  };

  /// Return information on the memory mechanism used to support the
  /// MemoryBuffer.
  virtual BufferKind getBufferKind() const = 0;

  MemoryBufferRef getMemBufferRef() const;
};

/// This class is an extension of MemoryBuffer, which allows copy-on-write
/// access to the underlying contents.  It only supports creation methods that
/// are guaranteed to produce a writable buffer.  For example, mapping a file
/// read-only is not supported.
class WritableMemoryBuffer : public MemoryBuffer {
protected:
  WritableMemoryBuffer() = default;

public:
  using MemoryBuffer::getBuffer;
  using MemoryBuffer::getBufferEnd;
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  MutableArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
  getFile(const Twine &Filename, bool IsVolatile = false,
          std::optional<Align> Alignment = std::nullopt);

  /// Map a subrange of the specified file as a WritableMemoryBuffer.
  static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
               bool IsVolatile = false,
               std::optional<Align> Alignment = std::nullopt);

  /// Allocate a new MemoryBuffer of the specified size that is not initialized.
  /// Note that the caller should initialize the memory allocated by this
  /// method. The memory is owned by the MemoryBuffer object.
  ///
  /// \param Alignment Set to indicate that the buffer should be aligned to at
  /// least the specified alignment.
  static std::unique_ptr<WritableMemoryBuffer>
  getNewUninitMemBuffer(size_t Size, const Twine &BufferName = "",
                        std::optional<Align> Alignment = std::nullopt);

  /// Allocate a new zero-initialized MemoryBuffer of the specified size. Note
  /// that the caller need not initialize the memory allocated by this method.
  /// The memory is owned by the MemoryBuffer object.
  static std::unique_ptr<WritableMemoryBuffer>
  getNewMemBuffer(size_t Size, const Twine &BufferName = "");

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that he got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

/// This class is an extension of MemoryBuffer, which allows write access to
/// the underlying contents and committing those changes to the original source.
/// It only supports creation methods that are guaranteed to produce a writable
/// buffer.  For example, mapping a file read-only is not supported.
class WriteThroughMemoryBuffer : public MemoryBuffer {
protected:
  WriteThroughMemoryBuffer() = default;

public:
  using MemoryBuffer::getBuffer;
  using MemoryBuffer::getBufferEnd;
  using MemoryBuffer::getBufferStart;

  // const_cast is well-defined here, because the underlying buffer is
  // guaranteed to have been initialized with a mutable buffer.
  char *getBufferStart() {
    return const_cast<char *>(MemoryBuffer::getBufferStart());
  }
  char *getBufferEnd() {
    return const_cast<char *>(MemoryBuffer::getBufferEnd());
  }
  MutableArrayRef<char> getBuffer() {
    return {getBufferStart(), getBufferEnd()};
  }

  static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
  getFile(const Twine &Filename, int64_t FileSize = -1);

  /// Map a subrange of the specified file as a ReadWriteMemoryBuffer.
  static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
  getFileSlice(const Twine &Filename, uint64_t MapSize, uint64_t Offset);

private:
  // Hide these base class factory function so one can't write
  //   WritableMemoryBuffer::getXXX()
  // and be surprised that he got a read-only Buffer.
  using MemoryBuffer::getFileAsStream;
  using MemoryBuffer::getFileOrSTDIN;
  using MemoryBuffer::getMemBuffer;
  using MemoryBuffer::getMemBufferCopy;
  using MemoryBuffer::getOpenFile;
  using MemoryBuffer::getOpenFileSlice;
  using MemoryBuffer::getSTDIN;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(MemoryBuffer, LLVMMemoryBufferRef)

} // end namespace llvm

#endif // LLVM_SUPPORT_MEMORYBUFFER_H
