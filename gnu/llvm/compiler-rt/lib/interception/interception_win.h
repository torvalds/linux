//===-- interception_linux.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Windows-specific interception methods.
//===----------------------------------------------------------------------===//

#if SANITIZER_WINDOWS

#if !defined(INCLUDED_FROM_INTERCEPTION_LIB)
# error "interception_win.h should be included from interception library only"
#endif

#ifndef INTERCEPTION_WIN_H
#define INTERCEPTION_WIN_H

namespace __interception {
// All the functions in the OverrideFunction() family return true on success,
// false on failure (including "couldn't find the function").

// Overrides a function by its address.
bool OverrideFunction(uptr old_func, uptr new_func, uptr *orig_old_func = 0);

// Overrides a function in a system DLL or DLL CRT by its exported name.
bool OverrideFunction(const char *name, uptr new_func, uptr *orig_old_func = 0);

// Windows-only replacement for GetProcAddress. Useful for some sanitizers.
uptr InternalGetProcAddress(void *module, const char *func_name);

// Overrides a function only when it is called from a specific DLL. For example,
// this is used to override calls to HeapAlloc/HeapFree from ucrtbase without
// affecting other third party libraries.
bool OverrideImportedFunction(const char *module_to_patch,
                              const char *imported_module,
                              const char *function_name, uptr new_function,
                              uptr *orig_old_func);

// Sets a callback to be used for reporting errors by interception_win. The
// callback will be called with printf-like arguments. Intended to be used with
// __sanitizer::Report. Pass nullptr to disable error reporting (default).
void SetErrorReportCallback(void (*callback)(const char *format, ...));

#if !SANITIZER_WINDOWS64
// Exposed for unittests
bool OverrideFunctionWithDetour(
    uptr old_func, uptr new_func, uptr *orig_old_func);
#endif

// Exposed for unittests
bool OverrideFunctionWithRedirectJump(
    uptr old_func, uptr new_func, uptr *orig_old_func);
bool OverrideFunctionWithHotPatch(
    uptr old_func, uptr new_func, uptr *orig_old_func);
bool OverrideFunctionWithTrampoline(
    uptr old_func, uptr new_func, uptr *orig_old_func);

// Exposed for unittests
void TestOnlyReleaseTrampolineRegions();

}  // namespace __interception

#if defined(INTERCEPTION_DYNAMIC_CRT)
#define INTERCEPT_FUNCTION_WIN(func)                                           \
  ::__interception::OverrideFunction(#func,                                    \
                                     (::__interception::uptr)WRAP(func),       \
                                     (::__interception::uptr *)&REAL(func))
#else
#define INTERCEPT_FUNCTION_WIN(func)                                           \
  ::__interception::OverrideFunction((::__interception::uptr)func,             \
                                     (::__interception::uptr)WRAP(func),       \
                                     (::__interception::uptr *)&REAL(func))
#endif

#define INTERCEPT_FUNCTION_VER_WIN(func, symver) INTERCEPT_FUNCTION_WIN(func)

#define INTERCEPT_FUNCTION_DLLIMPORT(user_dll, provider_dll, func)       \
  ::__interception::OverrideImportedFunction(                            \
      user_dll, provider_dll, #func, (::__interception::uptr)WRAP(func), \
      (::__interception::uptr *)&REAL(func))

#endif  // INTERCEPTION_WIN_H
#endif  // SANITIZER_WINDOWS
