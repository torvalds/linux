//===-- llvm/Debuginfod/Debuginfod.cpp - Debuginfod client library --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// This file contains several definitions for the debuginfod client and server.
/// For the client, this file defines the fetchInfo function. For the server,
/// this file defines the DebuginfodLogEntry and DebuginfodServer structs, as
/// well as the DebuginfodLog, DebuginfodCollection classes. The fetchInfo
/// function retrieves any of the three supported artifact types: (executable,
/// debuginfo, source file) associated with a build-id from debuginfod servers.
/// If a source file is to be fetched, its absolute path must be specified in
/// the Description argument to fetchInfo. The DebuginfodLogEntry,
/// DebuginfodLog, and DebuginfodCollection are used by the DebuginfodServer to
/// scan the local filesystem for binaries and serve the debuginfod protocol.
///
//===----------------------------------------------------------------------===//

#include "llvm/Debuginfod/Debuginfod.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Debuginfod/HTTPClient.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Caching.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/xxhash.h"

#include <atomic>
#include <optional>
#include <thread>

namespace llvm {

using llvm::object::BuildIDRef;

namespace {
std::optional<SmallVector<StringRef>> DebuginfodUrls;
// Many Readers/Single Writer lock protecting the global debuginfod URL list.
llvm::sys::RWMutex UrlsMutex;
} // namespace

std::string getDebuginfodCacheKey(llvm::StringRef S) {
  return utostr(xxh3_64bits(S));
}

// Returns a binary BuildID as a normalized hex string.
// Uses lowercase for compatibility with common debuginfod servers.
static std::string buildIDToString(BuildIDRef ID) {
  return llvm::toHex(ID, /*LowerCase=*/true);
}

bool canUseDebuginfod() {
  return HTTPClient::isAvailable() && !getDefaultDebuginfodUrls().empty();
}

SmallVector<StringRef> getDefaultDebuginfodUrls() {
  std::shared_lock<llvm::sys::RWMutex> ReadGuard(UrlsMutex);
  if (!DebuginfodUrls) {
    // Only read from the environment variable if the user hasn't already
    // set the value.
    ReadGuard.unlock();
    std::unique_lock<llvm::sys::RWMutex> WriteGuard(UrlsMutex);
    DebuginfodUrls = SmallVector<StringRef>();
    if (const char *DebuginfodUrlsEnv = std::getenv("DEBUGINFOD_URLS")) {
      StringRef(DebuginfodUrlsEnv)
          .split(DebuginfodUrls.value(), " ", -1, false);
    }
    WriteGuard.unlock();
    ReadGuard.lock();
  }
  return DebuginfodUrls.value();
}

// Set the default debuginfod URL list, override the environment variable.
void setDefaultDebuginfodUrls(const SmallVector<StringRef> &URLs) {
  std::unique_lock<llvm::sys::RWMutex> WriteGuard(UrlsMutex);
  DebuginfodUrls = URLs;
}

/// Finds a default local file caching directory for the debuginfod client,
/// first checking DEBUGINFOD_CACHE_PATH.
Expected<std::string> getDefaultDebuginfodCacheDirectory() {
  if (const char *CacheDirectoryEnv = std::getenv("DEBUGINFOD_CACHE_PATH"))
    return CacheDirectoryEnv;

  SmallString<64> CacheDirectory;
  if (!sys::path::cache_directory(CacheDirectory))
    return createStringError(
        errc::io_error, "Unable to determine appropriate cache directory.");
  sys::path::append(CacheDirectory, "llvm-debuginfod", "client");
  return std::string(CacheDirectory);
}

std::chrono::milliseconds getDefaultDebuginfodTimeout() {
  long Timeout;
  const char *DebuginfodTimeoutEnv = std::getenv("DEBUGINFOD_TIMEOUT");
  if (DebuginfodTimeoutEnv &&
      to_integer(StringRef(DebuginfodTimeoutEnv).trim(), Timeout, 10))
    return std::chrono::milliseconds(Timeout * 1000);

  return std::chrono::milliseconds(90 * 1000);
}

/// The following functions fetch a debuginfod artifact to a file in a local
/// cache and return the cached file path. They first search the local cache,
/// followed by the debuginfod servers.

std::string getDebuginfodSourceUrlPath(BuildIDRef ID,
                                       StringRef SourceFilePath) {
  SmallString<64> UrlPath;
  sys::path::append(UrlPath, sys::path::Style::posix, "buildid",
                    buildIDToString(ID), "source",
                    sys::path::convert_to_slash(SourceFilePath));
  return std::string(UrlPath);
}

Expected<std::string> getCachedOrDownloadSource(BuildIDRef ID,
                                                StringRef SourceFilePath) {
  std::string UrlPath = getDebuginfodSourceUrlPath(ID, SourceFilePath);
  return getCachedOrDownloadArtifact(getDebuginfodCacheKey(UrlPath), UrlPath);
}

std::string getDebuginfodExecutableUrlPath(BuildIDRef ID) {
  SmallString<64> UrlPath;
  sys::path::append(UrlPath, sys::path::Style::posix, "buildid",
                    buildIDToString(ID), "executable");
  return std::string(UrlPath);
}

Expected<std::string> getCachedOrDownloadExecutable(BuildIDRef ID) {
  std::string UrlPath = getDebuginfodExecutableUrlPath(ID);
  return getCachedOrDownloadArtifact(getDebuginfodCacheKey(UrlPath), UrlPath);
}

std::string getDebuginfodDebuginfoUrlPath(BuildIDRef ID) {
  SmallString<64> UrlPath;
  sys::path::append(UrlPath, sys::path::Style::posix, "buildid",
                    buildIDToString(ID), "debuginfo");
  return std::string(UrlPath);
}

Expected<std::string> getCachedOrDownloadDebuginfo(BuildIDRef ID) {
  std::string UrlPath = getDebuginfodDebuginfoUrlPath(ID);
  return getCachedOrDownloadArtifact(getDebuginfodCacheKey(UrlPath), UrlPath);
}

// General fetching function.
Expected<std::string> getCachedOrDownloadArtifact(StringRef UniqueKey,
                                                  StringRef UrlPath) {
  SmallString<10> CacheDir;

  Expected<std::string> CacheDirOrErr = getDefaultDebuginfodCacheDirectory();
  if (!CacheDirOrErr)
    return CacheDirOrErr.takeError();
  CacheDir = *CacheDirOrErr;

  return getCachedOrDownloadArtifact(UniqueKey, UrlPath, CacheDir,
                                     getDefaultDebuginfodUrls(),
                                     getDefaultDebuginfodTimeout());
}

namespace {

/// A simple handler which streams the returned data to a cache file. The cache
/// file is only created if a 200 OK status is observed.
class StreamedHTTPResponseHandler : public HTTPResponseHandler {
  using CreateStreamFn =
      std::function<Expected<std::unique_ptr<CachedFileStream>>()>;
  CreateStreamFn CreateStream;
  HTTPClient &Client;
  std::unique_ptr<CachedFileStream> FileStream;

public:
  StreamedHTTPResponseHandler(CreateStreamFn CreateStream, HTTPClient &Client)
      : CreateStream(CreateStream), Client(Client) {}
  virtual ~StreamedHTTPResponseHandler() = default;

  Error handleBodyChunk(StringRef BodyChunk) override;
};

} // namespace

Error StreamedHTTPResponseHandler::handleBodyChunk(StringRef BodyChunk) {
  if (!FileStream) {
    unsigned Code = Client.responseCode();
    if (Code && Code != 200)
      return Error::success();
    Expected<std::unique_ptr<CachedFileStream>> FileStreamOrError =
        CreateStream();
    if (!FileStreamOrError)
      return FileStreamOrError.takeError();
    FileStream = std::move(*FileStreamOrError);
  }
  *FileStream->OS << BodyChunk;
  return Error::success();
}

// An over-accepting simplification of the HTTP RFC 7230 spec.
static bool isHeader(StringRef S) {
  StringRef Name;
  StringRef Value;
  std::tie(Name, Value) = S.split(':');
  if (Name.empty() || Value.empty())
    return false;
  return all_of(Name, [](char C) { return llvm::isPrint(C) && C != ' '; }) &&
         all_of(Value, [](char C) { return llvm::isPrint(C) || C == '\t'; });
}

static SmallVector<std::string, 0> getHeaders() {
  const char *Filename = getenv("DEBUGINFOD_HEADERS_FILE");
  if (!Filename)
    return {};
  ErrorOr<std::unique_ptr<MemoryBuffer>> HeadersFile =
      MemoryBuffer::getFile(Filename, /*IsText=*/true);
  if (!HeadersFile)
    return {};

  SmallVector<std::string, 0> Headers;
  uint64_t LineNumber = 0;
  for (StringRef Line : llvm::split((*HeadersFile)->getBuffer(), '\n')) {
    LineNumber++;
    if (!Line.empty() && Line.back() == '\r')
      Line = Line.drop_back();
    if (!isHeader(Line)) {
      if (!all_of(Line, llvm::isSpace))
        WithColor::warning()
            << "could not parse debuginfod header: " << Filename << ':'
            << LineNumber << '\n';
      continue;
    }
    Headers.emplace_back(Line);
  }
  return Headers;
}

Expected<std::string> getCachedOrDownloadArtifact(
    StringRef UniqueKey, StringRef UrlPath, StringRef CacheDirectoryPath,
    ArrayRef<StringRef> DebuginfodUrls, std::chrono::milliseconds Timeout) {
  SmallString<64> AbsCachedArtifactPath;
  sys::path::append(AbsCachedArtifactPath, CacheDirectoryPath,
                    "llvmcache-" + UniqueKey);

  Expected<FileCache> CacheOrErr =
      localCache("Debuginfod-client", ".debuginfod-client", CacheDirectoryPath);
  if (!CacheOrErr)
    return CacheOrErr.takeError();

  FileCache Cache = *CacheOrErr;
  // We choose an arbitrary Task parameter as we do not make use of it.
  unsigned Task = 0;
  Expected<AddStreamFn> CacheAddStreamOrErr = Cache(Task, UniqueKey, "");
  if (!CacheAddStreamOrErr)
    return CacheAddStreamOrErr.takeError();
  AddStreamFn &CacheAddStream = *CacheAddStreamOrErr;
  if (!CacheAddStream)
    return std::string(AbsCachedArtifactPath);
  // The artifact was not found in the local cache, query the debuginfod
  // servers.
  if (!HTTPClient::isAvailable())
    return createStringError(errc::io_error,
                             "No working HTTP client is available.");

  if (!HTTPClient::IsInitialized)
    return createStringError(
        errc::io_error,
        "A working HTTP client is available, but it is not initialized. To "
        "allow Debuginfod to make HTTP requests, call HTTPClient::initialize() "
        "at the beginning of main.");

  HTTPClient Client;
  Client.setTimeout(Timeout);
  for (StringRef ServerUrl : DebuginfodUrls) {
    SmallString<64> ArtifactUrl;
    sys::path::append(ArtifactUrl, sys::path::Style::posix, ServerUrl, UrlPath);

    // Perform the HTTP request and if successful, write the response body to
    // the cache.
    {
      StreamedHTTPResponseHandler Handler(
          [&]() { return CacheAddStream(Task, ""); }, Client);
      HTTPRequest Request(ArtifactUrl);
      Request.Headers = getHeaders();
      Error Err = Client.perform(Request, Handler);
      if (Err)
        return std::move(Err);

      unsigned Code = Client.responseCode();
      if (Code && Code != 200)
        continue;
    }

    Expected<CachePruningPolicy> PruningPolicyOrErr =
        parseCachePruningPolicy(std::getenv("DEBUGINFOD_CACHE_POLICY"));
    if (!PruningPolicyOrErr)
      return PruningPolicyOrErr.takeError();
    pruneCache(CacheDirectoryPath, *PruningPolicyOrErr);

    // Return the path to the artifact on disk.
    return std::string(AbsCachedArtifactPath);
  }

  return createStringError(errc::argument_out_of_domain, "build id not found");
}

DebuginfodLogEntry::DebuginfodLogEntry(const Twine &Message)
    : Message(Message.str()) {}

void DebuginfodLog::push(const Twine &Message) {
  push(DebuginfodLogEntry(Message));
}

void DebuginfodLog::push(DebuginfodLogEntry Entry) {
  {
    std::lock_guard<std::mutex> Guard(QueueMutex);
    LogEntryQueue.push(Entry);
  }
  QueueCondition.notify_one();
}

DebuginfodLogEntry DebuginfodLog::pop() {
  {
    std::unique_lock<std::mutex> Guard(QueueMutex);
    // Wait for messages to be pushed into the queue.
    QueueCondition.wait(Guard, [&] { return !LogEntryQueue.empty(); });
  }
  std::lock_guard<std::mutex> Guard(QueueMutex);
  if (!LogEntryQueue.size())
    llvm_unreachable("Expected message in the queue.");

  DebuginfodLogEntry Entry = LogEntryQueue.front();
  LogEntryQueue.pop();
  return Entry;
}

DebuginfodCollection::DebuginfodCollection(ArrayRef<StringRef> PathsRef,
                                           DebuginfodLog &Log,
                                           ThreadPoolInterface &Pool,
                                           double MinInterval)
    : Log(Log), Pool(Pool), MinInterval(MinInterval) {
  for (StringRef Path : PathsRef)
    Paths.push_back(Path.str());
}

Error DebuginfodCollection::update() {
  std::lock_guard<sys::Mutex> Guard(UpdateMutex);
  if (UpdateTimer.isRunning())
    UpdateTimer.stopTimer();
  UpdateTimer.clear();
  for (const std::string &Path : Paths) {
    Log.push("Updating binaries at path " + Path);
    if (Error Err = findBinaries(Path))
      return Err;
  }
  Log.push("Updated collection");
  UpdateTimer.startTimer();
  return Error::success();
}

Expected<bool> DebuginfodCollection::updateIfStale() {
  if (!UpdateTimer.isRunning())
    return false;
  UpdateTimer.stopTimer();
  double Time = UpdateTimer.getTotalTime().getWallTime();
  UpdateTimer.startTimer();
  if (Time < MinInterval)
    return false;
  if (Error Err = update())
    return std::move(Err);
  return true;
}

Error DebuginfodCollection::updateForever(std::chrono::milliseconds Interval) {
  while (true) {
    if (Error Err = update())
      return Err;
    std::this_thread::sleep_for(Interval);
  }
  llvm_unreachable("updateForever loop should never end");
}

static bool hasELFMagic(StringRef FilePath) {
  file_magic Type;
  std::error_code EC = identify_magic(FilePath, Type);
  if (EC)
    return false;
  switch (Type) {
  case file_magic::elf:
  case file_magic::elf_relocatable:
  case file_magic::elf_executable:
  case file_magic::elf_shared_object:
  case file_magic::elf_core:
    return true;
  default:
    return false;
  }
}

Error DebuginfodCollection::findBinaries(StringRef Path) {
  std::error_code EC;
  sys::fs::recursive_directory_iterator I(Twine(Path), EC), E;
  std::mutex IteratorMutex;
  ThreadPoolTaskGroup IteratorGroup(Pool);
  for (unsigned WorkerIndex = 0; WorkerIndex < Pool.getMaxConcurrency();
       WorkerIndex++) {
    IteratorGroup.async([&, this]() -> void {
      std::string FilePath;
      while (true) {
        {
          // Check if iteration is over or there is an error during iteration
          std::lock_guard<std::mutex> Guard(IteratorMutex);
          if (I == E || EC)
            return;
          // Grab a file path from the directory iterator and advance the
          // iterator.
          FilePath = I->path();
          I.increment(EC);
        }

        // Inspect the file at this path to determine if it is debuginfo.
        if (!hasELFMagic(FilePath))
          continue;

        Expected<object::OwningBinary<object::Binary>> BinOrErr =
            object::createBinary(FilePath);

        if (!BinOrErr) {
          consumeError(BinOrErr.takeError());
          continue;
        }
        object::Binary *Bin = std::move(BinOrErr.get().getBinary());
        if (!Bin->isObject())
          continue;

        // TODO: Support non-ELF binaries
        object::ELFObjectFileBase *Object =
            dyn_cast<object::ELFObjectFileBase>(Bin);
        if (!Object)
          continue;

        BuildIDRef ID = getBuildID(Object);
        if (ID.empty())
          continue;

        std::string IDString = buildIDToString(ID);
        if (Object->hasDebugInfo()) {
          std::lock_guard<sys::RWMutex> DebugBinariesGuard(DebugBinariesMutex);
          (void)DebugBinaries.try_emplace(IDString, std::move(FilePath));
        } else {
          std::lock_guard<sys::RWMutex> BinariesGuard(BinariesMutex);
          (void)Binaries.try_emplace(IDString, std::move(FilePath));
        }
      }
    });
  }
  IteratorGroup.wait();
  std::unique_lock<std::mutex> Guard(IteratorMutex);
  if (EC)
    return errorCodeToError(EC);
  return Error::success();
}

Expected<std::optional<std::string>>
DebuginfodCollection::getBinaryPath(BuildIDRef ID) {
  Log.push("getting binary path of ID " + buildIDToString(ID));
  std::shared_lock<sys::RWMutex> Guard(BinariesMutex);
  auto Loc = Binaries.find(buildIDToString(ID));
  if (Loc != Binaries.end()) {
    std::string Path = Loc->getValue();
    return Path;
  }
  return std::nullopt;
}

Expected<std::optional<std::string>>
DebuginfodCollection::getDebugBinaryPath(BuildIDRef ID) {
  Log.push("getting debug binary path of ID " + buildIDToString(ID));
  std::shared_lock<sys::RWMutex> Guard(DebugBinariesMutex);
  auto Loc = DebugBinaries.find(buildIDToString(ID));
  if (Loc != DebugBinaries.end()) {
    std::string Path = Loc->getValue();
    return Path;
  }
  return std::nullopt;
}

Expected<std::string> DebuginfodCollection::findBinaryPath(BuildIDRef ID) {
  {
    // Check collection; perform on-demand update if stale.
    Expected<std::optional<std::string>> PathOrErr = getBinaryPath(ID);
    if (!PathOrErr)
      return PathOrErr.takeError();
    std::optional<std::string> Path = *PathOrErr;
    if (!Path) {
      Expected<bool> UpdatedOrErr = updateIfStale();
      if (!UpdatedOrErr)
        return UpdatedOrErr.takeError();
      if (*UpdatedOrErr) {
        // Try once more.
        PathOrErr = getBinaryPath(ID);
        if (!PathOrErr)
          return PathOrErr.takeError();
        Path = *PathOrErr;
      }
    }
    if (Path)
      return *Path;
  }

  // Try federation.
  Expected<std::string> PathOrErr = getCachedOrDownloadExecutable(ID);
  if (!PathOrErr)
    consumeError(PathOrErr.takeError());

  // Fall back to debug binary.
  return findDebugBinaryPath(ID);
}

Expected<std::string> DebuginfodCollection::findDebugBinaryPath(BuildIDRef ID) {
  // Check collection; perform on-demand update if stale.
  Expected<std::optional<std::string>> PathOrErr = getDebugBinaryPath(ID);
  if (!PathOrErr)
    return PathOrErr.takeError();
  std::optional<std::string> Path = *PathOrErr;
  if (!Path) {
    Expected<bool> UpdatedOrErr = updateIfStale();
    if (!UpdatedOrErr)
      return UpdatedOrErr.takeError();
    if (*UpdatedOrErr) {
      // Try once more.
      PathOrErr = getBinaryPath(ID);
      if (!PathOrErr)
        return PathOrErr.takeError();
      Path = *PathOrErr;
    }
  }
  if (Path)
    return *Path;

  // Try federation.
  return getCachedOrDownloadDebuginfo(ID);
}

DebuginfodServer::DebuginfodServer(DebuginfodLog &Log,
                                   DebuginfodCollection &Collection)
    : Log(Log), Collection(Collection) {
  cantFail(
      Server.get(R"(/buildid/(.*)/debuginfo)", [&](HTTPServerRequest Request) {
        Log.push("GET " + Request.UrlPath);
        std::string IDString;
        if (!tryGetFromHex(Request.UrlPathMatches[0], IDString)) {
          Request.setResponse(
              {404, "text/plain", "Build ID is not a hex string\n"});
          return;
        }
        object::BuildID ID(IDString.begin(), IDString.end());
        Expected<std::string> PathOrErr = Collection.findDebugBinaryPath(ID);
        if (Error Err = PathOrErr.takeError()) {
          consumeError(std::move(Err));
          Request.setResponse({404, "text/plain", "Build ID not found\n"});
          return;
        }
        streamFile(Request, *PathOrErr);
      }));
  cantFail(
      Server.get(R"(/buildid/(.*)/executable)", [&](HTTPServerRequest Request) {
        Log.push("GET " + Request.UrlPath);
        std::string IDString;
        if (!tryGetFromHex(Request.UrlPathMatches[0], IDString)) {
          Request.setResponse(
              {404, "text/plain", "Build ID is not a hex string\n"});
          return;
        }
        object::BuildID ID(IDString.begin(), IDString.end());
        Expected<std::string> PathOrErr = Collection.findBinaryPath(ID);
        if (Error Err = PathOrErr.takeError()) {
          consumeError(std::move(Err));
          Request.setResponse({404, "text/plain", "Build ID not found\n"});
          return;
        }
        streamFile(Request, *PathOrErr);
      }));
}

} // namespace llvm
