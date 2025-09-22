//===-- hwasan_exceptions.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// HWAddressSanitizer runtime.
//===----------------------------------------------------------------------===//

#include "hwasan_poisoning.h"
#include "sanitizer_common/sanitizer_common.h"

#include <unwind.h>

using namespace __hwasan;
using namespace __sanitizer;

typedef _Unwind_Reason_Code PersonalityFn(int version, _Unwind_Action actions,
                                          uint64_t exception_class,
                                          _Unwind_Exception* unwind_exception,
                                          _Unwind_Context* context);

// Pointers to the _Unwind_GetGR and _Unwind_GetCFA functions are passed in
// instead of being called directly. This is to handle cases where the unwinder
// is statically linked and the sanitizer runtime and the program are linked
// against different unwinders. The _Unwind_Context data structure is opaque so
// it may be incompatible between unwinders.
typedef uintptr_t GetGRFn(_Unwind_Context* context, int index);
typedef uintptr_t GetCFAFn(_Unwind_Context* context);

extern "C" SANITIZER_INTERFACE_ATTRIBUTE _Unwind_Reason_Code
__hwasan_personality_wrapper(int version, _Unwind_Action actions,
                             uint64_t exception_class,
                             _Unwind_Exception* unwind_exception,
                             _Unwind_Context* context,
                             PersonalityFn* real_personality, GetGRFn* get_gr,
                             GetCFAFn* get_cfa) {
  _Unwind_Reason_Code rc;
  if (real_personality)
    rc = real_personality(version, actions, exception_class, unwind_exception,
                          context);
  else
    rc = _URC_CONTINUE_UNWIND;

  // We only untag frames without a landing pad because landing pads are
  // responsible for untagging the stack themselves if they resume.
  //
  // Here we assume that the frame record appears after any locals. This is not
  // required by AAPCS but is a requirement for HWASAN instrumented functions.
  if ((actions & _UA_CLEANUP_PHASE) && rc == _URC_CONTINUE_UNWIND) {
#if defined(__x86_64__)
    uptr fp = get_gr(context, 6); // rbp
#elif defined(__aarch64__)
    uptr fp = get_gr(context, 29); // x29
#elif SANITIZER_RISCV64
    uptr fp = get_gr(context, 8);  // x8
#else
#error Unsupported architecture
#endif
    uptr sp = get_cfa(context);
    TagMemory(UntagAddr(sp), UntagAddr(fp) - UntagAddr(sp),
              GetTagFromPointer(sp));
  }

  return rc;
}
