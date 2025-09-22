//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  This file implements the "Array Construction and Destruction APIs"
//  https://itanium-cxx-abi.github.io/cxx-abi/abi.html#array-ctor
//
//===----------------------------------------------------------------------===//

#include "cxxabi.h"
#include "__cxxabi_config.h"

#include <exception>        // for std::terminate
#include <new>              // for std::bad_array_new_length

#include "abort_message.h"

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

namespace __cxxabiv1 {

//
// Helper routines and classes
//

namespace {
    inline static size_t __get_element_count ( void *p ) {
        return static_cast <size_t *> (p)[-1];
        }

    inline static void __set_element_count ( void *p, size_t element_count ) {
        static_cast <size_t *> (p)[-1] = element_count;
        }


//  A pair of classes to simplify exception handling and control flow.
//  They get passed a block of memory in the constructor, and unless the
//  'release' method is called, they deallocate the memory in the destructor.
//  Preferred usage is to allocate some memory, attach it to one of these objects,
//  and then, when all the operations to set up the memory block have succeeded,
//  call 'release'. If any of the setup operations fail, or an exception is
//  thrown, then the block is automatically deallocated.
//
//  The only difference between these two classes is the signature for the
//  deallocation function (to match new2/new3 and delete2/delete3.
    class st_heap_block2 {
    public:
        typedef void (*dealloc_f)(void *);

        st_heap_block2 ( dealloc_f dealloc, void *ptr )
            : dealloc_ ( dealloc ), ptr_ ( ptr ), enabled_ ( true ) {}
        ~st_heap_block2 () { if ( enabled_ ) dealloc_ ( ptr_ ) ; }
        void release () { enabled_ = false; }

    private:
        dealloc_f dealloc_;
        void *ptr_;
        bool enabled_;
    };

    class st_heap_block3 {
    public:
        typedef void (*dealloc_f)(void *, size_t);

        st_heap_block3 ( dealloc_f dealloc, void *ptr, size_t size )
            : dealloc_ ( dealloc ), ptr_ ( ptr ), size_ ( size ), enabled_ ( true ) {}
        ~st_heap_block3 () { if ( enabled_ ) dealloc_ ( ptr_, size_ ) ; }
        void release () { enabled_ = false; }

    private:
        dealloc_f dealloc_;
        void *ptr_;
        size_t size_;
        bool enabled_;
    };

    class st_cxa_cleanup {
    public:
        typedef void (*destruct_f)(void *);

        st_cxa_cleanup ( void *ptr, size_t &idx, size_t element_size, destruct_f destructor )
            : ptr_ ( ptr ), idx_ ( idx ), element_size_ ( element_size ),
                destructor_ ( destructor ), enabled_ ( true ) {}
        ~st_cxa_cleanup () {
            if ( enabled_ )
                __cxa_vec_cleanup ( ptr_, idx_, element_size_, destructor_ );
            }

        void release () { enabled_ = false; }

    private:
        void *ptr_;
        size_t &idx_;
        size_t element_size_;
        destruct_f destructor_;
        bool enabled_;
    };

    class st_terminate {
    public:
        st_terminate ( bool enabled = true ) : enabled_ ( enabled ) {}
        ~st_terminate () { if ( enabled_ ) std::terminate (); }
        void release () { enabled_ = false; }
    private:
        bool enabled_ ;
    };
}

//
// Externally visible routines
//

namespace {
_LIBCXXABI_NORETURN
void throw_bad_array_new_length() {
#ifndef _LIBCXXABI_NO_EXCEPTIONS
  throw std::bad_array_new_length();
#else
  abort_message("__cxa_vec_new failed to allocate memory");
#endif
}

bool mul_overflow(size_t x, size_t y, size_t *res) {
#if (defined(_LIBCXXABI_COMPILER_CLANG) && __has_builtin(__builtin_mul_overflow)) \
    || defined(_LIBCXXABI_COMPILER_GCC)
    return __builtin_mul_overflow(x, y, res);
#else
    *res = x * y;
    return x && ((*res / x) != y);
#endif
}

bool add_overflow(size_t x, size_t y, size_t *res) {
#if (defined(_LIBCXXABI_COMPILER_CLANG) && __has_builtin(__builtin_add_overflow)) \
    || defined(_LIBCXXABI_COMPILER_GCC)
  return __builtin_add_overflow(x, y, res);
#else
  *res = x + y;
  return *res < y;
#endif
}

size_t calculate_allocation_size_or_throw(size_t element_count,
                                          size_t element_size,
                                          size_t padding_size) {
  size_t element_heap_size;
  if (mul_overflow(element_count, element_size, &element_heap_size))
    throw_bad_array_new_length();

  size_t allocation_size;
  if (add_overflow(element_heap_size, padding_size, &allocation_size))
    throw_bad_array_new_length();

  return allocation_size;
}

} // namespace

extern "C" {

// Equivalent to
//
//   __cxa_vec_new2(element_count, element_size, padding_size, constructor,
//                  destructor, &::operator new[], &::operator delete[])
_LIBCXXABI_FUNC_VIS void *
__cxa_vec_new(size_t element_count, size_t element_size, size_t padding_size,
              void (*constructor)(void *), void (*destructor)(void *)) {
    return __cxa_vec_new2 ( element_count, element_size, padding_size,
        constructor, destructor, &::operator new [], &::operator delete [] );
}


// Given the number and size of elements for an array and the non-negative
// size of prefix padding for a cookie, allocate space (using alloc) for
// the array preceded by the specified padding, initialize the cookie if
// the padding is non-zero, and call the given constructor on each element.
// Return the address of the array proper, after the padding.
//
// If alloc throws an exception, rethrow the exception. If alloc returns
// NULL, return NULL. If the constructor throws an exception, call
// destructor for any already constructed elements, and rethrow the
// exception. If the destructor throws an exception, call std::terminate.
//
// The constructor may be NULL, in which case it must not be called. If the
// padding_size is zero, the destructor may be NULL; in that case it must
// not be called.
//
// Neither alloc nor dealloc may be NULL.
_LIBCXXABI_FUNC_VIS void *
__cxa_vec_new2(size_t element_count, size_t element_size, size_t padding_size,
               void (*constructor)(void *), void (*destructor)(void *),
               void *(*alloc)(size_t), void (*dealloc)(void *)) {
  const size_t heap_size = calculate_allocation_size_or_throw(
      element_count, element_size, padding_size);
  char* const heap_block = static_cast<char*>(alloc(heap_size));
  char* vec_base = heap_block;

  if (NULL != vec_base) {
    st_heap_block2 heap(dealloc, heap_block);

    //  put the padding before the array elements
        if ( 0 != padding_size ) {
            vec_base += padding_size;
            __set_element_count ( vec_base, element_count );
        }

    //  Construct the elements
        __cxa_vec_ctor ( vec_base, element_count, element_size, constructor, destructor );
        heap.release ();    // We're good!
    }

    return vec_base;
}


// Same as __cxa_vec_new2 except that the deallocation function takes both
// the object address and its size.
_LIBCXXABI_FUNC_VIS void *
__cxa_vec_new3(size_t element_count, size_t element_size, size_t padding_size,
               void (*constructor)(void *), void (*destructor)(void *),
               void *(*alloc)(size_t), void (*dealloc)(void *, size_t)) {
  const size_t heap_size = calculate_allocation_size_or_throw(
      element_count, element_size, padding_size);
  char* const heap_block = static_cast<char*>(alloc(heap_size));
  char* vec_base = heap_block;

  if (NULL != vec_base) {
    st_heap_block3 heap(dealloc, heap_block, heap_size);

    //  put the padding before the array elements
        if ( 0 != padding_size ) {
            vec_base += padding_size;
            __set_element_count ( vec_base, element_count );
        }

    //  Construct the elements
        __cxa_vec_ctor ( vec_base, element_count, element_size, constructor, destructor );
        heap.release ();    // We're good!
    }

    return vec_base;
}


// Given the (data) addresses of a destination and a source array, an
// element count and an element size, call the given copy constructor to
// copy each element from the source array to the destination array. The
// copy constructor's arguments are the destination address and source
// address, respectively. If an exception occurs, call the given destructor
// (if non-NULL) on each copied element and rethrow. If the destructor
// throws an exception, call terminate(). The constructor and or destructor
// pointers may be NULL. If either is NULL, no action is taken when it
// would have been called.

_LIBCXXABI_FUNC_VIS void __cxa_vec_cctor(void *dest_array, void *src_array,
                                         size_t element_count,
                                         size_t element_size,
                                         void (*constructor)(void *, void *),
                                         void (*destructor)(void *)) {
    if ( NULL != constructor ) {
        size_t idx = 0;
        char *src_ptr  = static_cast<char *>(src_array);
        char *dest_ptr = static_cast<char *>(dest_array);
        st_cxa_cleanup cleanup ( dest_array, idx, element_size, destructor );

        for ( idx = 0; idx < element_count;
                    ++idx, src_ptr += element_size, dest_ptr += element_size )
            constructor ( dest_ptr, src_ptr );
        cleanup.release ();     // We're good!
    }
}


// Given the (data) address of an array, not including any cookie padding,
// and the number and size of its elements, call the given constructor on
// each element. If the constructor throws an exception, call the given
// destructor for any already-constructed elements, and rethrow the
// exception. If the destructor throws an exception, call terminate(). The
// constructor and/or destructor pointers may be NULL. If either is NULL,
// no action is taken when it would have been called.
_LIBCXXABI_FUNC_VIS void
__cxa_vec_ctor(void *array_address, size_t element_count, size_t element_size,
               void (*constructor)(void *), void (*destructor)(void *)) {
    if ( NULL != constructor ) {
        size_t idx;
        char *ptr = static_cast <char *> ( array_address );
        st_cxa_cleanup cleanup ( array_address, idx, element_size, destructor );

    //  Construct the elements
        for ( idx = 0; idx < element_count; ++idx, ptr += element_size )
            constructor ( ptr );
        cleanup.release ();     // We're good!
    }
}

// Given the (data) address of an array, the number of elements, and the
// size of its elements, call the given destructor on each element. If the
// destructor throws an exception, rethrow after destroying the remaining
// elements if possible. If the destructor throws a second exception, call
// terminate(). The destructor pointer may be NULL, in which case this
// routine does nothing.
_LIBCXXABI_FUNC_VIS void __cxa_vec_dtor(void *array_address,
                                        size_t element_count,
                                        size_t element_size,
                                        void (*destructor)(void *)) {
    if ( NULL != destructor ) {
        char *ptr = static_cast <char *> (array_address);
        size_t idx = element_count;
        st_cxa_cleanup cleanup ( array_address, idx, element_size, destructor );
        {
            st_terminate exception_guard (__cxa_uncaught_exception ());
            ptr +=  element_count * element_size;   // one past the last element

            while ( idx-- > 0 ) {
                ptr -= element_size;
                destructor ( ptr );
            }
            exception_guard.release (); //  We're good !
        }
        cleanup.release ();     // We're still good!
    }
}

// Given the (data) address of an array, the number of elements, and the
// size of its elements, call the given destructor on each element. If the
// destructor throws an exception, call terminate(). The destructor pointer
// may be NULL, in which case this routine does nothing.
_LIBCXXABI_FUNC_VIS void __cxa_vec_cleanup(void *array_address,
                                           size_t element_count,
                                           size_t element_size,
                                           void (*destructor)(void *)) {
    if ( NULL != destructor ) {
        char *ptr = static_cast <char *> (array_address);
        size_t idx = element_count;
        st_terminate exception_guard;

        ptr += element_count * element_size;    // one past the last element
        while ( idx-- > 0 ) {
            ptr -= element_size;
            destructor ( ptr );
            }
        exception_guard.release ();     // We're done!
    }
}


// If the array_address is NULL, return immediately. Otherwise, given the
// (data) address of an array, the non-negative size of prefix padding for
// the cookie, and the size of its elements, call the given destructor on
// each element, using the cookie to determine the number of elements, and
// then delete the space by calling ::operator delete[](void *). If the
// destructor throws an exception, rethrow after (a) destroying the
// remaining elements, and (b) deallocating the storage. If the destructor
// throws a second exception, call terminate(). If padding_size is 0, the
// destructor pointer must be NULL. If the destructor pointer is NULL, no
// destructor call is to be made.
//
// The intent of this function is to permit an implementation to call this
// function when confronted with an expression of the form delete[] p in
// the source code, provided that the default deallocation function can be
// used. Therefore, the semantics of this function are consistent with
// those required by the standard. The requirement that the deallocation
// function be called even if the destructor throws an exception derives
// from the resolution to DR 353 to the C++ standard, which was adopted in
// April, 2003.
_LIBCXXABI_FUNC_VIS void __cxa_vec_delete(void *array_address,
                                          size_t element_size,
                                          size_t padding_size,
                                          void (*destructor)(void *)) {
    __cxa_vec_delete2 ( array_address, element_size, padding_size,
               destructor, &::operator delete [] );
}

// Same as __cxa_vec_delete, except that the given function is used for
// deallocation instead of the default delete function. If dealloc throws
// an exception, the result is undefined. The dealloc pointer may not be
// NULL.
_LIBCXXABI_FUNC_VIS void
__cxa_vec_delete2(void *array_address, size_t element_size, size_t padding_size,
                  void (*destructor)(void *), void (*dealloc)(void *)) {
    if ( NULL != array_address ) {
        char *vec_base   = static_cast <char *> (array_address);
        char *heap_block = vec_base - padding_size;
        st_heap_block2 heap ( dealloc, heap_block );

        if ( 0 != padding_size && NULL != destructor ) // call the destructors
            __cxa_vec_dtor ( array_address, __get_element_count ( vec_base ),
                                    element_size, destructor );
    }
}


// Same as __cxa_vec_delete, except that the given function is used for
// deallocation instead of the default delete function. The deallocation
// function takes both the object address and its size. If dealloc throws
// an exception, the result is undefined. The dealloc pointer may not be
// NULL.
_LIBCXXABI_FUNC_VIS void
__cxa_vec_delete3(void *array_address, size_t element_size, size_t padding_size,
                  void (*destructor)(void *), void (*dealloc)(void *, size_t)) {
    if ( NULL != array_address ) {
        char *vec_base   = static_cast <char *> (array_address);
        char *heap_block = vec_base - padding_size;
        const size_t element_count = padding_size ? __get_element_count ( vec_base ) : 0;
        const size_t heap_block_size = element_size * element_count + padding_size;
        st_heap_block3 heap ( dealloc, heap_block, heap_block_size );

        if ( 0 != padding_size && NULL != destructor ) // call the destructors
            __cxa_vec_dtor ( array_address, element_count, element_size, destructor );
    }
}


} // extern "C"

}  // abi
