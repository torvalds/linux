//===-- tsan_interface.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Public interface header for TSan.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_TSAN_INTERFACE_H
#define SANITIZER_TSAN_INTERFACE_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

// __tsan_release establishes a happens-before relation with a preceding
// __tsan_acquire on the same address.
void SANITIZER_CDECL __tsan_acquire(void *addr);
void SANITIZER_CDECL __tsan_release(void *addr);

// Annotations for custom mutexes.
// The annotations allow to get better reports (with sets of locked mutexes),
// detect more types of bugs (e.g. mutex misuses, races between lock/unlock and
// destruction and potential deadlocks) and improve precision and performance
// (by ignoring individual atomic operations in mutex code). However, the
// downside is that annotated mutex code itself is not checked for correctness.

// Mutex creation flags are passed to __tsan_mutex_create annotation.
// If mutex has no constructor and __tsan_mutex_create is not called,
// the flags may be passed to __tsan_mutex_pre_lock/__tsan_mutex_post_lock
// annotations.

// Mutex has static storage duration and no-op constructor and destructor.
// This effectively makes tsan ignore destroy annotation.
static const unsigned __tsan_mutex_linker_init      = 1 << 0;
// Mutex is write reentrant.
static const unsigned __tsan_mutex_write_reentrant  = 1 << 1;
// Mutex is read reentrant.
static const unsigned __tsan_mutex_read_reentrant   = 1 << 2;
// Mutex does not have static storage duration, and must not be used after
// its destructor runs.  The opposite of __tsan_mutex_linker_init.
// If this flag is passed to __tsan_mutex_destroy, then the destruction
// is ignored unless this flag was previously set on the mutex.
static const unsigned __tsan_mutex_not_static       = 1 << 8;

// Mutex operation flags:

// Denotes read lock operation.
static const unsigned __tsan_mutex_read_lock = 1 << 3;
// Denotes try lock operation.
static const unsigned __tsan_mutex_try_lock = 1 << 4;
// Denotes that a try lock operation has failed to acquire the mutex.
static const unsigned __tsan_mutex_try_lock_failed = 1 << 5;
// Denotes that the lock operation acquires multiple recursion levels.
// Number of levels is passed in recursion parameter.
// This is useful for annotation of e.g. Java builtin monitors,
// for which wait operation releases all recursive acquisitions of the mutex.
static const unsigned __tsan_mutex_recursive_lock = 1 << 6;
// Denotes that the unlock operation releases all recursion levels.
// Number of released levels is returned and later must be passed to
// the corresponding __tsan_mutex_post_lock annotation.
static const unsigned __tsan_mutex_recursive_unlock = 1 << 7;

// Convenient composed constants.
static const unsigned __tsan_mutex_try_read_lock =
    __tsan_mutex_read_lock | __tsan_mutex_try_lock;
static const unsigned __tsan_mutex_try_read_lock_failed =
    __tsan_mutex_try_read_lock | __tsan_mutex_try_lock_failed;

// Annotate creation of a mutex.
// Supported flags: mutex creation flags.
void SANITIZER_CDECL __tsan_mutex_create(void *addr, unsigned flags);

// Annotate destruction of a mutex.
// Supported flags:
//   - __tsan_mutex_linker_init
//   - __tsan_mutex_not_static
void SANITIZER_CDECL __tsan_mutex_destroy(void *addr, unsigned flags);

// Annotate start of lock operation.
// Supported flags:
//   - __tsan_mutex_read_lock
//   - __tsan_mutex_try_lock
//   - all mutex creation flags
void SANITIZER_CDECL __tsan_mutex_pre_lock(void *addr, unsigned flags);

// Annotate end of lock operation.
// Supported flags:
//   - __tsan_mutex_read_lock (must match __tsan_mutex_pre_lock)
//   - __tsan_mutex_try_lock (must match __tsan_mutex_pre_lock)
//   - __tsan_mutex_try_lock_failed
//   - __tsan_mutex_recursive_lock
//   - all mutex creation flags
void SANITIZER_CDECL __tsan_mutex_post_lock(void *addr, unsigned flags,
                                            int recursion);

// Annotate start of unlock operation.
// Supported flags:
//   - __tsan_mutex_read_lock
//   - __tsan_mutex_recursive_unlock
int SANITIZER_CDECL __tsan_mutex_pre_unlock(void *addr, unsigned flags);

// Annotate end of unlock operation.
// Supported flags:
//   - __tsan_mutex_read_lock (must match __tsan_mutex_pre_unlock)
void SANITIZER_CDECL __tsan_mutex_post_unlock(void *addr, unsigned flags);

// Annotate start/end of notify/signal/broadcast operation.
// Supported flags: none.
void SANITIZER_CDECL __tsan_mutex_pre_signal(void *addr, unsigned flags);
void SANITIZER_CDECL __tsan_mutex_post_signal(void *addr, unsigned flags);

// Annotate start/end of a region of code where lock/unlock/signal operation
// diverts to do something else unrelated to the mutex. This can be used to
// annotate, for example, calls into cooperative scheduler or contention
// profiling code.
// These annotations must be called only from within
// __tsan_mutex_pre/post_lock, __tsan_mutex_pre/post_unlock,
// __tsan_mutex_pre/post_signal regions.
// Supported flags: none.
void SANITIZER_CDECL __tsan_mutex_pre_divert(void *addr, unsigned flags);
void SANITIZER_CDECL __tsan_mutex_post_divert(void *addr, unsigned flags);

// Check that the current thread does not hold any mutexes,
// report a bug report otherwise.
void SANITIZER_CDECL __tsan_check_no_mutexes_held();

// External race detection API.
// Can be used by non-instrumented libraries to detect when their objects are
// being used in an unsafe manner.
//   - __tsan_external_read/__tsan_external_write annotates the logical reads
//       and writes of the object at the specified address. 'caller_pc' should
//       be the PC of the library user, which the library can obtain with e.g.
//       `__builtin_return_address(0)`.
//   - __tsan_external_register_tag registers a 'tag' with the specified name,
//       which is later used in read/write annotations to denote the object type
//   - __tsan_external_assign_tag can optionally mark a heap object with a tag
void *SANITIZER_CDECL __tsan_external_register_tag(const char *object_type);
void SANITIZER_CDECL __tsan_external_register_header(void *tag,
                                                     const char *header);
void SANITIZER_CDECL __tsan_external_assign_tag(void *addr, void *tag);
void SANITIZER_CDECL __tsan_external_read(void *addr, void *caller_pc,
                                          void *tag);
void SANITIZER_CDECL __tsan_external_write(void *addr, void *caller_pc,
                                           void *tag);

// Fiber switching API.
//   - TSAN context for fiber can be created by __tsan_create_fiber
//     and freed by __tsan_destroy_fiber.
//   - TSAN context of current fiber or thread can be obtained
//     by calling __tsan_get_current_fiber.
//   - __tsan_switch_to_fiber should be called immediately before switch
//     to fiber, such as call of swapcontext.
//   - Fiber name can be set by __tsan_set_fiber_name.
void *SANITIZER_CDECL __tsan_get_current_fiber(void);
void *SANITIZER_CDECL __tsan_create_fiber(unsigned flags);
void SANITIZER_CDECL __tsan_destroy_fiber(void *fiber);
void SANITIZER_CDECL __tsan_switch_to_fiber(void *fiber, unsigned flags);
void SANITIZER_CDECL __tsan_set_fiber_name(void *fiber, const char *name);

// Flags for __tsan_switch_to_fiber:
// Do not establish a happens-before relation between fibers
static const unsigned __tsan_switch_to_fiber_no_sync = 1 << 0;

// User-provided callback invoked on TSan initialization.
void SANITIZER_CDECL __tsan_on_initialize();

// User-provided callback invoked on TSan shutdown.
// `failed` - Nonzero if TSan did detect issues, zero otherwise.
// Return `0` if TSan should exit as if no issues were detected.  Return nonzero
// if TSan should exit as if issues were detected.
int SANITIZER_CDECL __tsan_on_finalize(int failed);

// Release TSan internal memory in a best-effort manner.
void SANITIZER_CDECL __tsan_flush_memory();

// User-provided default TSAN options.
const char *SANITIZER_CDECL __tsan_default_options(void);

// User-provided default TSAN suppressions.
const char *SANITIZER_CDECL __tsan_default_suppressions(void);

/// Returns a report's description.
///
/// Returns a report's description (issue type), number of duplicate issues
/// found, counts of array data (stack traces, memory operations, locations,
/// mutexes, threads, unique thread IDs) and a stack trace of a <c>sleep()</c>
/// call (if one was involved in the issue).
///
/// \param report Opaque pointer to the current report.
/// \param[out] description Report type description.
/// \param[out] count Count of duplicate issues.
/// \param[out] stack_count Count of stack traces.
/// \param[out] mop_count Count of memory operations.
/// \param[out] loc_count Count of locations.
/// \param[out] mutex_count Count of mutexes.
/// \param[out] thread_count Count of threads.
/// \param[out] unique_tid_count Count of unique thread IDs.
/// \param sleep_trace A buffer to store the stack trace of a <c>sleep()</c>
/// call.
/// \param trace_size Size in bytes of the trace buffer.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_data(
    void *report, const char **description, int *count, int *stack_count,
    int *mop_count, int *loc_count, int *mutex_count, int *thread_count,
    int *unique_tid_count, void **sleep_trace, unsigned long trace_size);

/// Returns information about stack traces included in the report.
///
/// \param report Opaque pointer to the current report.
/// \param idx Index to the report's stacks.
/// \param trace A buffer to store the stack trace.
/// \param trace_size Size in bytes of the trace buffer.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_stack(void *report, unsigned long idx,
                                            void **trace,
                                            unsigned long trace_size);

/// Returns information about memory operations included in the report.
///
/// \param report Opaque pointer to the current report.
/// \param idx Index to the report's memory operations.
/// \param[out] tid Thread ID of the memory operation.
/// \param[out] addr Address of the memory operation.
/// \param[out] size Size of the memory operation.
/// \param[out] write Write flag of the memory operation.
/// \param[out] atomic Atomicity flag of the memory operation.
/// \param trace A buffer to store the stack trace.
/// \param trace_size Size in bytes of the trace buffer.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_mop(void *report, unsigned long idx,
                                          int *tid, void **addr, int *size,
                                          int *write, int *atomic, void **trace,
                                          unsigned long trace_size);

/// Returns information about locations included in the report.
///
/// \param report Opaque pointer to the current report.
/// \param idx Index to the report's locations.
/// \param[out] type Type of the location.
/// \param[out] addr Address of the location.
/// \param[out] start Start of the location.
/// \param[out] size Size of the location.
/// \param[out] tid Thread ID of the location.
/// \param[out] fd File descriptor of the location.
/// \param[out] suppressable Suppressable flag.
/// \param trace A buffer to store the stack trace.
/// \param trace_size Size in bytes of the trace buffer.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_loc(void *report, unsigned long idx,
                                          const char **type, void **addr,
                                          void **start, unsigned long *size,
                                          int *tid, int *fd, int *suppressable,
                                          void **trace,
                                          unsigned long trace_size);

/// Returns information about mutexes included in the report.
///
/// \param report Opaque pointer to the current report.
/// \param idx Index to the report's mutexes.
/// \param[out] mutex_id Id of the mutex.
/// \param[out] addr Address of the mutex.
/// \param[out] destroyed Destroyed mutex flag.
/// \param trace A buffer to store the stack trace.
/// \param trace_size Size in bytes of the trace buffer.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_mutex(void *report, unsigned long idx,
                                            uint64_t *mutex_id, void **addr,
                                            int *destroyed, void **trace,
                                            unsigned long trace_size);

/// Returns information about threads included in the report.
///
/// \param report Opaque pointer to the current report.
/// \param idx Index to the report's threads.
/// \param[out] tid Thread ID of the thread.
/// \param[out] os_id Operating system's ID of the thread.
/// \param[out] running Running flag of the thread.
/// \param[out] name Name of the thread.
/// \param[out] parent_tid ID of the parent thread.
/// \param trace A buffer to store the stack trace.
/// \param trace_size Size in bytes of the trace buffer.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_thread(void *report, unsigned long idx,
                                             int *tid, uint64_t *os_id,
                                             int *running, const char **name,
                                             int *parent_tid, void **trace,
                                             unsigned long trace_size);

/// Returns information about unique thread IDs included in the report.
///
/// \param report Opaque pointer to the current report.
/// \param idx Index to the report's unique thread IDs.
/// \param[out] tid Unique thread ID of the report.
/// \returns Returns 1 if successful, 0 if not.
int SANITIZER_CDECL __tsan_get_report_unique_tid(void *report,
                                                 unsigned long idx, int *tid);

/// Returns the current report.
///
/// If TSan is currently reporting a detected issue on the current thread,
/// returns an opaque pointer to the current report. Otherwise returns NULL.
/// \returns An opaque pointer to the current report. Otherwise returns NULL.
void *SANITIZER_CDECL __tsan_get_current_report();

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_TSAN_INTERFACE_H
