//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __CXXABI_H
#define __CXXABI_H

/*
 * This header provides the interface to the C++ ABI as defined at:
 *       https://itanium-cxx-abi.github.io/cxx-abi/
 */

#include <stddef.h>
#include <stdint.h>

#include <__cxxabi_config.h>

#define _LIBCPPABI_VERSION 15000
#define _LIBCXXABI_NORETURN  __attribute__((noreturn))
#define _LIBCXXABI_ALWAYS_COLD __attribute__((cold))

#ifdef __cplusplus

namespace std {
#if defined(_WIN32)
class _LIBCXXABI_TYPE_VIS type_info; // forward declaration
#else
class type_info; // forward declaration
#endif
}


// runtime routines use C calling conventions, but are in __cxxabiv1 namespace
namespace __cxxabiv1 {

struct __cxa_exception;

extern "C"  {

// 2.4.2 Allocating the Exception Object
extern _LIBCXXABI_FUNC_VIS void *
__cxa_allocate_exception(size_t thrown_size) throw();
extern _LIBCXXABI_FUNC_VIS void
__cxa_free_exception(void *thrown_exception) throw();
// This function is an LLVM extension, which mirrors the same extension in libsupc++ and libcxxrt
extern _LIBCXXABI_FUNC_VIS __cxa_exception*
#ifdef __wasm__
// In Wasm, a destructor returns its argument
__cxa_init_primary_exception(void* object, std::type_info* tinfo, void*(_LIBCXXABI_DTOR_FUNC* dest)(void*)) throw();
#else
__cxa_init_primary_exception(void* object, std::type_info* tinfo, void(_LIBCXXABI_DTOR_FUNC* dest)(void*)) throw();
#endif

// 2.4.3 Throwing the Exception Object
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void
__cxa_throw(void *thrown_exception, std::type_info *tinfo,
#ifdef __wasm__
            void *(_LIBCXXABI_DTOR_FUNC *dest)(void *));
#else
            void (_LIBCXXABI_DTOR_FUNC *dest)(void *));
#endif

// 2.5.3 Exception Handlers
extern _LIBCXXABI_FUNC_VIS void *
__cxa_get_exception_ptr(void *exceptionObject) throw();
extern _LIBCXXABI_FUNC_VIS void *
__cxa_begin_catch(void *exceptionObject) throw();
extern _LIBCXXABI_FUNC_VIS void __cxa_end_catch();
#if defined(_LIBCXXABI_ARM_EHABI)
extern _LIBCXXABI_FUNC_VIS bool
__cxa_begin_cleanup(void *exceptionObject) throw();
extern _LIBCXXABI_FUNC_VIS void __cxa_end_cleanup();
#endif
extern _LIBCXXABI_FUNC_VIS std::type_info *__cxa_current_exception_type();

// GNU extension
// Calls `terminate` with the current exception being caught. This function is used by GCC when a `noexcept` function
// throws an exception inside a try/catch block and doesn't catch it.
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void __cxa_call_terminate(void*) throw();

// 2.5.4 Rethrowing Exceptions
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void __cxa_rethrow();

// 2.6 Auxiliary Runtime APIs
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void __cxa_bad_cast(void);
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void __cxa_bad_typeid(void);
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void
__cxa_throw_bad_array_new_length(void);

// 3.2.6 Pure Virtual Function API
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void __cxa_pure_virtual(void);

// 3.2.7 Deleted Virtual Function API
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_NORETURN void __cxa_deleted_virtual(void);

// 3.3.2 One-time Construction API
#if defined(_LIBCXXABI_GUARD_ABI_ARM)
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_ALWAYS_COLD int __cxa_guard_acquire(uint32_t *);
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_ALWAYS_COLD void __cxa_guard_release(uint32_t *);
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_ALWAYS_COLD void __cxa_guard_abort(uint32_t *);
#else
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_ALWAYS_COLD int __cxa_guard_acquire(uint64_t *);
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_ALWAYS_COLD void __cxa_guard_release(uint64_t *);
extern _LIBCXXABI_FUNC_VIS _LIBCXXABI_ALWAYS_COLD void __cxa_guard_abort(uint64_t *);
#endif

// 3.3.3 Array Construction and Destruction API
extern _LIBCXXABI_FUNC_VIS void *
__cxa_vec_new(size_t element_count, size_t element_size, size_t padding_size,
              void (*constructor)(void *), void (*destructor)(void *));

extern _LIBCXXABI_FUNC_VIS void *
__cxa_vec_new2(size_t element_count, size_t element_size, size_t padding_size,
               void (*constructor)(void *), void (*destructor)(void *),
               void *(*alloc)(size_t), void (*dealloc)(void *));

extern _LIBCXXABI_FUNC_VIS void *
__cxa_vec_new3(size_t element_count, size_t element_size, size_t padding_size,
               void (*constructor)(void *), void (*destructor)(void *),
               void *(*alloc)(size_t), void (*dealloc)(void *, size_t));

extern _LIBCXXABI_FUNC_VIS void
__cxa_vec_ctor(void *array_address, size_t element_count, size_t element_size,
               void (*constructor)(void *), void (*destructor)(void *));

extern _LIBCXXABI_FUNC_VIS void __cxa_vec_dtor(void *array_address,
                                               size_t element_count,
                                               size_t element_size,
                                               void (*destructor)(void *));

extern _LIBCXXABI_FUNC_VIS void __cxa_vec_cleanup(void *array_address,
                                                  size_t element_count,
                                                  size_t element_size,
                                                  void (*destructor)(void *));

extern _LIBCXXABI_FUNC_VIS void __cxa_vec_delete(void *array_address,
                                                 size_t element_size,
                                                 size_t padding_size,
                                                 void (*destructor)(void *));

extern _LIBCXXABI_FUNC_VIS void
__cxa_vec_delete2(void *array_address, size_t element_size, size_t padding_size,
                  void (*destructor)(void *), void (*dealloc)(void *));

extern _LIBCXXABI_FUNC_VIS void
__cxa_vec_delete3(void *__array_address, size_t element_size,
                  size_t padding_size, void (*destructor)(void *),
                  void (*dealloc)(void *, size_t));

extern _LIBCXXABI_FUNC_VIS void
__cxa_vec_cctor(void *dest_array, void *src_array, size_t element_count,
                size_t element_size, void (*constructor)(void *, void *),
                void (*destructor)(void *));

// 3.3.5.3 Runtime API
// These functions are part of the C++ ABI, but they are not defined in libc++abi:
//    int __cxa_atexit(void (*)(void *), void *, void *);
//    void __cxa_finalize(void *);

// 3.4 Demangler API
extern _LIBCXXABI_FUNC_VIS char *__cxa_demangle(const char *mangled_name,
                                                char *output_buffer,
                                                size_t *length, int *status);

// Apple additions to support C++ 0x exception_ptr class
// These are primitives to wrap a smart pointer around an exception object
extern _LIBCXXABI_FUNC_VIS void *__cxa_current_primary_exception() throw();
extern _LIBCXXABI_FUNC_VIS void
__cxa_rethrow_primary_exception(void *primary_exception);
extern _LIBCXXABI_FUNC_VIS void
__cxa_increment_exception_refcount(void *primary_exception) throw();
extern _LIBCXXABI_FUNC_VIS void
__cxa_decrement_exception_refcount(void *primary_exception) throw();

// Apple extension to support std::uncaught_exception()
extern _LIBCXXABI_FUNC_VIS bool __cxa_uncaught_exception() throw();
extern _LIBCXXABI_FUNC_VIS unsigned int __cxa_uncaught_exceptions() throw();

#if defined(__linux__) || defined(__Fuchsia__)
// Linux and Fuchsia TLS support. Not yet an official part of the Itanium ABI.
// https://sourceware.org/glibc/wiki/Destructor%20support%20for%20thread_local%20variables
extern _LIBCXXABI_FUNC_VIS int __cxa_thread_atexit(void (*)(void *), void *,
                                                   void *) throw();
#endif

} // extern "C"
} // namespace __cxxabiv1

namespace abi = __cxxabiv1;

#endif // __cplusplus

#endif // __CXXABI_H
