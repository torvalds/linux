// 1999-05-07
// bkoz 

// Copyright (C) 1999, 2002, 2003 Free Software Foundation, Inc.
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

// 23.2.4.2 vector capacity

#include <vector>
#include <stdexcept>
#include <testsuite_allocator.h>
#include <testsuite_hooks.h>

using __gnu_cxx_test::copy_tracker;
using __gnu_cxx_test::allocation_tracker;
using __gnu_cxx_test::tracker_alloc;
using __gnu_cxx_test::copy_constructor;
using __gnu_cxx_test::assignment_operator;
using __gnu_cxx_test::destructor;
 
template<typename T>
  struct A { };

struct B { };

void test01()
{
  // non POD types
  bool test = true;
  std::vector< A<B> > vec01;
  typedef std::vector< A<B> >::size_type size_type;

  size_type sz01 = vec01.capacity();
  vec01.reserve(100);
  size_type sz02 = vec01.capacity();
  VERIFY( sz02 >= sz01 );
  
  sz01 = vec01.size() + 5;
  vec01.resize(sz01);
  sz02 = vec01.size();
  VERIFY( sz01 == sz02 );

  sz01 = vec01.size() - 5;
  vec01.resize(sz01);
  sz02 = vec01.size();
  VERIFY( sz01 == sz02 );
}

// libstdc++/8230
void test02()
{
  bool test = true;
  {
    std::vector<int>  array;
    const std::size_t size = array.max_size();
    try 
      {
	array.reserve(size);
      } 
    catch (const std::length_error& error) 
      {
	test &= false;
      }
    catch (const std::bad_alloc& error)
      {
	test &= true;
      }
    catch (...)
      {
	test &= false;
      }
    VERIFY( test );
  }

  {
    std::vector<int>  array;
    const std::size_t size = array.max_size() + 1;
    try 
      {
	array.reserve(size);
      } 
    catch (const std::length_error& error) 
      {
	test &= true;
      }
    catch (...)
      {
	test &= false;
      }
    VERIFY( test );
  }
}

// Verifies basic functionality of reserve() with forced reallocation.
void
test_reserve()
{
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(3);
    const X::size_type old_size     = a.size();
    const X::size_type old_capacity = a.capacity();
    const X::size_type new_capacity = old_capacity + 10;
    T::reset();
    
    a.reserve(new_capacity);

    // [23.2.4.1 (2)]
    VERIFY(new_capacity <= a.capacity());
    // [23.2.4.1 (3)]
    VERIFY(old_size == a.size());
    VERIFY(copy_constructor::count() <= old_size);
    VERIFY(destructor::count() <= old_size);
  }
  // check for memory leaks
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());
}

// Verifies that reserve() with reallocation offers the strong
// exception guarantee.
void
test_reserve_exception_guarantee()
{
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    const X::size_type old_size     = a.size();
    const X::size_type old_capacity = a.capacity();
    const X::size_type new_capacity = old_capacity + 10;
    T::reset();
    copy_constructor::throw_on(3);
    
    try
    {
      a.reserve(new_capacity);
      VERIFY(("no exception thrown", false));
    }
    catch (...)
    {
    }

    VERIFY(old_capacity == a.capacity());
    VERIFY(copy_constructor::count() == destructor::count()+1);
  }
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());
}

int main()
{
  test01();
  test02();
  test_reserve();
  test_reserve_exception_guarantee();
  return 0;
}
