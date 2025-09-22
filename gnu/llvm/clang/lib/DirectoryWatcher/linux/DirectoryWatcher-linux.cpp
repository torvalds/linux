//===- DirectoryWatcher-linux.cpp - Linux-platform directory watching -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectoryScanner.h"
#include "clang/DirectoryWatcher/DirectoryWatcher.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <limits.h>
#include <optional>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace {

using namespace llvm;
using namespace clang;

/// Pipe for inter-thread synchronization - for epoll-ing on multiple
/// conditions. It is meant for uni-directional 1:1 signalling - specifically:
/// no multiple consumers, no data passing. Thread waiting for signal should
/// poll the FDRead. Signalling thread should call signal() which writes single
/// character to FDRead.
struct SemaphorePipe {
  // Expects two file-descriptors opened as a pipe in the canonical POSIX
  // order: pipefd[0] refers to the read end of the pipe. pipefd[1] refers to
  // the write end of the pipe.
  SemaphorePipe(int pipefd[2])
      : FDRead(pipefd[0]), FDWrite(pipefd[1]), OwnsFDs(true) {}
  SemaphorePipe(const SemaphorePipe &) = delete;
  void operator=(const SemaphorePipe &) = delete;
  SemaphorePipe(SemaphorePipe &&other)
      : FDRead(other.FDRead), FDWrite(other.FDWrite),
        OwnsFDs(other.OwnsFDs) // Someone could have moved from the other
                               // instance before.
  {
    other.OwnsFDs = false;
  };

  void signal() {
#ifndef NDEBUG
    ssize_t Result =
#endif
    llvm::sys::RetryAfterSignal(-1, write, FDWrite, "A", 1);
    assert(Result != -1);
  }
  ~SemaphorePipe() {
    if (OwnsFDs) {
      close(FDWrite);
      close(FDRead);
    }
  }
  const int FDRead;
  const int FDWrite;
  bool OwnsFDs;

  static std::optional<SemaphorePipe> create() {
    int InotifyPollingStopperFDs[2];
    if (pipe2(InotifyPollingStopperFDs, O_CLOEXEC) == -1)
      return std::nullopt;
    return SemaphorePipe(InotifyPollingStopperFDs);
  }
};

/// Mutex-protected queue of Events.
class EventQueue {
  std::mutex Mtx;
  std::condition_variable NonEmpty;
  std::queue<DirectoryWatcher::Event> Events;

public:
  void push_back(const DirectoryWatcher::Event::EventKind K,
                 StringRef Filename) {
    {
      std::unique_lock<std::mutex> L(Mtx);
      Events.emplace(K, Filename);
    }
    NonEmpty.notify_one();
  }

  // Blocks on caller thread and uses codition_variable to wait until there's an
  // event to return.
  DirectoryWatcher::Event pop_front_blocking() {
    std::unique_lock<std::mutex> L(Mtx);
    while (true) {
      // Since we might have missed all the prior notifications on NonEmpty we
      // have to check the queue first (under lock).
      if (!Events.empty()) {
        DirectoryWatcher::Event Front = Events.front();
        Events.pop();
        return Front;
      }
      NonEmpty.wait(L, [this]() { return !Events.empty(); });
    }
  }
};

class DirectoryWatcherLinux : public clang::DirectoryWatcher {
public:
  DirectoryWatcherLinux(
      llvm::StringRef WatchedDirPath,
      std::function<void(llvm::ArrayRef<Event>, bool)> Receiver,
      bool WaitForInitialSync, int InotifyFD, int InotifyWD,
      SemaphorePipe &&InotifyPollingStopSignal);

  ~DirectoryWatcherLinux() override {
    StopWork();
    InotifyPollingThread.join();
    EventsReceivingThread.join();
    inotify_rm_watch(InotifyFD, InotifyWD);
    llvm::sys::RetryAfterSignal(-1, close, InotifyFD);
  }

private:
  const std::string WatchedDirPath;
  // inotify file descriptor
  int InotifyFD = -1;
  // inotify watch descriptor
  int InotifyWD = -1;

  EventQueue Queue;

  // Make sure lifetime of Receiver fully contains lifetime of
  // EventsReceivingThread.
  std::function<void(llvm::ArrayRef<Event>, bool)> Receiver;

  // Consumes inotify events and pushes directory watcher events to the Queue.
  void InotifyPollingLoop();
  std::thread InotifyPollingThread;
  // Using pipe so we can epoll two file descriptors at once - inotify and
  // stopping condition.
  SemaphorePipe InotifyPollingStopSignal;

  // Does the initial scan of the directory - directly calling Receiver,
  // bypassing the Queue. Both InitialScan and EventReceivingLoop use Receiver
  // which isn't necessarily thread-safe.
  void InitialScan();

  // Processing events from the Queue.
  // In case client doesn't want to do the initial scan synchronously
  // (WaitForInitialSync=false in ctor) we do the initial scan at the beginning
  // of this thread.
  std::thread EventsReceivingThread;
  // Push event of WatcherGotInvalidated kind to the Queue to stop the loop.
  // Both InitialScan and EventReceivingLoop use Receiver which isn't
  // necessarily thread-safe.
  void EventReceivingLoop();

  // Stops all the async work. Reentrant.
  void StopWork() {
    Queue.push_back(DirectoryWatcher::Event::EventKind::WatcherGotInvalidated,
                    "");
    InotifyPollingStopSignal.signal();
  }
};

void DirectoryWatcherLinux::InotifyPollingLoop() {
  // We want to be able to read ~30 events at once even in the worst case
  // (obscenely long filenames).
  constexpr size_t EventBufferLength =
      30 * (sizeof(struct inotify_event) + NAME_MAX + 1);
  // http://man7.org/linux/man-pages/man7/inotify.7.html
  // Some systems cannot read integer variables if they are not
  // properly aligned. On other systems, incorrect alignment may
  // decrease performance. Hence, the buffer used for reading from
  // the inotify file descriptor should have the same alignment as
  // struct inotify_event.

  struct Buffer {
    alignas(struct inotify_event) char buffer[EventBufferLength];
  };
  auto ManagedBuffer = std::make_unique<Buffer>();
  char *const Buf = ManagedBuffer->buffer;

  const int EpollFD = epoll_create1(EPOLL_CLOEXEC);
  if (EpollFD == -1) {
    StopWork();
    return;
  }
  auto EpollFDGuard = llvm::make_scope_exit([EpollFD]() { close(EpollFD); });

  struct epoll_event EventSpec;
  EventSpec.events = EPOLLIN;
  EventSpec.data.fd = InotifyFD;
  if (epoll_ctl(EpollFD, EPOLL_CTL_ADD, InotifyFD, &EventSpec) == -1) {
    StopWork();
    return;
  }

  EventSpec.data.fd = InotifyPollingStopSignal.FDRead;
  if (epoll_ctl(EpollFD, EPOLL_CTL_ADD, InotifyPollingStopSignal.FDRead,
                &EventSpec) == -1) {
    StopWork();
    return;
  }

  std::array<struct epoll_event, 2> EpollEventBuffer;

  while (true) {
    const int EpollWaitResult = llvm::sys::RetryAfterSignal(
        -1, epoll_wait, EpollFD, EpollEventBuffer.data(),
        EpollEventBuffer.size(), /*timeout=*/-1 /*== infinity*/);
    if (EpollWaitResult == -1) {
      StopWork();
      return;
    }

    // Multiple epoll_events can be received for a single file descriptor per
    // epoll_wait call.
    for (int i = 0; i < EpollWaitResult; ++i) {
      if (EpollEventBuffer[i].data.fd == InotifyPollingStopSignal.FDRead) {
        StopWork();
        return;
      }
    }

    // epoll_wait() always return either error or >0 events. Since there was no
    // event for stopping, it must be an inotify event ready for reading.
    ssize_t NumRead = llvm::sys::RetryAfterSignal(-1, read, InotifyFD, Buf,
                                                  EventBufferLength);
    for (char *P = Buf; P < Buf + NumRead;) {
      if (P + sizeof(struct inotify_event) > Buf + NumRead) {
        StopWork();
        llvm_unreachable("an incomplete inotify_event was read");
        return;
      }

      struct inotify_event *Event = reinterpret_cast<struct inotify_event *>(P);
      P += sizeof(struct inotify_event) + Event->len;

      if (Event->mask & (IN_CREATE | IN_MODIFY | IN_MOVED_TO | IN_DELETE) &&
          Event->len <= 0) {
        StopWork();
        llvm_unreachable("expected a filename from inotify");
        return;
      }

      if (Event->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY)) {
        Queue.push_back(DirectoryWatcher::Event::EventKind::Modified,
                        Event->name);
      } else if (Event->mask & (IN_DELETE | IN_MOVED_FROM)) {
        Queue.push_back(DirectoryWatcher::Event::EventKind::Removed,
                        Event->name);
      } else if (Event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
        Queue.push_back(DirectoryWatcher::Event::EventKind::WatchedDirRemoved,
                        "");
        StopWork();
        return;
      } else if (Event->mask & IN_IGNORED) {
        StopWork();
        return;
      } else {
        StopWork();
        llvm_unreachable("Unknown event type.");
        return;
      }
    }
  }
}

void DirectoryWatcherLinux::InitialScan() {
  this->Receiver(getAsFileEvents(scanDirectory(WatchedDirPath)),
                 /*IsInitial=*/true);
}

void DirectoryWatcherLinux::EventReceivingLoop() {
  while (true) {
    DirectoryWatcher::Event Event = this->Queue.pop_front_blocking();
    this->Receiver(Event, false);
    if (Event.Kind ==
        DirectoryWatcher::Event::EventKind::WatcherGotInvalidated) {
      StopWork();
      return;
    }
  }
}

DirectoryWatcherLinux::DirectoryWatcherLinux(
    StringRef WatchedDirPath,
    std::function<void(llvm::ArrayRef<Event>, bool)> Receiver,
    bool WaitForInitialSync, int InotifyFD, int InotifyWD,
    SemaphorePipe &&InotifyPollingStopSignal)
    : WatchedDirPath(WatchedDirPath), InotifyFD(InotifyFD),
      InotifyWD(InotifyWD), Receiver(Receiver),
      InotifyPollingStopSignal(std::move(InotifyPollingStopSignal)) {

  InotifyPollingThread = std::thread([this]() { InotifyPollingLoop(); });
  // We have no guarantees about thread safety of the Receiver which is being
  // used in both InitialScan and EventReceivingLoop. We shouldn't run these
  // only synchronously.
  if (WaitForInitialSync) {
    InitialScan();
    EventsReceivingThread = std::thread([this]() { EventReceivingLoop(); });
  } else {
    EventsReceivingThread = std::thread([this]() {
      // FIXME: We might want to terminate an async initial scan early in case
      // of a failure in EventsReceivingThread.
      InitialScan();
      EventReceivingLoop();
    });
  }
}

} // namespace

llvm::Expected<std::unique_ptr<DirectoryWatcher>> clang::DirectoryWatcher::create(
    StringRef Path,
    std::function<void(llvm::ArrayRef<DirectoryWatcher::Event>, bool)> Receiver,
    bool WaitForInitialSync) {
  if (Path.empty())
    llvm::report_fatal_error(
        "DirectoryWatcher::create can not accept an empty Path.");

  const int InotifyFD = inotify_init1(IN_CLOEXEC);
  if (InotifyFD == -1)
    return llvm::make_error<llvm::StringError>(
        llvm::errnoAsErrorCode(), std::string(": inotify_init1()"));

  const int InotifyWD = inotify_add_watch(
      InotifyFD, Path.str().c_str(),
      IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY |
      IN_MOVED_FROM | IN_MOVE_SELF | IN_MOVED_TO | IN_ONLYDIR | IN_IGNORED
#ifdef IN_EXCL_UNLINK
      | IN_EXCL_UNLINK
#endif
      );
  if (InotifyWD == -1)
    return llvm::make_error<llvm::StringError>(
        llvm::errnoAsErrorCode(), std::string(": inotify_add_watch()"));

  auto InotifyPollingStopper = SemaphorePipe::create();

  if (!InotifyPollingStopper)
    return llvm::make_error<llvm::StringError>(
        llvm::errnoAsErrorCode(), std::string(": SemaphorePipe::create()"));

  return std::make_unique<DirectoryWatcherLinux>(
      Path, Receiver, WaitForInitialSync, InotifyFD, InotifyWD,
      std::move(*InotifyPollingStopper));
}
