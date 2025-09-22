//===--- HeaderMap.h - A file that acts like dir of symlinks ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the HeaderMap interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_HEADERMAP_H
#define LLVM_CLANG_LEX_HEADERMAP_H

#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Lex/HeaderMapTypes.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <optional>

namespace clang {

struct HMapBucket;
struct HMapHeader;

/// Implementation for \a HeaderMap that doesn't depend on \a FileManager.
class HeaderMapImpl {
  std::unique_ptr<const llvm::MemoryBuffer> FileBuffer;
  bool NeedsBSwap;
  mutable llvm::StringMap<StringRef> ReverseMap;

public:
  HeaderMapImpl(std::unique_ptr<const llvm::MemoryBuffer> File, bool NeedsBSwap)
      : FileBuffer(std::move(File)), NeedsBSwap(NeedsBSwap) {}

  // Check for a valid header and extract the byte swap.
  static bool checkHeader(const llvm::MemoryBuffer &File, bool &NeedsByteSwap);

  // Make a call for every Key in the map.
  template <typename Func> void forEachKey(Func Callback) const {
    const HMapHeader &Hdr = getHeader();
    unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);

    for (unsigned Bucket = 0; Bucket < NumBuckets; ++Bucket) {
      HMapBucket B = getBucket(Bucket);
      if (B.Key != HMAP_EmptyBucketKey)
        if (std::optional<StringRef> Key = getString(B.Key))
          Callback(*Key);
    }
  }

  /// If the specified relative filename is located in this HeaderMap return
  /// the filename it is mapped to, otherwise return an empty StringRef.
  StringRef lookupFilename(StringRef Filename,
                           SmallVectorImpl<char> &DestPath) const;

  /// Return the filename of the headermap.
  StringRef getFileName() const;

  /// Print the contents of this headermap to stderr.
  void dump() const;

  /// Return key for specifed path.
  StringRef reverseLookupFilename(StringRef DestPath) const;

private:
  unsigned getEndianAdjustedWord(unsigned X) const;
  const HMapHeader &getHeader() const;
  HMapBucket getBucket(unsigned BucketNo) const;

  /// Look up the specified string in the string table.  If the string index is
  /// not valid, return std::nullopt.
  std::optional<StringRef> getString(unsigned StrTabIdx) const;
};

/// This class represents an Apple concept known as a 'header map'.  To the
/// \#include file resolution process, it basically acts like a directory of
/// symlinks to files.  Its advantages are that it is dense and more efficient
/// to create and process than a directory of symlinks.
class HeaderMap : private HeaderMapImpl {
  HeaderMap(std::unique_ptr<const llvm::MemoryBuffer> File, bool BSwap)
      : HeaderMapImpl(std::move(File), BSwap) {}

public:
  /// This attempts to load the specified file as a header map.  If it doesn't
  /// look like a HeaderMap, it gives up and returns null.
  static std::unique_ptr<HeaderMap> Create(FileEntryRef FE, FileManager &FM);

  using HeaderMapImpl::dump;
  using HeaderMapImpl::forEachKey;
  using HeaderMapImpl::getFileName;
  using HeaderMapImpl::lookupFilename;
  using HeaderMapImpl::reverseLookupFilename;
};

} // end namespace clang.

#endif
