//===-- sanitizer_mac.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries and
// implements OSX-specific functions.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_APPLE
#  include "interception/interception.h"
#  include "sanitizer_mac.h"

// Use 64-bit inodes in file operations. ASan does not support OS X 10.5, so
// the clients will most certainly use 64-bit ones as well.
#  ifndef _DARWIN_USE_64_BIT_INODE
#    define _DARWIN_USE_64_BIT_INODE 1
#  endif
#  include <stdio.h>

#  include "sanitizer_common.h"
#  include "sanitizer_file.h"
#  include "sanitizer_flags.h"
#  include "sanitizer_interface_internal.h"
#  include "sanitizer_internal_defs.h"
#  include "sanitizer_libc.h"
#  include "sanitizer_platform_limits_posix.h"
#  include "sanitizer_procmaps.h"
#  include "sanitizer_ptrauth.h"

#  if !SANITIZER_IOS
#    include <crt_externs.h>  // for _NSGetEnviron
#  else
extern char **environ;
#  endif

#  if defined(__has_include) && __has_include(<os/trace.h>)
#    define SANITIZER_OS_TRACE 1
#    include <os/trace.h>
#  else
#    define SANITIZER_OS_TRACE 0
#  endif

// import new crash reporting api
#  if defined(__has_include) && __has_include(<CrashReporterClient.h>)
#    define HAVE_CRASHREPORTERCLIENT_H 1
#    include <CrashReporterClient.h>
#  else
#    define HAVE_CRASHREPORTERCLIENT_H 0
#  endif

#  if !SANITIZER_IOS
#    include <crt_externs.h>  // for _NSGetArgv and _NSGetEnviron
#  else
extern "C" {
extern char ***_NSGetArgv(void);
}
#  endif

#  include <asl.h>
#  include <dlfcn.h>  // for dladdr()
#  include <errno.h>
#  include <fcntl.h>
#  include <libkern/OSAtomic.h>
#  include <mach-o/dyld.h>
#  include <mach/mach.h>
#  include <mach/mach_time.h>
#  include <mach/vm_statistics.h>
#  include <malloc/malloc.h>
#  include <os/log.h>
#  include <pthread.h>
#  include <pthread/introspection.h>
#  include <sched.h>
#  include <signal.h>
#  include <spawn.h>
#  include <stdlib.h>
#  include <sys/ioctl.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#  include <sys/stat.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <util.h>

// From <crt_externs.h>, but we don't have that file on iOS.
extern "C" {
  extern char ***_NSGetArgv(void);
  extern char ***_NSGetEnviron(void);
}

// From <mach/mach_vm.h>, but we don't have that file on iOS.
extern "C" {
  extern kern_return_t mach_vm_region_recurse(
    vm_map_t target_task,
    mach_vm_address_t *address,
    mach_vm_size_t *size,
    natural_t *nesting_depth,
    vm_region_recurse_info_t info,
    mach_msg_type_number_t *infoCnt);
}

namespace __sanitizer {

#include "sanitizer_syscall_generic.inc"

// Direct syscalls, don't call libmalloc hooks (but not available on 10.6).
extern "C" void *__mmap(void *addr, size_t len, int prot, int flags, int fildes,
                        off_t off) SANITIZER_WEAK_ATTRIBUTE;
extern "C" int __munmap(void *, size_t) SANITIZER_WEAK_ATTRIBUTE;

// ---------------------- sanitizer_libc.h

// From <mach/vm_statistics.h>, but not on older OSs.
#ifndef VM_MEMORY_SANITIZER
#define VM_MEMORY_SANITIZER 99
#endif

// XNU on Darwin provides a mmap flag that optimizes allocation/deallocation of
// giant memory regions (i.e. shadow memory regions).
#define kXnuFastMmapFd 0x4
static size_t kXnuFastMmapThreshold = 2 << 30; // 2 GB
static bool use_xnu_fast_mmap = false;

uptr internal_mmap(void *addr, size_t length, int prot, int flags,
                   int fd, u64 offset) {
  if (fd == -1) {
    fd = VM_MAKE_TAG(VM_MEMORY_SANITIZER);
    if (length >= kXnuFastMmapThreshold) {
      if (use_xnu_fast_mmap) fd |= kXnuFastMmapFd;
    }
  }
  if (&__mmap) return (uptr)__mmap(addr, length, prot, flags, fd, offset);
  return (uptr)mmap(addr, length, prot, flags, fd, offset);
}

uptr internal_munmap(void *addr, uptr length) {
  if (&__munmap) return __munmap(addr, length);
  return munmap(addr, length);
}

uptr internal_mremap(void *old_address, uptr old_size, uptr new_size, int flags,
                     void *new_address) {
  CHECK(false && "internal_mremap is unimplemented on Mac");
  return 0;
}

int internal_mprotect(void *addr, uptr length, int prot) {
  return mprotect(addr, length, prot);
}

int internal_madvise(uptr addr, uptr length, int advice) {
  return madvise((void *)addr, length, advice);
}

uptr internal_close(fd_t fd) {
  return close(fd);
}

uptr internal_open(const char *filename, int flags) {
  return open(filename, flags);
}

uptr internal_open(const char *filename, int flags, u32 mode) {
  return open(filename, flags, mode);
}

uptr internal_read(fd_t fd, void *buf, uptr count) {
  return read(fd, buf, count);
}

uptr internal_write(fd_t fd, const void *buf, uptr count) {
  return write(fd, buf, count);
}

uptr internal_stat(const char *path, void *buf) {
  return stat(path, (struct stat *)buf);
}

uptr internal_lstat(const char *path, void *buf) {
  return lstat(path, (struct stat *)buf);
}

uptr internal_fstat(fd_t fd, void *buf) {
  return fstat(fd, (struct stat *)buf);
}

uptr internal_filesize(fd_t fd) {
  struct stat st;
  if (internal_fstat(fd, &st))
    return -1;
  return (uptr)st.st_size;
}

uptr internal_dup(int oldfd) {
  return dup(oldfd);
}

uptr internal_dup2(int oldfd, int newfd) {
  return dup2(oldfd, newfd);
}

uptr internal_readlink(const char *path, char *buf, uptr bufsize) {
  return readlink(path, buf, bufsize);
}

uptr internal_unlink(const char *path) {
  return unlink(path);
}

uptr internal_sched_yield() {
  return sched_yield();
}

void internal__exit(int exitcode) {
  _exit(exitcode);
}

void internal_usleep(u64 useconds) { usleep(useconds); }

uptr internal_getpid() {
  return getpid();
}

int internal_dlinfo(void *handle, int request, void *p) {
  UNIMPLEMENTED();
}

int internal_sigaction(int signum, const void *act, void *oldact) {
  return sigaction(signum,
                   (const struct sigaction *)act, (struct sigaction *)oldact);
}

void internal_sigfillset(__sanitizer_sigset_t *set) { sigfillset(set); }

uptr internal_sigprocmask(int how, __sanitizer_sigset_t *set,
                          __sanitizer_sigset_t *oldset) {
  // Don't use sigprocmask here, because it affects all threads.
  return pthread_sigmask(how, set, oldset);
}

// Doesn't call pthread_atfork() handlers (but not available on 10.6).
extern "C" pid_t __fork(void) SANITIZER_WEAK_ATTRIBUTE;

int internal_fork() {
  if (&__fork)
    return __fork();
  return fork();
}

int internal_sysctl(const int *name, unsigned int namelen, void *oldp,
                    uptr *oldlenp, const void *newp, uptr newlen) {
  return sysctl(const_cast<int *>(name), namelen, oldp, (size_t *)oldlenp,
                const_cast<void *>(newp), (size_t)newlen);
}

int internal_sysctlbyname(const char *sname, void *oldp, uptr *oldlenp,
                          const void *newp, uptr newlen) {
  return sysctlbyname(sname, oldp, (size_t *)oldlenp, const_cast<void *>(newp),
                      (size_t)newlen);
}

static fd_t internal_spawn_impl(const char *argv[], const char *envp[],
                                pid_t *pid) {
  fd_t primary_fd = kInvalidFd;
  fd_t secondary_fd = kInvalidFd;

  auto fd_closer = at_scope_exit([&] {
    internal_close(primary_fd);
    internal_close(secondary_fd);
  });

  // We need a new pseudoterminal to avoid buffering problems. The 'atos' tool
  // in particular detects when it's talking to a pipe and forgets to flush the
  // output stream after sending a response.
  primary_fd = posix_openpt(O_RDWR);
  if (primary_fd == kInvalidFd)
    return kInvalidFd;

  int res = grantpt(primary_fd) || unlockpt(primary_fd);
  if (res != 0) return kInvalidFd;

  // Use TIOCPTYGNAME instead of ptsname() to avoid threading problems.
  char secondary_pty_name[128];
  res = ioctl(primary_fd, TIOCPTYGNAME, secondary_pty_name);
  if (res == -1) return kInvalidFd;

  secondary_fd = internal_open(secondary_pty_name, O_RDWR);
  if (secondary_fd == kInvalidFd)
    return kInvalidFd;

  // File descriptor actions
  posix_spawn_file_actions_t acts;
  res = posix_spawn_file_actions_init(&acts);
  if (res != 0) return kInvalidFd;

  auto acts_cleanup = at_scope_exit([&] {
    posix_spawn_file_actions_destroy(&acts);
  });

  res = posix_spawn_file_actions_adddup2(&acts, secondary_fd, STDIN_FILENO) ||
        posix_spawn_file_actions_adddup2(&acts, secondary_fd, STDOUT_FILENO) ||
        posix_spawn_file_actions_addclose(&acts, secondary_fd);
  if (res != 0) return kInvalidFd;

  // Spawn attributes
  posix_spawnattr_t attrs;
  res = posix_spawnattr_init(&attrs);
  if (res != 0) return kInvalidFd;

  auto attrs_cleanup  = at_scope_exit([&] {
    posix_spawnattr_destroy(&attrs);
  });

  // In the spawned process, close all file descriptors that are not explicitly
  // described by the file actions object. This is Darwin-specific extension.
  res = posix_spawnattr_setflags(&attrs, POSIX_SPAWN_CLOEXEC_DEFAULT);
  if (res != 0) return kInvalidFd;

  // posix_spawn
  char **argv_casted = const_cast<char **>(argv);
  char **envp_casted = const_cast<char **>(envp);
  res = posix_spawn(pid, argv[0], &acts, &attrs, argv_casted, envp_casted);
  if (res != 0) return kInvalidFd;

  // Disable echo in the new terminal, disable CR.
  struct termios termflags;
  tcgetattr(primary_fd, &termflags);
  termflags.c_oflag &= ~ONLCR;
  termflags.c_lflag &= ~ECHO;
  tcsetattr(primary_fd, TCSANOW, &termflags);

  // On success, do not close primary_fd on scope exit.
  fd_t fd = primary_fd;
  primary_fd = kInvalidFd;

  return fd;
}

fd_t internal_spawn(const char *argv[], const char *envp[], pid_t *pid) {
  // The client program may close its stdin and/or stdout and/or stderr thus
  // allowing open/posix_openpt to reuse file descriptors 0, 1 or 2. In this
  // case the communication is broken if either the parent or the child tries to
  // close or duplicate these descriptors. We temporarily reserve these
  // descriptors here to prevent this.
  fd_t low_fds[3];
  size_t count = 0;

  for (; count < 3; count++) {
    low_fds[count] = posix_openpt(O_RDWR);
    if (low_fds[count] >= STDERR_FILENO)
      break;
  }

  fd_t fd = internal_spawn_impl(argv, envp, pid);

  for (; count > 0; count--) {
    internal_close(low_fds[count]);
  }

  return fd;
}

uptr internal_rename(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
}

uptr internal_ftruncate(fd_t fd, uptr size) {
  return ftruncate(fd, size);
}

uptr internal_execve(const char *filename, char *const argv[],
                     char *const envp[]) {
  return execve(filename, argv, envp);
}

uptr internal_waitpid(int pid, int *status, int options) {
  return waitpid(pid, status, options);
}

// ----------------- sanitizer_common.h
bool FileExists(const char *filename) {
  if (ShouldMockFailureToOpen(filename))
    return false;
  struct stat st;
  if (stat(filename, &st))
    return false;
  // Sanity check: filename is a regular file.
  return S_ISREG(st.st_mode);
}

bool DirExists(const char *path) {
  struct stat st;
  if (stat(path, &st))
    return false;
  return S_ISDIR(st.st_mode);
}

tid_t GetTid() {
  tid_t tid;
  pthread_threadid_np(nullptr, &tid);
  return tid;
}

void GetThreadStackTopAndBottom(bool at_initialization, uptr *stack_top,
                                uptr *stack_bottom) {
  CHECK(stack_top);
  CHECK(stack_bottom);
  uptr stacksize = pthread_get_stacksize_np(pthread_self());
  // pthread_get_stacksize_np() returns an incorrect stack size for the main
  // thread on Mavericks. See
  // https://github.com/google/sanitizers/issues/261
  if ((GetMacosAlignedVersion() >= MacosVersion(10, 9)) && at_initialization &&
      stacksize == (1 << 19))  {
    struct rlimit rl;
    CHECK_EQ(getrlimit(RLIMIT_STACK, &rl), 0);
    // Most often rl.rlim_cur will be the desired 8M.
    if (rl.rlim_cur < kMaxThreadStackSize) {
      stacksize = rl.rlim_cur;
    } else {
      stacksize = kMaxThreadStackSize;
    }
  }
  void *stackaddr = pthread_get_stackaddr_np(pthread_self());
  *stack_top = (uptr)stackaddr;
  *stack_bottom = *stack_top - stacksize;
}

char **GetEnviron() {
#if !SANITIZER_IOS
  char ***env_ptr = _NSGetEnviron();
  if (!env_ptr) {
    Report("_NSGetEnviron() returned NULL. Please make sure __asan_init() is "
           "called after libSystem_initializer().\n");
    CHECK(env_ptr);
  }
  char **environ = *env_ptr;
#endif
  CHECK(environ);
  return environ;
}

const char *GetEnv(const char *name) {
  char **env = GetEnviron();
  uptr name_len = internal_strlen(name);
  while (*env != 0) {
    uptr len = internal_strlen(*env);
    if (len > name_len) {
      const char *p = *env;
      if (!internal_memcmp(p, name, name_len) &&
          p[name_len] == '=') {  // Match.
        return *env + name_len + 1;  // String starting after =.
      }
    }
    env++;
  }
  return 0;
}

uptr ReadBinaryName(/*out*/char *buf, uptr buf_len) {
  CHECK_LE(kMaxPathLength, buf_len);

  // On OS X the executable path is saved to the stack by dyld. Reading it
  // from there is much faster than calling dladdr, especially for large
  // binaries with symbols.
  InternalMmapVector<char> exe_path(kMaxPathLength);
  uint32_t size = exe_path.size();
  if (_NSGetExecutablePath(exe_path.data(), &size) == 0 &&
      realpath(exe_path.data(), buf) != 0) {
    return internal_strlen(buf);
  }
  return 0;
}

uptr ReadLongProcessName(/*out*/char *buf, uptr buf_len) {
  return ReadBinaryName(buf, buf_len);
}

void ReExec() {
  UNIMPLEMENTED();
}

void CheckASLR() {
  // Do nothing
}

void CheckMPROTECT() {
  // Do nothing
}

uptr GetPageSize() {
  return sysconf(_SC_PAGESIZE);
}

extern "C" unsigned malloc_num_zones;
extern "C" malloc_zone_t **malloc_zones;
malloc_zone_t sanitizer_zone;

// We need to make sure that sanitizer_zone is registered as malloc_zones[0]. If
// libmalloc tries to set up a different zone as malloc_zones[0], it will call
// mprotect(malloc_zones, ..., PROT_READ).  This interceptor will catch that and
// make sure we are still the first (default) zone.
void MprotectMallocZones(void *addr, int prot) {
  if (addr == malloc_zones && prot == PROT_READ) {
    if (malloc_num_zones > 1 && malloc_zones[0] != &sanitizer_zone) {
      for (unsigned i = 1; i < malloc_num_zones; i++) {
        if (malloc_zones[i] == &sanitizer_zone) {
          // Swap malloc_zones[0] and malloc_zones[i].
          malloc_zones[i] = malloc_zones[0];
          malloc_zones[0] = &sanitizer_zone;
          break;
        }
      }
    }
  }
}

void FutexWait(atomic_uint32_t *p, u32 cmp) {
  // FIXME: implement actual blocking.
  sched_yield();
}

void FutexWake(atomic_uint32_t *p, u32 count) {}

u64 NanoTime() {
  timeval tv;
  internal_memset(&tv, 0, sizeof(tv));
  gettimeofday(&tv, 0);
  return (u64)tv.tv_sec * 1000*1000*1000 + tv.tv_usec * 1000;
}

// This needs to be called during initialization to avoid being racy.
u64 MonotonicNanoTime() {
  static mach_timebase_info_data_t timebase_info;
  if (timebase_info.denom == 0) mach_timebase_info(&timebase_info);
  return (mach_absolute_time() * timebase_info.numer) / timebase_info.denom;
}

uptr GetTlsSize() {
  return 0;
}

void InitTlsSize() {
}

uptr TlsBaseAddr() {
  uptr segbase = 0;
#if defined(__x86_64__)
  asm("movq %%gs:0,%0" : "=r"(segbase));
#elif defined(__i386__)
  asm("movl %%gs:0,%0" : "=r"(segbase));
#elif defined(__aarch64__)
  asm("mrs %x0, tpidrro_el0" : "=r"(segbase));
  segbase &= 0x07ul;  // clearing lower bits, cpu id stored there
#endif
  return segbase;
}

// The size of the tls on darwin does not appear to be well documented,
// however the vm memory map suggests that it is 1024 uptrs in size,
// with a size of 0x2000 bytes on x86_64 and 0x1000 bytes on i386.
uptr TlsSize() {
#if defined(__x86_64__) || defined(__i386__)
  return 1024 * sizeof(uptr);
#else
  return 0;
#endif
}

void GetThreadStackAndTls(bool main, uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
#if !SANITIZER_GO
  uptr stack_top, stack_bottom;
  GetThreadStackTopAndBottom(main, &stack_top, &stack_bottom);
  *stk_addr = stack_bottom;
  *stk_size = stack_top - stack_bottom;
  *tls_addr = TlsBaseAddr();
  *tls_size = TlsSize();
#else
  *stk_addr = 0;
  *stk_size = 0;
  *tls_addr = 0;
  *tls_size = 0;
#endif
}

void ListOfModules::init() {
  clearOrInit();
  MemoryMappingLayout memory_mapping(false);
  memory_mapping.DumpListOfModules(&modules_);
}

void ListOfModules::fallbackInit() { clear(); }

static HandleSignalMode GetHandleSignalModeImpl(int signum) {
  switch (signum) {
    case SIGABRT:
      return common_flags()->handle_abort;
    case SIGILL:
      return common_flags()->handle_sigill;
    case SIGTRAP:
      return common_flags()->handle_sigtrap;
    case SIGFPE:
      return common_flags()->handle_sigfpe;
    case SIGSEGV:
      return common_flags()->handle_segv;
    case SIGBUS:
      return common_flags()->handle_sigbus;
  }
  return kHandleSignalNo;
}

HandleSignalMode GetHandleSignalMode(int signum) {
  // Handling fatal signals on watchOS and tvOS devices is disallowed.
  if ((SANITIZER_WATCHOS || SANITIZER_TVOS) && !(SANITIZER_IOSSIM))
    return kHandleSignalNo;
  HandleSignalMode result = GetHandleSignalModeImpl(signum);
  if (result == kHandleSignalYes && !common_flags()->allow_user_segv_handler)
    return kHandleSignalExclusive;
  return result;
}

// Offset example:
// XNU 17 -- macOS 10.13 -- iOS 11 -- tvOS 11 -- watchOS 4
constexpr u16 GetOSMajorKernelOffset() {
  if (TARGET_OS_OSX) return 4;
  if (TARGET_OS_IOS || TARGET_OS_TV) return 6;
  if (TARGET_OS_WATCH) return 13;
}

using VersStr = char[64];

static uptr ApproximateOSVersionViaKernelVersion(VersStr vers) {
  u16 kernel_major = GetDarwinKernelVersion().major;
  u16 offset = GetOSMajorKernelOffset();
  CHECK_GE(kernel_major, offset);
  u16 os_major = kernel_major - offset;

  const char *format = "%d.0";
  if (TARGET_OS_OSX) {
    if (os_major >= 16) {  // macOS 11+
      os_major -= 5;
    } else {  // macOS 10.15 and below
      format = "10.%d";
    }
  }
  return internal_snprintf(vers, sizeof(VersStr), format, os_major);
}

static void GetOSVersion(VersStr vers) {
  uptr len = sizeof(VersStr);
  if (SANITIZER_IOSSIM) {
    const char *vers_env = GetEnv("SIMULATOR_RUNTIME_VERSION");
    if (!vers_env) {
      Report("ERROR: Running in simulator but SIMULATOR_RUNTIME_VERSION env "
          "var is not set.\n");
      Die();
    }
    len = internal_strlcpy(vers, vers_env, len);
  } else {
    int res =
        internal_sysctlbyname("kern.osproductversion", vers, &len, nullptr, 0);

    // XNU 17 (macOS 10.13) and below do not provide the sysctl
    // `kern.osproductversion` entry (res != 0).
    bool no_os_version = res != 0;

    // For launchd, sanitizer initialization runs before sysctl is setup
    // (res == 0 && len != strlen(vers), vers is not a valid version).  However,
    // the kernel version `kern.osrelease` is available.
    bool launchd = (res == 0 && internal_strlen(vers) < 3);
    if (launchd) CHECK_EQ(internal_getpid(), 1);

    if (no_os_version || launchd) {
      len = ApproximateOSVersionViaKernelVersion(vers);
    }
  }
  CHECK_LT(len, sizeof(VersStr));
}

void ParseVersion(const char *vers, u16 *major, u16 *minor) {
  // Format: <major>.<minor>[.<patch>]\0
  CHECK_GE(internal_strlen(vers), 3);
  const char *p = vers;
  *major = internal_simple_strtoll(p, &p, /*base=*/10);
  CHECK_EQ(*p, '.');
  p += 1;
  *minor = internal_simple_strtoll(p, &p, /*base=*/10);
}

// Aligned versions example:
// macOS 10.15 -- iOS 13 -- tvOS 13 -- watchOS 6
static void MapToMacos(u16 *major, u16 *minor) {
  if (TARGET_OS_OSX)
    return;

  if (TARGET_OS_IOS || TARGET_OS_TV)
    *major += 2;
  else if (TARGET_OS_WATCH)
    *major += 9;
  else
    UNREACHABLE("unsupported platform");

  if (*major >= 16) {  // macOS 11+
    *major -= 5;
  } else {  // macOS 10.15 and below
    *minor = *major;
    *major = 10;
  }
}

static MacosVersion GetMacosAlignedVersionInternal() {
  VersStr vers = {};
  GetOSVersion(vers);

  u16 major, minor;
  ParseVersion(vers, &major, &minor);
  MapToMacos(&major, &minor);

  return MacosVersion(major, minor);
}

static_assert(sizeof(MacosVersion) == sizeof(atomic_uint32_t::Type),
              "MacosVersion cache size");
static atomic_uint32_t cached_macos_version;

MacosVersion GetMacosAlignedVersion() {
  atomic_uint32_t::Type result =
      atomic_load(&cached_macos_version, memory_order_acquire);
  if (!result) {
    MacosVersion version = GetMacosAlignedVersionInternal();
    result = *reinterpret_cast<atomic_uint32_t::Type *>(&version);
    atomic_store(&cached_macos_version, result, memory_order_release);
  }
  return *reinterpret_cast<MacosVersion *>(&result);
}

DarwinKernelVersion GetDarwinKernelVersion() {
  VersStr vers = {};
  uptr len = sizeof(VersStr);
  int res = internal_sysctlbyname("kern.osrelease", vers, &len, nullptr, 0);
  CHECK_EQ(res, 0);
  CHECK_LT(len, sizeof(VersStr));

  u16 major, minor;
  ParseVersion(vers, &major, &minor);

  return DarwinKernelVersion(major, minor);
}

uptr GetRSS() {
  struct task_basic_info info;
  unsigned count = TASK_BASIC_INFO_COUNT;
  kern_return_t result =
      task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count);
  if (UNLIKELY(result != KERN_SUCCESS)) {
    Report("Cannot get task info. Error: %d\n", result);
    Die();
  }
  return info.resident_size;
}

void *internal_start_thread(void *(*func)(void *arg), void *arg) {
  // Start the thread with signals blocked, otherwise it can steal user signals.
  __sanitizer_sigset_t set, old;
  internal_sigfillset(&set);
  internal_sigprocmask(SIG_SETMASK, &set, &old);
  pthread_t th;
  pthread_create(&th, 0, func, arg);
  internal_sigprocmask(SIG_SETMASK, &old, 0);
  return th;
}

void internal_join_thread(void *th) { pthread_join((pthread_t)th, 0); }

#if !SANITIZER_GO
static Mutex syslog_lock;
#  endif

void WriteOneLineToSyslog(const char *s) {
#if !SANITIZER_GO
  syslog_lock.CheckLocked();
  if (GetMacosAlignedVersion() >= MacosVersion(10, 12)) {
    os_log_error(OS_LOG_DEFAULT, "%{public}s", s);
  } else {
    asl_log(nullptr, nullptr, ASL_LEVEL_ERR, "%s", s);
  }
#endif
}

// buffer to store crash report application information
static char crashreporter_info_buff[__sanitizer::kErrorMessageBufferSize] = {};
static Mutex crashreporter_info_mutex;

extern "C" {
// Integrate with crash reporter libraries.
#if HAVE_CRASHREPORTERCLIENT_H
CRASH_REPORTER_CLIENT_HIDDEN
struct crashreporter_annotations_t gCRAnnotations
    __attribute__((section("__DATA," CRASHREPORTER_ANNOTATIONS_SECTION))) = {
        CRASHREPORTER_ANNOTATIONS_VERSION,
        0,
        0,
        0,
        0,
        0,
        0,
#if CRASHREPORTER_ANNOTATIONS_VERSION > 4
        0,
#endif
};

#else
// fall back to old crashreporter api
static const char *__crashreporter_info__ __attribute__((__used__)) =
    &crashreporter_info_buff[0];
asm(".desc ___crashreporter_info__, 0x10");
#endif

}  // extern "C"

static void CRAppendCrashLogMessage(const char *msg) {
  Lock l(&crashreporter_info_mutex);
  internal_strlcat(crashreporter_info_buff, msg,
                   sizeof(crashreporter_info_buff));
#if HAVE_CRASHREPORTERCLIENT_H
  (void)CRSetCrashLogMessage(crashreporter_info_buff);
#endif
}

void LogMessageOnPrintf(const char *str) {
  // Log all printf output to CrashLog.
  if (common_flags()->abort_on_error)
    CRAppendCrashLogMessage(str);
}

void LogFullErrorReport(const char *buffer) {
#if !SANITIZER_GO
  // Log with os_trace. This will make it into the crash log.
#if SANITIZER_OS_TRACE
  if (GetMacosAlignedVersion() >= MacosVersion(10, 10)) {
    // os_trace requires the message (format parameter) to be a string literal.
    if (internal_strncmp(SanitizerToolName, "AddressSanitizer",
                         sizeof("AddressSanitizer") - 1) == 0)
      os_trace("Address Sanitizer reported a failure.");
    else if (internal_strncmp(SanitizerToolName, "UndefinedBehaviorSanitizer",
                              sizeof("UndefinedBehaviorSanitizer") - 1) == 0)
      os_trace("Undefined Behavior Sanitizer reported a failure.");
    else if (internal_strncmp(SanitizerToolName, "ThreadSanitizer",
                              sizeof("ThreadSanitizer") - 1) == 0)
      os_trace("Thread Sanitizer reported a failure.");
    else
      os_trace("Sanitizer tool reported a failure.");

    if (common_flags()->log_to_syslog)
      os_trace("Consult syslog for more information.");
  }
#endif

  // Log to syslog.
  // The logging on OS X may call pthread_create so we need the threading
  // environment to be fully initialized. Also, this should never be called when
  // holding the thread registry lock since that may result in a deadlock. If
  // the reporting thread holds the thread registry mutex, and asl_log waits
  // for GCD to dispatch a new thread, the process will deadlock, because the
  // pthread_create wrapper needs to acquire the lock as well.
  Lock l(&syslog_lock);
  if (common_flags()->log_to_syslog)
    WriteToSyslog(buffer);

  // The report is added to CrashLog as part of logging all of Printf output.
#endif
}

SignalContext::WriteFlag SignalContext::GetWriteFlag() const {
#if defined(__x86_64__) || defined(__i386__)
  ucontext_t *ucontext = static_cast<ucontext_t*>(context);
  return ucontext->uc_mcontext->__es.__err & 2 /*T_PF_WRITE*/ ? Write : Read;
#elif defined(__arm64__)
  ucontext_t *ucontext = static_cast<ucontext_t*>(context);
  return ucontext->uc_mcontext->__es.__esr & 0x40 /*ISS_DA_WNR*/ ? Write : Read;
#else
  return Unknown;
#endif
}

bool SignalContext::IsTrueFaultingAddress() const {
  auto si = static_cast<const siginfo_t *>(siginfo);
  // "Real" SIGSEGV codes (e.g., SEGV_MAPERR, SEGV_MAPERR) are non-zero.
  return si->si_signo == SIGSEGV && si->si_code != 0;
}

#if defined(__aarch64__) && defined(arm_thread_state64_get_sp)
  #define AARCH64_GET_REG(r) \
    (uptr)ptrauth_strip(     \
        (void *)arm_thread_state64_get_##r(ucontext->uc_mcontext->__ss), 0)
#else
  #define AARCH64_GET_REG(r) (uptr)ucontext->uc_mcontext->__ss.__##r
#endif

static void GetPcSpBp(void *context, uptr *pc, uptr *sp, uptr *bp) {
  ucontext_t *ucontext = (ucontext_t*)context;
# if defined(__aarch64__)
  *pc = AARCH64_GET_REG(pc);
  *bp = AARCH64_GET_REG(fp);
  *sp = AARCH64_GET_REG(sp);
# elif defined(__x86_64__)
  *pc = ucontext->uc_mcontext->__ss.__rip;
  *bp = ucontext->uc_mcontext->__ss.__rbp;
  *sp = ucontext->uc_mcontext->__ss.__rsp;
# elif defined(__arm__)
  *pc = ucontext->uc_mcontext->__ss.__pc;
  *bp = ucontext->uc_mcontext->__ss.__r[7];
  *sp = ucontext->uc_mcontext->__ss.__sp;
# elif defined(__i386__)
  *pc = ucontext->uc_mcontext->__ss.__eip;
  *bp = ucontext->uc_mcontext->__ss.__ebp;
  *sp = ucontext->uc_mcontext->__ss.__esp;
# else
# error "Unknown architecture"
# endif
}

void SignalContext::InitPcSpBp() {
  addr = (uptr)ptrauth_strip((void *)addr, 0);
  GetPcSpBp(context, &pc, &sp, &bp);
}

// ASan/TSan use mmap in a way that creates “deallocation gaps” which triggers
// EXC_GUARD exceptions on macOS 10.15+ (XNU 19.0+).
static void DisableMmapExcGuardExceptions() {
  using task_exc_guard_behavior_t = uint32_t;
  using task_set_exc_guard_behavior_t =
      kern_return_t(task_t task, task_exc_guard_behavior_t behavior);
  auto *set_behavior = (task_set_exc_guard_behavior_t *)dlsym(
      RTLD_DEFAULT, "task_set_exc_guard_behavior");
  if (set_behavior == nullptr) return;
  const task_exc_guard_behavior_t task_exc_guard_none = 0;
  set_behavior(mach_task_self(), task_exc_guard_none);
}

static void VerifyInterceptorsWorking();
static void StripEnv();

void InitializePlatformEarly() {
  // Only use xnu_fast_mmap when on x86_64 and the kernel supports it.
  use_xnu_fast_mmap =
#if defined(__x86_64__)
      GetDarwinKernelVersion() >= DarwinKernelVersion(17, 5);
#else
      false;
#endif
  if (GetDarwinKernelVersion() >= DarwinKernelVersion(19, 0))
    DisableMmapExcGuardExceptions();

#  if !SANITIZER_GO
  MonotonicNanoTime();  // Call to initialize mach_timebase_info
  VerifyInterceptorsWorking();
  StripEnv();
#  endif
}

#if !SANITIZER_GO
static const char kDyldInsertLibraries[] = "DYLD_INSERT_LIBRARIES";
LowLevelAllocator allocator_for_env;

static bool ShouldCheckInterceptors() {
  // Restrict "interceptors working?" check to ASan and TSan.
  const char *sanitizer_names[] = {"AddressSanitizer", "ThreadSanitizer"};
  size_t count = sizeof(sanitizer_names) / sizeof(sanitizer_names[0]);
  for (size_t i = 0; i < count; i++) {
    if (internal_strcmp(sanitizer_names[i], SanitizerToolName) == 0)
      return true;
  }
  return false;
}

static void VerifyInterceptorsWorking() {
  if (!common_flags()->verify_interceptors || !ShouldCheckInterceptors())
    return;

  // Verify that interceptors really work.  We'll use dlsym to locate
  // "puts", if interceptors are working, it should really point to
  // "wrap_puts" within our own dylib.
  Dl_info info_puts, info_runtime;
  RAW_CHECK(dladdr(dlsym(RTLD_DEFAULT, "puts"), &info_puts));
  RAW_CHECK(dladdr((void *)&VerifyInterceptorsWorking, &info_runtime));
  if (internal_strcmp(info_puts.dli_fname, info_runtime.dli_fname) != 0) {
    Report(
        "ERROR: Interceptors are not working. This may be because %s is "
        "loaded too late (e.g. via dlopen). Please launch the executable "
        "with:\n%s=%s\n",
        SanitizerToolName, kDyldInsertLibraries, info_runtime.dli_fname);
    RAW_CHECK("interceptors not installed" && 0);
  }
}

// Change the value of the env var |name|, leaking the original value.
// If |name_value| is NULL, the variable is deleted from the environment,
// otherwise the corresponding "NAME=value" string is replaced with
// |name_value|.
static void LeakyResetEnv(const char *name, const char *name_value) {
  char **env = GetEnviron();
  uptr name_len = internal_strlen(name);
  while (*env != 0) {
    uptr len = internal_strlen(*env);
    if (len > name_len) {
      const char *p = *env;
      if (!internal_memcmp(p, name, name_len) && p[name_len] == '=') {
        // Match.
        if (name_value) {
          // Replace the old value with the new one.
          *env = const_cast<char*>(name_value);
        } else {
          // Shift the subsequent pointers back.
          char **del = env;
          do {
            del[0] = del[1];
          } while (*del++);
        }
      }
    }
    env++;
  }
}

static void StripEnv() {
  if (!common_flags()->strip_env)
    return;

  char *dyld_insert_libraries =
      const_cast<char *>(GetEnv(kDyldInsertLibraries));
  if (!dyld_insert_libraries)
    return;

  Dl_info info;
  RAW_CHECK(dladdr((void *)&StripEnv, &info));
  const char *dylib_name = StripModuleName(info.dli_fname);
  bool lib_is_in_env = internal_strstr(dyld_insert_libraries, dylib_name);
  if (!lib_is_in_env)
    return;

  // DYLD_INSERT_LIBRARIES is set and contains the runtime library. Let's remove
  // the dylib from the environment variable, because interceptors are installed
  // and we don't want our children to inherit the variable.

  uptr old_env_len = internal_strlen(dyld_insert_libraries);
  uptr dylib_name_len = internal_strlen(dylib_name);
  uptr env_name_len = internal_strlen(kDyldInsertLibraries);
  // Allocate memory to hold the previous env var name, its value, the '='
  // sign and the '\0' char.
  char *new_env = (char*)allocator_for_env.Allocate(
      old_env_len + 2 + env_name_len);
  RAW_CHECK(new_env);
  internal_memset(new_env, '\0', old_env_len + 2 + env_name_len);
  internal_strncpy(new_env, kDyldInsertLibraries, env_name_len);
  new_env[env_name_len] = '=';
  char *new_env_pos = new_env + env_name_len + 1;

  // Iterate over colon-separated pieces of |dyld_insert_libraries|.
  char *piece_start = dyld_insert_libraries;
  char *piece_end = NULL;
  char *old_env_end = dyld_insert_libraries + old_env_len;
  do {
    if (piece_start[0] == ':') piece_start++;
    piece_end = internal_strchr(piece_start, ':');
    if (!piece_end) piece_end = dyld_insert_libraries + old_env_len;
    if ((uptr)(piece_start - dyld_insert_libraries) > old_env_len) break;
    uptr piece_len = piece_end - piece_start;

    char *filename_start =
        (char *)internal_memrchr(piece_start, '/', piece_len);
    uptr filename_len = piece_len;
    if (filename_start) {
      filename_start += 1;
      filename_len = piece_len - (filename_start - piece_start);
    } else {
      filename_start = piece_start;
    }

    // If the current piece isn't the runtime library name,
    // append it to new_env.
    if ((dylib_name_len != filename_len) ||
        (internal_memcmp(filename_start, dylib_name, dylib_name_len) != 0)) {
      if (new_env_pos != new_env + env_name_len + 1) {
        new_env_pos[0] = ':';
        new_env_pos++;
      }
      internal_strncpy(new_env_pos, piece_start, piece_len);
      new_env_pos += piece_len;
    }
    // Move on to the next piece.
    piece_start = piece_end;
  } while (piece_start < old_env_end);

  // Can't use setenv() here, because it requires the allocator to be
  // initialized.
  // FIXME: instead of filtering DYLD_INSERT_LIBRARIES here, do it in
  // a separate function called after InitializeAllocator().
  if (new_env_pos == new_env + env_name_len + 1) new_env = NULL;
  LeakyResetEnv(kDyldInsertLibraries, new_env);
}
#endif  // SANITIZER_GO

char **GetArgv() {
  return *_NSGetArgv();
}

#if SANITIZER_IOS && !SANITIZER_IOSSIM
// The task_vm_info struct is normally provided by the macOS SDK, but we need
// fields only available in 10.12+. Declare the struct manually to be able to
// build against older SDKs.
struct __sanitizer_task_vm_info {
  mach_vm_size_t virtual_size;
  integer_t region_count;
  integer_t page_size;
  mach_vm_size_t resident_size;
  mach_vm_size_t resident_size_peak;
  mach_vm_size_t device;
  mach_vm_size_t device_peak;
  mach_vm_size_t internal;
  mach_vm_size_t internal_peak;
  mach_vm_size_t external;
  mach_vm_size_t external_peak;
  mach_vm_size_t reusable;
  mach_vm_size_t reusable_peak;
  mach_vm_size_t purgeable_volatile_pmap;
  mach_vm_size_t purgeable_volatile_resident;
  mach_vm_size_t purgeable_volatile_virtual;
  mach_vm_size_t compressed;
  mach_vm_size_t compressed_peak;
  mach_vm_size_t compressed_lifetime;
  mach_vm_size_t phys_footprint;
  mach_vm_address_t min_address;
  mach_vm_address_t max_address;
};
#define __SANITIZER_TASK_VM_INFO_COUNT ((mach_msg_type_number_t) \
    (sizeof(__sanitizer_task_vm_info) / sizeof(natural_t)))

static uptr GetTaskInfoMaxAddress() {
  __sanitizer_task_vm_info vm_info = {} /* zero initialize */;
  mach_msg_type_number_t count = __SANITIZER_TASK_VM_INFO_COUNT;
  int err = task_info(mach_task_self(), TASK_VM_INFO, (int *)&vm_info, &count);
  return err ? 0 : vm_info.max_address;
}

uptr GetMaxUserVirtualAddress() {
  static uptr max_vm = GetTaskInfoMaxAddress();
  if (max_vm != 0) {
    const uptr ret_value = max_vm - 1;
    CHECK_LE(ret_value, SANITIZER_MMAP_RANGE_SIZE);
    return ret_value;
  }

  // xnu cannot provide vm address limit
# if SANITIZER_WORDSIZE == 32
  constexpr uptr fallback_max_vm = 0xffe00000 - 1;
# else
  constexpr uptr fallback_max_vm = 0x200000000 - 1;
# endif
  static_assert(fallback_max_vm <= SANITIZER_MMAP_RANGE_SIZE,
                "Max virtual address must be less than mmap range size.");
  return fallback_max_vm;
}

#else // !SANITIZER_IOS

uptr GetMaxUserVirtualAddress() {
# if SANITIZER_WORDSIZE == 64
  constexpr uptr max_vm = (1ULL << 47) - 1;  // 0x00007fffffffffffUL;
# else // SANITIZER_WORDSIZE == 32
  static_assert(SANITIZER_WORDSIZE == 32, "Wrong wordsize");
  constexpr uptr max_vm = (1ULL << 32) - 1;  // 0xffffffff;
# endif
  static_assert(max_vm <= SANITIZER_MMAP_RANGE_SIZE,
                "Max virtual address must be less than mmap range size.");
  return max_vm;
}
#endif

uptr GetMaxVirtualAddress() {
  return GetMaxUserVirtualAddress();
}

uptr MapDynamicShadow(uptr shadow_size_bytes, uptr shadow_scale,
                      uptr min_shadow_base_alignment, uptr &high_mem_end,
                      uptr granularity) {
  const uptr alignment =
      Max<uptr>(granularity << shadow_scale, 1ULL << min_shadow_base_alignment);
  const uptr left_padding =
      Max<uptr>(granularity, 1ULL << min_shadow_base_alignment);

  uptr space_size = shadow_size_bytes + left_padding;

  uptr largest_gap_found = 0;
  uptr max_occupied_addr = 0;
  VReport(2, "FindDynamicShadowStart, space_size = %p\n", (void *)space_size);
  uptr shadow_start =
      FindAvailableMemoryRange(space_size, alignment, granularity,
                               &largest_gap_found, &max_occupied_addr);
  // If the shadow doesn't fit, restrict the address space to make it fit.
  if (shadow_start == 0) {
    VReport(
        2,
        "Shadow doesn't fit, largest_gap_found = %p, max_occupied_addr = %p\n",
        (void *)largest_gap_found, (void *)max_occupied_addr);
    uptr new_max_vm = RoundDownTo(largest_gap_found << shadow_scale, alignment);
    if (new_max_vm < max_occupied_addr) {
      Report("Unable to find a memory range for dynamic shadow.\n");
      Report(
          "space_size = %p, largest_gap_found = %p, max_occupied_addr = %p, "
          "new_max_vm = %p\n",
          (void *)space_size, (void *)largest_gap_found,
          (void *)max_occupied_addr, (void *)new_max_vm);
      CHECK(0 && "cannot place shadow");
    }
    RestrictMemoryToMaxAddress(new_max_vm);
    high_mem_end = new_max_vm - 1;
    space_size = (high_mem_end >> shadow_scale) + left_padding;
    VReport(2, "FindDynamicShadowStart, space_size = %p\n", (void *)space_size);
    shadow_start = FindAvailableMemoryRange(space_size, alignment, granularity,
                                            nullptr, nullptr);
    if (shadow_start == 0) {
      Report("Unable to find a memory range after restricting VM.\n");
      CHECK(0 && "cannot place shadow after restricting vm");
    }
  }
  CHECK_NE((uptr)0, shadow_start);
  CHECK(IsAligned(shadow_start, alignment));
  return shadow_start;
}

uptr MapDynamicShadowAndAliases(uptr shadow_size, uptr alias_size,
                                uptr num_aliases, uptr ring_buffer_size) {
  CHECK(false && "HWASan aliasing is unimplemented on Mac");
  return 0;
}

uptr FindAvailableMemoryRange(uptr size, uptr alignment, uptr left_padding,
                              uptr *largest_gap_found,
                              uptr *max_occupied_addr) {
  typedef vm_region_submap_short_info_data_64_t RegionInfo;
  enum { kRegionInfoSize = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64 };
  // Start searching for available memory region past PAGEZERO, which is
  // 4KB on 32-bit and 4GB on 64-bit.
  mach_vm_address_t start_address =
    (SANITIZER_WORDSIZE == 32) ? 0x000000001000 : 0x000100000000;

  const mach_vm_address_t max_vm_address = GetMaxVirtualAddress() + 1;
  mach_vm_address_t address = start_address;
  mach_vm_address_t free_begin = start_address;
  kern_return_t kr = KERN_SUCCESS;
  if (largest_gap_found) *largest_gap_found = 0;
  if (max_occupied_addr) *max_occupied_addr = 0;
  while (kr == KERN_SUCCESS) {
    mach_vm_size_t vmsize = 0;
    natural_t depth = 0;
    RegionInfo vminfo;
    mach_msg_type_number_t count = kRegionInfoSize;
    kr = mach_vm_region_recurse(mach_task_self(), &address, &vmsize, &depth,
                                (vm_region_info_t)&vminfo, &count);
    if (kr == KERN_INVALID_ADDRESS) {
      // No more regions beyond "address", consider the gap at the end of VM.
      address = max_vm_address;
      vmsize = 0;
    } else {
      if (max_occupied_addr) *max_occupied_addr = address + vmsize;
    }
    if (free_begin != address) {
      // We found a free region [free_begin..address-1].
      uptr gap_start = RoundUpTo((uptr)free_begin + left_padding, alignment);
      uptr gap_end = RoundDownTo((uptr)Min(address, max_vm_address), alignment);
      uptr gap_size = gap_end > gap_start ? gap_end - gap_start : 0;
      if (size < gap_size) {
        return gap_start;
      }

      if (largest_gap_found && *largest_gap_found < gap_size) {
        *largest_gap_found = gap_size;
      }
    }
    // Move to the next region.
    address += vmsize;
    free_begin = address;
  }

  // We looked at all free regions and could not find one large enough.
  return 0;
}

// FIXME implement on this platform.
void GetMemoryProfile(fill_profile_f cb, uptr *stats) {}

void SignalContext::DumpAllRegisters(void *context) {
  Report("Register values:\n");

  ucontext_t *ucontext = (ucontext_t*)context;
# define DUMPREG64(r) \
    Printf("%s = 0x%016llx  ", #r, ucontext->uc_mcontext->__ss.__ ## r);
# define DUMPREGA64(r) \
    Printf("   %s = 0x%016lx  ", #r, AARCH64_GET_REG(r));
# define DUMPREG32(r) \
    Printf("%s = 0x%08x  ", #r, ucontext->uc_mcontext->__ss.__ ## r);
# define DUMPREG_(r)   Printf(" "); DUMPREG(r);
# define DUMPREG__(r)  Printf("  "); DUMPREG(r);
# define DUMPREG___(r) Printf("   "); DUMPREG(r);

# if defined(__x86_64__)
#  define DUMPREG(r) DUMPREG64(r)
  DUMPREG(rax); DUMPREG(rbx); DUMPREG(rcx); DUMPREG(rdx); Printf("\n");
  DUMPREG(rdi); DUMPREG(rsi); DUMPREG(rbp); DUMPREG(rsp); Printf("\n");
  DUMPREG_(r8); DUMPREG_(r9); DUMPREG(r10); DUMPREG(r11); Printf("\n");
  DUMPREG(r12); DUMPREG(r13); DUMPREG(r14); DUMPREG(r15); Printf("\n");
# elif defined(__i386__)
#  define DUMPREG(r) DUMPREG32(r)
  DUMPREG(eax); DUMPREG(ebx); DUMPREG(ecx); DUMPREG(edx); Printf("\n");
  DUMPREG(edi); DUMPREG(esi); DUMPREG(ebp); DUMPREG(esp); Printf("\n");
# elif defined(__aarch64__)
#  define DUMPREG(r) DUMPREG64(r)
  DUMPREG_(x[0]); DUMPREG_(x[1]); DUMPREG_(x[2]); DUMPREG_(x[3]); Printf("\n");
  DUMPREG_(x[4]); DUMPREG_(x[5]); DUMPREG_(x[6]); DUMPREG_(x[7]); Printf("\n");
  DUMPREG_(x[8]); DUMPREG_(x[9]); DUMPREG(x[10]); DUMPREG(x[11]); Printf("\n");
  DUMPREG(x[12]); DUMPREG(x[13]); DUMPREG(x[14]); DUMPREG(x[15]); Printf("\n");
  DUMPREG(x[16]); DUMPREG(x[17]); DUMPREG(x[18]); DUMPREG(x[19]); Printf("\n");
  DUMPREG(x[20]); DUMPREG(x[21]); DUMPREG(x[22]); DUMPREG(x[23]); Printf("\n");
  DUMPREG(x[24]); DUMPREG(x[25]); DUMPREG(x[26]); DUMPREG(x[27]); Printf("\n");
  DUMPREG(x[28]); DUMPREGA64(fp); DUMPREGA64(lr); DUMPREGA64(sp); Printf("\n");
# elif defined(__arm__)
#  define DUMPREG(r) DUMPREG32(r)
  DUMPREG_(r[0]); DUMPREG_(r[1]); DUMPREG_(r[2]); DUMPREG_(r[3]); Printf("\n");
  DUMPREG_(r[4]); DUMPREG_(r[5]); DUMPREG_(r[6]); DUMPREG_(r[7]); Printf("\n");
  DUMPREG_(r[8]); DUMPREG_(r[9]); DUMPREG(r[10]); DUMPREG(r[11]); Printf("\n");
  DUMPREG(r[12]); DUMPREG___(sp); DUMPREG___(lr); DUMPREG___(pc); Printf("\n");
# else
# error "Unknown architecture"
# endif

# undef DUMPREG64
# undef DUMPREG32
# undef DUMPREG_
# undef DUMPREG__
# undef DUMPREG___
# undef DUMPREG
}

static inline bool CompareBaseAddress(const LoadedModule &a,
                                      const LoadedModule &b) {
  return a.base_address() < b.base_address();
}

void FormatUUID(char *out, uptr size, const u8 *uuid) {
  internal_snprintf(out, size,
                    "<%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
                    "%02X%02X%02X%02X%02X%02X>",
                    uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
                    uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
                    uuid[12], uuid[13], uuid[14], uuid[15]);
}

void DumpProcessMap() {
  Printf("Process module map:\n");
  MemoryMappingLayout memory_mapping(false);
  InternalMmapVector<LoadedModule> modules;
  modules.reserve(128);
  memory_mapping.DumpListOfModules(&modules);
  Sort(modules.data(), modules.size(), CompareBaseAddress);
  for (uptr i = 0; i < modules.size(); ++i) {
    char uuid_str[128];
    FormatUUID(uuid_str, sizeof(uuid_str), modules[i].uuid());
    Printf("%p-%p %s (%s) %s\n", (void *)modules[i].base_address(),
           (void *)modules[i].max_address(), modules[i].full_name(),
           ModuleArchToString(modules[i].arch()), uuid_str);
  }
  Printf("End of module map.\n");
}

void CheckNoDeepBind(const char *filename, int flag) {
  // Do nothing.
}

bool GetRandom(void *buffer, uptr length, bool blocking) {
  if (!buffer || !length || length > 256)
    return false;
  // arc4random never fails.
  REAL(arc4random_buf)(buffer, length);
  return true;
}

u32 GetNumberOfCPUs() {
  return (u32)sysconf(_SC_NPROCESSORS_ONLN);
}

void InitializePlatformCommonFlags(CommonFlags *cf) {}

// Pthread introspection hook
//
// * GCD worker threads are created without a call to pthread_create(), but we
//   still need to register these threads (with ThreadCreate/Start()).
// * We use the "pthread introspection hook" below to observe the creation of
//   such threads.
// * GCD worker threads don't have parent threads and the CREATE event is
//   delivered in the context of the thread itself.  CREATE events for regular
//   threads, are delivered on the parent.  We use this to tell apart which
//   threads are GCD workers with `thread == pthread_self()`.
//
static pthread_introspection_hook_t prev_pthread_introspection_hook;
static ThreadEventCallbacks thread_event_callbacks;

static void sanitizer_pthread_introspection_hook(unsigned int event,
                                                 pthread_t thread, void *addr,
                                                 size_t size) {
  // create -> start -> terminate -> destroy
  // * create/destroy are usually (not guaranteed) delivered on the parent and
  //   track resource allocation/reclamation
  // * start/terminate are guaranteed to be delivered in the context of the
  //   thread and give hooks into "just after (before) thread starts (stops)
  //   executing"
  DCHECK(event >= PTHREAD_INTROSPECTION_THREAD_CREATE &&
         event <= PTHREAD_INTROSPECTION_THREAD_DESTROY);

  if (event == PTHREAD_INTROSPECTION_THREAD_CREATE) {
    bool gcd_worker = (thread == pthread_self());
    if (thread_event_callbacks.create)
      thread_event_callbacks.create((uptr)thread, gcd_worker);
  } else if (event == PTHREAD_INTROSPECTION_THREAD_START) {
    CHECK_EQ(thread, pthread_self());
    if (thread_event_callbacks.start)
      thread_event_callbacks.start((uptr)thread);
  }

  if (prev_pthread_introspection_hook)
    prev_pthread_introspection_hook(event, thread, addr, size);

  if (event == PTHREAD_INTROSPECTION_THREAD_TERMINATE) {
    CHECK_EQ(thread, pthread_self());
    if (thread_event_callbacks.terminate)
      thread_event_callbacks.terminate((uptr)thread);
  } else if (event == PTHREAD_INTROSPECTION_THREAD_DESTROY) {
    if (thread_event_callbacks.destroy)
      thread_event_callbacks.destroy((uptr)thread);
  }
}

void InstallPthreadIntrospectionHook(const ThreadEventCallbacks &callbacks) {
  thread_event_callbacks = callbacks;
  prev_pthread_introspection_hook =
      pthread_introspection_hook_install(&sanitizer_pthread_introspection_hook);
}

}  // namespace __sanitizer

#endif  // SANITIZER_APPLE
