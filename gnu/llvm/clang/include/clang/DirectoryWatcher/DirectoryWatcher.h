//===- DirectoryWatcher.h - Listens for directory file changes --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DIRECTORYWATCHER_DIRECTORYWATCHER_H
#define LLVM_CLANG_DIRECTORYWATCHER_DIRECTORYWATCHER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <memory>
#include <string>

namespace clang {
/// Provides notifications for file changes in a directory.
///
/// Invokes client-provided function on every filesystem event in the watched
/// directory. Initially the watched directory is scanned and for every file
/// found, an event is synthesized as if the file was added.
///
/// This is not a general purpose directory monitoring tool - list of
/// limitations follows.
///
/// Only flat directories with no subdirectories are supported. In case
/// subdirectories are present the behavior is unspecified - events *might* be
/// passed to Receiver on macOS (due to FSEvents being used) while they
/// *probably* won't be passed on Linux (due to inotify being used).
///
/// Known potential inconsistencies
/// - For files that are deleted befor the initial scan processed them, clients
/// might receive Removed notification without any prior Added notification.
/// - Multiple notifications might be produced when a file is added to the
/// watched directory during the initial scan. We are choosing the lesser evil
/// here as the only known alternative strategy would be to invalidate the
/// watcher instance and force user to create a new one whenever filesystem
/// event occurs during the initial scan but that would introduce continuous
/// restarting failure mode (watched directory is not always "owned" by the same
/// process that is consuming it). Since existing clients can handle duplicate
/// events well, we decided for simplicity.
///
/// Notifications are provided only for changes done through local user-space
/// filesystem interface. Specifically, it's unspecified if notification would
/// be provided in case of a:
/// - a file mmap-ed and changed
/// - a file changed via remote (NFS) or virtual (/proc) FS access to monitored
/// directory
/// - another filesystem mounted to the watched directory
///
/// No support for LLVM VFS.
///
/// It is unspecified whether notifications for files being deleted are sent in
/// case the whole watched directory is sent.
///
/// Directories containing "too many" files and/or receiving events "too
/// frequently" are not supported - if the initial scan can't be finished before
/// the watcher instance gets invalidated (see WatcherGotInvalidated) there's no
/// good error handling strategy - the only option for client is to destroy the
/// watcher, restart watching with new instance and hope it won't repeat.
class DirectoryWatcher {
public:
  struct Event {
    enum class EventKind {
      Removed,
      /// Content of a file was modified.
      Modified,
      /// The watched directory got deleted.
      WatchedDirRemoved,
      /// The DirectoryWatcher that originated this event is no longer valid and
      /// its behavior is unspecified.
      ///
      /// The prime case is kernel signalling to OS-specific implementation of
      /// DirectoryWatcher some resource limit being hit.
      /// *Usually* kernel starts dropping or squashing events together after
      /// that and so would DirectoryWatcher. This means that *some* events
      /// might still be passed to Receiver but this behavior is unspecified.
      ///
      /// Another case is after the watched directory itself is deleted.
      /// WatcherGotInvalidated will be received at least once during
      /// DirectoryWatcher instance lifetime - when handling errors this is done
      /// on best effort basis, when an instance is being destroyed then this is
      /// guaranteed.
      ///
      /// The only proper response to this kind of event is to destruct the
      /// originating DirectoryWatcher instance and create a new one.
      WatcherGotInvalidated
    };

    EventKind Kind;
    /// Filename that this event is related to or an empty string in
    /// case this event is related to the watched directory itself.
    std::string Filename;

    Event(EventKind Kind, llvm::StringRef Filename)
        : Kind(Kind), Filename(Filename) {}
  };

  /// llvm fatal_error if \param Path doesn't exist or isn't a directory.
  /// Returns llvm::Expected Error if OS kernel API told us we can't start
  /// watching. In such case it's unclear whether just retrying has any chance
  /// to succeed.
  static llvm::Expected<std::unique_ptr<DirectoryWatcher>>
  create(llvm::StringRef Path,
         std::function<void(llvm::ArrayRef<DirectoryWatcher::Event> Events,
                            bool IsInitial)>
             Receiver,
         bool WaitForInitialSync);

  virtual ~DirectoryWatcher() = default;
  DirectoryWatcher(const DirectoryWatcher &) = delete;
  DirectoryWatcher &operator=(const DirectoryWatcher &) = delete;
  DirectoryWatcher(DirectoryWatcher &&) = default;

protected:
  DirectoryWatcher() = default;
};

} // namespace clang

#endif // LLVM_CLANG_DIRECTORYWATCHER_DIRECTORYWATCHER_H
