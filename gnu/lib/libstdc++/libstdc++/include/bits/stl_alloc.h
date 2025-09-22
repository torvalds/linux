// Allocators -*- C++ -*-

// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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
 * Copyright (c) 1996-1997
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

/** @file stl_alloc.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_ALLOC_H
#define __GLIBCPP_INTERNAL_ALLOC_H

/**
 *  @defgroup Allocators Memory Allocators
 *  @if maint
 *  stl_alloc.h implements some node allocators.  These are NOT the same as
 *  allocators in the C++ standard, nor in the original H-P STL.  They do not
 *  encapsulate different pointer types; we assume that there is only one
 *  pointer type.  The C++ standard allocators are intended to allocate
 *  individual objects, not pools or arenas.
 *
 *  In this file allocators are of two different styles:  "standard" and
 *  "SGI" (quotes included).  "Standard" allocators conform to 20.4.  "SGI"
 *  allocators differ in AT LEAST the following ways (add to this list as you
 *  discover them):
 *
 *   - "Standard" allocate() takes two parameters (n_count,hint=0) but "SGI"
 *     allocate() takes one paramter (n_size).
 *   - Likewise, "standard" deallocate()'s argument is a count, but in "SGI"
 *     is a byte size.
 *   - max_size(), construct(), and destroy() are missing in "SGI" allocators.
 *   - reallocate(p,oldsz,newsz) is added in "SGI", and behaves as
 *     if p=realloc(p,newsz).
 *
 *  "SGI" allocators may be wrapped in __allocator to convert the interface
 *  into a "standard" one.
 *  @endif
 *
 *  @note The @c reallocate member functions have been deprecated for 3.2
 *        and will be removed in 3.4.  You must define @c _GLIBCPP_DEPRECATED
 *        to make this visible in 3.2; see c++config.h.
 *
 *  The canonical description of these classes is in docs/html/ext/howto.html
 *  or online at http://gcc.gnu.org/onlinedocs/libstdc++/ext/howto.html#3
*/

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <bits/functexcept.h>   // For __throw_bad_alloc
#include <bits/stl_threads.h>

#include <bits/atomicity.h>

namespace std
{
  /**
   *  @if maint
   *  A new-based allocator, as required by the standard.  Allocation and
   *  deallocation forward to global new and delete.  "SGI" style, minus
   *  reallocate().
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  class __new_alloc
  {
  public:
    static void*
    allocate(size_t __n)
    { return ::operator new(__n); }

    static void
    deallocate(void* __p, size_t)
    { ::operator delete(__p); }
  };


  /**
   *  @if maint
   *  A malloc-based allocator.  Typically slower than the
   *  __default_alloc_template (below).  Typically thread-safe and more
   *  storage efficient.  The template argument is unused and is only present
   *  to permit multiple instantiations (but see __default_alloc_template
   *  for caveats).  "SGI" style, plus __set_malloc_handler for OOM conditions.
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  template<int __inst>
    class __malloc_alloc_template
    {
    private:
      static void* _S_oom_malloc(size_t);
      static void* _S_oom_realloc(void*, size_t);
      static void (* __malloc_alloc_oom_handler)();

    public:
      static void*
      allocate(size_t __n)
      {
        void* __result = malloc(__n);
        if (__builtin_expect(__result == 0, 0))
	  __result = _S_oom_malloc(__n);
        return __result;
      }

      static void
      deallocate(void* __p, size_t /* __n */)
      { free(__p); }

      static void*
      reallocate(void* __p, size_t /* old_sz */, size_t __new_sz)
      {
        void* __result = realloc(__p, __new_sz);
        if (__builtin_expect(__result == 0, 0))
          __result = _S_oom_realloc(__p, __new_sz);
        return __result;
      }

      static void (* __set_malloc_handler(void (*__f)()))()
      {
        void (* __old)() = __malloc_alloc_oom_handler;
        __malloc_alloc_oom_handler = __f;
        return __old;
      }
    };

  // malloc_alloc out-of-memory handling
  template<int __inst>
    void (* __malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = 0;

  template<int __inst>
    void*
    __malloc_alloc_template<__inst>::
    _S_oom_malloc(size_t __n)
    {
      void (* __my_malloc_handler)();
      void* __result;

      for (;;)
        {
          __my_malloc_handler = __malloc_alloc_oom_handler;
          if (__builtin_expect(__my_malloc_handler == 0, 0))
            __throw_bad_alloc();
          (*__my_malloc_handler)();
          __result = malloc(__n);
          if (__result)
            return __result;
        }
    }

  template<int __inst>
    void*
    __malloc_alloc_template<__inst>::
    _S_oom_realloc(void* __p, size_t __n)
    {
      void (* __my_malloc_handler)();
      void* __result;

      for (;;)
        {
          __my_malloc_handler = __malloc_alloc_oom_handler;
          if (__builtin_expect(__my_malloc_handler == 0, 0))
            __throw_bad_alloc();
          (*__my_malloc_handler)();
          __result = realloc(__p, __n);
          if (__result)
            return __result;
        }
    }

  // Should not be referenced within the library anymore.
  typedef __new_alloc                 __mem_interface;

  /**
   *  @if maint
   *  This is used primarily (only?) in _Alloc_traits and other places to
   *  help provide the _Alloc_type typedef.  All it does is forward the
   *  requests after some minimal checking.
   *
   *  This is neither "standard"-conforming nor "SGI".  The _Alloc parameter
   *  must be "SGI" style.
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  template<typename _Tp, typename _Alloc>
    class __simple_alloc
    {
    public:
      static _Tp*
      allocate(size_t __n)
      {
	_Tp* __ret = 0;
	if (__n)
	  __ret = static_cast<_Tp*>(_Alloc::allocate(__n * sizeof(_Tp)));
	return __ret;
      }
  
      static _Tp*
      allocate()
      { return (_Tp*) _Alloc::allocate(sizeof (_Tp)); }
  
      static void
      deallocate(_Tp* __p, size_t __n)
      { if (0 != __n) _Alloc::deallocate(__p, __n * sizeof (_Tp)); }
  
      static void
      deallocate(_Tp* __p)
      { _Alloc::deallocate(__p, sizeof (_Tp)); }
    };


  /**
   *  @if maint
   *  An adaptor for an underlying allocator (_Alloc) to check the size
   *  arguments for debugging.
   *
   *  "There is some evidence that this can confuse Purify." - SGI comment
   *
   *  This adaptor is "SGI" style.  The _Alloc parameter must also be "SGI".
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  template<typename _Alloc>
    class __debug_alloc
    {
    private:
      // Size of space used to store size.  Note that this must be
      // large enough to preserve alignment.
      enum {_S_extra = 8};

    public:
      static void*
      allocate(size_t __n)
      {
        char* __result = (char*)_Alloc::allocate(__n + (int) _S_extra);
        *(size_t*)__result = __n;
        return __result + (int) _S_extra;
      }

      static void
      deallocate(void* __p, size_t __n)
      {
        char* __real_p = (char*)__p - (int) _S_extra;
        if (*(size_t*)__real_p != __n)
	  abort();
        _Alloc::deallocate(__real_p, __n + (int) _S_extra);
      }

      static void*
      reallocate(void* __p, size_t __old_sz, size_t __new_sz)
      {
        char* __real_p = (char*)__p - (int) _S_extra;
        if (*(size_t*)__real_p != __old_sz)
	  abort();
        char* __result = (char*) _Alloc::reallocate(__real_p, 
						    __old_sz + (int) _S_extra,
						    __new_sz + (int) _S_extra);
        *(size_t*)__result = __new_sz;
        return __result + (int) _S_extra;
      }
    };


  /**
   *  @if maint
   *  Default node allocator.  "SGI" style.  Uses various allocators to
   *  fulfill underlying requests (and makes as few requests as possible
   *  when in default high-speed pool mode).
   *
   *  Important implementation properties:
   *  0. If globally mandated, then allocate objects from __new_alloc
   *  1. If the clients request an object of size > _MAX_BYTES, the resulting
   *     object will be obtained directly from __new_alloc
   *  2. In all other cases, we allocate an object of size exactly
   *     _S_round_up(requested_size).  Thus the client has enough size
   *     information that we can return the object to the proper free list
   *     without permanently losing part of the object.
   *
   *  The first template parameter specifies whether more than one thread may
   *  use this allocator.  It is safe to allocate an object from one instance
   *  of a default_alloc and deallocate it with another one.  This effectively
   *  transfers its ownership to the second one.  This may have undesirable
   *  effects on reference locality.
   *
   *  The second parameter is unused and serves only to allow the creation of
   *  multiple default_alloc instances.  Note that containers built on different
   *  allocator instances have different types, limiting the utility of this
   *  approach.  If you do not wish to share the free lists with the main
   *  default_alloc instance, instantiate this with a non-zero __inst.
   *
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  template<bool __threads, int __inst>
    class __default_alloc_template
    {
    private:
      enum {_ALIGN = 8};
      enum {_MAX_BYTES = 128};
      enum {_NFREELISTS = _MAX_BYTES / _ALIGN};

      union _Obj
      {
        union _Obj* _M_free_list_link;
        char        _M_client_data[1];    // The client sees this.
      };

      static _Obj* volatile         _S_free_list[_NFREELISTS];

      // Chunk allocation state.
      static char*                  _S_start_free;
      static char*                  _S_end_free;
      static size_t                 _S_heap_size;

      static _STL_mutex_lock        _S_node_allocator_lock;

      static size_t
      _S_round_up(size_t __bytes)
      { return (((__bytes) + (size_t) _ALIGN-1) & ~((size_t) _ALIGN - 1)); }

      static size_t
      _S_freelist_index(size_t __bytes)
      { return (((__bytes) + (size_t)_ALIGN - 1)/(size_t)_ALIGN - 1); }

      // Returns an object of size __n, and optionally adds to size __n
      // free list.
      static void*
      _S_refill(size_t __n);

      // Allocates a chunk for nobjs of size size.  nobjs may be reduced
      // if it is inconvenient to allocate the requested number.
      static char*
      _S_chunk_alloc(size_t __size, int& __nobjs);

      // It would be nice to use _STL_auto_lock here.  But we need a
      // test whether threads are in use.
      struct _Lock
      {
        _Lock() { if (__threads) _S_node_allocator_lock._M_acquire_lock(); }
        ~_Lock() { if (__threads) _S_node_allocator_lock._M_release_lock(); }
      } __attribute__ ((__unused__));
      friend struct _Lock;

      static _Atomic_word _S_force_new;

    public:
      // __n must be > 0
      static void*
      allocate(size_t __n)
      {
	void* __ret = 0;

	// If there is a race through here, assume answer from getenv
	// will resolve in same direction.  Inspired by techniques
	// to efficiently support threading found in basic_string.h.
	if (_S_force_new == 0)
	  {
	    if (getenv("GLIBCPP_FORCE_NEW"))
	      __atomic_add(&_S_force_new, 1);
	    else
	      __atomic_add(&_S_force_new, -1);
	  }

	if ((__n > (size_t) _MAX_BYTES) || (_S_force_new > 0))
	  __ret = __new_alloc::allocate(__n);
	else
	  {
	    _Obj* volatile* __my_free_list = _S_free_list
	      + _S_freelist_index(__n);
	    // Acquire the lock here with a constructor call.  This
	    // ensures that it is released in exit or during stack
	    // unwinding.
	    _Lock __lock_instance;
	    _Obj* __restrict__ __result = *__my_free_list;
	    if (__builtin_expect(__result == 0, 0))
	      __ret = _S_refill(_S_round_up(__n));
	    else
	      {
		*__my_free_list = __result -> _M_free_list_link;
		__ret = __result;
	      }	    
	    if (__builtin_expect(__ret == 0, 0))
	      __throw_bad_alloc();
	  }
	return __ret;
      }

      // __p may not be 0
      static void
      deallocate(void* __p, size_t __n)
      {
	if ((__n > (size_t) _MAX_BYTES) || (_S_force_new > 0))
	  __new_alloc::deallocate(__p, __n);
	else
	  {
	    _Obj* volatile*  __my_free_list = _S_free_list
	      + _S_freelist_index(__n);
	    _Obj* __q = (_Obj*)__p;

	    // Acquire the lock here with a constructor call.  This
	    // ensures that it is released in exit or during stack
	    // unwinding.
	    _Lock __lock_instance;
	    __q -> _M_free_list_link = *__my_free_list;
	    *__my_free_list = __q;
	  }
      }

      static void*
      reallocate(void* __p, size_t __old_sz, size_t __new_sz);
    };

  template<bool __threads, int __inst> _Atomic_word
  __default_alloc_template<__threads, __inst>::_S_force_new = 0;

  template<bool __threads, int __inst>
    inline bool
    operator==(const __default_alloc_template<__threads,__inst>&,
               const __default_alloc_template<__threads,__inst>&)
    { return true; }

  template<bool __threads, int __inst>
    inline bool
    operator!=(const __default_alloc_template<__threads,__inst>&,
               const __default_alloc_template<__threads,__inst>&)
    { return false; }


  // We allocate memory in large chunks in order to avoid fragmenting the
  // heap too much.  We assume that __size is properly aligned.  We hold
  // the allocation lock.
  template<bool __threads, int __inst>
    char*
    __default_alloc_template<__threads, __inst>::
    _S_chunk_alloc(size_t __size, int& __nobjs)
    {
      char* __result;
      size_t __total_bytes = __size * __nobjs;
      size_t __bytes_left = _S_end_free - _S_start_free;

      if (__bytes_left >= __total_bytes)
        {
          __result = _S_start_free;
          _S_start_free += __total_bytes;
          return __result ;
        }
      else if (__bytes_left >= __size)
        {
          __nobjs = (int)(__bytes_left/__size);
          __total_bytes = __size * __nobjs;
          __result = _S_start_free;
          _S_start_free += __total_bytes;
          return __result;
        }
      else
        {
          size_t __bytes_to_get =
            2 * __total_bytes + _S_round_up(_S_heap_size >> 4);
          // Try to make use of the left-over piece.
          if (__bytes_left > 0)
            {
              _Obj* volatile* __my_free_list =
                _S_free_list + _S_freelist_index(__bytes_left);

              ((_Obj*)(void*)_S_start_free) -> _M_free_list_link = *__my_free_list;
              *__my_free_list = (_Obj*)(void*)_S_start_free;
            }
          _S_start_free = (char*) __new_alloc::allocate(__bytes_to_get);
          if (_S_start_free == 0)
            {
              size_t __i;
              _Obj* volatile* __my_free_list;
              _Obj* __p;
              // Try to make do with what we have.  That can't hurt.  We
              // do not try smaller requests, since that tends to result
              // in disaster on multi-process machines.
              __i = __size;
              for (; __i <= (size_t) _MAX_BYTES; __i += (size_t) _ALIGN)
                {
                  __my_free_list = _S_free_list + _S_freelist_index(__i);
                  __p = *__my_free_list;
                  if (__p != 0)
                    {
                      *__my_free_list = __p -> _M_free_list_link;
                      _S_start_free = (char*)__p;
                      _S_end_free = _S_start_free + __i;
                      return _S_chunk_alloc(__size, __nobjs);
                      // Any leftover piece will eventually make it to the
                      // right free list.
                    }
                }
              _S_end_free = 0;        // In case of exception.
              _S_start_free = (char*)__new_alloc::allocate(__bytes_to_get);
              // This should either throw an exception or remedy the situation.
              // Thus we assume it succeeded.
            }
          _S_heap_size += __bytes_to_get;
          _S_end_free = _S_start_free + __bytes_to_get;
          return _S_chunk_alloc(__size, __nobjs);
        }
    }


  // Returns an object of size __n, and optionally adds to "size
  // __n"'s free list.  We assume that __n is properly aligned.  We
  // hold the allocation lock.
  template<bool __threads, int __inst>
    void*
    __default_alloc_template<__threads, __inst>::_S_refill(size_t __n)
    {
      int __nobjs = 20;
      char* __chunk = _S_chunk_alloc(__n, __nobjs);
      _Obj* volatile* __my_free_list;
      _Obj* __result;
      _Obj* __current_obj;
      _Obj* __next_obj;
      int __i;

      if (1 == __nobjs)
        return __chunk;
      __my_free_list = _S_free_list + _S_freelist_index(__n);

      // Build free list in chunk.
      __result = (_Obj*)(void*)__chunk;
      *__my_free_list = __next_obj = (_Obj*)(void*)(__chunk + __n);
      for (__i = 1; ; __i++)
        {
	  __current_obj = __next_obj;
          __next_obj = (_Obj*)(void*)((char*)__next_obj + __n);
	  if (__nobjs - 1 == __i)
	    {
	      __current_obj -> _M_free_list_link = 0;
	      break;
	    }
	  else
	    __current_obj -> _M_free_list_link = __next_obj;
	}
      return __result;
    }


  template<bool threads, int inst>
    void*
    __default_alloc_template<threads, inst>::
    reallocate(void* __p, size_t __old_sz, size_t __new_sz)
    {
      void* __result;
      size_t __copy_sz;

      if (__old_sz > (size_t) _MAX_BYTES && __new_sz > (size_t) _MAX_BYTES)
        return(realloc(__p, __new_sz));
      if (_S_round_up(__old_sz) == _S_round_up(__new_sz))
        return(__p);
      __result = allocate(__new_sz);
      __copy_sz = __new_sz > __old_sz? __old_sz : __new_sz;
      memcpy(__result, __p, __copy_sz);
      deallocate(__p, __old_sz);
      return __result;
    }

  template<bool __threads, int __inst>
    _STL_mutex_lock
    __default_alloc_template<__threads,__inst>::_S_node_allocator_lock
    __STL_MUTEX_INITIALIZER;

  template<bool __threads, int __inst>
    char* __default_alloc_template<__threads,__inst>::_S_start_free = 0;

  template<bool __threads, int __inst>
    char* __default_alloc_template<__threads,__inst>::_S_end_free = 0;

  template<bool __threads, int __inst>
    size_t __default_alloc_template<__threads,__inst>::_S_heap_size = 0;

  template<bool __threads, int __inst>
    typename __default_alloc_template<__threads,__inst>::_Obj* volatile
    __default_alloc_template<__threads,__inst>::_S_free_list[_NFREELISTS];

  typedef __default_alloc_template<true,0>    __alloc;
  typedef __default_alloc_template<false,0>   __single_client_alloc;


  /**
   *  @brief  The "standard" allocator, as per [20.4].
   *
   *  The private _Alloc is "SGI" style.  (See comments at the top
   *  of stl_alloc.h.)
   *
   *  The underlying allocator behaves as follows.
   *    - __default_alloc_template is used via two typedefs
   *    - "__single_client_alloc" typedef does no locking for threads
   *    - "__alloc" typedef is threadsafe via the locks
   *    - __new_alloc is used for memory requests
   *
   *  (See @link Allocators allocators info @endlink for more.)
   */
  template<typename _Tp>
    class allocator
    {
      typedef __alloc _Alloc;          // The underlying allocator.
    public:
      typedef size_t     size_type;
      typedef ptrdiff_t  difference_type;
      typedef _Tp*       pointer;
      typedef const _Tp* const_pointer;
      typedef _Tp&       reference;
      typedef const _Tp& const_reference;
      typedef _Tp        value_type;

      template<typename _Tp1>
        struct rebind
        { typedef allocator<_Tp1> other; };

      allocator() throw() {}
      allocator(const allocator&) throw() {}
      template<typename _Tp1>
        allocator(const allocator<_Tp1>&) throw() {}
      ~allocator() throw() {}

      pointer
      address(reference __x) const { return &__x; }

      const_pointer
      address(const_reference __x) const { return &__x; }

      // NB: __n is permitted to be 0.  The C++ standard says nothing
      // about what the return value is when __n == 0.
      _Tp*
      allocate(size_type __n, const void* = 0)
      {
	_Tp* __ret = 0;
	if (__n)
	  {
	    if (__n <= this->max_size())
	      __ret = static_cast<_Tp*>(_Alloc::allocate(__n * sizeof(_Tp)));
	    else
	      __throw_bad_alloc();
	  }
	return __ret;
      }

      // __p is not permitted to be a null pointer.
      void
      deallocate(pointer __p, size_type __n)
      { _Alloc::deallocate(__p, __n * sizeof(_Tp)); }

      size_type
      max_size() const throw() { return size_t(-1) / sizeof(_Tp); }

      void construct(pointer __p, const _Tp& __val) { new(__p) _Tp(__val); }
      void destroy(pointer __p) { __p->~_Tp(); }
    };

  template<>
    class allocator<void>
    {
    public:
      typedef size_t      size_type;
      typedef ptrdiff_t   difference_type;
      typedef void*       pointer;
      typedef const void* const_pointer;
      typedef void        value_type;

      template<typename _Tp1>
        struct rebind
        { typedef allocator<_Tp1> other; };
    };


  template<typename _T1, typename _T2>
    inline bool
    operator==(const allocator<_T1>&, const allocator<_T2>&)
    { return true; }

  template<typename _T1, typename _T2>
    inline bool
    operator!=(const allocator<_T1>&, const allocator<_T2>&)
    { return false; }


  /**
   *  @if maint
   *  Allocator adaptor to turn an "SGI" style allocator (e.g.,
   *  __alloc, __malloc_alloc_template) into a "standard" conforming
   *  allocator.  Note that this adaptor does *not* assume that all
   *  objects of the underlying alloc class are identical, nor does it
   *  assume that all of the underlying alloc's member functions are
   *  static member functions.  Note, also, that __allocator<_Tp,
   *  __alloc> is essentially the same thing as allocator<_Tp>.
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  template<typename _Tp, typename _Alloc>
    struct __allocator
    {
      _Alloc __underlying_alloc;
      
      typedef size_t    size_type;
      typedef ptrdiff_t difference_type;
      typedef _Tp*       pointer;
      typedef const _Tp* const_pointer;
      typedef _Tp&       reference;
      typedef const _Tp& const_reference;
      typedef _Tp        value_type;

      template<typename _Tp1>
        struct rebind
        { typedef __allocator<_Tp1, _Alloc> other; };

      __allocator() throw() {}
      __allocator(const __allocator& __a) throw()
      : __underlying_alloc(__a.__underlying_alloc) {}

      template<typename _Tp1>
        __allocator(const __allocator<_Tp1, _Alloc>& __a) throw()
        : __underlying_alloc(__a.__underlying_alloc) {}

      ~__allocator() throw() {}

      pointer
      address(reference __x) const { return &__x; }

      const_pointer
      address(const_reference __x) const { return &__x; }

      // NB: __n is permitted to be 0.  The C++ standard says nothing
      // about what the return value is when __n == 0.
      _Tp*
      allocate(size_type __n, const void* = 0)
      {
	_Tp* __ret = 0;
	if (__n)
	  __ret = static_cast<_Tp*>(_Alloc::allocate(__n * sizeof(_Tp)));
	return __ret;
      }

      // __p is not permitted to be a null pointer.
      void
      deallocate(pointer __p, size_type __n)
      { __underlying_alloc.deallocate(__p, __n * sizeof(_Tp)); }
      
      size_type
      max_size() const throw() { return size_t(-1) / sizeof(_Tp); }
      
      void
      construct(pointer __p, const _Tp& __val) { new(__p) _Tp(__val); }
      
      void
      destroy(pointer __p) { __p->~_Tp(); }
    };

  template<typename _Alloc>
    struct __allocator<void, _Alloc>
    {
      typedef size_t      size_type;
      typedef ptrdiff_t   difference_type;
      typedef void*       pointer;
      typedef const void* const_pointer;
      typedef void        value_type;

      template<typename _Tp1>
        struct rebind
        { typedef __allocator<_Tp1, _Alloc> other; };
    };

  template<typename _Tp, typename _Alloc>
    inline bool
    operator==(const __allocator<_Tp,_Alloc>& __a1,
               const __allocator<_Tp,_Alloc>& __a2)
    { return __a1.__underlying_alloc == __a2.__underlying_alloc; }

  template<typename _Tp, typename _Alloc>
    inline bool
    operator!=(const __allocator<_Tp, _Alloc>& __a1,
               const __allocator<_Tp, _Alloc>& __a2)
    { return __a1.__underlying_alloc != __a2.__underlying_alloc; }


  //@{
  /** Comparison operators for all of the predifined SGI-style allocators.
   *  This ensures that __allocator<malloc_alloc> (for example) will work
   *  correctly.  As required, all allocators compare equal.
   */
  template<int inst>
    inline bool
    operator==(const __malloc_alloc_template<inst>&,
               const __malloc_alloc_template<inst>&)
    { return true; }

  template<int __inst>
    inline bool
    operator!=(const __malloc_alloc_template<__inst>&,
               const __malloc_alloc_template<__inst>&)
    { return false; }

  template<typename _Alloc>
    inline bool
    operator==(const __debug_alloc<_Alloc>&, const __debug_alloc<_Alloc>&)
    { return true; }

  template<typename _Alloc>
    inline bool
    operator!=(const __debug_alloc<_Alloc>&, const __debug_alloc<_Alloc>&)
    { return false; }
  //@}


  /**
   *  @if maint
   *  Another allocator adaptor:  _Alloc_traits.  This serves two purposes.
   *  First, make it possible to write containers that can use either "SGI"
   *  style allocators or "standard" allocators.  Second, provide a mechanism
   *  so that containers can query whether or not the allocator has distinct
   *  instances.  If not, the container can avoid wasting a word of memory to
   *  store an empty object.  For examples of use, see stl_vector.h, etc, or
   *  any of the other classes derived from this one.
   *
   *  This adaptor uses partial specialization.  The general case of
   *  _Alloc_traits<_Tp, _Alloc> assumes that _Alloc is a
   *  standard-conforming allocator, possibly with non-equal instances and
   *  non-static members.  (It still behaves correctly even if _Alloc has
   *  static member and if all instances are equal.  Refinements affect
   *  performance, not correctness.)
   *
   *  There are always two members:  allocator_type, which is a standard-
   *  conforming allocator type for allocating objects of type _Tp, and
   *  _S_instanceless, a static const member of type bool.  If
   *  _S_instanceless is true, this means that there is no difference
   *  between any two instances of type allocator_type.  Furthermore, if
   *  _S_instanceless is true, then _Alloc_traits has one additional
   *  member:  _Alloc_type.  This type encapsulates allocation and
   *  deallocation of objects of type _Tp through a static interface; it
   *  has two member functions, whose signatures are
   *
   *  -  static _Tp* allocate(size_t)
   *  -  static void deallocate(_Tp*, size_t)
   *
   *  The size_t parameters are "standard" style (see top of stl_alloc.h) in
   *  that they take counts, not sizes.
   *
   *  @endif
   *  (See @link Allocators allocators info @endlink for more.)
   */
  //@{
  // The fully general version.
  template<typename _Tp, typename _Allocator>
    struct _Alloc_traits
    {
      static const bool _S_instanceless = false;
      typedef typename _Allocator::template rebind<_Tp>::other allocator_type;
    };

  template<typename _Tp, typename _Allocator>
    const bool _Alloc_traits<_Tp, _Allocator>::_S_instanceless;

  /// The version for the default allocator.
  template<typename _Tp, typename _Tp1>
    struct _Alloc_traits<_Tp, allocator<_Tp1> >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __alloc> _Alloc_type;
      typedef allocator<_Tp> allocator_type;
    };
  //@}

  //@{
  /// Versions for the predefined "SGI" style allocators.
  template<typename _Tp, int __inst>
    struct _Alloc_traits<_Tp, __malloc_alloc_template<__inst> >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __malloc_alloc_template<__inst> > _Alloc_type;
      typedef __allocator<_Tp, __malloc_alloc_template<__inst> > allocator_type;
    };

  template<typename _Tp, bool __threads, int __inst>
    struct _Alloc_traits<_Tp, __default_alloc_template<__threads, __inst> >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __default_alloc_template<__threads, __inst> >
      _Alloc_type;
      typedef __allocator<_Tp, __default_alloc_template<__threads, __inst> >
      allocator_type;
    };

  template<typename _Tp, typename _Alloc>
    struct _Alloc_traits<_Tp, __debug_alloc<_Alloc> >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __debug_alloc<_Alloc> > _Alloc_type;
      typedef __allocator<_Tp, __debug_alloc<_Alloc> > allocator_type;
    };
  //@}

  //@{
  /// Versions for the __allocator adaptor used with the predefined
  /// "SGI" style allocators.
  template<typename _Tp, typename _Tp1, int __inst>
    struct _Alloc_traits<_Tp,
                         __allocator<_Tp1, __malloc_alloc_template<__inst> > >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __malloc_alloc_template<__inst> > _Alloc_type;
      typedef __allocator<_Tp, __malloc_alloc_template<__inst> > allocator_type;
    };

  template<typename _Tp, typename _Tp1, bool __thr, int __inst>
    struct _Alloc_traits<_Tp, __allocator<_Tp1, __default_alloc_template<__thr, __inst> > >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __default_alloc_template<__thr,__inst> >
      _Alloc_type;
      typedef __allocator<_Tp, __default_alloc_template<__thr,__inst> >
      allocator_type;
    };

  template<typename _Tp, typename _Tp1, typename _Alloc>
    struct _Alloc_traits<_Tp, __allocator<_Tp1, __debug_alloc<_Alloc> > >
    {
      static const bool _S_instanceless = true;
      typedef __simple_alloc<_Tp, __debug_alloc<_Alloc> > _Alloc_type;
      typedef __allocator<_Tp, __debug_alloc<_Alloc> > allocator_type;
    };
  //@}

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.
  // NB: This syntax is a GNU extension.
#if _GLIBCPP_EXTERN_TEMPLATE
  extern template class allocator<char>;
  extern template class allocator<wchar_t>;
  extern template class __default_alloc_template<true,0>;
#endif
} // namespace std

#endif
