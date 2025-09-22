//===- clang/Basic/FileEntry.h - File references ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines interfaces for clang::FileEntry and clang::FileEntryRef.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_FILEENTRY_H
#define LLVM_CLANG_BASIC_FILEENTRY_H

#include "clang/Basic/CustomizableOptional.h"
#include "clang/Basic/DirectoryEntry.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem/UniqueID.h"

#include <optional>
#include <utility>

namespace llvm {

class MemoryBuffer;

namespace vfs {

class File;

} // namespace vfs
} // namespace llvm

namespace clang {

class FileEntryRef;

namespace optional_detail {

/// Forward declare a template specialization for OptionalStorage.
template <> class OptionalStorage<clang::FileEntryRef>;

} // namespace optional_detail

class FileEntry;

/// A reference to a \c FileEntry that includes the name of the file as it was
/// accessed by the FileManager's client.
class FileEntryRef {
public:
  /// The name of this FileEntry. If a VFS uses 'use-external-name', this is
  /// the redirected name. See getRequestedName().
  StringRef getName() const { return getBaseMapEntry().first(); }

  /// The name of this FileEntry, as originally requested without applying any
  /// remappings for VFS 'use-external-name'.
  ///
  /// FIXME: this should be the semantics of getName(). See comment in
  /// FileManager::getFileRef().
  StringRef getNameAsRequested() const { return ME->first(); }

  const FileEntry &getFileEntry() const {
    return *getBaseMapEntry().second->V.get<FileEntry *>();
  }
  DirectoryEntryRef getDir() const { return ME->second->Dir; }

  inline off_t getSize() const;
  inline unsigned getUID() const;
  inline const llvm::sys::fs::UniqueID &getUniqueID() const;
  inline time_t getModificationTime() const;
  inline bool isNamedPipe() const;
  inline void closeFile() const;

  /// Check if the underlying FileEntry is the same, intentially ignoring
  /// whether the file was referenced with the same spelling of the filename.
  friend bool operator==(const FileEntryRef &LHS, const FileEntryRef &RHS) {
    return &LHS.getFileEntry() == &RHS.getFileEntry();
  }
  friend bool operator==(const FileEntry *LHS, const FileEntryRef &RHS) {
    return LHS == &RHS.getFileEntry();
  }
  friend bool operator==(const FileEntryRef &LHS, const FileEntry *RHS) {
    return &LHS.getFileEntry() == RHS;
  }
  friend bool operator!=(const FileEntryRef &LHS, const FileEntryRef &RHS) {
    return !(LHS == RHS);
  }
  friend bool operator!=(const FileEntry *LHS, const FileEntryRef &RHS) {
    return !(LHS == RHS);
  }
  friend bool operator!=(const FileEntryRef &LHS, const FileEntry *RHS) {
    return !(LHS == RHS);
  }

  /// Hash code is based on the FileEntry, not the specific named reference,
  /// just like operator==.
  friend llvm::hash_code hash_value(FileEntryRef Ref) {
    return llvm::hash_value(&Ref.getFileEntry());
  }

  struct MapValue;

  /// Type used in the StringMap.
  using MapEntry = llvm::StringMapEntry<llvm::ErrorOr<MapValue>>;

  /// Type stored in the StringMap.
  struct MapValue {
    /// The pointer at another MapEntry is used when the FileManager should
    /// silently forward from one name to another, which occurs in Redirecting
    /// VFSs that use external names. In that case, the \c FileEntryRef
    /// returned by the \c FileManager will have the external name, and not the
    /// name that was used to lookup the file.
    llvm::PointerUnion<FileEntry *, const MapEntry *> V;

    /// Directory the file was found in.
    DirectoryEntryRef Dir;

    MapValue() = delete;
    MapValue(FileEntry &FE, DirectoryEntryRef Dir) : V(&FE), Dir(Dir) {}
    MapValue(MapEntry &ME, DirectoryEntryRef Dir) : V(&ME), Dir(Dir) {}
  };

  /// Check if RHS referenced the file in exactly the same way.
  bool isSameRef(const FileEntryRef &RHS) const { return ME == RHS.ME; }

  /// Allow FileEntryRef to degrade into 'const FileEntry*' to facilitate
  /// incremental adoption.
  ///
  /// The goal is to avoid code churn due to dances like the following:
  /// \code
  /// // Old code.
  /// lvalue = rvalue;
  ///
  /// // Temporary code from an incremental patch.
  /// lvalue = &rvalue.getFileEntry();
  ///
  /// // Final code.
  /// lvalue = rvalue;
  /// \endcode
  ///
  /// FIXME: Once FileEntryRef is "everywhere" and FileEntry::LastRef and
  /// FileEntry::getName have been deleted, delete this implicit conversion.
  operator const FileEntry *() const { return &getFileEntry(); }

  FileEntryRef() = delete;
  explicit FileEntryRef(const MapEntry &ME) : ME(&ME) {
    assert(ME.second && "Expected payload");
    assert(ME.second->V && "Expected non-null");
  }

  /// Expose the underlying MapEntry to simplify packing in a PointerIntPair or
  /// PointerUnion and allow construction in Optional.
  const clang::FileEntryRef::MapEntry &getMapEntry() const { return *ME; }

  /// Retrieve the base MapEntry after redirects.
  const MapEntry &getBaseMapEntry() const {
    const MapEntry *Base = ME;
    while (const auto *Next = Base->second->V.dyn_cast<const MapEntry *>())
      Base = Next;
    return *Base;
  }

private:
  friend class FileMgr::MapEntryOptionalStorage<FileEntryRef>;
  struct optional_none_tag {};

  // Private constructor for use by OptionalStorage.
  FileEntryRef(optional_none_tag) : ME(nullptr) {}
  bool hasOptionalValue() const { return ME; }

  friend struct llvm::DenseMapInfo<FileEntryRef>;
  struct dense_map_empty_tag {};
  struct dense_map_tombstone_tag {};

  // Private constructors for use by DenseMapInfo.
  FileEntryRef(dense_map_empty_tag)
      : ME(llvm::DenseMapInfo<const MapEntry *>::getEmptyKey()) {}
  FileEntryRef(dense_map_tombstone_tag)
      : ME(llvm::DenseMapInfo<const MapEntry *>::getTombstoneKey()) {}
  bool isSpecialDenseMapKey() const {
    return isSameRef(FileEntryRef(dense_map_empty_tag())) ||
           isSameRef(FileEntryRef(dense_map_tombstone_tag()));
  }

  const MapEntry *ME;
};

static_assert(sizeof(FileEntryRef) == sizeof(const FileEntry *),
              "FileEntryRef must avoid size overhead");

static_assert(std::is_trivially_copyable<FileEntryRef>::value,
              "FileEntryRef must be trivially copyable");

using OptionalFileEntryRef = CustomizableOptional<FileEntryRef>;

namespace optional_detail {

/// Customize OptionalStorage<FileEntryRef> to use FileEntryRef and its
/// optional_none_tag to keep it the size of a single pointer.
template <>
class OptionalStorage<clang::FileEntryRef>
    : public clang::FileMgr::MapEntryOptionalStorage<clang::FileEntryRef> {
  using StorageImpl =
      clang::FileMgr::MapEntryOptionalStorage<clang::FileEntryRef>;

public:
  OptionalStorage() = default;

  template <class... ArgTypes>
  explicit OptionalStorage(std::in_place_t, ArgTypes &&...Args)
      : StorageImpl(std::in_place_t{}, std::forward<ArgTypes>(Args)...) {}

  OptionalStorage &operator=(clang::FileEntryRef Ref) {
    StorageImpl::operator=(Ref);
    return *this;
  }
};

static_assert(sizeof(OptionalFileEntryRef) == sizeof(FileEntryRef),
              "OptionalFileEntryRef must avoid size overhead");

static_assert(std::is_trivially_copyable<OptionalFileEntryRef>::value,
              "OptionalFileEntryRef should be trivially copyable");

} // end namespace optional_detail
} // namespace clang

namespace llvm {

/// Specialisation of DenseMapInfo for FileEntryRef.
template <> struct DenseMapInfo<clang::FileEntryRef> {
  static inline clang::FileEntryRef getEmptyKey() {
    return clang::FileEntryRef(clang::FileEntryRef::dense_map_empty_tag());
  }

  static inline clang::FileEntryRef getTombstoneKey() {
    return clang::FileEntryRef(clang::FileEntryRef::dense_map_tombstone_tag());
  }

  static unsigned getHashValue(clang::FileEntryRef Val) {
    return hash_value(Val);
  }

  static bool isEqual(clang::FileEntryRef LHS, clang::FileEntryRef RHS) {
    // Catch the easy cases: both empty, both tombstone, or the same ref.
    if (LHS.isSameRef(RHS))
      return true;

    // Confirm LHS and RHS are valid.
    if (LHS.isSpecialDenseMapKey() || RHS.isSpecialDenseMapKey())
      return false;

    // It's safe to use operator==.
    return LHS == RHS;
  }

  /// Support for finding `const FileEntry *` in a `DenseMap<FileEntryRef, T>`.
  /// @{
  static unsigned getHashValue(const clang::FileEntry *Val) {
    return llvm::hash_value(Val);
  }
  static bool isEqual(const clang::FileEntry *LHS, clang::FileEntryRef RHS) {
    if (RHS.isSpecialDenseMapKey())
      return false;
    return LHS == RHS;
  }
  /// @}
};

} // end namespace llvm

namespace clang {

inline bool operator==(const FileEntry *LHS, const OptionalFileEntryRef &RHS) {
  return LHS == (RHS ? &RHS->getFileEntry() : nullptr);
}
inline bool operator==(const OptionalFileEntryRef &LHS, const FileEntry *RHS) {
  return (LHS ? &LHS->getFileEntry() : nullptr) == RHS;
}
inline bool operator!=(const FileEntry *LHS, const OptionalFileEntryRef &RHS) {
  return !(LHS == RHS);
}
inline bool operator!=(const OptionalFileEntryRef &LHS, const FileEntry *RHS) {
  return !(LHS == RHS);
}

/// Cached information about one file (either on disk
/// or in the virtual file system).
///
/// If the 'File' member is valid, then this FileEntry has an open file
/// descriptor for the file.
class FileEntry {
  friend class FileManager;
  friend class FileEntryTestHelper;
  FileEntry();
  FileEntry(const FileEntry &) = delete;
  FileEntry &operator=(const FileEntry &) = delete;

  std::string RealPathName;   // Real path to the file; could be empty.
  off_t Size = 0;             // File size in bytes.
  time_t ModTime = 0;         // Modification time of file.
  const DirectoryEntry *Dir = nullptr; // Directory file lives in.
  llvm::sys::fs::UniqueID UniqueID;
  unsigned UID = 0; // A unique (small) ID for the file.
  bool IsNamedPipe = false;

  /// The open file, if it is owned by the \p FileEntry.
  mutable std::unique_ptr<llvm::vfs::File> File;

  /// The file content, if it is owned by the \p FileEntry.
  std::unique_ptr<llvm::MemoryBuffer> Content;

public:
  ~FileEntry();

  StringRef tryGetRealPathName() const { return RealPathName; }
  off_t getSize() const { return Size; }
  unsigned getUID() const { return UID; }
  const llvm::sys::fs::UniqueID &getUniqueID() const { return UniqueID; }
  time_t getModificationTime() const { return ModTime; }

  /// Return the directory the file lives in.
  const DirectoryEntry *getDir() const { return Dir; }

  /// Check whether the file is a named pipe (and thus can't be opened by
  /// the native FileManager methods).
  bool isNamedPipe() const { return IsNamedPipe; }

  void closeFile() const;
};

off_t FileEntryRef::getSize() const { return getFileEntry().getSize(); }

unsigned FileEntryRef::getUID() const { return getFileEntry().getUID(); }

const llvm::sys::fs::UniqueID &FileEntryRef::getUniqueID() const {
  return getFileEntry().getUniqueID();
}

time_t FileEntryRef::getModificationTime() const {
  return getFileEntry().getModificationTime();
}

bool FileEntryRef::isNamedPipe() const { return getFileEntry().isNamedPipe(); }

void FileEntryRef::closeFile() const { getFileEntry().closeFile(); }

} // end namespace clang

#endif // LLVM_CLANG_BASIC_FILEENTRY_H
