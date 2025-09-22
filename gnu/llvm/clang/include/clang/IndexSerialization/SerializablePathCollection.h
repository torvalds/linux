//===--- SerializablePathCollection.h -- Index of paths ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INDEXSERIALIZATION_SERIALIZABLEPATHCOLLECTION_H
#define LLVM_CLANG_INDEXSERIALIZATION_SERIALIZABLEPATHCOLLECTION_H

#include "clang/Basic/FileManager.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"

#include <string>
#include <vector>

namespace clang {
namespace index {

/// Pool of strings
class StringPool {
  llvm::SmallString<512> Buffer;

public:
  struct StringOffsetSize {
    std::size_t Offset;
    std::size_t Size;

    StringOffsetSize(size_t Offset, size_t Size) : Offset(Offset), Size(Size) {}
  };

  StringOffsetSize add(StringRef Str);
  StringRef getBuffer() const { return Buffer; }
};

/// Pool of filesystem paths backed by a StringPool
class PathPool {
public:
  /// Special root directory of a filesystem path.
  enum class RootDirKind {
    Regular = 0,
    CurrentWorkDir = 1,
    SysRoot = 2,
  };

  struct DirPath {
    RootDirKind Root;
    StringPool::StringOffsetSize Path;

    DirPath(RootDirKind Root, const StringPool::StringOffsetSize &Path)
        : Root(Root), Path(Path) {}
  };

  struct FilePath {
    DirPath Dir;
    StringPool::StringOffsetSize Filename;

    FilePath(const DirPath &Dir, const StringPool::StringOffsetSize &Filename)
        : Dir(Dir), Filename(Filename) {}
  };

  /// \returns index of the newly added file in FilePaths.
  size_t addFilePath(RootDirKind Root, const StringPool::StringOffsetSize &Dir,
                     StringRef Filename);

  /// \returns offset in Paths and size of newly added directory.
  StringPool::StringOffsetSize addDirPath(StringRef Dir);

  llvm::ArrayRef<FilePath> getFilePaths() const;

  StringRef getPaths() const;

private:
  StringPool Paths;
  std::vector<FilePath> FilePaths;
};

/// Stores file paths and produces serialization-friendly representation.
class SerializablePathCollection {
  std::string WorkDir;
  std::string SysRoot;

  PathPool Paths;
  llvm::DenseMap<const clang::FileEntry *, std::size_t> UniqueFiles;
  llvm::StringMap<PathPool::DirPath, llvm::BumpPtrAllocator> UniqueDirs;

public:
  const StringPool::StringOffsetSize WorkDirPath;
  const StringPool::StringOffsetSize SysRootPath;
  const StringPool::StringOffsetSize OutputFilePath;

  SerializablePathCollection(llvm::StringRef CurrentWorkDir,
                             llvm::StringRef SysRoot,
                             llvm::StringRef OutputFile);

  /// \returns buffer containing all the paths.
  llvm::StringRef getPathsBuffer() const { return Paths.getPaths(); }

  /// \returns file paths (no directories) backed by buffer exposed in
  /// getPathsBuffer.
  ArrayRef<PathPool::FilePath> getFilePaths() const {
    return Paths.getFilePaths();
  }

  /// Stores path to \p FE if it hasn't been stored yet.
  /// \returns index to array exposed by getPathsBuffer().
  size_t tryStoreFilePath(FileEntryRef FE);

private:
  /// Stores \p Path if it is non-empty.
  /// Warning: this method doesn't check for uniqueness.
  /// \returns offset of \p Path value begin in buffer with stored paths.
  StringPool::StringOffsetSize storePath(llvm::StringRef Path);

  /// Stores \p dirStr path if it hasn't been stored yet.
  PathPool::DirPath tryStoreDirPath(llvm::StringRef dirStr);
};

} // namespace index
} // namespace clang

#endif // LLVM_CLANG_INDEXSERIALIZATION_SERIALIZABLEPATHCOLLECTION_H
