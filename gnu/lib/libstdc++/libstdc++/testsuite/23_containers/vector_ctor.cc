// 1999-06-29 bkoz

// Copyright (C) 1999-2001, 2002, 2003 Free Software Foundation, Inc.
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

// 23.2.4.1 vector constructors, copy, and assignment

#include <vector>
#include <string>
#include <testsuite_allocator.h>
#include <testsuite_hooks.h>

using __gnu_cxx_test::copy_tracker;
using __gnu_cxx_test::allocation_tracker;
using __gnu_cxx_test::tracker_alloc;
using __gnu_cxx_test::copy_constructor;
using __gnu_cxx_test::assignment_operator;
 
template<typename T>
  struct A { };

struct B { };

void test01()
{
  // 1
  bool test = true;
  std::vector< A<B> > vec01;
  std::vector< A<B> > vec02(5);
  typedef std::vector< A<B> >::size_type size_type;

  vec01 = vec02;

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

// 2
template class std::vector<double>;
template class std::vector< A<B> >;


// libstdc++/102
void test02()
{
  std::vector<int> v1;
  std::vector<int> v2 (v1);
}

// test range constructors and range-fill constructor
void
test03()
{
  bool test = true;
  const int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const int B[] = {7, 7, 7, 7, 7};
  const int N = sizeof(A) / sizeof(int);
  const int M = sizeof(B) / sizeof(int);
  
  std::vector<int> v3(A, A + N);
  VERIFY(std::equal(v3.begin(), v3.end(), A));
  
  std::vector<int> v4(v3.begin(), v3.end());
  VERIFY(std::equal(v4.begin(), v4.end(), A));
  
  std::vector<int> v5(M, 7);
  VERIFY(std::equal(v5.begin(), v5.end(), B));
  VERIFY(std::equal(B, B + M, v5.begin()));
  
#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

// libstdc++/6513
void test04()
{
  bool test = true;
  const char* c_strings[5] = { "1", "2", "3", "4", "5" };
  std::vector<std::string> strings(c_strings, c_strings + 5);

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}


// @fn test_default_ctor_exception_gurantee This test verifies that if
// one of the vector's contained objects throws an exception from its
// constructor while the vector is being constructed and filled with
// default values, all memory is returned to the allocator whence it
// came.
void
test_default_ctor_exception_gurantee()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  copy_tracker::reset();
  copy_constructor::throw_on(3);
  allocation_tracker::resetCounts();

  // run test
  try
  {
    X a(7);
    VERIFY(("no exception thrown", false));
  }
  catch (...)
  {
  }

  // assert postconditions
  VERIFY(("memory leak detected:",
          allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal()));

  // teardown
}

// @fn test_copy_ctor_exception_gurantee This test verifies that if
// one of the vector's contained objects throws an exception from its
// constructor while the vector is being copy constructed, all memory
// is returned to the allocator whence it came.
void
test_copy_ctor_exception_gurantee()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    copy_tracker::reset();
    copy_constructor::throw_on(3);

    // run test
    try
    {
      X u(a);
      VERIFY(("no exception thrown", false));
    }
    catch (...)
    {
    }
  }

  // assert postconditions
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

// operator=()
//
// case 1: lhs.size() > rhs.size()
// case 2: lhs.size() < rhs.size() < lhs.capacity()
// case 3: lhs.capacity() < rhs.size()
//
void
test_assignment_operator_1()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X r(9);
  X a(r.size() - 2);
  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // preconditions
  VERIFY(r.size() > a.size());

  // run test
  r = a;

  // assert postconditions
  VERIFY(r == a);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_assignment_operator_2()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X r(1);
  r.reserve(17);
  X a(r.size() + 7);
  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // preconditions
  VERIFY(r.size() < a.size());
  VERIFY(a.size() < r.capacity());

  // run test
  r = a;

  // assert postconditions
  VERIFY(r == a);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_assignment_operator_3()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X r(1);
    X a(r.capacity() + 7);
    copy_tracker::reset();

    // preconditions
    VERIFY(r.capacity() < a.size());

    // run test
    r = a;

    // assert postconditions
    VERIFY(r == a);
  }
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_assignment_operator_3_exception_guarantee()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X r(1);
    X a(r.capacity() + 7);
    copy_tracker::reset();
    copy_constructor::throw_on(3);

    // preconditions
    VERIFY(r.capacity() < a.size());

    // run test
    try
    {
      r = a;
      VERIFY(("no exception thrown", false));
    }
    catch (...)
    {
    }
  }

  // assert postconditions
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

// fill assign()
//
// case 1: [23.2.4.1 (3)] n <= size()
// case 2: [23.2.4.1 (3)] size() < n <= capacity()
// case 3: [23.2.4.1 (3)] n > capacity()
// case 4: [23.2.4.1 (3)] n > capacity(), exception guarantees
// case 5: [23.1.1 (9)] fill assign disguised as a range assign
//
void
test_fill_assign_1()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X a(7);
  X::size_type old_size = a.size();
  X::size_type new_size = old_size - 2;
  const T t;

  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // run test
  a.assign(new_size, t);

  // assert postconditions
  VERIFY(a.size() == new_size);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_fill_assign_2()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X a(7);
  a.reserve(11);
  X::size_type old_size     = a.size();
  X::size_type old_capacity = a.capacity();
  X::size_type new_size     = old_size + 2;
  const T t;

  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // assert preconditions
  VERIFY(old_size < new_size);
  VERIFY(new_size <= old_capacity);

  // run test
  a.assign(new_size, t);

  // assert postconditions
  VERIFY(a.size() == new_size);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_fill_assign_3()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    X::size_type old_size     = a.size();
    X::size_type old_capacity = a.capacity();
    X::size_type new_size     = old_capacity + 4;
    const T t;

    copy_tracker::reset();

    // assert preconditions
    VERIFY(new_size > old_capacity);

    // run test
    a.assign(new_size, t);

    // assert postconditions
    VERIFY(a.size() == new_size);
  }

  VERIFY(allocation_tracker::allocationTotal() > 0);
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_fill_assign_3_exception_guarantee()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    X::size_type old_size     = a.size();
    X::size_type old_capacity = a.capacity();
    X::size_type new_size     = old_capacity + 4;
    const T t;

    copy_tracker::reset();
    copy_constructor::throw_on(3);

    // assert preconditions
    VERIFY(new_size > old_capacity);

    // run test
    try
    {
      a.assign(new_size, t);
      VERIFY(("no exception thrown", false));
    }
    catch (...)
    {
    }

    // assert postconditions
    VERIFY(a.size() == old_size);
    VERIFY(a.capacity() == old_capacity);
  }

  VERIFY(allocation_tracker::allocationTotal() > 0);
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_fill_assign_4()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X a(7);
  X::size_type old_size  = a.size();
  X::size_type new_size  = old_size - 2;
  X::size_type new_value = 117;

  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // run test
  a.assign(new_size, new_value);

  // assert postconditions
  VERIFY(a.size() == new_size);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

// range assign()
//
// case 1: [23.2.4.1 (2)] input iterator
// case 2: [23.2.4.1 (2)] forward iterator, distance(first, last) <= size()
// case 3: [23.2.4.1 (2)] 
//         forward iterator, size() < distance(first, last) <= capacity()
// case 4: [23.2.4.1 (2)] forward iterator, distance(first, last) > capacity()
// case 5: [23.2.4.1 (2)] 
//         forward iterator, distance(first, last) > capacity(), 
//         exception guarantees
void
test_range_assign_1()
{
  // @TODO
}

void
test_range_assign_2()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X a(7);
  X b(3);
  X::size_type old_size = a.size();

  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // assert preconditions
  VERIFY(b.size() < a.capacity());

  // run test
  a.assign(b.begin(), b.end());

  // assert postconditions
  VERIFY(a.size() == b.size());
  VERIFY(a == b);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_range_assign_3()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  X a(7);
  a.reserve(a.size() + 7);
  X b(a.size() + 3);
  X::size_type old_size = a.size();

  copy_tracker::reset();
  allocation_tracker::resetCounts();

  // assert preconditions
  VERIFY(a.size() < b.size());
  VERIFY(b.size() < a.capacity());

  // run test
  a.assign(b.begin(), b.end());

  // assert postconditions
  VERIFY(a.size() == b.size());
  VERIFY(a == b);
  VERIFY(allocation_tracker::allocationTotal() == 0);

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_range_assign_4()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    X b(a.capacity() + 7);
    X::size_type old_size = a.size();

    copy_tracker::reset();

    // assert preconditions
    VERIFY(b.size() > a.capacity());

    // run test
    a.assign(b.begin(), b.end());

    // assert postconditions
    VERIFY(a.size() == b.size());
    VERIFY(a == b);
  }
  VERIFY(allocation_tracker::allocationTotal() > 0);
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}

void
test_range_assign_4_exception_guarantee()
{
  // setup
  bool test = true;
  typedef copy_tracker T;
  typedef std::vector<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    X b(a.capacity() + 7);
    X::size_type old_size = a.size();

    copy_tracker::reset();
    copy_constructor::throw_on(3);

    // assert preconditions
    VERIFY(b.size() > a.capacity());

    // run test
    try
    {
      a.assign(b.begin(), b.end());
      VERIFY(("no exception thrown", false));
    }
    catch (...)
    {
    }
  }

  // assert postconditions
  VERIFY(allocation_tracker::allocationTotal() > 0);
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
  copy_tracker::reset();
  allocation_tracker::resetCounts();
}


int main()
{
  test01();
  test02(); 
  test03();
  test04();
  test_default_ctor_exception_gurantee();
  test_copy_ctor_exception_gurantee();
  test_assignment_operator_1();
  test_assignment_operator_2();
  test_assignment_operator_3();
  test_assignment_operator_3_exception_guarantee();
  test_fill_assign_1();
  test_fill_assign_2();
  test_fill_assign_3();
  test_fill_assign_3_exception_guarantee();
  test_fill_assign_4();
  test_range_assign_1();
  test_range_assign_2();
  test_range_assign_3();
  test_range_assign_4();
  test_range_assign_4_exception_guarantee();

  return 0;
}
