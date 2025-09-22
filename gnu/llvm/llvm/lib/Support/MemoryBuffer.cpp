//===--- MemoryBuffer.cpp - Memory Buffer implementation ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the MemoryBuffer interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>
#include <sys/types.h>
#include <system_error>
#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

#ifdef __MVS__
#include "llvm/Support/AutoConvert.h"
#endif
using namespace llvm;

//===----------------------------------------------------------------------===//
// MemoryBuffer implementation itself.
//===----------------------------------------------------------------------===//

MemoryBuffer::~MemoryBuffer() = default;

/// init - Initialize this MemoryBuffer as a reference to externally allocated
/// memory, memory that we know is already null terminated.
void MemoryBuffer::init(const char *BufStart, const char *BufEnd,
                        bool RequiresNullTerminator) {
  assert((!RequiresNullTerminator || BufEnd[0] == 0) &&
         "Buffer is not null terminated!");
  BufferStart = BufStart;
  BufferEnd = BufEnd;
}

//===----------------------------------------------------------------------===//
// MemoryBufferMem implementation.
//===----------------------------------------------------------------------===//

/// CopyStringRef - Copies contents of a StringRef into a block of memory and
/// null-terminates it.
static void CopyStringRef(char *Memory, StringRef Data) {
  if (!Data.empty())
    memcpy(Memory, Data.data(), Data.size());
  Memory[Data.size()] = 0; // Null terminate string.
}

namespace {
struct NamedBufferAlloc {
  const Twine &Name;
  NamedBufferAlloc(const Twine &Name) : Name(Name) {}
};
} // namespace

void *operator new(size_t N, const NamedBufferAlloc &Alloc) {
  SmallString<256> NameBuf;
  StringRef NameRef = Alloc.Name.toStringRef(NameBuf);

  // We use malloc() and manually handle it returning null instead of calling
  // operator new because we need all uses of NamedBufferAlloc to be
  // deallocated with a call to free() due to needing to use malloc() in
  // WritableMemoryBuffer::getNewUninitMemBuffer() to work around the out-of-
  // memory handler installed by default in LLVM. See operator delete() member
  // functions within this file for the paired call to free().
  char *Mem =
      static_cast<char *>(std::malloc(N + sizeof(size_t) + NameRef.size() + 1));
  if (!Mem)
    llvm::report_bad_alloc_error("Allocation failed");
  *reinterpret_cast<size_t *>(Mem + N) = NameRef.size();
  CopyStringRef(Mem + N + sizeof(size_t), NameRef);
  return Mem;
}

namespace {
/// MemoryBufferMem - Named MemoryBuffer pointing to a block of memory.
template<typename MB>
class MemoryBufferMem : public MB {
public:
  MemoryBufferMem(StringRef InputData, bool RequiresNullTerminator) {
    MemoryBuffer::init(InputData.begin(), InputData.end(),
                       RequiresNullTerminator);
  }

  /// Disable sized deallocation for MemoryBufferMem, because it has
  /// tail-allocated data.
  void operator delete(void *p) { std::free(p); }

  StringRef getBufferIdentifier() const override {
    // The name is stored after the class itself.
    return StringRef(reinterpret_cast<const char *>(this + 1) + sizeof(size_t),
                     *reinterpret_cast<const size_t *>(this + 1));
  }

  MemoryBuffer::BufferKind getBufferKind() const override {
    return MemoryBuffer::MemoryBuffer_Malloc;
  }
};
} // namespace

template <typename MB>
static ErrorOr<std::unique_ptr<MB>>
getFileAux(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
           bool IsText, bool RequiresNullTerminator, bool IsVolatile,
           std::optional<Align> Alignment);

std::unique_ptr<MemoryBuffer>
MemoryBuffer::getMemBuffer(StringRef InputData, StringRef BufferName,
                           bool RequiresNullTerminator) {
  auto *Ret = new (NamedBufferAlloc(BufferName))
      MemoryBufferMem<MemoryBuffer>(InputData, RequiresNullTerminator);
  return std::unique_ptr<MemoryBuffer>(Ret);
}

std::unique_ptr<MemoryBuffer>
MemoryBuffer::getMemBuffer(MemoryBufferRef Ref, bool RequiresNullTerminator) {
  return std::unique_ptr<MemoryBuffer>(getMemBuffer(
      Ref.getBuffer(), Ref.getBufferIdentifier(), RequiresNullTerminator));
}

static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
getMemBufferCopyImpl(StringRef InputData, const Twine &BufferName) {
  auto Buf =
      WritableMemoryBuffer::getNewUninitMemBuffer(InputData.size(), BufferName);
  if (!Buf)
    return make_error_code(errc::not_enough_memory);
  // Calling memcpy with null src/dst is UB, and an empty StringRef is
  // represented with {nullptr, 0}.
  llvm::copy(InputData, Buf->getBufferStart());
  return std::move(Buf);
}

std::unique_ptr<MemoryBuffer>
MemoryBuffer::getMemBufferCopy(StringRef InputData, const Twine &BufferName) {
  auto Buf = getMemBufferCopyImpl(InputData, BufferName);
  if (Buf)
    return std::move(*Buf);
  return nullptr;
}

ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFileOrSTDIN(const Twine &Filename, bool IsText,
                             bool RequiresNullTerminator,
                             std::optional<Align> Alignment) {
  SmallString<256> NameBuf;
  StringRef NameRef = Filename.toStringRef(NameBuf);

  if (NameRef == "-")
    return getSTDIN();
  return getFile(Filename, IsText, RequiresNullTerminator,
                 /*IsVolatile=*/false, Alignment);
}

ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFileSlice(const Twine &FilePath, uint64_t MapSize,
                           uint64_t Offset, bool IsVolatile,
                           std::optional<Align> Alignment) {
  return getFileAux<MemoryBuffer>(FilePath, MapSize, Offset, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false, IsVolatile,
                                  Alignment);
}

//===----------------------------------------------------------------------===//
// MemoryBuffer::getFile implementation.
//===----------------------------------------------------------------------===//

namespace {

template <typename MB>
constexpr sys::fs::mapped_file_region::mapmode Mapmode =
    sys::fs::mapped_file_region::readonly;
template <>
constexpr sys::fs::mapped_file_region::mapmode Mapmode<MemoryBuffer> =
    sys::fs::mapped_file_region::readonly;
template <>
constexpr sys::fs::mapped_file_region::mapmode Mapmode<WritableMemoryBuffer> =
    sys::fs::mapped_file_region::priv;
template <>
constexpr sys::fs::mapped_file_region::mapmode
    Mapmode<WriteThroughMemoryBuffer> = sys::fs::mapped_file_region::readwrite;

/// Memory maps a file descriptor using sys::fs::mapped_file_region.
///
/// This handles converting the offset into a legal offset on the platform.
template<typename MB>
class MemoryBufferMMapFile : public MB {
  sys::fs::mapped_file_region MFR;

  static uint64_t getLegalMapOffset(uint64_t Offset) {
    return Offset & ~(sys::fs::mapped_file_region::alignment() - 1);
  }

  static uint64_t getLegalMapSize(uint64_t Len, uint64_t Offset) {
    return Len + (Offset - getLegalMapOffset(Offset));
  }

  const char *getStart(uint64_t Len, uint64_t Offset) {
    return MFR.const_data() + (Offset - getLegalMapOffset(Offset));
  }

public:
  MemoryBufferMMapFile(bool RequiresNullTerminator, sys::fs::file_t FD, uint64_t Len,
                       uint64_t Offset, std::error_code &EC)
      : MFR(FD, Mapmode<MB>, getLegalMapSize(Len, Offset),
            getLegalMapOffset(Offset), EC) {
    if (!EC) {
      const char *Start = getStart(Len, Offset);
      MemoryBuffer::init(Start, Start + Len, RequiresNullTerminator);
    }
  }

  /// Disable sized deallocation for MemoryBufferMMapFile, because it has
  /// tail-allocated data.
  void operator delete(void *p) { std::free(p); }

  StringRef getBufferIdentifier() const override {
    // The name is stored after the class itself.
    return StringRef(reinterpret_cast<const char *>(this + 1) + sizeof(size_t),
                     *reinterpret_cast<const size_t *>(this + 1));
  }

  MemoryBuffer::BufferKind getBufferKind() const override {
    return MemoryBuffer::MemoryBuffer_MMap;
  }

  void dontNeedIfMmap() override { MFR.dontNeed(); }
};
} // namespace

static ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
getMemoryBufferForStream(sys::fs::file_t FD, const Twine &BufferName) {
  SmallString<sys::fs::DefaultReadChunkSize> Buffer;
  if (Error E = sys::fs::readNativeFileToEOF(FD, Buffer))
    return errorToErrorCode(std::move(E));
  return getMemBufferCopyImpl(Buffer, BufferName);
}

ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFile(const Twine &Filename, bool IsText,
                      bool RequiresNullTerminator, bool IsVolatile,
                      std::optional<Align> Alignment) {
  return getFileAux<MemoryBuffer>(Filename, /*MapSize=*/-1, /*Offset=*/0,
                                  IsText, RequiresNullTerminator, IsVolatile,
                                  Alignment);
}

template <typename MB>
static ErrorOr<std::unique_ptr<MB>>
getOpenFileImpl(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
                uint64_t MapSize, int64_t Offset, bool RequiresNullTerminator,
                bool IsVolatile, std::optional<Align> Alignment);

template <typename MB>
static ErrorOr<std::unique_ptr<MB>>
getFileAux(const Twine &Filename, uint64_t MapSize, uint64_t Offset,
           bool IsText, bool RequiresNullTerminator, bool IsVolatile,
           std::optional<Align> Alignment) {
  Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForRead(
      Filename, IsText ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  sys::fs::file_t FD = *FDOrErr;
  auto Ret = getOpenFileImpl<MB>(FD, Filename, /*FileSize=*/-1, MapSize, Offset,
                                 RequiresNullTerminator, IsVolatile, Alignment);
  sys::fs::closeFile(FD);
  return Ret;
}

ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
WritableMemoryBuffer::getFile(const Twine &Filename, bool IsVolatile,
                              std::optional<Align> Alignment) {
  return getFileAux<WritableMemoryBuffer>(
      Filename, /*MapSize=*/-1, /*Offset=*/0, /*IsText=*/false,
      /*RequiresNullTerminator=*/false, IsVolatile, Alignment);
}

ErrorOr<std::unique_ptr<WritableMemoryBuffer>>
WritableMemoryBuffer::getFileSlice(const Twine &Filename, uint64_t MapSize,
                                   uint64_t Offset, bool IsVolatile,
                                   std::optional<Align> Alignment) {
  return getFileAux<WritableMemoryBuffer>(
      Filename, MapSize, Offset, /*IsText=*/false,
      /*RequiresNullTerminator=*/false, IsVolatile, Alignment);
}

std::unique_ptr<WritableMemoryBuffer>
WritableMemoryBuffer::getNewUninitMemBuffer(size_t Size,
                                            const Twine &BufferName,
                                            std::optional<Align> Alignment) {
  using MemBuffer = MemoryBufferMem<WritableMemoryBuffer>;

  // Use 16-byte alignment if no alignment is specified.
  Align BufAlign = Alignment.value_or(Align(16));

  // Allocate space for the MemoryBuffer, the data and the name. It is important
  // that MemoryBuffer and data are aligned so PointerIntPair works with them.
  SmallString<256> NameBuf;
  StringRef NameRef = BufferName.toStringRef(NameBuf);

  size_t StringLen = sizeof(MemBuffer) + sizeof(size_t) + NameRef.size() + 1;
  size_t RealLen = StringLen + Size + 1 + BufAlign.value();
  if (RealLen <= Size) // Check for rollover.
    return nullptr;
  // We use a call to malloc() rather than a call to a non-throwing operator
  // new() because LLVM unconditionally installs an out of memory new handler
  // when exceptions are disabled. This new handler intentionally crashes to
  // aid with debugging, but that makes non-throwing new calls unhelpful.
  // See MemoryBufferMem::operator delete() for the paired call to free(), and
  // llvm::install_out_of_memory_new_handler() for the installation of the
  // custom new handler.
  char *Mem = static_cast<char *>(std::malloc(RealLen));
  if (!Mem)
    return nullptr;

  // The name is stored after the class itself.
  *reinterpret_cast<size_t *>(Mem + sizeof(MemBuffer)) = NameRef.size();
  CopyStringRef(Mem + sizeof(MemBuffer) + sizeof(size_t), NameRef);

  // The buffer begins after the name and must be aligned.
  char *Buf = (char *)alignAddr(Mem + StringLen, BufAlign);
  Buf[Size] = 0; // Null terminate buffer.

  auto *Ret = new (Mem) MemBuffer(StringRef(Buf, Size), true);
  return std::unique_ptr<WritableMemoryBuffer>(Ret);
}

std::unique_ptr<WritableMemoryBuffer>
WritableMemoryBuffer::getNewMemBuffer(size_t Size, const Twine &BufferName) {
  auto SB = WritableMemoryBuffer::getNewUninitMemBuffer(Size, BufferName);
  if (!SB)
    return nullptr;
  memset(SB->getBufferStart(), 0, Size);
  return SB;
}

static bool shouldUseMmap(sys::fs::file_t FD,
                          size_t FileSize,
                          size_t MapSize,
                          off_t Offset,
                          bool RequiresNullTerminator,
                          int PageSize,
                          bool IsVolatile) {
  // mmap may leave the buffer without null terminator if the file size changed
  // by the time the last page is mapped in, so avoid it if the file size is
  // likely to change.
  if (IsVolatile && RequiresNullTerminator)
    return false;

  // We don't use mmap for small files because this can severely fragment our
  // address space.
  if (MapSize < 4 * 4096 || MapSize < (unsigned)PageSize)
    return false;

  if (!RequiresNullTerminator)
    return true;

  // If we don't know the file size, use fstat to find out.  fstat on an open
  // file descriptor is cheaper than stat on a random path.
  // FIXME: this chunk of code is duplicated, but it avoids a fstat when
  // RequiresNullTerminator = false and MapSize != -1.
  if (FileSize == size_t(-1)) {
    sys::fs::file_status Status;
    if (sys::fs::status(FD, Status))
      return false;
    FileSize = Status.getSize();
  }

  // If we need a null terminator and the end of the map is inside the file,
  // we cannot use mmap.
  size_t End = Offset + MapSize;
  assert(End <= FileSize);
  if (End != FileSize)
    return false;

  // Don't try to map files that are exactly a multiple of the system page size
  // if we need a null terminator.
  if ((FileSize & (PageSize -1)) == 0)
    return false;

#if defined(__CYGWIN__)
  // Don't try to map files that are exactly a multiple of the physical page size
  // if we need a null terminator.
  // FIXME: We should reorganize again getPageSize() on Win32.
  if ((FileSize & (4096 - 1)) == 0)
    return false;
#endif

  return true;
}

static ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
getReadWriteFile(const Twine &Filename, uint64_t FileSize, uint64_t MapSize,
                 uint64_t Offset) {
  Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForReadWrite(
      Filename, sys::fs::CD_OpenExisting, sys::fs::OF_None);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  sys::fs::file_t FD = *FDOrErr;

  // Default is to map the full file.
  if (MapSize == uint64_t(-1)) {
    // If we don't know the file size, use fstat to find out.  fstat on an open
    // file descriptor is cheaper than stat on a random path.
    if (FileSize == uint64_t(-1)) {
      sys::fs::file_status Status;
      std::error_code EC = sys::fs::status(FD, Status);
      if (EC)
        return EC;

      // If this not a file or a block device (e.g. it's a named pipe
      // or character device), we can't mmap it, so error out.
      sys::fs::file_type Type = Status.type();
      if (Type != sys::fs::file_type::regular_file &&
          Type != sys::fs::file_type::block_file)
        return make_error_code(errc::invalid_argument);

      FileSize = Status.getSize();
    }
    MapSize = FileSize;
  }

  std::error_code EC;
  std::unique_ptr<WriteThroughMemoryBuffer> Result(
      new (NamedBufferAlloc(Filename))
          MemoryBufferMMapFile<WriteThroughMemoryBuffer>(false, FD, MapSize,
                                                         Offset, EC));
  if (EC)
    return EC;
  return std::move(Result);
}

ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
WriteThroughMemoryBuffer::getFile(const Twine &Filename, int64_t FileSize) {
  return getReadWriteFile(Filename, FileSize, FileSize, 0);
}

/// Map a subrange of the specified file as a WritableMemoryBuffer.
ErrorOr<std::unique_ptr<WriteThroughMemoryBuffer>>
WriteThroughMemoryBuffer::getFileSlice(const Twine &Filename, uint64_t MapSize,
                                       uint64_t Offset) {
  return getReadWriteFile(Filename, -1, MapSize, Offset);
}

template <typename MB>
static ErrorOr<std::unique_ptr<MB>>
getOpenFileImpl(sys::fs::file_t FD, const Twine &Filename, uint64_t FileSize,
                uint64_t MapSize, int64_t Offset, bool RequiresNullTerminator,
                bool IsVolatile, std::optional<Align> Alignment) {
  static int PageSize = sys::Process::getPageSizeEstimate();

  // Default is to map the full file.
  if (MapSize == uint64_t(-1)) {
    // If we don't know the file size, use fstat to find out.  fstat on an open
    // file descriptor is cheaper than stat on a random path.
    if (FileSize == uint64_t(-1)) {
      sys::fs::file_status Status;
      std::error_code EC = sys::fs::status(FD, Status);
      if (EC)
        return EC;

      // If this not a file or a block device (e.g. it's a named pipe
      // or character device), we can't trust the size. Create the memory
      // buffer by copying off the stream.
      sys::fs::file_type Type = Status.type();
      if (Type != sys::fs::file_type::regular_file &&
          Type != sys::fs::file_type::block_file)
        return getMemoryBufferForStream(FD, Filename);

      FileSize = Status.getSize();
    }
    MapSize = FileSize;
  }

  if (shouldUseMmap(FD, FileSize, MapSize, Offset, RequiresNullTerminator,
                    PageSize, IsVolatile)) {
    std::error_code EC;
    std::unique_ptr<MB> Result(
        new (NamedBufferAlloc(Filename)) MemoryBufferMMapFile<MB>(
            RequiresNullTerminator, FD, MapSize, Offset, EC));
    if (!EC)
      return std::move(Result);
  }

#ifdef __MVS__
  // Set codepage auto-conversion for z/OS.
  if (auto EC = llvm::enableAutoConversion(FD))
    return EC;
#endif

  auto Buf =
      WritableMemoryBuffer::getNewUninitMemBuffer(MapSize, Filename, Alignment);
  if (!Buf) {
    // Failed to create a buffer. The only way it can fail is if
    // new(std::nothrow) returns 0.
    return make_error_code(errc::not_enough_memory);
  }

  // Read until EOF, zero-initialize the rest.
  MutableArrayRef<char> ToRead = Buf->getBuffer();
  while (!ToRead.empty()) {
    Expected<size_t> ReadBytes =
        sys::fs::readNativeFileSlice(FD, ToRead, Offset);
    if (!ReadBytes)
      return errorToErrorCode(ReadBytes.takeError());
    if (*ReadBytes == 0) {
      std::memset(ToRead.data(), 0, ToRead.size());
      break;
    }
    ToRead = ToRead.drop_front(*ReadBytes);
    Offset += *ReadBytes;
  }

  return std::move(Buf);
}

ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getOpenFile(sys::fs::file_t FD, const Twine &Filename,
                          uint64_t FileSize, bool RequiresNullTerminator,
                          bool IsVolatile, std::optional<Align> Alignment) {
  return getOpenFileImpl<MemoryBuffer>(FD, Filename, FileSize, FileSize, 0,
                                       RequiresNullTerminator, IsVolatile,
                                       Alignment);
}

ErrorOr<std::unique_ptr<MemoryBuffer>> MemoryBuffer::getOpenFileSlice(
    sys::fs::file_t FD, const Twine &Filename, uint64_t MapSize, int64_t Offset,
    bool IsVolatile, std::optional<Align> Alignment) {
  assert(MapSize != uint64_t(-1));
  return getOpenFileImpl<MemoryBuffer>(FD, Filename, -1, MapSize, Offset, false,
                                       IsVolatile, Alignment);
}

ErrorOr<std::unique_ptr<MemoryBuffer>> MemoryBuffer::getSTDIN() {
  // Read in all of the data from stdin, we cannot mmap stdin.
  //
  // FIXME: That isn't necessarily true, we should try to mmap stdin and
  // fallback if it fails.
  sys::ChangeStdinMode(sys::fs::OF_Text);

  return getMemoryBufferForStream(sys::fs::getStdinHandle(), "<stdin>");
}

ErrorOr<std::unique_ptr<MemoryBuffer>>
MemoryBuffer::getFileAsStream(const Twine &Filename) {
  Expected<sys::fs::file_t> FDOrErr =
      sys::fs::openNativeFileForRead(Filename, sys::fs::OF_None);
  if (!FDOrErr)
    return errorToErrorCode(FDOrErr.takeError());
  sys::fs::file_t FD = *FDOrErr;
  ErrorOr<std::unique_ptr<MemoryBuffer>> Ret =
      getMemoryBufferForStream(FD, Filename);
  sys::fs::closeFile(FD);
  return Ret;
}

MemoryBufferRef MemoryBuffer::getMemBufferRef() const {
  StringRef Data = getBuffer();
  StringRef Identifier = getBufferIdentifier();
  return MemoryBufferRef(Data, Identifier);
}

SmallVectorMemoryBuffer::~SmallVectorMemoryBuffer() = default;
