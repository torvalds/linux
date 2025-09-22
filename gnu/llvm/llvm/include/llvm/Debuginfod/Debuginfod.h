//===-- llvm/Debuginfod/Debuginfod.h - Debuginfod client --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains several declarations for the debuginfod client and
/// server. The client functions are getDefaultDebuginfodUrls,
/// getCachedOrDownloadArtifact, and several convenience functions for specific
/// artifact types: getCachedOrDownloadSource, getCachedOrDownloadExecutable,
/// and getCachedOrDownloadDebuginfo. For the server, this file declares the
/// DebuginfodLogEntry and DebuginfodServer structs, as well as the
/// DebuginfodLog, DebuginfodCollection classes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFOD_DEBUGINFOD_H
#define LLVM_DEBUGINFOD_DEBUGINFOD_H

#include "HTTPServer.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/Support/Timer.h"

#include <chrono>
#include <condition_variable>
#include <optional>
#include <queue>

namespace llvm {

/// Returns false if a debuginfod lookup can be determined to have no chance of
/// succeeding.
bool canUseDebuginfod();

/// Finds default array of Debuginfod server URLs by checking DEBUGINFOD_URLS
/// environment variable.
SmallVector<StringRef> getDefaultDebuginfodUrls();

/// Returns the cache key for a given debuginfod URL path.
std::string getDebuginfodCacheKey(StringRef UrlPath);

/// Sets the list of debuginfod server URLs to query. This overrides the
/// environment variable DEBUGINFOD_URLS.
void setDefaultDebuginfodUrls(const SmallVector<StringRef> &URLs);

/// Finds a default local file caching directory for the debuginfod client,
/// first checking DEBUGINFOD_CACHE_PATH.
Expected<std::string> getDefaultDebuginfodCacheDirectory();

/// Finds a default timeout for debuginfod HTTP requests. Checks
/// DEBUGINFOD_TIMEOUT environment variable, default is 90 seconds (90000 ms).
std::chrono::milliseconds getDefaultDebuginfodTimeout();

/// Get the full URL path for a source request of a given BuildID and file
/// path.
std::string getDebuginfodSourceUrlPath(object::BuildIDRef ID,
                                       StringRef SourceFilePath);

/// Fetches a specified source file by searching the default local cache
/// directory and server URLs.
Expected<std::string> getCachedOrDownloadSource(object::BuildIDRef ID,
                                                StringRef SourceFilePath);

/// Get the full URL path for an executable request of a given BuildID.
std::string getDebuginfodExecutableUrlPath(object::BuildIDRef ID);

/// Fetches an executable by searching the default local cache directory and
/// server URLs.
Expected<std::string> getCachedOrDownloadExecutable(object::BuildIDRef ID);

/// Get the full URL path for a debug binary request of a given BuildID.
std::string getDebuginfodDebuginfoUrlPath(object::BuildIDRef ID);

/// Fetches a debug binary by searching the default local cache directory and
/// server URLs.
Expected<std::string> getCachedOrDownloadDebuginfo(object::BuildIDRef ID);

/// Fetches any debuginfod artifact using the default local cache directory and
/// server URLs.
Expected<std::string> getCachedOrDownloadArtifact(StringRef UniqueKey,
                                                  StringRef UrlPath);

/// Fetches any debuginfod artifact using the specified local cache directory,
/// server URLs, and request timeout (in milliseconds). If the artifact is
/// found, uses the UniqueKey for the local cache file.
Expected<std::string> getCachedOrDownloadArtifact(
    StringRef UniqueKey, StringRef UrlPath, StringRef CacheDirectoryPath,
    ArrayRef<StringRef> DebuginfodUrls, std::chrono::milliseconds Timeout);

class ThreadPoolInterface;

struct DebuginfodLogEntry {
  std::string Message;
  DebuginfodLogEntry() = default;
  DebuginfodLogEntry(const Twine &Message);
};

class DebuginfodLog {
  std::mutex QueueMutex;
  std::condition_variable QueueCondition;
  std::queue<DebuginfodLogEntry> LogEntryQueue;

public:
  // Adds a log entry to end of the queue.
  void push(DebuginfodLogEntry Entry);
  // Adds a log entry to end of the queue.
  void push(const Twine &Message);
  // Blocks until there are log entries in the queue, then pops and returns the
  // first one.
  DebuginfodLogEntry pop();
};

/// Tracks a collection of debuginfod artifacts on the local filesystem.
class DebuginfodCollection {
  SmallVector<std::string, 1> Paths;
  sys::RWMutex BinariesMutex;
  StringMap<std::string> Binaries;
  sys::RWMutex DebugBinariesMutex;
  StringMap<std::string> DebugBinaries;
  Error findBinaries(StringRef Path);
  Expected<std::optional<std::string>> getDebugBinaryPath(object::BuildIDRef);
  Expected<std::optional<std::string>> getBinaryPath(object::BuildIDRef);
  // If the collection has not been updated since MinInterval, call update() and
  // return true. Otherwise return false. If update returns an error, return the
  // error.
  Expected<bool> updateIfStale();
  DebuginfodLog &Log;
  ThreadPoolInterface &Pool;
  Timer UpdateTimer;
  sys::Mutex UpdateMutex;

  // Minimum update interval, in seconds, for on-demand updates triggered when a
  // build-id is not found.
  double MinInterval;

public:
  DebuginfodCollection(ArrayRef<StringRef> Paths, DebuginfodLog &Log,
                       ThreadPoolInterface &Pool, double MinInterval);
  Error update();
  Error updateForever(std::chrono::milliseconds Interval);
  Expected<std::string> findDebugBinaryPath(object::BuildIDRef);
  Expected<std::string> findBinaryPath(object::BuildIDRef);
};

struct DebuginfodServer {
  HTTPServer Server;
  DebuginfodLog &Log;
  DebuginfodCollection &Collection;
  DebuginfodServer(DebuginfodLog &Log, DebuginfodCollection &Collection);
};

} // end namespace llvm

#endif
