//===- Minidump.h - Minidump object file implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MINIDUMP_H
#define LLVM_OBJECT_MINIDUMP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/iterator.h"
#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace object {

/// A class providing access to the contents of a minidump file.
class MinidumpFile : public Binary {
public:
  /// Construct a new MinidumpFile object from the given memory buffer. Returns
  /// an error if this file cannot be identified as a minidump file, or if its
  /// contents are badly corrupted (i.e. we cannot read the stream directory).
  static Expected<std::unique_ptr<MinidumpFile>> create(MemoryBufferRef Source);

  static bool classof(const Binary *B) { return B->isMinidump(); }

  /// Returns the contents of the minidump header.
  const minidump::Header &header() const { return Header; }

  /// Returns the list of streams (stream directory entries) in this file.
  ArrayRef<minidump::Directory> streams() const { return Streams; }

  /// Returns the raw contents of the stream given by the directory entry.
  ArrayRef<uint8_t> getRawStream(const minidump::Directory &Stream) const {
    return getData().slice(Stream.Location.RVA, Stream.Location.DataSize);
  }

  /// Returns the raw contents of the stream of the given type, or std::nullopt
  /// if the file does not contain a stream of this type.
  std::optional<ArrayRef<uint8_t>>
  getRawStream(minidump::StreamType Type) const;

  /// Returns the raw contents of an object given by the LocationDescriptor. An
  /// error is returned if the descriptor points outside of the minidump file.
  Expected<ArrayRef<uint8_t>>
  getRawData(minidump::LocationDescriptor Desc) const {
    return getDataSlice(getData(), Desc.RVA, Desc.DataSize);
  }

  /// Returns the minidump string at the given offset. An error is returned if
  /// we fail to parse the string, or the string is invalid UTF16.
  Expected<std::string> getString(size_t Offset) const;

  /// Returns the contents of the SystemInfo stream, cast to the appropriate
  /// type. An error is returned if the file does not contain this stream, or
  /// the stream is smaller than the size of the SystemInfo structure. The
  /// internal consistency of the stream is not checked in any way.
  Expected<const minidump::SystemInfo &> getSystemInfo() const {
    return getStream<minidump::SystemInfo>(minidump::StreamType::SystemInfo);
  }

  /// Returns the module list embedded in the ModuleList stream. An error is
  /// returned if the file does not contain this stream, or if the stream is
  /// not large enough to contain the number of modules declared in the stream
  /// header. The consistency of the Module entries themselves is not checked in
  /// any way.
  Expected<ArrayRef<minidump::Module>> getModuleList() const {
    return getListStream<minidump::Module>(minidump::StreamType::ModuleList);
  }

  /// Returns the thread list embedded in the ThreadList stream. An error is
  /// returned if the file does not contain this stream, or if the stream is
  /// not large enough to contain the number of threads declared in the stream
  /// header. The consistency of the Thread entries themselves is not checked in
  /// any way.
  Expected<ArrayRef<minidump::Thread>> getThreadList() const {
    return getListStream<minidump::Thread>(minidump::StreamType::ThreadList);
  }

  /// Returns the contents of the Exception stream.  An error is returned if the
  /// file does not contain this stream, or the stream is smaller than the size
  /// of the ExceptionStream structure.  The internal consistency of the stream
  /// is not checked in any way.
  Expected<const minidump::ExceptionStream &> getExceptionStream() const {
    return getStream<minidump::ExceptionStream>(
        minidump::StreamType::Exception);
  }

  /// Returns the list of descriptors embedded in the MemoryList stream. The
  /// descriptors provide the content of interesting regions of memory at the
  /// time the minidump was taken. An error is returned if the file does not
  /// contain this stream, or if the stream is not large enough to contain the
  /// number of memory descriptors declared in the stream header. The
  /// consistency of the MemoryDescriptor entries themselves is not checked in
  /// any way.
  Expected<ArrayRef<minidump::MemoryDescriptor>> getMemoryList() const {
    return getListStream<minidump::MemoryDescriptor>(
        minidump::StreamType::MemoryList);
  }

  class MemoryInfoIterator
      : public iterator_facade_base<MemoryInfoIterator,
                                    std::forward_iterator_tag,
                                    minidump::MemoryInfo> {
  public:
    MemoryInfoIterator(ArrayRef<uint8_t> Storage, size_t Stride)
        : Storage(Storage), Stride(Stride) {
      assert(Storage.size() % Stride == 0);
    }

    bool operator==(const MemoryInfoIterator &R) const {
      return Storage.size() == R.Storage.size();
    }

    const minidump::MemoryInfo &operator*() const {
      assert(Storage.size() >= sizeof(minidump::MemoryInfo));
      return *reinterpret_cast<const minidump::MemoryInfo *>(Storage.data());
    }

    MemoryInfoIterator &operator++() {
      Storage = Storage.drop_front(Stride);
      return *this;
    }

  private:
    ArrayRef<uint8_t> Storage;
    size_t Stride;
  };

  /// Returns the list of descriptors embedded in the MemoryInfoList stream. The
  /// descriptors provide properties (e.g. permissions) of interesting regions
  /// of memory at the time the minidump was taken. An error is returned if the
  /// file does not contain this stream, or if the stream is not large enough to
  /// contain the number of memory descriptors declared in the stream header.
  /// The consistency of the MemoryInfoList entries themselves is not checked
  /// in any way.
  Expected<iterator_range<MemoryInfoIterator>> getMemoryInfoList() const;

private:
  static Error createError(StringRef Str) {
    return make_error<GenericBinaryError>(Str, object_error::parse_failed);
  }

  static Error createEOFError() {
    return make_error<GenericBinaryError>("Unexpected EOF",
                                          object_error::unexpected_eof);
  }

  /// Return a slice of the given data array, with bounds checking.
  static Expected<ArrayRef<uint8_t>> getDataSlice(ArrayRef<uint8_t> Data,
                                                  size_t Offset, size_t Size);

  /// Return the slice of the given data array as an array of objects of the
  /// given type. The function checks that the input array is large enough to
  /// contain the correct number of objects of the given type.
  template <typename T>
  static Expected<ArrayRef<T>> getDataSliceAs(ArrayRef<uint8_t> Data,
                                              size_t Offset, size_t Count);

  MinidumpFile(MemoryBufferRef Source, const minidump::Header &Header,
               ArrayRef<minidump::Directory> Streams,
               DenseMap<minidump::StreamType, std::size_t> StreamMap)
      : Binary(ID_Minidump, Source), Header(Header), Streams(Streams),
        StreamMap(std::move(StreamMap)) {}

  ArrayRef<uint8_t> getData() const {
    return arrayRefFromStringRef(Data.getBuffer());
  }

  /// Return the stream of the given type, cast to the appropriate type. Checks
  /// that the stream is large enough to hold an object of this type.
  template <typename T>
  Expected<const T &> getStream(minidump::StreamType Stream) const;

  /// Return the contents of a stream which contains a list of fixed-size items,
  /// prefixed by the list size.
  template <typename T>
  Expected<ArrayRef<T>> getListStream(minidump::StreamType Stream) const;

  const minidump::Header &Header;
  ArrayRef<minidump::Directory> Streams;
  DenseMap<minidump::StreamType, std::size_t> StreamMap;
};

template <typename T>
Expected<const T &> MinidumpFile::getStream(minidump::StreamType Type) const {
  if (std::optional<ArrayRef<uint8_t>> Stream = getRawStream(Type)) {
    if (Stream->size() >= sizeof(T))
      return *reinterpret_cast<const T *>(Stream->data());
    return createEOFError();
  }
  return createError("No such stream");
}

template <typename T>
Expected<ArrayRef<T>> MinidumpFile::getDataSliceAs(ArrayRef<uint8_t> Data,
                                                   size_t Offset,
                                                   size_t Count) {
  // Check for overflow.
  if (Count > std::numeric_limits<size_t>::max() / sizeof(T))
    return createEOFError();
  Expected<ArrayRef<uint8_t>> Slice =
      getDataSlice(Data, Offset, sizeof(T) * Count);
  if (!Slice)
    return Slice.takeError();
  return ArrayRef<T>(reinterpret_cast<const T *>(Slice->data()), Count);
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_MINIDUMP_H
