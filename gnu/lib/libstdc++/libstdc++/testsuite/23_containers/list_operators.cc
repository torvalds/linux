// Copyright (C) 2001 Free Software Foundation, Inc.
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

// 23.2.2.4 list operations [lib.list.ops]

#include <list>
#include <testsuite_hooks.h>

bool test = true;

// splice(p, x) + remove + reverse
void
test01()
{
  const int K = 417;
  const int A[] = {1, 2, 3, 4, 5};
  const int B[] = {K, K, K, K, K};
  const int N = sizeof(A) / sizeof(int);
  const int M = sizeof(B) / sizeof(int);

  std::list<int> list0101(A, A + N);
  std::list<int> list0102(B, B + M);
  std::list<int>::iterator p = list0101.begin();

  VERIFY(list0101.size() == N);
  VERIFY(list0102.size() == M);

  ++p;
  list0101.splice(p, list0102); // [1 K K K K K 2 3 4 5]
  VERIFY(list0101.size() == N + M);
  VERIFY(list0102.size() == 0);

  // remove range from middle
  list0101.remove(K);
  VERIFY(list0101.size() == N);

  // remove first element
  list0101.remove(1);
  VERIFY(list0101.size() == N - 1);

  // remove last element
  list0101.remove(5);
  VERIFY(list0101.size() == N - 2);

  // reverse
  list0101.reverse();
  p = list0101.begin();
  VERIFY(*p == 4); ++p;
  VERIFY(*p == 3); ++p;
  VERIFY(*p == 2); ++p;
  VERIFY(p == list0101.end());
}

// splice(p, x, i) + remove_if + operator==
void
test02()
{
  const int A[] = {1, 2, 3, 4, 5};
  const int B[] = {2, 1, 3, 4, 5};
  const int C[] = {1, 3, 4, 5, 2};
  const int N = sizeof(A) / sizeof(int);
  std::list<int> list0201(A, A + N);
  std::list<int> list0202(A, A + N);
  std::list<int> list0203(B, B + N);
  std::list<int> list0204(C, C + N);
  std::list<int>::iterator i = list0201.begin();

  // result should be unchanged
  list0201.splice(list0201.begin(), list0201, i);
  VERIFY(list0201 == list0202);

  // result should be [2 1 3 4 5]
  ++i;
  list0201.splice(list0201.begin(), list0201, i);
  VERIFY(list0201 != list0202);
  VERIFY(list0201 == list0203);

  // result should be [1 3 4 5 2]
  list0201.splice(list0201.end(), list0201, i);
  VERIFY(list0201 == list0204);
}

// splice(p, x, f, l) + sort + merge + unique
void
test03()
{
  const int A[] = {103, 203, 603, 303, 403, 503};
  const int B[] = {417, 417, 417, 417, 417};
  const int E[] = {103, 417, 417, 203, 603, 303, 403, 503};
  const int F[] = {103, 203, 303, 403, 417, 417, 503, 603};
  const int C[] = {103, 203, 303, 403, 417, 417, 417, 417, 417, 503, 603};
  const int D[] = {103, 203, 303, 403, 417, 503, 603};
  const int N = sizeof(A) / sizeof(int);
  const int M = sizeof(B) / sizeof(int);
  const int P = sizeof(C) / sizeof(int);
  const int Q = sizeof(D) / sizeof(int);
  const int R = sizeof(E) / sizeof(int);

  std::list<int> list0301(A, A + N);
  std::list<int> list0302(B, B + M);
  std::list<int> list0303(C, C + P);
  std::list<int> list0304(D, D + Q);
  std::list<int> list0305(E, E + R);
  std::list<int> list0306(F, F + R);
  std::list<int>::iterator p = list0301.begin();
  std::list<int>::iterator q = list0302.begin();

  ++p; ++q; ++q;
  list0301.splice(p, list0302, list0302.begin(), q);
  VERIFY(list0301 == list0305);
  VERIFY(list0301.size() == N + 2);
  VERIFY(list0302.size() == M - 2);

  list0301.sort();
  VERIFY(list0301 == list0306);

  list0301.merge(list0302);
  VERIFY(list0301.size() == N + M);
  VERIFY(list0302.size() == 0);
  VERIFY(list0301 == list0303);

  list0301.unique();
  VERIFY(list0301 == list0304);
}

// A comparison predicate to order by rightmost digit.  Tracks call counts for
// performance checks.
struct CompLastLt
{
  bool operator()(const int x, const int y) { ++itsCount; return x % 10 < y % 10; }
  static int count() { return itsCount; }
  static void reset() { itsCount = 0; }
  static int itsCount;
};

int CompLastLt::itsCount;

struct CompLastEq
{
  bool operator()(const int x, const int y) { ++itsCount; return x % 10 == y % 10; }
  static int count() { return itsCount; }
  static void reset() { itsCount = 0; }
  static int itsCount;
};

int CompLastEq::itsCount;

// sort(pred) + merge(pred) + unique(pred)
// also checks performance requirements
void
test04()
{
  const int A[] = {1, 2, 3, 4, 5, 6};
  const int B[] = {12, 15, 13, 14, 11};
  const int C[] = {11, 12, 13, 14, 15};
  const int D[] = {1, 11, 2, 12, 3, 13, 4, 14, 5, 15, 6};
  const int N = sizeof(A) / sizeof(int);
  const int M = sizeof(B) / sizeof(int);
  const int Q = sizeof(D) / sizeof(int);

  std::list<int> list0401(A, A + N);
  std::list<int> list0402(B, B + M);
  std::list<int> list0403(C, C + M);
  std::list<int> list0404(D, D + Q);
  std::list<int> list0405(A, A + N);

  // sort B
  CompLastLt lt;

  CompLastLt::reset();
  list0402.sort(lt);
  VERIFY(list0402 == list0403);

  CompLastLt::reset();
  list0401.merge(list0402, lt);
  VERIFY(list0401 == list0404);
  VERIFY(lt.count() <= (N + M - 1));

  CompLastEq eq;

  CompLastEq::reset();
  list0401.unique(eq);
  VERIFY(list0401 == list0405);
  VERIFY(eq.count() == (N + M - 1));
}

main(int argc, char* argv[])
{
    test01();
    test02();
    test03();
    test04();

    return !test;
}
// vi:set sw=2 ts=2:
