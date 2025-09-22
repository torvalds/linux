//===-- ThreadLauncher.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// lldb Includes
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/HostNativeThread.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Log.h"

#if defined(_WIN32)
#include "lldb/Host/windows/windows.h"
#endif

#include "llvm/Support/WindowsError.h"

using namespace lldb;
using namespace lldb_private;

llvm::Expected<HostThread>
ThreadLauncher::LaunchThread(llvm::StringRef name,
                             std::function<thread_result_t()> impl,
                             size_t min_stack_byte_size) {
  // Host::ThreadCreateTrampoline will take ownership if thread creation is
  // successful.
  auto info_up = std::make_unique<HostThreadCreateInfo>(name.str(), impl);
  lldb::thread_t thread;
#ifdef _WIN32
  thread = (lldb::thread_t)::_beginthreadex(
      0, (unsigned)min_stack_byte_size,
      HostNativeThread::ThreadCreateTrampoline, info_up.get(), 0, NULL);
  if (thread == LLDB_INVALID_HOST_THREAD)
    return llvm::errorCodeToError(llvm::mapWindowsError(GetLastError()));
#else

// ASAN instrumentation adds a lot of bookkeeping overhead on stack frames.
#if __has_feature(address_sanitizer)
  const size_t eight_megabytes = 8 * 1024 * 1024;
  if (min_stack_byte_size < eight_megabytes) {
    min_stack_byte_size += eight_megabytes;
  }
#endif

  pthread_attr_t *thread_attr_ptr = nullptr;
  pthread_attr_t thread_attr;
  bool destroy_attr = false;
  if (min_stack_byte_size > 0) {
    if (::pthread_attr_init(&thread_attr) == 0) {
      destroy_attr = true;
      size_t default_min_stack_byte_size = 0;
      if (::pthread_attr_getstacksize(&thread_attr,
                                      &default_min_stack_byte_size) == 0) {
        if (default_min_stack_byte_size < min_stack_byte_size) {
          if (::pthread_attr_setstacksize(&thread_attr, min_stack_byte_size) ==
              0)
            thread_attr_ptr = &thread_attr;
        }
      }
    }
  }
  int err =
      ::pthread_create(&thread, thread_attr_ptr,
                       HostNativeThread::ThreadCreateTrampoline, info_up.get());

  if (destroy_attr)
    ::pthread_attr_destroy(&thread_attr);

  if (err)
    return llvm::errorCodeToError(
        std::error_code(err, std::generic_category()));
#endif

  info_up.release();
  return HostThread(thread);
}
