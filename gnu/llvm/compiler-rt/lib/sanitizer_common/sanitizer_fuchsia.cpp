//===-- sanitizer_fuchsia.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and other sanitizer
// run-time libraries and implements Fuchsia-specific functions from
// sanitizer_common.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_fuchsia.h"
#if SANITIZER_FUCHSIA

#  include <pthread.h>
#  include <stdlib.h>
#  include <unistd.h>
#  include <zircon/errors.h>
#  include <zircon/process.h>
#  include <zircon/syscalls.h>
#  include <zircon/utc.h>

#  include "sanitizer_common.h"
#  include "sanitizer_interface_internal.h"
#  include "sanitizer_libc.h"
#  include "sanitizer_mutex.h"

namespace __sanitizer {

void NORETURN internal__exit(int exitcode) { _zx_process_exit(exitcode); }

uptr internal_sched_yield() {
  zx_status_t status = _zx_thread_legacy_yield(0u);
  CHECK_EQ(status, ZX_OK);
  return 0;  // Why doesn't this return void?
}

void internal_usleep(u64 useconds) {
  zx_status_t status = _zx_nanosleep(_zx_deadline_after(ZX_USEC(useconds)));
  CHECK_EQ(status, ZX_OK);
}

u64 NanoTime() {
  zx_handle_t utc_clock = _zx_utc_reference_get();
  CHECK_NE(utc_clock, ZX_HANDLE_INVALID);
  zx_time_t time;
  zx_status_t status = _zx_clock_read(utc_clock, &time);
  CHECK_EQ(status, ZX_OK);
  return time;
}

u64 MonotonicNanoTime() { return _zx_clock_get_monotonic(); }

uptr internal_getpid() {
  zx_info_handle_basic_t info;
  zx_status_t status =
      _zx_object_get_info(_zx_process_self(), ZX_INFO_HANDLE_BASIC, &info,
                          sizeof(info), NULL, NULL);
  CHECK_EQ(status, ZX_OK);
  uptr pid = static_cast<uptr>(info.koid);
  CHECK_EQ(pid, info.koid);
  return pid;
}

int internal_dlinfo(void *handle, int request, void *p) { UNIMPLEMENTED(); }

uptr GetThreadSelf() { return reinterpret_cast<uptr>(thrd_current()); }

tid_t GetTid() { return GetThreadSelf(); }

void Abort() { abort(); }

int Atexit(void (*function)(void)) { return atexit(function); }

void GetThreadStackTopAndBottom(bool, uptr *stack_top, uptr *stack_bottom) {
  pthread_attr_t attr;
  CHECK_EQ(pthread_getattr_np(pthread_self(), &attr), 0);
  void *base;
  size_t size;
  CHECK_EQ(pthread_attr_getstack(&attr, &base, &size), 0);
  CHECK_EQ(pthread_attr_destroy(&attr), 0);

  *stack_bottom = reinterpret_cast<uptr>(base);
  *stack_top = *stack_bottom + size;
}

void InitializePlatformEarly() {}
void CheckASLR() {}
void CheckMPROTECT() {}
void PlatformPrepareForSandboxing(void *args) {}
void DisableCoreDumperIfNecessary() {}
void InstallDeadlySignalHandlers(SignalHandlerType handler) {}
void SetAlternateSignalStack() {}
void UnsetAlternateSignalStack() {}
void InitTlsSize() {}

bool SignalContext::IsStackOverflow() const { return false; }
void SignalContext::DumpAllRegisters(void *context) { UNIMPLEMENTED(); }
const char *SignalContext::Describe() const { UNIMPLEMENTED(); }

void FutexWait(atomic_uint32_t *p, u32 cmp) {
  zx_status_t status = _zx_futex_wait(reinterpret_cast<zx_futex_t *>(p), cmp,
                                      ZX_HANDLE_INVALID, ZX_TIME_INFINITE);
  if (status != ZX_ERR_BAD_STATE)  // Normal race.
    CHECK_EQ(status, ZX_OK);
}

void FutexWake(atomic_uint32_t *p, u32 count) {
  zx_status_t status = _zx_futex_wake(reinterpret_cast<zx_futex_t *>(p), count);
  CHECK_EQ(status, ZX_OK);
}

uptr GetPageSize() { return _zx_system_get_page_size(); }

uptr GetMmapGranularity() { return _zx_system_get_page_size(); }

sanitizer_shadow_bounds_t ShadowBounds;

void InitShadowBounds() { ShadowBounds = __sanitizer_shadow_bounds(); }

uptr GetMaxUserVirtualAddress() {
  InitShadowBounds();
  return ShadowBounds.memory_limit - 1;
}

uptr GetMaxVirtualAddress() { return GetMaxUserVirtualAddress(); }

bool ErrorIsOOM(error_t err) { return err == ZX_ERR_NO_MEMORY; }

// For any sanitizer internal that needs to map something which can be unmapped
// later, first attempt to map to a pre-allocated VMAR. This helps reduce
// fragmentation from many small anonymous mmap calls. A good value for this
// VMAR size would be the total size of your typical sanitizer internal objects
// allocated in an "average" process lifetime. Examples of this include:
// FakeStack, LowLevelAllocator mappings, TwoLevelMap, InternalMmapVector,
// StackStore, CreateAsanThread, etc.
//
// This is roughly equal to the total sum of sanitizer internal mappings for a
// large test case.
constexpr size_t kSanitizerHeapVmarSize = 13ULL << 20;
static zx_handle_t gSanitizerHeapVmar = ZX_HANDLE_INVALID;

static zx_status_t GetSanitizerHeapVmar(zx_handle_t *vmar) {
  zx_status_t status = ZX_OK;
  if (gSanitizerHeapVmar == ZX_HANDLE_INVALID) {
    CHECK_EQ(kSanitizerHeapVmarSize % GetPageSizeCached(), 0);
    uintptr_t base;
    status = _zx_vmar_allocate(
        _zx_vmar_root_self(),
        ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
        kSanitizerHeapVmarSize, &gSanitizerHeapVmar, &base);
  }
  *vmar = gSanitizerHeapVmar;
  if (status == ZX_OK)
    CHECK_NE(gSanitizerHeapVmar, ZX_HANDLE_INVALID);
  return status;
}

static zx_status_t TryVmoMapSanitizerVmar(zx_vm_option_t options,
                                          size_t vmar_offset, zx_handle_t vmo,
                                          size_t size, uintptr_t *addr,
                                          zx_handle_t *vmar_used = nullptr) {
  zx_handle_t vmar;
  zx_status_t status = GetSanitizerHeapVmar(&vmar);
  if (status != ZX_OK)
    return status;

  status = _zx_vmar_map(gSanitizerHeapVmar, options, vmar_offset, vmo,
                        /*vmo_offset=*/0, size, addr);
  if (vmar_used)
    *vmar_used = gSanitizerHeapVmar;
  if (status == ZX_ERR_NO_RESOURCES || status == ZX_ERR_INVALID_ARGS) {
    // This means there's no space in the heap VMAR, so fallback to the root
    // VMAR.
    status = _zx_vmar_map(_zx_vmar_root_self(), options, vmar_offset, vmo,
                          /*vmo_offset=*/0, size, addr);
    if (vmar_used)
      *vmar_used = _zx_vmar_root_self();
  }

  return status;
}

static void *DoAnonymousMmapOrDie(uptr size, const char *mem_type,
                                  bool raw_report, bool die_for_nomem) {
  size = RoundUpTo(size, GetPageSize());

  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(size, 0, &vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmo_create", status,
                              raw_report);
    return nullptr;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, mem_type,
                          internal_strlen(mem_type));

  uintptr_t addr;
  status = TryVmoMapSanitizerVmar(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                  /*vmar_offset=*/0, vmo, size, &addr);
  _zx_handle_close(vmo);

  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmar_map", status,
                              raw_report);
    return nullptr;
  }

  IncreaseTotalMmap(size);

  return reinterpret_cast<void *>(addr);
}

void *MmapOrDie(uptr size, const char *mem_type, bool raw_report) {
  return DoAnonymousMmapOrDie(size, mem_type, raw_report, true);
}

void *MmapNoReserveOrDie(uptr size, const char *mem_type) {
  return MmapOrDie(size, mem_type);
}

void *MmapOrDieOnFatalError(uptr size, const char *mem_type) {
  return DoAnonymousMmapOrDie(size, mem_type, false, false);
}

uptr ReservedAddressRange::Init(uptr init_size, const char *name,
                                uptr fixed_addr) {
  init_size = RoundUpTo(init_size, GetPageSize());
  DCHECK_EQ(os_handle_, ZX_HANDLE_INVALID);
  uintptr_t base;
  zx_handle_t vmar;
  zx_status_t status = _zx_vmar_allocate(
      _zx_vmar_root_self(),
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
      init_size, &vmar, &base);
  if (status != ZX_OK)
    ReportMmapFailureAndDie(init_size, name, "zx_vmar_allocate", status);
  base_ = reinterpret_cast<void *>(base);
  size_ = init_size;
  name_ = name;
  os_handle_ = vmar;

  return reinterpret_cast<uptr>(base_);
}

static uptr DoMmapFixedOrDie(zx_handle_t vmar, uptr fixed_addr, uptr map_size,
                             void *base, const char *name, bool die_for_nomem) {
  uptr offset = fixed_addr - reinterpret_cast<uptr>(base);
  map_size = RoundUpTo(map_size, GetPageSize());
  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(map_size, 0, &vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem)
      ReportMmapFailureAndDie(map_size, name, "zx_vmo_create", status);
    return 0;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, name, internal_strlen(name));
  DCHECK_GE(base + size_, map_size + offset);
  uintptr_t addr;

  status =
      _zx_vmar_map(vmar, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC,
                   offset, vmo, 0, map_size, &addr);
  _zx_handle_close(vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY || die_for_nomem) {
      ReportMmapFailureAndDie(map_size, name, "zx_vmar_map", status);
    }
    return 0;
  }
  IncreaseTotalMmap(map_size);
  return addr;
}

uptr ReservedAddressRange::Map(uptr fixed_addr, uptr map_size,
                               const char *name) {
  return DoMmapFixedOrDie(os_handle_, fixed_addr, map_size, base_,
                          name ? name : name_, false);
}

uptr ReservedAddressRange::MapOrDie(uptr fixed_addr, uptr map_size,
                                    const char *name) {
  return DoMmapFixedOrDie(os_handle_, fixed_addr, map_size, base_,
                          name ? name : name_, true);
}

void UnmapOrDieVmar(void *addr, uptr size, zx_handle_t target_vmar,
                    bool raw_report) {
  if (!addr || !size)
    return;
  size = RoundUpTo(size, GetPageSize());

  zx_status_t status =
      _zx_vmar_unmap(target_vmar, reinterpret_cast<uintptr_t>(addr), size);
  if (status == ZX_ERR_INVALID_ARGS && target_vmar == gSanitizerHeapVmar) {
    // If there wasn't any space in the heap vmar, the fallback was the root
    // vmar.
    status = _zx_vmar_unmap(_zx_vmar_root_self(),
                            reinterpret_cast<uintptr_t>(addr), size);
  }
  if (status != ZX_OK)
    ReportMunmapFailureAndDie(addr, size, status, raw_report);

  DecreaseTotalMmap(size);
}

void ReservedAddressRange::Unmap(uptr addr, uptr size) {
  CHECK_LE(size, size_);
  const zx_handle_t vmar = static_cast<zx_handle_t>(os_handle_);
  if (addr == reinterpret_cast<uptr>(base_)) {
    if (size == size_) {
      // Destroying the vmar effectively unmaps the whole mapping.
      _zx_vmar_destroy(vmar);
      _zx_handle_close(vmar);
      os_handle_ = static_cast<uptr>(ZX_HANDLE_INVALID);
      DecreaseTotalMmap(size);
      return;
    }
  } else {
    CHECK_EQ(addr + size, reinterpret_cast<uptr>(base_) + size_);
  }
  // Partial unmapping does not affect the fact that the initial range is still
  // reserved, and the resulting unmapped memory can't be reused.
  UnmapOrDieVmar(reinterpret_cast<void *>(addr), size, vmar,
                 /*raw_report=*/false);
}

// This should never be called.
void *MmapFixedNoAccess(uptr fixed_addr, uptr size, const char *name) {
  UNIMPLEMENTED();
}

bool MprotectNoAccess(uptr addr, uptr size) {
  return _zx_vmar_protect(_zx_vmar_root_self(), 0, addr, size) == ZX_OK;
}

bool MprotectReadOnly(uptr addr, uptr size) {
  return _zx_vmar_protect(_zx_vmar_root_self(), ZX_VM_PERM_READ, addr, size) ==
         ZX_OK;
}

bool MprotectReadWrite(uptr addr, uptr size) {
  return _zx_vmar_protect(_zx_vmar_root_self(),
                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, addr,
                          size) == ZX_OK;
}

void *MmapAlignedOrDieOnFatalError(uptr size, uptr alignment,
                                   const char *mem_type) {
  CHECK_GE(size, GetPageSize());
  CHECK(IsPowerOfTwo(size));
  CHECK(IsPowerOfTwo(alignment));

  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(size, 0, &vmo);
  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmo_create", status, false);
    return nullptr;
  }
  _zx_object_set_property(vmo, ZX_PROP_NAME, mem_type,
                          internal_strlen(mem_type));

  // Map a larger size to get a chunk of address space big enough that
  // it surely contains an aligned region of the requested size.  Then
  // overwrite the aligned middle portion with a mapping from the
  // beginning of the VMO, and unmap the excess before and after.
  size_t map_size = size + alignment;
  uintptr_t addr;
  zx_handle_t vmar_used;
  status = TryVmoMapSanitizerVmar(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                  /*vmar_offset=*/0, vmo, map_size, &addr,
                                  &vmar_used);
  if (status == ZX_OK) {
    uintptr_t map_addr = addr;
    uintptr_t map_end = map_addr + map_size;
    addr = RoundUpTo(map_addr, alignment);
    uintptr_t end = addr + size;
    if (addr != map_addr) {
      zx_info_vmar_t info;
      status = _zx_object_get_info(vmar_used, ZX_INFO_VMAR, &info, sizeof(info),
                                   NULL, NULL);
      if (status == ZX_OK) {
        uintptr_t new_addr;
        status = _zx_vmar_map(
            vmar_used,
            ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC_OVERWRITE,
            addr - info.base, vmo, 0, size, &new_addr);
        if (status == ZX_OK)
          CHECK_EQ(new_addr, addr);
      }
    }
    if (status == ZX_OK && addr != map_addr)
      status = _zx_vmar_unmap(vmar_used, map_addr, addr - map_addr);
    if (status == ZX_OK && end != map_end)
      status = _zx_vmar_unmap(vmar_used, end, map_end - end);
  }
  _zx_handle_close(vmo);

  if (status != ZX_OK) {
    if (status != ZX_ERR_NO_MEMORY)
      ReportMmapFailureAndDie(size, mem_type, "zx_vmar_map", status, false);
    return nullptr;
  }

  IncreaseTotalMmap(size);

  return reinterpret_cast<void *>(addr);
}

void UnmapOrDie(void *addr, uptr size, bool raw_report) {
  UnmapOrDieVmar(addr, size, gSanitizerHeapVmar, raw_report);
}

void ReleaseMemoryPagesToOS(uptr beg, uptr end) {
  uptr beg_aligned = RoundUpTo(beg, GetPageSize());
  uptr end_aligned = RoundDownTo(end, GetPageSize());
  if (beg_aligned < end_aligned) {
    zx_handle_t root_vmar = _zx_vmar_root_self();
    CHECK_NE(root_vmar, ZX_HANDLE_INVALID);
    zx_status_t status =
        _zx_vmar_op_range(root_vmar, ZX_VMAR_OP_DECOMMIT, beg_aligned,
                          end_aligned - beg_aligned, nullptr, 0);
    CHECK_EQ(status, ZX_OK);
  }
}

void DumpProcessMap() {
  // TODO(mcgrathr): write it
  return;
}

bool IsAccessibleMemoryRange(uptr beg, uptr size) {
  // TODO(mcgrathr): Figure out a better way.
  zx_handle_t vmo;
  zx_status_t status = _zx_vmo_create(size, 0, &vmo);
  if (status == ZX_OK) {
    status = _zx_vmo_write(vmo, reinterpret_cast<const void *>(beg), 0, size);
    _zx_handle_close(vmo);
  }
  return status == ZX_OK;
}

// FIXME implement on this platform.
void GetMemoryProfile(fill_profile_f cb, uptr *stats) {}

bool ReadFileToBuffer(const char *file_name, char **buff, uptr *buff_size,
                      uptr *read_len, uptr max_len, error_t *errno_p) {
  *errno_p = ZX_ERR_NOT_SUPPORTED;
  return false;
}

void RawWrite(const char *buffer) {
  constexpr size_t size = 128;
  static _Thread_local char line[size];
  static _Thread_local size_t lastLineEnd = 0;
  static _Thread_local size_t cur = 0;

  while (*buffer) {
    if (cur >= size) {
      if (lastLineEnd == 0)
        lastLineEnd = size;
      __sanitizer_log_write(line, lastLineEnd);
      internal_memmove(line, line + lastLineEnd, cur - lastLineEnd);
      cur = cur - lastLineEnd;
      lastLineEnd = 0;
    }
    if (*buffer == '\n')
      lastLineEnd = cur + 1;
    line[cur++] = *buffer++;
  }
  // Flush all complete lines before returning.
  if (lastLineEnd != 0) {
    __sanitizer_log_write(line, lastLineEnd);
    internal_memmove(line, line + lastLineEnd, cur - lastLineEnd);
    cur = cur - lastLineEnd;
    lastLineEnd = 0;
  }
}

void CatastrophicErrorWrite(const char *buffer, uptr length) {
  __sanitizer_log_write(buffer, length);
}

char **StoredArgv;
char **StoredEnviron;

char **GetArgv() { return StoredArgv; }
char **GetEnviron() { return StoredEnviron; }

const char *GetEnv(const char *name) {
  if (StoredEnviron) {
    uptr NameLen = internal_strlen(name);
    for (char **Env = StoredEnviron; *Env != 0; Env++) {
      if (internal_strncmp(*Env, name, NameLen) == 0 && (*Env)[NameLen] == '=')
        return (*Env) + NameLen + 1;
    }
  }
  return nullptr;
}

uptr ReadBinaryName(/*out*/ char *buf, uptr buf_len) {
  const char *argv0 = "<UNKNOWN>";
  if (StoredArgv && StoredArgv[0]) {
    argv0 = StoredArgv[0];
  }
  internal_strncpy(buf, argv0, buf_len);
  return internal_strlen(buf);
}

uptr ReadLongProcessName(/*out*/ char *buf, uptr buf_len) {
  return ReadBinaryName(buf, buf_len);
}

uptr MainThreadStackBase, MainThreadStackSize;

bool GetRandom(void *buffer, uptr length, bool blocking) {
  CHECK_LE(length, ZX_CPRNG_DRAW_MAX_LEN);
  _zx_cprng_draw(buffer, length);
  return true;
}

u32 GetNumberOfCPUs() { return zx_system_get_num_cpus(); }

uptr GetRSS() { UNIMPLEMENTED(); }

void *internal_start_thread(void *(*func)(void *arg), void *arg) { return 0; }
void internal_join_thread(void *th) {}

void InitializePlatformCommonFlags(CommonFlags *cf) {}

}  // namespace __sanitizer

using namespace __sanitizer;

extern "C" {
void __sanitizer_startup_hook(int argc, char **argv, char **envp,
                              void *stack_base, size_t stack_size) {
  __sanitizer::StoredArgv = argv;
  __sanitizer::StoredEnviron = envp;
  __sanitizer::MainThreadStackBase = reinterpret_cast<uintptr_t>(stack_base);
  __sanitizer::MainThreadStackSize = stack_size;
}

void __sanitizer_set_report_path(const char *path) {
  // Handle the initialization code in each sanitizer, but no other calls.
  // This setting is never consulted on Fuchsia.
  DCHECK_EQ(path, common_flags()->log_path);
}

void __sanitizer_set_report_fd(void *fd) {
  UNREACHABLE("not available on Fuchsia");
}

const char *__sanitizer_get_report_path() {
  UNREACHABLE("not available on Fuchsia");
}
}  // extern "C"

#endif  // SANITIZER_FUCHSIA
