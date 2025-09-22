//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__thread/support/windows.h>
#include <chrono>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <fibersapi.h>

_LIBCPP_BEGIN_NAMESPACE_STD

static_assert(sizeof(__libcpp_mutex_t) == sizeof(SRWLOCK), "");
static_assert(alignof(__libcpp_mutex_t) == alignof(SRWLOCK), "");

static_assert(sizeof(__libcpp_recursive_mutex_t) == sizeof(CRITICAL_SECTION), "");
static_assert(alignof(__libcpp_recursive_mutex_t) == alignof(CRITICAL_SECTION), "");

static_assert(sizeof(__libcpp_condvar_t) == sizeof(CONDITION_VARIABLE), "");
static_assert(alignof(__libcpp_condvar_t) == alignof(CONDITION_VARIABLE), "");

static_assert(sizeof(__libcpp_exec_once_flag) == sizeof(INIT_ONCE), "");
static_assert(alignof(__libcpp_exec_once_flag) == alignof(INIT_ONCE), "");

static_assert(sizeof(__libcpp_thread_id) == sizeof(DWORD), "");
static_assert(alignof(__libcpp_thread_id) == alignof(DWORD), "");

static_assert(sizeof(__libcpp_thread_t) == sizeof(HANDLE), "");
static_assert(alignof(__libcpp_thread_t) == alignof(HANDLE), "");

static_assert(sizeof(__libcpp_tls_key) == sizeof(DWORD), "");
static_assert(alignof(__libcpp_tls_key) == alignof(DWORD), "");

// Mutex
int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t* __m) {
  InitializeCriticalSection((LPCRITICAL_SECTION)__m);
  return 0;
}

int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t* __m) {
  EnterCriticalSection((LPCRITICAL_SECTION)__m);
  return 0;
}

bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t* __m) {
  return TryEnterCriticalSection((LPCRITICAL_SECTION)__m) != 0;
}

int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t* __m) {
  LeaveCriticalSection((LPCRITICAL_SECTION)__m);
  return 0;
}

int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t* __m) {
  DeleteCriticalSection((LPCRITICAL_SECTION)__m);
  return 0;
}

int __libcpp_mutex_lock(__libcpp_mutex_t* __m) {
  AcquireSRWLockExclusive((PSRWLOCK)__m);
  return 0;
}

bool __libcpp_mutex_trylock(__libcpp_mutex_t* __m) { return TryAcquireSRWLockExclusive((PSRWLOCK)__m) != 0; }

int __libcpp_mutex_unlock(__libcpp_mutex_t* __m) {
  ReleaseSRWLockExclusive((PSRWLOCK)__m);
  return 0;
}

int __libcpp_mutex_destroy(__libcpp_mutex_t* __m) {
  static_cast<void>(__m);
  return 0;
}

// Condition Variable
int __libcpp_condvar_signal(__libcpp_condvar_t* __cv) {
  WakeConditionVariable((PCONDITION_VARIABLE)__cv);
  return 0;
}

int __libcpp_condvar_broadcast(__libcpp_condvar_t* __cv) {
  WakeAllConditionVariable((PCONDITION_VARIABLE)__cv);
  return 0;
}

int __libcpp_condvar_wait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m) {
  SleepConditionVariableSRW((PCONDITION_VARIABLE)__cv, (PSRWLOCK)__m, INFINITE, 0);
  return 0;
}

int __libcpp_condvar_timedwait(__libcpp_condvar_t* __cv, __libcpp_mutex_t* __m, __libcpp_timespec_t* __ts) {
  using namespace std::chrono;

  auto duration   = seconds(__ts->tv_sec) + nanoseconds(__ts->tv_nsec);
  auto abstime    = system_clock::time_point(duration_cast<system_clock::duration>(duration));
  auto timeout_ms = duration_cast<milliseconds>(abstime - system_clock::now());

  if (!SleepConditionVariableSRW(
          (PCONDITION_VARIABLE)__cv, (PSRWLOCK)__m, timeout_ms.count() > 0 ? timeout_ms.count() : 0, 0)) {
    auto __ec = GetLastError();
    return __ec == ERROR_TIMEOUT ? ETIMEDOUT : __ec;
  }
  return 0;
}

int __libcpp_condvar_destroy(__libcpp_condvar_t* __cv) {
  static_cast<void>(__cv);
  return 0;
}

// Execute Once
static inline _LIBCPP_HIDE_FROM_ABI BOOL CALLBACK
__libcpp_init_once_execute_once_thunk(PINIT_ONCE __init_once, PVOID __parameter, PVOID* __context) {
  static_cast<void>(__init_once);
  static_cast<void>(__context);

  void (*init_routine)(void) = reinterpret_cast<void (*)(void)>(__parameter);
  init_routine();
  return TRUE;
}

int __libcpp_execute_once(__libcpp_exec_once_flag* __flag, void (*__init_routine)(void)) {
  if (!InitOnceExecuteOnce(
          (PINIT_ONCE)__flag, __libcpp_init_once_execute_once_thunk, reinterpret_cast<void*>(__init_routine), NULL))
    return GetLastError();
  return 0;
}

// Thread ID
bool __libcpp_thread_id_equal(__libcpp_thread_id __lhs, __libcpp_thread_id __rhs) { return __lhs == __rhs; }

bool __libcpp_thread_id_less(__libcpp_thread_id __lhs, __libcpp_thread_id __rhs) { return __lhs < __rhs; }

// Thread
struct __libcpp_beginthreadex_thunk_data {
  void* (*__func)(void*);
  void* __arg;
};

static inline _LIBCPP_HIDE_FROM_ABI unsigned WINAPI __libcpp_beginthreadex_thunk(void* __raw_data) {
  auto* __data = static_cast<__libcpp_beginthreadex_thunk_data*>(__raw_data);
  auto* __func = __data->__func;
  void* __arg  = __data->__arg;
  delete __data;
  return static_cast<unsigned>(reinterpret_cast<uintptr_t>(__func(__arg)));
}

bool __libcpp_thread_isnull(const __libcpp_thread_t* __t) { return *__t == 0; }

int __libcpp_thread_create(__libcpp_thread_t* __t, void* (*__func)(void*), void* __arg) {
  auto* __data   = new __libcpp_beginthreadex_thunk_data;
  __data->__func = __func;
  __data->__arg  = __arg;

  *__t = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, __libcpp_beginthreadex_thunk, __data, 0, nullptr));

  if (*__t)
    return 0;
  return GetLastError();
}

__libcpp_thread_id __libcpp_thread_get_current_id() { return GetCurrentThreadId(); }

__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t* __t) { return GetThreadId(*__t); }

int __libcpp_thread_join(__libcpp_thread_t* __t) {
  if (WaitForSingleObjectEx(*__t, INFINITE, FALSE) == WAIT_FAILED)
    return GetLastError();
  if (!CloseHandle(*__t))
    return GetLastError();
  return 0;
}

int __libcpp_thread_detach(__libcpp_thread_t* __t) {
  if (!CloseHandle(*__t))
    return GetLastError();
  return 0;
}

void __libcpp_thread_yield() { SwitchToThread(); }

void __libcpp_thread_sleep_for(const chrono::nanoseconds& __ns) {
  // round-up to the nearest millisecond
  chrono::milliseconds __ms = chrono::ceil<chrono::milliseconds>(__ns);
  // FIXME(compnerd) this should be an alertable sleep (WFSO or SleepEx)
  Sleep(__ms.count());
}

// Thread Local Storage
int __libcpp_tls_create(__libcpp_tls_key* __key, void(_LIBCPP_TLS_DESTRUCTOR_CC* __at_exit)(void*)) {
  DWORD index = FlsAlloc(__at_exit);
  if (index == FLS_OUT_OF_INDEXES)
    return GetLastError();
  *__key = index;
  return 0;
}

void* __libcpp_tls_get(__libcpp_tls_key __key) { return FlsGetValue(__key); }

int __libcpp_tls_set(__libcpp_tls_key __key, void* __p) {
  if (!FlsSetValue(__key, __p))
    return GetLastError();
  return 0;
}

_LIBCPP_END_NAMESPACE_STD
