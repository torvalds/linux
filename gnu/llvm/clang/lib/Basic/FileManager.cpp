//===--- FileManager.cpp - File System Probing and Caching ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the FileManager interface.
//
//===----------------------------------------------------------------------===//
//
// TODO: This should index all interesting directories with dirent calls.
//  getdirentries ?
//  opendir/readdir_r/closedir ?
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemStatCache.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

using namespace clang;

#define DEBUG_TYPE "file-search"

//===----------------------------------------------------------------------===//
// Common logic.
//===----------------------------------------------------------------------===//

FileManager::FileManager(const FileSystemOptions &FSO,
                         IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : FS(std::move(FS)), FileSystemOpts(FSO), SeenDirEntries(64),
      SeenFileEntries(64), NextFileUID(0) {
  // If the caller doesn't provide a virtual file system, just grab the real
  // file system.
  if (!this->FS)
    this->FS = llvm::vfs::getRealFileSystem();
}

FileManager::~FileManager() = default;

void FileManager::setStatCache(std::unique_ptr<FileSystemStatCache> statCache) {
  assert(statCache && "No stat cache provided?");
  StatCache = std::move(statCache);
}

void FileManager::clearStatCache() { StatCache.reset(); }

/// Retrieve the directory that the given file name resides in.
/// Filename can point to either a real file or a virtual file.
static llvm::Expected<DirectoryEntryRef>
getDirectoryFromFile(FileManager &FileMgr, StringRef Filename,
                     bool CacheFailure) {
  if (Filename.empty())
    return llvm::errorCodeToError(
        make_error_code(std::errc::no_such_file_or_directory));

  if (llvm::sys::path::is_separator(Filename[Filename.size() - 1]))
    return llvm::errorCodeToError(make_error_code(std::errc::is_a_directory));

  StringRef DirName = llvm::sys::path::parent_path(Filename);
  // Use the current directory if file has no path component.
  if (DirName.empty())
    DirName = ".";

  return FileMgr.getDirectoryRef(DirName, CacheFailure);
}

DirectoryEntry *&FileManager::getRealDirEntry(const llvm::vfs::Status &Status) {
  assert(Status.isDirectory() && "The directory should exist!");
  // See if we have already opened a directory with the
  // same inode (this occurs on Unix-like systems when one dir is
  // symlinked to another, for example) or the same path (on
  // Windows).
  DirectoryEntry *&UDE = UniqueRealDirs[Status.getUniqueID()];

  if (!UDE) {
    // We don't have this directory yet, add it.  We use the string
    // key from the SeenDirEntries map as the string.
    UDE = new (DirsAlloc.Allocate()) DirectoryEntry();
  }
  return UDE;
}

/// Add all ancestors of the given path (pointing to either a file or
/// a directory) as virtual directories.
void FileManager::addAncestorsAsVirtualDirs(StringRef Path) {
  StringRef DirName = llvm::sys::path::parent_path(Path);
  if (DirName.empty())
    DirName = ".";

  auto &NamedDirEnt = *SeenDirEntries.insert(
        {DirName, std::errc::no_such_file_or_directory}).first;

  // When caching a virtual directory, we always cache its ancestors
  // at the same time.  Therefore, if DirName is already in the cache,
  // we don't need to recurse as its ancestors must also already be in
  // the cache (or it's a known non-virtual directory).
  if (NamedDirEnt.second)
    return;

  // Check to see if the directory exists.
  llvm::vfs::Status Status;
  auto statError =
      getStatValue(DirName, Status, false, nullptr /*directory lookup*/);
  if (statError) {
    // There's no real directory at the given path.
    // Add the virtual directory to the cache.
    auto *UDE = new (DirsAlloc.Allocate()) DirectoryEntry();
    NamedDirEnt.second = *UDE;
    VirtualDirectoryEntries.push_back(UDE);
  } else {
    // There is the real directory
    DirectoryEntry *&UDE = getRealDirEntry(Status);
    NamedDirEnt.second = *UDE;
  }

  // Recursively add the other ancestors.
  addAncestorsAsVirtualDirs(DirName);
}

llvm::Expected<DirectoryEntryRef>
FileManager::getDirectoryRef(StringRef DirName, bool CacheFailure) {
  // stat doesn't like trailing separators except for root directory.
  // At least, on Win32 MSVCRT, stat() cannot strip trailing '/'.
  // (though it can strip '\\')
  if (DirName.size() > 1 &&
      DirName != llvm::sys::path::root_path(DirName) &&
      llvm::sys::path::is_separator(DirName.back()))
    DirName = DirName.substr(0, DirName.size()-1);
  std::optional<std::string> DirNameStr;
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // Fixing a problem with "clang C:test.c" on Windows.
    // Stat("C:") does not recognize "C:" as a valid directory
    if (DirName.size() > 1 && DirName.back() == ':' &&
        DirName.equals_insensitive(llvm::sys::path::root_name(DirName))) {
      DirNameStr = DirName.str() + '.';
      DirName = *DirNameStr;
    }
  }

  ++NumDirLookups;

  // See if there was already an entry in the map.  Note that the map
  // contains both virtual and real directories.
  auto SeenDirInsertResult =
      SeenDirEntries.insert({DirName, std::errc::no_such_file_or_directory});
  if (!SeenDirInsertResult.second) {
    if (SeenDirInsertResult.first->second)
      return DirectoryEntryRef(*SeenDirInsertResult.first);
    return llvm::errorCodeToError(SeenDirInsertResult.first->second.getError());
  }

  // We've not seen this before. Fill it in.
  ++NumDirCacheMisses;
  auto &NamedDirEnt = *SeenDirInsertResult.first;
  assert(!NamedDirEnt.second && "should be newly-created");

  // Get the null-terminated directory name as stored as the key of the
  // SeenDirEntries map.
  StringRef InterndDirName = NamedDirEnt.first();

  // Check to see if the directory exists.
  llvm::vfs::Status Status;
  auto statError = getStatValue(InterndDirName, Status, false,
                                nullptr /*directory lookup*/);
  if (statError) {
    // There's no real directory at the given path.
    if (CacheFailure)
      NamedDirEnt.second = statError;
    else
      SeenDirEntries.erase(DirName);
    return llvm::errorCodeToError(statError);
  }

  // It exists.
  DirectoryEntry *&UDE = getRealDirEntry(Status);
  NamedDirEnt.second = *UDE;

  return DirectoryEntryRef(NamedDirEnt);
}

llvm::ErrorOr<const DirectoryEntry *>
FileManager::getDirectory(StringRef DirName, bool CacheFailure) {
  auto Result = getDirectoryRef(DirName, CacheFailure);
  if (Result)
    return &Result->getDirEntry();
  return llvm::errorToErrorCode(Result.takeError());
}

llvm::ErrorOr<const FileEntry *>
FileManager::getFile(StringRef Filename, bool openFile, bool CacheFailure) {
  auto Result = getFileRef(Filename, openFile, CacheFailure);
  if (Result)
    return &Result->getFileEntry();
  return llvm::errorToErrorCode(Result.takeError());
}

llvm::Expected<FileEntryRef>
FileManager::getFileRef(StringRef Filename, bool openFile, bool CacheFailure) {
  ++NumFileLookups;

  // See if there is already an entry in the map.
  auto SeenFileInsertResult =
      SeenFileEntries.insert({Filename, std::errc::no_such_file_or_directory});
  if (!SeenFileInsertResult.second) {
    if (!SeenFileInsertResult.first->second)
      return llvm::errorCodeToError(
          SeenFileInsertResult.first->second.getError());
    return FileEntryRef(*SeenFileInsertResult.first);
  }

  // We've not seen this before. Fill it in.
  ++NumFileCacheMisses;
  auto *NamedFileEnt = &*SeenFileInsertResult.first;
  assert(!NamedFileEnt->second && "should be newly-created");

  // Get the null-terminated file name as stored as the key of the
  // SeenFileEntries map.
  StringRef InterndFileName = NamedFileEnt->first();

  // Look up the directory for the file.  When looking up something like
  // sys/foo.h we'll discover all of the search directories that have a 'sys'
  // subdirectory.  This will let us avoid having to waste time on known-to-fail
  // searches when we go to find sys/bar.h, because all the search directories
  // without a 'sys' subdir will get a cached failure result.
  auto DirInfoOrErr = getDirectoryFromFile(*this, Filename, CacheFailure);
  if (!DirInfoOrErr) { // Directory doesn't exist, file can't exist.
    std::error_code Err = errorToErrorCode(DirInfoOrErr.takeError());
    if (CacheFailure)
      NamedFileEnt->second = Err;
    else
      SeenFileEntries.erase(Filename);

    return llvm::errorCodeToError(Err);
  }
  DirectoryEntryRef DirInfo = *DirInfoOrErr;

  // FIXME: Use the directory info to prune this, before doing the stat syscall.
  // FIXME: This will reduce the # syscalls.

  // Check to see if the file exists.
  std::unique_ptr<llvm::vfs::File> F;
  llvm::vfs::Status Status;
  auto statError = getStatValue(InterndFileName, Status, true,
                                openFile ? &F : nullptr);
  if (statError) {
    // There's no real file at the given path.
    if (CacheFailure)
      NamedFileEnt->second = statError;
    else
      SeenFileEntries.erase(Filename);

    return llvm::errorCodeToError(statError);
  }

  assert((openFile || !F) && "undesired open file");

  // It exists.  See if we have already opened a file with the same inode.
  // This occurs when one dir is symlinked to another, for example.
  FileEntry *&UFE = UniqueRealFiles[Status.getUniqueID()];
  bool ReusingEntry = UFE != nullptr;
  if (!UFE)
    UFE = new (FilesAlloc.Allocate()) FileEntry();

  if (!Status.ExposesExternalVFSPath || Status.getName() == Filename) {
    // Use the requested name. Set the FileEntry.
    NamedFileEnt->second = FileEntryRef::MapValue(*UFE, DirInfo);
  } else {
    // Name mismatch. We need a redirect. First grab the actual entry we want
    // to return.
    //
    // This redirection logic intentionally leaks the external name of a
    // redirected file that uses 'use-external-name' in \a
    // vfs::RedirectionFileSystem. This allows clang to report the external
    // name to users (in diagnostics) and to tools that don't have access to
    // the VFS (in debug info and dependency '.d' files).
    //
    // FIXME: This is pretty complex and has some very complicated interactions
    // with the rest of clang. It's also inconsistent with how "real"
    // filesystems behave and confuses parts of clang expect to see the
    // name-as-accessed on the \a FileEntryRef.
    //
    // A potential plan to remove this is as follows -
    //   - Update callers such as `HeaderSearch::findUsableModuleForHeader()`
    //     to explicitly use the `getNameAsRequested()` rather than just using
    //     `getName()`.
    //   - Add a `FileManager::getExternalPath` API for explicitly getting the
    //     remapped external filename when there is one available. Adopt it in
    //     callers like diagnostics/deps reporting instead of calling
    //     `getName()` directly.
    //   - Switch the meaning of `FileEntryRef::getName()` to get the requested
    //     name, not the external name. Once that sticks, revert callers that
    //     want the requested name back to calling `getName()`.
    //   - Update the VFS to always return the requested name. This could also
    //     return the external name, or just have an API to request it
    //     lazily. The latter has the benefit of making accesses of the
    //     external path easily tracked, but may also require extra work than
    //     just returning up front.
    //   - (Optionally) Add an API to VFS to get the external filename lazily
    //     and update `FileManager::getExternalPath()` to use it instead. This
    //     has the benefit of making such accesses easily tracked, though isn't
    //     necessarily required (and could cause extra work than just adding to
    //     eg. `vfs::Status` up front).
    auto &Redirection =
        *SeenFileEntries
             .insert({Status.getName(), FileEntryRef::MapValue(*UFE, DirInfo)})
             .first;
    assert(Redirection.second->V.is<FileEntry *>() &&
           "filename redirected to a non-canonical filename?");
    assert(Redirection.second->V.get<FileEntry *>() == UFE &&
           "filename from getStatValue() refers to wrong file");

    // Cache the redirection in the previously-inserted entry, still available
    // in the tentative return value.
    NamedFileEnt->second = FileEntryRef::MapValue(Redirection, DirInfo);
  }

  FileEntryRef ReturnedRef(*NamedFileEnt);
  if (ReusingEntry) { // Already have an entry with this inode, return it.
    return ReturnedRef;
  }

  // Otherwise, we don't have this file yet, add it.
  UFE->Size = Status.getSize();
  UFE->ModTime = llvm::sys::toTimeT(Status.getLastModificationTime());
  UFE->Dir = &DirInfo.getDirEntry();
  UFE->UID = NextFileUID++;
  UFE->UniqueID = Status.getUniqueID();
  UFE->IsNamedPipe = Status.getType() == llvm::sys::fs::file_type::fifo_file;
  UFE->File = std::move(F);

  if (UFE->File) {
    if (auto PathName = UFE->File->getName())
      fillRealPathName(UFE, *PathName);
  } else if (!openFile) {
    // We should still fill the path even if we aren't opening the file.
    fillRealPathName(UFE, InterndFileName);
  }
  return ReturnedRef;
}

llvm::Expected<FileEntryRef> FileManager::getSTDIN() {
  // Only read stdin once.
  if (STDIN)
    return *STDIN;

  std::unique_ptr<llvm::MemoryBuffer> Content;
  if (auto ContentOrError = llvm::MemoryBuffer::getSTDIN())
    Content = std::move(*ContentOrError);
  else
    return llvm::errorCodeToError(ContentOrError.getError());

  STDIN = getVirtualFileRef(Content->getBufferIdentifier(),
                            Content->getBufferSize(), 0);
  FileEntry &FE = const_cast<FileEntry &>(STDIN->getFileEntry());
  FE.Content = std::move(Content);
  FE.IsNamedPipe = true;
  return *STDIN;
}

void FileManager::trackVFSUsage(bool Active) {
  FS->visit([Active](llvm::vfs::FileSystem &FileSys) {
    if (auto *RFS = dyn_cast<llvm::vfs::RedirectingFileSystem>(&FileSys))
      RFS->setUsageTrackingActive(Active);
  });
}

const FileEntry *FileManager::getVirtualFile(StringRef Filename, off_t Size,
                                             time_t ModificationTime) {
  return &getVirtualFileRef(Filename, Size, ModificationTime).getFileEntry();
}

FileEntryRef FileManager::getVirtualFileRef(StringRef Filename, off_t Size,
                                            time_t ModificationTime) {
  ++NumFileLookups;

  // See if there is already an entry in the map for an existing file.
  auto &NamedFileEnt = *SeenFileEntries.insert(
      {Filename, std::errc::no_such_file_or_directory}).first;
  if (NamedFileEnt.second) {
    FileEntryRef::MapValue Value = *NamedFileEnt.second;
    if (LLVM_LIKELY(Value.V.is<FileEntry *>()))
      return FileEntryRef(NamedFileEnt);
    return FileEntryRef(*Value.V.get<const FileEntryRef::MapEntry *>());
  }

  // We've not seen this before, or the file is cached as non-existent.
  ++NumFileCacheMisses;
  addAncestorsAsVirtualDirs(Filename);
  FileEntry *UFE = nullptr;

  // Now that all ancestors of Filename are in the cache, the
  // following call is guaranteed to find the DirectoryEntry from the
  // cache. A virtual file can also have an empty filename, that could come
  // from a source location preprocessor directive with an empty filename as
  // an example, so we need to pretend it has a name to ensure a valid directory
  // entry can be returned.
  auto DirInfo = expectedToOptional(getDirectoryFromFile(
      *this, Filename.empty() ? "." : Filename, /*CacheFailure=*/true));
  assert(DirInfo &&
         "The directory of a virtual file should already be in the cache.");

  // Check to see if the file exists. If so, drop the virtual file
  llvm::vfs::Status Status;
  const char *InterndFileName = NamedFileEnt.first().data();
  if (!getStatValue(InterndFileName, Status, true, nullptr)) {
    Status = llvm::vfs::Status(
      Status.getName(), Status.getUniqueID(),
      llvm::sys::toTimePoint(ModificationTime),
      Status.getUser(), Status.getGroup(), Size,
      Status.getType(), Status.getPermissions());

    auto &RealFE = UniqueRealFiles[Status.getUniqueID()];
    if (RealFE) {
      // If we had already opened this file, close it now so we don't
      // leak the descriptor. We're not going to use the file
      // descriptor anyway, since this is a virtual file.
      if (RealFE->File)
        RealFE->closeFile();
      // If we already have an entry with this inode, return it.
      //
      // FIXME: Surely this should add a reference by the new name, and return
      // it instead...
      NamedFileEnt.second = FileEntryRef::MapValue(*RealFE, *DirInfo);
      return FileEntryRef(NamedFileEnt);
    }
    // File exists, but no entry - create it.
    RealFE = new (FilesAlloc.Allocate()) FileEntry();
    RealFE->UniqueID = Status.getUniqueID();
    RealFE->IsNamedPipe =
        Status.getType() == llvm::sys::fs::file_type::fifo_file;
    fillRealPathName(RealFE, Status.getName());

    UFE = RealFE;
  } else {
    // File does not exist, create a virtual entry.
    UFE = new (FilesAlloc.Allocate()) FileEntry();
    VirtualFileEntries.push_back(UFE);
  }

  NamedFileEnt.second = FileEntryRef::MapValue(*UFE, *DirInfo);
  UFE->Size    = Size;
  UFE->ModTime = ModificationTime;
  UFE->Dir     = &DirInfo->getDirEntry();
  UFE->UID     = NextFileUID++;
  UFE->File.reset();
  return FileEntryRef(NamedFileEnt);
}

OptionalFileEntryRef FileManager::getBypassFile(FileEntryRef VF) {
  // Stat of the file and return nullptr if it doesn't exist.
  llvm::vfs::Status Status;
  if (getStatValue(VF.getName(), Status, /*isFile=*/true, /*F=*/nullptr))
    return std::nullopt;

  if (!SeenBypassFileEntries)
    SeenBypassFileEntries = std::make_unique<
        llvm::StringMap<llvm::ErrorOr<FileEntryRef::MapValue>>>();

  // If we've already bypassed just use the existing one.
  auto Insertion = SeenBypassFileEntries->insert(
      {VF.getName(), std::errc::no_such_file_or_directory});
  if (!Insertion.second)
    return FileEntryRef(*Insertion.first);

  // Fill in the new entry from the stat.
  FileEntry *BFE = new (FilesAlloc.Allocate()) FileEntry();
  BypassFileEntries.push_back(BFE);
  Insertion.first->second = FileEntryRef::MapValue(*BFE, VF.getDir());
  BFE->Size = Status.getSize();
  BFE->Dir = VF.getFileEntry().Dir;
  BFE->ModTime = llvm::sys::toTimeT(Status.getLastModificationTime());
  BFE->UID = NextFileUID++;

  // Save the entry in the bypass table and return.
  return FileEntryRef(*Insertion.first);
}

bool FileManager::FixupRelativePath(SmallVectorImpl<char> &path) const {
  StringRef pathRef(path.data(), path.size());

  if (FileSystemOpts.WorkingDir.empty()
      || llvm::sys::path::is_absolute(pathRef))
    return false;

  SmallString<128> NewPath(FileSystemOpts.WorkingDir);
  llvm::sys::path::append(NewPath, pathRef);
  path = NewPath;
  return true;
}

bool FileManager::makeAbsolutePath(SmallVectorImpl<char> &Path) const {
  bool Changed = FixupRelativePath(Path);

  if (!llvm::sys::path::is_absolute(StringRef(Path.data(), Path.size()))) {
    FS->makeAbsolute(Path);
    Changed = true;
  }

  return Changed;
}

void FileManager::fillRealPathName(FileEntry *UFE, llvm::StringRef FileName) {
  llvm::SmallString<128> AbsPath(FileName);
  // This is not the same as `VFS::getRealPath()`, which resolves symlinks
  // but can be very expensive on real file systems.
  // FIXME: the semantic of RealPathName is unclear, and the name might be
  // misleading. We need to clean up the interface here.
  makeAbsolutePath(AbsPath);
  llvm::sys::path::remove_dots(AbsPath, /*remove_dot_dot=*/true);
  UFE->RealPathName = std::string(AbsPath);
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
FileManager::getBufferForFile(FileEntryRef FE, bool isVolatile,
                              bool RequiresNullTerminator,
                              std::optional<int64_t> MaybeLimit) {
  const FileEntry *Entry = &FE.getFileEntry();
  // If the content is living on the file entry, return a reference to it.
  if (Entry->Content)
    return llvm::MemoryBuffer::getMemBuffer(Entry->Content->getMemBufferRef());

  uint64_t FileSize = Entry->getSize();

  if (MaybeLimit)
    FileSize = *MaybeLimit;

  // If there's a high enough chance that the file have changed since we
  // got its size, force a stat before opening it.
  if (isVolatile || Entry->isNamedPipe())
    FileSize = -1;

  StringRef Filename = FE.getName();
  // If the file is already open, use the open file descriptor.
  if (Entry->File) {
    auto Result = Entry->File->getBuffer(Filename, FileSize,
                                         RequiresNullTerminator, isVolatile);
    Entry->closeFile();
    return Result;
  }

  // Otherwise, open the file.
  return getBufferForFileImpl(Filename, FileSize, isVolatile,
                              RequiresNullTerminator);
}

llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
FileManager::getBufferForFileImpl(StringRef Filename, int64_t FileSize,
                                  bool isVolatile,
                                  bool RequiresNullTerminator) const {
  if (FileSystemOpts.WorkingDir.empty())
    return FS->getBufferForFile(Filename, FileSize, RequiresNullTerminator,
                                isVolatile);

  SmallString<128> FilePath(Filename);
  FixupRelativePath(FilePath);
  return FS->getBufferForFile(FilePath, FileSize, RequiresNullTerminator,
                              isVolatile);
}

/// getStatValue - Get the 'stat' information for the specified path,
/// using the cache to accelerate it if possible.  This returns true
/// if the path points to a virtual file or does not exist, or returns
/// false if it's an existent real file.  If FileDescriptor is NULL,
/// do directory look-up instead of file look-up.
std::error_code
FileManager::getStatValue(StringRef Path, llvm::vfs::Status &Status,
                          bool isFile, std::unique_ptr<llvm::vfs::File> *F) {
  // FIXME: FileSystemOpts shouldn't be passed in here, all paths should be
  // absolute!
  if (FileSystemOpts.WorkingDir.empty())
    return FileSystemStatCache::get(Path, Status, isFile, F,
                                    StatCache.get(), *FS);

  SmallString<128> FilePath(Path);
  FixupRelativePath(FilePath);

  return FileSystemStatCache::get(FilePath.c_str(), Status, isFile, F,
                                  StatCache.get(), *FS);
}

std::error_code
FileManager::getNoncachedStatValue(StringRef Path,
                                   llvm::vfs::Status &Result) {
  SmallString<128> FilePath(Path);
  FixupRelativePath(FilePath);

  llvm::ErrorOr<llvm::vfs::Status> S = FS->status(FilePath.c_str());
  if (!S)
    return S.getError();
  Result = *S;
  return std::error_code();
}

void FileManager::GetUniqueIDMapping(
    SmallVectorImpl<OptionalFileEntryRef> &UIDToFiles) const {
  UIDToFiles.clear();
  UIDToFiles.resize(NextFileUID);

  for (const auto &Entry : SeenFileEntries) {
    // Only return files that exist and are not redirected.
    if (!Entry.getValue() || !Entry.getValue()->V.is<FileEntry *>())
      continue;
    FileEntryRef FE(Entry);
    // Add this file if it's the first one with the UID, or if its name is
    // better than the existing one.
    OptionalFileEntryRef &ExistingFE = UIDToFiles[FE.getUID()];
    if (!ExistingFE || FE.getName() < ExistingFE->getName())
      ExistingFE = FE;
  }
}

StringRef FileManager::getCanonicalName(DirectoryEntryRef Dir) {
  return getCanonicalName(Dir, Dir.getName());
}

StringRef FileManager::getCanonicalName(FileEntryRef File) {
  return getCanonicalName(File, File.getName());
}

StringRef FileManager::getCanonicalName(const void *Entry, StringRef Name) {
  llvm::DenseMap<const void *, llvm::StringRef>::iterator Known =
      CanonicalNames.find(Entry);
  if (Known != CanonicalNames.end())
    return Known->second;

  // Name comes from FileEntry/DirectoryEntry::getName(), so it is safe to
  // store it in the DenseMap below.
  StringRef CanonicalName(Name);

  SmallString<256> AbsPathBuf;
  SmallString<256> RealPathBuf;
  if (!FS->getRealPath(Name, RealPathBuf)) {
    if (is_style_windows(llvm::sys::path::Style::native)) {
      // For Windows paths, only use the real path if it doesn't resolve
      // a substitute drive, as those are used to avoid MAX_PATH issues.
      AbsPathBuf = Name;
      if (!FS->makeAbsolute(AbsPathBuf)) {
        if (llvm::sys::path::root_name(RealPathBuf) ==
            llvm::sys::path::root_name(AbsPathBuf)) {
          CanonicalName = RealPathBuf.str().copy(CanonicalNameStorage);
        } else {
          // Fallback to using the absolute path.
          // Simplifying /../ is semantically valid on Windows even in the
          // presence of symbolic links.
          llvm::sys::path::remove_dots(AbsPathBuf, /*remove_dot_dot=*/true);
          CanonicalName = AbsPathBuf.str().copy(CanonicalNameStorage);
        }
      }
    } else {
      CanonicalName = RealPathBuf.str().copy(CanonicalNameStorage);
    }
  }

  CanonicalNames.insert({Entry, CanonicalName});
  return CanonicalName;
}

void FileManager::AddStats(const FileManager &Other) {
  assert(&Other != this && "Collecting stats into the same FileManager");
  NumDirLookups += Other.NumDirLookups;
  NumFileLookups += Other.NumFileLookups;
  NumDirCacheMisses += Other.NumDirCacheMisses;
  NumFileCacheMisses += Other.NumFileCacheMisses;
}

void FileManager::PrintStats() const {
  llvm::errs() << "\n*** File Manager Stats:\n";
  llvm::errs() << UniqueRealFiles.size() << " real files found, "
               << UniqueRealDirs.size() << " real dirs found.\n";
  llvm::errs() << VirtualFileEntries.size() << " virtual files found, "
               << VirtualDirectoryEntries.size() << " virtual dirs found.\n";
  llvm::errs() << NumDirLookups << " dir lookups, "
               << NumDirCacheMisses << " dir cache misses.\n";
  llvm::errs() << NumFileLookups << " file lookups, "
               << NumFileCacheMisses << " file cache misses.\n";

  //llvm::errs() << PagesMapped << BytesOfPagesMapped << FSLookups;
}
