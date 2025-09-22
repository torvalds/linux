// POSIX thread-related memory allocation -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/*
 * Copyright (c) 1996
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file pthread_allocimpl.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_PTHREAD_ALLOCIMPL_H
#define _CPP_BITS_PTHREAD_ALLOCIMPL_H 1

// Pthread-specific node allocator.
// This is similar to the default allocator, except that free-list
// information is kept separately for each thread, avoiding locking.
// This should be reasonably fast even in the presence of threads.
// The down side is that storage may not be well-utilized.
// It is not an error to allocate memory in thread A and deallocate
// it in thread B.  But this effectively transfers ownership of the memory,
// so that it can only be reallocated by thread B.  Thus this can effectively
// result in a storage leak if it's done on a regular basis.
// It can also result in frequent sharing of
// cache lines among processors, with potentially serious performance
// consequences.

#include <bits/c++config.h>
#include <cerrno>
#include <bits/stl_alloc.h>
#ifndef __RESTRICT
#  define __RESTRICT
#endif

#include <new>

namespace std
{

#define __STL_DATA_ALIGNMENT 8

union _Pthread_alloc_obj {
    union _Pthread_alloc_obj * __free_list_link;
    char __client_data[__STL_DATA_ALIGNMENT];    /* The client sees this.    */
};

// Pthread allocators don't appear to the client to have meaningful
// instances.  We do in fact need to associate some state with each
// thread.  That state is represented by
// _Pthread_alloc_per_thread_state<_Max_size>.

template<size_t _Max_size>
struct _Pthread_alloc_per_thread_state {
  typedef _Pthread_alloc_obj __obj;
  enum { _S_NFREELISTS = _Max_size/__STL_DATA_ALIGNMENT };
  _Pthread_alloc_obj* volatile __free_list[_S_NFREELISTS]; 
  _Pthread_alloc_per_thread_state<_Max_size> * __next; 
	// Free list link for list of available per thread structures.
  	// When one of these becomes available for reuse due to thread
	// termination, any objects in its free list remain associated
	// with it.  The whole structure may then be used by a newly
	// created thread.
  _Pthread_alloc_per_thread_state() : __next(0)
  {
    memset((void *)__free_list, 0, (size_t) _S_NFREELISTS * sizeof(__obj *));
  }
  // Returns an object of size __n, and possibly adds to size n free list.
  void *_M_refill(size_t __n);
};

// Pthread-specific allocator.
// The argument specifies the largest object size allocated from per-thread
// free lists.  Larger objects are allocated using malloc_alloc.
// Max_size must be a power of 2.
template <size_t _Max_size = 128>
class _Pthread_alloc_template {

public: // but only for internal use:

  typedef _Pthread_alloc_obj __obj;

  // Allocates a chunk for nobjs of size size.  nobjs may be reduced
  // if it is inconvenient to allocate the requested number.
  static char *_S_chunk_alloc(size_t __size, int &__nobjs);

  enum {_S_ALIGN = __STL_DATA_ALIGNMENT};

  static size_t _S_round_up(size_t __bytes) {
    return (((__bytes) + (int) _S_ALIGN-1) & ~((int) _S_ALIGN - 1));
  }
  static size_t _S_freelist_index(size_t __bytes) {
    return (((__bytes) + (int) _S_ALIGN-1)/(int)_S_ALIGN - 1);
  }

private:
  // Chunk allocation state. And other shared state.
  // Protected by _S_chunk_allocator_lock.
  static pthread_mutex_t _S_chunk_allocator_lock;
  static char *_S_start_free;
  static char *_S_end_free;
  static size_t _S_heap_size;
  static _Pthread_alloc_per_thread_state<_Max_size>* _S_free_per_thread_states;
  static pthread_key_t _S_key;
  static bool _S_key_initialized;
        // Pthread key under which per thread state is stored. 
        // Allocator instances that are currently unclaimed by any thread.
  static void _S_destructor(void *instance);
        // Function to be called on thread exit to reclaim per thread
        // state.
  static _Pthread_alloc_per_thread_state<_Max_size> *_S_new_per_thread_state();
        // Return a recycled or new per thread state.
  static _Pthread_alloc_per_thread_state<_Max_size> *_S_get_per_thread_state();
        // ensure that the current thread has an associated
        // per thread state.
  class _M_lock;
  friend class _M_lock;
  class _M_lock {
      public:
        _M_lock () { pthread_mutex_lock(&_S_chunk_allocator_lock); }
        ~_M_lock () { pthread_mutex_unlock(&_S_chunk_allocator_lock); }
  };

public:

  /* n must be > 0      */
  static void * allocate(size_t __n)
  {
    __obj * volatile * __my_free_list;
    __obj * __RESTRICT __result;
    _Pthread_alloc_per_thread_state<_Max_size>* __a;

    if (__n > _Max_size) {
        return(malloc_alloc::allocate(__n));
    }
    if (!_S_key_initialized ||
        !(__a = (_Pthread_alloc_per_thread_state<_Max_size>*)
                                 pthread_getspecific(_S_key))) {
        __a = _S_get_per_thread_state();
    }
    __my_free_list = __a -> __free_list + _S_freelist_index(__n);
    __result = *__my_free_list;
    if (__result == 0) {
        void *__r = __a -> _M_refill(_S_round_up(__n));
        return __r;
    }
    *__my_free_list = __result -> __free_list_link;
    return (__result);
  };

  /* p may not be 0 */
  static void deallocate(void *__p, size_t __n)
  {
    __obj *__q = (__obj *)__p;
    __obj * volatile * __my_free_list;
    _Pthread_alloc_per_thread_state<_Max_size>* __a;

    if (__n > _Max_size) {
        malloc_alloc::deallocate(__p, __n);
        return;
    }
    if (!_S_key_initialized ||
        !(__a = (_Pthread_alloc_per_thread_state<_Max_size> *)
                pthread_getspecific(_S_key))) {
        __a = _S_get_per_thread_state();
    }
    __my_free_list = __a->__free_list + _S_freelist_index(__n);
    __q -> __free_list_link = *__my_free_list;
    *__my_free_list = __q;
  }

  static void * reallocate(void *__p, size_t __old_sz, size_t __new_sz);

} ;

typedef _Pthread_alloc_template<> pthread_alloc;


template <size_t _Max_size>
void _Pthread_alloc_template<_Max_size>::_S_destructor(void * __instance)
{
    _M_lock __lock_instance;	// Need to acquire lock here.
    _Pthread_alloc_per_thread_state<_Max_size>* __s =
        (_Pthread_alloc_per_thread_state<_Max_size> *)__instance;
    __s -> __next = _S_free_per_thread_states;
    _S_free_per_thread_states = __s;
}

template <size_t _Max_size>
_Pthread_alloc_per_thread_state<_Max_size> *
_Pthread_alloc_template<_Max_size>::_S_new_per_thread_state()
{    
    /* lock already held here.	*/
    if (0 != _S_free_per_thread_states) {
        _Pthread_alloc_per_thread_state<_Max_size> *__result =
					_S_free_per_thread_states;
        _S_free_per_thread_states = _S_free_per_thread_states -> __next;
        return __result;
    } else {
        return new _Pthread_alloc_per_thread_state<_Max_size>;
    }
}

template <size_t _Max_size>
_Pthread_alloc_per_thread_state<_Max_size> *
_Pthread_alloc_template<_Max_size>::_S_get_per_thread_state()
{
    /*REFERENCED*/
    _M_lock __lock_instance;	// Need to acquire lock here.
    int __ret_code;
    _Pthread_alloc_per_thread_state<_Max_size> * __result;
    if (!_S_key_initialized) {
        if (pthread_key_create(&_S_key, _S_destructor)) {
	    std::__throw_bad_alloc();  // defined in funcexcept.h
        }
        _S_key_initialized = true;
    }
    __result = _S_new_per_thread_state();
    __ret_code = pthread_setspecific(_S_key, __result);
    if (__ret_code) {
      if (__ret_code == ENOMEM) {
	std::__throw_bad_alloc();
      } else {
	// EINVAL
	abort();
      }
    }
    return __result;
}

/* We allocate memory in large chunks in order to avoid fragmenting     */
/* the malloc heap too much.                                            */
/* We assume that size is properly aligned.                             */
template <size_t _Max_size>
char *_Pthread_alloc_template<_Max_size>
::_S_chunk_alloc(size_t __size, int &__nobjs)
{
  {
    char * __result;
    size_t __total_bytes;
    size_t __bytes_left;
    /*REFERENCED*/
    _M_lock __lock_instance;         // Acquire lock for this routine

    __total_bytes = __size * __nobjs;
    __bytes_left = _S_end_free - _S_start_free;
    if (__bytes_left >= __total_bytes) {
        __result = _S_start_free;
        _S_start_free += __total_bytes;
        return(__result);
    } else if (__bytes_left >= __size) {
        __nobjs = __bytes_left/__size;
        __total_bytes = __size * __nobjs;
        __result = _S_start_free;
        _S_start_free += __total_bytes;
        return(__result);
    } else {
        size_t __bytes_to_get =
		2 * __total_bytes + _S_round_up(_S_heap_size >> 4);
        // Try to make use of the left-over piece.
        if (__bytes_left > 0) {
            _Pthread_alloc_per_thread_state<_Max_size>* __a = 
                (_Pthread_alloc_per_thread_state<_Max_size>*)
			pthread_getspecific(_S_key);
            __obj * volatile * __my_free_list =
                        __a->__free_list + _S_freelist_index(__bytes_left);

            ((__obj *)_S_start_free) -> __free_list_link = *__my_free_list;
            *__my_free_list = (__obj *)_S_start_free;
        }
#       ifdef _SGI_SOURCE
          // Try to get memory that's aligned on something like a
          // cache line boundary, so as to avoid parceling out
          // parts of the same line to different threads and thus
          // possibly different processors.
          {
            const int __cache_line_size = 128;  // probable upper bound
            __bytes_to_get &= ~(__cache_line_size-1);
            _S_start_free = (char *)memalign(__cache_line_size, __bytes_to_get); 
            if (0 == _S_start_free) {
              _S_start_free = (char *)malloc_alloc::allocate(__bytes_to_get);
            }
          }
#       else  /* !SGI_SOURCE */
          _S_start_free = (char *)malloc_alloc::allocate(__bytes_to_get);
#       endif
        _S_heap_size += __bytes_to_get;
        _S_end_free = _S_start_free + __bytes_to_get;
    }
  }
  // lock is released here
  return(_S_chunk_alloc(__size, __nobjs));
}


/* Returns an object of size n, and optionally adds to size n free list.*/
/* We assume that n is properly aligned.                                */
/* We hold the allocation lock.                                         */
template <size_t _Max_size>
void *_Pthread_alloc_per_thread_state<_Max_size>
::_M_refill(size_t __n)
{
    int __nobjs = 128;
    char * __chunk =
	_Pthread_alloc_template<_Max_size>::_S_chunk_alloc(__n, __nobjs);
    __obj * volatile * __my_free_list;
    __obj * __result;
    __obj * __current_obj, * __next_obj;
    int __i;

    if (1 == __nobjs)  {
        return(__chunk);
    }
    __my_free_list = __free_list
		 + _Pthread_alloc_template<_Max_size>::_S_freelist_index(__n);

    /* Build free list in chunk */
      __result = (__obj *)__chunk;
      *__my_free_list = __next_obj = (__obj *)(__chunk + __n);
      for (__i = 1; ; __i++) {
        __current_obj = __next_obj;
        __next_obj = (__obj *)((char *)__next_obj + __n);
        if (__nobjs - 1 == __i) {
            __current_obj -> __free_list_link = 0;
            break;
        } else {
            __current_obj -> __free_list_link = __next_obj;
        }
      }
    return(__result);
}

template <size_t _Max_size>
void *_Pthread_alloc_template<_Max_size>
::reallocate(void *__p, size_t __old_sz, size_t __new_sz)
{
    void * __result;
    size_t __copy_sz;

    if (__old_sz > _Max_size
	&& __new_sz > _Max_size) {
        return(realloc(__p, __new_sz));
    }
    if (_S_round_up(__old_sz) == _S_round_up(__new_sz)) return(__p);
    __result = allocate(__new_sz);
    __copy_sz = __new_sz > __old_sz? __old_sz : __new_sz;
    memcpy(__result, __p, __copy_sz);
    deallocate(__p, __old_sz);
    return(__result);
}

template <size_t _Max_size>
_Pthread_alloc_per_thread_state<_Max_size> *
_Pthread_alloc_template<_Max_size>::_S_free_per_thread_states = 0;

template <size_t _Max_size>
pthread_key_t _Pthread_alloc_template<_Max_size>::_S_key;

template <size_t _Max_size>
bool _Pthread_alloc_template<_Max_size>::_S_key_initialized = false;

template <size_t _Max_size>
pthread_mutex_t _Pthread_alloc_template<_Max_size>::_S_chunk_allocator_lock
= PTHREAD_MUTEX_INITIALIZER;

template <size_t _Max_size>
char *_Pthread_alloc_template<_Max_size>
::_S_start_free = 0;

template <size_t _Max_size>
char *_Pthread_alloc_template<_Max_size>
::_S_end_free = 0;

template <size_t _Max_size>
size_t _Pthread_alloc_template<_Max_size>
::_S_heap_size = 0;


template <class _Tp>
class pthread_allocator {
  typedef pthread_alloc _S_Alloc;          // The underlying allocator.
public:
  typedef size_t     size_type;
  typedef ptrdiff_t  difference_type;
  typedef _Tp*       pointer;
  typedef const _Tp* const_pointer;
  typedef _Tp&       reference;
  typedef const _Tp& const_reference;
  typedef _Tp        value_type;

  template <class _NewType> struct rebind {
    typedef pthread_allocator<_NewType> other;
  };

  pthread_allocator() throw() {}
  pthread_allocator(const pthread_allocator& a) throw() {}
  template <class _OtherType>
	pthread_allocator(const pthread_allocator<_OtherType>&)
		throw() {}
  ~pthread_allocator() throw() {}

  pointer address(reference __x) const { return &__x; }
  const_pointer address(const_reference __x) const { return &__x; }

  // __n is permitted to be 0.  The C++ standard says nothing about what
  // the return value is when __n == 0.
  _Tp* allocate(size_type __n, const void* = 0) {
    return __n != 0 ? static_cast<_Tp*>(_S_Alloc::allocate(__n * sizeof(_Tp)))
                    : 0;
  }

  // p is not permitted to be a null pointer.
  void deallocate(pointer __p, size_type __n)
    { _S_Alloc::deallocate(__p, __n * sizeof(_Tp)); }

  size_type max_size() const throw() 
    { return size_t(-1) / sizeof(_Tp); }

  void construct(pointer __p, const _Tp& __val) { new(__p) _Tp(__val); }
  void destroy(pointer _p) { _p->~_Tp(); }
};

template<>
class pthread_allocator<void> {
public:
  typedef size_t      size_type;
  typedef ptrdiff_t   difference_type;
  typedef void*       pointer;
  typedef const void* const_pointer;
  typedef void        value_type;

  template <class _NewType> struct rebind {
    typedef pthread_allocator<_NewType> other;
  };
};

template <size_t _Max_size>
inline bool operator==(const _Pthread_alloc_template<_Max_size>&,
                       const _Pthread_alloc_template<_Max_size>&)
{
  return true;
}

template <class _T1, class _T2>
inline bool operator==(const pthread_allocator<_T1>&,
                       const pthread_allocator<_T2>& a2) 
{
  return true;
}

template <class _T1, class _T2>
inline bool operator!=(const pthread_allocator<_T1>&,
                       const pthread_allocator<_T2>&)
{
  return false;
}

template <class _Tp, size_t _Max_size>
struct _Alloc_traits<_Tp, _Pthread_alloc_template<_Max_size> >
{
  static const bool _S_instanceless = true;
  typedef simple_alloc<_Tp, _Pthread_alloc_template<_Max_size> > _Alloc_type;
  typedef __allocator<_Tp, _Pthread_alloc_template<_Max_size> > 
          allocator_type;
};

template <class _Tp, class _Atype, size_t _Max>
struct _Alloc_traits<_Tp, __allocator<_Atype, _Pthread_alloc_template<_Max> > >
{
  static const bool _S_instanceless = true;
  typedef simple_alloc<_Tp, _Pthread_alloc_template<_Max> > _Alloc_type;
  typedef __allocator<_Tp, _Pthread_alloc_template<_Max> > allocator_type;
};

template <class _Tp, class _Atype>
struct _Alloc_traits<_Tp, pthread_allocator<_Atype> >
{
  static const bool _S_instanceless = true;
  typedef simple_alloc<_Tp, _Pthread_alloc_template<> > _Alloc_type;
  typedef pthread_allocator<_Tp> allocator_type;
};


} // namespace std

#endif /* _CPP_BITS_PTHREAD_ALLOCIMPL_H */

// Local Variables:
// mode:C++
// End:
