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

#ifndef _CXA_EXCEPTION_H
#define _CXA_EXCEPTION_H

#include <exception> // for std::unexpected_handler and std::terminate_handler
#include "cxxabi.h"
#include "unwind.h"

namespace __cxxabiv1 {

static const uint64_t kOurExceptionClass          = 0x434C4E47432B2B00; // CLNGC++\0
static const uint64_t kOurDependentExceptionClass = 0x434C4E47432B2B01; // CLNGC++\1
static const uint64_t get_vendor_and_language     = 0xFFFFFFFFFFFFFF00; // mask for CLNGC++

_LIBCXXABI_HIDDEN uint64_t __getExceptionClass  (const _Unwind_Exception*);
_LIBCXXABI_HIDDEN void     __setExceptionClass  (      _Unwind_Exception*, uint64_t);
_LIBCXXABI_HIDDEN bool     __isOurExceptionClass(const _Unwind_Exception*);

struct _LIBCXXABI_HIDDEN __cxa_exception {
#if defined(__LP64__) || defined(_WIN64) || defined(_LIBCXXABI_ARM_EHABI)
    // Now _Unwind_Exception is marked with __attribute__((aligned)),
    // which implies __cxa_exception is also aligned. Insert padding
    // in the beginning of the struct, rather than before unwindHeader.
    void *reserve;

    // This is a new field to support C++11 exception_ptr.
    // For binary compatibility it is at the start of this
    // struct which is prepended to the object thrown in
    // __cxa_allocate_exception.
    size_t referenceCount;
#endif

    //  Manage the exception object itself.
    std::type_info *exceptionType;
#ifdef __wasm__
    // In Wasm, a destructor returns its argument
    void *(_LIBCXXABI_DTOR_FUNC *exceptionDestructor)(void *);
#else
    void (_LIBCXXABI_DTOR_FUNC *exceptionDestructor)(void *);
#endif
    std::unexpected_handler unexpectedHandler;
    std::terminate_handler  terminateHandler;

    __cxa_exception *nextException;

    int handlerCount;

#if defined(_LIBCXXABI_ARM_EHABI)
    __cxa_exception* nextPropagatingException;
    int propagationCount;
#else
    int handlerSwitchValue;
    const unsigned char *actionRecord;
    const unsigned char *languageSpecificData;
    void *catchTemp;
    void *adjustedPtr;
#endif

#if !defined(__LP64__) && !defined(_WIN64) && !defined(_LIBCXXABI_ARM_EHABI)
    // This is a new field to support C++11 exception_ptr.
    // For binary compatibility it is placed where the compiler
    // previously added padding to 64-bit align unwindHeader.
    size_t referenceCount;
#endif
    _Unwind_Exception unwindHeader;
};

// http://sourcery.mentor.com/archives/cxx-abi-dev/msg01924.html
// The layout of this structure MUST match the layout of __cxa_exception, with
// primaryException instead of referenceCount.
struct _LIBCXXABI_HIDDEN __cxa_dependent_exception {
#if defined(__LP64__) || defined(_WIN64) || defined(_LIBCXXABI_ARM_EHABI)
    void* reserve; // padding.
    void* primaryException;
#endif

    std::type_info *exceptionType;
    void (_LIBCXXABI_DTOR_FUNC *exceptionDestructor)(void *);
    std::unexpected_handler unexpectedHandler;
    std::terminate_handler terminateHandler;

    __cxa_exception *nextException;

    int handlerCount;

#if defined(_LIBCXXABI_ARM_EHABI)
    __cxa_exception* nextPropagatingException;
    int propagationCount;
#else
    int handlerSwitchValue;
    const unsigned char *actionRecord;
    const unsigned char *languageSpecificData;
    void * catchTemp;
    void *adjustedPtr;
#endif

#if !defined(__LP64__) && !defined(_WIN64) && !defined(_LIBCXXABI_ARM_EHABI)
    void* primaryException;
#endif
    _Unwind_Exception unwindHeader;
};

// Verify the negative offsets of different fields.
static_assert(sizeof(_Unwind_Exception) +
                      offsetof(__cxa_exception, unwindHeader) ==
                  sizeof(__cxa_exception),
              "unwindHeader has wrong negative offsets");
static_assert(sizeof(_Unwind_Exception) +
                      offsetof(__cxa_dependent_exception, unwindHeader) ==
                  sizeof(__cxa_dependent_exception),
              "unwindHeader has wrong negative offsets");

#if defined(_LIBCXXABI_ARM_EHABI)
static_assert(offsetof(__cxa_exception, propagationCount) +
                      sizeof(_Unwind_Exception) + sizeof(void*) ==
                  sizeof(__cxa_exception),
              "propagationCount has wrong negative offset");
static_assert(offsetof(__cxa_dependent_exception, propagationCount) +
                      sizeof(_Unwind_Exception) + sizeof(void*) ==
                  sizeof(__cxa_dependent_exception),
              "propagationCount has wrong negative offset");
#elif defined(__LP64__) || defined(_WIN64)
static_assert(offsetof(__cxa_exception, adjustedPtr) +
                      sizeof(_Unwind_Exception) + sizeof(void*) ==
                  sizeof(__cxa_exception),
              "adjustedPtr has wrong negative offset");
static_assert(offsetof(__cxa_dependent_exception, adjustedPtr) +
                      sizeof(_Unwind_Exception) + sizeof(void*) ==
                  sizeof(__cxa_dependent_exception),
              "adjustedPtr has wrong negative offset");
#else
static_assert(offsetof(__cxa_exception, referenceCount) +
                      sizeof(_Unwind_Exception) + sizeof(void*) ==
                  sizeof(__cxa_exception),
              "referenceCount has wrong negative offset");
static_assert(offsetof(__cxa_dependent_exception, primaryException) +
                      sizeof(_Unwind_Exception) + sizeof(void*) ==
                  sizeof(__cxa_dependent_exception),
              "primaryException has wrong negative offset");
#endif

struct _LIBCXXABI_HIDDEN __cxa_eh_globals {
    __cxa_exception *   caughtExceptions;
    unsigned int        uncaughtExceptions;
#if defined(_LIBCXXABI_ARM_EHABI)
    __cxa_exception* propagatingExceptions;
#endif
};

extern "C" _LIBCXXABI_FUNC_VIS __cxa_eh_globals * __cxa_get_globals      ();
extern "C" _LIBCXXABI_FUNC_VIS __cxa_eh_globals * __cxa_get_globals_fast ();

extern "C" _LIBCXXABI_FUNC_VIS void * __cxa_allocate_dependent_exception ();
extern "C" _LIBCXXABI_FUNC_VIS void __cxa_free_dependent_exception (void * dependent_exception);

}  // namespace __cxxabiv1

#endif // _CXA_EXCEPTION_H
