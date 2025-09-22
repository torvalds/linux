// -*- C++ -*-
// Testing allocator for the C++ library testsuite.
//
// Copyright (C) 2002, 2003 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.
//
// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// This file provides an test instrumentation allocator that can be
// used to verify allocation functionality of standard library
// containers.  2002.11.25 smw

#ifndef _GLIBCPP_TESTSUITE_ALLOCATOR_H
#define _GLIBCPP_TESTSUITE_ALLOCATOR_H

#include <cstddef>
#include <limits>

namespace __gnu_cxx_test
{
  class allocation_tracker
  {
  public:
    typedef std::size_t    size_type; 
    
    static void*
    allocate(size_type blocksize)
    {
      allocationTotal_ += blocksize;
      return ::operator new(blocksize);
    }
    
    static void
    construct() { constructCount_++; }

    static void
    destroy() { destructCount_++; }

    static void
    deallocate(void* p, size_type blocksize)
    {
      ::operator delete(p);
      deallocationTotal_ += blocksize;
    }
    
    static size_type
    allocationTotal() { return allocationTotal_; }
    
    static size_type
    deallocationTotal() { return deallocationTotal_; }
    
    static int
    constructCount() { return constructCount_; }

    static int
    destructCount() { return destructCount_; }
    
    static void
    resetCounts()
    {
      allocationTotal_ = 0;
      deallocationTotal_ = 0;
      constructCount_ = 0;
    destructCount_ = 0;
    }

 private:
    static size_type  allocationTotal_;
    static size_type  deallocationTotal_;
    static int        constructCount_;
    static int        destructCount_;
  };

  // A simple basic allocator that just forwards to the
  // allocation_tracker to fulfill memory requests.  This class is
  // templated on the target object type, but tracker isn't.
  template<class T>
  class tracker_alloc
  {
  public:
    typedef T              value_type;
    typedef T*             pointer;
    typedef const T*       const_pointer;
    typedef T&             reference;
    typedef const T&       const_reference;
    typedef std::size_t    size_type; 
    typedef std::ptrdiff_t difference_type; 
    
    template<class U> struct rebind { typedef tracker_alloc<U> other; };
    
    pointer
    address(reference value) const
    { return &value; }
    
    const_pointer
    address(const_reference value) const
    { return &value; }
    
    tracker_alloc() throw()
    { }

    tracker_alloc(const tracker_alloc&) throw()
    { }

    template<class U>
      tracker_alloc(const tracker_alloc<U>&) throw()
      { }

    ~tracker_alloc() throw()
    { }

    size_type
    max_size() const throw()
    { return std::numeric_limits<std::size_t>::max() / sizeof(T); }

    pointer
    allocate(size_type n, const void* = 0)
    { 
      return static_cast<pointer>(allocation_tracker::allocate(n * sizeof(T)));
    }

    void
    construct(pointer p, const T& value)
    {
      new (p) T(value);
      allocation_tracker::construct();
    }

    void
    destroy(pointer p)
    {
      p->~T();
      allocation_tracker::destroy();
    }

    void
    deallocate(pointer p, size_type num)
    { allocation_tracker::deallocate(p, num * sizeof(T)); }
  };

  template<class T1, class T2>
    bool
    operator==(const tracker_alloc<T1>&, const tracker_alloc<T2>&) throw()
    { return true; }

  template<class T1, class T2>
    bool
    operator!=(const tracker_alloc<T1>&, const tracker_alloc<T2>&) throw()
    { return false; }
}; // namespace __gnu_cxx_test

#endif // _GLIBCPP_TESTSUITE_ALLOCATOR_H

