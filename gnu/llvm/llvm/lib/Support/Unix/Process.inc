//===- Unix/Process.cpp - Unix Process Implementation --------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the generic Unix implementation of the Process class.
//
//===----------------------------------------------------------------------===//

#include "Unix.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include <mutex>
#include <optional>
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if defined(HAVE_MALLINFO) || defined(HAVE_MALLINFO2)
#include <malloc.h>
#endif
#if defined(HAVE_MALLCTL)
#include <malloc_np.h>
#endif
#ifdef HAVE_MALLOC_MALLOC_H
#include <malloc/malloc.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only generic UNIX code that
//===          is guaranteed to work on *all* UNIX variants.
//===----------------------------------------------------------------------===//

using namespace llvm;
using namespace sys;

static std::pair<std::chrono::microseconds, std::chrono::microseconds>
getRUsageTimes() {
#if defined(HAVE_GETRUSAGE)
  struct rusage RU;
  ::getrusage(RUSAGE_SELF, &RU);
  return {toDuration(RU.ru_utime), toDuration(RU.ru_stime)};
#else
#ifndef __MVS__ // Exclude for MVS in case -pedantic is used
#warning Cannot get usage times on this platform
#endif
  return {std::chrono::microseconds::zero(), std::chrono::microseconds::zero()};
#endif
}

Process::Pid Process::getProcessId() {
  static_assert(sizeof(Pid) >= sizeof(pid_t),
                "Process::Pid should be big enough to store pid_t");
  return Pid(::getpid());
}

// On Cygwin, getpagesize() returns 64k(AllocationGranularity) and
// offset in mmap(3) should be aligned to the AllocationGranularity.
Expected<unsigned> Process::getPageSize() {
#if defined(HAVE_GETPAGESIZE)
  static const int page_size = ::getpagesize();
#elif defined(HAVE_SYSCONF)
  static long page_size = ::sysconf(_SC_PAGE_SIZE);
#else
#error Cannot get the page size on this machine
#endif
  if (page_size == -1)
    return errorCodeToError(errnoAsErrorCode());

  return static_cast<unsigned>(page_size);
}

size_t Process::GetMallocUsage() {
#if defined(HAVE_MALLINFO2)
  struct mallinfo2 mi;
  mi = ::mallinfo2();
  return mi.uordblks;
#elif defined(HAVE_MALLINFO)
  struct mallinfo mi;
  mi = ::mallinfo();
  return mi.uordblks;
#elif defined(HAVE_MALLOC_ZONE_STATISTICS) && defined(HAVE_MALLOC_MALLOC_H)
  malloc_statistics_t Stats;
  malloc_zone_statistics(malloc_default_zone(), &Stats);
  return Stats.size_in_use; // darwin
#elif defined(HAVE_MALLCTL)
  size_t alloc, sz;
  sz = sizeof(size_t);
  if (mallctl("stats.allocated", &alloc, &sz, NULL, 0) == 0)
    return alloc;
  return 0;
#elif defined(HAVE_SBRK)
  // Note this is only an approximation and more closely resembles
  // the value returned by mallinfo in the arena field.
  static char *StartOfMemory = reinterpret_cast<char *>(::sbrk(0));
  char *EndOfMemory = (char *)sbrk(0);
  if (EndOfMemory != ((char *)-1) && StartOfMemory != ((char *)-1))
    return EndOfMemory - StartOfMemory;
  return 0;
#else
#ifndef __MVS__ // Exclude for MVS in case -pedantic is used
#warning Cannot get malloc info on this platform
#endif
  return 0;
#endif
}

void Process::GetTimeUsage(TimePoint<> &elapsed,
                           std::chrono::nanoseconds &user_time,
                           std::chrono::nanoseconds &sys_time) {
  elapsed = std::chrono::system_clock::now();
  std::tie(user_time, sys_time) = getRUsageTimes();
}

#if defined(HAVE_MACH_MACH_H) && !defined(__GNU__)
#include <mach/mach.h>
#endif

// Some LLVM programs such as bugpoint produce core files as a normal part of
// their operation. To prevent the disk from filling up, this function
// does what's necessary to prevent their generation.
void Process::PreventCoreFiles() {
#if HAVE_SETRLIMIT
  struct rlimit rlim;
  getrlimit(RLIMIT_CORE, &rlim);
#ifdef __linux__
  // On Linux, if the kernel.core_pattern sysctl starts with a '|' (i.e. it
  // is being piped to a coredump handler such as systemd-coredumpd), the
  // kernel ignores RLIMIT_CORE (since we aren't creating a file in the file
  // system) except for the magic value of 1, which disables coredumps when
  // piping. 1 byte is too small for any kind of valid core dump, so it
  // also disables coredumps if kernel.core_pattern creates files directly.
  // While most piped coredump handlers do respect the crashing processes'
  // RLIMIT_CORE, this is notable not the case for Debian's systemd-coredump
  // due to a local patch that changes sysctl.d/50-coredump.conf to ignore
  // the specified limit and instead use RLIM_INFINITY.
  //
  // The alternative to using RLIMIT_CORE=1 would be to use prctl() with the
  // PR_SET_DUMPABLE flag, however that also prevents ptrace(), so makes it
  // impossible to attach a debugger.
  rlim.rlim_cur = std::min<rlim_t>(1, rlim.rlim_max);
#else
  rlim.rlim_cur = 0;
#endif
  setrlimit(RLIMIT_CORE, &rlim);
#endif

#if defined(HAVE_MACH_MACH_H) && !defined(__GNU__)
  // Disable crash reporting on Mac OS X 10.0-10.4

  // get information about the original set of exception ports for the task
  mach_msg_type_number_t Count = 0;
  exception_mask_t OriginalMasks[EXC_TYPES_COUNT];
  exception_port_t OriginalPorts[EXC_TYPES_COUNT];
  exception_behavior_t OriginalBehaviors[EXC_TYPES_COUNT];
  thread_state_flavor_t OriginalFlavors[EXC_TYPES_COUNT];
  kern_return_t err = task_get_exception_ports(
      mach_task_self(), EXC_MASK_ALL, OriginalMasks, &Count, OriginalPorts,
      OriginalBehaviors, OriginalFlavors);
  if (err == KERN_SUCCESS) {
    // replace each with MACH_PORT_NULL.
    for (unsigned i = 0; i != Count; ++i)
      task_set_exception_ports(mach_task_self(), OriginalMasks[i],
                               MACH_PORT_NULL, OriginalBehaviors[i],
                               OriginalFlavors[i]);
  }

  // Disable crash reporting on Mac OS X 10.5
  signal(SIGABRT, _exit);
  signal(SIGILL, _exit);
  signal(SIGFPE, _exit);
  signal(SIGSEGV, _exit);
  signal(SIGBUS, _exit);
#endif

  coreFilesPrevented = true;
}

std::optional<std::string> Process::GetEnv(StringRef Name) {
  std::string NameStr = Name.str();
  const char *Val = ::getenv(NameStr.c_str());
  if (!Val)
    return std::nullopt;
  return std::string(Val);
}

namespace {
class FDCloser {
public:
  FDCloser(int &FD) : FD(FD), KeepOpen(false) {}
  void keepOpen() { KeepOpen = true; }
  ~FDCloser() {
    if (!KeepOpen && FD >= 0)
      ::close(FD);
  }

private:
  FDCloser(const FDCloser &) = delete;
  void operator=(const FDCloser &) = delete;

  int &FD;
  bool KeepOpen;
};
} // namespace

std::error_code Process::FixupStandardFileDescriptors() {
  int NullFD = -1;
  FDCloser FDC(NullFD);
  const int StandardFDs[] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO};
  for (int StandardFD : StandardFDs) {
    struct stat st;
    errno = 0;
    if (RetryAfterSignal(-1, ::fstat, StandardFD, &st) < 0) {
      assert(errno && "expected errno to be set if fstat failed!");
      // fstat should return EBADF if the file descriptor is closed.
      if (errno != EBADF)
        return errnoAsErrorCode();
    }
    // if fstat succeeds, move on to the next FD.
    if (!errno)
      continue;
    assert(errno == EBADF && "expected errno to have EBADF at this point!");

    if (NullFD < 0) {
      // Call ::open in a lambda to avoid overload resolution in
      // RetryAfterSignal when open is overloaded, such as in Bionic.
      auto Open = [&]() { return ::open("/dev/null", O_RDWR); };
      if ((NullFD = RetryAfterSignal(-1, Open)) < 0)
        return errnoAsErrorCode();
    }

    if (NullFD == StandardFD)
      FDC.keepOpen();
    else if (dup2(NullFD, StandardFD) < 0)
      return errnoAsErrorCode();
  }
  return std::error_code();
}

std::error_code Process::SafelyCloseFileDescriptor(int FD) {
  // Create a signal set filled with *all* signals.
  sigset_t FullSet, SavedSet;
  if (sigfillset(&FullSet) < 0 || sigfillset(&SavedSet) < 0)
    return errnoAsErrorCode();

    // Atomically swap our current signal mask with a full mask.
#if LLVM_ENABLE_THREADS
  if (int EC = pthread_sigmask(SIG_SETMASK, &FullSet, &SavedSet))
    return std::error_code(EC, std::generic_category());
#else
  if (sigprocmask(SIG_SETMASK, &FullSet, &SavedSet) < 0)
    return errnoAsErrorCode();
#endif
  // Attempt to close the file descriptor.
  // We need to save the error, if one occurs, because our subsequent call to
  // pthread_sigmask might tamper with errno.
  int ErrnoFromClose = 0;
  if (::close(FD) < 0)
    ErrnoFromClose = errno;
  // Restore the signal mask back to what we saved earlier.
  int EC = 0;
#if LLVM_ENABLE_THREADS
  EC = pthread_sigmask(SIG_SETMASK, &SavedSet, nullptr);
#else
  if (sigprocmask(SIG_SETMASK, &SavedSet, nullptr) < 0)
    EC = errno;
#endif
  // The error code from close takes precedence over the one from
  // pthread_sigmask.
  if (ErrnoFromClose)
    return std::error_code(ErrnoFromClose, std::generic_category());
  return std::error_code(EC, std::generic_category());
}

bool Process::StandardInIsUserInput() {
  return FileDescriptorIsDisplayed(STDIN_FILENO);
}

bool Process::StandardOutIsDisplayed() {
  return FileDescriptorIsDisplayed(STDOUT_FILENO);
}

bool Process::StandardErrIsDisplayed() {
  return FileDescriptorIsDisplayed(STDERR_FILENO);
}

bool Process::FileDescriptorIsDisplayed(int fd) {
#if HAVE_ISATTY
  return isatty(fd);
#else
  // If we don't have isatty, just return false.
  return false;
#endif
}

static unsigned getColumns() {
  // If COLUMNS is defined in the environment, wrap to that many columns.
  if (const char *ColumnsStr = std::getenv("COLUMNS")) {
    int Columns = std::atoi(ColumnsStr);
    if (Columns > 0)
      return Columns;
  }

  // We used to call ioctl TIOCGWINSZ to determine the width. It is considered
  // unuseful.
  return 0;
}

unsigned Process::StandardOutColumns() {
  if (!StandardOutIsDisplayed())
    return 0;

  return getColumns();
}

unsigned Process::StandardErrColumns() {
  if (!StandardErrIsDisplayed())
    return 0;

  return getColumns();
}

static bool terminalHasColors() {
#if defined(__OpenBSD__)
	return false;
#endif
  // Check if the current terminal is one of terminals that are known to support
  // ANSI color escape codes.
  if (const char *TermStr = std::getenv("TERM")) {
    return StringSwitch<bool>(TermStr)
        .Case("ansi", true)
        .Case("cygwin", true)
        .Case("linux", true)
        .StartsWith("screen", true)
        .StartsWith("xterm", true)
        .StartsWith("vt100", true)
        .StartsWith("rxvt", true)
        .EndsWith("color", true)
        .Default(false);
  }

  return false;
}

bool Process::FileDescriptorHasColors(int fd) {
  // A file descriptor has colors if it is displayed and the terminal has
  // colors.
  return FileDescriptorIsDisplayed(fd) && terminalHasColors();
}

bool Process::StandardOutHasColors() {
  return FileDescriptorHasColors(STDOUT_FILENO);
}

bool Process::StandardErrHasColors() {
  return FileDescriptorHasColors(STDERR_FILENO);
}

void Process::UseANSIEscapeCodes(bool /*enable*/) {
  // No effect.
}

bool Process::ColorNeedsFlush() {
  // No, we use ANSI escape sequences.
  return false;
}

const char *Process::OutputColor(char code, bool bold, bool bg) {
  return colorcodes[bg ? 1 : 0][bold ? 1 : 0][code & 15];
}

const char *Process::OutputBold(bool bg) { return "\033[1m"; }

const char *Process::OutputReverse() { return "\033[7m"; }

const char *Process::ResetColor() { return "\033[0m"; }

#if !HAVE_DECL_ARC4RANDOM
static unsigned GetRandomNumberSeed() {
  // Attempt to get the initial seed from /dev/urandom, if possible.
  int urandomFD = open("/dev/urandom", O_RDONLY);

  if (urandomFD != -1) {
    unsigned seed;
    // Don't use a buffered read to avoid reading more data
    // from /dev/urandom than we need.
    int count = read(urandomFD, (void *)&seed, sizeof(seed));

    close(urandomFD);

    // Return the seed if the read was successful.
    if (count == sizeof(seed))
      return seed;
  }

  // Otherwise, swizzle the current time and the process ID to form a reasonable
  // seed.
  const auto Now = std::chrono::high_resolution_clock::now();
  return hash_combine(Now.time_since_epoch().count(), ::getpid());
}
#endif

unsigned llvm::sys::Process::GetRandomNumber() {
#if HAVE_DECL_ARC4RANDOM
  return arc4random();
#else
  static int x = (static_cast<void>(::srand(GetRandomNumberSeed())), 0);
  (void)x;
  return ::rand();
#endif
}

[[noreturn]] void Process::ExitNoCleanup(int RetCode) { _Exit(RetCode); }
