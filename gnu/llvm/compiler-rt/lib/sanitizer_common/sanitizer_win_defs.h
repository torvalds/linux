//===-- sanitizer_win_defs.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common definitions for Windows-specific code.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_WIN_DEFS_H
#define SANITIZER_WIN_DEFS_H

#include "sanitizer_platform.h"
#if SANITIZER_WINDOWS

#ifndef WINAPI
#if defined(_M_IX86) || defined(__i386__)
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#endif

#if defined(_M_IX86) || defined(__i386__)
#define WIN_SYM_PREFIX "_"
#else
#define WIN_SYM_PREFIX
#endif

// For MinGW, the /export: directives contain undecorated symbols, contrary to
// link/lld-link. The GNU linker doesn't support /alternatename and /include
// though, thus lld-link in MinGW mode interprets them in the same way as
// in the default mode.
#ifdef __MINGW32__
#define WIN_EXPORT_PREFIX
#else
#define WIN_EXPORT_PREFIX WIN_SYM_PREFIX
#endif

// Intermediate macro to ensure the parameter is expanded before stringified.
#define STRINGIFY_(A) #A
#define STRINGIFY(A) STRINGIFY_(A)

#if !SANITIZER_GO

// ----------------- A workaround for the absence of weak symbols --------------
// We don't have a direct equivalent of weak symbols when using MSVC, but we can
// use the /alternatename directive to tell the linker to default a specific
// symbol to a specific value.
// Take into account that this is a pragma directive for the linker, so it will
// be ignored by the compiler and the function will be marked as UNDEF in the
// symbol table of the resulting object file. The linker won't find the default
// implementation until it links with that object file.
// So, suppose we provide a default implementation "fundef" for "fun", and this
// is compiled into the object file "test.obj" including the pragma directive.
// If we have some code with references to "fun" and we link that code with
// "test.obj", it will work because the linker always link object files.
// But, if "test.obj" is included in a static library, like "test.lib", then the
// liker will only link to "test.obj" if necessary. If we only included the
// definition of "fun", it won't link to "test.obj" (from test.lib) because
// "fun" appears as UNDEF, so it doesn't resolve the symbol "fun", and will
// result in a link error (the linker doesn't find the pragma directive).
// So, a workaround is to force linkage with the modules that include weak
// definitions, with the following macro: WIN_FORCE_LINK()

#define WIN_WEAK_ALIAS(Name, Default)                                          \
  __pragma(comment(linker, "/alternatename:" WIN_SYM_PREFIX STRINGIFY(Name) "="\
                                             WIN_SYM_PREFIX STRINGIFY(Default)))

#define WIN_FORCE_LINK(Name)                                                   \
  __pragma(comment(linker, "/include:" WIN_SYM_PREFIX STRINGIFY(Name)))

#define WIN_EXPORT(ExportedName, Name)                                         \
  __pragma(comment(linker, "/export:" WIN_EXPORT_PREFIX STRINGIFY(ExportedName)\
                                  "=" WIN_EXPORT_PREFIX STRINGIFY(Name)))

// We cannot define weak functions on Windows, but we can use WIN_WEAK_ALIAS()
// which defines an alias to a default implementation, and only works when
// linking statically.
// So, to define a weak function "fun", we define a default implementation with
// a different name "fun__def" and we create a "weak alias" fun = fun__def.
// Then, users can override it just defining "fun".
// We impose "extern "C"" because otherwise WIN_WEAK_ALIAS() will fail because
// of name mangling.

// Dummy name for default implementation of weak function.
# define WEAK_DEFAULT_NAME(Name) Name##__def
// Name for exported implementation of weak function.
# define WEAK_EXPORT_NAME(Name) Name##__dll

// Use this macro when you need to define and export a weak function from a
// library. For example:
//   WIN_WEAK_EXPORT_DEF(bool, compare, int a, int b) { return a > b; }
# define WIN_WEAK_EXPORT_DEF(ReturnType, Name, ...)                            \
  WIN_WEAK_ALIAS(Name, WEAK_DEFAULT_NAME(Name))                                \
  WIN_EXPORT(WEAK_EXPORT_NAME(Name), Name)                                     \
  extern "C" ReturnType Name(__VA_ARGS__);                                     \
  extern "C" ReturnType WEAK_DEFAULT_NAME(Name)(__VA_ARGS__)

// Use this macro when you need to import a weak function from a library. It
// defines a weak alias to the imported function from the dll. For example:
//   WIN_WEAK_IMPORT_DEF(compare)
# define WIN_WEAK_IMPORT_DEF(Name)                                             \
  WIN_WEAK_ALIAS(Name, WEAK_EXPORT_NAME(Name))

// So, for Windows we provide something similar to weak symbols in Linux, with
// some differences:
// + A default implementation must always be provided.
//
// + When linking statically it works quite similarly. For example:
//
//   // libExample.cc
//   WIN_WEAK_EXPORT_DEF(bool, compare, int a, int b) { return a > b; }
//
//   // client.cc
//   // We can use the default implementation from the library:
//   compare(1, 2);
//   // Or we can override it:
//   extern "C" bool compare (int a, int b) { return a >= b; }
//
//  And it will work fine. If we don't override the function, we need to ensure
//  that the linker includes the object file with the default implementation.
//  We can do so with the linker option "-wholearchive:".
//
// + When linking dynamically with a library (dll), weak functions are exported
//  with "__dll" suffix. Clients can use the macro WIN_WEAK_IMPORT_DEF(fun)
//  which defines a "weak alias" fun = fun__dll.
//
//   // libExample.cc
//   WIN_WEAK_EXPORT_DEF(bool, compare, int a, int b) { return a > b; }
//
//   // client.cc
//   WIN_WEAK_IMPORT_DEF(compare)
//   // We can use the default implementation from the library:
//   compare(1, 2);
//   // Or we can override it:
//   extern "C" bool compare (int a, int b) { return a >= b; }
//
//  But if we override the function, the dlls don't have access to it (which
//  is different in linux). If that is desired, the strong definition must be
//  exported and interception can be used from the rest of the dlls.
//
//   // libExample.cc
//   WIN_WEAK_EXPORT_DEF(bool, compare, int a, int b) { return a > b; }
//   // When initialized, check if the main executable defined "compare".
//   int libExample_init() {
//     uptr fnptr = __interception::InternalGetProcAddress(
//         (void *)GetModuleHandleA(0), "compare");
//     if (fnptr && !__interception::OverrideFunction((uptr)compare, fnptr, 0))
//       abort();
//     return 0;
//   }
//
//   // client.cc
//   WIN_WEAK_IMPORT_DEF(compare)
//   // We override and export compare:
//   extern "C" __declspec(dllexport) bool compare (int a, int b) {
//     return a >= b;
//   }
//

#else // SANITIZER_GO

// Go neither needs nor wants weak references.
// The shenanigans above don't work for gcc.
# define WIN_WEAK_EXPORT_DEF(ReturnType, Name, ...)                            \
  extern "C" ReturnType Name(__VA_ARGS__)

#endif // SANITIZER_GO

#endif // SANITIZER_WINDOWS
#endif // SANITIZER_WIN_DEFS_H
