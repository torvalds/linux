//===-CachePruning.cpp - LLVM Cache Directory Pruning ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the pruning of a directory based on least recently used.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CachePruning.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cache-pruning"

#include <set>
#include <system_error>

using namespace llvm;

namespace {
struct FileInfo {
  sys::TimePoint<> Time;
  uint64_t Size;
  std::string Path;

  /// Used to determine which files to prune first. Also used to determine
  /// set membership, so must take into account all fields.
  bool operator<(const FileInfo &Other) const {
    return std::tie(Time, Other.Size, Path) <
           std::tie(Other.Time, Size, Other.Path);
  }
};
} // anonymous namespace

/// Write a new timestamp file with the given path. This is used for the pruning
/// interval option.
static void writeTimestampFile(StringRef TimestampFile) {
  std::error_code EC;
  raw_fd_ostream Out(TimestampFile.str(), EC, sys::fs::OF_None);
}

static Expected<std::chrono::seconds> parseDuration(StringRef Duration) {
  if (Duration.empty())
    return make_error<StringError>("Duration must not be empty",
                                   inconvertibleErrorCode());

  StringRef NumStr = Duration.slice(0, Duration.size()-1);
  uint64_t Num;
  if (NumStr.getAsInteger(0, Num))
    return make_error<StringError>("'" + NumStr + "' not an integer",
                                   inconvertibleErrorCode());

  switch (Duration.back()) {
  case 's':
    return std::chrono::seconds(Num);
  case 'm':
    return std::chrono::minutes(Num);
  case 'h':
    return std::chrono::hours(Num);
  default:
    return make_error<StringError>("'" + Duration +
                                       "' must end with one of 's', 'm' or 'h'",
                                   inconvertibleErrorCode());
  }
}

Expected<CachePruningPolicy>
llvm::parseCachePruningPolicy(StringRef PolicyStr) {
  CachePruningPolicy Policy;
  std::pair<StringRef, StringRef> P = {"", PolicyStr};
  while (!P.second.empty()) {
    P = P.second.split(':');

    StringRef Key, Value;
    std::tie(Key, Value) = P.first.split('=');
    if (Key == "prune_interval") {
      auto DurationOrErr = parseDuration(Value);
      if (!DurationOrErr)
        return DurationOrErr.takeError();
      Policy.Interval = *DurationOrErr;
    } else if (Key == "prune_after") {
      auto DurationOrErr = parseDuration(Value);
      if (!DurationOrErr)
        return DurationOrErr.takeError();
      Policy.Expiration = *DurationOrErr;
    } else if (Key == "cache_size") {
      if (Value.back() != '%')
        return make_error<StringError>("'" + Value + "' must be a percentage",
                                       inconvertibleErrorCode());
      StringRef SizeStr = Value.drop_back();
      uint64_t Size;
      if (SizeStr.getAsInteger(0, Size))
        return make_error<StringError>("'" + SizeStr + "' not an integer",
                                       inconvertibleErrorCode());
      if (Size > 100)
        return make_error<StringError>("'" + SizeStr +
                                           "' must be between 0 and 100",
                                       inconvertibleErrorCode());
      Policy.MaxSizePercentageOfAvailableSpace = Size;
    } else if (Key == "cache_size_bytes") {
      uint64_t Mult = 1;
      switch (tolower(Value.back())) {
      case 'k':
        Mult = 1024;
        Value = Value.drop_back();
        break;
      case 'm':
        Mult = 1024 * 1024;
        Value = Value.drop_back();
        break;
      case 'g':
        Mult = 1024 * 1024 * 1024;
        Value = Value.drop_back();
        break;
      }
      uint64_t Size;
      if (Value.getAsInteger(0, Size))
        return make_error<StringError>("'" + Value + "' not an integer",
                                       inconvertibleErrorCode());
      Policy.MaxSizeBytes = Size * Mult;
    } else if (Key == "cache_size_files") {
      if (Value.getAsInteger(0, Policy.MaxSizeFiles))
        return make_error<StringError>("'" + Value + "' not an integer",
                                       inconvertibleErrorCode());
    } else {
      return make_error<StringError>("Unknown key: '" + Key + "'",
                                     inconvertibleErrorCode());
    }
  }

  return Policy;
}

/// Prune the cache of files that haven't been accessed in a long time.
bool llvm::pruneCache(StringRef Path, CachePruningPolicy Policy,
                      const std::vector<std::unique_ptr<MemoryBuffer>> &Files) {
  using namespace std::chrono;

  if (Path.empty())
    return false;

  bool isPathDir;
  if (sys::fs::is_directory(Path, isPathDir))
    return false;

  if (!isPathDir)
    return false;

  Policy.MaxSizePercentageOfAvailableSpace =
      std::min(Policy.MaxSizePercentageOfAvailableSpace, 100u);

  if (Policy.Expiration == seconds(0) &&
      Policy.MaxSizePercentageOfAvailableSpace == 0 &&
      Policy.MaxSizeBytes == 0 && Policy.MaxSizeFiles == 0) {
    LLVM_DEBUG(dbgs() << "No pruning settings set, exit early\n");
    // Nothing will be pruned, early exit
    return false;
  }

  // Try to stat() the timestamp file.
  SmallString<128> TimestampFile(Path);
  sys::path::append(TimestampFile, "llvmcache.timestamp");
  sys::fs::file_status FileStatus;
  const auto CurrentTime = system_clock::now();
  if (auto EC = sys::fs::status(TimestampFile, FileStatus)) {
    if (EC == errc::no_such_file_or_directory) {
      // If the timestamp file wasn't there, create one now.
      writeTimestampFile(TimestampFile);
    } else {
      // Unknown error?
      return false;
    }
  } else {
    if (!Policy.Interval)
      return false;
    if (Policy.Interval != seconds(0)) {
      // Check whether the time stamp is older than our pruning interval.
      // If not, do nothing.
      const auto TimeStampModTime = FileStatus.getLastModificationTime();
      auto TimeStampAge = CurrentTime - TimeStampModTime;
      if (TimeStampAge <= *Policy.Interval) {
        LLVM_DEBUG(dbgs() << "Timestamp file too recent ("
                          << duration_cast<seconds>(TimeStampAge).count()
                          << "s old), do not prune.\n");
        return false;
      }
    }
    // Write a new timestamp file so that nobody else attempts to prune.
    // There is a benign race condition here, if two processes happen to
    // notice at the same time that the timestamp is out-of-date.
    writeTimestampFile(TimestampFile);
  }

  // Keep track of files to delete to get below the size limit.
  // Order by time of last use so that recently used files are preserved.
  std::set<FileInfo> FileInfos;
  uint64_t TotalSize = 0;

  // Walk the entire directory cache, looking for unused files.
  std::error_code EC;
  SmallString<128> CachePathNative;
  sys::path::native(Path, CachePathNative);
  // Walk all of the files within this directory.
  for (sys::fs::directory_iterator File(CachePathNative, EC), FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    // Ignore filenames not beginning with "llvmcache-" or "Thin-". This
    // includes the timestamp file as well as any files created by the user.
    // This acts as a safeguard against data loss if the user specifies the
    // wrong directory as their cache directory.
    StringRef filename = sys::path::filename(File->path());
    if (!filename.starts_with("llvmcache-") && !filename.starts_with("Thin-"))
      continue;

    // Look at this file. If we can't stat it, there's nothing interesting
    // there.
    ErrorOr<sys::fs::basic_file_status> StatusOrErr = File->status();
    if (!StatusOrErr) {
      LLVM_DEBUG(dbgs() << "Ignore " << File->path() << " (can't stat)\n");
      continue;
    }

    // If the file hasn't been used recently enough, delete it
    const auto FileAccessTime = StatusOrErr->getLastAccessedTime();
    auto FileAge = CurrentTime - FileAccessTime;
    if (Policy.Expiration != seconds(0) && FileAge > Policy.Expiration) {
      LLVM_DEBUG(dbgs() << "Remove " << File->path() << " ("
                        << duration_cast<seconds>(FileAge).count()
                        << "s old)\n");
      sys::fs::remove(File->path());
      continue;
    }

    // Leave it here for now, but add it to the list of size-based pruning.
    TotalSize += StatusOrErr->getSize();
    FileInfos.insert({FileAccessTime, StatusOrErr->getSize(), File->path()});
  }

  auto FileInfo = FileInfos.begin();
  size_t NumFiles = FileInfos.size();

  auto RemoveCacheFile = [&]() {
    // Remove the file.
    sys::fs::remove(FileInfo->Path);
    // Update size
    TotalSize -= FileInfo->Size;
    NumFiles--;
    LLVM_DEBUG(dbgs() << " - Remove " << FileInfo->Path << " (size "
                      << FileInfo->Size << "), new occupancy is " << TotalSize
                      << "%\n");
    ++FileInfo;
  };

  // files.size() is greater the number of inputs  by one. However, a timestamp
  // file is created and stored in the cache directory if --thinlto-cache-policy
  // option is used. Therefore, files.size() is used as ActualNums.
  const size_t ActualNums = Files.size();
  if (Policy.MaxSizeFiles && ActualNums > Policy.MaxSizeFiles)
    WithColor::warning()
        << "ThinLTO cache pruning happens since the number of created files ("
        << ActualNums << ") exceeds the maximum number of files ("
        << Policy.MaxSizeFiles
        << "); consider adjusting --thinlto-cache-policy\n";

  // Prune for number of files.
  if (Policy.MaxSizeFiles)
    while (NumFiles > Policy.MaxSizeFiles)
      RemoveCacheFile();

  // Prune for size now if needed
  if (Policy.MaxSizePercentageOfAvailableSpace > 0 || Policy.MaxSizeBytes > 0) {
    auto ErrOrSpaceInfo = sys::fs::disk_space(Path);
    if (!ErrOrSpaceInfo) {
      report_fatal_error("Can't get available size");
    }
    sys::fs::space_info SpaceInfo = ErrOrSpaceInfo.get();
    auto AvailableSpace = TotalSize + SpaceInfo.free;

    if (Policy.MaxSizePercentageOfAvailableSpace == 0)
      Policy.MaxSizePercentageOfAvailableSpace = 100;
    if (Policy.MaxSizeBytes == 0)
      Policy.MaxSizeBytes = AvailableSpace;
    auto TotalSizeTarget = std::min<uint64_t>(
        AvailableSpace * Policy.MaxSizePercentageOfAvailableSpace / 100ull,
        Policy.MaxSizeBytes);

    LLVM_DEBUG(dbgs() << "Occupancy: " << ((100 * TotalSize) / AvailableSpace)
                      << "% target is: "
                      << Policy.MaxSizePercentageOfAvailableSpace << "%, "
                      << Policy.MaxSizeBytes << " bytes\n");

    size_t ActualSizes = 0;
    for (const auto &File : Files)
      if (File)
        ActualSizes += File->getBufferSize();

    if (ActualSizes > TotalSizeTarget)
      WithColor::warning()
          << "ThinLTO cache pruning happens since the total size of the cache "
             "files consumed by the current link job ("
          << ActualSizes << "  bytes) exceeds maximum cache size ("
          << TotalSizeTarget
          << " bytes); consider adjusting --thinlto-cache-policy\n";

    // Remove the oldest accessed files first, till we get below the threshold.
    while (TotalSize > TotalSizeTarget && FileInfo != FileInfos.end())
      RemoveCacheFile();
  }
  return true;
}
