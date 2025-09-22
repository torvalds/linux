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

// 23.2.2.1 list constructors, copy, and assignment

#include <list>
#include <testsuite_hooks.h>

bool test = true;

// A nontrivial type.
template<typename T>
  struct A { };

// Another nontrivial type
struct B { };

// A nontrivial type convertible from an int
struct C {
  C(int i) : i_(i) { }
  bool operator==(const C& rhs) { return i_ == rhs.i_; }
  int i_;
};

// Default constructor, basic properties
//
// This test verifies the following.
// 23.2.2.1     explicit list(const a& = Allocator())
// 23.1 (7)     iterator behaviour of empty containers
// 23.2.2       iterator begin()
// 23.2.2       iterator end()
// 23.2.2       size_type size() const
// 23.2.2	existence of required typedefs
//
void
test01()
{
  std::list< A<B> > list0101;
  VERIFY(list0101.begin() == list0101.end());
  VERIFY(list0101.size() == 0);

  // check type definitions -- will fail compile if missing
  typedef std::list< A<B> >::reference              reference;
  typedef std::list< A<B> >::const_reference        const_reference;
  typedef std::list< A<B> >::iterator               iterator;
  typedef std::list< A<B> >::const_iterator         const_iterator;
  typedef std::list< A<B> >::size_type              size_type;
  typedef std::list< A<B> >::difference_type        difference_type;
  typedef std::list< A<B> >::value_type             value_type;
  typedef std::list< A<B> >::allocator_type         allocator_type;
  typedef std::list< A<B> >::pointer                pointer;
  typedef std::list< A<B> >::const_pointer          const_pointer;
  typedef std::list< A<B> >::reverse_iterator       reverse_iterator;
  typedef std::list< A<B> >::const_reverse_iterator const_reverse_iterator;

  // allocator checks?
}

// Fill constructor
//
// This test verifies the following.
// 23.2.2.1     explicit list(size_type n, const T& v = T(), const a& = Allocator())
// 23.2.2       const_iterator begin() const
// 23.2.2       const_iterator end() const
// 23.2.2       size_type size() const
//
void
test02()
{
  const int LIST_SIZE = 5;
  const int INIT_VALUE = 7;
  int count;
  std::list<int>::const_iterator i;

  // nontrivial value_type
  std::list< A<B> > list0201(LIST_SIZE);

  // default value
  std::list<int> list0202(LIST_SIZE);
  for (i = list0202.begin(), count = 0;
       i != list0202.end();
       ++i, ++count)
    VERIFY(*i == 0);
  VERIFY(count == LIST_SIZE);
  VERIFY(list0202.size() == LIST_SIZE);

  // explicit value
  std::list<int> list0203(LIST_SIZE, INIT_VALUE);
  for (i = list0203.begin(), count = 0;
       i != list0203.end();
       ++i, ++count)
    VERIFY(*i == INIT_VALUE);
  VERIFY(count == LIST_SIZE);
  VERIFY(list0203.size() == LIST_SIZE);
}

// Fill constructor disguised as a range constructor
void
test02D()
{
  const int LIST_SIZE = 5;
  const int INIT_VALUE = 7;
  int count = 0;
  std::list<C> list0204(LIST_SIZE, INIT_VALUE);
  std::list<C>::iterator i = list0204.begin();
  for (; i != list0204.end(); ++i, ++count)
    VERIFY(*i == INIT_VALUE);
  VERIFY(count == LIST_SIZE);
  VERIFY(list0204.size() == LIST_SIZE);
}

// Range constructor
//
// This test verifies the following.
// 23.2.2.1     template list(InputIterator f, InputIterator l, const Allocator& a = Allocator())
// 23.2.2       const_iterator begin() const
// 23.2.2       const_iterator end() const
// 23.2.2       size_type size() const
//
void
test03()
{
  const int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const int N = sizeof(A) / sizeof(int);
  int count;
  std::list<int>::const_iterator i;

  // construct from a dissimilar range
  std::list<int> list0301(A, A + N);
  for (i = list0301.begin(), count = 0;
       i != list0301.end();
       ++i, ++count)
    VERIFY(*i == A[count]);
  VERIFY(count == N);
  VERIFY(list0301.size() == N);

  // construct from a similar range
  std::list<int> list0302(list0301.begin(), list0301.end());
  for (i = list0302.begin(), count = 0;
       i != list0302.end();
       ++i, ++count)
    VERIFY(*i == A[count]);
  VERIFY(count == N);
  VERIFY(list0302.size() == N);
}

// Copy constructor
//
// This test verifies the following.
// 23.2.2.1     list(const list& x)
// 23.2.2       reverse_iterator rbegin() 
// 23.2.2       reverse_iterator rend()
// 23.2.2       size_type size() const
//
void
test04()
{
  const int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const int N = sizeof(A) / sizeof(int);
  int count;
  std::list<int>::reverse_iterator i;
  std::list<int> list0401(A, A + N);

  std::list<int> list0402(list0401);
  for (i = list0401.rbegin(), count = N - 1;
       i != list0401.rend();
       ++i, --count)
    VERIFY(*i == A[count]);
  VERIFY(count == -1);
  VERIFY(list0401.size() == N);
}

// Range assign
//
// This test verifies the following.
// 23.2.2.1     void assign(InputIterator f, InputIterator l)
// 23.2.2       const_iterator begin() const
// 23.2.2       const_iterator end() const
// 23.2.2       size_type size() const
//
void
test05()
{
  const int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
  const int B[] = {101, 102, 103, 104, 105};
  const int N = sizeof(A) / sizeof(int);
  const int M = sizeof(B) / sizeof(int);
  int count;
  std::list<int>::const_iterator i;

  std::list<int> list0501;

  // make it bigger
  list0501.assign(A, A + N);
  for (i = list0501.begin(), count = 0;
       i != list0501.end();
       ++i, ++count)
    VERIFY(*i == A[count]);
  VERIFY(count == N);
  VERIFY(list0501.size() == N);

  // make it smaller
  list0501.assign(B, B + M);
  for (i = list0501.begin(), count = 0;
       i != list0501.end();
       ++i, ++count)
    VERIFY(*i == B[count]);
  VERIFY(count == M);
  VERIFY(list0501.size() == M);
}

// Fill assign
//
// This test verifies the following.
// 23.2.2.1     void assign(size_type n, const T& v)
// 23.2.2       const_iterator begin() const
// 23.2.2       const_iterator end() const
// 23.2.2       size_type size() const
//
void
test06()
{
  const int BIG_LIST_SIZE = 11;
  const int BIG_INIT_VALUE = 7;
  const int SMALL_LIST_SIZE = 5;
  const int SMALL_INIT_VALUE = 17;
  int count;
  std::list<int>::const_iterator i;

  std::list<int> list0601;
  VERIFY(list0601.size() == 0);

  // make it bigger
  list0601.assign(BIG_LIST_SIZE, BIG_INIT_VALUE);
  for (i = list0601.begin(), count = 0;
       i != list0601.end();
       ++i, ++count)
    VERIFY(*i == BIG_INIT_VALUE);
  VERIFY(count == BIG_LIST_SIZE);
  VERIFY(list0601.size() == BIG_LIST_SIZE);

  // make it shrink
  list0601.assign(SMALL_LIST_SIZE, SMALL_INIT_VALUE);
  for (i = list0601.begin(), count = 0;
       i != list0601.end();
       ++i, ++count)
    VERIFY(*i == SMALL_INIT_VALUE);
  VERIFY(count == SMALL_LIST_SIZE);
  VERIFY(list0601.size() == SMALL_LIST_SIZE);
}

// Fill Assignment disguised as a Range Assignment
void
test06D()
{
  const int LIST_SIZE = 5;
  const int INIT_VALUE = 7;
  int count = 0;
  std::list<C> list0604;
  VERIFY(list0604.size() == 0);
  
  list0604.assign(LIST_SIZE, INIT_VALUE);
  std::list<C>::iterator i = list0604.begin();
  for (; i != list0604.end(); ++i, ++count)
    VERIFY(*i == INIT_VALUE);
  VERIFY(count == LIST_SIZE);
  VERIFY(list0604.size() == LIST_SIZE);
}

// Assignment operator
//
// This test verifies the following.
// 23.2.2       operator=(const list& x)
// 23.2.2       iterator begin()
// 23.2.2       iterator end()
// 23.2.2       size_type size() const
// 23.2.2       bool operator==(const list& x, const list& y)
//
void
test07()
{
  const int A[] = {701, 702, 703, 704, 705};
  const int N = sizeof(A) / sizeof(int);
  int count;
  std::list<int>::iterator i;

  std::list<int> list0701(A, A + N);
  VERIFY(list0701.size() == N);

  std::list<int> list0702;
  VERIFY(list0702.size() == 0);

  list0702 = list0701;
  VERIFY(list0702.size() == N);
  for (i = list0702.begin(), count = 0;
       i != list0702.end();
       ++i, ++count)
    VERIFY(*i == A[count]);
  VERIFY(count == N);
  VERIFY(list0702 == list0701);
}

int main()
{
  test01();
  test02(); 
  test02D(); 
  test03();
  test04();
  test05();
  test06();
  test06D();
  test07();

  return !test;
}
// vi:set sw=2 ts=2:
