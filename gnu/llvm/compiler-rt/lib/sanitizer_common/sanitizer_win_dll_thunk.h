//===-- sanitizer_win_dll_thunk.h -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This header provide helper macros to delegate calls to the shared runtime
// that lives in the main executable. It should be included to dll_thunks that
// will be linked to the dlls, when the sanitizer is a static library included
// in the main executable.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_WIN_DLL_THUNK_H
#define SANITIZER_WIN_DLL_THUNK_H
#include "sanitizer_internal_defs.h"

namespace __sanitizer {
uptr dllThunkGetRealAddrOrDie(const char *name);

int dllThunkIntercept(const char* main_function, uptr dll_function);

int dllThunkInterceptWhenPossible(const char* main_function,
    const char* default_function, uptr dll_function);
}

extern "C" int __dll_thunk_init();

// ----------------- Function interception helper macros -------------------- //
// Override dll_function with main_function from main executable.
#define INTERCEPT_OR_DIE(main_function, dll_function)                          \
  static int intercept_##dll_function() {                                      \
    return __sanitizer::dllThunkIntercept(main_function, (__sanitizer::uptr)   \
        dll_function);                                                         \
  }                                                                            \
  __pragma(section(".DLLTH$M", long, read))                                    \
  __declspec(allocate(".DLLTH$M")) int (*__dll_thunk_##dll_function)() =       \
    intercept_##dll_function;

// Try to override dll_function with main_function from main executable.
// If main_function is not present, override dll_function with default_function.
#define INTERCEPT_WHEN_POSSIBLE(main_function, default_function, dll_function) \
  static int intercept_##dll_function() {                                      \
    return __sanitizer::dllThunkInterceptWhenPossible(main_function,           \
        default_function, (__sanitizer::uptr)dll_function);                    \
  }                                                                            \
  __pragma(section(".DLLTH$M", long, read))                                    \
  __declspec(allocate(".DLLTH$M")) int (*__dll_thunk_##dll_function)() =       \
    intercept_##dll_function;

// -------------------- Function interception macros ------------------------ //
// Special case of hooks -- ASan own interface functions.  Those are only called
// after __asan_init, thus an empty implementation is sufficient.
#define INTERCEPT_SANITIZER_FUNCTION(name)                                     \
  extern "C" __declspec(noinline) void name() {                                \
    volatile int prevent_icf = (__LINE__ << 8) ^ __COUNTER__;                  \
    static const char function_name[] = #name;                                 \
    for (const char* ptr = &function_name[0]; *ptr; ++ptr)                     \
      prevent_icf ^= *ptr;                                                     \
    (void)prevent_icf;                                                         \
    __debugbreak();                                                            \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name)

// Special case of hooks -- Weak functions, could be redefined in the main
// executable, but that is not necessary, so we shouldn't die if we can not find
// a reference. Instead, when the function is not present in the main executable
// we consider the default impl provided by asan library.
#define INTERCEPT_SANITIZER_WEAK_FUNCTION(name)                                \
  extern "C" __declspec(noinline) void name() {                                \
    volatile int prevent_icf = (__LINE__ << 8) ^ __COUNTER__;                  \
    static const char function_name[] = #name;                                 \
    for (const char* ptr = &function_name[0]; *ptr; ++ptr)                     \
      prevent_icf ^= *ptr;                                                     \
    (void)prevent_icf;                                                         \
    __debugbreak();                                                            \
  }                                                                            \
  INTERCEPT_WHEN_POSSIBLE(#name, STRINGIFY(WEAK_EXPORT_NAME(name)), name)

// We can't define our own version of strlen etc. because that would lead to
// link-time or even type mismatch errors.  Instead, we can declare a function
// just to be able to get its address.  Me may miss the first few calls to the
// functions since it can be called before __dll_thunk_init, but that would lead
// to false negatives in the startup code before user's global initializers,
// which isn't a big deal.
#define INTERCEPT_LIBRARY_FUNCTION(name)                                       \
  extern "C" void name();                                                      \
  INTERCEPT_OR_DIE(STRINGIFY(WRAP(name)), name)

// Use these macros for functions that could be called before __dll_thunk_init()
// is executed and don't lead to errors if defined (free, malloc, etc).
#define INTERCEPT_WRAP_V_V(name)                                               \
  extern "C" void name() {                                                     \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    fn();                                                                      \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_V_W(name)                                               \
  extern "C" void name(void *arg) {                                            \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    fn(arg);                                                                   \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_V_WW(name)                                              \
  extern "C" void name(void *arg1, void *arg2) {                               \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    fn(arg1, arg2);                                                            \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_V_WWW(name)                                             \
  extern "C" void name(void *arg1, void *arg2, void *arg3) {                   \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    fn(arg1, arg2, arg3);                                                      \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_V(name)                                               \
  extern "C" void *name() {                                                    \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn();                                                               \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_W(name)                                               \
  extern "C" void *name(void *arg) {                                           \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn(arg);                                                            \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_WW(name)                                              \
  extern "C" void *name(void *arg1, void *arg2) {                              \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn(arg1, arg2);                                                     \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_WWW(name)                                             \
  extern "C" void *name(void *arg1, void *arg2, void *arg3) {                  \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn(arg1, arg2, arg3);                                               \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_WWWW(name)                                            \
  extern "C" void *name(void *arg1, void *arg2, void *arg3, void *arg4) {      \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn(arg1, arg2, arg3, arg4);                                         \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_WWWWW(name)                                           \
  extern "C" void *name(void *arg1, void *arg2, void *arg3, void *arg4,        \
                        void *arg5) {                                          \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn(arg1, arg2, arg3, arg4, arg5);                                   \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#define INTERCEPT_WRAP_W_WWWWWW(name)                                          \
  extern "C" void *name(void *arg1, void *arg2, void *arg3, void *arg4,        \
                        void *arg5, void *arg6) {                              \
    typedef decltype(name) *fntype;                                            \
    static fntype fn = (fntype)__sanitizer::dllThunkGetRealAddrOrDie(#name);   \
    return fn(arg1, arg2, arg3, arg4, arg5, arg6);                             \
  }                                                                            \
  INTERCEPT_OR_DIE(#name, name);

#endif // SANITIZER_WIN_DLL_THUNK_H
