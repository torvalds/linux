//===-- BinaryHolder.h - Utility class for accessing binaries -------------===//
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
#ifndef LLVM_TOOLS_DSYMUTIL_BINARYHOLDER_H
#define LLVM_TOOLS_DSYMUTIL_BINARYHOLDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"

#include <mutex>

namespace llvm {
namespace dsymutil {

/// The BinaryHolder class is responsible for creating and owning
/// ObjectFiles and their underlying MemoryBuffers. It differs from a simple
/// OwningBinary in that it handles accessing and caching of archives and its
/// members.
class BinaryHolder {
public:
  using TimestampTy = sys::TimePoint<std::chrono::seconds>;

  BinaryHolder(IntrusiveRefCntPtr<vfs::FileSystem> VFS, bool Verbose = false)
      : VFS(VFS), Verbose(Verbose) {}

  // Forward declarations for friend declaration.
  class ObjectEntry;
  class ArchiveEntry;

  /// Base class shared by cached entries, representing objects and archives.
  class EntryBase {
  protected:
    std::unique_ptr<MemoryBuffer> MemBuffer;
    std::unique_ptr<object::MachOUniversalBinary> FatBinary;
    std::string FatBinaryName;
  };

  /// Cached entry holding one or more (in case of a fat binary) object files.
  class ObjectEntry : public EntryBase {
  public:
    /// Load the given object binary in memory.
    Error load(IntrusiveRefCntPtr<vfs::FileSystem> VFS, StringRef Filename,
               TimestampTy Timestamp, bool Verbose = false);

    /// Access all owned ObjectFiles.
    std::vector<const object::ObjectFile *> getObjects() const;

    /// Access to a derived version of all the currently owned ObjectFiles. The
    /// conversion might be invalid, in which case an Error is returned.
    template <typename ObjectFileType>
    Expected<std::vector<const ObjectFileType *>> getObjectsAs() const {
      std::vector<const ObjectFileType *> Result;
      Result.reserve(Objects.size());
      for (auto &Object : Objects) {
        const auto *Derived = dyn_cast<ObjectFileType>(Object.get());
        if (!Derived)
          return errorCodeToError(object::object_error::invalid_file_type);
        Result.push_back(Derived);
      }
      return Result;
    }

    /// Access the owned ObjectFile with architecture \p T.
    Expected<const object::ObjectFile &> getObject(const Triple &T) const;

    /// Access to a derived version of the currently owned ObjectFile with
    /// architecture \p T. The conversion must be known to be valid.
    template <typename ObjectFileType>
    Expected<const ObjectFileType &> getObjectAs(const Triple &T) const {
      auto Object = getObject(T);
      if (!Object)
        return Object.takeError();
      return cast<ObjectFileType>(*Object);
    }

  private:
    std::vector<std::unique_ptr<object::ObjectFile>> Objects;
    friend ArchiveEntry;
  };

  /// Cached entry holding one or more (in the of a fat binary) archive files.
  class ArchiveEntry : public EntryBase {
  public:
    struct KeyTy {
      std::string Filename;
      TimestampTy Timestamp;

      KeyTy() {}
      KeyTy(StringRef Filename, TimestampTy Timestamp)
          : Filename(Filename.str()), Timestamp(Timestamp) {}
    };

    /// Load the given object binary in memory.
    Error load(IntrusiveRefCntPtr<vfs::FileSystem> VFS, StringRef Filename,
               TimestampTy Timestamp, bool Verbose = false);

    Expected<const ObjectEntry &> getObjectEntry(StringRef Filename,
                                                 TimestampTy Timestamp,
                                                 bool Verbose = false);

  private:
    std::vector<std::unique_ptr<object::Archive>> Archives;
    DenseMap<KeyTy, std::unique_ptr<ObjectEntry>> MemberCache;
    std::mutex MemberCacheMutex;
  };

  Expected<const ObjectEntry &>
  getObjectEntry(StringRef Filename, TimestampTy Timestamp = TimestampTy());

  void clear();
  void eraseObjectEntry(StringRef Filename);

private:
  /// Cache of static archives. Objects that are part of a static archive are
  /// stored under this object, rather than in the map below.
  StringMap<std::unique_ptr<ArchiveEntry>> ArchiveCache;
  StringMap<uint32_t> ArchiveRefCounter;
  std::mutex ArchiveCacheMutex;

  /// Object entries for objects that are not in a static archive.
  StringMap<std::unique_ptr<ObjectEntry>> ObjectCache;
  StringMap<uint32_t> ObjectRefCounter;
  std::mutex ObjectCacheMutex;

  /// Virtual File System instance.
  IntrusiveRefCntPtr<vfs::FileSystem> VFS;

  bool Verbose;
};

} // namespace dsymutil

template <> struct DenseMapInfo<dsymutil::BinaryHolder::ArchiveEntry::KeyTy> {

  static inline dsymutil::BinaryHolder::ArchiveEntry::KeyTy getEmptyKey() {
    return dsymutil::BinaryHolder::ArchiveEntry::KeyTy();
  }

  static inline dsymutil::BinaryHolder::ArchiveEntry::KeyTy getTombstoneKey() {
    return dsymutil::BinaryHolder::ArchiveEntry::KeyTy("/", {});
  }

  static unsigned
  getHashValue(const dsymutil::BinaryHolder::ArchiveEntry::KeyTy &K) {
    return hash_combine(DenseMapInfo<StringRef>::getHashValue(K.Filename),
                        DenseMapInfo<unsigned>::getHashValue(
                            K.Timestamp.time_since_epoch().count()));
  }

  static bool isEqual(const dsymutil::BinaryHolder::ArchiveEntry::KeyTy &LHS,
                      const dsymutil::BinaryHolder::ArchiveEntry::KeyTy &RHS) {
    return LHS.Filename == RHS.Filename && LHS.Timestamp == RHS.Timestamp;
  }
};

} // namespace llvm
#endif
