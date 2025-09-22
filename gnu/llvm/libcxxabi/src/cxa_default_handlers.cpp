//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
// This file implements the default terminate_handler, unexpected_handler and
// new_handler.
//===----------------------------------------------------------------------===//

#include <exception>
#include <memory>
#include <stdlib.h>
#include "abort_message.h"
#include "cxxabi.h"
#include "cxa_handlers.h"
#include "cxa_exception.h"
#include "private_typeinfo.h"
#include "include/atomic_support.h" // from libc++

#if !defined(LIBCXXABI_SILENT_TERMINATE)

static constinit const char* cause = "uncaught";

#ifndef _LIBCXXABI_NO_EXCEPTIONS
// Demangle the given string, or return the string as-is in case of an error.
static std::unique_ptr<char const, void (*)(char const*)> demangle(char const* str)
{
#if !defined(LIBCXXABI_NON_DEMANGLING_TERMINATE)
    if (const char* result = __cxxabiv1::__cxa_demangle(str, nullptr, nullptr, nullptr))
        return {result, [](char const* p) { std::free(const_cast<char*>(p)); }};
#endif
    return {str, [](char const*) { /* nothing to free */ }};
}

__attribute__((noreturn))
static void demangling_terminate_handler()
{
    using namespace __cxxabiv1;
    __cxa_eh_globals* globals = __cxa_get_globals_fast();

    // If there is no uncaught exception, just note that we're terminating
    if (!globals)
        abort_message("terminating");

    __cxa_exception* exception_header = globals->caughtExceptions;
    if (!exception_header)
        abort_message("terminating");

    _Unwind_Exception* unwind_exception =
        reinterpret_cast<_Unwind_Exception*>(exception_header + 1) - 1;

    // If we're terminating due to a foreign exception
    if (!__isOurExceptionClass(unwind_exception))
        abort_message("terminating due to %s foreign exception", cause);

    void* thrown_object =
        __getExceptionClass(unwind_exception) == kOurDependentExceptionClass ?
            ((__cxa_dependent_exception*)exception_header)->primaryException :
            exception_header + 1;
    const __shim_type_info* thrown_type =
        static_cast<const __shim_type_info*>(exception_header->exceptionType);
    auto name = demangle(thrown_type->name());
    // If the uncaught exception can be caught with std::exception&
    const __shim_type_info* catch_type =
        static_cast<const __shim_type_info*>(&typeid(std::exception));
    if (catch_type->can_catch(thrown_type, thrown_object))
    {
        // Include the what() message from the exception
        const std::exception* e = static_cast<const std::exception*>(thrown_object);
        abort_message("terminating due to %s exception of type %s: %s", cause, name.get(), e->what());
    }
    else
    {
        // Else just note that we're terminating due to an exception
        abort_message("terminating due to %s exception of type %s", cause, name.get());
    }
}
#else // !_LIBCXXABI_NO_EXCEPTIONS
__attribute__((noreturn))
static void demangling_terminate_handler()
{
    abort_message("terminating");
}
#endif // !_LIBCXXABI_NO_EXCEPTIONS

__attribute__((noreturn))
static void demangling_unexpected_handler()
{
    cause = "unexpected";
    std::terminate();
}

static constexpr std::terminate_handler default_terminate_handler = demangling_terminate_handler;
static constexpr std::terminate_handler default_unexpected_handler = demangling_unexpected_handler;
#else // !LIBCXXABI_SILENT_TERMINATE
static constexpr std::terminate_handler default_terminate_handler = ::abort;
static constexpr std::terminate_handler default_unexpected_handler = std::terminate;
#endif // !LIBCXXABI_SILENT_TERMINATE

//
// Global variables that hold the pointers to the current handler
//
_LIBCXXABI_DATA_VIS
constinit std::terminate_handler __cxa_terminate_handler = default_terminate_handler;

_LIBCXXABI_DATA_VIS
constinit std::unexpected_handler __cxa_unexpected_handler = default_unexpected_handler;

_LIBCXXABI_DATA_VIS
constinit std::new_handler __cxa_new_handler = nullptr;

namespace std
{

unexpected_handler
set_unexpected(unexpected_handler func) noexcept
{
    if (func == 0)
        func = default_unexpected_handler;
    return __libcpp_atomic_exchange(&__cxa_unexpected_handler, func,
                                    _AO_Acq_Rel);
}

terminate_handler
set_terminate(terminate_handler func) noexcept
{
    if (func == 0)
        func = default_terminate_handler;
    return __libcpp_atomic_exchange(&__cxa_terminate_handler, func,
                                    _AO_Acq_Rel);
}

new_handler
set_new_handler(new_handler handler) noexcept
{
    return __libcpp_atomic_exchange(&__cxa_new_handler, handler, _AO_Acq_Rel);
}

}
