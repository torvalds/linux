//===--- SerializablePathCollection.cpp -- Index of paths -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/IndexSerialization/SerializablePathCollection.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace clang;
using namespace clang::index;

StringPool::StringOffsetSize StringPool::add(StringRef Str) {
  const std::size_t Offset = Buffer.size();
  Buffer += Str;
  return StringPool::StringOffsetSize(Offset, Str.size());
}

size_t PathPool::addFilePath(RootDirKind Root,
                             const StringPool::StringOffsetSize &Dir,
                             StringRef Filename) {
  FilePaths.emplace_back(DirPath(Root, Dir), Paths.add(Filename));
  return FilePaths.size() - 1;
}

StringPool::StringOffsetSize PathPool::addDirPath(StringRef Dir) {
  return Paths.add(Dir);
}

llvm::ArrayRef<PathPool::FilePath> PathPool::getFilePaths() const {
  return FilePaths;
}

StringRef PathPool::getPaths() const { return Paths.getBuffer(); }

SerializablePathCollection::SerializablePathCollection(
    StringRef CurrentWorkDir, StringRef SysRoot, llvm::StringRef OutputFile)
    : WorkDir(CurrentWorkDir),
      SysRoot(llvm::sys::path::parent_path(SysRoot).empty() ? StringRef()
                                                            : SysRoot),
      WorkDirPath(Paths.addDirPath(WorkDir)),
      SysRootPath(Paths.addDirPath(SysRoot)),
      OutputFilePath(Paths.addDirPath(OutputFile)) {}

size_t SerializablePathCollection::tryStoreFilePath(FileEntryRef FE) {
  auto FileIt = UniqueFiles.find(FE);
  if (FileIt != UniqueFiles.end())
    return FileIt->second;

  const auto Dir = tryStoreDirPath(sys::path::parent_path(FE.getName()));
  const auto FileIdx =
      Paths.addFilePath(Dir.Root, Dir.Path, sys::path::filename(FE.getName()));

  UniqueFiles.try_emplace(FE, FileIdx);
  return FileIdx;
}

PathPool::DirPath SerializablePathCollection::tryStoreDirPath(StringRef Dir) {
  // We don't want to strip separator if Dir is "/" - so we check size > 1.
  while (Dir.size() > 1 && llvm::sys::path::is_separator(Dir.back()))
    Dir = Dir.drop_back();

  auto DirIt = UniqueDirs.find(Dir);
  if (DirIt != UniqueDirs.end())
    return DirIt->second;

  const std::string OrigDir = Dir.str();

  PathPool::RootDirKind Root = PathPool::RootDirKind::Regular;
  if (!SysRoot.empty() && Dir.starts_with(SysRoot) &&
      llvm::sys::path::is_separator(Dir[SysRoot.size()])) {
    Root = PathPool::RootDirKind::SysRoot;
    Dir = Dir.drop_front(SysRoot.size());
  } else if (!WorkDir.empty() && Dir.starts_with(WorkDir) &&
             llvm::sys::path::is_separator(Dir[WorkDir.size()])) {
    Root = PathPool::RootDirKind::CurrentWorkDir;
    Dir = Dir.drop_front(WorkDir.size());
  }

  if (Root != PathPool::RootDirKind::Regular) {
    while (!Dir.empty() && llvm::sys::path::is_separator(Dir.front()))
      Dir = Dir.drop_front();
  }

  PathPool::DirPath Result(Root, Paths.addDirPath(Dir));
  UniqueDirs.try_emplace(OrigDir, Result);
  return Result;
}
