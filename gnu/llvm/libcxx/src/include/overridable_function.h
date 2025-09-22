// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_SRC_INCLUDE_OVERRIDABLE_FUNCTION_H
#define _LIBCPP_SRC_INCLUDE_OVERRIDABLE_FUNCTION_H

#include <__config>
#include <cstdint>

#if __has_feature(ptrauth_calls)
#  include <ptrauth.h>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

//
// This file provides the std::__is_function_overridden utility, which allows checking
// whether an overridable function (typically a weak symbol) like `operator new`
// has been overridden by a user or not.
//
// This is a low-level utility which does not work on all platforms, since it needs
// to make assumptions about the object file format in use. Furthermore, it requires
// the "base definition" of the function (the one we want to check whether it has been
// overridden) to be annotated with the _LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE macro.
//
// This currently works with Mach-O files (used on Darwin) and with ELF files (used on Linux
// and others). On platforms where we know how to implement this detection, the macro
// _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION is defined to 1, and it is defined to 0 on
// other platforms. The _LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE macro is defined to
// nothing on unsupported platforms so that it can be used to decorate functions regardless
// of whether detection is actually supported.
//
// How does this work?
// -------------------
//
// Let's say we want to check whether a weak function `f` has been overridden by the user.
// The general mechanism works by placing `f`'s definition (in the libc++ built library)
// inside a special section, which we do using the `__section__` attribute via the
// _LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE macro.
//
// Then, when comes the time to check whether the function has been overridden, we take
// the address of the function and we check whether it falls inside the special function
// we created. This can be done by finding pointers to the start and the end of the section
// (which is done differently for ELF and Mach-O), and then checking whether `f` falls
// within those bounds. If it falls within those bounds, then `f` is still inside the
// special section and so it is the version we defined in the libc++ built library, i.e.
// it was not overridden. Otherwise, it was overridden by the user because it falls
// outside of the section.
//
// Important note
// --------------
//
// This mechanism should never be used outside of the libc++ built library. In particular,
// attempting to use this within the libc++ headers will not work at all because we don't
// want to be defining special sections inside user's executables which use our headers.
//

#if defined(_LIBCPP_OBJECT_FORMAT_MACHO)

#  define _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION 1
#  define _LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE                                                                 \
    __attribute__((__section__("__TEXT,__lcxx_override,regular,pure_instructions")))

_LIBCPP_BEGIN_NAMESPACE_STD
template <class _Ret, class... _Args>
_LIBCPP_HIDE_FROM_ABI bool __is_function_overridden(_Ret (*__fptr)(_Args...)) noexcept {
  // Declare two dummy bytes and give them these special `__asm` values. These values are
  // defined by the linker, which means that referring to `&__lcxx_override_start` will
  // effectively refer to the address where the section starts (and same for the end).
  extern char __lcxx_override_start __asm("section$start$__TEXT$__lcxx_override");
  extern char __lcxx_override_end __asm("section$end$__TEXT$__lcxx_override");

  // Now get a uintptr_t out of these locations, and out of the function pointer.
  uintptr_t __start = reinterpret_cast<uintptr_t>(&__lcxx_override_start);
  uintptr_t __end   = reinterpret_cast<uintptr_t>(&__lcxx_override_end);
  uintptr_t __ptr   = reinterpret_cast<uintptr_t>(__fptr);

#  if __has_feature(ptrauth_calls)
  // We must pass a void* to ptrauth_strip since it only accepts a pointer type. Also, in particular,
  // we must NOT pass a function pointer, otherwise we will strip the function pointer, and then attempt
  // to authenticate and re-sign it when casting it to a uintptr_t again, which will fail because we just
  // stripped the function pointer. See rdar://122927845.
  __ptr = reinterpret_cast<uintptr_t>(ptrauth_strip(reinterpret_cast<void*>(__ptr), ptrauth_key_function_pointer));
#  endif

  // Finally, the function was overridden if it falls outside of the section's bounds.
  return __ptr < __start || __ptr > __end;
}
_LIBCPP_END_NAMESPACE_STD

#elif defined(_LIBCPP_OBJECT_FORMAT_ELF)

#  define _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION 1
#  define _LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE __attribute__((__section__("__lcxx_override")))

// This is very similar to what we do for Mach-O above. The ELF linker will implicitly define
// variables with those names corresponding to the start and the end of the section.
//
// See https://stackoverflow.com/questions/16552710/how-do-you-get-the-start-and-end-addresses-of-a-custom-elf-section
extern char __start___lcxx_override;
extern char __stop___lcxx_override;

_LIBCPP_BEGIN_NAMESPACE_STD
template <class _Ret, class... _Args>
_LIBCPP_HIDE_FROM_ABI bool __is_function_overridden(_Ret (*__fptr)(_Args...)) noexcept {
  uintptr_t __start = reinterpret_cast<uintptr_t>(&__start___lcxx_override);
  uintptr_t __end   = reinterpret_cast<uintptr_t>(&__stop___lcxx_override);
  uintptr_t __ptr   = reinterpret_cast<uintptr_t>(__fptr);

  return __ptr < __start || __ptr > __end;
}
_LIBCPP_END_NAMESPACE_STD

#else

#  define _LIBCPP_CAN_DETECT_OVERRIDDEN_FUNCTION 0
#  define _LIBCPP_MAKE_OVERRIDABLE_FUNCTION_DETECTABLE /* nothing */

#endif

#endif // _LIBCPP_SRC_INCLUDE_OVERRIDABLE_FUNCTION_H
