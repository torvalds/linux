//===-- LZMA.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#if LLDB_ENABLE_LZMA
#include <lzma.h>
#endif // LLDB_ENABLE_LZMA

namespace lldb_private {

namespace lzma {

#if !LLDB_ENABLE_LZMA
bool isAvailable() { return false; }
llvm::Expected<uint64_t>
getUncompressedSize(llvm::ArrayRef<uint8_t> InputBuffer) {
  llvm_unreachable("lzma::getUncompressedSize is unavailable");
}

llvm::Error uncompress(llvm::ArrayRef<uint8_t> InputBuffer,
                       llvm::SmallVectorImpl<uint8_t> &Uncompressed) {
  llvm_unreachable("lzma::uncompress is unavailable");
}

#else // LLDB_ENABLE_LZMA

bool isAvailable() { return true; }

static const char *convertLZMACodeToString(lzma_ret Code) {
  switch (Code) {
  case LZMA_STREAM_END:
    return "lzma error: LZMA_STREAM_END";
  case LZMA_NO_CHECK:
    return "lzma error: LZMA_NO_CHECK";
  case LZMA_UNSUPPORTED_CHECK:
    return "lzma error: LZMA_UNSUPPORTED_CHECK";
  case LZMA_GET_CHECK:
    return "lzma error: LZMA_GET_CHECK";
  case LZMA_MEM_ERROR:
    return "lzma error: LZMA_MEM_ERROR";
  case LZMA_MEMLIMIT_ERROR:
    return "lzma error: LZMA_MEMLIMIT_ERROR";
  case LZMA_FORMAT_ERROR:
    return "lzma error: LZMA_FORMAT_ERROR";
  case LZMA_OPTIONS_ERROR:
    return "lzma error: LZMA_OPTIONS_ERROR";
  case LZMA_DATA_ERROR:
    return "lzma error: LZMA_DATA_ERROR";
  case LZMA_BUF_ERROR:
    return "lzma error: LZMA_BUF_ERROR";
  case LZMA_PROG_ERROR:
    return "lzma error: LZMA_PROG_ERROR";
  default:
    llvm_unreachable("unknown or unexpected lzma status code");
  }
}

llvm::Expected<uint64_t>
getUncompressedSize(llvm::ArrayRef<uint8_t> InputBuffer) {
  lzma_stream_flags opts{};
  if (InputBuffer.size() < LZMA_STREAM_HEADER_SIZE) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "size of xz-compressed blob (%lu bytes) is smaller than the "
        "LZMA_STREAM_HEADER_SIZE (%lu bytes)",
        InputBuffer.size(), LZMA_STREAM_HEADER_SIZE);
  }

  // Decode xz footer.
  lzma_ret xzerr = lzma_stream_footer_decode(
      &opts, InputBuffer.take_back(LZMA_STREAM_HEADER_SIZE).data());
  if (xzerr != LZMA_OK) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "lzma_stream_footer_decode()=%s",
                                   convertLZMACodeToString(xzerr));
  }
  if (InputBuffer.size() < (opts.backward_size + LZMA_STREAM_HEADER_SIZE)) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "xz-compressed buffer size (%lu bytes) too small (required at "
        "least %lu bytes) ",
        InputBuffer.size(), (opts.backward_size + LZMA_STREAM_HEADER_SIZE));
  }

  // Decode xz index.
  lzma_index *xzindex;
  uint64_t memlimit(UINT64_MAX);
  size_t inpos = 0;
  xzerr = lzma_index_buffer_decode(
      &xzindex, &memlimit, nullptr,
      InputBuffer.take_back(LZMA_STREAM_HEADER_SIZE + opts.backward_size)
          .data(),
      &inpos, InputBuffer.size());
  if (xzerr != LZMA_OK) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "lzma_index_buffer_decode()=%s",
                                   convertLZMACodeToString(xzerr));
  }

  // Get size of uncompressed file to construct an in-memory buffer of the
  // same size on the calling end (if needed).
  uint64_t uncompressedSize = lzma_index_uncompressed_size(xzindex);

  // Deallocate xz index as it is no longer needed.
  lzma_index_end(xzindex, nullptr);

  return uncompressedSize;
}

llvm::Error uncompress(llvm::ArrayRef<uint8_t> InputBuffer,
                       llvm::SmallVectorImpl<uint8_t> &Uncompressed) {
  llvm::Expected<uint64_t> uncompressedSize = getUncompressedSize(InputBuffer);

  if (auto err = uncompressedSize.takeError())
    return err;

  Uncompressed.resize(*uncompressedSize);

  // Decompress xz buffer to buffer.
  uint64_t memlimit = UINT64_MAX;
  size_t inpos = 0;
  size_t outpos = 0;
  lzma_ret ret = lzma_stream_buffer_decode(
      &memlimit, 0, nullptr, InputBuffer.data(), &inpos, InputBuffer.size(),
      Uncompressed.data(), &outpos, Uncompressed.size());
  if (ret != LZMA_OK) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "lzma_stream_buffer_decode()=%s",
                                   convertLZMACodeToString(ret));
  }

  return llvm::Error::success();
}

#endif // LLDB_ENABLE_LZMA

} // end of namespace lzma
} // namespace lldb_private
