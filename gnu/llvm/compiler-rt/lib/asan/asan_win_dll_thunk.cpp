//===-- asan_win_dll_thunk.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// This file defines a family of thunks that should be statically linked into
// the DLLs that have ASan instrumentation in order to delegate the calls to the
// shared runtime that lives in the main binary.
// See https://github.com/google/sanitizers/issues/209 for the details.
//===----------------------------------------------------------------------===//

#ifdef SANITIZER_DLL_THUNK
#include "asan_init_version.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_win_defs.h"
#include "sanitizer_common/sanitizer_win_dll_thunk.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

// ASan own interface functions.
#define INTERFACE_FUNCTION(Name) INTERCEPT_SANITIZER_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)
#include "asan_interface.inc"

// Memory allocation functions.
INTERCEPT_WRAP_V_W(free)
INTERCEPT_WRAP_V_W(_free_base)
INTERCEPT_WRAP_V_WW(_free_dbg)

INTERCEPT_WRAP_W_W(malloc)
INTERCEPT_WRAP_W_W(_malloc_base)
INTERCEPT_WRAP_W_WWWW(_malloc_dbg)

INTERCEPT_WRAP_W_WW(calloc)
INTERCEPT_WRAP_W_WW(_calloc_base)
INTERCEPT_WRAP_W_WWWWW(_calloc_dbg)
INTERCEPT_WRAP_W_WWW(_calloc_impl)

INTERCEPT_WRAP_W_WW(realloc)
INTERCEPT_WRAP_W_WW(_realloc_base)
INTERCEPT_WRAP_W_WWW(_realloc_dbg)
INTERCEPT_WRAP_W_WWW(_recalloc)
INTERCEPT_WRAP_W_WWW(_recalloc_base)

INTERCEPT_WRAP_W_W(_msize)
INTERCEPT_WRAP_W_W(_msize_base)
INTERCEPT_WRAP_W_W(_expand)
INTERCEPT_WRAP_W_W(_expand_dbg)

// TODO(timurrrr): Might want to add support for _aligned_* allocation
// functions to detect a bit more bugs.  Those functions seem to wrap malloc().

// TODO(timurrrr): Do we need to add _Crt* stuff here? (see asan_malloc_win.cpp)

#  if defined(_MSC_VER) && !defined(__clang__)
// Disable warnings such as: 'void memchr(void)': incorrect number of arguments
// for intrinsic function, expected '3' arguments.
#    pragma warning(push)
#    pragma warning(disable : 4392)
#  endif

INTERCEPT_LIBRARY_FUNCTION(atoi);
INTERCEPT_LIBRARY_FUNCTION(atol);
INTERCEPT_LIBRARY_FUNCTION(atoll);
INTERCEPT_LIBRARY_FUNCTION(frexp);
INTERCEPT_LIBRARY_FUNCTION(longjmp);
#if SANITIZER_INTERCEPT_MEMCHR
INTERCEPT_LIBRARY_FUNCTION(memchr);
#endif
INTERCEPT_LIBRARY_FUNCTION(memcmp);
INTERCEPT_LIBRARY_FUNCTION(memcpy);
INTERCEPT_LIBRARY_FUNCTION(memmove);
INTERCEPT_LIBRARY_FUNCTION(memset);
INTERCEPT_LIBRARY_FUNCTION(strcat);
INTERCEPT_LIBRARY_FUNCTION(strchr);
INTERCEPT_LIBRARY_FUNCTION(strcmp);
INTERCEPT_LIBRARY_FUNCTION(strcpy);
INTERCEPT_LIBRARY_FUNCTION(strcspn);
INTERCEPT_LIBRARY_FUNCTION(_strdup);
INTERCEPT_LIBRARY_FUNCTION(strlen);
INTERCEPT_LIBRARY_FUNCTION(strncat);
INTERCEPT_LIBRARY_FUNCTION(strncmp);
INTERCEPT_LIBRARY_FUNCTION(strncpy);
INTERCEPT_LIBRARY_FUNCTION(strnlen);
INTERCEPT_LIBRARY_FUNCTION(strpbrk);
INTERCEPT_LIBRARY_FUNCTION(strrchr);
INTERCEPT_LIBRARY_FUNCTION(strspn);
INTERCEPT_LIBRARY_FUNCTION(strstr);
INTERCEPT_LIBRARY_FUNCTION(strtok);
INTERCEPT_LIBRARY_FUNCTION(strtol);
INTERCEPT_LIBRARY_FUNCTION(strtoll);
INTERCEPT_LIBRARY_FUNCTION(wcslen);
INTERCEPT_LIBRARY_FUNCTION(wcsnlen);

#  if defined(_MSC_VER) && !defined(__clang__)
#    pragma warning(pop)
#  endif

#ifdef _WIN64
INTERCEPT_LIBRARY_FUNCTION(__C_specific_handler);
#else
INTERCEPT_LIBRARY_FUNCTION(_except_handler3);
// _except_handler4 checks -GS cookie which is different for each module, so we
// can't use INTERCEPT_LIBRARY_FUNCTION(_except_handler4).
INTERCEPTOR(int, _except_handler4, void *a, void *b, void *c, void *d) {
  __asan_handle_no_return();
  return REAL(_except_handler4)(a, b, c, d);
}
#endif

// Windows specific functions not included in asan_interface.inc.
INTERCEPT_WRAP_W_V(__asan_should_detect_stack_use_after_return)
INTERCEPT_WRAP_W_V(__asan_get_shadow_memory_dynamic_address)
INTERCEPT_WRAP_W_W(__asan_unhandled_exception_filter)

using namespace __sanitizer;

extern "C" {
int __asan_option_detect_stack_use_after_return;
uptr __asan_shadow_memory_dynamic_address;
} // extern "C"

static int asan_dll_thunk_init() {
  typedef void (*fntype)();
  static fntype fn = 0;
  // asan_dll_thunk_init is expected to be called by only one thread.
  if (fn) return 0;

  // Ensure all interception was executed.
  __dll_thunk_init();

  fn = (fntype) dllThunkGetRealAddrOrDie("__asan_init");
  fn();
  __asan_option_detect_stack_use_after_return =
      (__asan_should_detect_stack_use_after_return() != 0);
  __asan_shadow_memory_dynamic_address =
      (uptr)__asan_get_shadow_memory_dynamic_address();

#ifndef _WIN64
  INTERCEPT_FUNCTION(_except_handler4);
#endif
  // In DLLs, the callbacks are expected to return 0,
  // otherwise CRT initialization fails.
  return 0;
}

#pragma section(".CRT$XIB", long, read)
__declspec(allocate(".CRT$XIB")) int (*__asan_preinit)() = asan_dll_thunk_init;

static void WINAPI asan_thread_init(void *mod, unsigned long reason,
                                    void *reserved) {
  if (reason == /*DLL_PROCESS_ATTACH=*/1) asan_dll_thunk_init();
}

#pragma section(".CRT$XLAB", long, read)
__declspec(allocate(".CRT$XLAB")) void (WINAPI *__asan_tls_init)(void *,
    unsigned long, void *) = asan_thread_init;

WIN_FORCE_LINK(__asan_dso_reg_hook)

#endif // SANITIZER_DLL_THUNK
