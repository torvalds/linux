// <memory> -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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
 * Copyright (c) 1997-1999
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 */

/** @file memory
 *  This is a Standard C++ Library header.  You should @c #include this header
 *  in your programs, rather than any of the "st[dl]_*.h" implementation files.
 */

#ifndef _CPP_MEMORY
#define _CPP_MEMORY 1

#pragma GCC system_header

#include <bits/stl_algobase.h>
#include <bits/stl_alloc.h>
#include <bits/stl_construct.h>
#include <bits/stl_iterator_base_types.h> //for iterator_traits
#include <bits/stl_uninitialized.h>
#include <bits/stl_raw_storage_iter.h>

namespace std
{
  /**
   *  @if maint
   *  This is a helper function.  The unused second parameter exists to
   *  permit the real get_temporary_buffer to use template parameter deduction.
   *
   *  XXX This should perhaps use the pool.
   *  @endif
   */
  template<typename _Tp>
    pair<_Tp*, ptrdiff_t>
    __get_temporary_buffer(ptrdiff_t __len, _Tp*)
    {
      if (__len > ptrdiff_t(INT_MAX / sizeof(_Tp)))
	__len = INT_MAX / sizeof(_Tp);
      
      while (__len > 0) 
	{
	  _Tp* __tmp = (_Tp*) std::malloc((std::size_t)__len * sizeof(_Tp));
	  if (__tmp != 0)
	    return pair<_Tp*, ptrdiff_t>(__tmp, __len);
	  __len /= 2;
	}
      return pair<_Tp*, ptrdiff_t>((_Tp*)0, 0);
    }

  /**
   *  @brief This is a mostly-useless wrapper around malloc().
   *  @param  len  The number of objects of type Tp.
   *  @return   See full description.
   *
   *  Reinventing the wheel, but this time with prettier spokes!
   *
   *  This function tries to obtain storage for @c len adjacent Tp objects.
   *  The objects themselves are not constructed, of course.  A pair<> is
   *  returned containing "the buffer s address and capacity (in the units of
   *  sizeof(Tp)), or a pair of 0 values if no storage can be obtained."
   *  Note that the capacity obtained may be less than that requested if the
   *  memory is unavailable; you should compare len with the .second return
   *  value.
   */
  template<typename _Tp>
    inline pair<_Tp*,ptrdiff_t>
    get_temporary_buffer(ptrdiff_t __len)
    { return __get_temporary_buffer(__len, (_Tp*) 0); }

  /**
   *  @brief The companion to get_temporary_buffer().
   *  @param  p  A buffer previously allocated by get_temporary_buffer.
   *  @return   None.
   *
   *  Frees the memory pointed to by p.
   */
  template<typename _Tp>
    void
    return_temporary_buffer(_Tp* __p)
    { std::free(__p); }

  /**
   *  A wrapper class to provide auto_ptr with reference semantics.  For
   *  example, an auto_ptr can be assigned (or constructed from) the result of
   *  a function which returns an auto_ptr by value.
   *
   *  All the auto_ptr_ref stuff should happen behind the scenes.
   */
  template<typename _Tp1>
    struct auto_ptr_ref
    {
      _Tp1* _M_ptr;
      
      explicit
      auto_ptr_ref(_Tp1* __p): _M_ptr(__p) { }
    };


  /**
   *  @brief  A simple smart pointer providing strict ownership semantics.
   *
   *  The Standard says:
   *  <pre>
   *  An @c auto_ptr owns the object it holds a pointer to.  Copying an
   *  @c auto_ptr copies the pointer and transfers ownership to the destination.
   *  If more than one @c auto_ptr owns the same object at the same time the
   *  behavior of the program is undefined.
   *
   *  The uses of @c auto_ptr include providing temporary exception-safety for
   *  dynamically allocated memory, passing ownership of dynamically allocated
   *  memory to a function, and returning dynamically allocated memory from a
   *  function.  @c auto_ptr does not meet the CopyConstructible and Assignable
   *  requirements for Standard Library <a href="tables.html#65">container</a>
   *  elements and thus instantiating a Standard Library container with an
   *  @c auto_ptr results in undefined behavior.
   *  </pre>
   *  Quoted from [20.4.5]/3.
   *
   *  Good examples of what can and cannot be done with auto_ptr can be found
   *  in the libstdc++ testsuite.
   *
   *  @if maint
   *  _GLIBCPP_RESOLVE_LIB_DEFECTS
   *  127.  auto_ptr<> conversion issues
   *  These resolutions have all been incorporated.
   *  @endif
   */
  template<typename _Tp>
    class auto_ptr
    {
    private:
      _Tp* _M_ptr;
      
    public:
      /// The pointed-to type.
      typedef _Tp element_type;
      
      /**
       *  @brief  An %auto_ptr is usually constructed from a raw pointer.
       *  @param  p  A pointer (defaults to NULL).
       *
       *  This object now @e owns the object pointed to by @a p.
       */
      explicit
      auto_ptr(element_type* __p = 0) throw() : _M_ptr(__p) { }

      /**
       *  @brief  An %auto_ptr can be constructed from another %auto_ptr.
       *  @param  a  Another %auto_ptr of the same type.
       *
       *  This object now @e owns the object previously owned by @a a,
       *  which has given up ownsership.
       */
      auto_ptr(auto_ptr& __a) throw() : _M_ptr(__a.release()) { }

      /**
       *  @brief  An %auto_ptr can be constructed from another %auto_ptr.
       *  @param  a  Another %auto_ptr of a different but related type.
       *
       *  A pointer-to-Tp1 must be convertible to a pointer-to-Tp/element_type.
       *
       *  This object now @e owns the object previously owned by @a a,
       *  which has given up ownsership.
       */
      template<typename _Tp1>
        auto_ptr(auto_ptr<_Tp1>& __a) throw() : _M_ptr(__a.release()) { }

      /**
       *  @brief  %auto_ptr assignment operator.
       *  @param  a  Another %auto_ptr of the same type.
       *
       *  This object now @e owns the object previously owned by @a a,
       *  which has given up ownsership.  The object that this one @e
       *  used to own and track has been deleted.
       */
      auto_ptr&
      operator=(auto_ptr& __a) throw()
      {
	reset(__a.release());
	return *this;
      }

      /**
       *  @brief  %auto_ptr assignment operator.
       *  @param  a  Another %auto_ptr of a different but related type.
       *
       *  A pointer-to-Tp1 must be convertible to a pointer-to-Tp/element_type.
       *
       *  This object now @e owns the object previously owned by @a a,
       *  which has given up ownsership.  The object that this one @e
       *  used to own and track has been deleted.
       */
      template<typename _Tp1>
        auto_ptr&
        operator=(auto_ptr<_Tp1>& __a) throw()
        {
	  reset(__a.release());
	  return *this;
	}

      /**
       *  When the %auto_ptr goes out of scope, the object it owns is deleted.
       *  If it no longer owns anything (i.e., @c get() is @c NULL), then this
       *  has no effect.
       *
       *  @if maint
       *  The C++ standard says there is supposed to be an empty throw
       *  specification here, but omitting it is standard conforming.  Its
       *  presence can be detected only if _Tp::~_Tp() throws, but this is
       *  prohibited.  [17.4.3.6]/2
       *  @end maint
       */
      ~auto_ptr() { delete _M_ptr; }
      
      /**
       *  @brief  Smart pointer dereferencing.
       *
       *  If this %auto_ptr no longer owns anything, then this
       *  operation will crash.  (For a smart pointer, "no longer owns
       *  anything" is the same as being a null pointer, and you know
       *  what happens when you dereference one of those...)
       */
      element_type&
      operator*() const throw() { return *_M_ptr; }
      
      /**
       *  @brief  Smart pointer dereferencing.
       *
       *  This returns the pointer itself, which the language then will
       *  automatically cause to be dereferenced.
       */
      element_type*
      operator->() const throw() { return _M_ptr; }
      
      /**
       *  @brief  Bypassing the smart pointer.
       *  @return  The raw pointer being managed.
       *
       *  You can get a copy of the pointer that this object owns, for
       *  situations such as passing to a function which only accepts a raw
       *  pointer.
       *
       *  @note  This %auto_ptr still owns the memory.
       */
      element_type*
      get() const throw() { return _M_ptr; }
      
      /**
       *  @brief  Bypassing the smart pointer.
       *  @return  The raw pointer being managed.
       *
       *  You can get a copy of the pointer that this object owns, for
       *  situations such as passing to a function which only accepts a raw
       *  pointer.
       *
       *  @note  This %auto_ptr no longer owns the memory.  When this object
       *  goes out of scope, nothing will happen.
       */
      element_type*
      release() throw()
      {
	element_type* __tmp = _M_ptr;
	_M_ptr = 0;
	return __tmp;
      }
      
      /**
       *  @brief  Forcibly deletes the managed object.
       *  @param  p  A pointer (defaults to NULL).
       *
       *  This object now @e owns the object pointed to by @a p.  The previous
       *  object has been deleted.
       */
      void
      reset(element_type* __p = 0) throw()
      {
	if (__p != _M_ptr)
	  {
	    delete _M_ptr;
	    _M_ptr = __p;
	  }
      }
      
      /** @{
       *  @brief  Automatic conversions
       *
       *  These operations convert an %auto_ptr into and from an auto_ptr_ref
       *  automatically as needed.  This allows constructs such as
       *  @code
       *    auto_ptr<Derived>  func_returning_auto_ptr(.....);
       *    ...
       *    auto_ptr<Base> ptr = func_returning_auto_ptr(.....);
       *  @endcode
       */
      auto_ptr(auto_ptr_ref<element_type> __ref) throw()
      : _M_ptr(__ref._M_ptr) { }
      
      auto_ptr&
      operator=(auto_ptr_ref<element_type> __ref) throw()
      {
	if (__ref._M_ptr != this->get())
	  {
	    delete _M_ptr;
	    _M_ptr = __ref._M_ptr;
	  }
	return *this;
      }
      
      template<typename _Tp1>
        operator auto_ptr_ref<_Tp1>() throw()
        { return auto_ptr_ref<_Tp1>(this->release()); }

      template<typename _Tp1>
        operator auto_ptr<_Tp1>() throw()
        { return auto_ptr<_Tp1>(this->release()); }
      /** @}  */
  };
} // namespace std

#endif 
