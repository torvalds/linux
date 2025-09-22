//===- DependencyScanningFilesystem.cpp - clang-scan-deps fs --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningFilesystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include "llvm/Support/Threading.h"
#include <optional>

using namespace clang;
using namespace tooling;
using namespace dependencies;

llvm::ErrorOr<DependencyScanningWorkerFilesystem::TentativeEntry>
DependencyScanningWorkerFilesystem::readFile(StringRef Filename) {
  // Load the file and its content from the file system.
  auto MaybeFile = getUnderlyingFS().openFileForRead(Filename);
  if (!MaybeFile)
    return MaybeFile.getError();
  auto File = std::move(*MaybeFile);

  auto MaybeStat = File->status();
  if (!MaybeStat)
    return MaybeStat.getError();
  auto Stat = std::move(*MaybeStat);

  auto MaybeBuffer = File->getBuffer(Stat.getName());
  if (!MaybeBuffer)
    return MaybeBuffer.getError();
  auto Buffer = std::move(*MaybeBuffer);

  // If the file size changed between read and stat, pretend it didn't.
  if (Stat.getSize() != Buffer->getBufferSize())
    Stat = llvm::vfs::Status::copyWithNewSize(Stat, Buffer->getBufferSize());

  return TentativeEntry(Stat, std::move(Buffer));
}

bool DependencyScanningWorkerFilesystem::ensureDirectiveTokensArePopulated(
    EntryRef Ref) {
  auto &Entry = Ref.Entry;

  if (Entry.isError() || Entry.isDirectory())
    return false;

  CachedFileContents *Contents = Entry.getCachedContents();
  assert(Contents && "contents not initialized");

  // Double-checked locking.
  if (Contents->DepDirectives.load())
    return true;

  std::lock_guard<std::mutex> GuardLock(Contents->ValueLock);

  // Double-checked locking.
  if (Contents->DepDirectives.load())
    return true;

  SmallVector<dependency_directives_scan::Directive, 64> Directives;
  // Scan the file for preprocessor directives that might affect the
  // dependencies.
  if (scanSourceForDependencyDirectives(Contents->Original->getBuffer(),
                                        Contents->DepDirectiveTokens,
                                        Directives)) {
    Contents->DepDirectiveTokens.clear();
    // FIXME: Propagate the diagnostic if desired by the client.
    Contents->DepDirectives.store(new std::optional<DependencyDirectivesTy>());
    return false;
  }

  // This function performed double-checked locking using `DepDirectives`.
  // Assigning it must be the last thing this function does, otherwise other
  // threads may skip the critical section (`DepDirectives != nullptr`), leading
  // to a data race.
  Contents->DepDirectives.store(
      new std::optional<DependencyDirectivesTy>(std::move(Directives)));
  return true;
}

DependencyScanningFilesystemSharedCache::
    DependencyScanningFilesystemSharedCache() {
  // This heuristic was chosen using a empirical testing on a
  // reasonably high core machine (iMacPro 18 cores / 36 threads). The cache
  // sharding gives a performance edge by reducing the lock contention.
  // FIXME: A better heuristic might also consider the OS to account for
  // the different cost of lock contention on different OSes.
  NumShards =
      std::max(2u, llvm::hardware_concurrency().compute_thread_count() / 4);
  CacheShards = std::make_unique<CacheShard[]>(NumShards);
}

DependencyScanningFilesystemSharedCache::CacheShard &
DependencyScanningFilesystemSharedCache::getShardForFilename(
    StringRef Filename) const {
  assert(llvm::sys::path::is_absolute_gnu(Filename));
  return CacheShards[llvm::hash_value(Filename) % NumShards];
}

DependencyScanningFilesystemSharedCache::CacheShard &
DependencyScanningFilesystemSharedCache::getShardForUID(
    llvm::sys::fs::UniqueID UID) const {
  auto Hash = llvm::hash_combine(UID.getDevice(), UID.getFile());
  return CacheShards[Hash % NumShards];
}

const CachedFileSystemEntry *
DependencyScanningFilesystemSharedCache::CacheShard::findEntryByFilename(
    StringRef Filename) const {
  assert(llvm::sys::path::is_absolute_gnu(Filename));
  std::lock_guard<std::mutex> LockGuard(CacheLock);
  auto It = CacheByFilename.find(Filename);
  return It == CacheByFilename.end() ? nullptr : It->getValue().first;
}

const CachedFileSystemEntry *
DependencyScanningFilesystemSharedCache::CacheShard::findEntryByUID(
    llvm::sys::fs::UniqueID UID) const {
  std::lock_guard<std::mutex> LockGuard(CacheLock);
  auto It = EntriesByUID.find(UID);
  return It == EntriesByUID.end() ? nullptr : It->getSecond();
}

const CachedFileSystemEntry &
DependencyScanningFilesystemSharedCache::CacheShard::
    getOrEmplaceEntryForFilename(StringRef Filename,
                                 llvm::ErrorOr<llvm::vfs::Status> Stat) {
  std::lock_guard<std::mutex> LockGuard(CacheLock);
  auto [It, Inserted] = CacheByFilename.insert({Filename, {nullptr, nullptr}});
  auto &[CachedEntry, CachedRealPath] = It->getValue();
  if (!CachedEntry) {
    // The entry is not present in the shared cache. Either the cache doesn't
    // know about the file at all, or it only knows about its real path.
    assert((Inserted || CachedRealPath) && "existing file with empty pair");
    CachedEntry =
        new (EntryStorage.Allocate()) CachedFileSystemEntry(std::move(Stat));
  }
  return *CachedEntry;
}

const CachedFileSystemEntry &
DependencyScanningFilesystemSharedCache::CacheShard::getOrEmplaceEntryForUID(
    llvm::sys::fs::UniqueID UID, llvm::vfs::Status Stat,
    std::unique_ptr<llvm::MemoryBuffer> Contents) {
  std::lock_guard<std::mutex> LockGuard(CacheLock);
  auto [It, Inserted] = EntriesByUID.insert({UID, nullptr});
  auto &CachedEntry = It->getSecond();
  if (Inserted) {
    CachedFileContents *StoredContents = nullptr;
    if (Contents)
      StoredContents = new (ContentsStorage.Allocate())
          CachedFileContents(std::move(Contents));
    CachedEntry = new (EntryStorage.Allocate())
        CachedFileSystemEntry(std::move(Stat), StoredContents);
  }
  return *CachedEntry;
}

const CachedFileSystemEntry &
DependencyScanningFilesystemSharedCache::CacheShard::
    getOrInsertEntryForFilename(StringRef Filename,
                                const CachedFileSystemEntry &Entry) {
  std::lock_guard<std::mutex> LockGuard(CacheLock);
  auto [It, Inserted] = CacheByFilename.insert({Filename, {&Entry, nullptr}});
  auto &[CachedEntry, CachedRealPath] = It->getValue();
  if (!Inserted || !CachedEntry)
    CachedEntry = &Entry;
  return *CachedEntry;
}

const CachedRealPath *
DependencyScanningFilesystemSharedCache::CacheShard::findRealPathByFilename(
    StringRef Filename) const {
  assert(llvm::sys::path::is_absolute_gnu(Filename));
  std::lock_guard<std::mutex> LockGuard(CacheLock);
  auto It = CacheByFilename.find(Filename);
  return It == CacheByFilename.end() ? nullptr : It->getValue().second;
}

const CachedRealPath &DependencyScanningFilesystemSharedCache::CacheShard::
    getOrEmplaceRealPathForFilename(StringRef Filename,
                                    llvm::ErrorOr<llvm::StringRef> RealPath) {
  std::lock_guard<std::mutex> LockGuard(CacheLock);

  const CachedRealPath *&StoredRealPath = CacheByFilename[Filename].second;
  if (!StoredRealPath) {
    auto OwnedRealPath = [&]() -> CachedRealPath {
      if (!RealPath)
        return RealPath.getError();
      return RealPath->str();
    }();

    StoredRealPath = new (RealPathStorage.Allocate())
        CachedRealPath(std::move(OwnedRealPath));
  }

  return *StoredRealPath;
}

static bool shouldCacheStatFailures(StringRef Filename) {
  StringRef Ext = llvm::sys::path::extension(Filename);
  if (Ext.empty())
    return false; // This may be the module cache directory.
  return true;
}

DependencyScanningWorkerFilesystem::DependencyScanningWorkerFilesystem(
    DependencyScanningFilesystemSharedCache &SharedCache,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : llvm::RTTIExtends<DependencyScanningWorkerFilesystem,
                        llvm::vfs::ProxyFileSystem>(std::move(FS)),
      SharedCache(SharedCache),
      WorkingDirForCacheLookup(llvm::errc::invalid_argument) {
  updateWorkingDirForCacheLookup();
}

const CachedFileSystemEntry &
DependencyScanningWorkerFilesystem::getOrEmplaceSharedEntryForUID(
    TentativeEntry TEntry) {
  auto &Shard = SharedCache.getShardForUID(TEntry.Status.getUniqueID());
  return Shard.getOrEmplaceEntryForUID(TEntry.Status.getUniqueID(),
                                       std::move(TEntry.Status),
                                       std::move(TEntry.Contents));
}

const CachedFileSystemEntry *
DependencyScanningWorkerFilesystem::findEntryByFilenameWithWriteThrough(
    StringRef Filename) {
  if (const auto *Entry = LocalCache.findEntryByFilename(Filename))
    return Entry;
  auto &Shard = SharedCache.getShardForFilename(Filename);
  if (const auto *Entry = Shard.findEntryByFilename(Filename))
    return &LocalCache.insertEntryForFilename(Filename, *Entry);
  return nullptr;
}

llvm::ErrorOr<const CachedFileSystemEntry &>
DependencyScanningWorkerFilesystem::computeAndStoreResult(
    StringRef OriginalFilename, StringRef FilenameForLookup) {
  llvm::ErrorOr<llvm::vfs::Status> Stat =
      getUnderlyingFS().status(OriginalFilename);
  if (!Stat) {
    if (!shouldCacheStatFailures(OriginalFilename))
      return Stat.getError();
    const auto &Entry =
        getOrEmplaceSharedEntryForFilename(FilenameForLookup, Stat.getError());
    return insertLocalEntryForFilename(FilenameForLookup, Entry);
  }

  if (const auto *Entry = findSharedEntryByUID(*Stat))
    return insertLocalEntryForFilename(FilenameForLookup, *Entry);

  auto TEntry =
      Stat->isDirectory() ? TentativeEntry(*Stat) : readFile(OriginalFilename);

  const CachedFileSystemEntry *SharedEntry = [&]() {
    if (TEntry) {
      const auto &UIDEntry = getOrEmplaceSharedEntryForUID(std::move(*TEntry));
      return &getOrInsertSharedEntryForFilename(FilenameForLookup, UIDEntry);
    }
    return &getOrEmplaceSharedEntryForFilename(FilenameForLookup,
                                               TEntry.getError());
  }();

  return insertLocalEntryForFilename(FilenameForLookup, *SharedEntry);
}

llvm::ErrorOr<EntryRef>
DependencyScanningWorkerFilesystem::getOrCreateFileSystemEntry(
    StringRef OriginalFilename) {
  SmallString<256> PathBuf;
  auto FilenameForLookup = tryGetFilenameForLookup(OriginalFilename, PathBuf);
  if (!FilenameForLookup)
    return FilenameForLookup.getError();

  if (const auto *Entry =
          findEntryByFilenameWithWriteThrough(*FilenameForLookup))
    return EntryRef(OriginalFilename, *Entry).unwrapError();
  auto MaybeEntry = computeAndStoreResult(OriginalFilename, *FilenameForLookup);
  if (!MaybeEntry)
    return MaybeEntry.getError();
  return EntryRef(OriginalFilename, *MaybeEntry).unwrapError();
}

llvm::ErrorOr<llvm::vfs::Status>
DependencyScanningWorkerFilesystem::status(const Twine &Path) {
  SmallString<256> OwnedFilename;
  StringRef Filename = Path.toStringRef(OwnedFilename);

  if (Filename.ends_with(".pcm"))
    return getUnderlyingFS().status(Path);

  llvm::ErrorOr<EntryRef> Result = getOrCreateFileSystemEntry(Filename);
  if (!Result)
    return Result.getError();
  return Result->getStatus();
}

bool DependencyScanningWorkerFilesystem::exists(const Twine &Path) {
  // While some VFS overlay filesystems may implement more-efficient
  // mechanisms for `exists` queries, `DependencyScanningWorkerFilesystem`
  // typically wraps `RealFileSystem` which does not specialize `exists`,
  // so it is not likely to benefit from such optimizations. Instead,
  // it is more-valuable to have this query go through the
  // cached-`status` code-path of the `DependencyScanningWorkerFilesystem`.
  llvm::ErrorOr<llvm::vfs::Status> Status = status(Path);
  return Status && Status->exists();
}

namespace {

/// The VFS that is used by clang consumes the \c CachedFileSystemEntry using
/// this subclass.
class DepScanFile final : public llvm::vfs::File {
public:
  DepScanFile(std::unique_ptr<llvm::MemoryBuffer> Buffer,
              llvm::vfs::Status Stat)
      : Buffer(std::move(Buffer)), Stat(std::move(Stat)) {}

  static llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>> create(EntryRef Entry);

  llvm::ErrorOr<llvm::vfs::Status> status() override { return Stat; }

  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t FileSize, bool RequiresNullTerminator,
            bool IsVolatile) override {
    return std::move(Buffer);
  }

  std::error_code close() override { return {}; }

private:
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
  llvm::vfs::Status Stat;
};

} // end anonymous namespace

llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
DepScanFile::create(EntryRef Entry) {
  assert(!Entry.isError() && "error");

  if (Entry.isDirectory())
    return std::make_error_code(std::errc::is_a_directory);

  auto Result = std::make_unique<DepScanFile>(
      llvm::MemoryBuffer::getMemBuffer(Entry.getContents(),
                                       Entry.getStatus().getName(),
                                       /*RequiresNullTerminator=*/false),
      Entry.getStatus());

  return llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>(
      std::unique_ptr<llvm::vfs::File>(std::move(Result)));
}

llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>>
DependencyScanningWorkerFilesystem::openFileForRead(const Twine &Path) {
  SmallString<256> OwnedFilename;
  StringRef Filename = Path.toStringRef(OwnedFilename);

  if (Filename.ends_with(".pcm"))
    return getUnderlyingFS().openFileForRead(Path);

  llvm::ErrorOr<EntryRef> Result = getOrCreateFileSystemEntry(Filename);
  if (!Result)
    return Result.getError();
  return DepScanFile::create(Result.get());
}

std::error_code
DependencyScanningWorkerFilesystem::getRealPath(const Twine &Path,
                                                SmallVectorImpl<char> &Output) {
  SmallString<256> OwnedFilename;
  StringRef OriginalFilename = Path.toStringRef(OwnedFilename);

  SmallString<256> PathBuf;
  auto FilenameForLookup = tryGetFilenameForLookup(OriginalFilename, PathBuf);
  if (!FilenameForLookup)
    return FilenameForLookup.getError();

  auto HandleCachedRealPath =
      [&Output](const CachedRealPath &RealPath) -> std::error_code {
    if (!RealPath)
      return RealPath.getError();
    Output.assign(RealPath->begin(), RealPath->end());
    return {};
  };

  // If we already have the result in local cache, no work required.
  if (const auto *RealPath =
          LocalCache.findRealPathByFilename(*FilenameForLookup))
    return HandleCachedRealPath(*RealPath);

  // If we have the result in the shared cache, cache it locally.
  auto &Shard = SharedCache.getShardForFilename(*FilenameForLookup);
  if (const auto *ShardRealPath =
          Shard.findRealPathByFilename(*FilenameForLookup)) {
    const auto &RealPath = LocalCache.insertRealPathForFilename(
        *FilenameForLookup, *ShardRealPath);
    return HandleCachedRealPath(RealPath);
  }

  // If we don't know the real path, compute it...
  std::error_code EC = getUnderlyingFS().getRealPath(OriginalFilename, Output);
  llvm::ErrorOr<llvm::StringRef> ComputedRealPath = EC;
  if (!EC)
    ComputedRealPath = StringRef{Output.data(), Output.size()};

  // ...and try to write it into the shared cache. In case some other thread won
  // this race and already wrote its own result there, just adopt it. Write
  // whatever is in the shared cache into the local one.
  const auto &RealPath = Shard.getOrEmplaceRealPathForFilename(
      *FilenameForLookup, ComputedRealPath);
  return HandleCachedRealPath(
      LocalCache.insertRealPathForFilename(*FilenameForLookup, RealPath));
}

std::error_code DependencyScanningWorkerFilesystem::setCurrentWorkingDirectory(
    const Twine &Path) {
  std::error_code EC = ProxyFileSystem::setCurrentWorkingDirectory(Path);
  updateWorkingDirForCacheLookup();
  return EC;
}

void DependencyScanningWorkerFilesystem::updateWorkingDirForCacheLookup() {
  llvm::ErrorOr<std::string> CWD =
      getUnderlyingFS().getCurrentWorkingDirectory();
  if (!CWD) {
    WorkingDirForCacheLookup = CWD.getError();
  } else if (!llvm::sys::path::is_absolute_gnu(*CWD)) {
    WorkingDirForCacheLookup = llvm::errc::invalid_argument;
  } else {
    WorkingDirForCacheLookup = *CWD;
  }
  assert(!WorkingDirForCacheLookup ||
         llvm::sys::path::is_absolute_gnu(*WorkingDirForCacheLookup));
}

llvm::ErrorOr<StringRef>
DependencyScanningWorkerFilesystem::tryGetFilenameForLookup(
    StringRef OriginalFilename, llvm::SmallVectorImpl<char> &PathBuf) const {
  StringRef FilenameForLookup;
  if (llvm::sys::path::is_absolute_gnu(OriginalFilename)) {
    FilenameForLookup = OriginalFilename;
  } else if (!WorkingDirForCacheLookup) {
    return WorkingDirForCacheLookup.getError();
  } else {
    StringRef RelFilename = OriginalFilename;
    RelFilename.consume_front("./");
    PathBuf.assign(WorkingDirForCacheLookup->begin(),
                   WorkingDirForCacheLookup->end());
    llvm::sys::path::append(PathBuf, RelFilename);
    FilenameForLookup = StringRef{PathBuf.begin(), PathBuf.size()};
  }
  assert(llvm::sys::path::is_absolute_gnu(FilenameForLookup));
  return FilenameForLookup;
}

const char DependencyScanningWorkerFilesystem::ID = 0;
