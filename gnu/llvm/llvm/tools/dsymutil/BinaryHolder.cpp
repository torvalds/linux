//===-- BinaryHolder.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that aims to be a dropin replacement for
// Darwin's dsymutil.
//
//===----------------------------------------------------------------------===//

#include "BinaryHolder.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace dsymutil {

static std::pair<StringRef, StringRef>
getArchiveAndObjectName(StringRef Filename) {
  StringRef Archive = Filename.substr(0, Filename.rfind('('));
  StringRef Object = Filename.substr(Archive.size() + 1).drop_back();
  return {Archive, Object};
}

static bool isArchive(StringRef Filename) { return Filename.ends_with(")"); }

static std::vector<MemoryBufferRef>
getMachOFatMemoryBuffers(StringRef Filename, MemoryBuffer &Mem,
                         object::MachOUniversalBinary &Fat) {
  std::vector<MemoryBufferRef> Buffers;
  StringRef FatData = Fat.getData();
  for (auto It = Fat.begin_objects(), End = Fat.end_objects(); It != End;
       ++It) {
    StringRef ObjData = FatData.substr(It->getOffset(), It->getSize());
    Buffers.emplace_back(ObjData, Filename);
  }
  return Buffers;
}

Error BinaryHolder::ArchiveEntry::load(IntrusiveRefCntPtr<vfs::FileSystem> VFS,
                                       StringRef Filename,
                                       TimestampTy Timestamp, bool Verbose) {
  StringRef ArchiveFilename = getArchiveAndObjectName(Filename).first;

  // Try to load archive and force it to be memory mapped.
  auto ErrOrBuff = (ArchiveFilename == "-")
                       ? MemoryBuffer::getSTDIN()
                       : VFS->getBufferForFile(ArchiveFilename, -1, false);
  if (auto Err = ErrOrBuff.getError())
    return errorCodeToError(Err);

  MemBuffer = std::move(*ErrOrBuff);

  if (Verbose)
    WithColor::note() << "loaded archive '" << ArchiveFilename << "'\n";

  // Load one or more archive buffers, depending on whether we're dealing with
  // a fat binary.
  std::vector<MemoryBufferRef> ArchiveBuffers;

  auto ErrOrFat =
      object::MachOUniversalBinary::create(MemBuffer->getMemBufferRef());
  if (!ErrOrFat) {
    consumeError(ErrOrFat.takeError());
    ArchiveBuffers.push_back(MemBuffer->getMemBufferRef());
  } else {
    FatBinary = std::move(*ErrOrFat);
    FatBinaryName = std::string(ArchiveFilename);
    ArchiveBuffers =
        getMachOFatMemoryBuffers(FatBinaryName, *MemBuffer, *FatBinary);
  }

  // Finally, try to load the archives.
  Archives.reserve(ArchiveBuffers.size());
  for (auto MemRef : ArchiveBuffers) {
    auto ErrOrArchive = object::Archive::create(MemRef);
    if (!ErrOrArchive)
      return ErrOrArchive.takeError();
    Archives.push_back(std::move(*ErrOrArchive));
  }

  return Error::success();
}

Error BinaryHolder::ObjectEntry::load(IntrusiveRefCntPtr<vfs::FileSystem> VFS,
                                      StringRef Filename, TimestampTy Timestamp,
                                      bool Verbose) {
  // Try to load regular binary and force it to be memory mapped.
  auto ErrOrBuff = (Filename == "-")
                       ? MemoryBuffer::getSTDIN()
                       : VFS->getBufferForFile(Filename, -1, false);
  if (auto Err = ErrOrBuff.getError())
    return errorCodeToError(Err);

  if (Filename != "-" && Timestamp != sys::TimePoint<>()) {
    llvm::ErrorOr<vfs::Status> Stat = VFS->status(Filename);
    if (!Stat)
      return errorCodeToError(Stat.getError());
    if (Timestamp != std::chrono::time_point_cast<std::chrono::seconds>(
                         Stat->getLastModificationTime()))
      WithColor::warning() << Filename
                           << ": timestamp mismatch between object file ("
                           << Stat->getLastModificationTime()
                           << ") and debug map (" << Timestamp << ")\n";
  }

  MemBuffer = std::move(*ErrOrBuff);

  if (Verbose)
    WithColor::note() << "loaded object.\n";

  // Load one or more object buffers, depending on whether we're dealing with a
  // fat binary.
  std::vector<MemoryBufferRef> ObjectBuffers;

  auto ErrOrFat =
      object::MachOUniversalBinary::create(MemBuffer->getMemBufferRef());
  if (!ErrOrFat) {
    consumeError(ErrOrFat.takeError());
    ObjectBuffers.push_back(MemBuffer->getMemBufferRef());
  } else {
    FatBinary = std::move(*ErrOrFat);
    FatBinaryName = std::string(Filename);
    ObjectBuffers =
        getMachOFatMemoryBuffers(FatBinaryName, *MemBuffer, *FatBinary);
  }

  Objects.reserve(ObjectBuffers.size());
  for (auto MemRef : ObjectBuffers) {
    auto ErrOrObjectFile = object::ObjectFile::createObjectFile(MemRef);
    if (!ErrOrObjectFile)
      return ErrOrObjectFile.takeError();
    Objects.push_back(std::move(*ErrOrObjectFile));
  }

  return Error::success();
}

std::vector<const object::ObjectFile *>
BinaryHolder::ObjectEntry::getObjects() const {
  std::vector<const object::ObjectFile *> Result;
  Result.reserve(Objects.size());
  for (auto &Object : Objects) {
    Result.push_back(Object.get());
  }
  return Result;
}
Expected<const object::ObjectFile &>
BinaryHolder::ObjectEntry::getObject(const Triple &T) const {
  for (const auto &Obj : Objects) {
    if (const auto *MachO = dyn_cast<object::MachOObjectFile>(Obj.get())) {
      if (MachO->getArchTriple().str() == T.str())
        return *MachO;
    } else if (Obj->getArch() == T.getArch())
      return *Obj;
  }
  return errorCodeToError(object::object_error::arch_not_found);
}

Expected<const BinaryHolder::ObjectEntry &>
BinaryHolder::ArchiveEntry::getObjectEntry(StringRef Filename,
                                           TimestampTy Timestamp,
                                           bool Verbose) {
  StringRef ArchiveFilename;
  StringRef ObjectFilename;
  std::tie(ArchiveFilename, ObjectFilename) = getArchiveAndObjectName(Filename);
  KeyTy Key = {ObjectFilename, Timestamp};

  // Try the cache first.
  std::lock_guard<std::mutex> Lock(MemberCacheMutex);
  if (MemberCache.count(Key))
    return *MemberCache[Key];

  // Create a new ObjectEntry, but don't add it to the cache yet. Loading of
  // the archive members might fail and we don't want to lock the whole archive
  // during this operation.
  auto OE = std::make_unique<ObjectEntry>();

  for (const auto &Archive : Archives) {
    Error Err = Error::success();
    for (const auto &Child : Archive->children(Err)) {
      if (auto NameOrErr = Child.getName()) {
        if (*NameOrErr == ObjectFilename) {
          auto ModTimeOrErr = Child.getLastModified();
          if (!ModTimeOrErr)
            return ModTimeOrErr.takeError();

          if (Timestamp != sys::TimePoint<>() &&
              Timestamp != std::chrono::time_point_cast<std::chrono::seconds>(
                               ModTimeOrErr.get())) {
            if (Verbose)
              WithColor::warning()
                  << *NameOrErr
                  << ": timestamp mismatch between archive member ("
                  << ModTimeOrErr.get() << ") and debug map (" << Timestamp
                  << ")\n";
            continue;
          }

          if (Verbose)
            WithColor::note() << "found member in archive.\n";

          auto ErrOrMem = Child.getMemoryBufferRef();
          if (!ErrOrMem)
            return ErrOrMem.takeError();

          auto ErrOrObjectFile =
              object::ObjectFile::createObjectFile(*ErrOrMem);
          if (!ErrOrObjectFile)
            return ErrOrObjectFile.takeError();

          OE->Objects.push_back(std::move(*ErrOrObjectFile));
        }
      }
    }
    if (Err)
      return std::move(Err);
  }

  if (OE->Objects.empty())
    return errorCodeToError(errc::no_such_file_or_directory);

  MemberCache[Key] = std::move(OE);
  return *MemberCache[Key];
}

Expected<const BinaryHolder::ObjectEntry &>
BinaryHolder::getObjectEntry(StringRef Filename, TimestampTy Timestamp) {
  if (Verbose)
    WithColor::note() << "trying to open '" << Filename << "'\n";

  // If this is an archive, we might have either the object or the archive
  // cached. In this case we can load it without accessing the file system.
  if (isArchive(Filename)) {
    StringRef ArchiveFilename = getArchiveAndObjectName(Filename).first;
    std::lock_guard<std::mutex> Lock(ArchiveCacheMutex);
    ArchiveRefCounter[ArchiveFilename]++;
    if (ArchiveCache.count(ArchiveFilename)) {
      return ArchiveCache[ArchiveFilename]->getObjectEntry(Filename, Timestamp,
                                                           Verbose);
    } else {
      auto AE = std::make_unique<ArchiveEntry>();
      auto Err = AE->load(VFS, Filename, Timestamp, Verbose);
      if (Err) {
        // Don't return the error here: maybe the file wasn't an archive.
        llvm::consumeError(std::move(Err));
      } else {
        ArchiveCache[ArchiveFilename] = std::move(AE);
        return ArchiveCache[ArchiveFilename]->getObjectEntry(
            Filename, Timestamp, Verbose);
      }
    }
  }

  // If this is an object, we might have it cached. If not we'll have to load
  // it from the file system and cache it now.
  std::lock_guard<std::mutex> Lock(ObjectCacheMutex);
  ObjectRefCounter[Filename]++;
  if (!ObjectCache.count(Filename)) {
    auto OE = std::make_unique<ObjectEntry>();
    auto Err = OE->load(VFS, Filename, Timestamp, Verbose);
    if (Err)
      return std::move(Err);
    ObjectCache[Filename] = std::move(OE);
  }

  return *ObjectCache[Filename];
}

void BinaryHolder::clear() {
  std::lock_guard<std::mutex> ArchiveLock(ArchiveCacheMutex);
  std::lock_guard<std::mutex> ObjectLock(ObjectCacheMutex);
  ArchiveCache.clear();
  ObjectCache.clear();
}

void BinaryHolder::eraseObjectEntry(StringRef Filename) {
  if (Verbose)
    WithColor::note() << "erasing '" << Filename << "' from cache\n";

  if (isArchive(Filename)) {
    StringRef ArchiveFilename = getArchiveAndObjectName(Filename).first;
    std::lock_guard<std::mutex> Lock(ArchiveCacheMutex);
    ArchiveRefCounter[ArchiveFilename]--;
    if (ArchiveRefCounter[ArchiveFilename] == 0)
      ArchiveCache.erase(ArchiveFilename);
    return;
  }

  std::lock_guard<std::mutex> Lock(ObjectCacheMutex);
  ObjectRefCounter[Filename]--;
  if (ObjectRefCounter[Filename] == 0)
    ObjectCache.erase(Filename);
}

} // namespace dsymutil
} // namespace llvm
