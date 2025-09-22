//===- DependencyScanningFilesystem.h - clang-scan-deps fs ===---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H
#define LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H

#include "clang/Basic/LLVM.h"
#include "clang/Lex/DependencyDirectivesScanner.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <mutex>
#include <optional>

namespace clang {
namespace tooling {
namespace dependencies {

using DependencyDirectivesTy =
    SmallVector<dependency_directives_scan::Directive, 20>;

/// Contents and directive tokens of a cached file entry. Single instance can
/// be shared between multiple entries.
struct CachedFileContents {
  CachedFileContents(std::unique_ptr<llvm::MemoryBuffer> Contents)
      : Original(std::move(Contents)), DepDirectives(nullptr) {}

  /// Owning storage for the original contents.
  std::unique_ptr<llvm::MemoryBuffer> Original;

  /// The mutex that must be locked before mutating directive tokens.
  std::mutex ValueLock;
  SmallVector<dependency_directives_scan::Token, 10> DepDirectiveTokens;
  /// Accessor to the directive tokens that's atomic to avoid data races.
  /// \p CachedFileContents has ownership of the pointer.
  std::atomic<const std::optional<DependencyDirectivesTy> *> DepDirectives;

  ~CachedFileContents() { delete DepDirectives.load(); }
};

/// An in-memory representation of a file system entity that is of interest to
/// the dependency scanning filesystem.
///
/// It represents one of the following:
/// - opened file with contents and a stat value,
/// - opened file with contents, directive tokens and a stat value,
/// - directory entry with its stat value,
/// - filesystem error.
///
/// Single instance of this class can be shared across different filenames (e.g.
/// a regular file and a symlink). For this reason the status filename is empty
/// and is only materialized by \c EntryRef that knows the requested filename.
class CachedFileSystemEntry {
public:
  /// Creates an entry without contents: either a filesystem error or
  /// a directory with stat value.
  CachedFileSystemEntry(llvm::ErrorOr<llvm::vfs::Status> Stat)
      : MaybeStat(std::move(Stat)), Contents(nullptr) {
    clearStatName();
  }

  /// Creates an entry representing a file with contents.
  CachedFileSystemEntry(llvm::ErrorOr<llvm::vfs::Status> Stat,
                        CachedFileContents *Contents)
      : MaybeStat(std::move(Stat)), Contents(std::move(Contents)) {
    clearStatName();
  }

  /// \returns True if the entry is a filesystem error.
  bool isError() const { return !MaybeStat; }

  /// \returns True if the current entry represents a directory.
  bool isDirectory() const { return !isError() && MaybeStat->isDirectory(); }

  /// \returns Original contents of the file.
  StringRef getOriginalContents() const {
    assert(!isError() && "error");
    assert(!MaybeStat->isDirectory() && "not a file");
    assert(Contents && "contents not initialized");
    return Contents->Original->getBuffer();
  }

  /// \returns The scanned preprocessor directive tokens of the file that are
  /// used to speed up preprocessing, if available.
  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  getDirectiveTokens() const {
    assert(!isError() && "error");
    assert(!isDirectory() && "not a file");
    assert(Contents && "contents not initialized");
    if (auto *Directives = Contents->DepDirectives.load()) {
      if (Directives->has_value())
        return ArrayRef<dependency_directives_scan::Directive>(**Directives);
    }
    return std::nullopt;
  }

  /// \returns The error.
  std::error_code getError() const { return MaybeStat.getError(); }

  /// \returns The entry status with empty filename.
  llvm::vfs::Status getStatus() const {
    assert(!isError() && "error");
    assert(MaybeStat->getName().empty() && "stat name must be empty");
    return *MaybeStat;
  }

  /// \returns The unique ID of the entry.
  llvm::sys::fs::UniqueID getUniqueID() const {
    assert(!isError() && "error");
    return MaybeStat->getUniqueID();
  }

  /// \returns The data structure holding both contents and directive tokens.
  CachedFileContents *getCachedContents() const {
    assert(!isError() && "error");
    assert(!isDirectory() && "not a file");
    return Contents;
  }

private:
  void clearStatName() {
    if (MaybeStat)
      MaybeStat = llvm::vfs::Status::copyWithNewName(*MaybeStat, "");
  }

  /// Either the filesystem error or status of the entry.
  /// The filename is empty and only materialized by \c EntryRef.
  llvm::ErrorOr<llvm::vfs::Status> MaybeStat;

  /// Non-owning pointer to the file contents.
  ///
  /// We're using pointer here to keep the size of this class small. Instances
  /// representing directories and filesystem errors don't hold any contents
  /// anyway.
  CachedFileContents *Contents;
};

using CachedRealPath = llvm::ErrorOr<std::string>;

/// This class is a shared cache, that caches the 'stat' and 'open' calls to the
/// underlying real file system, and the scanned preprocessor directives of
/// files.
///
/// It is sharded based on the hash of the key to reduce the lock contention for
/// the worker threads.
class DependencyScanningFilesystemSharedCache {
public:
  struct CacheShard {
    /// The mutex that needs to be locked before mutation of any member.
    mutable std::mutex CacheLock;

    /// Map from filenames to cached entries and real paths.
    llvm::StringMap<
        std::pair<const CachedFileSystemEntry *, const CachedRealPath *>,
        llvm::BumpPtrAllocator>
        CacheByFilename;

    /// Map from unique IDs to cached entries.
    llvm::DenseMap<llvm::sys::fs::UniqueID, const CachedFileSystemEntry *>
        EntriesByUID;

    /// The backing storage for cached entries.
    llvm::SpecificBumpPtrAllocator<CachedFileSystemEntry> EntryStorage;

    /// The backing storage for cached contents.
    llvm::SpecificBumpPtrAllocator<CachedFileContents> ContentsStorage;

    /// The backing storage for cached real paths.
    llvm::SpecificBumpPtrAllocator<CachedRealPath> RealPathStorage;

    /// Returns entry associated with the filename or nullptr if none is found.
    const CachedFileSystemEntry *findEntryByFilename(StringRef Filename) const;

    /// Returns entry associated with the unique ID or nullptr if none is found.
    const CachedFileSystemEntry *
    findEntryByUID(llvm::sys::fs::UniqueID UID) const;

    /// Returns entry associated with the filename if there is some. Otherwise,
    /// constructs new one with the given status, associates it with the
    /// filename and returns the result.
    const CachedFileSystemEntry &
    getOrEmplaceEntryForFilename(StringRef Filename,
                                 llvm::ErrorOr<llvm::vfs::Status> Stat);

    /// Returns entry associated with the unique ID if there is some. Otherwise,
    /// constructs new one with the given status and contents, associates it
    /// with the unique ID and returns the result.
    const CachedFileSystemEntry &
    getOrEmplaceEntryForUID(llvm::sys::fs::UniqueID UID, llvm::vfs::Status Stat,
                            std::unique_ptr<llvm::MemoryBuffer> Contents);

    /// Returns entry associated with the filename if there is some. Otherwise,
    /// associates the given entry with the filename and returns it.
    const CachedFileSystemEntry &
    getOrInsertEntryForFilename(StringRef Filename,
                                const CachedFileSystemEntry &Entry);

    /// Returns the real path associated with the filename or nullptr if none is
    /// found.
    const CachedRealPath *findRealPathByFilename(StringRef Filename) const;

    /// Returns the real path associated with the filename if there is some.
    /// Otherwise, constructs new one with the given one, associates it with the
    /// filename and returns the result.
    const CachedRealPath &
    getOrEmplaceRealPathForFilename(StringRef Filename,
                                    llvm::ErrorOr<StringRef> RealPath);
  };

  DependencyScanningFilesystemSharedCache();

  /// Returns shard for the given key.
  CacheShard &getShardForFilename(StringRef Filename) const;
  CacheShard &getShardForUID(llvm::sys::fs::UniqueID UID) const;

private:
  std::unique_ptr<CacheShard[]> CacheShards;
  unsigned NumShards;
};

/// This class is a local cache, that caches the 'stat' and 'open' calls to the
/// underlying real file system.
class DependencyScanningFilesystemLocalCache {
  llvm::StringMap<
      std::pair<const CachedFileSystemEntry *, const CachedRealPath *>,
      llvm::BumpPtrAllocator>
      Cache;

public:
  /// Returns entry associated with the filename or nullptr if none is found.
  const CachedFileSystemEntry *findEntryByFilename(StringRef Filename) const {
    assert(llvm::sys::path::is_absolute_gnu(Filename));
    auto It = Cache.find(Filename);
    return It == Cache.end() ? nullptr : It->getValue().first;
  }

  /// Associates the given entry with the filename and returns the given entry
  /// pointer (for convenience).
  const CachedFileSystemEntry &
  insertEntryForFilename(StringRef Filename,
                         const CachedFileSystemEntry &Entry) {
    assert(llvm::sys::path::is_absolute_gnu(Filename));
    auto [It, Inserted] = Cache.insert({Filename, {&Entry, nullptr}});
    auto &[CachedEntry, CachedRealPath] = It->getValue();
    if (!Inserted) {
      // The file is already present in the local cache. If we got here, it only
      // contains the real path. Let's make sure the entry is populated too.
      assert((!CachedEntry && CachedRealPath) && "entry already present");
      CachedEntry = &Entry;
    }
    return *CachedEntry;
  }

  /// Returns real path associated with the filename or nullptr if none is
  /// found.
  const CachedRealPath *findRealPathByFilename(StringRef Filename) const {
    assert(llvm::sys::path::is_absolute_gnu(Filename));
    auto It = Cache.find(Filename);
    return It == Cache.end() ? nullptr : It->getValue().second;
  }

  /// Associates the given real path with the filename and returns the given
  /// entry pointer (for convenience).
  const CachedRealPath &
  insertRealPathForFilename(StringRef Filename,
                            const CachedRealPath &RealPath) {
    assert(llvm::sys::path::is_absolute_gnu(Filename));
    auto [It, Inserted] = Cache.insert({Filename, {nullptr, &RealPath}});
    auto &[CachedEntry, CachedRealPath] = It->getValue();
    if (!Inserted) {
      // The file is already present in the local cache. If we got here, it only
      // contains the entry. Let's make sure the real path is populated too.
      assert((!CachedRealPath && CachedEntry) && "real path already present");
      CachedRealPath = &RealPath;
    }
    return *CachedRealPath;
  }
};

/// Reference to a CachedFileSystemEntry.
/// If the underlying entry is an opened file, this wrapper returns the file
/// contents and the scanned preprocessor directives.
class EntryRef {
  /// The filename used to access this entry.
  std::string Filename;

  /// The underlying cached entry.
  const CachedFileSystemEntry &Entry;

  friend class DependencyScanningWorkerFilesystem;

public:
  EntryRef(StringRef Name, const CachedFileSystemEntry &Entry)
      : Filename(Name), Entry(Entry) {}

  llvm::vfs::Status getStatus() const {
    llvm::vfs::Status Stat = Entry.getStatus();
    if (!Stat.isDirectory())
      Stat = llvm::vfs::Status::copyWithNewSize(Stat, getContents().size());
    return llvm::vfs::Status::copyWithNewName(Stat, Filename);
  }

  bool isError() const { return Entry.isError(); }
  bool isDirectory() const { return Entry.isDirectory(); }

  /// If the cached entry represents an error, promotes it into `ErrorOr`.
  llvm::ErrorOr<EntryRef> unwrapError() const {
    if (isError())
      return Entry.getError();
    return *this;
  }

  StringRef getContents() const { return Entry.getOriginalContents(); }

  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  getDirectiveTokens() const {
    return Entry.getDirectiveTokens();
  }
};

/// A virtual file system optimized for the dependency discovery.
///
/// It is primarily designed to work with source files whose contents was
/// preprocessed to remove any tokens that are unlikely to affect the dependency
/// computation.
///
/// This is not a thread safe VFS. A single instance is meant to be used only in
/// one thread. Multiple instances are allowed to service multiple threads
/// running in parallel.
class DependencyScanningWorkerFilesystem
    : public llvm::RTTIExtends<DependencyScanningWorkerFilesystem,
                               llvm::vfs::ProxyFileSystem> {
public:
  static const char ID;

  DependencyScanningWorkerFilesystem(
      DependencyScanningFilesystemSharedCache &SharedCache,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS);

  llvm::ErrorOr<llvm::vfs::Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
  openFileForRead(const Twine &Path) override;

  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;

  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

  /// Returns entry for the given filename.
  ///
  /// Attempts to use the local and shared caches first, then falls back to
  /// using the underlying filesystem.
  llvm::ErrorOr<EntryRef> getOrCreateFileSystemEntry(StringRef Filename);

  /// Ensure the directive tokens are populated for this file entry.
  ///
  /// Returns true if the directive tokens are populated for this file entry,
  /// false if not (i.e. this entry is not a file or its scan fails).
  bool ensureDirectiveTokensArePopulated(EntryRef Entry);

  /// Check whether \p Path exists. By default checks cached result of \c
  /// status(), and falls back on FS if unable to do so.
  bool exists(const Twine &Path) override;

private:
  /// For a filename that's not yet associated with any entry in the caches,
  /// uses the underlying filesystem to either look up the entry based in the
  /// shared cache indexed by unique ID, or creates new entry from scratch.
  /// \p FilenameForLookup will always be an absolute path, and different than
  /// \p OriginalFilename if \p OriginalFilename is relative.
  llvm::ErrorOr<const CachedFileSystemEntry &>
  computeAndStoreResult(StringRef OriginalFilename,
                        StringRef FilenameForLookup);

  /// Represents a filesystem entry that has been stat-ed (and potentially read)
  /// and that's about to be inserted into the cache as `CachedFileSystemEntry`.
  struct TentativeEntry {
    llvm::vfs::Status Status;
    std::unique_ptr<llvm::MemoryBuffer> Contents;

    TentativeEntry(llvm::vfs::Status Status,
                   std::unique_ptr<llvm::MemoryBuffer> Contents = nullptr)
        : Status(std::move(Status)), Contents(std::move(Contents)) {}
  };

  /// Reads file at the given path. Enforces consistency between the file size
  /// in status and size of read contents.
  llvm::ErrorOr<TentativeEntry> readFile(StringRef Filename);

  /// Returns entry associated with the unique ID of the given tentative entry
  /// if there is some in the shared cache. Otherwise, constructs new one,
  /// associates it with the unique ID and returns the result.
  const CachedFileSystemEntry &
  getOrEmplaceSharedEntryForUID(TentativeEntry TEntry);

  /// Returns entry associated with the filename or nullptr if none is found.
  ///
  /// Returns entry from local cache if there is some. Otherwise, if the entry
  /// is found in the shared cache, writes it through the local cache and
  /// returns it. Otherwise returns nullptr.
  const CachedFileSystemEntry *
  findEntryByFilenameWithWriteThrough(StringRef Filename);

  /// Returns entry associated with the unique ID in the shared cache or nullptr
  /// if none is found.
  const CachedFileSystemEntry *
  findSharedEntryByUID(llvm::vfs::Status Stat) const {
    return SharedCache.getShardForUID(Stat.getUniqueID())
        .findEntryByUID(Stat.getUniqueID());
  }

  /// Associates the given entry with the filename in the local cache and
  /// returns it.
  const CachedFileSystemEntry &
  insertLocalEntryForFilename(StringRef Filename,
                              const CachedFileSystemEntry &Entry) {
    return LocalCache.insertEntryForFilename(Filename, Entry);
  }

  /// Returns entry associated with the filename in the shared cache if there is
  /// some. Otherwise, constructs new one with the given error code, associates
  /// it with the filename and returns the result.
  const CachedFileSystemEntry &
  getOrEmplaceSharedEntryForFilename(StringRef Filename, std::error_code EC) {
    return SharedCache.getShardForFilename(Filename)
        .getOrEmplaceEntryForFilename(Filename, EC);
  }

  /// Returns entry associated with the filename in the shared cache if there is
  /// some. Otherwise, associates the given entry with the filename and returns
  /// it.
  const CachedFileSystemEntry &
  getOrInsertSharedEntryForFilename(StringRef Filename,
                                    const CachedFileSystemEntry &Entry) {
    return SharedCache.getShardForFilename(Filename)
        .getOrInsertEntryForFilename(Filename, Entry);
  }

  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override {
    printIndent(OS, IndentLevel);
    OS << "DependencyScanningFilesystem\n";
    getUnderlyingFS().print(OS, Type, IndentLevel + 1);
  }

  /// The global cache shared between worker threads.
  DependencyScanningFilesystemSharedCache &SharedCache;
  /// The local cache is used by the worker thread to cache file system queries
  /// locally instead of querying the global cache every time.
  DependencyScanningFilesystemLocalCache LocalCache;

  /// The working directory to use for making relative paths absolute before
  /// using them for cache lookups.
  llvm::ErrorOr<std::string> WorkingDirForCacheLookup;

  void updateWorkingDirForCacheLookup();

  llvm::ErrorOr<StringRef>
  tryGetFilenameForLookup(StringRef OriginalFilename,
                          llvm::SmallVectorImpl<char> &PathBuf) const;
};

} // end namespace dependencies
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGFILESYSTEM_H
