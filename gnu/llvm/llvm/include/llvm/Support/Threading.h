//===-- llvm/Support/Threading.h - Control multithreading mode --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares helper functions for running LLVM in a multi-threaded
// environment.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREADING_H
#define LLVM_SUPPORT_THREADING_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h" // for LLVM_ON_UNIX
#include "llvm/Support/Compiler.h"
#include <ciso646> // So we can check the C++ standard lib macros.
#include <optional>

#if defined(_MSC_VER)
// MSVC's call_once implementation worked since VS 2015, which is the minimum
// supported version as of this writing.
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#elif defined(LLVM_ON_UNIX) &&                                                 \
    (defined(_LIBCPP_VERSION) ||                                               \
     !(defined(__NetBSD__) || defined(__OpenBSD__) || defined(__powerpc__)))
// std::call_once from libc++ is used on all Unix platforms. Other
// implementations like libstdc++ are known to have problems on NetBSD,
// OpenBSD and PowerPC.
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#elif defined(LLVM_ON_UNIX) &&                                                 \
    (defined(__powerpc__) && defined(__LITTLE_ENDIAN__))
#define LLVM_THREADING_USE_STD_CALL_ONCE 1
#else
#define LLVM_THREADING_USE_STD_CALL_ONCE 0
#endif

#if LLVM_THREADING_USE_STD_CALL_ONCE
#include <mutex>
#else
#include "llvm/Support/Atomic.h"
#endif

namespace llvm {
class Twine;

/// Returns true if LLVM is compiled with support for multi-threading, and
/// false otherwise.
constexpr bool llvm_is_multithreaded() { return LLVM_ENABLE_THREADS; }

#if LLVM_THREADING_USE_STD_CALL_ONCE

  typedef std::once_flag once_flag;

#else

  enum InitStatus { Uninitialized = 0, Wait = 1, Done = 2 };

  /// The llvm::once_flag structure
  ///
  /// This type is modeled after std::once_flag to use with llvm::call_once.
  /// This structure must be used as an opaque object. It is a struct to force
  /// autoinitialization and behave like std::once_flag.
  struct once_flag {
    volatile sys::cas_flag status = Uninitialized;
  };

#endif

  /// Execute the function specified as a parameter once.
  ///
  /// Typical usage:
  /// \code
  ///   void foo() {...};
  ///   ...
  ///   static once_flag flag;
  ///   call_once(flag, foo);
  /// \endcode
  ///
  /// \param flag Flag used for tracking whether or not this has run.
  /// \param F Function to call once.
  template <typename Function, typename... Args>
  void call_once(once_flag &flag, Function &&F, Args &&... ArgList) {
#if LLVM_THREADING_USE_STD_CALL_ONCE
    std::call_once(flag, std::forward<Function>(F),
                   std::forward<Args>(ArgList)...);
#else
    // For other platforms we use a generic (if brittle) version based on our
    // atomics.
    sys::cas_flag old_val = sys::CompareAndSwap(&flag.status, Wait, Uninitialized);
    if (old_val == Uninitialized) {
      std::forward<Function>(F)(std::forward<Args>(ArgList)...);
      sys::MemoryFence();
      TsanIgnoreWritesBegin();
      TsanHappensBefore(&flag.status);
      flag.status = Done;
      TsanIgnoreWritesEnd();
    } else {
      // Wait until any thread doing the call has finished.
      sys::cas_flag tmp = flag.status;
      sys::MemoryFence();
      while (tmp != Done) {
        tmp = flag.status;
        sys::MemoryFence();
      }
    }
    TsanHappensAfter(&flag.status);
#endif
  }

  /// This tells how a thread pool will be used
  class ThreadPoolStrategy {
  public:
    // The default value (0) means all available threads should be used,
    // taking the affinity mask into account. If set, this value only represents
    // a suggested high bound, the runtime might choose a lower value (not
    // higher).
    unsigned ThreadsRequested = 0;

    // If SMT is active, use hyper threads. If false, there will be only one
    // std::thread per core.
    bool UseHyperThreads = true;

    // If set, will constrain 'ThreadsRequested' to the number of hardware
    // threads, or hardware cores.
    bool Limit = false;

    /// Retrieves the max available threads for the current strategy. This
    /// accounts for affinity masks and takes advantage of all CPU sockets.
    unsigned compute_thread_count() const;

    /// Assign the current thread to an ideal hardware CPU or NUMA node. In a
    /// multi-socket system, this ensures threads are assigned to all CPU
    /// sockets. \p ThreadPoolNum represents a number bounded by [0,
    /// compute_thread_count()).
    void apply_thread_strategy(unsigned ThreadPoolNum) const;

    /// Finds the CPU socket where a thread should go. Returns 'std::nullopt' if
    /// the thread shall remain on the actual CPU socket.
    std::optional<unsigned> compute_cpu_socket(unsigned ThreadPoolNum) const;
  };

  /// Build a strategy from a number of threads as a string provided in \p Num.
  /// When Num is above the max number of threads specified by the \p Default
  /// strategy, we attempt to equally allocate the threads on all CPU sockets.
  /// "0" or an empty string will return the \p Default strategy.
  /// "all" for using all hardware threads.
  std::optional<ThreadPoolStrategy>
  get_threadpool_strategy(StringRef Num, ThreadPoolStrategy Default = {});

  /// Returns a thread strategy for tasks requiring significant memory or other
  /// resources. To be used for workloads where hardware_concurrency() proves to
  /// be less efficient. Avoid this strategy if doing lots of I/O. Currently
  /// based on physical cores, if available for the host system, otherwise falls
  /// back to hardware_concurrency(). Returns 1 when LLVM is configured with
  /// LLVM_ENABLE_THREADS = OFF.
  inline ThreadPoolStrategy
  heavyweight_hardware_concurrency(unsigned ThreadCount = 0) {
    ThreadPoolStrategy S;
    S.UseHyperThreads = false;
    S.ThreadsRequested = ThreadCount;
    return S;
  }

  /// Like heavyweight_hardware_concurrency() above, but builds a strategy
  /// based on the rules described for get_threadpool_strategy().
  /// If \p Num is invalid, returns a default strategy where one thread per
  /// hardware core is used.
  inline ThreadPoolStrategy heavyweight_hardware_concurrency(StringRef Num) {
    std::optional<ThreadPoolStrategy> S =
        get_threadpool_strategy(Num, heavyweight_hardware_concurrency());
    if (S)
      return *S;
    return heavyweight_hardware_concurrency();
  }

  /// Returns a default thread strategy where all available hardware resources
  /// are to be used, except for those initially excluded by an affinity mask.
  /// This function takes affinity into consideration. Returns 1 when LLVM is
  /// configured with LLVM_ENABLE_THREADS=OFF.
  inline ThreadPoolStrategy hardware_concurrency(unsigned ThreadCount = 0) {
    ThreadPoolStrategy S;
    S.ThreadsRequested = ThreadCount;
    return S;
  }

  /// Returns an optimal thread strategy to execute specified amount of tasks.
  /// This strategy should prevent us from creating too many threads if we
  /// occasionaly have an unexpectedly small amount of tasks.
  inline ThreadPoolStrategy optimal_concurrency(unsigned TaskCount = 0) {
    ThreadPoolStrategy S;
    S.Limit = true;
    S.ThreadsRequested = TaskCount;
    return S;
  }

  /// Return the current thread id, as used in various OS system calls.
  /// Note that not all platforms guarantee that the value returned will be
  /// unique across the entire system, so portable code should not assume
  /// this.
  uint64_t get_threadid();

  /// Get the maximum length of a thread name on this platform.
  /// A value of 0 means there is no limit.
  uint32_t get_max_thread_name_length();

  /// Set the name of the current thread.  Setting a thread's name can
  /// be helpful for enabling useful diagnostics under a debugger or when
  /// logging.  The level of support for setting a thread's name varies
  /// wildly across operating systems, and we only make a best effort to
  /// perform the operation on supported platforms.  No indication of success
  /// or failure is returned.
  void set_thread_name(const Twine &Name);

  /// Get the name of the current thread.  The level of support for
  /// getting a thread's name varies wildly across operating systems, and it
  /// is not even guaranteed that if you can successfully set a thread's name
  /// that you can later get it back.  This function is intended for diagnostic
  /// purposes, and as with setting a thread's name no indication of whether
  /// the operation succeeded or failed is returned.
  void get_thread_name(SmallVectorImpl<char> &Name);

  /// Returns a mask that represents on which hardware thread, core, CPU, NUMA
  /// group, the calling thread can be executed. On Windows, threads cannot
  /// cross CPU sockets boundaries.
  llvm::BitVector get_thread_affinity_mask();

  /// Returns how many physical CPUs or NUMA groups the system has.
  unsigned get_cpus();

  /// Returns how many physical cores (as opposed to logical cores returned from
  /// thread::hardware_concurrency(), which includes hyperthreads).
  /// Returns -1 if unknown for the current host system.
  int get_physical_cores();

  enum class ThreadPriority {
    /// Lower the current thread's priority as much as possible. Can be used
    /// for long-running tasks that are not time critical; more energy-
    /// efficient than Low.
    Background = 0,

    /// Lower the current thread's priority such that it does not affect
    /// foreground tasks significantly. This is a good default for long-
    /// running, latency-insensitive tasks to make sure cpu is not hogged
    /// by this task.
    Low = 1,

    /// Restore the current thread's priority to default scheduling priority.
    Default = 2,
  };
  enum class SetThreadPriorityResult { FAILURE, SUCCESS };
  SetThreadPriorityResult set_thread_priority(ThreadPriority Priority);
}

#endif
