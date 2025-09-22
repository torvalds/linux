//===-- DNBDefs.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/26/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBDEFS_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBDEFS_H

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/syslimits.h>
#include <unistd.h>
#include <vector>

// Define nub_addr_t and the invalid address value from the architecture
#if defined(__x86_64__) || defined(__arm64__) || defined(__aarch64__)

// 64 bit address architectures
typedef uint64_t nub_addr_t;
#define INVALID_NUB_ADDRESS ((nub_addr_t)~0ull)

#elif defined(__i386__) || defined(__powerpc__) || defined(__arm__)

// 32 bit address architectures

typedef uint32_t nub_addr_t;
#define INVALID_NUB_ADDRESS ((nub_addr_t)~0ul)

#else

// Default to 64 bit address for unrecognized architectures.

#warning undefined architecture, defaulting to 8 byte addresses
typedef uint64_t nub_addr_t;
#define INVALID_NUB_ADDRESS ((nub_addr_t)~0ull)

#endif

typedef size_t nub_size_t;
typedef ssize_t nub_ssize_t;
typedef uint32_t nub_index_t;
typedef pid_t nub_process_t;
typedef uint64_t nub_thread_t;
typedef uint32_t nub_event_t;
typedef uint32_t nub_bool_t;

#define INVALID_NUB_PROCESS ((nub_process_t)0)
#define INVALID_NUB_PROCESS_ARCH ((nub_process_t)-1)
#define INVALID_NUB_THREAD ((nub_thread_t)0)
#define INVALID_NUB_WATCH_ID ((nub_watch_t)0)
#define INVALID_NUB_HW_INDEX UINT32_MAX
#define INVALID_NUB_REGNUM UINT32_MAX
#define NUB_GENERIC_ERROR UINT32_MAX

// Watchpoint types
#define WATCH_TYPE_READ (1u << 0)
#define WATCH_TYPE_WRITE (1u << 1)

enum nub_state_t {
  eStateInvalid = 0,
  eStateUnloaded,
  eStateAttaching,
  eStateLaunching,
  eStateStopped,
  eStateRunning,
  eStateStepping,
  eStateCrashed,
  eStateDetached,
  eStateExited,
  eStateSuspended
};

enum nub_launch_flavor_t {
  eLaunchFlavorDefault = 0,
  eLaunchFlavorPosixSpawn = 1,
  eLaunchFlavorForkExec = 2,
#ifdef WITH_SPRINGBOARD
  eLaunchFlavorSpringBoard = 3,
#endif
#ifdef WITH_BKS
  eLaunchFlavorBKS = 4,
#endif
#ifdef WITH_FBS
  eLaunchFlavorFBS = 5
#endif
};

#define NUB_STATE_IS_RUNNING(s)                                                \
  ((s) == eStateAttaching || (s) == eStateLaunching || (s) == eStateRunning || \
   (s) == eStateStepping || (s) == eStateDetached)

#define NUB_STATE_IS_STOPPED(s)                                                \
  ((s) == eStateUnloaded || (s) == eStateStopped || (s) == eStateCrashed ||    \
   (s) == eStateExited)

enum {
  eEventProcessRunningStateChanged =
      1 << 0, // The process has changed state to running
  eEventProcessStoppedStateChanged =
      1 << 1, // The process has changed state to stopped
  eEventSharedLibsStateChange =
      1 << 2, // Shared libraries loaded/unloaded state has changed
  eEventStdioAvailable = 1 << 3, // Something is available on stdout/stderr
  eEventProfileDataAvailable = 1 << 4, // Profile data ready for retrieval
  kAllEventsMask = eEventProcessRunningStateChanged |
                   eEventProcessStoppedStateChanged |
                   eEventSharedLibsStateChange | eEventStdioAvailable |
                   eEventProfileDataAvailable
};

#define LOG_VERBOSE (1u << 0)
#define LOG_PROCESS (1u << 1)
#define LOG_THREAD (1u << 2)
#define LOG_EXCEPTIONS (1u << 3)
#define LOG_SHLIB (1u << 4)
#define LOG_MEMORY (1u << 5)             // Log memory reads/writes calls
#define LOG_MEMORY_DATA_SHORT (1u << 6)  // Log short memory reads/writes bytes
#define LOG_MEMORY_DATA_LONG (1u << 7)   // Log all memory reads/writes bytes
#define LOG_MEMORY_PROTECTIONS (1u << 8) // Log memory protection changes
#define LOG_BREAKPOINTS (1u << 9)
#define LOG_EVENTS (1u << 10)
#define LOG_WATCHPOINTS (1u << 11)
#define LOG_STEP (1u << 12)
#define LOG_TASK (1u << 13)
#define LOG_DARWIN_LOG (1u << 14)
#define LOG_LO_USER (1u << 16)
#define LOG_HI_USER (1u << 31)
#define LOG_ALL 0xFFFFFFFFu
#define LOG_DEFAULT                                                            \
  ((LOG_PROCESS) | (LOG_TASK) | (LOG_THREAD) | (LOG_EXCEPTIONS) |              \
   (LOG_SHLIB) | (LOG_MEMORY) | (LOG_BREAKPOINTS) | (LOG_WATCHPOINTS) |        \
   (LOG_STEP))

#define REGISTER_SET_ALL 0
// Generic Register set to be defined by each architecture for access to common
// register values.
#define REGISTER_SET_GENERIC ((uint32_t)0xFFFFFFFFu)
#define GENERIC_REGNUM_PC 0    // Program Counter
#define GENERIC_REGNUM_SP 1    // Stack Pointer
#define GENERIC_REGNUM_FP 2    // Frame Pointer
#define GENERIC_REGNUM_RA 3    // Return Address
#define GENERIC_REGNUM_FLAGS 4 // Processor flags register
#define GENERIC_REGNUM_ARG1                                                    \
  5 // The register that would contain pointer size or less argument 1 (if any)
#define GENERIC_REGNUM_ARG2                                                    \
  6 // The register that would contain pointer size or less argument 2 (if any)
#define GENERIC_REGNUM_ARG3                                                    \
  7 // The register that would contain pointer size or less argument 3 (if any)
#define GENERIC_REGNUM_ARG4                                                    \
  8 // The register that would contain pointer size or less argument 4 (if any)
#define GENERIC_REGNUM_ARG5                                                    \
  9 // The register that would contain pointer size or less argument 5 (if any)
#define GENERIC_REGNUM_ARG6                                                    \
  10 // The register that would contain pointer size or less argument 6 (if any)
#define GENERIC_REGNUM_ARG7                                                    \
  11 // The register that would contain pointer size or less argument 7 (if any)
#define GENERIC_REGNUM_ARG8                                                    \
  12 // The register that would contain pointer size or less argument 8 (if any)

enum DNBRegisterType {
  InvalidRegType = 0,
  Uint,    // unsigned integer
  Sint,    // signed integer
  IEEE754, // float
  Vector   // vector registers
};

enum DNBRegisterFormat {
  InvalidRegFormat = 0,
  Binary,
  Decimal,
  Hex,
  Float,
  VectorOfSInt8,
  VectorOfUInt8,
  VectorOfSInt16,
  VectorOfUInt16,
  VectorOfSInt32,
  VectorOfUInt32,
  VectorOfFloat32,
  VectorOfUInt128
};

struct DNBRegisterInfo {
  uint32_t set;     // Register set
  uint32_t reg;     // Register number
  const char *name; // Name of this register
  const char *alt;  // Alternate name
  uint16_t type;    // Type of the register bits (DNBRegisterType)
  uint16_t format;  // Default format for display (DNBRegisterFormat),
  uint32_t size;    // Size in bytes of the register
  uint32_t offset;  // Offset from the beginning of the register context
  uint32_t
      reg_ehframe;    // eh_frame register number (INVALID_NUB_REGNUM when none)
  uint32_t reg_dwarf; // DWARF register number (INVALID_NUB_REGNUM when none)
  uint32_t
      reg_generic; // Generic register number (INVALID_NUB_REGNUM when none)
  uint32_t reg_debugserver; // The debugserver register number we'll use over
                            // gdb-remote protocol (INVALID_NUB_REGNUM when
                            // none)
  const char **value_regs;  // If this register is a part of other registers,
                            // list the register names terminated by NULL
  const char **update_regs; // If modifying this register will invalidate other
                            // registers, list the register names terminated by
                            // NULL
};

struct DNBRegisterSetInfo {
  const char *name;                        // Name of this register set
  const struct DNBRegisterInfo *registers; // An array of register descriptions
  nub_size_t num_registers; // The number of registers in REGISTERS array above
};

struct DNBThreadResumeAction {
  nub_thread_t tid;  // The thread ID that this action applies to,
                     // INVALID_NUB_THREAD for the default thread action
  nub_state_t state; // Valid values are eStateStopped/eStateSuspended,
                     // eStateRunning, and eStateStepping.
  int signal;        // When resuming this thread, resume it with this signal
  nub_addr_t addr; // If not INVALID_NUB_ADDRESS, then set the PC for the thread
                   // to ADDR before resuming/stepping
};

enum DNBThreadStopType {
  eStopTypeInvalid = 0,
  eStopTypeSignal,
  eStopTypeException,
  eStopTypeExec,
  eStopTypeWatchpoint
};

enum DNBMemoryPermissions {
  eMemoryPermissionsWritable = (1 << 0),
  eMemoryPermissionsReadable = (1 << 1),
  eMemoryPermissionsExecutable = (1 << 2)
};

#define DNB_THREAD_STOP_INFO_MAX_DESC_LENGTH 256
#define DNB_THREAD_STOP_INFO_MAX_EXC_DATA 8

// DNBThreadStopInfo
//
// Describes the reason a thread stopped.
struct DNBThreadStopInfo {
  DNBThreadStopType reason;
  char description[DNB_THREAD_STOP_INFO_MAX_DESC_LENGTH];
  union {
    // eStopTypeSignal
    struct {
      uint32_t signo;
    } signal;

    // eStopTypeException
    struct {
      uint32_t type;
      nub_size_t data_count;
      nub_addr_t data[DNB_THREAD_STOP_INFO_MAX_EXC_DATA];
    } exception;

    // eStopTypeWatchpoint
    struct {
      // The trigger address from the mach exception
      // (likely the contents of the FAR register)
      nub_addr_t mach_exception_addr;

      // The trigger address, adjusted to be the start
      // address of one of the existing watchpoints for
      // lldb's benefit.
      nub_addr_t addr;

      // The watchpoint hardware index.
      uint32_t hw_idx;

      // If the esr_fields bitfields have been filled in.
      bool esr_fields_set;
      struct {
        uint32_t
            iss; // "ISS encoding for an exception from a Watchpoint exception"
        uint32_t wpt;  // Watchpoint number
        bool wptv;     // Watchpoint number Valid
        bool wpf;      // Watchpoint might be false-positive
        bool fnp;      // FAR not Precise
        bool vncr;     // watchpoint from use of VNCR_EL2 reg by EL1
        bool fnv;      // FAR not Valid
        bool cm;       // Cache maintenance
        bool wnr;      // Write not Read
        uint32_t dfsc; // Data Fault Status Code
      } esr_fields;
    } watchpoint;
  } details;
};

struct DNBRegisterValue {
  struct DNBRegisterInfo info; // Register information for this register
  union {
    int8_t sint8;
    int16_t sint16;
    int32_t sint32;
    int64_t sint64;
    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;
    float float32;
    double float64;
    int8_t v_sint8[64];
    int16_t v_sint16[32];
    int32_t v_sint32[16];
    int64_t v_sint64[8];
    uint8_t v_uint8[64];
    uint16_t v_uint16[32];
    uint32_t v_uint32[16];
    uint64_t v_uint64[8];
    float v_float32[16];
    double v_float64[8];
    void *pointer;
    char *c_str;
  } value;
};

enum DNBSharedLibraryState { eShlibStateUnloaded = 0, eShlibStateLoaded = 1 };

#ifndef DNB_MAX_SEGMENT_NAME_LENGTH
#define DNB_MAX_SEGMENT_NAME_LENGTH 32
#endif

struct DNBSegment {
  char name[DNB_MAX_SEGMENT_NAME_LENGTH];
  nub_addr_t addr;
  nub_addr_t size;
};

struct DNBExecutableImageInfo {
  char name[PATH_MAX]; // Name of the executable image (usually a full path)
  uint32_t
      state; // State of the executable image (see enum DNBSharedLibraryState)
  nub_addr_t header_addr; // Executable header address
  uuid_t uuid;            // Unique identifier for matching with symbols
  uint32_t
      num_segments; // Number of contiguous memory segments to in SEGMENTS array
  DNBSegment *segments; // Array of contiguous memory segments in executable
};

struct DNBRegionInfo {
public:
  DNBRegionInfo()
      : addr(0), size(0), permissions(0), dirty_pages(), vm_types() {}
  nub_addr_t addr;
  nub_addr_t size;
  uint32_t permissions;
  std::vector<nub_addr_t> dirty_pages;
  std::vector<std::string> vm_types;
};

enum DNBProfileDataScanType {
  eProfileHostCPU = (1 << 0),
  eProfileCPU = (1 << 1),

  eProfileThreadsCPU =
      (1 << 2), // By default excludes eProfileThreadName and eProfileQueueName.
  eProfileThreadName =
      (1 << 3), // Assume eProfileThreadsCPU, get thread name as well.
  eProfileQueueName =
      (1 << 4), // Assume eProfileThreadsCPU, get queue name as well.

  eProfileHostMemory = (1 << 5),

  eProfileMemory = (1 << 6),
  eProfileMemoryAnonymous =
      (1 << 8), // Assume eProfileMemory, get Anonymous memory as well.

  eProfileEnergy = (1 << 9),
  eProfileEnergyCPUCap = (1 << 10),

  eProfileMemoryCap = (1 << 15),

  eProfileAll = 0xffffffff
};

typedef nub_addr_t (*DNBCallbackNameToAddress)(nub_process_t pid,
                                               const char *name,
                                               const char *shlib_regex,
                                               void *baton);
typedef nub_size_t (*DNBCallbackCopyExecutableImageInfos)(
    nub_process_t pid, struct DNBExecutableImageInfo **image_infos,
    nub_bool_t only_changed, void *baton);
typedef void (*DNBCallbackLog)(void *baton, uint32_t flags, const char *format,
                               va_list args);

#define UNUSED_IF_ASSERT_DISABLED(x) ((void)(x))

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBDEFS_H
