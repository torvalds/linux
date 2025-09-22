// 2001-12-27 pme
//
// Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
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

// 23.2.1.1 deque constructors, copy, and assignment

#include <deque>
#include <iterator>
#include <sstream>
#include <testsuite_allocator.h>
#include <testsuite_hooks.h>

using __gnu_cxx_test::copy_tracker;
using __gnu_cxx_test::allocation_tracker;
using __gnu_cxx_test::tracker_alloc;
using __gnu_cxx_test::copy_constructor;
using __gnu_cxx_test::assignment_operator;
using __gnu_cxx_test::counter;
using __gnu_cxx_test::destructor;

typedef std::deque<counter>   gdeque;

bool test = true;

// see http://gcc.gnu.org/ml/libstdc++/2001-11/msg00139.html
void
test01()
{
  assert_count (0);
  {
     gdeque   d(10);
     assert_count (10);
  }
  assert_count (0);
}


// 23.2.1     required types
//
// A missing required type will cause a compile failure.
//
void
requiredTypesCheck()
{
  typedef int             T;
  typedef std::deque<T>   X;

  typedef X::reference              reference;
  typedef X::const_reference        const_reference;
  typedef X::iterator               iterator;
  typedef X::const_iterator         const_iterator;
  typedef X::size_type              size_type;
  typedef X::difference_type        difference_type;
  typedef X::value_type             value_type;
  typedef X::allocator_type         allocator_type;
  typedef X::pointer                pointer;
  typedef X::const_pointer          const_pointer;
  typedef X::reverse_iterator       reverse_iterator;
  typedef X::const_reverse_iterator const_reverse_iterator;
}


// @fn defaultConstructorCheck
// Explicitly checks the default deque constructor and destructor for both
// trivial and non-trivial types.  In addition, the size() and empty()
// member functions are explicitly checked here since it should be their
// first use. Checking those functions means checking the begin() and
// end() and their const brethren functions as well.
//
// @verbatim
// 23.2.1.1   default ctor/dtor
//  effects:
//    23.2.1.1        constructs an empty deque using the specified allocator
//  postconditions:
//    23.1 table 65   u.size() == 0
//  throws:
//  complexity:
//    23.1 table 65   constant
//
// 23.2.1.2   bool empty() const
//  semantics:
//    23.1 table 65   a.size() == 0
//    23.1 (7)        a.begin() == a.end()
//  throws:
//  complexity:
//    23.1 table 65   constant
//
// 23.2.1.2   size_type size() const
//  semantics:
//    23.1 table 65   a.end() - a.begin()
//  throws:
//  complexity:
//    23.1 table 65(A) should be constant
//
// 23.2.1     iterator begin()
//            const_iterator begin() const
//            iterator end() 
//            const_iterator end() const
//  throws:
//    23.1 (10) pt. 4 does not throw
//  complexity:
//    23.1 table 65   constant
// @endverbatim
void
defaultConstructorCheckPOD()
{
  // setup
  typedef int             T;
  typedef std::deque<T>   X;

  // run test
  X u;

  // assert postconditions
  VERIFY(u.empty());
  VERIFY(0 == u.size());
  VERIFY(u.begin() == u.end());
  VERIFY(0 == std::distance(u.begin(), u.end()));

  // teardown
}


void
defaultConstructorCheck()
{
  // setup
  typedef copy_tracker  T;
  typedef std::deque<T>     X;

  copy_tracker::reset();

  // run test
  const X u;

  // assert postconditions
  VERIFY(u.empty());
  VERIFY(0 == u.size());
  VERIFY(u.begin() == u.end());
  VERIFY(0 == std::distance(u.begin(), u.end()));

  // teardown
}


// @fn copyConstructorCheck()
// Explicitly checks the deque copy constructor.  Continues verificaton of
// ancillary member functions documented under defaultConstructorCheck().
//
// This check also tests the push_back() member function.
//
// @verbatim
// 23.2.1     copy constructor
//  effects:
//  postconditions:
//    22.1.1 table 65 a == X(a)
//                    u == a
//  throws:
//  complexity:
//    22.1.1 table 65 linear
// @endverbatim
void
copyConstructorCheck()
{
  // setup
  typedef copy_tracker  T;
  typedef std::deque<T>     X;

  const int copyBaseSize = 17;  // arbitrary

  X a;
  for (int i = 0; i < copyBaseSize; ++i)
    a.push_back(i);
  copy_tracker::reset();

  // assert preconditions
  VERIFY(!a.empty());
  VERIFY(copyBaseSize == a.size());
  VERIFY(a.begin() != a.end());
  VERIFY(copyBaseSize == std::distance(a.begin(), a.end()));

  // run test
  X u = a;

  // assert postconditions
  VERIFY(u == a);
  VERIFY(copyBaseSize == copy_constructor::count());

  // teardown
}


// @fn fillConstructorCheck()
// This test explicitly verifies the basic fill constructor.  Like the default
// constructor, later tests depend on the fill constructor working correctly.
// That means this explicit test should preceed the later tests so the error
// message given on assertion failure can be more helpful n tracking the
// problem.
// 
// 23.2.1.1   fill constructor
//  complexity:
//    23.2.1.1        linear in N
void
fillConstructorCheck()
{
  // setup
  typedef copy_tracker  T;
  typedef std::deque<T>   X;

  const X::size_type  n(23);  
  const X::value_type t(111);

  copy_tracker::reset();

  // run test
  X a(n, t);

  // assert postconditions
  VERIFY(n == a.size());
  VERIFY(n == copy_constructor::count());

  // teardown
}


// @fn fillConstructorCheck2()
// Explicit check for fill constructors masqueraded as range constructors as
// elucidated in clause 23.1.1 paragraph 9 of the standard.
//
// 23.1.1 (9) fill constructor looking like a range constructor
void
fillConstructorCheck2()
{
  typedef copy_tracker  T;
  typedef std::deque<T>   X;

  const int f = 23;  
  const int l = 111;

  copy_tracker::reset();

  X a(f, l);

  VERIFY(f == a.size());
  VERIFY(f == copy_constructor::count());
}


// @fn rangeConstructorCheckForwardIterator()
// This test copies from one deque to another to force the copy
// constructor for T to be used because the compiler will kindly
// elide copies if the default constructor can be used with
// type conversions.  Trust me.
//
// 23.2.1.1   range constructor, forward iterators
void
rangeConstructorCheckForwardIterator()
{
  // setup
  typedef copy_tracker  T;
  typedef std::deque<T>   X;

  const X::size_type  n(726); 
  const X::value_type t(307);
  X source(n, t);
  X::iterator i = source.begin();
  X::iterator j = source.end();
  X::size_type rangeSize = std::distance(i, j);

  copy_tracker::reset();

  // test
  X a(i, j);

  // assert postconditions
  VERIFY(rangeSize == a.size());
  VERIFY(copy_constructor::count() <= rangeSize);
}


// @fn rangeConstructorCheckInputIterator()
// An explicit check for range construction on an input iterator
// range, which the standard expounds upon as having a different
// complexity than forward iterators.
//
// 23.2.1.1   range constructor, input iterators
void
rangeConstructorCheckInputIterator()
{
  typedef copy_tracker  T;
  typedef std::deque<T>     X;

  std::istringstream ibuf("1234567890123456789");
  const X::size_type rangeSize = ibuf.str().size();  
  std::istream_iterator<char>  i(ibuf);
  std::istream_iterator<char>  j;

  copy_tracker::reset();

  X a(i, j);

  VERIFY(rangeSize == a.size());
  VERIFY(copy_constructor::count() <= (2 * rangeSize));
}


// 23.2.1     copy assignment
void
copyAssignmentCheck()
{
  typedef copy_tracker  T;
  typedef std::deque<T>     X;

  const X::size_type  n(18);  
  const X::value_type t(1023);
  X a(n, t);
  X r;

  copy_tracker::reset();

  r = a;

  VERIFY(r == a);
  VERIFY(n == copy_constructor::count());
}


// 23.2.1.1   fill assignment
//
// The complexity check must check dtors+copyAssign and
// copyCtor+copyAssign because that's the way the SGI implementation
// works.  Dunno if it's true standard compliant (which specifies fill
// assignment in terms of erase and insert only), but it should work
// as (most) users expect and is more efficient.
void
fillAssignmentCheck()
{
  typedef copy_tracker  T;
  typedef std::deque<T>   X;

  const X::size_type  starting_size(10);  
  const X::value_type starting_value(66);
  const X::size_type  n(23);  
  const X::value_type t(111);

  X a(starting_size, starting_value);
  copy_tracker::reset();

  // preconditions
  VERIFY(starting_size == a.size());

  // test
  a.assign(n, t);

  // postconditions
  VERIFY(n == a.size());
  VERIFY(n == (copy_constructor::count() + assignment_operator::count()));
  VERIFY(starting_size == (destructor::count() + assignment_operator::count()));
}


// @verbatim
// 23.2.1     range assignment
// 23.2.1.1   deque constructors, copy, and assignment
//  effects:
//  Constructs a deque equal to the range [first, last), using the
//  specified allocator.
//
//      template<typename InputIterator>
//        assign(InputIterator first, InputIterator last);
//      
//    is equivalent to
//
//      erase(begin(), end());
//      insert(begin(), first, last);
//
//  postconditions:
//  throws:
//  complexity:
//    forward iterators: N calls to the copy constructor, 0 reallocations
//    input iterators:   2N calls to the copy constructor, log(N) reallocations
// @endverbatim
void
rangeAssignmentCheck()
{
  typedef copy_tracker  T;
  typedef std::deque<T>   X;

  const X::size_type  source_size(726); 
  const X::value_type source_value(307);
  const X::size_type  starting_size(10);  
  const X::value_type starting_value(66);

  X source(source_size, source_value);
  X::iterator i = source.begin();
  X::iterator j = source.end();
  X::size_type rangeSize = std::distance(i, j);

  X a(starting_size, starting_value);
  VERIFY(starting_size == a.size());

  copy_tracker::reset();

  a.assign(i, j);

  VERIFY(source == a);
  VERIFY(rangeSize == (copy_constructor::count() + assignment_operator::count()));
  VERIFY(starting_size == (destructor::count() + assignment_operator::count()));
}


// 23.1 (10)  range assignment
// 23.2.1.3   with exception
void
rangeAssignmentCheckWithException()
{
  // setup
  typedef copy_tracker T;
  typedef std::deque<T>    X;

  // test
  // What does "no effects" mean?
}


// 23.1.1 (9) fill assignment looking like a range assignment
void
fillAssignmentCheck2()
{
  // setup
  typedef copy_tracker T;
  typedef std::deque<T>    X;

  // test
  // What does "no effects" mean?
}

// Verify that the default deque constructor offers the basic exception
// guarantee.
void
test_default_ctor_exception_safety()
{
  // setup
  typedef copy_tracker T;
  typedef std::deque<T, tracker_alloc<T> > X;

  T::reset();
  copy_constructor::throw_on(3);
  allocation_tracker::resetCounts();

  // test
  try
  {
    X a(7);
    VERIFY(("no exception thrown", false));
  }
  catch (...)
  {
  }

  // assert postconditions
  VERIFY(allocation_tracker::allocationTotal() == allocation_tracker::deallocationTotal());

  // teardown
}

// Verify that the copy constructor offers the basic exception guarantee.
void
test_copy_ctor_exception_safety()
{
  // setup
  typedef copy_tracker T;
  typedef std::deque<T, tracker_alloc<T> > X;

  allocation_tracker::resetCounts();
  {
    X a(7);
    T::reset();
    copy_constructor::throw_on(3);


    // test
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
}


int main()
{
  // basic functionality and standard conformance checks
  requiredTypesCheck();
  defaultConstructorCheckPOD();
  defaultConstructorCheck();
  test_default_ctor_exception_safety();
  copyConstructorCheck();
  test_copy_ctor_exception_safety();
  fillConstructorCheck();
  fillConstructorCheck2();
  rangeConstructorCheckInputIterator();
  rangeConstructorCheckForwardIterator();
  copyAssignmentCheck();
  fillAssignmentCheck();
  fillAssignmentCheck2();
  rangeAssignmentCheck();
  rangeAssignmentCheckWithException();

  // specific bug fix checks
  test01();

  return !test;
}
