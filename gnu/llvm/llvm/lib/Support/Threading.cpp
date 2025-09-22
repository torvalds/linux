//===-- llvm/Support/Threading.cpp- Control multithreading mode --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines helper functions for running LLVM in a multi-threaded
// environment.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Threading.h"
#include "llvm/Config/config.h"
#include "llvm/Config/llvm-config.h"

#include <cassert>
#include <errno.h>
#include <optional>
#include <stdlib.h>
#include <string.h>

using namespace llvm;

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

#if LLVM_ENABLE_THREADS == 0 ||                                                \
    (!defined(_WIN32) && !defined(HAVE_PTHREAD_H))
uint64_t llvm::get_threadid() { return 0; }

uint32_t llvm::get_max_thread_name_length() { return 0; }

void llvm::set_thread_name(const Twine &Name) {}

void llvm::get_thread_name(SmallVectorImpl<char> &Name) { Name.clear(); }

llvm::BitVector llvm::get_thread_affinity_mask() { return {}; }

unsigned llvm::ThreadPoolStrategy::compute_thread_count() const {
  // When threads are disabled, ensure clients will loop at least once.
  return 1;
}

// Unknown if threading turned off
int llvm::get_physical_cores() { return -1; }

#else

static int computeHostNumHardwareThreads();

unsigned llvm::ThreadPoolStrategy::compute_thread_count() const {
  int MaxThreadCount =
      UseHyperThreads ? computeHostNumHardwareThreads() : get_physical_cores();
  if (MaxThreadCount <= 0)
    MaxThreadCount = 1;
  if (ThreadsRequested == 0)
    return MaxThreadCount;
  if (!Limit)
    return ThreadsRequested;
  return std::min((unsigned)MaxThreadCount, ThreadsRequested);
}

// Include the platform-specific parts of this class.
#ifdef LLVM_ON_UNIX
#include "Unix/Threading.inc"
#endif
#ifdef _WIN32
#include "Windows/Threading.inc"
#endif

// Must be included after Threading.inc to provide definition for llvm::thread
// because FreeBSD's condvar.h (included by user.h) misuses the "thread"
// keyword.
#include "llvm/Support/thread.h"

#if defined(__APPLE__)
  // Darwin's default stack size for threads except the main one is only 512KB,
  // which is not enough for some/many normal LLVM compilations. This implements
  // the same interface as std::thread but requests the same stack size as the
  // main thread (8MB) before creation.
const std::optional<unsigned> llvm::thread::DefaultStackSize = 8 * 1024 * 1024;
#elif defined(_AIX)
  // On AIX, the default pthread stack size limit is ~192k for 64-bit programs.
  // This limit is easily reached when doing link-time thinLTO. AIX library
  // developers have used 4MB, so we'll do the same.
const std::optional<unsigned> llvm::thread::DefaultStackSize = 4 * 1024 * 1024;
#else
const std::optional<unsigned> llvm::thread::DefaultStackSize;
#endif


#endif

std::optional<ThreadPoolStrategy>
llvm::get_threadpool_strategy(StringRef Num, ThreadPoolStrategy Default) {
  if (Num == "all")
    return llvm::hardware_concurrency();
  if (Num.empty())
    return Default;
  unsigned V;
  if (Num.getAsInteger(10, V))
    return std::nullopt; // malformed 'Num' value
  if (V == 0)
    return Default;

  // Do not take the Default into account. This effectively disables
  // heavyweight_hardware_concurrency() if the user asks for any number of
  // threads on the cmd-line.
  ThreadPoolStrategy S = llvm::hardware_concurrency();
  S.ThreadsRequested = V;
  return S;
}
