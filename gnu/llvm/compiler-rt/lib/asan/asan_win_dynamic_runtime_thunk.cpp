//===-- asan_win_dynamic_runtime_thunk.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file defines things that need to be present in the application modules
// to interact with the ASan DLL runtime correctly and can't be implemented
// using the default "import library" generated when linking the DLL RTL.
//
// This includes:
//  - creating weak aliases to default implementation imported from asan dll.
//  - forwarding the detect_stack_use_after_return runtime option
//  - working around deficiencies of the MD runtime
//  - installing a custom SEH handler
//
//===----------------------------------------------------------------------===//

#ifdef SANITIZER_DYNAMIC_RUNTIME_THUNK
#define SANITIZER_IMPORT_INTERFACE 1
#include "sanitizer_common/sanitizer_win_defs.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Define weak alias for all weak functions imported from asan dll.
#define INTERFACE_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) WIN_WEAK_IMPORT_DEF(Name)
#include "asan_interface.inc"

// First, declare CRT sections we'll be using in this file
#pragma section(".CRT$XIB", long, read)
#pragma section(".CRT$XID", long, read)
#pragma section(".CRT$XCAB", long, read)
#pragma section(".CRT$XTW", long, read)
#pragma section(".CRT$XTY", long, read)
#pragma section(".CRT$XLAB", long, read)

////////////////////////////////////////////////////////////////////////////////
// Define a copy of __asan_option_detect_stack_use_after_return that should be
// used when linking an MD runtime with a set of object files on Windows.
//
// The ASan MD runtime dllexports '__asan_option_detect_stack_use_after_return',
// so normally we would just dllimport it.  Unfortunately, the dllimport
// attribute adds __imp_ prefix to the symbol name of a variable.
// Since in general we don't know if a given TU is going to be used
// with a MT or MD runtime and we don't want to use ugly __imp_ names on Windows
// just to work around this issue, let's clone the variable that is constant
// after initialization anyways.
extern "C" {
__declspec(dllimport) int __asan_should_detect_stack_use_after_return();
int __asan_option_detect_stack_use_after_return;

__declspec(dllimport) void* __asan_get_shadow_memory_dynamic_address();
void* __asan_shadow_memory_dynamic_address;
}

static int InitializeClonedVariables() {
  __asan_option_detect_stack_use_after_return =
    __asan_should_detect_stack_use_after_return();
  __asan_shadow_memory_dynamic_address =
    __asan_get_shadow_memory_dynamic_address();
  return 0;
}

static void NTAPI asan_thread_init(void *mod, unsigned long reason,
    void *reserved) {
  if (reason == DLL_PROCESS_ATTACH) InitializeClonedVariables();
}

// Our cloned variables must be initialized before C/C++ constructors.  If TLS
// is used, our .CRT$XLAB initializer will run first. If not, our .CRT$XIB
// initializer is needed as a backup.
__declspec(allocate(".CRT$XIB")) int (*__asan_initialize_cloned_variables)() =
    InitializeClonedVariables;
__declspec(allocate(".CRT$XLAB")) void (NTAPI *__asan_tls_init)(void *,
    unsigned long, void *) = asan_thread_init;

////////////////////////////////////////////////////////////////////////////////
// For some reason, the MD CRT doesn't call the C/C++ terminators during on DLL
// unload or on exit.  ASan relies on LLVM global_dtors to call
// __asan_unregister_globals on these events, which unfortunately doesn't work
// with the MD runtime, see PR22545 for the details.
// To work around this, for each DLL we schedule a call to UnregisterGlobals
// using atexit() that calls a small subset of C terminators
// where LLVM global_dtors is placed.  Fingers crossed, no other C terminators
// are there.
extern "C" int __cdecl atexit(void (__cdecl *f)(void));
extern "C" void __cdecl _initterm(void *a, void *b);

namespace {
__declspec(allocate(".CRT$XTW")) void* before_global_dtors = 0;
__declspec(allocate(".CRT$XTY")) void* after_global_dtors = 0;

void UnregisterGlobals() {
  _initterm(&before_global_dtors, &after_global_dtors);
}

int ScheduleUnregisterGlobals() {
  return atexit(UnregisterGlobals);
}
}  // namespace

// We need to call 'atexit(UnregisterGlobals);' as early as possible, but after
// atexit() is initialized (.CRT$XIC).  As this is executed before C++
// initializers (think ctors for globals), UnregisterGlobals gets executed after
// dtors for C++ globals.
__declspec(allocate(".CRT$XID"))
int (*__asan_schedule_unregister_globals)() = ScheduleUnregisterGlobals;

////////////////////////////////////////////////////////////////////////////////
// ASan SEH handling.
// We need to set the ASan-specific SEH handler at the end of CRT initialization
// of each module (see also asan_win.cpp).
extern "C" {
__declspec(dllimport) int __asan_set_seh_filter();
static int SetSEHFilter() { return __asan_set_seh_filter(); }

// Unfortunately, putting a pointer to __asan_set_seh_filter into
// __asan_intercept_seh gets optimized out, so we have to use an extra function.
__declspec(allocate(".CRT$XCAB")) int (*__asan_seh_interceptor)() =
    SetSEHFilter;
}

WIN_FORCE_LINK(__asan_dso_reg_hook)

#endif // SANITIZER_DYNAMIC_RUNTIME_THUNK
