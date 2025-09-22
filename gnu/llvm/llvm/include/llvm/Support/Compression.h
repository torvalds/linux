//===-- llvm/Support/Compression.h ---Compression----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains basic functions for compression/decompression.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMPRESSION_H
#define LLVM_SUPPORT_COMPRESSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
template <typename T> class SmallVectorImpl;
class Error;

// None indicates no compression. The other members are a subset of
// compression::Format, which is used for compressed debug sections in some
// object file formats (e.g. ELF). This is a separate class as we may add new
// compression::Format members for non-debugging purposes.
enum class DebugCompressionType {
  None, ///< No compression
  Zlib, ///< zlib
  Zstd, ///< Zstandard
};

namespace compression {
namespace zlib {

constexpr int NoCompression = 0;
constexpr int BestSpeedCompression = 1;
constexpr int DefaultCompression = 6;
constexpr int BestSizeCompression = 9;

bool isAvailable();

void compress(ArrayRef<uint8_t> Input,
              SmallVectorImpl<uint8_t> &CompressedBuffer,
              int Level = DefaultCompression);

Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                 size_t &UncompressedSize);

Error decompress(ArrayRef<uint8_t> Input, SmallVectorImpl<uint8_t> &Output,
                 size_t UncompressedSize);

} // End of namespace zlib

namespace zstd {

constexpr int NoCompression = -5;
constexpr int BestSpeedCompression = 1;
constexpr int DefaultCompression = 5;
constexpr int BestSizeCompression = 12;

bool isAvailable();

void compress(ArrayRef<uint8_t> Input,
              SmallVectorImpl<uint8_t> &CompressedBuffer,
              int Level = DefaultCompression, bool EnableLdm = false);

Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                 size_t &UncompressedSize);

Error decompress(ArrayRef<uint8_t> Input, SmallVectorImpl<uint8_t> &Output,
                 size_t UncompressedSize);

} // End of namespace zstd

enum class Format {
  Zlib,
  Zstd,
};

inline Format formatFor(DebugCompressionType Type) {
  switch (Type) {
  case DebugCompressionType::None:
    llvm_unreachable("not a compression type");
  case DebugCompressionType::Zlib:
    return Format::Zlib;
  case DebugCompressionType::Zstd:
    return Format::Zstd;
  }
  llvm_unreachable("");
}

struct Params {
  constexpr Params(Format F)
      : format(F), level(F == Format::Zlib ? zlib::DefaultCompression
                                           : zstd::DefaultCompression) {}
  constexpr Params(Format F, int L, bool Ldm = false)
      : format(F), level(L), zstdEnableLdm(Ldm) {}
  Params(DebugCompressionType Type) : Params(formatFor(Type)) {}

  Format format;
  int level;
  bool zstdEnableLdm = false; // Enable zstd long distance matching
  // This may support multi-threading for zstd in the future. Note that
  // different threads may produce different output, so be careful if certain
  // output determinism is desired.
};

// Return nullptr if LLVM was built with support (LLVM_ENABLE_ZLIB,
// LLVM_ENABLE_ZSTD) for the specified compression format; otherwise
// return a string literal describing the reason.
const char *getReasonIfUnsupported(Format F);

// Compress Input with the specified format P.Format. If Level is -1, use
// *::DefaultCompression for the format.
void compress(Params P, ArrayRef<uint8_t> Input,
              SmallVectorImpl<uint8_t> &Output);

// Decompress Input. The uncompressed size must be available.
Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                 uint8_t *Output, size_t UncompressedSize);
Error decompress(Format F, ArrayRef<uint8_t> Input,
                 SmallVectorImpl<uint8_t> &Output, size_t UncompressedSize);
Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                 SmallVectorImpl<uint8_t> &Output, size_t UncompressedSize);

} // End of namespace compression

} // End of namespace llvm

#endif
