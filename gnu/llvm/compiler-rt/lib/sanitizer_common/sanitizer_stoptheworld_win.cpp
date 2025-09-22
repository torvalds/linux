//===-- sanitizer_stoptheworld_win.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See sanitizer_stoptheworld.h for details.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_WINDOWS

#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
// windows.h needs to be included before tlhelp32.h
#  include <tlhelp32.h>

#  include "sanitizer_stoptheworld.h"

namespace __sanitizer {

namespace {

struct SuspendedThreadsListWindows final : public SuspendedThreadsList {
  InternalMmapVector<HANDLE> threadHandles;
  InternalMmapVector<DWORD> threadIds;

  SuspendedThreadsListWindows() {
    threadIds.reserve(1024);
    threadHandles.reserve(1024);
  }

  PtraceRegistersStatus GetRegistersAndSP(uptr index,
                                          InternalMmapVector<uptr> *buffer,
                                          uptr *sp) const override;

  tid_t GetThreadID(uptr index) const override;
  uptr ThreadCount() const override;
};

// Stack Pointer register names on different architectures
#  if SANITIZER_X64
#    define SP_REG Rsp
#  elif SANITIZER_I386
#    define SP_REG Esp
#  elif SANITIZER_ARM | SANITIZER_ARM64
#    define SP_REG Sp
#  else
#    error Architecture not supported!
#  endif

PtraceRegistersStatus SuspendedThreadsListWindows::GetRegistersAndSP(
    uptr index, InternalMmapVector<uptr> *buffer, uptr *sp) const {
  CHECK_LT(index, threadHandles.size());

  buffer->resize(RoundUpTo(sizeof(CONTEXT), sizeof(uptr)) / sizeof(uptr));
  CONTEXT *thread_context = reinterpret_cast<CONTEXT *>(buffer->data());
  thread_context->ContextFlags = CONTEXT_ALL;
  CHECK(GetThreadContext(threadHandles[index], thread_context));
  *sp = thread_context->SP_REG;

  return REGISTERS_AVAILABLE;
}

tid_t SuspendedThreadsListWindows::GetThreadID(uptr index) const {
  CHECK_LT(index, threadIds.size());
  return threadIds[index];
}

uptr SuspendedThreadsListWindows::ThreadCount() const {
  return threadIds.size();
}

struct RunThreadArgs {
  StopTheWorldCallback callback;
  void *argument;
};

DWORD WINAPI RunThread(void *argument) {
  RunThreadArgs *run_args = (RunThreadArgs *)argument;

  const DWORD this_thread = GetCurrentThreadId();
  const DWORD this_process = GetCurrentProcessId();

  SuspendedThreadsListWindows suspended_threads_list;
  bool new_thread_found;

  do {
    // Take a snapshot of all Threads
    const HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    CHECK(threads != INVALID_HANDLE_VALUE);

    THREADENTRY32 thread_entry;
    thread_entry.dwSize = sizeof(thread_entry);
    new_thread_found = false;

    if (!Thread32First(threads, &thread_entry))
      break;

    do {
      if (thread_entry.th32ThreadID == this_thread ||
          thread_entry.th32OwnerProcessID != this_process)
        continue;

      bool suspended_thread = false;
      for (const auto thread_id : suspended_threads_list.threadIds) {
        if (thread_id == thread_entry.th32ThreadID) {
          suspended_thread = true;
          break;
        }
      }

      // Skip the Thread if it was already suspended
      if (suspended_thread)
        continue;

      const HANDLE thread =
          OpenThread(THREAD_ALL_ACCESS, FALSE, thread_entry.th32ThreadID);
      CHECK(thread);

      if (SuspendThread(thread) == (DWORD)-1) {
        DWORD last_error = GetLastError();

        VPrintf(1, "Could not suspend thread %lu (error %lu)",
                thread_entry.th32ThreadID, last_error);
        continue;
      }

      suspended_threads_list.threadIds.push_back(thread_entry.th32ThreadID);
      suspended_threads_list.threadHandles.push_back(thread);
      new_thread_found = true;
    } while (Thread32Next(threads, &thread_entry));

    CloseHandle(threads);

    // Between the call to `CreateToolhelp32Snapshot` and suspending the
    // relevant Threads, new Threads could have potentially been created. So
    // continue to find and suspend new Threads until we don't find any.
  } while (new_thread_found);

  // Now all Threads of this Process except of this Thread should be suspended.
  // Execute the callback function.
  run_args->callback(suspended_threads_list, run_args->argument);

  // Resume all Threads
  for (const auto suspended_thread_handle :
       suspended_threads_list.threadHandles) {
    CHECK_NE(ResumeThread(suspended_thread_handle), -1);
    CloseHandle(suspended_thread_handle);
  }

  return 0;
}

}  // namespace

void StopTheWorld(StopTheWorldCallback callback, void *argument) {
  struct RunThreadArgs arg = {callback, argument};
  DWORD trace_thread_id;

  auto trace_thread =
      CreateThread(nullptr, 0, RunThread, &arg, 0, &trace_thread_id);
  CHECK(trace_thread);

  WaitForSingleObject(trace_thread, INFINITE);
  CloseHandle(trace_thread);
}

}  // namespace __sanitizer

#endif  // SANITIZER_WINDOWS
