//===- DirectoryWatcher-windows.cpp - Windows-platform directory watching -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectoryScanner.h"
#include "clang/DirectoryWatcher/DirectoryWatcher.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace {

using DirectoryWatcherCallback =
    std::function<void(llvm::ArrayRef<clang::DirectoryWatcher::Event>, bool)>;

using namespace llvm;
using namespace clang;

class DirectoryWatcherWindows : public clang::DirectoryWatcher {
  OVERLAPPED Overlapped;

  std::vector<DWORD> Notifications;

  std::thread WatcherThread;
  std::thread HandlerThread;
  std::function<void(ArrayRef<DirectoryWatcher::Event>, bool)> Callback;
  SmallString<MAX_PATH> Path;
  HANDLE Terminate;

  std::mutex Mutex;
  bool WatcherActive = false;
  std::condition_variable Ready;

  class EventQueue {
    std::mutex M;
    std::queue<DirectoryWatcher::Event> Q;
    std::condition_variable CV;

  public:
    void emplace(DirectoryWatcher::Event::EventKind Kind, StringRef Path) {
      {
        std::unique_lock<std::mutex> L(M);
        Q.emplace(Kind, Path);
      }
      CV.notify_one();
    }

    DirectoryWatcher::Event pop_front() {
      std::unique_lock<std::mutex> L(M);
      while (true) {
        if (!Q.empty()) {
          DirectoryWatcher::Event E = Q.front();
          Q.pop();
          return E;
        }
        CV.wait(L, [this]() { return !Q.empty(); });
      }
    }
  } Q;

public:
  DirectoryWatcherWindows(HANDLE DirectoryHandle, bool WaitForInitialSync,
                          DirectoryWatcherCallback Receiver);

  ~DirectoryWatcherWindows() override;

  void InitialScan();
  void WatcherThreadProc(HANDLE DirectoryHandle);
  void NotifierThreadProc(bool WaitForInitialSync);
};

DirectoryWatcherWindows::DirectoryWatcherWindows(
    HANDLE DirectoryHandle, bool WaitForInitialSync,
    DirectoryWatcherCallback Receiver)
    : Callback(Receiver), Terminate(INVALID_HANDLE_VALUE) {
  // Pre-compute the real location as we will be handing over the directory
  // handle to the watcher and performing synchronous operations.
  {
    DWORD Size = GetFinalPathNameByHandleW(DirectoryHandle, NULL, 0, 0);
    std::unique_ptr<WCHAR[]> Buffer{new WCHAR[Size + 1]};
    Size = GetFinalPathNameByHandleW(DirectoryHandle, Buffer.get(), Size, 0);
    Buffer[Size] = L'\0';
    WCHAR *Data = Buffer.get();
    if (Size >= 4 && ::memcmp(Data, L"\\\\?\\", 8) == 0) {
      Data += 4;
      Size -= 4;
    }
    llvm::sys::windows::UTF16ToUTF8(Data, Size, Path);
  }

  size_t EntrySize = sizeof(FILE_NOTIFY_INFORMATION) + MAX_PATH * sizeof(WCHAR);
  Notifications.resize((4 * EntrySize) / sizeof(DWORD));

  memset(&Overlapped, 0, sizeof(Overlapped));
  Overlapped.hEvent =
      CreateEventW(NULL, /*bManualReset=*/FALSE, /*bInitialState=*/FALSE, NULL);
  assert(Overlapped.hEvent && "unable to create event");

  Terminate =
      CreateEventW(NULL, /*bManualReset=*/TRUE, /*bInitialState=*/FALSE, NULL);

  WatcherThread = std::thread([this, DirectoryHandle]() {
    this->WatcherThreadProc(DirectoryHandle);
  });

  if (WaitForInitialSync)
    InitialScan();

  HandlerThread = std::thread([this, WaitForInitialSync]() {
    this->NotifierThreadProc(WaitForInitialSync);
  });
}

DirectoryWatcherWindows::~DirectoryWatcherWindows() {
  // Signal the Watcher to exit.
  SetEvent(Terminate);
  HandlerThread.join();
  WatcherThread.join();
  CloseHandle(Terminate);
  CloseHandle(Overlapped.hEvent);
}

void DirectoryWatcherWindows::InitialScan() {
  std::unique_lock<std::mutex> lock(Mutex);
  Ready.wait(lock, [this] { return this->WatcherActive; });

  Callback(getAsFileEvents(scanDirectory(Path.data())), /*IsInitial=*/true);
}

void DirectoryWatcherWindows::WatcherThreadProc(HANDLE DirectoryHandle) {
  while (true) {
    // We do not guarantee subdirectories, but macOS already provides
    // subdirectories, might as well as ...
    BOOL WatchSubtree = TRUE;
    DWORD NotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME
                       | FILE_NOTIFY_CHANGE_DIR_NAME
                       | FILE_NOTIFY_CHANGE_SIZE
                       | FILE_NOTIFY_CHANGE_LAST_WRITE
                       | FILE_NOTIFY_CHANGE_CREATION;

    DWORD BytesTransferred;
    if (!ReadDirectoryChangesW(DirectoryHandle, Notifications.data(),
                               Notifications.size() * sizeof(DWORD),
                               WatchSubtree, NotifyFilter, &BytesTransferred,
                               &Overlapped, NULL)) {
      Q.emplace(DirectoryWatcher::Event::EventKind::WatcherGotInvalidated,
                "");
      break;
    }

    if (!WatcherActive) {
      std::unique_lock<std::mutex> lock(Mutex);
      WatcherActive = true;
    }
    Ready.notify_one();

    HANDLE Handles[2] = { Terminate, Overlapped.hEvent };
    switch (WaitForMultipleObjects(2, Handles, FALSE, INFINITE)) {
    case WAIT_OBJECT_0: // Terminate Request
    case WAIT_FAILED:   // Failure
      Q.emplace(DirectoryWatcher::Event::EventKind::WatcherGotInvalidated,
                "");
      (void)CloseHandle(DirectoryHandle);
      return;
    case WAIT_TIMEOUT:  // Spurious wakeup?
      continue;
    case WAIT_OBJECT_0 + 1: // Directory change
      break;
    }

    if (!GetOverlappedResult(DirectoryHandle, &Overlapped, &BytesTransferred,
                             FALSE)) {
      Q.emplace(DirectoryWatcher::Event::EventKind::WatchedDirRemoved,
                "");
      Q.emplace(DirectoryWatcher::Event::EventKind::WatcherGotInvalidated,
                "");
      break;
    }

    // There was a buffer underrun on the kernel side.  We may have lost
    // events, please re-synchronize.
    if (BytesTransferred == 0) {
      Q.emplace(DirectoryWatcher::Event::EventKind::WatcherGotInvalidated,
                "");
      break;
    }

    for (FILE_NOTIFY_INFORMATION *I =
            (FILE_NOTIFY_INFORMATION *)Notifications.data();
         I;
         I = I->NextEntryOffset
              ? (FILE_NOTIFY_INFORMATION *)((CHAR *)I + I->NextEntryOffset)
              : NULL) {
      DirectoryWatcher::Event::EventKind Kind =
          DirectoryWatcher::Event::EventKind::WatcherGotInvalidated;
      switch (I->Action) {
      case FILE_ACTION_ADDED:
      case FILE_ACTION_MODIFIED:
      case FILE_ACTION_RENAMED_NEW_NAME:
        Kind = DirectoryWatcher::Event::EventKind::Modified;
        break;
      case FILE_ACTION_REMOVED:
      case FILE_ACTION_RENAMED_OLD_NAME:
        Kind = DirectoryWatcher::Event::EventKind::Removed;
        break;
      }

      SmallString<MAX_PATH> filename;
      sys::windows::UTF16ToUTF8(I->FileName, I->FileNameLength / sizeof(WCHAR),
                                filename);
      Q.emplace(Kind, filename);
    }
  }

  (void)CloseHandle(DirectoryHandle);
}

void DirectoryWatcherWindows::NotifierThreadProc(bool WaitForInitialSync) {
  // If we did not wait for the initial sync, then we should perform the
  // scan when we enter the thread.
  if (!WaitForInitialSync)
    this->InitialScan();

  while (true) {
    DirectoryWatcher::Event E = Q.pop_front();
    Callback(E, /*IsInitial=*/false);
    if (E.Kind == DirectoryWatcher::Event::EventKind::WatcherGotInvalidated)
      break;
  }
}

auto error(DWORD ErrorCode) {
  DWORD Flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
              | FORMAT_MESSAGE_FROM_SYSTEM
              | FORMAT_MESSAGE_IGNORE_INSERTS;

  LPSTR Buffer;
  if (!FormatMessageA(Flags, NULL, ErrorCode,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&Buffer,
                      0, NULL)) {
    return make_error<llvm::StringError>("error " + utostr(ErrorCode),
                                         inconvertibleErrorCode());
  }
  std::string Message{Buffer};
  LocalFree(Buffer);
  return make_error<llvm::StringError>(Message, inconvertibleErrorCode());
}

} // namespace

llvm::Expected<std::unique_ptr<DirectoryWatcher>>
clang::DirectoryWatcher::create(StringRef Path,
                                DirectoryWatcherCallback Receiver,
                                bool WaitForInitialSync) {
  if (Path.empty())
    llvm::report_fatal_error(
        "DirectoryWatcher::create can not accept an empty Path.");

  if (!sys::fs::is_directory(Path))
    llvm::report_fatal_error(
        "DirectoryWatcher::create can not accept a filepath.");

  SmallVector<wchar_t, MAX_PATH> WidePath;
  if (sys::windows::UTF8ToUTF16(Path, WidePath))
    return llvm::make_error<llvm::StringError>(
        "unable to convert path to UTF-16", llvm::inconvertibleErrorCode());

  DWORD DesiredAccess = FILE_LIST_DIRECTORY;
  DWORD ShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  DWORD CreationDisposition = OPEN_EXISTING;
  DWORD FlagsAndAttributes = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED;

  HANDLE DirectoryHandle =
      CreateFileW(WidePath.data(), DesiredAccess, ShareMode,
                  /*lpSecurityAttributes=*/NULL, CreationDisposition,
                  FlagsAndAttributes, NULL);
  if (DirectoryHandle == INVALID_HANDLE_VALUE)
    return error(GetLastError());

  // NOTE: We use the watcher instance as a RAII object to discard the handles
  // for the directory in case of an error.  Hence, this is early allocated,
  // with the state being written directly to the watcher.
  return std::make_unique<DirectoryWatcherWindows>(
      DirectoryHandle, WaitForInitialSync, Receiver);
}
