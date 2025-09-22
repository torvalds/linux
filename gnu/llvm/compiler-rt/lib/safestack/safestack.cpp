//===-- safestack.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the runtime support for the safe stack protection
// mechanism. The runtime manages allocation/deallocation of the unsafe stack
// for the main thread, as well as all pthreads that are created/destroyed
// during program execution.
//
//===----------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "safestack_platform.h"
#include "safestack_util.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#include <errno.h>
#include <string.h>
#include <sys/resource.h>

#include "interception/interception.h"

// interception.h drags in sanitizer_redefine_builtins.h, which in turn
// creates references to __sanitizer_internal_memcpy etc.  The interceptors
// aren't needed here, so just forward to libc.
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE void *__sanitizer_internal_memcpy(void *dest,
                                                                const void *src,
                                                                size_t n) {
  return memcpy(dest, src, n);
}

SANITIZER_INTERFACE_ATTRIBUTE void *__sanitizer_internal_memmove(
    void *dest, const void *src, size_t n) {
  return memmove(dest, src, n);
}

SANITIZER_INTERFACE_ATTRIBUTE void *__sanitizer_internal_memset(void *s, int c,
                                                                size_t n) {
  return memset(s, c, n);
}
}  // extern "C"

using namespace safestack;

// TODO: To make accessing the unsafe stack pointer faster, we plan to
// eventually store it directly in the thread control block data structure on
// platforms where this structure is pointed to by %fs or %gs. This is exactly
// the same mechanism as currently being used by the traditional stack
// protector pass to store the stack guard (see getStackCookieLocation()
// function above). Doing so requires changing the tcbhead_t struct in glibc
// on Linux and tcb struct in libc on FreeBSD.
//
// For now, store it in a thread-local variable.
extern "C" {
__attribute__((visibility(
    "default"))) __thread void *__safestack_unsafe_stack_ptr = nullptr;
}

namespace {

// TODO: The runtime library does not currently protect the safe stack beyond
// relying on the system-enforced ASLR. The protection of the (safe) stack can
// be provided by three alternative features:
//
// 1) Protection via hardware segmentation on x86-32 and some x86-64
// architectures: the (safe) stack segment (implicitly accessed via the %ss
// segment register) can be separated from the data segment (implicitly
// accessed via the %ds segment register). Dereferencing a pointer to the safe
// segment would result in a segmentation fault.
//
// 2) Protection via software fault isolation: memory writes that are not meant
// to access the safe stack can be prevented from doing so through runtime
// instrumentation. One way to do it is to allocate the safe stack(s) in the
// upper half of the userspace and bitmask the corresponding upper bit of the
// memory addresses of memory writes that are not meant to access the safe
// stack.
//
// 3) Protection via information hiding on 64 bit architectures: the location
// of the safe stack(s) can be randomized through secure mechanisms, and the
// leakage of the stack pointer can be prevented. Currently, libc can leak the
// stack pointer in several ways (e.g. in longjmp, signal handling, user-level
// context switching related functions, etc.). These can be fixed in libc and
// in other low-level libraries, by either eliminating the escaping/dumping of
// the stack pointer (i.e., %rsp) when that's possible, or by using
// encryption/PTR_MANGLE (XOR-ing the dumped stack pointer with another secret
// we control and protect better, as is already done for setjmp in glibc.)
// Furthermore, a static machine code level verifier can be ran after code
// generation to make sure that the stack pointer is never written to memory,
// or if it is, its written on the safe stack.
//
// Finally, while the Unsafe Stack pointer is currently stored in a thread
// local variable, with libc support it could be stored in the TCB (thread
// control block) as well, eliminating another level of indirection and making
// such accesses faster. Alternatively, dedicating a separate register for
// storing it would also be possible.

/// Minimum stack alignment for the unsafe stack.
const unsigned kStackAlign = 16;

/// Default size of the unsafe stack. This value is only used if the stack
/// size rlimit is set to infinity.
const unsigned kDefaultUnsafeStackSize = 0x2800000;

// Per-thread unsafe stack information. It's not frequently accessed, so there
// it can be kept out of the tcb in normal thread-local variables.
__thread void *unsafe_stack_start = nullptr;
__thread size_t unsafe_stack_size = 0;
__thread size_t unsafe_stack_guard = 0;

inline void *unsafe_stack_alloc(size_t size, size_t guard) {
  SFS_CHECK(size + guard >= size);
  void *addr = Mmap(nullptr, size + guard, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON, -1, 0);
  SFS_CHECK(MAP_FAILED != addr);
  Mprotect(addr, guard, PROT_NONE);
  return (char *)addr + guard;
}

inline void unsafe_stack_setup(void *start, size_t size, size_t guard) {
  SFS_CHECK((char *)start + size >= (char *)start);
  SFS_CHECK((char *)start + guard >= (char *)start);
  void *stack_ptr = (char *)start + size;
  SFS_CHECK((((size_t)stack_ptr) & (kStackAlign - 1)) == 0);

  __safestack_unsafe_stack_ptr = stack_ptr;
  unsafe_stack_start = start;
  unsafe_stack_size = size;
  unsafe_stack_guard = guard;
}

/// Thread data for the cleanup handler
pthread_key_t thread_cleanup_key;

/// Safe stack per-thread information passed to the thread_start function
struct tinfo {
  void *(*start_routine)(void *);
  void *start_routine_arg;

  void *unsafe_stack_start;
  size_t unsafe_stack_size;
  size_t unsafe_stack_guard;
};

/// Wrap the thread function in order to deallocate the unsafe stack when the
/// thread terminates by returning from its main function.
void *thread_start(void *arg) {
  struct tinfo *tinfo = (struct tinfo *)arg;

  void *(*start_routine)(void *) = tinfo->start_routine;
  void *start_routine_arg = tinfo->start_routine_arg;

  // Setup the unsafe stack; this will destroy tinfo content
  unsafe_stack_setup(tinfo->unsafe_stack_start, tinfo->unsafe_stack_size,
                     tinfo->unsafe_stack_guard);

  // Make sure out thread-specific destructor will be called
  pthread_setspecific(thread_cleanup_key, (void *)1);

  return start_routine(start_routine_arg);
}

/// Linked list used to store exiting threads stack/thread information.
struct thread_stack_ll {
  struct thread_stack_ll *next;
  void *stack_base;
  size_t size;
  pid_t pid;
  ThreadId tid;
};

/// Linked list of unsafe stacks for threads that are exiting. We delay
/// unmapping them until the thread exits.
thread_stack_ll *thread_stacks = nullptr;
pthread_mutex_t thread_stacks_mutex = PTHREAD_MUTEX_INITIALIZER;

/// Thread-specific data destructor. We want to free the unsafe stack only after
/// this thread is terminated. libc can call functions in safestack-instrumented
/// code (like free) after thread-specific data destructors have run.
void thread_cleanup_handler(void *_iter) {
  SFS_CHECK(unsafe_stack_start != nullptr);
  pthread_setspecific(thread_cleanup_key, NULL);

  pthread_mutex_lock(&thread_stacks_mutex);
  // Temporary list to hold the previous threads stacks so we don't hold the
  // thread_stacks_mutex for long.
  thread_stack_ll *temp_stacks = thread_stacks;
  thread_stacks = nullptr;
  pthread_mutex_unlock(&thread_stacks_mutex);

  pid_t pid = getpid();
  ThreadId tid = GetTid();

  // Free stacks for dead threads
  thread_stack_ll **stackp = &temp_stacks;
  while (*stackp) {
    thread_stack_ll *stack = *stackp;
    if (stack->pid != pid ||
        (-1 == TgKill(stack->pid, stack->tid, 0) && errno == ESRCH)) {
      Munmap(stack->stack_base, stack->size);
      *stackp = stack->next;
      free(stack);
    } else
      stackp = &stack->next;
  }

  thread_stack_ll *cur_stack =
      (thread_stack_ll *)malloc(sizeof(thread_stack_ll));
  cur_stack->stack_base = (char *)unsafe_stack_start - unsafe_stack_guard;
  cur_stack->size = unsafe_stack_size + unsafe_stack_guard;
  cur_stack->pid = pid;
  cur_stack->tid = tid;

  pthread_mutex_lock(&thread_stacks_mutex);
  // Merge thread_stacks with the current thread's stack and any remaining
  // temp_stacks
  *stackp = thread_stacks;
  cur_stack->next = temp_stacks;
  thread_stacks = cur_stack;
  pthread_mutex_unlock(&thread_stacks_mutex);

  unsafe_stack_start = nullptr;
}

void EnsureInterceptorsInitialized();

/// Intercept thread creation operation to allocate and setup the unsafe stack
INTERCEPTOR(int, pthread_create, pthread_t *thread,
            const pthread_attr_t *attr,
            void *(*start_routine)(void*), void *arg) {
  EnsureInterceptorsInitialized();
  size_t size = 0;
  size_t guard = 0;

  if (attr) {
    pthread_attr_getstacksize(attr, &size);
    pthread_attr_getguardsize(attr, &guard);
  } else {
    // get pthread default stack size
    pthread_attr_t tmpattr;
    pthread_attr_init(&tmpattr);
    pthread_attr_getstacksize(&tmpattr, &size);
    pthread_attr_getguardsize(&tmpattr, &guard);
    pthread_attr_destroy(&tmpattr);
  }

#if SANITIZER_SOLARIS
  // Solaris pthread_attr_init initializes stacksize to 0 (the default), so
  // hardcode the actual values as documented in pthread_create(3C).
  if (size == 0)
#  if defined(_LP64)
    size = 2 * 1024 * 1024;
#  else
    size = 1024 * 1024;
#  endif
#endif

  SFS_CHECK(size);
  size = RoundUpTo(size, kStackAlign);

  void *addr = unsafe_stack_alloc(size, guard);
  // Put tinfo at the end of the buffer. guard may be not page aligned.
  // If that is so then some bytes after addr can be mprotected.
  struct tinfo *tinfo =
      (struct tinfo *)(((char *)addr) + size - sizeof(struct tinfo));
  tinfo->start_routine = start_routine;
  tinfo->start_routine_arg = arg;
  tinfo->unsafe_stack_start = addr;
  tinfo->unsafe_stack_size = size;
  tinfo->unsafe_stack_guard = guard;

  return REAL(pthread_create)(thread, attr, thread_start, tinfo);
}

pthread_mutex_t interceptor_init_mutex = PTHREAD_MUTEX_INITIALIZER;
bool interceptors_inited = false;

void EnsureInterceptorsInitialized() {
  MutexLock lock(interceptor_init_mutex);
  if (interceptors_inited)
    return;

  // Initialize pthread interceptors for thread allocation
  INTERCEPT_FUNCTION(pthread_create);

  interceptors_inited = true;
}

}  // namespace

extern "C" __attribute__((visibility("default")))
#if !SANITIZER_CAN_USE_PREINIT_ARRAY
// On ELF platforms, the constructor is invoked using .preinit_array (see below)
__attribute__((constructor(0)))
#endif
void __safestack_init() {
  // Determine the stack size for the main thread.
  size_t size = kDefaultUnsafeStackSize;
  size_t guard = 4096;

  struct rlimit limit;
  if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
    size = limit.rlim_cur;

  // Allocate unsafe stack for main thread
  void *addr = unsafe_stack_alloc(size, guard);
  unsafe_stack_setup(addr, size, guard);

  // Setup the cleanup handler
  pthread_key_create(&thread_cleanup_key, thread_cleanup_handler);
}

#if SANITIZER_CAN_USE_PREINIT_ARRAY
// On ELF platforms, run safestack initialization before any other constructors.
// On other platforms we use the constructor attribute to arrange to run our
// initialization early.
extern "C" {
__attribute__((section(".preinit_array"),
               used)) void (*__safestack_preinit)(void) = __safestack_init;
}
#endif

extern "C"
    __attribute__((visibility("default"))) void *__get_unsafe_stack_bottom() {
  return unsafe_stack_start;
}

extern "C"
    __attribute__((visibility("default"))) void *__get_unsafe_stack_top() {
  return (char*)unsafe_stack_start + unsafe_stack_size;
}

extern "C"
    __attribute__((visibility("default"))) void *__get_unsafe_stack_start() {
  return unsafe_stack_start;
}

extern "C"
    __attribute__((visibility("default"))) void *__get_unsafe_stack_ptr() {
  return __safestack_unsafe_stack_ptr;
}
