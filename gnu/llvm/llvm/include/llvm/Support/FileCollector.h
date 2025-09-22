//===-- FileCollector.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FILECOLLECTOR_H
#define LLVM_SUPPORT_FILECOLLECTOR_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <mutex>
#include <string>

namespace llvm {
class FileCollectorFileSystem;
class Twine;

class FileCollectorBase {
public:
  FileCollectorBase();
  virtual ~FileCollectorBase();

  void addFile(const Twine &file);
  void addDirectory(const Twine &Dir);

protected:
  bool markAsSeen(StringRef Path) {
    if (Path.empty())
      return false;
    return Seen.insert(Path).second;
  }

  virtual void addFileImpl(StringRef SrcPath) = 0;

  virtual llvm::vfs::directory_iterator
  addDirectoryImpl(const llvm::Twine &Dir,
                   IntrusiveRefCntPtr<vfs::FileSystem> FS,
                   std::error_code &EC) = 0;

  /// Synchronizes access to internal data structures.
  std::mutex Mutex;

  /// Tracks already seen files so they can be skipped.
  StringSet<> Seen;
};

/// Captures file system interaction and generates data to be later replayed
/// with the RedirectingFileSystem.
///
/// For any file that gets accessed we eventually create:
/// - a copy of the file inside Root
/// - a record in RedirectingFileSystem mapping that maps:
///   current real path -> path to the copy in Root
///
/// That intent is that later when the mapping is used by RedirectingFileSystem
/// it simulates the state of FS that we collected.
///
/// We generate file copies and mapping lazily - see writeMapping and copyFiles.
/// We don't try to capture the state of the file at the exact time when it's
/// accessed. Files might get changed, deleted ... we record only the "final"
/// state.
///
/// In order to preserve the relative topology of files we use their real paths
/// as relative paths inside of the Root.
class FileCollector : public FileCollectorBase {
public:
  /// Helper utility that encapsulates the logic for canonicalizing a virtual
  /// path and a path to copy from.
  class PathCanonicalizer {
  public:
    struct PathStorage {
      SmallString<256> CopyFrom;
      SmallString<256> VirtualPath;
    };

    /// Canonicalize a pair of virtual and real paths.
    PathStorage canonicalize(StringRef SrcPath);

  private:
    /// Replace with a (mostly) real path, or don't modify. Resolves symlinks
    /// in the directory, using \a CachedDirs to avoid redundant lookups, but
    /// leaves the filename as a possible symlink.
    void updateWithRealPath(SmallVectorImpl<char> &Path);

    StringMap<std::string> CachedDirs;
  };

  /// \p Root is the directory where collected files are will be stored.
  /// \p OverlayRoot is VFS mapping root.
  /// \p Root directory gets created in copyFiles unless it already exists.
  FileCollector(std::string Root, std::string OverlayRoot);

  /// Write the yaml mapping (for the VFS) to the given file.
  std::error_code writeMapping(StringRef MappingFile);

  /// Copy the files into the root directory.
  ///
  /// When StopOnError is true (the default) we abort as soon as one file
  /// cannot be copied. This is relatively common, for example when a file was
  /// removed after it was added to the mapping.
  std::error_code copyFiles(bool StopOnError = true);

  /// Create a VFS that uses \p Collector to collect files accessed via \p
  /// BaseFS.
  static IntrusiveRefCntPtr<vfs::FileSystem>
  createCollectorVFS(IntrusiveRefCntPtr<vfs::FileSystem> BaseFS,
                     std::shared_ptr<FileCollector> Collector);

private:
  friend FileCollectorFileSystem;

  void addFileToMapping(StringRef VirtualPath, StringRef RealPath) {
    if (sys::fs::is_directory(VirtualPath))
      VFSWriter.addDirectoryMapping(VirtualPath, RealPath);
    else
      VFSWriter.addFileMapping(VirtualPath, RealPath);
  }

protected:
  void addFileImpl(StringRef SrcPath) override;

  llvm::vfs::directory_iterator
  addDirectoryImpl(const llvm::Twine &Dir,
                   IntrusiveRefCntPtr<vfs::FileSystem> FS,
                   std::error_code &EC) override;

  /// The directory where collected files are copied to in copyFiles().
  const std::string Root;

  /// The root directory where the VFS overlay lives.
  const std::string OverlayRoot;

  /// The yaml mapping writer.
  vfs::YAMLVFSWriter VFSWriter;

  /// Helper utility for canonicalizing paths.
  PathCanonicalizer Canonicalizer;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_FILECOLLECTOR_H
