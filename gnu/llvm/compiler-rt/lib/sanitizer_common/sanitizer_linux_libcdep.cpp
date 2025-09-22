//===-- sanitizer_linux_libcdep.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements linux-specific functions from
// sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"

#if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
    SANITIZER_SOLARIS

#  include "sanitizer_allocator_internal.h"
#  include "sanitizer_atomic.h"
#  include "sanitizer_common.h"
#  include "sanitizer_file.h"
#  include "sanitizer_flags.h"
#  include "sanitizer_getauxval.h"
#  include "sanitizer_glibc_version.h"
#  include "sanitizer_linux.h"
#  include "sanitizer_placement_new.h"
#  include "sanitizer_procmaps.h"
#  include "sanitizer_solaris.h"

#  if SANITIZER_NETBSD
#    define _RTLD_SOURCE  // for __lwp_gettcb_fast() / __lwp_getprivate_fast()
#  endif

#  include <dlfcn.h>  // for dlsym()
#  include <link.h>
#  include <pthread.h>
#  include <signal.h>
#  include <sys/mman.h>
#  include <sys/resource.h>
#  include <syslog.h>

#  if !defined(ElfW)
#    define ElfW(type) Elf_##type
#  endif

#  if SANITIZER_FREEBSD
#    include <pthread_np.h>
#    include <sys/auxv.h>
#    include <sys/sysctl.h>
#    define pthread_getattr_np pthread_attr_get_np
// The MAP_NORESERVE define has been removed in FreeBSD 11.x, and even before
// that, it was never implemented. So just define it to zero.
#    undef MAP_NORESERVE
#    define MAP_NORESERVE 0
extern const Elf_Auxinfo *__elf_aux_vector;
extern "C" int __sys_sigaction(int signum, const struct sigaction *act,
                               struct sigaction *oldact);
#  endif

#  if SANITIZER_NETBSD
#    include <lwp.h>
#    include <sys/sysctl.h>
#    include <sys/tls.h>
#  endif

#  if SANITIZER_SOLARIS
#    include <stddef.h>
#    include <stdlib.h>
#    include <thread.h>
#  endif

#  if SANITIZER_ANDROID
#    include <android/api-level.h>
#    if !defined(CPU_COUNT) && !defined(__aarch64__)
#      include <dirent.h>
#      include <fcntl.h>
struct __sanitizer::linux_dirent {
  long d_ino;
  off_t d_off;
  unsigned short d_reclen;
  char d_name[];
};
#    endif
#  endif

#  if !SANITIZER_ANDROID
#    include <elf.h>
#    include <unistd.h>
#  endif

namespace __sanitizer {

SANITIZER_WEAK_ATTRIBUTE int real_sigaction(int signum, const void *act,
                                            void *oldact);

int internal_sigaction(int signum, const void *act, void *oldact) {
#  if SANITIZER_FREEBSD
  // On FreeBSD, call the sigaction syscall directly (part of libsys in FreeBSD
  // 15) since the libc version goes via a global interposing table. Due to
  // library initialization order the table can be relocated after the call to
  // InitializeDeadlySignals() which then crashes when dereferencing the
  // uninitialized pointer in libc.
  return __sys_sigaction(signum, (const struct sigaction *)act,
                         (struct sigaction *)oldact);
#  else
#    if !SANITIZER_GO
  if (&real_sigaction)
    return real_sigaction(signum, act, oldact);
#    endif
  return sigaction(signum, (const struct sigaction *)act,
                   (struct sigaction *)oldact);
#  endif
}

void GetThreadStackTopAndBottom(bool at_initialization, uptr *stack_top,
                                uptr *stack_bottom) {
  CHECK(stack_top);
  CHECK(stack_bottom);
  if (at_initialization) {
    // This is the main thread. Libpthread may not be initialized yet.
    struct rlimit rl;
    CHECK_EQ(getrlimit(RLIMIT_STACK, &rl), 0);

    // Find the mapping that contains a stack variable.
    MemoryMappingLayout proc_maps(/*cache_enabled*/ true);
    if (proc_maps.Error()) {
      *stack_top = *stack_bottom = 0;
      return;
    }
    MemoryMappedSegment segment;
    uptr prev_end = 0;
    while (proc_maps.Next(&segment)) {
      if ((uptr)&rl < segment.end)
        break;
      prev_end = segment.end;
    }
    CHECK((uptr)&rl >= segment.start && (uptr)&rl < segment.end);

    // Get stacksize from rlimit, but clip it so that it does not overlap
    // with other mappings.
    uptr stacksize = rl.rlim_cur;
    if (stacksize > segment.end - prev_end)
      stacksize = segment.end - prev_end;
    // When running with unlimited stack size, we still want to set some limit.
    // The unlimited stack size is caused by 'ulimit -s unlimited'.
    // Also, for some reason, GNU make spawns subprocesses with unlimited stack.
    if (stacksize > kMaxThreadStackSize)
      stacksize = kMaxThreadStackSize;
    *stack_top = segment.end;
    *stack_bottom = segment.end - stacksize;

    uptr maxAddr = GetMaxUserVirtualAddress();
    // Edge case: the stack mapping on some systems may be off-by-one e.g.,
    //     fffffffdf000-1000000000000 rw-p 00000000 00:00 0 [stack]
    // instead of:
    //     fffffffdf000- ffffffffffff
    // The out-of-range stack_top can result in an invalid shadow address
    // calculation, since those usually assume the parameters are in range.
    if (*stack_top == maxAddr + 1)
      *stack_top = maxAddr;
    else
      CHECK_LE(*stack_top, maxAddr);

    return;
  }
  uptr stacksize = 0;
  void *stackaddr = nullptr;
#  if SANITIZER_SOLARIS
  stack_t ss;
  CHECK_EQ(thr_stksegment(&ss), 0);
  stacksize = ss.ss_size;
  stackaddr = (char *)ss.ss_sp - stacksize;
#  else   // !SANITIZER_SOLARIS
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  CHECK_EQ(pthread_getattr_np(pthread_self(), &attr), 0);
  internal_pthread_attr_getstack(&attr, &stackaddr, &stacksize);
  pthread_attr_destroy(&attr);
#  endif  // SANITIZER_SOLARIS

  *stack_top = (uptr)stackaddr + stacksize;
  *stack_bottom = (uptr)stackaddr;
}

#  if !SANITIZER_GO
bool SetEnv(const char *name, const char *value) {
  void *f = dlsym(RTLD_NEXT, "setenv");
  if (!f)
    return false;
  typedef int (*setenv_ft)(const char *name, const char *value, int overwrite);
  setenv_ft setenv_f;
  CHECK_EQ(sizeof(setenv_f), sizeof(f));
  internal_memcpy(&setenv_f, &f, sizeof(f));
  return setenv_f(name, value, 1) == 0;
}
#  endif

__attribute__((unused)) static bool GetLibcVersion(int *major, int *minor,
                                                   int *patch) {
#  ifdef _CS_GNU_LIBC_VERSION
  char buf[64];
  uptr len = confstr(_CS_GNU_LIBC_VERSION, buf, sizeof(buf));
  if (len >= sizeof(buf))
    return false;
  buf[len] = 0;
  static const char kGLibC[] = "glibc ";
  if (internal_strncmp(buf, kGLibC, sizeof(kGLibC) - 1) != 0)
    return false;
  const char *p = buf + sizeof(kGLibC) - 1;
  *major = internal_simple_strtoll(p, &p, 10);
  *minor = (*p == '.') ? internal_simple_strtoll(p + 1, &p, 10) : 0;
  *patch = (*p == '.') ? internal_simple_strtoll(p + 1, &p, 10) : 0;
  return true;
#  else
  return false;
#  endif
}

// True if we can use dlpi_tls_data. glibc before 2.25 may leave NULL (BZ
// #19826) so dlpi_tls_data cannot be used.
//
// musl before 1.2.3 and FreeBSD as of 12.2 incorrectly set dlpi_tls_data to
// the TLS initialization image
// https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=254774
__attribute__((unused)) static int g_use_dlpi_tls_data;

#  if SANITIZER_GLIBC && !SANITIZER_GO
__attribute__((unused)) static size_t g_tls_size;
void InitTlsSize() {
  int major, minor, patch;
  g_use_dlpi_tls_data =
      GetLibcVersion(&major, &minor, &patch) && major == 2 && minor >= 25;

#    if defined(__aarch64__) || defined(__x86_64__) || \
        defined(__powerpc64__) || defined(__loongarch__)
  void *get_tls_static_info = dlsym(RTLD_NEXT, "_dl_get_tls_static_info");
  size_t tls_align;
  ((void (*)(size_t *, size_t *))get_tls_static_info)(&g_tls_size, &tls_align);
#    endif
}
#  else
void InitTlsSize() {}
#  endif  // SANITIZER_GLIBC && !SANITIZER_GO

// On glibc x86_64, ThreadDescriptorSize() needs to be precise due to the usage
// of g_tls_size. On other targets, ThreadDescriptorSize() is only used by lsan
// to get the pointer to thread-specific data keys in the thread control block.
#  if (SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_SOLARIS) && \
      !SANITIZER_ANDROID && !SANITIZER_GO
// sizeof(struct pthread) from glibc.
static atomic_uintptr_t thread_descriptor_size;

static uptr ThreadDescriptorSizeFallback() {
  uptr val = 0;
#    if defined(__x86_64__) || defined(__i386__) || defined(__arm__)
  int major;
  int minor;
  int patch;
  if (GetLibcVersion(&major, &minor, &patch) && major == 2) {
    /* sizeof(struct pthread) values from various glibc versions.  */
    if (SANITIZER_X32)
      val = 1728;  // Assume only one particular version for x32.
    // For ARM sizeof(struct pthread) changed in Glibc 2.23.
    else if (SANITIZER_ARM)
      val = minor <= 22 ? 1120 : 1216;
    else if (minor <= 3)
      val = FIRST_32_SECOND_64(1104, 1696);
    else if (minor == 4)
      val = FIRST_32_SECOND_64(1120, 1728);
    else if (minor == 5)
      val = FIRST_32_SECOND_64(1136, 1728);
    else if (minor <= 9)
      val = FIRST_32_SECOND_64(1136, 1712);
    else if (minor == 10)
      val = FIRST_32_SECOND_64(1168, 1776);
    else if (minor == 11 || (minor == 12 && patch == 1))
      val = FIRST_32_SECOND_64(1168, 2288);
    else if (minor <= 14)
      val = FIRST_32_SECOND_64(1168, 2304);
    else if (minor < 32)  // Unknown version
      val = FIRST_32_SECOND_64(1216, 2304);
    else  // minor == 32
      val = FIRST_32_SECOND_64(1344, 2496);
  }
#    elif defined(__s390__) || defined(__sparc__)
  // The size of a prefix of TCB including pthread::{specific_1stblock,specific}
  // suffices. Just return offsetof(struct pthread, specific_used), which hasn't
  // changed since 2007-05. Technically this applies to i386/x86_64 as well but
  // we call _dl_get_tls_static_info and need the precise size of struct
  // pthread.
  return FIRST_32_SECOND_64(524, 1552);
#    elif defined(__mips__)
  // TODO(sagarthakur): add more values as per different glibc versions.
  val = FIRST_32_SECOND_64(1152, 1776);
#    elif SANITIZER_LOONGARCH64
  val = 1856;  // from glibc 2.36
#    elif SANITIZER_RISCV64
  int major;
  int minor;
  int patch;
  if (GetLibcVersion(&major, &minor, &patch) && major == 2) {
    // TODO: consider adding an optional runtime check for an unknown (untested)
    // glibc version
    if (minor <= 28)  // WARNING: the highest tested version is 2.29
      val = 1772;     // no guarantees for this one
    else if (minor <= 31)
      val = 1772;  // tested against glibc 2.29, 2.31
    else
      val = 1936;  // tested against glibc 2.32
  }

#    elif defined(__aarch64__)
  // The sizeof (struct pthread) is the same from GLIBC 2.17 to 2.22.
  val = 1776;
#    elif defined(__powerpc64__)
  val = 1776;  // from glibc.ppc64le 2.20-8.fc21
#    endif
  return val;
}

uptr ThreadDescriptorSize() {
  uptr val = atomic_load_relaxed(&thread_descriptor_size);
  if (val)
    return val;
  // _thread_db_sizeof_pthread is a GLIBC_PRIVATE symbol that is exported in
  // glibc 2.34 and later.
  if (unsigned *psizeof = static_cast<unsigned *>(
          dlsym(RTLD_DEFAULT, "_thread_db_sizeof_pthread")))
    val = *psizeof;
  if (!val)
    val = ThreadDescriptorSizeFallback();
  atomic_store_relaxed(&thread_descriptor_size, val);
  return val;
}

#    if defined(__mips__) || defined(__powerpc64__) || SANITIZER_RISCV64 || \
        SANITIZER_LOONGARCH64
// TlsPreTcbSize includes size of struct pthread_descr and size of tcb
// head structure. It lies before the static tls blocks.
static uptr TlsPreTcbSize() {
#      if defined(__mips__)
  const uptr kTcbHead = 16;  // sizeof (tcbhead_t)
#      elif defined(__powerpc64__)
  const uptr kTcbHead = 88;  // sizeof (tcbhead_t)
#      elif SANITIZER_RISCV64
  const uptr kTcbHead = 16;  // sizeof (tcbhead_t)
#      elif SANITIZER_LOONGARCH64
  const uptr kTcbHead = 16;  // sizeof (tcbhead_t)
#      endif
  const uptr kTlsAlign = 16;
  const uptr kTlsPreTcbSize =
      RoundUpTo(ThreadDescriptorSize() + kTcbHead, kTlsAlign);
  return kTlsPreTcbSize;
}
#    endif

namespace {
struct TlsBlock {
  uptr begin, end, align;
  size_t tls_modid;
  bool operator<(const TlsBlock &rhs) const { return begin < rhs.begin; }
};
}  // namespace

#    ifdef __s390__
extern "C" uptr __tls_get_offset(void *arg);

static uptr TlsGetOffset(uptr ti_module, uptr ti_offset) {
  // The __tls_get_offset ABI requires %r12 to point to GOT and %r2 to be an
  // offset of a struct tls_index inside GOT. We don't possess either of the
  // two, so violate the letter of the "ELF Handling For Thread-Local
  // Storage" document and assume that the implementation just dereferences
  // %r2 + %r12.
  uptr tls_index[2] = {ti_module, ti_offset};
  register uptr r2 asm("2") = 0;
  register void *r12 asm("12") = tls_index;
  asm("basr %%r14, %[__tls_get_offset]"
      : "+r"(r2)
      : [__tls_get_offset] "r"(__tls_get_offset), "r"(r12)
      : "memory", "cc", "0", "1", "3", "4", "5", "14");
  return r2;
}
#    else
extern "C" void *__tls_get_addr(size_t *);
#    endif

static size_t main_tls_modid;

static int CollectStaticTlsBlocks(struct dl_phdr_info *info, size_t size,
                                  void *data) {
  size_t tls_modid;
#    if SANITIZER_SOLARIS
  // dlpi_tls_modid is only available since Solaris 11.4 SRU 10.  Use
  // dlinfo(RTLD_DI_LINKMAP) instead which works on all of Solaris 11.3,
  // 11.4, and Illumos.  The tlsmodid of the executable was changed to 1 in
  // 11.4 to match other implementations.
  if (size >= offsetof(dl_phdr_info_test, dlpi_tls_modid))
    main_tls_modid = 1;
  else
    main_tls_modid = 0;
  g_use_dlpi_tls_data = 0;
  Rt_map *map;
  dlinfo(RTLD_SELF, RTLD_DI_LINKMAP, &map);
  tls_modid = map->rt_tlsmodid;
#    else
  main_tls_modid = 1;
  tls_modid = info->dlpi_tls_modid;
#    endif

  if (tls_modid < main_tls_modid)
    return 0;
  uptr begin;
#    if !SANITIZER_SOLARIS
  begin = (uptr)info->dlpi_tls_data;
#    endif
  if (!g_use_dlpi_tls_data) {
    // Call __tls_get_addr as a fallback. This forces TLS allocation on glibc
    // and FreeBSD.
#    ifdef __s390__
    begin = (uptr)__builtin_thread_pointer() + TlsGetOffset(tls_modid, 0);
#    else
    size_t mod_and_off[2] = {tls_modid, 0};
    begin = (uptr)__tls_get_addr(mod_and_off);
#    endif
  }
  for (unsigned i = 0; i != info->dlpi_phnum; ++i)
    if (info->dlpi_phdr[i].p_type == PT_TLS) {
      static_cast<InternalMmapVector<TlsBlock> *>(data)->push_back(
          TlsBlock{begin, begin + info->dlpi_phdr[i].p_memsz,
                   info->dlpi_phdr[i].p_align, tls_modid});
      break;
    }
  return 0;
}

__attribute__((unused)) static void GetStaticTlsBoundary(uptr *addr, uptr *size,
                                                         uptr *align) {
  InternalMmapVector<TlsBlock> ranges;
  dl_iterate_phdr(CollectStaticTlsBlocks, &ranges);
  uptr len = ranges.size();
  Sort(ranges.begin(), len);
  // Find the range with tls_modid == main_tls_modid. For glibc, because
  // libc.so uses PT_TLS, this module is guaranteed to exist and is one of
  // the initially loaded modules.
  uptr one = 0;
  while (one != len && ranges[one].tls_modid != main_tls_modid) ++one;
  if (one == len) {
    // This may happen with musl if no module uses PT_TLS.
    *addr = 0;
    *size = 0;
    *align = 1;
    return;
  }
  // Find the maximum consecutive ranges. We consider two modules consecutive if
  // the gap is smaller than the alignment of the latter range. The dynamic
  // loader places static TLS blocks this way not to waste space.
  uptr l = one;
  *align = ranges[l].align;
  while (l != 0 && ranges[l].begin < ranges[l - 1].end + ranges[l].align)
    *align = Max(*align, ranges[--l].align);
  uptr r = one + 1;
  while (r != len && ranges[r].begin < ranges[r - 1].end + ranges[r].align)
    *align = Max(*align, ranges[r++].align);
  *addr = ranges[l].begin;
  *size = ranges[r - 1].end - ranges[l].begin;
}
#  endif  // (x86_64 || i386 || mips || ...) && (SANITIZER_FREEBSD ||
          // SANITIZER_LINUX) && !SANITIZER_ANDROID && !SANITIZER_GO

#  if SANITIZER_NETBSD
static struct tls_tcb *ThreadSelfTlsTcb() {
  struct tls_tcb *tcb = nullptr;
#    ifdef __HAVE___LWP_GETTCB_FAST
  tcb = (struct tls_tcb *)__lwp_gettcb_fast();
#    elif defined(__HAVE___LWP_GETPRIVATE_FAST)
  tcb = (struct tls_tcb *)__lwp_getprivate_fast();
#    endif
  return tcb;
}

uptr ThreadSelf() { return (uptr)ThreadSelfTlsTcb()->tcb_pthread; }

int GetSizeFromHdr(struct dl_phdr_info *info, size_t size, void *data) {
  const Elf_Phdr *hdr = info->dlpi_phdr;
  const Elf_Phdr *last_hdr = hdr + info->dlpi_phnum;

  for (; hdr != last_hdr; ++hdr) {
    if (hdr->p_type == PT_TLS && info->dlpi_tls_modid == 1) {
      *(uptr *)data = hdr->p_memsz;
      break;
    }
  }
  return 0;
}
#  endif  // SANITIZER_NETBSD

#  if SANITIZER_ANDROID
// Bionic provides this API since S.
extern "C" SANITIZER_WEAK_ATTRIBUTE void __libc_get_static_tls_bounds(void **,
                                                                      void **);
#  endif

#  if !SANITIZER_GO
static void GetTls(uptr *addr, uptr *size) {
#    if SANITIZER_ANDROID
  if (&__libc_get_static_tls_bounds) {
    void *start_addr;
    void *end_addr;
    __libc_get_static_tls_bounds(&start_addr, &end_addr);
    *addr = reinterpret_cast<uptr>(start_addr);
    *size =
        reinterpret_cast<uptr>(end_addr) - reinterpret_cast<uptr>(start_addr);
  } else {
    *addr = 0;
    *size = 0;
  }
#    elif SANITIZER_GLIBC && defined(__x86_64__)
  // For aarch64 and x86-64, use an O(1) approach which requires relatively
  // precise ThreadDescriptorSize. g_tls_size was initialized in InitTlsSize.
#      if SANITIZER_X32
  asm("mov %%fs:8,%0" : "=r"(*addr));
#      else
  asm("mov %%fs:16,%0" : "=r"(*addr));
#      endif
  *size = g_tls_size;
  *addr -= *size;
  *addr += ThreadDescriptorSize();
#    elif SANITIZER_GLIBC && defined(__aarch64__)
  *addr = reinterpret_cast<uptr>(__builtin_thread_pointer()) -
          ThreadDescriptorSize();
  *size = g_tls_size + ThreadDescriptorSize();
#    elif SANITIZER_GLIBC && defined(__loongarch__)
#      ifdef __clang__
  *addr = reinterpret_cast<uptr>(__builtin_thread_pointer()) -
          ThreadDescriptorSize();
#      else
  asm("or %0,$tp,$zero" : "=r"(*addr));
  *addr -= ThreadDescriptorSize();
#      endif
  *size = g_tls_size + ThreadDescriptorSize();
#    elif SANITIZER_GLIBC && defined(__powerpc64__)
  // Workaround for glibc<2.25(?). 2.27 is known to not need this.
  uptr tp;
  asm("addi %0,13,-0x7000" : "=r"(tp));
  const uptr pre_tcb_size = TlsPreTcbSize();
  *addr = tp - pre_tcb_size;
  *size = g_tls_size + pre_tcb_size;
#    elif SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_SOLARIS
  uptr align;
  GetStaticTlsBoundary(addr, size, &align);
#      if defined(__x86_64__) || defined(__i386__) || defined(__s390__) || \
          defined(__sparc__)
  if (SANITIZER_GLIBC) {
#        if defined(__x86_64__) || defined(__i386__)
    align = Max<uptr>(align, 64);
#        else
    align = Max<uptr>(align, 16);
#        endif
  }
  const uptr tp = RoundUpTo(*addr + *size, align);

  // lsan requires the range to additionally cover the static TLS surplus
  // (elf/dl-tls.c defines 1664). Otherwise there may be false positives for
  // allocations only referenced by tls in dynamically loaded modules.
  if (SANITIZER_GLIBC)
    *size += 1644;
  else if (SANITIZER_FREEBSD)
    *size += 128;  // RTLD_STATIC_TLS_EXTRA

  // Extend the range to include the thread control block. On glibc, lsan needs
  // the range to include pthread::{specific_1stblock,specific} so that
  // allocations only referenced by pthread_setspecific can be scanned. This may
  // underestimate by at most TLS_TCB_ALIGN-1 bytes but it should be fine
  // because the number of bytes after pthread::specific is larger.
  *addr = tp - RoundUpTo(*size, align);
  *size = tp - *addr + ThreadDescriptorSize();
#      else
  if (SANITIZER_GLIBC)
    *size += 1664;
  else if (SANITIZER_FREEBSD)
    *size += 128;  // RTLD_STATIC_TLS_EXTRA
#        if defined(__mips__) || defined(__powerpc64__) || SANITIZER_RISCV64
  const uptr pre_tcb_size = TlsPreTcbSize();
  *addr -= pre_tcb_size;
  *size += pre_tcb_size;
#        else
  // arm and aarch64 reserve two words at TP, so this underestimates the range.
  // However, this is sufficient for the purpose of finding the pointers to
  // thread-specific data keys.
  const uptr tcb_size = ThreadDescriptorSize();
  *addr -= tcb_size;
  *size += tcb_size;
#        endif
#      endif
#    elif SANITIZER_NETBSD
  struct tls_tcb *const tcb = ThreadSelfTlsTcb();
  *addr = 0;
  *size = 0;
  if (tcb != 0) {
    // Find size (p_memsz) of dlpi_tls_modid 1 (TLS block of the main program).
    // ld.elf_so hardcodes the index 1.
    dl_iterate_phdr(GetSizeFromHdr, size);

    if (*size != 0) {
      // The block has been found and tcb_dtv[1] contains the base address
      *addr = (uptr)tcb->tcb_dtv[1];
    }
  }
#    else
#      error "Unknown OS"
#    endif
}
#  endif

#  if !SANITIZER_GO
uptr GetTlsSize() {
#    if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
        SANITIZER_SOLARIS
  uptr addr, size;
  GetTls(&addr, &size);
  return size;
#    else
  return 0;
#    endif
}
#  endif

void GetThreadStackAndTls(bool main, uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
#  if SANITIZER_GO
  // Stub implementation for Go.
  *stk_addr = *stk_size = *tls_addr = *tls_size = 0;
#  else
  GetTls(tls_addr, tls_size);

  uptr stack_top, stack_bottom;
  GetThreadStackTopAndBottom(main, &stack_top, &stack_bottom);
  *stk_addr = stack_bottom;
  *stk_size = stack_top - stack_bottom;

  if (!main) {
    // If stack and tls intersect, make them non-intersecting.
    if (*tls_addr > *stk_addr && *tls_addr < *stk_addr + *stk_size) {
      if (*stk_addr + *stk_size < *tls_addr + *tls_size)
        *tls_size = *stk_addr + *stk_size - *tls_addr;
      *stk_size = *tls_addr - *stk_addr;
    }
  }
#  endif
}

#  if !SANITIZER_FREEBSD
typedef ElfW(Phdr) Elf_Phdr;
#  endif

struct DlIteratePhdrData {
  InternalMmapVectorNoCtor<LoadedModule> *modules;
  bool first;
};

static int AddModuleSegments(const char *module_name, dl_phdr_info *info,
                             InternalMmapVectorNoCtor<LoadedModule> *modules) {
  if (module_name[0] == '\0')
    return 0;
  LoadedModule cur_module;
  cur_module.set(module_name, info->dlpi_addr);
  for (int i = 0; i < (int)info->dlpi_phnum; i++) {
    const Elf_Phdr *phdr = &info->dlpi_phdr[i];
    if (phdr->p_type == PT_LOAD) {
      uptr cur_beg = info->dlpi_addr + phdr->p_vaddr;
      uptr cur_end = cur_beg + phdr->p_memsz;
      bool executable = phdr->p_flags & PF_X;
      bool writable = phdr->p_flags & PF_W;
      cur_module.addAddressRange(cur_beg, cur_end, executable, writable);
    } else if (phdr->p_type == PT_NOTE) {
#  ifdef NT_GNU_BUILD_ID
      uptr off = 0;
      while (off + sizeof(ElfW(Nhdr)) < phdr->p_memsz) {
        auto *nhdr = reinterpret_cast<const ElfW(Nhdr) *>(info->dlpi_addr +
                                                          phdr->p_vaddr + off);
        constexpr auto kGnuNamesz = 4;  // "GNU" with NUL-byte.
        static_assert(kGnuNamesz % 4 == 0, "kGnuNameSize is aligned to 4.");
        if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == kGnuNamesz) {
          if (off + sizeof(ElfW(Nhdr)) + nhdr->n_namesz + nhdr->n_descsz >
              phdr->p_memsz) {
            // Something is very wrong, bail out instead of reading potentially
            // arbitrary memory.
            break;
          }
          const char *name =
              reinterpret_cast<const char *>(nhdr) + sizeof(*nhdr);
          if (internal_memcmp(name, "GNU", 3) == 0) {
            const char *value = reinterpret_cast<const char *>(nhdr) +
                                sizeof(*nhdr) + kGnuNamesz;
            cur_module.setUuid(value, nhdr->n_descsz);
            break;
          }
        }
        off += sizeof(*nhdr) + RoundUpTo(nhdr->n_namesz, 4) +
               RoundUpTo(nhdr->n_descsz, 4);
      }
#  endif
    }
  }
  modules->push_back(cur_module);
  return 0;
}

static int dl_iterate_phdr_cb(dl_phdr_info *info, size_t size, void *arg) {
  DlIteratePhdrData *data = (DlIteratePhdrData *)arg;
  if (data->first) {
    InternalMmapVector<char> module_name(kMaxPathLength);
    data->first = false;
    // First module is the binary itself.
    ReadBinaryNameCached(module_name.data(), module_name.size());
    return AddModuleSegments(module_name.data(), info, data->modules);
  }

  if (info->dlpi_name)
    return AddModuleSegments(info->dlpi_name, info, data->modules);

  return 0;
}

#  if SANITIZER_ANDROID && __ANDROID_API__ < 21
extern "C" __attribute__((weak)) int dl_iterate_phdr(
    int (*)(struct dl_phdr_info *, size_t, void *), void *);
#  endif

static bool requiresProcmaps() {
#  if SANITIZER_ANDROID && __ANDROID_API__ <= 22
  // Fall back to /proc/maps if dl_iterate_phdr is unavailable or broken.
  // The runtime check allows the same library to work with
  // both K and L (and future) Android releases.
  return AndroidGetApiLevel() <= ANDROID_LOLLIPOP_MR1;
#  else
  return false;
#  endif
}

static void procmapsInit(InternalMmapVectorNoCtor<LoadedModule> *modules) {
  MemoryMappingLayout memory_mapping(/*cache_enabled*/ true);
  memory_mapping.DumpListOfModules(modules);
}

void ListOfModules::init() {
  clearOrInit();
  if (requiresProcmaps()) {
    procmapsInit(&modules_);
  } else {
    DlIteratePhdrData data = {&modules_, true};
    dl_iterate_phdr(dl_iterate_phdr_cb, &data);
  }
}

// When a custom loader is used, dl_iterate_phdr may not contain the full
// list of modules. Allow callers to fall back to using procmaps.
void ListOfModules::fallbackInit() {
  if (!requiresProcmaps()) {
    clearOrInit();
    procmapsInit(&modules_);
  } else {
    clear();
  }
}

// getrusage does not give us the current RSS, only the max RSS.
// Still, this is better than nothing if /proc/self/statm is not available
// for some reason, e.g. due to a sandbox.
static uptr GetRSSFromGetrusage() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))  // Failed, probably due to a sandbox.
    return 0;
  return usage.ru_maxrss << 10;  // ru_maxrss is in Kb.
}

uptr GetRSS() {
  if (!common_flags()->can_use_proc_maps_statm)
    return GetRSSFromGetrusage();
  fd_t fd = OpenFile("/proc/self/statm", RdOnly);
  if (fd == kInvalidFd)
    return GetRSSFromGetrusage();
  char buf[64];
  uptr len = internal_read(fd, buf, sizeof(buf) - 1);
  internal_close(fd);
  if ((sptr)len <= 0)
    return 0;
  buf[len] = 0;
  // The format of the file is:
  // 1084 89 69 11 0 79 0
  // We need the second number which is RSS in pages.
  char *pos = buf;
  // Skip the first number.
  while (*pos >= '0' && *pos <= '9') pos++;
  // Skip whitespaces.
  while (!(*pos >= '0' && *pos <= '9') && *pos != 0) pos++;
  // Read the number.
  uptr rss = 0;
  while (*pos >= '0' && *pos <= '9') rss = rss * 10 + *pos++ - '0';
  return rss * GetPageSizeCached();
}

// sysconf(_SC_NPROCESSORS_{CONF,ONLN}) cannot be used on most platforms as
// they allocate memory.
u32 GetNumberOfCPUs() {
#  if SANITIZER_FREEBSD || SANITIZER_NETBSD
  u32 ncpu;
  int req[2];
  uptr len = sizeof(ncpu);
  req[0] = CTL_HW;
  req[1] = HW_NCPU;
  CHECK_EQ(internal_sysctl(req, 2, &ncpu, &len, NULL, 0), 0);
  return ncpu;
#  elif SANITIZER_ANDROID && !defined(CPU_COUNT) && !defined(__aarch64__)
  // Fall back to /sys/devices/system/cpu on Android when cpu_set_t doesn't
  // exist in sched.h. That is the case for toolchains generated with older
  // NDKs.
  // This code doesn't work on AArch64 because internal_getdents makes use of
  // the 64bit getdents syscall, but cpu_set_t seems to always exist on AArch64.
  uptr fd = internal_open("/sys/devices/system/cpu", O_RDONLY | O_DIRECTORY);
  if (internal_iserror(fd))
    return 0;
  InternalMmapVector<u8> buffer(4096);
  uptr bytes_read = buffer.size();
  uptr n_cpus = 0;
  u8 *d_type;
  struct linux_dirent *entry = (struct linux_dirent *)&buffer[bytes_read];
  while (true) {
    if ((u8 *)entry >= &buffer[bytes_read]) {
      bytes_read = internal_getdents(fd, (struct linux_dirent *)buffer.data(),
                                     buffer.size());
      if (internal_iserror(bytes_read) || !bytes_read)
        break;
      entry = (struct linux_dirent *)buffer.data();
    }
    d_type = (u8 *)entry + entry->d_reclen - 1;
    if (d_type >= &buffer[bytes_read] ||
        (u8 *)&entry->d_name[3] >= &buffer[bytes_read])
      break;
    if (entry->d_ino != 0 && *d_type == DT_DIR) {
      if (entry->d_name[0] == 'c' && entry->d_name[1] == 'p' &&
          entry->d_name[2] == 'u' && entry->d_name[3] >= '0' &&
          entry->d_name[3] <= '9')
        n_cpus++;
    }
    entry = (struct linux_dirent *)(((u8 *)entry) + entry->d_reclen);
  }
  internal_close(fd);
  return n_cpus;
#  elif SANITIZER_SOLARIS
  return sysconf(_SC_NPROCESSORS_ONLN);
#  else
  cpu_set_t CPUs;
  CHECK_EQ(sched_getaffinity(0, sizeof(cpu_set_t), &CPUs), 0);
  return CPU_COUNT(&CPUs);
#  endif
}

#  if SANITIZER_LINUX

#    if SANITIZER_ANDROID
static atomic_uint8_t android_log_initialized;

void AndroidLogInit() {
  openlog(GetProcessName(), 0, LOG_USER);
  atomic_store(&android_log_initialized, 1, memory_order_release);
}

static bool ShouldLogAfterPrintf() {
  return atomic_load(&android_log_initialized, memory_order_acquire);
}

extern "C" SANITIZER_WEAK_ATTRIBUTE int async_safe_write_log(int pri,
                                                             const char *tag,
                                                             const char *msg);
extern "C" SANITIZER_WEAK_ATTRIBUTE int __android_log_write(int prio,
                                                            const char *tag,
                                                            const char *msg);

// ANDROID_LOG_INFO is 4, but can't be resolved at runtime.
#      define SANITIZER_ANDROID_LOG_INFO 4

// async_safe_write_log is a new public version of __libc_write_log that is
// used behind syslog. It is preferable to syslog as it will not do any dynamic
// memory allocation or formatting.
// If the function is not available, syslog is preferred for L+ (it was broken
// pre-L) as __android_log_write triggers a racey behavior with the strncpy
// interceptor. Fallback to __android_log_write pre-L.
void WriteOneLineToSyslog(const char *s) {
  if (&async_safe_write_log) {
    async_safe_write_log(SANITIZER_ANDROID_LOG_INFO, GetProcessName(), s);
  } else if (AndroidGetApiLevel() > ANDROID_KITKAT) {
    syslog(LOG_INFO, "%s", s);
  } else {
    CHECK(&__android_log_write);
    __android_log_write(SANITIZER_ANDROID_LOG_INFO, nullptr, s);
  }
}

extern "C" SANITIZER_WEAK_ATTRIBUTE void android_set_abort_message(
    const char *);

void SetAbortMessage(const char *str) {
  if (&android_set_abort_message)
    android_set_abort_message(str);
}
#    else
void AndroidLogInit() {}

static bool ShouldLogAfterPrintf() { return true; }

void WriteOneLineToSyslog(const char *s) { syslog(LOG_INFO, "%s", s); }

void SetAbortMessage(const char *str) {}
#    endif  // SANITIZER_ANDROID

void LogMessageOnPrintf(const char *str) {
  if (common_flags()->log_to_syslog && ShouldLogAfterPrintf())
    WriteToSyslog(str);
}

#  endif  // SANITIZER_LINUX

#  if SANITIZER_GLIBC && !SANITIZER_GO
// glibc crashes when using clock_gettime from a preinit_array function as the
// vDSO function pointers haven't been initialized yet. __progname is
// initialized after the vDSO function pointers, so if it exists, is not null
// and is not empty, we can use clock_gettime.
extern "C" SANITIZER_WEAK_ATTRIBUTE char *__progname;
inline bool CanUseVDSO() { return &__progname && __progname && *__progname; }

// MonotonicNanoTime is a timing function that can leverage the vDSO by calling
// clock_gettime. real_clock_gettime only exists if clock_gettime is
// intercepted, so define it weakly and use it if available.
extern "C" SANITIZER_WEAK_ATTRIBUTE int real_clock_gettime(u32 clk_id,
                                                           void *tp);
u64 MonotonicNanoTime() {
  timespec ts;
  if (CanUseVDSO()) {
    if (&real_clock_gettime)
      real_clock_gettime(CLOCK_MONOTONIC, &ts);
    else
      clock_gettime(CLOCK_MONOTONIC, &ts);
  } else {
    internal_clock_gettime(CLOCK_MONOTONIC, &ts);
  }
  return (u64)ts.tv_sec * (1000ULL * 1000 * 1000) + ts.tv_nsec;
}
#  else
// Non-glibc & Go always use the regular function.
u64 MonotonicNanoTime() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * (1000ULL * 1000 * 1000) + ts.tv_nsec;
}
#  endif  // SANITIZER_GLIBC && !SANITIZER_GO

void ReExec() {
  const char *pathname = "/proc/self/exe";

#  if SANITIZER_FREEBSD
  for (const auto *aux = __elf_aux_vector; aux->a_type != AT_NULL; aux++) {
    if (aux->a_type == AT_EXECPATH) {
      pathname = static_cast<const char *>(aux->a_un.a_ptr);
      break;
    }
  }
#  elif SANITIZER_NETBSD
  static const int name[] = {
      CTL_KERN,
      KERN_PROC_ARGS,
      -1,
      KERN_PROC_PATHNAME,
  };
  char path[400];
  uptr len;

  len = sizeof(path);
  if (internal_sysctl(name, ARRAY_SIZE(name), path, &len, NULL, 0) != -1)
    pathname = path;
#  elif SANITIZER_SOLARIS
  pathname = getexecname();
  CHECK_NE(pathname, NULL);
#  elif SANITIZER_USE_GETAUXVAL
  // Calling execve with /proc/self/exe sets that as $EXEC_ORIGIN. Binaries that
  // rely on that will fail to load shared libraries. Query AT_EXECFN instead.
  pathname = reinterpret_cast<const char *>(getauxval(AT_EXECFN));
#  endif

  uptr rv = internal_execve(pathname, GetArgv(), GetEnviron());
  int rverrno;
  CHECK_EQ(internal_iserror(rv, &rverrno), true);
  Printf("execve failed, errno %d\n", rverrno);
  Die();
}

void UnmapFromTo(uptr from, uptr to) {
  if (to == from)
    return;
  CHECK(to >= from);
  uptr res = internal_munmap(reinterpret_cast<void *>(from), to - from);
  if (UNLIKELY(internal_iserror(res))) {
    Report("ERROR: %s failed to unmap 0x%zx (%zd) bytes at address %p\n",
           SanitizerToolName, to - from, to - from, (void *)from);
    CHECK("unable to unmap" && 0);
  }
}

uptr MapDynamicShadow(uptr shadow_size_bytes, uptr shadow_scale,
                      uptr min_shadow_base_alignment, UNUSED uptr &high_mem_end,
                      uptr granularity) {
  const uptr alignment =
      Max<uptr>(granularity << shadow_scale, 1ULL << min_shadow_base_alignment);
  const uptr left_padding =
      Max<uptr>(granularity, 1ULL << min_shadow_base_alignment);

  const uptr shadow_size = RoundUpTo(shadow_size_bytes, granularity);
  const uptr map_size = shadow_size + left_padding + alignment;

  const uptr map_start = (uptr)MmapNoAccess(map_size);
  CHECK_NE(map_start, ~(uptr)0);

  const uptr shadow_start = RoundUpTo(map_start + left_padding, alignment);

  UnmapFromTo(map_start, shadow_start - left_padding);
  UnmapFromTo(shadow_start + shadow_size, map_start + map_size);

  return shadow_start;
}

static uptr MmapSharedNoReserve(uptr addr, uptr size) {
  return internal_mmap(
      reinterpret_cast<void *>(addr), size, PROT_READ | PROT_WRITE,
      MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
}

static uptr MremapCreateAlias(uptr base_addr, uptr alias_addr,
                              uptr alias_size) {
#  if SANITIZER_LINUX
  return internal_mremap(reinterpret_cast<void *>(base_addr), 0, alias_size,
                         MREMAP_MAYMOVE | MREMAP_FIXED,
                         reinterpret_cast<void *>(alias_addr));
#  else
  CHECK(false && "mremap is not supported outside of Linux");
  return 0;
#  endif
}

static void CreateAliases(uptr start_addr, uptr alias_size, uptr num_aliases) {
  uptr total_size = alias_size * num_aliases;
  uptr mapped = MmapSharedNoReserve(start_addr, total_size);
  CHECK_EQ(mapped, start_addr);

  for (uptr i = 1; i < num_aliases; ++i) {
    uptr alias_addr = start_addr + i * alias_size;
    CHECK_EQ(MremapCreateAlias(start_addr, alias_addr, alias_size), alias_addr);
  }
}

uptr MapDynamicShadowAndAliases(uptr shadow_size, uptr alias_size,
                                uptr num_aliases, uptr ring_buffer_size) {
  CHECK_EQ(alias_size & (alias_size - 1), 0);
  CHECK_EQ(num_aliases & (num_aliases - 1), 0);
  CHECK_EQ(ring_buffer_size & (ring_buffer_size - 1), 0);

  const uptr granularity = GetMmapGranularity();
  shadow_size = RoundUpTo(shadow_size, granularity);
  CHECK_EQ(shadow_size & (shadow_size - 1), 0);

  const uptr alias_region_size = alias_size * num_aliases;
  const uptr alignment =
      2 * Max(Max(shadow_size, alias_region_size), ring_buffer_size);
  const uptr left_padding = ring_buffer_size;

  const uptr right_size = alignment;
  const uptr map_size = left_padding + 2 * alignment;

  const uptr map_start = reinterpret_cast<uptr>(MmapNoAccess(map_size));
  CHECK_NE(map_start, static_cast<uptr>(-1));
  const uptr right_start = RoundUpTo(map_start + left_padding, alignment);

  UnmapFromTo(map_start, right_start - left_padding);
  UnmapFromTo(right_start + right_size, map_start + map_size);

  CreateAliases(right_start + right_size / 2, alias_size, num_aliases);

  return right_start;
}

void InitializePlatformCommonFlags(CommonFlags *cf) {
#  if SANITIZER_ANDROID
  if (&__libc_get_static_tls_bounds == nullptr)
    cf->detect_leaks = false;
#  endif
}

}  // namespace __sanitizer

#endif
