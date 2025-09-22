//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  This file implements the "Exception Handling APIs"
//  https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
//
//===----------------------------------------------------------------------===//

// Support functions for the no-exceptions libc++ library

#include "cxxabi.h"

#include <exception>        // for std::terminate
#include "cxa_exception.h"
#include "cxa_handlers.h"

namespace __cxxabiv1 {

extern "C" {

void
__cxa_increment_exception_refcount(void *thrown_object) throw() {
    if (thrown_object != nullptr)
        std::terminate();
}

void
__cxa_decrement_exception_refcount(void *thrown_object) throw() {
    if (thrown_object != nullptr)
      std::terminate();
}


void *__cxa_current_primary_exception() throw() { return nullptr; }

void
__cxa_rethrow_primary_exception(void* thrown_object) {
    if (thrown_object != nullptr)
      std::terminate();
}

bool
__cxa_uncaught_exception() throw() { return false; }

unsigned int
__cxa_uncaught_exceptions() throw() { return 0; }

} // extern "C"

// provide dummy implementations for the 'no exceptions' case.
uint64_t __getExceptionClass  (const _Unwind_Exception*)           { return 0; }
void     __setExceptionClass  (      _Unwind_Exception*, uint64_t) {}
bool     __isOurExceptionClass(const _Unwind_Exception*)           { return false; }

}  // abi
