//===- VirtualFileSystem.h - Virtual File System Layer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the virtual file system interface vfs::FileSystem.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALFILESYSTEM_H
#define LLVM_SUPPORT_VIRTUALFILESYSTEM_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include <cassert>
#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;
class Twine;

namespace vfs {

/// The result of a \p status operation.
class Status {
  std::string Name;
  llvm::sys::fs::UniqueID UID;
  llvm::sys::TimePoint<> MTime;
  uint32_t User;
  uint32_t Group;
  uint64_t Size;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::status_error;
  llvm::sys::fs::perms Perms;

public:
  /// Whether this entity has an external path different from the virtual path,
  /// and the external path is exposed by leaking it through the abstraction.
  /// For example, a RedirectingFileSystem will set this for paths where
  /// UseExternalName is true.
  ///
  /// FIXME: Currently the external path is exposed by replacing the virtual
  /// path in this Status object. Instead, we should leave the path in the
  /// Status intact (matching the requested virtual path) - see
  /// FileManager::getFileRef for how we plan to fix this.
  bool ExposesExternalVFSPath = false;

  Status() = default;
  Status(const llvm::sys::fs::file_status &Status);
  Status(const Twine &Name, llvm::sys::fs::UniqueID UID,
         llvm::sys::TimePoint<> MTime, uint32_t User, uint32_t Group,
         uint64_t Size, llvm::sys::fs::file_type Type,
         llvm::sys::fs::perms Perms);

  /// Get a copy of a Status with a different size.
  static Status copyWithNewSize(const Status &In, uint64_t NewSize);
  /// Get a copy of a Status with a different name.
  static Status copyWithNewName(const Status &In, const Twine &NewName);
  static Status copyWithNewName(const llvm::sys::fs::file_status &In,
                                const Twine &NewName);

  /// Returns the name that should be used for this file or directory.
  StringRef getName() const { return Name; }

  /// @name Status interface from llvm::sys::fs
  /// @{
  llvm::sys::fs::file_type getType() const { return Type; }
  llvm::sys::fs::perms getPermissions() const { return Perms; }
  llvm::sys::TimePoint<> getLastModificationTime() const { return MTime; }
  llvm::sys::fs::UniqueID getUniqueID() const { return UID; }
  uint32_t getUser() const { return User; }
  uint32_t getGroup() const { return Group; }
  uint64_t getSize() const { return Size; }
  /// @}
  /// @name Status queries
  /// These are static queries in llvm::sys::fs.
  /// @{
  bool equivalent(const Status &Other) const;
  bool isDirectory() const;
  bool isRegularFile() const;
  bool isOther() const;
  bool isSymlink() const;
  bool isStatusKnown() const;
  bool exists() const;
  /// @}
};

/// Represents an open file.
class File {
public:
  /// Destroy the file after closing it (if open).
  /// Sub-classes should generally call close() inside their destructors.  We
  /// cannot do that from the base class, since close is virtual.
  virtual ~File();

  /// Get the status of the file.
  virtual llvm::ErrorOr<Status> status() = 0;

  /// Get the name of the file
  virtual llvm::ErrorOr<std::string> getName() {
    if (auto Status = status())
      return Status->getName().str();
    else
      return Status.getError();
  }

  /// Get the contents of the file as a \p MemoryBuffer.
  virtual llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBuffer(const Twine &Name, int64_t FileSize = -1,
            bool RequiresNullTerminator = true, bool IsVolatile = false) = 0;

  /// Closes the file.
  virtual std::error_code close() = 0;

  // Get the same file with a different path.
  static ErrorOr<std::unique_ptr<File>>
  getWithPath(ErrorOr<std::unique_ptr<File>> Result, const Twine &P);

protected:
  // Set the file's underlying path.
  virtual void setPath(const Twine &Path) {}
};

/// A member of a directory, yielded by a directory_iterator.
/// Only information available on most platforms is included.
class directory_entry {
  std::string Path;
  llvm::sys::fs::file_type Type = llvm::sys::fs::file_type::type_unknown;

public:
  directory_entry() = default;
  directory_entry(std::string Path, llvm::sys::fs::file_type Type)
      : Path(std::move(Path)), Type(Type) {}

  llvm::StringRef path() const { return Path; }
  llvm::sys::fs::file_type type() const { return Type; }
};

namespace detail {

/// An interface for virtual file systems to provide an iterator over the
/// (non-recursive) contents of a directory.
struct DirIterImpl {
  virtual ~DirIterImpl();

  /// Sets \c CurrentEntry to the next entry in the directory on success,
  /// to directory_entry() at end,  or returns a system-defined \c error_code.
  virtual std::error_code increment() = 0;

  directory_entry CurrentEntry;
};

} // namespace detail

/// An input iterator over the entries in a virtual path, similar to
/// llvm::sys::fs::directory_iterator.
class directory_iterator {
  std::shared_ptr<detail::DirIterImpl> Impl; // Input iterator semantics on copy

public:
  directory_iterator(std::shared_ptr<detail::DirIterImpl> I)
      : Impl(std::move(I)) {
    assert(Impl.get() != nullptr && "requires non-null implementation");
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
  }

  /// Construct an 'end' iterator.
  directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  directory_iterator &increment(std::error_code &EC) {
    assert(Impl && "attempting to increment past end");
    EC = Impl->increment();
    if (Impl->CurrentEntry.path().empty())
      Impl.reset(); // Normalize the end iterator to Impl == nullptr.
    return *this;
  }

  const directory_entry &operator*() const { return Impl->CurrentEntry; }
  const directory_entry *operator->() const { return &Impl->CurrentEntry; }

  bool operator==(const directory_iterator &RHS) const {
    if (Impl && RHS.Impl)
      return Impl->CurrentEntry.path() == RHS.Impl->CurrentEntry.path();
    return !Impl && !RHS.Impl;
  }
  bool operator!=(const directory_iterator &RHS) const {
    return !(*this == RHS);
  }
};

class FileSystem;

namespace detail {

/// Keeps state for the recursive_directory_iterator.
struct RecDirIterState {
  std::vector<directory_iterator> Stack;
  bool HasNoPushRequest = false;
};

} // end namespace detail

/// An input iterator over the recursive contents of a virtual path,
/// similar to llvm::sys::fs::recursive_directory_iterator.
class recursive_directory_iterator {
  FileSystem *FS;
  std::shared_ptr<detail::RecDirIterState>
      State; // Input iterator semantics on copy.

public:
  recursive_directory_iterator(FileSystem &FS, const Twine &Path,
                               std::error_code &EC);

  /// Construct an 'end' iterator.
  recursive_directory_iterator() = default;

  /// Equivalent to operator++, with an error code.
  recursive_directory_iterator &increment(std::error_code &EC);

  const directory_entry &operator*() const { return *State->Stack.back(); }
  const directory_entry *operator->() const { return &*State->Stack.back(); }

  bool operator==(const recursive_directory_iterator &Other) const {
    return State == Other.State; // identity
  }
  bool operator!=(const recursive_directory_iterator &RHS) const {
    return !(*this == RHS);
  }

  /// Gets the current level. Starting path is at level 0.
  int level() const {
    assert(!State->Stack.empty() &&
           "Cannot get level without any iteration state");
    return State->Stack.size() - 1;
  }

  void no_push() { State->HasNoPushRequest = true; }
};

/// The virtual file system interface.
class FileSystem : public llvm::ThreadSafeRefCountedBase<FileSystem>,
                   public RTTIExtends<FileSystem, RTTIRoot> {
public:
  static const char ID;
  virtual ~FileSystem();

  /// Get the status of the entry at \p Path, if one exists.
  virtual llvm::ErrorOr<Status> status(const Twine &Path) = 0;

  /// Get a \p File object for the file at \p Path, if one exists.
  virtual llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) = 0;

  /// This is a convenience method that opens a file, gets its content and then
  /// closes the file.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>>
  getBufferForFile(const Twine &Name, int64_t FileSize = -1,
                   bool RequiresNullTerminator = true, bool IsVolatile = false);

  /// Get a directory_iterator for \p Dir.
  /// \note The 'end' iterator is directory_iterator().
  virtual directory_iterator dir_begin(const Twine &Dir,
                                       std::error_code &EC) = 0;

  /// Set the working directory. This will affect all following operations on
  /// this file system and may propagate down for nested file systems.
  virtual std::error_code setCurrentWorkingDirectory(const Twine &Path) = 0;

  /// Get the working directory of this file system.
  virtual llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const = 0;

  /// Gets real path of \p Path e.g. collapse all . and .. patterns, resolve
  /// symlinks. For real file system, this uses `llvm::sys::fs::real_path`.
  /// This returns errc::operation_not_permitted if not implemented by subclass.
  virtual std::error_code getRealPath(const Twine &Path,
                                      SmallVectorImpl<char> &Output);

  /// Check whether \p Path exists. By default this uses \c status(), but
  /// filesystems may provide a more efficient implementation if available.
  virtual bool exists(const Twine &Path);

  /// Is the file mounted on a local filesystem?
  virtual std::error_code isLocal(const Twine &Path, bool &Result);

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the current directory if it is not already.
  /// An empty \a Path will result in the current directory.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <current-directory>/relative/../path
  ///
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  virtual std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const;

  /// \returns true if \p A and \p B represent the same file, or an error or
  /// false if they do not.
  llvm::ErrorOr<bool> equivalent(const Twine &A, const Twine &B);

  enum class PrintType { Summary, Contents, RecursiveContents };
  void print(raw_ostream &OS, PrintType Type = PrintType::Contents,
             unsigned IndentLevel = 0) const {
    printImpl(OS, Type, IndentLevel);
  }

  using VisitCallbackTy = llvm::function_ref<void(FileSystem &)>;
  virtual void visitChildFileSystems(VisitCallbackTy Callback) {}
  void visit(VisitCallbackTy Callback) {
    Callback(*this);
    visitChildFileSystems(Callback);
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const;
#endif

protected:
  virtual void printImpl(raw_ostream &OS, PrintType Type,
                         unsigned IndentLevel) const {
    printIndent(OS, IndentLevel);
    OS << "FileSystem\n";
  }

  void printIndent(raw_ostream &OS, unsigned IndentLevel) const {
    for (unsigned i = 0; i < IndentLevel; ++i)
      OS << "  ";
  }
};

/// Gets an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
/// The working directory is linked to the process's working directory.
/// (This is usually thread-hostile).
IntrusiveRefCntPtr<FileSystem> getRealFileSystem();

/// Create an \p vfs::FileSystem for the 'real' file system, as seen by
/// the operating system.
/// It has its own working directory, independent of (but initially equal to)
/// that of the process.
std::unique_ptr<FileSystem> createPhysicalFileSystem();

/// A file system that allows overlaying one \p AbstractFileSystem on top
/// of another.
///
/// Consists of a stack of >=1 \p FileSystem objects, which are treated as being
/// one merged file system. When there is a directory that exists in more than
/// one file system, the \p OverlayFileSystem contains a directory containing
/// the union of their contents.  The attributes (permissions, etc.) of the
/// top-most (most recently added) directory are used.  When there is a file
/// that exists in more than one file system, the file in the top-most file
/// system overrides the other(s).
class OverlayFileSystem : public RTTIExtends<OverlayFileSystem, FileSystem> {
  using FileSystemList = SmallVector<IntrusiveRefCntPtr<FileSystem>, 1>;

  /// The stack of file systems, implemented as a list in order of
  /// their addition.
  FileSystemList FSList;

public:
  static const char ID;
  OverlayFileSystem(IntrusiveRefCntPtr<FileSystem> Base);

  /// Pushes a file system on top of the stack.
  void pushOverlay(IntrusiveRefCntPtr<FileSystem> FS);

  llvm::ErrorOr<Status> status(const Twine &Path) override;
  bool exists(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;

  using iterator = FileSystemList::reverse_iterator;
  using const_iterator = FileSystemList::const_reverse_iterator;
  using reverse_iterator = FileSystemList::iterator;
  using const_reverse_iterator = FileSystemList::const_iterator;
  using range = iterator_range<iterator>;
  using const_range = iterator_range<const_iterator>;

  /// Get an iterator pointing to the most recently added file system.
  iterator overlays_begin() { return FSList.rbegin(); }
  const_iterator overlays_begin() const { return FSList.rbegin(); }

  /// Get an iterator pointing one-past the least recently added file system.
  iterator overlays_end() { return FSList.rend(); }
  const_iterator overlays_end() const { return FSList.rend(); }

  /// Get an iterator pointing to the least recently added file system.
  reverse_iterator overlays_rbegin() { return FSList.begin(); }
  const_reverse_iterator overlays_rbegin() const { return FSList.begin(); }

  /// Get an iterator pointing one-past the most recently added file system.
  reverse_iterator overlays_rend() { return FSList.end(); }
  const_reverse_iterator overlays_rend() const { return FSList.end(); }

  range overlays_range() { return llvm::reverse(FSList); }
  const_range overlays_range() const { return llvm::reverse(FSList); }

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
  void visitChildFileSystems(VisitCallbackTy Callback) override;
};

/// By default, this delegates all calls to the underlying file system. This
/// is useful when derived file systems want to override some calls and still
/// proxy other calls.
class ProxyFileSystem : public RTTIExtends<ProxyFileSystem, FileSystem> {
public:
  static const char ID;
  explicit ProxyFileSystem(IntrusiveRefCntPtr<FileSystem> FS)
      : FS(std::move(FS)) {}

  llvm::ErrorOr<Status> status(const Twine &Path) override {
    return FS->status(Path);
  }
  bool exists(const Twine &Path) override { return FS->exists(Path); }
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override {
    return FS->openFileForRead(Path);
  }
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override {
    return FS->dir_begin(Dir, EC);
  }
  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override {
    return FS->getCurrentWorkingDirectory();
  }
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override {
    return FS->setCurrentWorkingDirectory(Path);
  }
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override {
    return FS->getRealPath(Path, Output);
  }
  std::error_code isLocal(const Twine &Path, bool &Result) override {
    return FS->isLocal(Path, Result);
  }

protected:
  FileSystem &getUnderlyingFS() const { return *FS; }
  void visitChildFileSystems(VisitCallbackTy Callback) override {
    if (FS) {
      Callback(*FS);
      FS->visitChildFileSystems(Callback);
    }
  }

private:
  IntrusiveRefCntPtr<FileSystem> FS;

  virtual void anchor() override;
};

namespace detail {

class InMemoryDirectory;
class InMemoryNode;

struct NewInMemoryNodeInfo {
  llvm::sys::fs::UniqueID DirUID;
  StringRef Path;
  StringRef Name;
  time_t ModificationTime;
  std::unique_ptr<llvm::MemoryBuffer> Buffer;
  uint32_t User;
  uint32_t Group;
  llvm::sys::fs::file_type Type;
  llvm::sys::fs::perms Perms;

  Status makeStatus() const;
};

class NamedNodeOrError {
  ErrorOr<std::pair<llvm::SmallString<128>, const detail::InMemoryNode *>>
      Value;

public:
  NamedNodeOrError(llvm::SmallString<128> Name,
                   const detail::InMemoryNode *Node)
      : Value(std::make_pair(Name, Node)) {}
  NamedNodeOrError(std::error_code EC) : Value(EC) {}
  NamedNodeOrError(llvm::errc EC) : Value(EC) {}

  StringRef getName() const { return (*Value).first; }
  explicit operator bool() const { return static_cast<bool>(Value); }
  operator std::error_code() const { return Value.getError(); }
  std::error_code getError() const { return Value.getError(); }
  const detail::InMemoryNode *operator*() const { return (*Value).second; }
};

} // namespace detail

/// An in-memory file system.
class InMemoryFileSystem : public RTTIExtends<InMemoryFileSystem, FileSystem> {
  std::unique_ptr<detail::InMemoryDirectory> Root;
  std::string WorkingDirectory;
  bool UseNormalizedPaths = true;

public:
  static const char ID;

private:
  using MakeNodeFn = llvm::function_ref<std::unique_ptr<detail::InMemoryNode>(
      detail::NewInMemoryNodeInfo)>;

  /// Create node with \p MakeNode and add it into this filesystem at \p Path.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer,
               std::optional<uint32_t> User, std::optional<uint32_t> Group,
               std::optional<llvm::sys::fs::file_type> Type,
               std::optional<llvm::sys::fs::perms> Perms, MakeNodeFn MakeNode);

  /// Looks up the in-memory node for the path \p P.
  /// If \p FollowFinalSymlink is true, the returned node is guaranteed to
  /// not be a symlink and its path may differ from \p P.
  detail::NamedNodeOrError lookupNode(const Twine &P, bool FollowFinalSymlink,
                                      size_t SymlinkDepth = 0) const;

  class DirIterator;

public:
  explicit InMemoryFileSystem(bool UseNormalizedPaths = true);
  ~InMemoryFileSystem() override;

  /// Add a file containing a buffer or a directory to the VFS with a
  /// path. The VFS owns the buffer.  If present, User, Group, Type
  /// and Perms apply to the newly-created file or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  bool addFile(const Twine &Path, time_t ModificationTime,
               std::unique_ptr<llvm::MemoryBuffer> Buffer,
               std::optional<uint32_t> User = std::nullopt,
               std::optional<uint32_t> Group = std::nullopt,
               std::optional<llvm::sys::fs::file_type> Type = std::nullopt,
               std::optional<llvm::sys::fs::perms> Perms = std::nullopt);

  /// Add a hard link to a file.
  ///
  /// Here hard links are not intended to be fully equivalent to the classical
  /// filesystem. Both the hard link and the file share the same buffer and
  /// status (and thus have the same UniqueID). Because of this there is no way
  /// to distinguish between the link and the file after the link has been
  /// added.
  ///
  /// The \p Target path must be an existing file or a hardlink. The
  /// \p NewLink file must not have been added before. The \p Target
  /// path must not be a directory. The \p NewLink node is added as a hard
  /// link which points to the resolved file of \p Target node.
  /// \return true if the above condition is satisfied and hardlink was
  /// successfully created, false otherwise.
  bool addHardLink(const Twine &NewLink, const Twine &Target);

  /// Arbitrary max depth to search through symlinks. We can get into problems
  /// if a link links to a link that links back to the link, for example.
  static constexpr size_t MaxSymlinkDepth = 16;

  /// Add a symbolic link. Unlike a HardLink, because \p Target doesn't need
  /// to refer to a file (or refer to anything, as it happens). Also, an
  /// in-memory directory for \p Target isn't automatically created.
  bool
  addSymbolicLink(const Twine &NewLink, const Twine &Target,
                  time_t ModificationTime,
                  std::optional<uint32_t> User = std::nullopt,
                  std::optional<uint32_t> Group = std::nullopt,
                  std::optional<llvm::sys::fs::perms> Perms = std::nullopt);

  /// Add a buffer to the VFS with a path. The VFS does not own the buffer.
  /// If present, User, Group, Type and Perms apply to the newly-created file
  /// or directory.
  /// \return true if the file or directory was successfully added,
  /// false if the file or directory already exists in the file system with
  /// different contents.
  bool addFileNoOwn(const Twine &Path, time_t ModificationTime,
                    const llvm::MemoryBufferRef &Buffer,
                    std::optional<uint32_t> User = std::nullopt,
                    std::optional<uint32_t> Group = std::nullopt,
                    std::optional<llvm::sys::fs::file_type> Type = std::nullopt,
                    std::optional<llvm::sys::fs::perms> Perms = std::nullopt);

  std::string toString() const;

  /// Return true if this file system normalizes . and .. in paths.
  bool useNormalizedPaths() const { return UseNormalizedPaths; }

  llvm::ErrorOr<Status> status(const Twine &Path) override;
  llvm::ErrorOr<std::unique_ptr<File>>
  openFileForRead(const Twine &Path) override;
  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override {
    return WorkingDirectory;
  }
  /// Canonicalizes \p Path by combining with the current working
  /// directory and normalizing the path (e.g. remove dots). If the current
  /// working directory is not set, this returns errc::operation_not_permitted.
  ///
  /// This doesn't resolve symlinks as they are not supported in in-memory file
  /// system.
  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;
  std::error_code isLocal(const Twine &Path, bool &Result) override;
  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
};

/// Get a globally unique ID for a virtual file or directory.
llvm::sys::fs::UniqueID getNextVirtualUniqueID();

/// Gets a \p FileSystem for a virtual file system described in YAML
/// format.
std::unique_ptr<FileSystem>
getVFSFromYAML(std::unique_ptr<llvm::MemoryBuffer> Buffer,
               llvm::SourceMgr::DiagHandlerTy DiagHandler,
               StringRef YAMLFilePath, void *DiagContext = nullptr,
               IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

struct YAMLVFSEntry {
  template <typename T1, typename T2>
  YAMLVFSEntry(T1 &&VPath, T2 &&RPath, bool IsDirectory = false)
      : VPath(std::forward<T1>(VPath)), RPath(std::forward<T2>(RPath)),
        IsDirectory(IsDirectory) {}
  std::string VPath;
  std::string RPath;
  bool IsDirectory = false;
};

class RedirectingFSDirIterImpl;
class RedirectingFileSystemParser;

/// A virtual file system parsed from a YAML file.
///
/// Currently, this class allows creating virtual files and directories. Virtual
/// files map to existing external files in \c ExternalFS, and virtual
/// directories may either map to existing directories in \c ExternalFS or list
/// their contents in the form of other virtual directories and/or files.
///
/// The basic structure of the parsed file is:
/// \verbatim
/// {
///   'version': <version number>,
///   <optional configuration>
///   'roots': [
///              <directory entries>
///            ]
/// }
/// \endverbatim
/// The roots may be absolute or relative. If relative they will be made
/// absolute against either current working directory or the directory where
/// the Overlay YAML file is located, depending on the 'root-relative'
/// configuration.
///
/// All configuration options are optional.
///   'case-sensitive': <boolean, default=(true for Posix, false for Windows)>
///   'use-external-names': <boolean, default=true>
///   'root-relative': <string, one of 'cwd' or 'overlay-dir', default='cwd'>
///   'overlay-relative': <boolean, default=false>
///   'fallthrough': <boolean, default=true, deprecated - use 'redirecting-with'
///                   instead>
///   'redirecting-with': <string, one of 'fallthrough', 'fallback', or
///                        'redirect-only', default='fallthrough'>
///
/// To clarify, 'root-relative' option will prepend the current working
/// directory, or the overlay directory to the 'roots->name' field only if
/// 'roots->name' is a relative path. On the other hand, when 'overlay-relative'
/// is set to 'true', external paths will always be prepended with the overlay
/// directory, even if external paths are not relative paths. The
/// 'root-relative' option has no interaction with the 'overlay-relative'
/// option.
///
/// Virtual directories that list their contents are represented as
/// \verbatim
/// {
///   'type': 'directory',
///   'name': <string>,
///   'contents': [ <file or directory entries> ]
/// }
/// \endverbatim
/// The default attributes for such virtual directories are:
/// \verbatim
/// MTime = now() when created
/// Perms = 0777
/// User = Group = 0
/// Size = 0
/// UniqueID = unspecified unique value
/// \endverbatim
/// When a path prefix matches such a directory, the next component in the path
/// is matched against the entries in the 'contents' array.
///
/// Re-mapped directories, on the other hand, are represented as
/// /// \verbatim
/// {
///   'type': 'directory-remap',
///   'name': <string>,
///   'use-external-name': <boolean>, # Optional
///   'external-contents': <path to external directory>
/// }
/// \endverbatim
/// and inherit their attributes from the external directory. When a path
/// prefix matches such an entry, the unmatched components are appended to the
/// 'external-contents' path, and the resulting path is looked up in the
/// external file system instead.
///
/// Re-mapped files are represented as
/// \verbatim
/// {
///   'type': 'file',
///   'name': <string>,
///   'use-external-name': <boolean>, # Optional
///   'external-contents': <path to external file>
/// }
/// \endverbatim
/// Their attributes and file contents are determined by looking up the file at
/// their 'external-contents' path in the external file system.
///
/// For 'file', 'directory' and 'directory-remap' entries the 'name' field may
/// contain multiple path components (e.g. /path/to/file). However, any
/// directory in such a path that contains more than one child must be uniquely
/// represented by a 'directory' entry.
///
/// When the 'use-external-name' field is set, calls to \a vfs::File::status()
/// give the external (remapped) filesystem name instead of the name the file
/// was accessed by. This is an intentional leak through the \a
/// RedirectingFileSystem abstraction layer. It enables clients to discover
/// (and use) the external file location when communicating with users or tools
/// that don't use the same VFS overlay.
///
/// FIXME: 'use-external-name' causes behaviour that's inconsistent with how
/// "real" filesystems behave. Maybe there should be a separate channel for
/// this information.
class RedirectingFileSystem
    : public RTTIExtends<RedirectingFileSystem, vfs::FileSystem> {
public:
  static const char ID;
  enum EntryKind { EK_Directory, EK_DirectoryRemap, EK_File };
  enum NameKind { NK_NotSet, NK_External, NK_Virtual };

  /// The type of redirection to perform.
  enum class RedirectKind {
    /// Lookup the redirected path first (ie. the one specified in
    /// 'external-contents') and if that fails "fallthrough" to a lookup of the
    /// originally provided path.
    Fallthrough,
    /// Lookup the provided path first and if that fails, "fallback" to a
    /// lookup of the redirected path.
    Fallback,
    /// Only lookup the redirected path, do not lookup the originally provided
    /// path.
    RedirectOnly
  };

  /// The type of relative path used by Roots.
  enum class RootRelativeKind {
    /// The roots are relative to the current working directory.
    CWD,
    /// The roots are relative to the directory where the Overlay YAML file
    // locates.
    OverlayDir
  };

  /// A single file or directory in the VFS.
  class Entry {
    EntryKind Kind;
    std::string Name;

  public:
    Entry(EntryKind K, StringRef Name) : Kind(K), Name(Name) {}
    virtual ~Entry() = default;

    StringRef getName() const { return Name; }
    EntryKind getKind() const { return Kind; }
  };

  /// A directory in the vfs with explicitly specified contents.
  class DirectoryEntry : public Entry {
    std::vector<std::unique_ptr<Entry>> Contents;
    Status S;

  public:
    /// Constructs a directory entry with explicitly specified contents.
    DirectoryEntry(StringRef Name, std::vector<std::unique_ptr<Entry>> Contents,
                   Status S)
        : Entry(EK_Directory, Name), Contents(std::move(Contents)),
          S(std::move(S)) {}

    /// Constructs an empty directory entry.
    DirectoryEntry(StringRef Name, Status S)
        : Entry(EK_Directory, Name), S(std::move(S)) {}

    Status getStatus() { return S; }

    void addContent(std::unique_ptr<Entry> Content) {
      Contents.push_back(std::move(Content));
    }

    Entry *getLastContent() const { return Contents.back().get(); }

    using iterator = decltype(Contents)::iterator;

    iterator contents_begin() { return Contents.begin(); }
    iterator contents_end() { return Contents.end(); }

    static bool classof(const Entry *E) { return E->getKind() == EK_Directory; }
  };

  /// A file or directory in the vfs that is mapped to a file or directory in
  /// the external filesystem.
  class RemapEntry : public Entry {
    std::string ExternalContentsPath;
    NameKind UseName;

  protected:
    RemapEntry(EntryKind K, StringRef Name, StringRef ExternalContentsPath,
               NameKind UseName)
        : Entry(K, Name), ExternalContentsPath(ExternalContentsPath),
          UseName(UseName) {}

  public:
    StringRef getExternalContentsPath() const { return ExternalContentsPath; }

    /// Whether to use the external path as the name for this file or directory.
    bool useExternalName(bool GlobalUseExternalName) const {
      return UseName == NK_NotSet ? GlobalUseExternalName
                                  : (UseName == NK_External);
    }

    NameKind getUseName() const { return UseName; }

    static bool classof(const Entry *E) {
      switch (E->getKind()) {
      case EK_DirectoryRemap:
        [[fallthrough]];
      case EK_File:
        return true;
      case EK_Directory:
        return false;
      }
      llvm_unreachable("invalid entry kind");
    }
  };

  /// A directory in the vfs that maps to a directory in the external file
  /// system.
  class DirectoryRemapEntry : public RemapEntry {
  public:
    DirectoryRemapEntry(StringRef Name, StringRef ExternalContentsPath,
                        NameKind UseName)
        : RemapEntry(EK_DirectoryRemap, Name, ExternalContentsPath, UseName) {}

    static bool classof(const Entry *E) {
      return E->getKind() == EK_DirectoryRemap;
    }
  };

  /// A file in the vfs that maps to a file in the external file system.
  class FileEntry : public RemapEntry {
  public:
    FileEntry(StringRef Name, StringRef ExternalContentsPath, NameKind UseName)
        : RemapEntry(EK_File, Name, ExternalContentsPath, UseName) {}

    static bool classof(const Entry *E) { return E->getKind() == EK_File; }
  };

  /// Represents the result of a path lookup into the RedirectingFileSystem.
  struct LookupResult {
    /// Chain of parent directory entries for \c E.
    llvm::SmallVector<Entry *, 32> Parents;

    /// The entry the looked-up path corresponds to.
    Entry *E;

  private:
    /// When the found Entry is a DirectoryRemapEntry, stores the path in the
    /// external file system that the looked-up path in the virtual file system
    //  corresponds to.
    std::optional<std::string> ExternalRedirect;

  public:
    LookupResult(Entry *E, sys::path::const_iterator Start,
                 sys::path::const_iterator End);

    /// If the found Entry maps the input path to a path in the external
    /// file system (i.e. it is a FileEntry or DirectoryRemapEntry), returns
    /// that path.
    std::optional<StringRef> getExternalRedirect() const {
      if (isa<DirectoryRemapEntry>(E))
        return StringRef(*ExternalRedirect);
      if (auto *FE = dyn_cast<FileEntry>(E))
        return FE->getExternalContentsPath();
      return std::nullopt;
    }

    /// Get the (canonical) path of the found entry. This uses the as-written
    /// path components from the VFS specification.
    void getPath(llvm::SmallVectorImpl<char> &Path) const;
  };

private:
  friend class RedirectingFSDirIterImpl;
  friend class RedirectingFileSystemParser;

  /// Canonicalize path by removing ".", "..", "./", components. This is
  /// a VFS request, do not bother about symlinks in the path components
  /// but canonicalize in order to perform the correct entry search.
  std::error_code makeCanonicalForLookup(SmallVectorImpl<char> &Path) const;

  /// Get the File status, or error, from the underlying external file system.
  /// This returns the status with the originally requested name, while looking
  /// up the entry using a potentially different path.
  ErrorOr<Status> getExternalStatus(const Twine &LookupPath,
                                    const Twine &OriginalPath) const;

  /// Make \a Path an absolute path.
  ///
  /// Makes \a Path absolute using the \a WorkingDir if it is not already.
  ///
  /// /absolute/path   => /absolute/path
  /// relative/../path => <WorkingDir>/relative/../path
  ///
  /// \param WorkingDir  A path that will be used as the base Dir if \a Path
  ///                    is not already absolute.
  /// \param Path A path that is modified to be an absolute path.
  /// \returns success if \a path has been made absolute, otherwise a
  ///          platform-specific error_code.
  std::error_code makeAbsolute(StringRef WorkingDir,
                               SmallVectorImpl<char> &Path) const;

  // In a RedirectingFileSystem, keys can be specified in Posix or Windows
  // style (or even a mixture of both), so this comparison helper allows
  // slashes (representing a root) to match backslashes (and vice versa).  Note
  // that, other than the root, path components should not contain slashes or
  // backslashes.
  bool pathComponentMatches(llvm::StringRef lhs, llvm::StringRef rhs) const {
    if ((CaseSensitive ? lhs == rhs : lhs.equals_insensitive(rhs)))
      return true;
    return (lhs == "/" && rhs == "\\") || (lhs == "\\" && rhs == "/");
  }

  /// The root(s) of the virtual file system.
  std::vector<std::unique_ptr<Entry>> Roots;

  /// The current working directory of the file system.
  std::string WorkingDirectory;

  /// The file system to use for external references.
  IntrusiveRefCntPtr<FileSystem> ExternalFS;

  /// This represents the directory path that the YAML file is located.
  /// This will be prefixed to each 'external-contents' if IsRelativeOverlay
  /// is set. This will also be prefixed to each 'roots->name' if RootRelative
  /// is set to RootRelativeKind::OverlayDir and the path is relative.
  std::string OverlayFileDir;

  /// @name Configuration
  /// @{

  /// Whether to perform case-sensitive comparisons.
  ///
  /// Currently, case-insensitive matching only works correctly with ASCII.
  bool CaseSensitive = is_style_posix(sys::path::Style::native);

  /// IsRelativeOverlay marks whether a OverlayFileDir path must
  /// be prefixed in every 'external-contents' when reading from YAML files.
  bool IsRelativeOverlay = false;

  /// Whether to use to use the value of 'external-contents' for the
  /// names of files.  This global value is overridable on a per-file basis.
  bool UseExternalNames = true;

  /// True if this FS has redirected a lookup. This does not include
  /// fallthrough.
  mutable bool HasBeenUsed = false;

  /// Used to enable or disable updating `HasBeenUsed`.
  bool UsageTrackingActive = false;

  /// Determines the lookups to perform, as well as their order. See
  /// \c RedirectKind for details.
  RedirectKind Redirection = RedirectKind::Fallthrough;

  /// Determine the prefix directory if the roots are relative paths. See
  /// \c RootRelativeKind for details.
  RootRelativeKind RootRelative = RootRelativeKind::CWD;
  /// @}

  RedirectingFileSystem(IntrusiveRefCntPtr<FileSystem> ExternalFS);

  /// Looks up the path <tt>[Start, End)</tt> in \p From, possibly recursing
  /// into the contents of \p From if it is a directory. Returns a LookupResult
  /// giving the matched entry and, if that entry is a FileEntry or
  /// DirectoryRemapEntry, the path it redirects to in the external file system.
  ErrorOr<LookupResult>
  lookupPathImpl(llvm::sys::path::const_iterator Start,
                 llvm::sys::path::const_iterator End, Entry *From,
                 llvm::SmallVectorImpl<Entry *> &Entries) const;

  /// Get the status for a path with the provided \c LookupResult.
  ErrorOr<Status> status(const Twine &LookupPath, const Twine &OriginalPath,
                         const LookupResult &Result);

public:
  /// Looks up \p Path in \c Roots and returns a LookupResult giving the
  /// matched entry and, if the entry was a FileEntry or DirectoryRemapEntry,
  /// the path it redirects to in the external file system.
  ErrorOr<LookupResult> lookupPath(StringRef Path) const;

  /// Parses \p Buffer, which is expected to be in YAML format and
  /// returns a virtual file system representing its contents.
  static std::unique_ptr<RedirectingFileSystem>
  create(std::unique_ptr<MemoryBuffer> Buffer,
         SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
         void *DiagContext, IntrusiveRefCntPtr<FileSystem> ExternalFS);

  /// Redirect each of the remapped files from first to second.
  static std::unique_ptr<RedirectingFileSystem>
  create(ArrayRef<std::pair<std::string, std::string>> RemappedFiles,
         bool UseExternalNames, FileSystem &ExternalFS);

  ErrorOr<Status> status(const Twine &Path) override;
  bool exists(const Twine &Path) override;
  ErrorOr<std::unique_ptr<File>> openFileForRead(const Twine &Path) override;

  std::error_code getRealPath(const Twine &Path,
                              SmallVectorImpl<char> &Output) override;

  llvm::ErrorOr<std::string> getCurrentWorkingDirectory() const override;

  std::error_code setCurrentWorkingDirectory(const Twine &Path) override;

  std::error_code isLocal(const Twine &Path, bool &Result) override;

  std::error_code makeAbsolute(SmallVectorImpl<char> &Path) const override;

  directory_iterator dir_begin(const Twine &Dir, std::error_code &EC) override;

  void setOverlayFileDir(StringRef PrefixDir);

  StringRef getOverlayFileDir() const;

  /// Sets the redirection kind to \c Fallthrough if true or \c RedirectOnly
  /// otherwise. Will removed in the future, use \c setRedirection instead.
  void setFallthrough(bool Fallthrough);

  void setRedirection(RedirectingFileSystem::RedirectKind Kind);

  std::vector<llvm::StringRef> getRoots() const;

  bool hasBeenUsed() const { return HasBeenUsed; };
  void clearHasBeenUsed() { HasBeenUsed = false; }

  void setUsageTrackingActive(bool Active) { UsageTrackingActive = Active; }

  void printEntry(raw_ostream &OS, Entry *E, unsigned IndentLevel = 0) const;

protected:
  void printImpl(raw_ostream &OS, PrintType Type,
                 unsigned IndentLevel) const override;
  void visitChildFileSystems(VisitCallbackTy Callback) override;
};

/// Collect all pairs of <virtual path, real path> entries from the
/// \p YAMLFilePath. This is used by the module dependency collector to forward
/// the entries into the reproducer output VFS YAML file.
void collectVFSFromYAML(
    std::unique_ptr<llvm::MemoryBuffer> Buffer,
    llvm::SourceMgr::DiagHandlerTy DiagHandler, StringRef YAMLFilePath,
    SmallVectorImpl<YAMLVFSEntry> &CollectedEntries,
    void *DiagContext = nullptr,
    IntrusiveRefCntPtr<FileSystem> ExternalFS = getRealFileSystem());

class YAMLVFSWriter {
  std::vector<YAMLVFSEntry> Mappings;
  std::optional<bool> IsCaseSensitive;
  std::optional<bool> IsOverlayRelative;
  std::optional<bool> UseExternalNames;
  std::string OverlayDir;

  void addEntry(StringRef VirtualPath, StringRef RealPath, bool IsDirectory);

public:
  YAMLVFSWriter() = default;

  void addFileMapping(StringRef VirtualPath, StringRef RealPath);
  void addDirectoryMapping(StringRef VirtualPath, StringRef RealPath);

  void setCaseSensitivity(bool CaseSensitive) {
    IsCaseSensitive = CaseSensitive;
  }

  void setUseExternalNames(bool UseExtNames) { UseExternalNames = UseExtNames; }

  void setOverlayDir(StringRef OverlayDirectory) {
    IsOverlayRelative = true;
    OverlayDir.assign(OverlayDirectory.str());
  }

  const std::vector<YAMLVFSEntry> &getMappings() const { return Mappings; }

  void write(llvm::raw_ostream &OS);
};

} // namespace vfs
} // namespace llvm

#endif // LLVM_SUPPORT_VIRTUALFILESYSTEM_H
