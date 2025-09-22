// Copyright (C) 2001, 2003 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without Pred the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// 23.2.2.3 list modifiers [lib.list.modifiers]

#include <list>
#include <testsuite_hooks.h>

typedef __gnu_cxx_test::copy_tracker  T;

bool test = true;


// This test verifies the following.
//
// 23.2.2.3     void push_front(const T& x)
// 23.2.2.3     void push_back(const T& x)
// 23.2.2.3 (1) iterator and reference non-invalidation 
// 23.2.2.3 (1) exception effects
// 23.2.2.3 (2) complexity requirements
//
// 23.2.2.3     void pop_front()
// 23.2.2.3     void pop_back()
// 23.2.2.3 (3) iterator and reference non-invalidation 
// 23.2.2.3 (5) complexity requirements
//
// 23.2.2       const_iterator begin() const
// 23.2.2       iterator end() 
// 23.2.2       const_reverse_iterator rbegin() const
// 23.2.2       _reference front() 
// 23.2.2       const_reference front() const
// 23.2.2       reference back() 
// 23.2.2       const_reference back() const
//
void
test01()
{
  std::list<T> list0101;
  std::list<T>::const_iterator i;
  std::list<T>::const_reverse_iterator j;
  std::list<T>::iterator k;
  T::reset();

  list0101.push_back(T(1));     // list should be [1]
  VERIFY(list0101.size() == 1);
  VERIFY(T::copyCount() == 1);

  k = list0101.end();
  --k;
  VERIFY(k->id() == 1);
  VERIFY(k->id() == list0101.front().id());
  VERIFY(k->id() == list0101.back().id());

  list0101.push_front(T(2));    // list should be [2 1]
  VERIFY(list0101.size() == 2);
  VERIFY(T::copyCount() == 2);
  VERIFY(k->id() == 1);

  list0101.push_back(T(3));     // list should be [2 1 3]
  VERIFY(list0101.size() == 3);
  VERIFY(T::copyCount() == 3);
  VERIFY(k->id() == 1);

  try
  {
    list0101.push_back(T(4, true));
    VERIFY(("no exception thrown", false));
  }
  catch (...)
  {
    VERIFY(list0101.size() == 3);
    VERIFY(T::copyCount() == 4);
  }

  i = list0101.begin();
  VERIFY(i->id() == 2);
  VERIFY(i->id() == list0101.front().id());

  j = list0101.rbegin();
  VERIFY(j->id() == 3);
  VERIFY(j->id() == list0101.back().id());

  ++i;
  VERIFY(i->id() == 1);

  ++j;
  VERIFY(j->id() == 1);

  T::reset();

  list0101.pop_back();          // list should be [2 1]
  VERIFY(list0101.size() == 2);
  VERIFY(T::dtorCount() == 1);
  VERIFY(i->id() == 1);
  VERIFY(j->id() == 1);
  VERIFY(k->id() == 1);

  list0101.pop_front();          // list should be [1]
  VERIFY(list0101.size() == 1);
  VERIFY(T::dtorCount() == 2);
  VERIFY(i->id() == 1);
  VERIFY(j->id() == 1);
  VERIFY(k->id() == 1);
}

// general single insert/erase + swap
void
test02()
{
  std::list<T> list0201;
  T::reset();

  list0201.insert(list0201.begin(), T(1));     // list should be [1]
  VERIFY(list0201.size() == 1);
  VERIFY(T::copyCount() == 1);

  list0201.insert(list0201.end(), T(2));     // list should be [1 2]
  VERIFY(list0201.size() == 2);
  VERIFY(T::copyCount() == 2);

  std::list<T>::iterator i = list0201.begin();
  std::list<T>::const_iterator j = i;
  VERIFY(i->id() == 1); ++i;
  VERIFY(i->id() == 2);

  list0201.insert(i, T(3));     // list should be [1 3 2]
  VERIFY(list0201.size() == 3);
  VERIFY(T::copyCount() == 3);

  std::list<T>::const_iterator k = i;
  VERIFY(i->id() == 2); --i;
  VERIFY(i->id() == 3); --i;
  VERIFY(i->id() == 1); 
  VERIFY(j->id() == 1); 

  ++i; // will point to '3'
  T::reset();
  list0201.erase(i); // should be [1 2]
  VERIFY(list0201.size() == 2);
  VERIFY(T::dtorCount() == 1);
  VERIFY(k->id() == 2);
  VERIFY(j->id() == 1); 

  std::list<T> list0202;
  T::reset();
  VERIFY(list0202.size() == 0);
  VERIFY(T::copyCount() == 0);
  VERIFY(T::dtorCount() == 0);

  // member swap
  list0202.swap(list0201);
  VERIFY(list0201.size() == 0);
  VERIFY(list0202.size() == 2);
  VERIFY(T::copyCount() == 0);
  VERIFY(T::dtorCount() == 0);

  // specialized swap
  swap(list0201, list0202);
  VERIFY(list0201.size() == 2);
  VERIFY(list0202.size() == 0);
  VERIFY(T::copyCount() == 0);
  VERIFY(T::dtorCount() == 0);
}

// range and fill insert/erase + clear
// missing: o  fill insert disguised as a range insert in all its variants
//          o  exception effects
void
test03()
{
  std::list<T> list0301;
  T::reset();

  // fill insert at beginning of list / empty list
  list0301.insert(list0301.begin(), 3, T(11)); // should be [11 11 11]
  VERIFY(list0301.size() == 3);
  VERIFY(T::copyCount() == 3);

  // save iterators to verify post-insert validity
  std::list<T>::iterator b = list0301.begin();          
  std::list<T>::iterator m = list0301.end(); --m;          
  std::list<T>::iterator e = list0301.end();

  // fill insert at end of list
  T::reset();
  list0301.insert(list0301.end(), 3, T(13)); // should be [11 11 11 13 13 13]
  VERIFY(list0301.size() == 6);
  VERIFY(T::copyCount() == 3);
  VERIFY(b == list0301.begin() && b->id() == 11);
  VERIFY(e == list0301.end());
  VERIFY(m->id() == 11);

  // fill insert in the middle of list
  ++m;
  T::reset();
  list0301.insert(m, 3, T(12)); // should be [11 11 11 12 12 12 13 13 13]
  VERIFY(list0301.size() == 9);
  VERIFY(T::copyCount() == 3);
  VERIFY(b == list0301.begin() && b->id() == 11);
  VERIFY(e == list0301.end());
  VERIFY(m->id() == 13);

  // single erase
  T::reset();
  m = list0301.erase(m); // should be [11 11 11 12 12 12 13 13]
  VERIFY(list0301.size() == 8);
  VERIFY(T::dtorCount() == 1);
  VERIFY(b == list0301.begin() && b->id() == 11);
  VERIFY(e == list0301.end());
  VERIFY(m->id() == 13);

  // range erase
  T::reset();
  m = list0301.erase(list0301.begin(), m); // should be [13 13]
  VERIFY(list0301.size() == 2);
  VERIFY(T::dtorCount() == 6);
  VERIFY(m->id() == 13);

  // range fill at beginning
  const int A[] = {321, 322, 333};
  const int N = sizeof(A) / sizeof(int);
  T::reset();
  b = list0301.begin();          
  list0301.insert(b, A, A + N); // should be [321 322 333 13 13]
  VERIFY(list0301.size() == 5);
  VERIFY(T::copyCount() == 3);
  VERIFY(m->id() == 13);
  
  // range fill at end
  T::reset();
  list0301.insert(e, A, A + N); // should be [321 322 333 13 13 321 322 333]
  VERIFY(list0301.size() == 8);
  VERIFY(T::copyCount() == 3);
  VERIFY(e == list0301.end());
  VERIFY(m->id() == 13);
  
  // range fill in middle
  T::reset();
  list0301.insert(m, A, A + N); 
  VERIFY(list0301.size() == 11);
  VERIFY(T::copyCount() == 3);
  VERIFY(e == list0301.end());
  VERIFY(m->id() == 13);

  T::reset();
  list0301.clear();
  VERIFY(list0301.size() == 0);
  VERIFY(T::dtorCount() == 11);
  VERIFY(e == list0301.end());
}

int main()
{
    test01();
    test02();
    test03();

    return !test;
}
// vi:set sw=2 ts=2:
