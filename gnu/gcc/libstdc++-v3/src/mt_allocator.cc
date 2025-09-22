// Allocator details.

// Copyright (C) 2004, 2005, 2006 Free Software Foundation, Inc.
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882:
//

#include <bits/c++config.h>
#include <ext/concurrence.h>
#include <ext/mt_allocator.h>
#include <cstring>

namespace
{
#ifdef __GTHREADS
  struct __freelist
  {
    typedef __gnu_cxx::__pool<true>::_Thread_record _Thread_record;
    _Thread_record* 	_M_thread_freelist;
    _Thread_record* 	_M_thread_freelist_array;
    size_t 		_M_max_threads;
    __gthread_key_t 	_M_key;

    ~__freelist()
    {
      if (_M_thread_freelist_array)
	{
	  __gthread_key_delete(_M_key);
	  ::operator delete(static_cast<void*>(_M_thread_freelist_array));
	}
    }
  };

  // Ensure freelist is constructed first.
  static __freelist freelist;
  __gnu_cxx::__mutex freelist_mutex;

  static void 
  _M_destroy_thread_key(void* __id)
  {
    // Return this thread id record to the front of thread_freelist.
    __gnu_cxx::__scoped_lock sentry(freelist_mutex);
    size_t _M_id = reinterpret_cast<size_t>(__id);

    typedef __gnu_cxx::__pool<true>::_Thread_record _Thread_record;
    _Thread_record* __tr = &freelist._M_thread_freelist_array[_M_id - 1];
    __tr->_M_next = freelist._M_thread_freelist;
    freelist._M_thread_freelist = __tr;
  }
#endif
} // anonymous namespace

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  void
  __pool<false>::_M_destroy() throw()
  {
    if (_M_init && !_M_options._M_force_new)
      {
	for (size_t __n = 0; __n < _M_bin_size; ++__n)
	  {
	    _Bin_record& __bin = _M_bin[__n];
	    while (__bin._M_address)
	      {
		_Block_address* __tmp = __bin._M_address->_M_next;
		::operator delete(__bin._M_address->_M_initial);
		__bin._M_address = __tmp;
	      }
	    ::operator delete(__bin._M_first);
	  }
	::operator delete(_M_bin);
	::operator delete(_M_binmap);
      }
  }

  void
  __pool<false>::_M_reclaim_block(char* __p, size_t __bytes)
  {
    // Round up to power of 2 and figure out which bin to use.
    const size_t __which = _M_binmap[__bytes];
    _Bin_record& __bin = _M_bin[__which];

    char* __c = __p - _M_get_align();
    _Block_record* __block = reinterpret_cast<_Block_record*>(__c);
      
    // Single threaded application - return to global pool.
    __block->_M_next = __bin._M_first[0];
    __bin._M_first[0] = __block;
  }

  char* 
  __pool<false>::_M_reserve_block(size_t __bytes, const size_t __thread_id)
  {
    // Round up to power of 2 and figure out which bin to use.
    const size_t __which = _M_binmap[__bytes];
    _Bin_record& __bin = _M_bin[__which];
    const _Tune& __options = _M_get_options();
    const size_t __bin_size = (__options._M_min_bin << __which) 
			       + __options._M_align;
    size_t __block_count = __options._M_chunk_size - sizeof(_Block_address);
    __block_count /= __bin_size;	  

    // Get a new block dynamically, set it up for use.
    void* __v = ::operator new(__options._M_chunk_size);
    _Block_address* __address = static_cast<_Block_address*>(__v);
    __address->_M_initial = __v;
    __address->_M_next = __bin._M_address;
    __bin._M_address = __address;

    char* __c = static_cast<char*>(__v) + sizeof(_Block_address);
    _Block_record* __block = reinterpret_cast<_Block_record*>(__c);
    __bin._M_first[__thread_id] = __block;
    while (--__block_count > 0)
      {
	__c += __bin_size;
	__block->_M_next = reinterpret_cast<_Block_record*>(__c);
	__block = __block->_M_next;
      }
    __block->_M_next = NULL;

    __block = __bin._M_first[__thread_id];
    __bin._M_first[__thread_id] = __block->_M_next;

    // NB: For alignment reasons, we can't use the first _M_align
    // bytes, even when sizeof(_Block_record) < _M_align.
    return reinterpret_cast<char*>(__block) + __options._M_align;
  }

  void
  __pool<false>::_M_initialize()
  {
    // _M_force_new must not change after the first allocate(), which
    // in turn calls this method, so if it's false, it's false forever
    // and we don't need to return here ever again.
    if (_M_options._M_force_new) 
      {
	_M_init = true;
	return;
      }
      
    // Create the bins.
    // Calculate the number of bins required based on _M_max_bytes.
    // _M_bin_size is statically-initialized to one.
    size_t __bin_size = _M_options._M_min_bin;
    while (_M_options._M_max_bytes > __bin_size)
      {
	__bin_size <<= 1;
	++_M_bin_size;
      }
      
    // Setup the bin map for quick lookup of the relevant bin.
    const size_t __j = (_M_options._M_max_bytes + 1) * sizeof(_Binmap_type);
    _M_binmap = static_cast<_Binmap_type*>(::operator new(__j));
    _Binmap_type* __bp = _M_binmap;
    _Binmap_type __bin_max = _M_options._M_min_bin;
    _Binmap_type __bint = 0;
    for (_Binmap_type __ct = 0; __ct <= _M_options._M_max_bytes; ++__ct)
      {
	if (__ct > __bin_max)
	  {
	    __bin_max <<= 1;
	    ++__bint;
	  }
	*__bp++ = __bint;
      }
      
    // Initialize _M_bin and its members.
    void* __v = ::operator new(sizeof(_Bin_record) * _M_bin_size);
    _M_bin = static_cast<_Bin_record*>(__v);
    for (size_t __n = 0; __n < _M_bin_size; ++__n)
      {
	_Bin_record& __bin = _M_bin[__n];
	__v = ::operator new(sizeof(_Block_record*));
	__bin._M_first = static_cast<_Block_record**>(__v);
	__bin._M_first[0] = NULL;
	__bin._M_address = NULL;
      }
    _M_init = true;
  }

  
#ifdef __GTHREADS
  void
  __pool<true>::_M_destroy() throw()
  {
    if (_M_init && !_M_options._M_force_new)
      {
	if (__gthread_active_p())
	  {
	    for (size_t __n = 0; __n < _M_bin_size; ++__n)
	      {
		_Bin_record& __bin = _M_bin[__n];
		while (__bin._M_address)
		  {
		    _Block_address* __tmp = __bin._M_address->_M_next;
		    ::operator delete(__bin._M_address->_M_initial);
		    __bin._M_address = __tmp;
		  }
		::operator delete(__bin._M_first);
		::operator delete(__bin._M_free);
		::operator delete(__bin._M_used);
		::operator delete(__bin._M_mutex);
	      }
	  }
	else
	  {
	    for (size_t __n = 0; __n < _M_bin_size; ++__n)
	      {
		_Bin_record& __bin = _M_bin[__n];
		while (__bin._M_address)
		  {
		    _Block_address* __tmp = __bin._M_address->_M_next;
		    ::operator delete(__bin._M_address->_M_initial);
		    __bin._M_address = __tmp;
		  }
		::operator delete(__bin._M_first);
	      }
	  }
	::operator delete(_M_bin);
	::operator delete(_M_binmap);
      }
  }

  void
  __pool<true>::_M_reclaim_block(char* __p, size_t __bytes)
  {
    // Round up to power of 2 and figure out which bin to use.
    const size_t __which = _M_binmap[__bytes];
    const _Bin_record& __bin = _M_bin[__which];

    // Know __p not null, assume valid block.
    char* __c = __p - _M_get_align();
    _Block_record* __block = reinterpret_cast<_Block_record*>(__c);
    if (__gthread_active_p())
      {
	// Calculate the number of records to remove from our freelist:
	// in order to avoid too much contention we wait until the
	// number of records is "high enough".
	const size_t __thread_id = _M_get_thread_id();
	const _Tune& __options = _M_get_options();	
	const size_t __limit = (100 * (_M_bin_size - __which)
				* __options._M_freelist_headroom);

	size_t __remove = __bin._M_free[__thread_id];
	__remove *= __options._M_freelist_headroom;

	// NB: We assume that reads of _Atomic_words are atomic.
	const size_t __max_threads = __options._M_max_threads + 1;
	_Atomic_word* const __reclaimed_base =
	  reinterpret_cast<_Atomic_word*>(__bin._M_used + __max_threads);
	const _Atomic_word __reclaimed = __reclaimed_base[__thread_id];
	const size_t __net_used = __bin._M_used[__thread_id] - __reclaimed;

	// NB: For performance sake we don't resync every time, in order
	// to spare atomic ops.  Note that if __reclaimed increased by,
	// say, 1024, since the last sync, it means that the other
	// threads executed the atomic in the else below at least the
	// same number of times (at least, because _M_reserve_block may
	// have decreased the counter), therefore one more cannot hurt.
	if (__reclaimed > 1024)
	  {
	    __bin._M_used[__thread_id] -= __reclaimed;
	    __atomic_add(&__reclaimed_base[__thread_id], -__reclaimed);
	  }

	if (__remove >= __net_used)
	  __remove -= __net_used;
	else
	  __remove = 0;
	if (__remove > __limit && __remove > __bin._M_free[__thread_id])
	  {
	    _Block_record* __first = __bin._M_first[__thread_id];
	    _Block_record* __tmp = __first;
	    __remove /= __options._M_freelist_headroom;
	    const size_t __removed = __remove;
	    while (--__remove > 0)
	      __tmp = __tmp->_M_next;
	    __bin._M_first[__thread_id] = __tmp->_M_next;
	    __bin._M_free[__thread_id] -= __removed;
	    
	    __gthread_mutex_lock(__bin._M_mutex);
	    __tmp->_M_next = __bin._M_first[0];
	    __bin._M_first[0] = __first;
	    __bin._M_free[0] += __removed;
	    __gthread_mutex_unlock(__bin._M_mutex);
	  }

	// Return this block to our list and update counters and
	// owner id as needed.
	if (__block->_M_thread_id == __thread_id)
	  --__bin._M_used[__thread_id];
	else
	  __atomic_add(&__reclaimed_base[__block->_M_thread_id], 1);

	__block->_M_next = __bin._M_first[__thread_id];
	__bin._M_first[__thread_id] = __block;
	
	++__bin._M_free[__thread_id];
      }
    else
      {
	// Not using threads, so single threaded application - return
	// to global pool.
	__block->_M_next = __bin._M_first[0];
	__bin._M_first[0] = __block;
      }
  }

  char* 
  __pool<true>::_M_reserve_block(size_t __bytes, const size_t __thread_id)
  {
    // Round up to power of 2 and figure out which bin to use.
    const size_t __which = _M_binmap[__bytes];
    const _Tune& __options = _M_get_options();
    const size_t __bin_size = ((__options._M_min_bin << __which)
			       + __options._M_align);
    size_t __block_count = __options._M_chunk_size - sizeof(_Block_address);
    __block_count /= __bin_size;	  
    
    // Are we using threads?
    // - Yes, check if there are free blocks on the global
    //   list. If so, grab up to __block_count blocks in one
    //   lock and change ownership. If the global list is 
    //   empty, we allocate a new chunk and add those blocks 
    //   directly to our own freelist (with us as owner).
    // - No, all operations are made directly to global pool 0
    //   no need to lock or change ownership but check for free
    //   blocks on global list (and if not add new ones) and
    //   get the first one.
    _Bin_record& __bin = _M_bin[__which];
    _Block_record* __block = NULL;
    if (__gthread_active_p())
      {
	// Resync the _M_used counters.
	const size_t __max_threads = __options._M_max_threads + 1;
	_Atomic_word* const __reclaimed_base =
	  reinterpret_cast<_Atomic_word*>(__bin._M_used + __max_threads);
	const _Atomic_word __reclaimed = __reclaimed_base[__thread_id];
	__bin._M_used[__thread_id] -= __reclaimed;
	__atomic_add(&__reclaimed_base[__thread_id], -__reclaimed);

	__gthread_mutex_lock(__bin._M_mutex);
	if (__bin._M_first[0] == NULL)
	  {
	    void* __v = ::operator new(__options._M_chunk_size);
	    _Block_address* __address = static_cast<_Block_address*>(__v);
	    __address->_M_initial = __v;
	    __address->_M_next = __bin._M_address;
	    __bin._M_address = __address;
	    __gthread_mutex_unlock(__bin._M_mutex);

	    // No need to hold the lock when we are adding a whole
	    // chunk to our own list.
	    char* __c = static_cast<char*>(__v) + sizeof(_Block_address);
	    __block = reinterpret_cast<_Block_record*>(__c);
	    __bin._M_free[__thread_id] = __block_count;
	    __bin._M_first[__thread_id] = __block;
	    while (--__block_count > 0)
	      {
		__c += __bin_size;
		__block->_M_next = reinterpret_cast<_Block_record*>(__c);
		__block = __block->_M_next;
	      }
	    __block->_M_next = NULL;
	  }
	else
	  {
	    // Is the number of required blocks greater than or equal
	    // to the number that can be provided by the global free
	    // list?
	    __bin._M_first[__thread_id] = __bin._M_first[0];
	    if (__block_count >= __bin._M_free[0])
	      {
		__bin._M_free[__thread_id] = __bin._M_free[0];
		__bin._M_free[0] = 0;
		__bin._M_first[0] = NULL;
	      }
	    else
	      {
		__bin._M_free[__thread_id] = __block_count;
		__bin._M_free[0] -= __block_count;
		__block = __bin._M_first[0];
		while (--__block_count > 0)
		  __block = __block->_M_next;
		__bin._M_first[0] = __block->_M_next;
		__block->_M_next = NULL;
	      }
	    __gthread_mutex_unlock(__bin._M_mutex);
	  }
      }
    else
      {
	void* __v = ::operator new(__options._M_chunk_size);
	_Block_address* __address = static_cast<_Block_address*>(__v);
	__address->_M_initial = __v;
	__address->_M_next = __bin._M_address;
	__bin._M_address = __address;

	char* __c = static_cast<char*>(__v) + sizeof(_Block_address);
	__block = reinterpret_cast<_Block_record*>(__c);
 	__bin._M_first[0] = __block;
	while (--__block_count > 0)
	  {
	    __c += __bin_size;
	    __block->_M_next = reinterpret_cast<_Block_record*>(__c);
	    __block = __block->_M_next;
	  }
	__block->_M_next = NULL;
      }
      
    __block = __bin._M_first[__thread_id];
    __bin._M_first[__thread_id] = __block->_M_next;

    if (__gthread_active_p())
      {
	__block->_M_thread_id = __thread_id;
	--__bin._M_free[__thread_id];
	++__bin._M_used[__thread_id];
      }

    // NB: For alignment reasons, we can't use the first _M_align
    // bytes, even when sizeof(_Block_record) < _M_align.
    return reinterpret_cast<char*>(__block) + __options._M_align;
  }

  void
  __pool<true>::_M_initialize()
  {
    // _M_force_new must not change after the first allocate(),
    // which in turn calls this method, so if it's false, it's false
    // forever and we don't need to return here ever again.
    if (_M_options._M_force_new) 
      {
	_M_init = true;
	return;
      }

    // Create the bins.
    // Calculate the number of bins required based on _M_max_bytes.
    // _M_bin_size is statically-initialized to one.
    size_t __bin_size = _M_options._M_min_bin;
    while (_M_options._M_max_bytes > __bin_size)
      {
	__bin_size <<= 1;
	++_M_bin_size;
      }
      
    // Setup the bin map for quick lookup of the relevant bin.
    const size_t __j = (_M_options._M_max_bytes + 1) * sizeof(_Binmap_type);
    _M_binmap = static_cast<_Binmap_type*>(::operator new(__j));
    _Binmap_type* __bp = _M_binmap;
    _Binmap_type __bin_max = _M_options._M_min_bin;
    _Binmap_type __bint = 0;
    for (_Binmap_type __ct = 0; __ct <= _M_options._M_max_bytes; ++__ct)
      {
	if (__ct > __bin_max)
	  {
	    __bin_max <<= 1;
	    ++__bint;
	  }
	*__bp++ = __bint;
      }
      
    // Initialize _M_bin and its members.
    void* __v = ::operator new(sizeof(_Bin_record) * _M_bin_size);
    _M_bin = static_cast<_Bin_record*>(__v);
      
    // If __gthread_active_p() create and initialize the list of
    // free thread ids. Single threaded applications use thread id 0
    // directly and have no need for this.
    if (__gthread_active_p())
      {
	{
	  __gnu_cxx::__scoped_lock sentry(freelist_mutex);

	  if (!freelist._M_thread_freelist_array
	      || freelist._M_max_threads < _M_options._M_max_threads)
	    {
	      const size_t __k = sizeof(_Thread_record)
				 * _M_options._M_max_threads;
	      __v = ::operator new(__k);
	      _M_thread_freelist = static_cast<_Thread_record*>(__v);

	      // NOTE! The first assignable thread id is 1 since the
	      // global pool uses id 0
	      size_t __i;
	      for (__i = 1; __i < _M_options._M_max_threads; ++__i)
		{
		  _Thread_record& __tr = _M_thread_freelist[__i - 1];
		  __tr._M_next = &_M_thread_freelist[__i];
		  __tr._M_id = __i;
		}

	      // Set last record.
	      _M_thread_freelist[__i - 1]._M_next = NULL;
	      _M_thread_freelist[__i - 1]._M_id = __i;

	      if (!freelist._M_thread_freelist_array)
		{
		  // Initialize per thread key to hold pointer to
		  // _M_thread_freelist.
		  __gthread_key_create(&freelist._M_key,
				       ::_M_destroy_thread_key);
		  freelist._M_thread_freelist = _M_thread_freelist;
		}
	      else
		{
		  _Thread_record* _M_old_freelist
		    = freelist._M_thread_freelist;
		  _Thread_record* _M_old_array
		    = freelist._M_thread_freelist_array;
		  freelist._M_thread_freelist
		    = &_M_thread_freelist[_M_old_freelist - _M_old_array];
		  while (_M_old_freelist)
		    {
		      size_t next_id;
		      if (_M_old_freelist->_M_next)
			next_id = _M_old_freelist->_M_next - _M_old_array;
		      else
			next_id = freelist._M_max_threads;
		      _M_thread_freelist[_M_old_freelist->_M_id - 1]._M_next
			= &_M_thread_freelist[next_id];
		      _M_old_freelist = _M_old_freelist->_M_next;
		    }
		  ::operator delete(static_cast<void*>(_M_old_array));
		}
	      freelist._M_thread_freelist_array = _M_thread_freelist;
	      freelist._M_max_threads = _M_options._M_max_threads;
	    }
	}

	const size_t __max_threads = _M_options._M_max_threads + 1;
	for (size_t __n = 0; __n < _M_bin_size; ++__n)
	  {
	    _Bin_record& __bin = _M_bin[__n];
	    __v = ::operator new(sizeof(_Block_record*) * __max_threads);
	    std::memset(__v, 0, sizeof(_Block_record*) * __max_threads);    
	    __bin._M_first = static_cast<_Block_record**>(__v);

	    __bin._M_address = NULL;

	    __v = ::operator new(sizeof(size_t) * __max_threads);
	    std::memset(__v, 0, sizeof(size_t) * __max_threads);

	    __bin._M_free = static_cast<size_t*>(__v);

	    __v = ::operator new(sizeof(size_t) * __max_threads
				 + sizeof(_Atomic_word) * __max_threads);
	    std::memset(__v, 0, (sizeof(size_t) * __max_threads
				 + sizeof(_Atomic_word) * __max_threads));
	    __bin._M_used = static_cast<size_t*>(__v);
	      
	    __v = ::operator new(sizeof(__gthread_mutex_t));
	    __bin._M_mutex = static_cast<__gthread_mutex_t*>(__v);
	      
#ifdef __GTHREAD_MUTEX_INIT
	    {
	      // Do not copy a POSIX/gthr mutex once in use.
	      __gthread_mutex_t __tmp = __GTHREAD_MUTEX_INIT;
	      *__bin._M_mutex = __tmp;
	    }
#else
	    { __GTHREAD_MUTEX_INIT_FUNCTION(__bin._M_mutex); }
#endif
	  }
      }
    else
      {
	for (size_t __n = 0; __n < _M_bin_size; ++__n)
	  {
	    _Bin_record& __bin = _M_bin[__n];
	    __v = ::operator new(sizeof(_Block_record*));
	    __bin._M_first = static_cast<_Block_record**>(__v);
	    __bin._M_first[0] = NULL;
	    __bin._M_address = NULL;
	  }
      }
    _M_init = true;
  }

  size_t
  __pool<true>::_M_get_thread_id()
  {
    // If we have thread support and it's active we check the thread
    // key value and return its id or if it's not set we take the
    // first record from _M_thread_freelist and sets the key and
    // returns it's id.
    if (__gthread_active_p())
      {
	void* v = __gthread_getspecific(freelist._M_key);
	size_t _M_id = (size_t)v;
	if (_M_id == 0)
	  {
	    {
	      __gnu_cxx::__scoped_lock sentry(freelist_mutex);
	      if (freelist._M_thread_freelist)
		{
		  _M_id = freelist._M_thread_freelist->_M_id;
		  freelist._M_thread_freelist
		    = freelist._M_thread_freelist->_M_next;
		}
	    }

	    __gthread_setspecific(freelist._M_key, (void*)_M_id);
	  }
	return _M_id >= _M_options._M_max_threads ? 0 : _M_id;
      }

    // Otherwise (no thread support or inactive) all requests are
    // served from the global pool 0.
    return 0;
  }

  // XXX GLIBCXX_ABI Deprecated
  void 
  __pool<true>::_M_destroy_thread_key(void*) { }

  // XXX GLIBCXX_ABI Deprecated
  void
  __pool<true>::_M_initialize(__destroy_handler)
  {
    // _M_force_new must not change after the first allocate(),
    // which in turn calls this method, so if it's false, it's false
    // forever and we don't need to return here ever again.
    if (_M_options._M_force_new) 
      {
	_M_init = true;
	return;
      }

    // Create the bins.
    // Calculate the number of bins required based on _M_max_bytes.
    // _M_bin_size is statically-initialized to one.
    size_t __bin_size = _M_options._M_min_bin;
    while (_M_options._M_max_bytes > __bin_size)
      {
	__bin_size <<= 1;
	++_M_bin_size;
      }
      
    // Setup the bin map for quick lookup of the relevant bin.
    const size_t __j = (_M_options._M_max_bytes + 1) * sizeof(_Binmap_type);
    _M_binmap = static_cast<_Binmap_type*>(::operator new(__j));
    _Binmap_type* __bp = _M_binmap;
    _Binmap_type __bin_max = _M_options._M_min_bin;
    _Binmap_type __bint = 0;
    for (_Binmap_type __ct = 0; __ct <= _M_options._M_max_bytes; ++__ct)
      {
	if (__ct > __bin_max)
	  {
	    __bin_max <<= 1;
	    ++__bint;
	  }
	*__bp++ = __bint;
      }
      
    // Initialize _M_bin and its members.
    void* __v = ::operator new(sizeof(_Bin_record) * _M_bin_size);
    _M_bin = static_cast<_Bin_record*>(__v);
      
    // If __gthread_active_p() create and initialize the list of
    // free thread ids. Single threaded applications use thread id 0
    // directly and have no need for this.
    if (__gthread_active_p())
      {
	{
	  __gnu_cxx::__scoped_lock sentry(freelist_mutex);

	  if (!freelist._M_thread_freelist_array
	      || freelist._M_max_threads < _M_options._M_max_threads)
	    {
	      const size_t __k = sizeof(_Thread_record)
				 * _M_options._M_max_threads;
	      __v = ::operator new(__k);
	      _M_thread_freelist = static_cast<_Thread_record*>(__v);

	      // NOTE! The first assignable thread id is 1 since the
	      // global pool uses id 0
	      size_t __i;
	      for (__i = 1; __i < _M_options._M_max_threads; ++__i)
		{
		  _Thread_record& __tr = _M_thread_freelist[__i - 1];
		  __tr._M_next = &_M_thread_freelist[__i];
		  __tr._M_id = __i;
		}

	      // Set last record.
	      _M_thread_freelist[__i - 1]._M_next = NULL;
	      _M_thread_freelist[__i - 1]._M_id = __i;

	      if (!freelist._M_thread_freelist_array)
		{
		  // Initialize per thread key to hold pointer to
		  // _M_thread_freelist.
		  __gthread_key_create(&freelist._M_key, 
				       ::_M_destroy_thread_key);
		  freelist._M_thread_freelist = _M_thread_freelist;
		}
	      else
		{
		  _Thread_record* _M_old_freelist
		    = freelist._M_thread_freelist;
		  _Thread_record* _M_old_array
		    = freelist._M_thread_freelist_array;
		  freelist._M_thread_freelist
		    = &_M_thread_freelist[_M_old_freelist - _M_old_array];
		  while (_M_old_freelist)
		    {
		      size_t next_id;
		      if (_M_old_freelist->_M_next)
			next_id = _M_old_freelist->_M_next - _M_old_array;
		      else
			next_id = freelist._M_max_threads;
		      _M_thread_freelist[_M_old_freelist->_M_id - 1]._M_next
			= &_M_thread_freelist[next_id];
		      _M_old_freelist = _M_old_freelist->_M_next;
		    }
		  ::operator delete(static_cast<void*>(_M_old_array));
		}
	      freelist._M_thread_freelist_array = _M_thread_freelist;
	      freelist._M_max_threads = _M_options._M_max_threads;
	    }
	}

	const size_t __max_threads = _M_options._M_max_threads + 1;
	for (size_t __n = 0; __n < _M_bin_size; ++__n)
	  {
	    _Bin_record& __bin = _M_bin[__n];
	    __v = ::operator new(sizeof(_Block_record*) * __max_threads);
	    std::memset(__v, 0, sizeof(_Block_record*) * __max_threads);
	    __bin._M_first = static_cast<_Block_record**>(__v);

	    __bin._M_address = NULL;

	    __v = ::operator new(sizeof(size_t) * __max_threads);
	    std::memset(__v, 0, sizeof(size_t) * __max_threads);
	    __bin._M_free = static_cast<size_t*>(__v);
	      
	    __v = ::operator new(sizeof(size_t) * __max_threads + 
				 sizeof(_Atomic_word) * __max_threads);
	    std::memset(__v, 0, (sizeof(size_t) * __max_threads
				 + sizeof(_Atomic_word) * __max_threads));
	    __bin._M_used = static_cast<size_t*>(__v);

	    __v = ::operator new(sizeof(__gthread_mutex_t));
	    __bin._M_mutex = static_cast<__gthread_mutex_t*>(__v);
	      
#ifdef __GTHREAD_MUTEX_INIT
	    {
	      // Do not copy a POSIX/gthr mutex once in use.
	      __gthread_mutex_t __tmp = __GTHREAD_MUTEX_INIT;
	      *__bin._M_mutex = __tmp;
	    }
#else
	    { __GTHREAD_MUTEX_INIT_FUNCTION(__bin._M_mutex); }
#endif
	  }
      }
    else
      {
	for (size_t __n = 0; __n < _M_bin_size; ++__n)
	  {
	    _Bin_record& __bin = _M_bin[__n];
	    __v = ::operator new(sizeof(_Block_record*));
	    __bin._M_first = static_cast<_Block_record**>(__v);
	    __bin._M_first[0] = NULL;
	    __bin._M_address = NULL;
	  }
      }
    _M_init = true;
  }
#endif

  // Instantiations.
  template class __mt_alloc<char>;
  template class __mt_alloc<wchar_t>;

_GLIBCXX_END_NAMESPACE
