//===- Unix/Threading.inc - Unix Threading Implementation ----- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Unix specific implementation of Threading functions.
//
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#if defined(__APPLE__)
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <pthread/qos.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#include <pthread.h>

#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h> // For pthread_getthreadid_np() / pthread_set_name_np()
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <errno.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>
#endif

#if defined(__NetBSD__)
#include <lwp.h> // For _lwp_self()
#endif

#if defined(__OpenBSD__)
#include <unistd.h> // For getthrid()
#endif

#if defined(__linux__)
#include <sched.h>       // For sched_getaffinity
#include <sys/syscall.h> // For syscall codes
#include <unistd.h>      // For syscall()
#endif

namespace llvm {
pthread_t
llvm_execute_on_thread_impl(void *(*ThreadFunc)(void *), void *Arg,
                            std::optional<unsigned> StackSizeInBytes) {
  int errnum;

  // Construct the attributes object.
  pthread_attr_t Attr;
  if ((errnum = ::pthread_attr_init(&Attr)) != 0) {
    ReportErrnumFatal("pthread_attr_init failed", errnum);
  }

  auto AttrGuard = llvm::make_scope_exit([&] {
    if ((errnum = ::pthread_attr_destroy(&Attr)) != 0) {
      ReportErrnumFatal("pthread_attr_destroy failed", errnum);
    }
  });

  // Set the requested stack size, if given.
  if (StackSizeInBytes) {
    if ((errnum = ::pthread_attr_setstacksize(&Attr, *StackSizeInBytes)) != 0) {
      ReportErrnumFatal("pthread_attr_setstacksize failed", errnum);
    }
  }

  // Construct and execute the thread.
  pthread_t Thread;
  if ((errnum = ::pthread_create(&Thread, &Attr, ThreadFunc, Arg)) != 0)
    ReportErrnumFatal("pthread_create failed", errnum);

  return Thread;
}

void llvm_thread_detach_impl(pthread_t Thread) {
  int errnum;

  if ((errnum = ::pthread_detach(Thread)) != 0) {
    ReportErrnumFatal("pthread_detach failed", errnum);
  }
}

void llvm_thread_join_impl(pthread_t Thread) {
  int errnum;

  if ((errnum = ::pthread_join(Thread, nullptr)) != 0) {
    ReportErrnumFatal("pthread_join failed", errnum);
  }
}

pthread_t llvm_thread_get_id_impl(pthread_t Thread) { return Thread; }

pthread_t llvm_thread_get_current_id_impl() { return ::pthread_self(); }

} // namespace llvm

uint64_t llvm::get_threadid() {
#if defined(__APPLE__)
  // Calling "mach_thread_self()" bumps the reference count on the thread
  // port, so we need to deallocate it. mach_task_self() doesn't bump the ref
  // count.
  static thread_local thread_port_t Self = [] {
    thread_port_t InitSelf = mach_thread_self();
    mach_port_deallocate(mach_task_self(), Self);
    return InitSelf;
  }();
  return Self;
#elif defined(__FreeBSD__)
  return uint64_t(pthread_getthreadid_np());
#elif defined(__NetBSD__)
  return uint64_t(_lwp_self());
#elif defined(__OpenBSD__)
  return uint64_t(getthrid());
#elif defined(__ANDROID__)
  return uint64_t(gettid());
#elif defined(__linux__)
  return uint64_t(syscall(SYS_gettid));
#else
  return uint64_t(pthread_self());
#endif
}

static constexpr uint32_t get_max_thread_name_length_impl() {
#if defined(__NetBSD__)
  return PTHREAD_MAX_NAMELEN_NP;
#elif defined(__APPLE__)
  return 64;
#elif defined(__linux__)
#if HAVE_PTHREAD_SETNAME_NP
  return 16;
#else
  return 0;
#endif
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  return 16;
#elif defined(__OpenBSD__)
  return 32;
#else
  return 0;
#endif
}

uint32_t llvm::get_max_thread_name_length() {
  return get_max_thread_name_length_impl();
}

void llvm::set_thread_name(const Twine &Name) {
  // Make sure the input is null terminated.
  SmallString<64> Storage;
  StringRef NameStr = Name.toNullTerminatedStringRef(Storage);

  // Truncate from the beginning, not the end, if the specified name is too
  // long.  For one, this ensures that the resulting string is still null
  // terminated, but additionally the end of a long thread name will usually
  // be more unique than the beginning, since a common pattern is for similar
  // threads to share a common prefix.
  // Note that the name length includes the null terminator.
  if (get_max_thread_name_length() > 0)
    NameStr = NameStr.take_back(get_max_thread_name_length() - 1);
  (void)NameStr;
#if defined(__linux__)
#if (defined(__GLIBC__) && defined(_GNU_SOURCE)) || defined(__ANDROID__)
#if HAVE_PTHREAD_SETNAME_NP
  ::pthread_setname_np(::pthread_self(), NameStr.data());
#endif
#endif
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
  ::pthread_set_name_np(::pthread_self(), NameStr.data());
#elif defined(__NetBSD__)
  ::pthread_setname_np(::pthread_self(), "%s",
                       const_cast<char *>(NameStr.data()));
#elif defined(__APPLE__)
  ::pthread_setname_np(NameStr.data());
#endif
}

void llvm::get_thread_name(SmallVectorImpl<char> &Name) {
  Name.clear();

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  int pid = ::getpid();
  uint64_t tid = get_threadid();

  struct kinfo_proc *kp = nullptr, *nkp;
  size_t len = 0;
  int error;
  int ctl[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID | KERN_PROC_INC_THREAD,
                (int)pid};

  while (1) {
    error = sysctl(ctl, 4, kp, &len, nullptr, 0);
    if (kp == nullptr || (error != 0 && errno == ENOMEM)) {
      // Add extra space in case threads are added before next call.
      len += sizeof(*kp) + len / 10;
      nkp = (struct kinfo_proc *)::realloc(kp, len);
      if (nkp == nullptr) {
        free(kp);
        return;
      }
      kp = nkp;
      continue;
    }
    if (error != 0)
      len = 0;
    break;
  }

  for (size_t i = 0; i < len / sizeof(*kp); i++) {
    if (kp[i].ki_tid == (lwpid_t)tid) {
      Name.append(kp[i].ki_tdname, kp[i].ki_tdname + strlen(kp[i].ki_tdname));
      break;
    }
  }
  free(kp);
  return;
#elif defined(__NetBSD__)
  constexpr uint32_t len = get_max_thread_name_length_impl();
  char buf[len];
  ::pthread_getname_np(::pthread_self(), buf, len);

  Name.append(buf, buf + strlen(buf));
#elif defined(__OpenBSD__)
  constexpr uint32_t len = get_max_thread_name_length_impl();
  char buf[len];
  ::pthread_get_name_np(::pthread_self(), buf, len);

  Name.append(buf, buf + strlen(buf));
#elif defined(__linux__)
#if HAVE_PTHREAD_GETNAME_NP
  constexpr uint32_t len = get_max_thread_name_length_impl();
  char Buffer[len] = {'\0'}; // FIXME: working around MSan false positive.
  if (0 == ::pthread_getname_np(::pthread_self(), Buffer, len))
    Name.append(Buffer, Buffer + strlen(Buffer));
#endif
#endif
}

SetThreadPriorityResult llvm::set_thread_priority(ThreadPriority Priority) {
#if defined(__linux__) && defined(SCHED_IDLE)
  // Some *really* old glibcs are missing SCHED_IDLE.
  // http://man7.org/linux/man-pages/man3/pthread_setschedparam.3.html
  // http://man7.org/linux/man-pages/man2/sched_setscheduler.2.html
  sched_param priority;
  // For each of the above policies, param->sched_priority must be 0.
  priority.sched_priority = 0;
  // SCHED_IDLE    for running very low priority background jobs.
  // SCHED_OTHER   the standard round-robin time-sharing policy;
  return !pthread_setschedparam(
             pthread_self(),
             // FIXME: consider SCHED_BATCH for Low
             Priority == ThreadPriority::Default ? SCHED_OTHER : SCHED_IDLE,
             &priority)
             ? SetThreadPriorityResult::SUCCESS
             : SetThreadPriorityResult::FAILURE;
#elif defined(__APPLE__)
  // https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon
  //
  // Background - Applies to work that isn’t visible to the user and may take
  // significant time to complete. Examples include indexing, backing up, or
  // synchronizing data. This class emphasizes energy efficiency.
  //
  // Utility - Applies to work that takes anywhere from a few seconds to a few
  // minutes to complete. Examples include downloading a document or importing
  // data. This class offers a balance between responsiveness, performance, and
  // energy efficiency.
  const auto qosClass = [&]() {
    switch (Priority) {
    case ThreadPriority::Background:
      return QOS_CLASS_BACKGROUND;
    case ThreadPriority::Low:
      return QOS_CLASS_UTILITY;
    case ThreadPriority::Default:
      return QOS_CLASS_DEFAULT;
    }
  }();
  return !pthread_set_qos_class_self_np(qosClass, 0)
             ? SetThreadPriorityResult::SUCCESS
             : SetThreadPriorityResult::FAILURE;
#endif
  return SetThreadPriorityResult::FAILURE;
}

#include <thread>

static int computeHostNumHardwareThreads() {
#if defined(__FreeBSD__)
  cpuset_t mask;
  CPU_ZERO(&mask);
  if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(mask),
                         &mask) == 0)
    return CPU_COUNT(&mask);
#elif defined(__linux__)
  cpu_set_t Set;
  if (sched_getaffinity(0, sizeof(Set), &Set) == 0)
    return CPU_COUNT(&Set);
#endif
  // Guard against std::thread::hardware_concurrency() returning 0.
  if (unsigned Val = std::thread::hardware_concurrency())
    return Val;
  return 1;
}

void llvm::ThreadPoolStrategy::apply_thread_strategy(
    unsigned ThreadPoolNum) const {}

llvm::BitVector llvm::get_thread_affinity_mask() {
  // FIXME: Implement
  llvm_unreachable("Not implemented!");
}

unsigned llvm::get_cpus() { return 1; }

#if defined(__linux__) && (defined(__i386__) || defined(__x86_64__))
// On Linux, the number of physical cores can be computed from /proc/cpuinfo,
// using the number of unique physical/core id pairs. The following
// implementation reads the /proc/cpuinfo format on an x86_64 system.
static int computeHostNumPhysicalCores() {
  // Enabled represents the number of physical id/core id pairs with at least
  // one processor id enabled by the CPU affinity mask.
  cpu_set_t Affinity, Enabled;
  if (sched_getaffinity(0, sizeof(Affinity), &Affinity) != 0)
    return -1;
  CPU_ZERO(&Enabled);

  // Read /proc/cpuinfo as a stream (until EOF reached). It cannot be
  // mmapped because it appears to have 0 size.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Text =
      llvm::MemoryBuffer::getFileAsStream("/proc/cpuinfo");
  if (std::error_code EC = Text.getError()) {
    llvm::errs() << "Can't read "
                 << "/proc/cpuinfo: " << EC.message() << "\n";
    return -1;
  }
  SmallVector<StringRef, 8> strs;
  (*Text)->getBuffer().split(strs, "\n", /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
  int CurProcessor = -1;
  int CurPhysicalId = -1;
  int CurSiblings = -1;
  int CurCoreId = -1;
  for (StringRef Line : strs) {
    std::pair<StringRef, StringRef> Data = Line.split(':');
    auto Name = Data.first.trim();
    auto Val = Data.second.trim();
    // These fields are available if the kernel is configured with CONFIG_SMP.
    if (Name == "processor")
      Val.getAsInteger(10, CurProcessor);
    else if (Name == "physical id")
      Val.getAsInteger(10, CurPhysicalId);
    else if (Name == "siblings")
      Val.getAsInteger(10, CurSiblings);
    else if (Name == "core id") {
      Val.getAsInteger(10, CurCoreId);
      // The processor id corresponds to an index into cpu_set_t.
      if (CPU_ISSET(CurProcessor, &Affinity))
        CPU_SET(CurPhysicalId * CurSiblings + CurCoreId, &Enabled);
    }
  }
  return CPU_COUNT(&Enabled);
}
#elif (defined(__linux__) && defined(__s390x__)) || defined(_AIX)
static int computeHostNumPhysicalCores() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}
#elif defined(__linux__) && !defined(__ANDROID__)
static int computeHostNumPhysicalCores() {
  cpu_set_t Affinity;
  if (sched_getaffinity(0, sizeof(Affinity), &Affinity) == 0)
    return CPU_COUNT(&Affinity);

  // The call to sched_getaffinity() may have failed because the Affinity
  // mask is too small for the number of CPU's on the system (i.e. the
  // system has more than 1024 CPUs). Allocate a mask large enough for
  // twice as many CPUs.
  cpu_set_t *DynAffinity;
  DynAffinity = CPU_ALLOC(2048);
  if (sched_getaffinity(0, CPU_ALLOC_SIZE(2048), DynAffinity) == 0) {
    int NumCPUs = CPU_COUNT(DynAffinity);
    CPU_FREE(DynAffinity);
    return NumCPUs;
  }
  return -1;
}
#elif defined(__APPLE__)
// Gets the number of *physical cores* on the machine.
static int computeHostNumPhysicalCores() {
  uint32_t count;
  size_t len = sizeof(count);
  sysctlbyname("hw.physicalcpu", &count, &len, NULL, 0);
  if (count < 1) {
    int nm[2];
    nm[0] = CTL_HW;
    nm[1] = HW_AVAILCPU;
    sysctl(nm, 2, &count, &len, NULL, 0);
    if (count < 1)
      return -1;
  }
  return count;
}
#elif defined(__MVS__)
static int computeHostNumPhysicalCores() {
  enum {
    // Byte offset of the pointer to the Communications Vector Table (CVT) in
    // the Prefixed Save Area (PSA). The table entry is a 31-bit pointer and
    // will be zero-extended to uintptr_t.
    FLCCVT = 16,
    // Byte offset of the pointer to the Common System Data Area (CSD) in the
    // CVT. The table entry is a 31-bit pointer and will be zero-extended to
    // uintptr_t.
    CVTCSD = 660,
    // Byte offset to the number of live CPs in the LPAR, stored as a signed
    // 32-bit value in the table.
    CSD_NUMBER_ONLINE_STANDARD_CPS = 264,
  };
  char *PSA = 0;
  char *CVT = reinterpret_cast<char *>(
      static_cast<uintptr_t>(reinterpret_cast<unsigned int &>(PSA[FLCCVT])));
  char *CSD = reinterpret_cast<char *>(
      static_cast<uintptr_t>(reinterpret_cast<unsigned int &>(CVT[CVTCSD])));
  return reinterpret_cast<int &>(CSD[CSD_NUMBER_ONLINE_STANDARD_CPS]);
}
#else
// On other systems, return -1 to indicate unknown.
static int computeHostNumPhysicalCores() { return -1; }
#endif

int llvm::get_physical_cores() {
  static int NumCores = computeHostNumPhysicalCores();
  return NumCores;
}
