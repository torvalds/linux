//===--- FileManager.h - File System Probing and Caching --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::FileManager interface and associated types.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_FILEMANAGER_H
#define LLVM_CLANG_BASIC_FILEMANAGER_H

#include "clang/Basic/DirectoryEntry.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <ctime>
#include <map>
#include <memory>
#include <string>

namespace llvm {

class MemoryBuffer;

} // end namespace llvm

namespace clang {

class FileSystemStatCache;

/// Implements support for file system lookup, file system caching,
/// and directory search management.
///
/// This also handles more advanced properties, such as uniquing files based
/// on "inode", so that a file with two names (e.g. symlinked) will be treated
/// as a single file.
///
class FileManager : public RefCountedBase<FileManager> {
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;
  FileSystemOptions FileSystemOpts;
  llvm::SpecificBumpPtrAllocator<FileEntry> FilesAlloc;
  llvm::SpecificBumpPtrAllocator<DirectoryEntry> DirsAlloc;

  /// Cache for existing real directories.
  llvm::DenseMap<llvm::sys::fs::UniqueID, DirectoryEntry *> UniqueRealDirs;

  /// Cache for existing real files.
  llvm::DenseMap<llvm::sys::fs::UniqueID, FileEntry *> UniqueRealFiles;

  /// The virtual directories that we have allocated.
  ///
  /// For each virtual file (e.g. foo/bar/baz.cpp), we add all of its parent
  /// directories (foo/ and foo/bar/) here.
  SmallVector<DirectoryEntry *, 4> VirtualDirectoryEntries;
  /// The virtual files that we have allocated.
  SmallVector<FileEntry *, 4> VirtualFileEntries;

  /// A set of files that bypass the maps and uniquing.  They can have
  /// conflicting filenames.
  SmallVector<FileEntry *, 0> BypassFileEntries;

  /// A cache that maps paths to directory entries (either real or
  /// virtual) we have looked up, or an error that occurred when we looked up
  /// the directory.
  ///
  /// The actual Entries for real directories/files are
  /// owned by UniqueRealDirs/UniqueRealFiles above, while the Entries
  /// for virtual directories/files are owned by
  /// VirtualDirectoryEntries/VirtualFileEntries above.
  ///
  llvm::StringMap<llvm::ErrorOr<DirectoryEntry &>, llvm::BumpPtrAllocator>
  SeenDirEntries;

  /// A cache that maps paths to file entries (either real or
  /// virtual) we have looked up, or an error that occurred when we looked up
  /// the file.
  ///
  /// \see SeenDirEntries
  llvm::StringMap<llvm::ErrorOr<FileEntryRef::MapValue>, llvm::BumpPtrAllocator>
      SeenFileEntries;

  /// A mirror of SeenFileEntries to give fake answers for getBypassFile().
  ///
  /// Don't bother hooking up a BumpPtrAllocator. This should be rarely used,
  /// and only on error paths.
  std::unique_ptr<llvm::StringMap<llvm::ErrorOr<FileEntryRef::MapValue>>>
      SeenBypassFileEntries;

  /// The file entry for stdin, if it has been accessed through the FileManager.
  OptionalFileEntryRef STDIN;

  /// The canonical names of files and directories .
  llvm::DenseMap<const void *, llvm::StringRef> CanonicalNames;

  /// Storage for canonical names that we have computed.
  llvm::BumpPtrAllocator CanonicalNameStorage;

  /// Each FileEntry we create is assigned a unique ID #.
  ///
  unsigned NextFileUID;

  /// Statistics gathered during the lifetime of the FileManager.
  unsigned NumDirLookups = 0;
  unsigned NumFileLookups = 0;
  unsigned NumDirCacheMisses = 0;
  unsigned NumFileCacheMisses = 0;

  // Caching.
  std::unique_ptr<FileSystemStatCache> StatCache;

  std::error_code getStatValue(StringRef Path, llvm::vfs::Status &Status,
                               bool isFile,
                               std::unique_ptr<llvm::vfs::File> *F);

  /// Add all ancestors of the given path (pointing to either a file
  /// or a directory) as virtual directories.
  void addAncestorsAsVirtualDirs(StringRef Path);

  /// Fills the RealPathName in file entry.
  void fillRealPathName(FileEntry *UFE, llvm::StringRef FileName);

public:
  /// Construct a file manager, optionally with a custom VFS.
  ///
  /// \param FS if non-null, the VFS to use.  Otherwise uses
  /// llvm::vfs::getRealFileSystem().
  FileManager(const FileSystemOptions &FileSystemOpts,
              IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = nullptr);
  ~FileManager();

  /// Installs the provided FileSystemStatCache object within
  /// the FileManager.
  ///
  /// Ownership of this object is transferred to the FileManager.
  ///
  /// \param statCache the new stat cache to install. Ownership of this
  /// object is transferred to the FileManager.
  void setStatCache(std::unique_ptr<FileSystemStatCache> statCache);

  /// Removes the FileSystemStatCache object from the manager.
  void clearStatCache();

  /// Returns the number of unique real file entries cached by the file manager.
  size_t getNumUniqueRealFiles() const { return UniqueRealFiles.size(); }

  /// Lookup, cache, and verify the specified directory (real or
  /// virtual).
  ///
  /// This returns a \c std::error_code if there was an error reading the
  /// directory. On success, returns the reference to the directory entry
  /// together with the exact path that was used to access a file by a
  /// particular call to getDirectoryRef.
  ///
  /// \param CacheFailure If true and the file does not exist, we'll cache
  /// the failure to find this file.
  llvm::Expected<DirectoryEntryRef> getDirectoryRef(StringRef DirName,
                                                    bool CacheFailure = true);

  /// Get a \c DirectoryEntryRef if it exists, without doing anything on error.
  OptionalDirectoryEntryRef getOptionalDirectoryRef(StringRef DirName,
                                                    bool CacheFailure = true) {
    return llvm::expectedToOptional(getDirectoryRef(DirName, CacheFailure));
  }

  /// Lookup, cache, and verify the specified directory (real or
  /// virtual).
  ///
  /// This function is deprecated and will be removed at some point in the
  /// future, new clients should use
  ///  \c getDirectoryRef.
  ///
  /// This returns a \c std::error_code if there was an error reading the
  /// directory. If there is no error, the DirectoryEntry is guaranteed to be
  /// non-NULL.
  ///
  /// \param CacheFailure If true and the file does not exist, we'll cache
  /// the failure to find this file.
  llvm::ErrorOr<const DirectoryEntry *>
  getDirectory(StringRef DirName, bool CacheFailure = true);

  /// Lookup, cache, and verify the specified file (real or
  /// virtual).
  ///
  /// This function is deprecated and will be removed at some point in the
  /// future, new clients should use
  ///  \c getFileRef.
  ///
  /// This returns a \c std::error_code if there was an error loading the file.
  /// If there is no error, the FileEntry is guaranteed to be non-NULL.
  ///
  /// \param OpenFile if true and the file exists, it will be opened.
  ///
  /// \param CacheFailure If true and the file does not exist, we'll cache
  /// the failure to find this file.
  llvm::ErrorOr<const FileEntry *>
  getFile(StringRef Filename, bool OpenFile = false, bool CacheFailure = true);

  /// Lookup, cache, and verify the specified file (real or virtual). Return the
  /// reference to the file entry together with the exact path that was used to
  /// access a file by a particular call to getFileRef. If the underlying VFS is
  /// a redirecting VFS that uses external file names, the returned FileEntryRef
  /// will use the external name instead of the filename that was passed to this
  /// method.
  ///
  /// This returns a \c std::error_code if there was an error loading the file,
  /// or a \c FileEntryRef otherwise.
  ///
  /// \param OpenFile if true and the file exists, it will be opened.
  ///
  /// \param CacheFailure If true and the file does not exist, we'll cache
  /// the failure to find this file.
  llvm::Expected<FileEntryRef> getFileRef(StringRef Filename,
                                          bool OpenFile = false,
                                          bool CacheFailure = true);

  /// Get the FileEntryRef for stdin, returning an error if stdin cannot be
  /// read.
  ///
  /// This reads and caches stdin before returning. Subsequent calls return the
  /// same file entry, and a reference to the cached input is returned by calls
  /// to getBufferForFile.
  llvm::Expected<FileEntryRef> getSTDIN();

  /// Get a FileEntryRef if it exists, without doing anything on error.
  OptionalFileEntryRef getOptionalFileRef(StringRef Filename,
                                          bool OpenFile = false,
                                          bool CacheFailure = true) {
    return llvm::expectedToOptional(
        getFileRef(Filename, OpenFile, CacheFailure));
  }

  /// Returns the current file system options
  FileSystemOptions &getFileSystemOpts() { return FileSystemOpts; }
  const FileSystemOptions &getFileSystemOpts() const { return FileSystemOpts; }

  llvm::vfs::FileSystem &getVirtualFileSystem() const { return *FS; }
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
  getVirtualFileSystemPtr() const {
    return FS;
  }

  /// Enable or disable tracking of VFS usage. Used to not track full header
  /// search and implicit modulemap lookup.
  void trackVFSUsage(bool Active);

  void setVirtualFileSystem(IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS) {
    this->FS = std::move(FS);
  }

  /// Retrieve a file entry for a "virtual" file that acts as
  /// if there were a file with the given name on disk.
  ///
  /// The file itself is not accessed.
  FileEntryRef getVirtualFileRef(StringRef Filename, off_t Size,
                                 time_t ModificationTime);

  const FileEntry *getVirtualFile(StringRef Filename, off_t Size,
                                  time_t ModificationTime);

  /// Retrieve a FileEntry that bypasses VFE, which is expected to be a virtual
  /// file entry, to access the real file.  The returned FileEntry will have
  /// the same filename as FE but a different identity and its own stat.
  ///
  /// This should be used only for rare error recovery paths because it
  /// bypasses all mapping and uniquing, blindly creating a new FileEntry.
  /// There is no attempt to deduplicate these; if you bypass the same file
  /// twice, you get two new file entries.
  OptionalFileEntryRef getBypassFile(FileEntryRef VFE);

  /// Open the specified file as a MemoryBuffer, returning a new
  /// MemoryBuffer if successful, otherwise returning null.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(FileEntryRef Entry, bool isVolatile = false,
                   bool RequiresNullTerminator = true,
                   std::optional<int64_t> MaybeLimit = std::nullopt);
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(StringRef Filename, bool isVolatile = false,
                   bool RequiresNullTerminator = true,
                   std::optional<int64_t> MaybeLimit = std::nullopt) const {
    return getBufferForFileImpl(Filename,
                                /*FileSize=*/(MaybeLimit ? *MaybeLimit : -1),
                                isVolatile, RequiresNullTerminator);
  }

private:
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFileImpl(StringRef Filename, int64_t FileSize, bool isVolatile,
                       bool RequiresNullTerminator) const;

  DirectoryEntry *&getRealDirEntry(const llvm::vfs::Status &Status);

public:
  /// Get the 'stat' information for the given \p Path.
  ///
  /// If the path is relative, it will be resolved against the WorkingDir of the
  /// FileManager's FileSystemOptions.
  ///
  /// \returns a \c std::error_code describing an error, if there was one
  std::error_code getNoncachedStatValue(StringRef Path,
                                        llvm::vfs::Status &Result);

  /// If path is not absolute and FileSystemOptions set the working
  /// directory, the path is modified to be relative to the given
  /// working directory.
  /// \returns true if \c path changed.
  bool FixupRelativePath(SmallVectorImpl<char> &path) const;

  /// Makes \c Path absolute taking into account FileSystemOptions and the
  /// working directory option.
  /// \returns true if \c Path changed to absolute.
  bool makeAbsolutePath(SmallVectorImpl<char> &Path) const;

  /// Produce an array mapping from the unique IDs assigned to each
  /// file to the corresponding FileEntryRef.
  void
  GetUniqueIDMapping(SmallVectorImpl<OptionalFileEntryRef> &UIDToFiles) const;

  /// Retrieve the canonical name for a given directory.
  ///
  /// This is a very expensive operation, despite its results being cached,
  /// and should only be used when the physical layout of the file system is
  /// required, which is (almost) never.
  StringRef getCanonicalName(DirectoryEntryRef Dir);

  /// Retrieve the canonical name for a given file.
  ///
  /// This is a very expensive operation, despite its results being cached,
  /// and should only be used when the physical layout of the file system is
  /// required, which is (almost) never.
  StringRef getCanonicalName(FileEntryRef File);

private:
  /// Retrieve the canonical name for a given file or directory.
  ///
  /// The first param is a key in the CanonicalNames array.
  StringRef getCanonicalName(const void *Entry, StringRef Name);

public:
  void PrintStats() const;

  /// Import statistics from a child FileManager and add them to this current
  /// FileManager.
  void AddStats(const FileManager &Other);
};

} // end namespace clang

#endif // LLVM_CLANG_BASIC_FILEMANAGER_H
