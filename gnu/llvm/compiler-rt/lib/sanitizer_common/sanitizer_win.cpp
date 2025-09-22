//===-- sanitizer_win.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements windows-specific functions from
// sanitizer_libc.h.
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#include <io.h>
#include <psapi.h>
#include <stdlib.h>

#include "sanitizer_common.h"
#include "sanitizer_file.h"
#include "sanitizer_libc.h"
#include "sanitizer_mutex.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_win_defs.h"

#if defined(PSAPI_VERSION) && PSAPI_VERSION == 1
#pragma comment(lib, "psapi")
#endif
#if SANITIZER_WIN_TRACE
#include <traceloggingprovider.h>
//  Windows trace logging provider init
#pragma comment(lib, "advapi32.lib")
TRACELOGGING_DECLARE_PROVIDER(g_asan_provider);
// GUID must be the same in utils/AddressSanitizerLoggingProvider.wprp
TRACELOGGING_DEFINE_PROVIDER(g_asan_provider, "AddressSanitizerLoggingProvider",
                             (0x6c6c766d, 0x3846, 0x4e6a, 0xa4, 0xfb, 0x5b,
                              0x53, 0x0b, 0xd0, 0xf3, 0xfa));
#else
#define TraceLoggingUnregister(x)
#endif

// For WaitOnAddress
#  pragma comment(lib, "synchronization.lib")

// A macro to tell the compiler that this part of the code cannot be reached,
// if the compiler supports this feature. Since we're using this in
// code that is called when terminating the process, the expansion of the
// macro should not terminate the process to avoid infinite recursion.
#if defined(__clang__)
# define BUILTIN_UNREACHABLE() __builtin_unreachable()
#elif defined(__GNUC__) && \
    (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5))
# define BUILTIN_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
# define BUILTIN_UNREACHABLE() __assume(0)
#else
# define BUILTIN_UNREACHABLE()
#endif

namespace __sanitizer {

#include "sanitizer_syscall_generic.inc"

// --------------------- sanitizer_common.h
uptr GetPageSize() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
}

uptr GetMmapGranularity() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwAllocationGranularity;
}

uptr GetMaxUserVirtualAddress() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return (uptr)si.lpMaximumApplicationAddress;
}

uptr GetMaxVirtualAddress() {
  return GetMaxUserVirtualAddress();
}

bool FileExists(const char *filename) {
  return ::GetFileAttributesA(filename) != INVALID_FILE_ATTRIBUTES;
}

bool DirExists(const char *path) {
  auto attr = ::GetFileAttributesA(path);
  return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

uptr internal_getpid() {
  return GetProcessId(GetCurrentProcess());
}

int internal_dlinfo(void *handle, int request, void *p) {
  UNIMPLEMENTED();
}

// In contrast to POSIX, on Windows GetCurrentThreadId()
// returns a system-unique identifier.
tid_t GetTid() {
  return GetCurrentThreadId();
}

uptr GetThreadSelf() {
  return GetTid();
}

#if !SANITIZER_GO
void GetThreadStackTopAndBottom(bool at_initialization, uptr *stack_top,
                                uptr *stack_bottom) {
  CHECK(stack_top);
  CHECK(stack_bottom);
  MEMORY_BASIC_INFORMATION mbi;
  CHECK_NE(VirtualQuery(&mbi /* on stack */, &mbi, sizeof(mbi)), 0);
  // FIXME: is it possible for the stack to not be a single allocation?
  // Are these values what ASan expects to get (reserved, not committed;
  // including stack guard page) ?
  *stack_top = (uptr)mbi.BaseAddress + mbi.RegionSize;
  *stack_bottom = (uptr)mbi.AllocationBase;
}
#endif  // #if !SANITIZER_GO

bool ErrorIsOOM(error_t err) {
  // TODO: This should check which `err`s correspond to OOM.
  return false;
}

void *MmapOrDie(uptr size, const char *mem_type, bool raw_report) {
  void *rv = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (rv == 0)
    ReportMmapFailureAndDie(size, mem_type, "allocate",
                            GetLastError(), raw_report);
  return rv;
}

void UnmapOrDie(void *addr, uptr size, bool raw_report) {
  if (!size || !addr)
    return;

  MEMORY_BASIC_INFORMATION mbi;
  CHECK(VirtualQuery(addr, &mbi, sizeof(mbi)));

  // MEM_RELEASE can only be used to unmap whole regions previously mapped with
  // VirtualAlloc. So we first try MEM_RELEASE since it is better, and if that
  // fails try MEM_DECOMMIT.
  if (VirtualFree(addr, 0, MEM_RELEASE) == 0) {
    if (VirtualFree(addr, size, MEM_DECOMMIT) == 0) {
      ReportMunmapFailureAndDie(addr, size, GetLastError(), raw_report);
    }
  }
}

static void *ReturnNullptrOnOOMOrDie(uptr size, const char *mem_type,
                                     const char *mmap_type) {
  error_t last_error = GetLastError();
  if (last_error == ERROR_NOT_ENOUGH_MEMORY)
    return nullptr;
  ReportMmapFailureAndDie(size, mem_type, mmap_type, last_error);
}

void *MmapOrDieOnFatalError(uptr size, const char *mem_type) {
  void *rv = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (rv == 0)
    return ReturnNullptrOnOOMOrDie(size, mem_type, "allocate");
  return rv;
}

// We want to map a chunk of address space aligned to 'alignment'.
void *MmapAlignedOrDieOnFatalError(uptr size, uptr alignment,
                                   const char *mem_type) {
  CHECK(IsPowerOfTwo(size));
  CHECK(IsPowerOfTwo(alignment));

  // Windows will align our allocations to at least 64K.
  alignment = Max(alignment, GetMmapGranularity());

  uptr mapped_addr =
      (uptr)VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!mapped_addr)
    return ReturnNullptrOnOOMOrDie(size, mem_type, "allocate aligned");

  // If we got it right on the first try, return. Otherwise, unmap it and go to
  // the slow path.
  if (IsAligned(mapped_addr, alignment))
    return (void*)mapped_addr;
  if (VirtualFree((void *)mapped_addr, 0, MEM_RELEASE) == 0)
    ReportMmapFailureAndDie(size, mem_type, "deallocate", GetLastError());

  // If we didn't get an aligned address, overallocate, find an aligned address,
  // unmap, and try to allocate at that aligned address.
  int retries = 0;
  const int kMaxRetries = 10;
  for (; retries < kMaxRetries &&
         (mapped_addr == 0 || !IsAligned(mapped_addr, alignment));
       retries++) {
    // Overallocate size + alignment bytes.
    mapped_addr =
        (uptr)VirtualAlloc(0, size + alignment, MEM_RESERVE, PAGE_NOACCESS);
    if (!mapped_addr)
      return ReturnNullptrOnOOMOrDie(size, mem_type, "allocate aligned");

    // Find the aligned address.
    uptr aligned_addr = RoundUpTo(mapped_addr, alignment);

    // Free the overallocation.
    if (VirtualFree((void *)mapped_addr, 0, MEM_RELEASE) == 0)
      ReportMmapFailureAndDie(size, mem_type, "deallocate", GetLastError());

    // Attempt to allocate exactly the number of bytes we need at the aligned
    // address. This may fail for a number of reasons, in which case we continue
    // the loop.
    mapped_addr = (uptr)VirtualAlloc((void *)aligned_addr, size,
                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  }

  // Fail if we can't make this work quickly.
  if (retries == kMaxRetries && mapped_addr == 0)
    return ReturnNullptrOnOOMOrDie(size, mem_type, "allocate aligned");

  return (void *)mapped_addr;
}

// ZeroMmapFixedRegion zero's out a region of memory previously returned from a
// call to one of the MmapFixed* helpers. On non-windows systems this would be
// done with another mmap, but on windows remapping is not an option.
// VirtualFree(DECOMMIT)+VirtualAlloc(RECOMMIT) would also be a way to zero the
// memory, but we can't do this atomically, so instead we fall back to using
// internal_memset.
bool ZeroMmapFixedRegion(uptr fixed_addr, uptr size) {
  internal_memset((void*) fixed_addr, 0, size);
  return true;
}

bool MmapFixedNoReserve(uptr fixed_addr, uptr size, const char *name) {
  // FIXME: is this really "NoReserve"? On Win32 this does not matter much,
  // but on Win64 it does.
  (void)name;  // unsupported
#if !SANITIZER_GO && SANITIZER_WINDOWS64
  // On asan/Windows64, use MEM_COMMIT would result in error
  // 1455:ERROR_COMMITMENT_LIMIT.
  // Asan uses exception handler to commit page on demand.
  void *p = VirtualAlloc((LPVOID)fixed_addr, size, MEM_RESERVE, PAGE_READWRITE);
#else
  void *p = VirtualAlloc((LPVOID)fixed_addr, size, MEM_RESERVE | MEM_COMMIT,
                         PAGE_READWRITE);
#endif
  if (p == 0) {
    Report("ERROR: %s failed to "
           "allocate %p (%zd) bytes at %p (error code: %d)\n",
           SanitizerToolName, size, size, fixed_addr, GetLastError());
    return false;
  }
  return true;
}

bool MmapFixedSuperNoReserve(uptr fixed_addr, uptr size, const char *name) {
  // FIXME: Windows support large pages too. Might be worth checking
  return MmapFixedNoReserve(fixed_addr, size, name);
}

// Memory space mapped by 'MmapFixedOrDie' must have been reserved by
// 'MmapFixedNoAccess'.
void *MmapFixedOrDie(uptr fixed_addr, uptr size, const char *name) {
  void *p = VirtualAlloc((LPVOID)fixed_addr, size,
      MEM_COMMIT, PAGE_READWRITE);
  if (p == 0) {
    char mem_type[30];
    internal_snprintf(mem_type, sizeof(mem_type), "memory at address %p",
                      (void *)fixed_addr);
    ReportMmapFailureAndDie(size, mem_type, "allocate", GetLastError());
  }
  return p;
}

// Uses fixed_addr for now.
// Will use offset instead once we've implemented this function for real.
uptr ReservedAddressRange::Map(uptr fixed_addr, uptr size, const char *name) {
  return reinterpret_cast<uptr>(MmapFixedOrDieOnFatalError(fixed_addr, size));
}

uptr ReservedAddressRange::MapOrDie(uptr fixed_addr, uptr size,
                                    const char *name) {
  return reinterpret_cast<uptr>(MmapFixedOrDie(fixed_addr, size));
}

void ReservedAddressRange::Unmap(uptr addr, uptr size) {
  // Only unmap if it covers the entire range.
  CHECK((addr == reinterpret_cast<uptr>(base_)) && (size == size_));
  // We unmap the whole range, just null out the base.
  base_ = nullptr;
  size_ = 0;
  UnmapOrDie(reinterpret_cast<void*>(addr), size);
}

void *MmapFixedOrDieOnFatalError(uptr fixed_addr, uptr size, const char *name) {
  void *p = VirtualAlloc((LPVOID)fixed_addr, size,
      MEM_COMMIT, PAGE_READWRITE);
  if (p == 0) {
    char mem_type[30];
    internal_snprintf(mem_type, sizeof(mem_type), "memory at address %p",
                      (void *)fixed_addr);
    return ReturnNullptrOnOOMOrDie(size, mem_type, "allocate");
  }
  return p;
}

void *MmapNoReserveOrDie(uptr size, const char *mem_type) {
  // FIXME: make this really NoReserve?
  return MmapOrDie(size, mem_type);
}

uptr ReservedAddressRange::Init(uptr size, const char *name, uptr fixed_addr) {
  base_ = fixed_addr ? MmapFixedNoAccess(fixed_addr, size) : MmapNoAccess(size);
  size_ = size;
  name_ = name;
  (void)os_handle_;  // unsupported
  return reinterpret_cast<uptr>(base_);
}


void *MmapFixedNoAccess(uptr fixed_addr, uptr size, const char *name) {
  (void)name; // unsupported
  void *res = VirtualAlloc((LPVOID)fixed_addr, size,
                           MEM_RESERVE, PAGE_NOACCESS);
  if (res == 0)
    Report("WARNING: %s failed to "
           "mprotect %p (%zd) bytes at %p (error code: %d)\n",
           SanitizerToolName, size, size, fixed_addr, GetLastError());
  return res;
}

void *MmapNoAccess(uptr size) {
  void *res = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
  if (res == 0)
    Report("WARNING: %s failed to "
           "mprotect %p (%zd) bytes (error code: %d)\n",
           SanitizerToolName, size, size, GetLastError());
  return res;
}

bool MprotectNoAccess(uptr addr, uptr size) {
  DWORD old_protection;
  return VirtualProtect((LPVOID)addr, size, PAGE_NOACCESS, &old_protection);
}

bool MprotectReadOnly(uptr addr, uptr size) {
  DWORD old_protection;
  return VirtualProtect((LPVOID)addr, size, PAGE_READONLY, &old_protection);
}

bool MprotectReadWrite(uptr addr, uptr size) {
  DWORD old_protection;
  return VirtualProtect((LPVOID)addr, size, PAGE_READWRITE, &old_protection);
}

void ReleaseMemoryPagesToOS(uptr beg, uptr end) {
  uptr beg_aligned = RoundDownTo(beg, GetPageSizeCached()),
       end_aligned = RoundDownTo(end, GetPageSizeCached());
  CHECK(beg < end);                // make sure the region is sane
  if (beg_aligned == end_aligned)  // make sure we're freeing at least 1 page;
    return;
  UnmapOrDie((void *)beg, end_aligned - beg_aligned);
}

void SetShadowRegionHugePageMode(uptr addr, uptr size) {
  // FIXME: probably similar to ReleaseMemoryToOS.
}

bool DontDumpShadowMemory(uptr addr, uptr length) {
  // This is almost useless on 32-bits.
  // FIXME: add madvise-analog when we move to 64-bits.
  return true;
}

uptr MapDynamicShadow(uptr shadow_size_bytes, uptr shadow_scale,
                      uptr min_shadow_base_alignment, UNUSED uptr &high_mem_end,
                      uptr granularity) {
  const uptr alignment =
      Max<uptr>(granularity << shadow_scale, 1ULL << min_shadow_base_alignment);
  const uptr left_padding =
      Max<uptr>(granularity, 1ULL << min_shadow_base_alignment);
  uptr space_size = shadow_size_bytes + left_padding;
  uptr shadow_start = FindAvailableMemoryRange(space_size, alignment,
                                               granularity, nullptr, nullptr);
  CHECK_NE((uptr)0, shadow_start);
  CHECK(IsAligned(shadow_start, alignment));
  return shadow_start;
}

uptr FindAvailableMemoryRange(uptr size, uptr alignment, uptr left_padding,
                              uptr *largest_gap_found,
                              uptr *max_occupied_addr) {
  uptr address = 0;
  while (true) {
    MEMORY_BASIC_INFORMATION info;
    if (!::VirtualQuery((void*)address, &info, sizeof(info)))
      return 0;

    if (info.State == MEM_FREE) {
      uptr shadow_address = RoundUpTo((uptr)info.BaseAddress + left_padding,
                                      alignment);
      if (shadow_address + size < (uptr)info.BaseAddress + info.RegionSize)
        return shadow_address;
    }

    // Move to the next region.
    address = (uptr)info.BaseAddress + info.RegionSize;
  }
  return 0;
}

uptr MapDynamicShadowAndAliases(uptr shadow_size, uptr alias_size,
                                uptr num_aliases, uptr ring_buffer_size) {
  CHECK(false && "HWASan aliasing is unimplemented on Windows");
  return 0;
}

bool MemoryRangeIsAvailable(uptr range_start, uptr range_end) {
  MEMORY_BASIC_INFORMATION mbi;
  CHECK(VirtualQuery((void *)range_start, &mbi, sizeof(mbi)));
  return mbi.Protect == PAGE_NOACCESS &&
         (uptr)mbi.BaseAddress + mbi.RegionSize >= range_end;
}

void *MapFileToMemory(const char *file_name, uptr *buff_size) {
  UNIMPLEMENTED();
}

void *MapWritableFileToMemory(void *addr, uptr size, fd_t fd, OFF_T offset) {
  UNIMPLEMENTED();
}

static const int kMaxEnvNameLength = 128;
static const DWORD kMaxEnvValueLength = 32767;

namespace {

struct EnvVariable {
  char name[kMaxEnvNameLength];
  char value[kMaxEnvValueLength];
};

}  // namespace

static const int kEnvVariables = 5;
static EnvVariable env_vars[kEnvVariables];
static int num_env_vars;

const char *GetEnv(const char *name) {
  // Note: this implementation caches the values of the environment variables
  // and limits their quantity.
  for (int i = 0; i < num_env_vars; i++) {
    if (0 == internal_strcmp(name, env_vars[i].name))
      return env_vars[i].value;
  }
  CHECK_LT(num_env_vars, kEnvVariables);
  DWORD rv = GetEnvironmentVariableA(name, env_vars[num_env_vars].value,
                                     kMaxEnvValueLength);
  if (rv > 0 && rv < kMaxEnvValueLength) {
    CHECK_LT(internal_strlen(name), kMaxEnvNameLength);
    internal_strncpy(env_vars[num_env_vars].name, name, kMaxEnvNameLength);
    num_env_vars++;
    return env_vars[num_env_vars - 1].value;
  }
  return 0;
}

const char *GetPwd() {
  UNIMPLEMENTED();
}

u32 GetUid() {
  UNIMPLEMENTED();
}

namespace {
struct ModuleInfo {
  const char *filepath;
  uptr base_address;
  uptr end_address;
};

#if !SANITIZER_GO
int CompareModulesBase(const void *pl, const void *pr) {
  const ModuleInfo *l = (const ModuleInfo *)pl, *r = (const ModuleInfo *)pr;
  if (l->base_address < r->base_address)
    return -1;
  return l->base_address > r->base_address;
}
#endif
}  // namespace

#if !SANITIZER_GO
void DumpProcessMap() {
  Report("Dumping process modules:\n");
  ListOfModules modules;
  modules.init();
  uptr num_modules = modules.size();

  InternalMmapVector<ModuleInfo> module_infos(num_modules);
  for (size_t i = 0; i < num_modules; ++i) {
    module_infos[i].filepath = modules[i].full_name();
    module_infos[i].base_address = modules[i].ranges().front()->beg;
    module_infos[i].end_address = modules[i].ranges().back()->end;
  }
  qsort(module_infos.data(), num_modules, sizeof(ModuleInfo),
        CompareModulesBase);

  for (size_t i = 0; i < num_modules; ++i) {
    const ModuleInfo &mi = module_infos[i];
    if (mi.end_address != 0) {
      Printf("\t%p-%p %s\n", mi.base_address, mi.end_address,
             mi.filepath[0] ? mi.filepath : "[no name]");
    } else if (mi.filepath[0]) {
      Printf("\t??\?-??? %s\n", mi.filepath);
    } else {
      Printf("\t???\n");
    }
  }
}
#endif

void DisableCoreDumperIfNecessary() {
  // Do nothing.
}

void ReExec() {
  UNIMPLEMENTED();
}

void PlatformPrepareForSandboxing(void *args) {}

bool StackSizeIsUnlimited() {
  UNIMPLEMENTED();
}

void SetStackSizeLimitInBytes(uptr limit) {
  UNIMPLEMENTED();
}

bool AddressSpaceIsUnlimited() {
  UNIMPLEMENTED();
}

void SetAddressSpaceUnlimited() {
  UNIMPLEMENTED();
}

bool IsPathSeparator(const char c) {
  return c == '\\' || c == '/';
}

static bool IsAlpha(char c) {
  c = ToLower(c);
  return c >= 'a' && c <= 'z';
}

bool IsAbsolutePath(const char *path) {
  return path != nullptr && IsAlpha(path[0]) && path[1] == ':' &&
         IsPathSeparator(path[2]);
}

void internal_usleep(u64 useconds) { Sleep(useconds / 1000); }

u64 NanoTime() {
  static LARGE_INTEGER frequency = {};
  LARGE_INTEGER counter;
  if (UNLIKELY(frequency.QuadPart == 0)) {
    QueryPerformanceFrequency(&frequency);
    CHECK_NE(frequency.QuadPart, 0);
  }
  QueryPerformanceCounter(&counter);
  counter.QuadPart *= 1000ULL * 1000000ULL;
  counter.QuadPart /= frequency.QuadPart;
  return counter.QuadPart;
}

u64 MonotonicNanoTime() { return NanoTime(); }

void Abort() {
  internal__exit(3);
}

bool CreateDir(const char *pathname) {
  return CreateDirectoryA(pathname, nullptr) != 0;
}

#if !SANITIZER_GO
// Read the file to extract the ImageBase field from the PE header. If ASLR is
// disabled and this virtual address is available, the loader will typically
// load the image at this address. Therefore, we call it the preferred base. Any
// addresses in the DWARF typically assume that the object has been loaded at
// this address.
static uptr GetPreferredBase(const char *modname, char *buf, size_t buf_size) {
  fd_t fd = OpenFile(modname, RdOnly, nullptr);
  if (fd == kInvalidFd)
    return 0;
  FileCloser closer(fd);

  // Read just the DOS header.
  IMAGE_DOS_HEADER dos_header;
  uptr bytes_read;
  if (!ReadFromFile(fd, &dos_header, sizeof(dos_header), &bytes_read) ||
      bytes_read != sizeof(dos_header))
    return 0;

  // The file should start with the right signature.
  if (dos_header.e_magic != IMAGE_DOS_SIGNATURE)
    return 0;

  // The layout at e_lfanew is:
  // "PE\0\0"
  // IMAGE_FILE_HEADER
  // IMAGE_OPTIONAL_HEADER
  // Seek to e_lfanew and read all that data.
  if (::SetFilePointer(fd, dos_header.e_lfanew, nullptr, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER)
    return 0;
  if (!ReadFromFile(fd, buf, buf_size, &bytes_read) || bytes_read != buf_size)
    return 0;

  // Check for "PE\0\0" before the PE header.
  char *pe_sig = &buf[0];
  if (internal_memcmp(pe_sig, "PE\0\0", 4) != 0)
    return 0;

  // Skip over IMAGE_FILE_HEADER. We could do more validation here if we wanted.
  IMAGE_OPTIONAL_HEADER *pe_header =
      (IMAGE_OPTIONAL_HEADER *)(pe_sig + 4 + sizeof(IMAGE_FILE_HEADER));

  // Check for more magic in the PE header.
  if (pe_header->Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
    return 0;

  // Finally, return the ImageBase.
  return (uptr)pe_header->ImageBase;
}

void ListOfModules::init() {
  clearOrInit();
  HANDLE cur_process = GetCurrentProcess();

  // Query the list of modules.  Start by assuming there are no more than 256
  // modules and retry if that's not sufficient.
  HMODULE *hmodules = 0;
  uptr modules_buffer_size = sizeof(HMODULE) * 256;
  DWORD bytes_required;
  while (!hmodules) {
    hmodules = (HMODULE *)MmapOrDie(modules_buffer_size, __FUNCTION__);
    CHECK(EnumProcessModules(cur_process, hmodules, modules_buffer_size,
                             &bytes_required));
    if (bytes_required > modules_buffer_size) {
      // Either there turned out to be more than 256 hmodules, or new hmodules
      // could have loaded since the last try.  Retry.
      UnmapOrDie(hmodules, modules_buffer_size);
      hmodules = 0;
      modules_buffer_size = bytes_required;
    }
  }

  InternalMmapVector<char> buf(4 + sizeof(IMAGE_FILE_HEADER) +
                               sizeof(IMAGE_OPTIONAL_HEADER));
  InternalMmapVector<wchar_t> modname_utf16(kMaxPathLength);
  InternalMmapVector<char> module_name(kMaxPathLength);
  // |num_modules| is the number of modules actually present,
  size_t num_modules = bytes_required / sizeof(HMODULE);
  for (size_t i = 0; i < num_modules; ++i) {
    HMODULE handle = hmodules[i];
    MODULEINFO mi;
    if (!GetModuleInformation(cur_process, handle, &mi, sizeof(mi)))
      continue;

    // Get the UTF-16 path and convert to UTF-8.
    int modname_utf16_len =
        GetModuleFileNameW(handle, &modname_utf16[0], kMaxPathLength);
    if (modname_utf16_len == 0)
      modname_utf16[0] = '\0';
    int module_name_len = ::WideCharToMultiByte(
        CP_UTF8, 0, &modname_utf16[0], modname_utf16_len + 1, &module_name[0],
        kMaxPathLength, NULL, NULL);
    module_name[module_name_len] = '\0';

    uptr base_address = (uptr)mi.lpBaseOfDll;
    uptr end_address = (uptr)mi.lpBaseOfDll + mi.SizeOfImage;

    // Adjust the base address of the module so that we get a VA instead of an
    // RVA when computing the module offset. This helps llvm-symbolizer find the
    // right DWARF CU. In the common case that the image is loaded at it's
    // preferred address, we will now print normal virtual addresses.
    uptr preferred_base =
        GetPreferredBase(&module_name[0], &buf[0], buf.size());
    uptr adjusted_base = base_address - preferred_base;

    modules_.push_back(LoadedModule());
    LoadedModule &cur_module = modules_.back();
    cur_module.set(&module_name[0], adjusted_base);
    // We add the whole module as one single address range.
    cur_module.addAddressRange(base_address, end_address, /*executable*/ true,
                               /*writable*/ true);
  }
  UnmapOrDie(hmodules, modules_buffer_size);
}

void ListOfModules::fallbackInit() { clear(); }

// We can't use atexit() directly at __asan_init time as the CRT is not fully
// initialized at this point.  Place the functions into a vector and use
// atexit() as soon as it is ready for use (i.e. after .CRT$XIC initializers).
InternalMmapVectorNoCtor<void (*)(void)> atexit_functions;

static int queueAtexit(void (*function)(void)) {
  atexit_functions.push_back(function);
  return 0;
}

// If Atexit() is being called after RunAtexit() has already been run, it needs
// to be able to call atexit() directly. Here we use a function ponter to
// switch out its behaviour.
// An example of where this is needed is the asan_dynamic runtime on MinGW-w64.
// On this environment, __asan_init is called during global constructor phase,
// way after calling the .CRT$XID initializer.
static int (*volatile queueOrCallAtExit)(void (*)(void)) = &queueAtexit;

int Atexit(void (*function)(void)) { return queueOrCallAtExit(function); }

static int RunAtexit() {
  TraceLoggingUnregister(g_asan_provider);
  queueOrCallAtExit = &atexit;
  int ret = 0;
  for (uptr i = 0; i < atexit_functions.size(); ++i) {
    ret |= atexit(atexit_functions[i]);
  }
  return ret;
}

#pragma section(".CRT$XID", long, read)
__declspec(allocate(".CRT$XID")) int (*__run_atexit)() = RunAtexit;
#endif

// ------------------ sanitizer_libc.h
fd_t OpenFile(const char *filename, FileAccessMode mode, error_t *last_error) {
  // FIXME: Use the wide variants to handle Unicode filenames.
  fd_t res;
  if (mode == RdOnly) {
    res = CreateFileA(filename, GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  } else if (mode == WrOnly) {
    res = CreateFileA(filename, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
  } else {
    UNIMPLEMENTED();
  }
  CHECK(res != kStdoutFd || kStdoutFd == kInvalidFd);
  CHECK(res != kStderrFd || kStderrFd == kInvalidFd);
  if (res == kInvalidFd && last_error)
    *last_error = GetLastError();
  return res;
}

void CloseFile(fd_t fd) {
  CloseHandle(fd);
}

bool ReadFromFile(fd_t fd, void *buff, uptr buff_size, uptr *bytes_read,
                  error_t *error_p) {
  CHECK(fd != kInvalidFd);

  // bytes_read can't be passed directly to ReadFile:
  // uptr is unsigned long long on 64-bit Windows.
  unsigned long num_read_long;

  bool success = ::ReadFile(fd, buff, buff_size, &num_read_long, nullptr);
  if (!success && error_p)
    *error_p = GetLastError();
  if (bytes_read)
    *bytes_read = num_read_long;
  return success;
}

bool SupportsColoredOutput(fd_t fd) {
  // FIXME: support colored output.
  return false;
}

bool WriteToFile(fd_t fd, const void *buff, uptr buff_size, uptr *bytes_written,
                 error_t *error_p) {
  CHECK(fd != kInvalidFd);

  // Handle null optional parameters.
  error_t dummy_error;
  error_p = error_p ? error_p : &dummy_error;
  uptr dummy_bytes_written;
  bytes_written = bytes_written ? bytes_written : &dummy_bytes_written;

  // Initialize output parameters in case we fail.
  *error_p = 0;
  *bytes_written = 0;

  // Map the conventional Unix fds 1 and 2 to Windows handles. They might be
  // closed, in which case this will fail.
  if (fd == kStdoutFd || fd == kStderrFd) {
    fd = GetStdHandle(fd == kStdoutFd ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    if (fd == 0) {
      *error_p = ERROR_INVALID_HANDLE;
      return false;
    }
  }

  DWORD bytes_written_32;
  if (!WriteFile(fd, buff, buff_size, &bytes_written_32, 0)) {
    *error_p = GetLastError();
    return false;
  } else {
    *bytes_written = bytes_written_32;
    return true;
  }
}

uptr internal_sched_yield() {
  Sleep(0);
  return 0;
}

void internal__exit(int exitcode) {
  TraceLoggingUnregister(g_asan_provider);
  // ExitProcess runs some finalizers, so use TerminateProcess to avoid that.
  // The debugger doesn't stop on TerminateProcess like it does on ExitProcess,
  // so add our own breakpoint here.
  if (::IsDebuggerPresent())
    __debugbreak();
  TerminateProcess(GetCurrentProcess(), exitcode);
  BUILTIN_UNREACHABLE();
}

uptr internal_ftruncate(fd_t fd, uptr size) {
  UNIMPLEMENTED();
}

uptr GetRSS() {
  PROCESS_MEMORY_COUNTERS counters;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return 0;
  return counters.WorkingSetSize;
}

void *internal_start_thread(void *(*func)(void *arg), void *arg) { return 0; }
void internal_join_thread(void *th) { }

void FutexWait(atomic_uint32_t *p, u32 cmp) {
  WaitOnAddress(p, &cmp, sizeof(cmp), INFINITE);
}

void FutexWake(atomic_uint32_t *p, u32 count) {
  if (count == 1)
    WakeByAddressSingle(p);
  else
    WakeByAddressAll(p);
}

uptr GetTlsSize() {
  return 0;
}

void InitTlsSize() {
}

void GetThreadStackAndTls(bool main, uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
#if SANITIZER_GO
  *stk_addr = 0;
  *stk_size = 0;
  *tls_addr = 0;
  *tls_size = 0;
#else
  uptr stack_top, stack_bottom;
  GetThreadStackTopAndBottom(main, &stack_top, &stack_bottom);
  *stk_addr = stack_bottom;
  *stk_size = stack_top - stack_bottom;
  *tls_addr = 0;
  *tls_size = 0;
#endif
}

void ReportFile::Write(const char *buffer, uptr length) {
  SpinMutexLock l(mu);
  ReopenIfNecessary();
  if (!WriteToFile(fd, buffer, length)) {
    // stderr may be closed, but we may be able to print to the debugger
    // instead.  This is the case when launching a program from Visual Studio,
    // and the following routine should write to its console.
    OutputDebugStringA(buffer);
  }
}

void SetAlternateSignalStack() {
  // FIXME: Decide what to do on Windows.
}

void UnsetAlternateSignalStack() {
  // FIXME: Decide what to do on Windows.
}

void InstallDeadlySignalHandlers(SignalHandlerType handler) {
  (void)handler;
  // FIXME: Decide what to do on Windows.
}

HandleSignalMode GetHandleSignalMode(int signum) {
  // FIXME: Decide what to do on Windows.
  return kHandleSignalNo;
}

// Check based on flags if we should handle this exception.
bool IsHandledDeadlyException(DWORD exceptionCode) {
  switch (exceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_IN_PAGE_ERROR:
      return common_flags()->handle_segv;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_BREAKPOINT:
      return common_flags()->handle_sigill;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
      return common_flags()->handle_sigfpe;
  }
  return false;
}

bool IsAccessibleMemoryRange(uptr beg, uptr size) {
  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  uptr page_size = si.dwPageSize;
  uptr page_mask = ~(page_size - 1);

  for (uptr page = beg & page_mask, end = (beg + size - 1) & page_mask;
       page <= end;) {
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery((LPCVOID)page, &info, sizeof(info)) != sizeof(info))
      return false;

    if (info.Protect == 0 || info.Protect == PAGE_NOACCESS ||
        info.Protect == PAGE_EXECUTE)
      return false;

    if (info.RegionSize == 0)
      return false;

    page += info.RegionSize;
  }

  return true;
}

bool SignalContext::IsStackOverflow() const {
  return (DWORD)GetType() == EXCEPTION_STACK_OVERFLOW;
}

void SignalContext::InitPcSpBp() {
  EXCEPTION_RECORD *exception_record = (EXCEPTION_RECORD *)siginfo;
  CONTEXT *context_record = (CONTEXT *)context;

  pc = (uptr)exception_record->ExceptionAddress;
#  if SANITIZER_WINDOWS64
#    if SANITIZER_ARM64
  bp = (uptr)context_record->Fp;
  sp = (uptr)context_record->Sp;
#    else
  bp = (uptr)context_record->Rbp;
  sp = (uptr)context_record->Rsp;
#    endif
#  else
#    if SANITIZER_ARM
  bp = (uptr)context_record->R11;
  sp = (uptr)context_record->Sp;
#    else
  bp = (uptr)context_record->Ebp;
  sp = (uptr)context_record->Esp;
#    endif
#  endif
}

uptr SignalContext::GetAddress() const {
  EXCEPTION_RECORD *exception_record = (EXCEPTION_RECORD *)siginfo;
  if (exception_record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    return exception_record->ExceptionInformation[1];
  return (uptr)exception_record->ExceptionAddress;
}

bool SignalContext::IsMemoryAccess() const {
  return ((EXCEPTION_RECORD *)siginfo)->ExceptionCode ==
         EXCEPTION_ACCESS_VIOLATION;
}

bool SignalContext::IsTrueFaultingAddress() const { return true; }

SignalContext::WriteFlag SignalContext::GetWriteFlag() const {
  EXCEPTION_RECORD *exception_record = (EXCEPTION_RECORD *)siginfo;

  // The write flag is only available for access violation exceptions.
  if (exception_record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    return SignalContext::Unknown;

  // The contents of this array are documented at
  // https://docs.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record
  // The first element indicates read as 0, write as 1, or execute as 8.  The
  // second element is the faulting address.
  switch (exception_record->ExceptionInformation[0]) {
    case 0:
      return SignalContext::Read;
    case 1:
      return SignalContext::Write;
    case 8:
      return SignalContext::Unknown;
  }
  return SignalContext::Unknown;
}

void SignalContext::DumpAllRegisters(void *context) {
  // FIXME: Implement this.
}

int SignalContext::GetType() const {
  return static_cast<const EXCEPTION_RECORD *>(siginfo)->ExceptionCode;
}

const char *SignalContext::Describe() const {
  unsigned code = GetType();
  // Get the string description of the exception if this is a known deadly
  // exception.
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      return "access-violation";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      return "array-bounds-exceeded";
    case EXCEPTION_STACK_OVERFLOW:
      return "stack-overflow";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      return "datatype-misalignment";
    case EXCEPTION_IN_PAGE_ERROR:
      return "in-page-error";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      return "illegal-instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
      return "priv-instruction";
    case EXCEPTION_BREAKPOINT:
      return "breakpoint";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
      return "flt-denormal-operand";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      return "flt-divide-by-zero";
    case EXCEPTION_FLT_INEXACT_RESULT:
      return "flt-inexact-result";
    case EXCEPTION_FLT_INVALID_OPERATION:
      return "flt-invalid-operation";
    case EXCEPTION_FLT_OVERFLOW:
      return "flt-overflow";
    case EXCEPTION_FLT_STACK_CHECK:
      return "flt-stack-check";
    case EXCEPTION_FLT_UNDERFLOW:
      return "flt-underflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      return "int-divide-by-zero";
    case EXCEPTION_INT_OVERFLOW:
      return "int-overflow";
  }
  return "unknown exception";
}

uptr ReadBinaryName(/*out*/char *buf, uptr buf_len) {
  if (buf_len == 0)
    return 0;

  // Get the UTF-16 path and convert to UTF-8.
  InternalMmapVector<wchar_t> binname_utf16(kMaxPathLength);
  int binname_utf16_len =
      GetModuleFileNameW(NULL, &binname_utf16[0], kMaxPathLength);
  if (binname_utf16_len == 0) {
    buf[0] = '\0';
    return 0;
  }
  int binary_name_len =
      ::WideCharToMultiByte(CP_UTF8, 0, &binname_utf16[0], binname_utf16_len,
                            buf, buf_len, NULL, NULL);
  if ((unsigned)binary_name_len == buf_len)
    --binary_name_len;
  buf[binary_name_len] = '\0';
  return binary_name_len;
}

uptr ReadLongProcessName(/*out*/char *buf, uptr buf_len) {
  return ReadBinaryName(buf, buf_len);
}

void CheckVMASize() {
  // Do nothing.
}

void InitializePlatformEarly() {
  // Do nothing.
}

void CheckASLR() {
  // Do nothing
}

void CheckMPROTECT() {
  // Do nothing
}

char **GetArgv() {
  // FIXME: Actually implement this function.
  return 0;
}

char **GetEnviron() {
  // FIXME: Actually implement this function.
  return 0;
}

pid_t StartSubprocess(const char *program, const char *const argv[],
                      const char *const envp[], fd_t stdin_fd, fd_t stdout_fd,
                      fd_t stderr_fd) {
  // FIXME: implement on this platform
  // Should be implemented based on
  // SymbolizerProcess::StarAtSymbolizerSubprocess
  // from lib/sanitizer_common/sanitizer_symbolizer_win.cpp.
  return -1;
}

bool IsProcessRunning(pid_t pid) {
  // FIXME: implement on this platform.
  return false;
}

int WaitForProcess(pid_t pid) { return -1; }

// FIXME implement on this platform.
void GetMemoryProfile(fill_profile_f cb, uptr *stats) {}

void CheckNoDeepBind(const char *filename, int flag) {
  // Do nothing.
}

// FIXME: implement on this platform.
bool GetRandom(void *buffer, uptr length, bool blocking) {
  UNIMPLEMENTED();
}

u32 GetNumberOfCPUs() {
  SYSTEM_INFO sysinfo = {};
  GetNativeSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
}

#if SANITIZER_WIN_TRACE
// TODO(mcgov): Rename this project-wide to PlatformLogInit
void AndroidLogInit(void) {
  HRESULT hr = TraceLoggingRegister(g_asan_provider);
  if (!SUCCEEDED(hr))
    return;
}

void SetAbortMessage(const char *) {}

void LogFullErrorReport(const char *buffer) {
  if (common_flags()->log_to_syslog) {
    InternalMmapVector<wchar_t> filename;
    DWORD filename_length = 0;
    do {
      filename.resize(filename.size() + 0x100);
      filename_length =
          GetModuleFileNameW(NULL, filename.begin(), filename.size());
    } while (filename_length >= filename.size());
    TraceLoggingWrite(g_asan_provider, "AsanReportEvent",
                      TraceLoggingValue(filename.begin(), "ExecutableName"),
                      TraceLoggingValue(buffer, "AsanReportContents"));
  }
}
#endif // SANITIZER_WIN_TRACE

void InitializePlatformCommonFlags(CommonFlags *cf) {}

}  // namespace __sanitizer

#endif  // _WIN32
