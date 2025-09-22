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

// 25.3.1 algorithms, sort()

#include <algorithm>
#include <testsuite_hooks.h>

bool test = true;

const int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
const int B[] = {10, 20, 1, 11, 2, 12, 3, 13, 4, 14, 5, 15, 6, 16, 7, 17, 8, 18, 9, 19};
const int C[] = {20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
const int N = sizeof(A) / sizeof(int);
const int logN = 3; // ln(N) rounded up
const int P = 7;

// comparison predicate for stable_sort: order by rightmost digit
struct CompLast
{
    bool
    operator()(const int x, const int y)
    { return x % 10 < y % 10; }
};

// This functor has the equivalent functionality of std::geater<>,
// but there is no dependency on <functional> and it also tracks the
// number of invocations since creation.
class Gt
{
public:
    static int count() { return itsCount; }
    static void reset() { itsCount = 0; }

    bool
    operator()(const int& x, const int& y)
    {
        ++itsCount;
        return x > y; 
    }

private:
    static int itsCount;
};

int Gt::itsCount = 0;


// 25.3.1.1 sort()
void
test01()
{
    int s1[N];
    std::copy(B, B + N, s1);
    VERIFY(std::equal(s1, s1 + N, B));

    std::sort(s1, s1 + N);
    VERIFY(std::equal(s1, s1 + N, A));

    Gt gt;
    gt.reset();
    std::sort(s1, s1 + N, gt);
    VERIFY(std::equal(s1, s1 + N, C));
}

// 25.3.1.2 stable_sort()
void
test02()
{
    int s1[N];
    std::copy(A, A + N, s1);
    VERIFY(std::equal(s1, s1 + N, A));

    std::stable_sort(s1, s1 + N, CompLast());
    VERIFY(std::equal(s1, s1 + N, B));

    std::stable_sort(s1, s1 + N);
    VERIFY(std::equal(s1, s1 + N, A));

    Gt gt;
    gt.reset();
    std::stable_sort(s1, s1 + N, gt);
    VERIFY(std::equal(s1, s1 + N, C));
    VERIFY(gt.count() <= N * logN * logN);
}

// 25.3.1.3 partial_sort()
void
test03()
{
    int s1[N];
    std::copy(B, B + N, s1);
    VERIFY(std::equal(s1, s1 + N, B));

    std::partial_sort(s1, s1 + P, s1 + N);
    VERIFY(std::equal(s1, s1 + P, A));

    Gt gt;
    gt.reset();
    std::partial_sort(s1, s1 + P, s1 + N, gt);
    VERIFY(std::equal(s1, s1 + P, C));
}

// 25.3.1.4 partial_sort_copy()
void
test04()
{
    using std::partial_sort_copy;

    int s1[N];
    std::copy(B, B + N, s1);
    VERIFY(std::equal(s1, s1 + N, B));

    int s2[2*N];

    partial_sort_copy(s1, s1 + N, s2, s2 + P);
    VERIFY(std::equal(s2, s2 + P, A));

    Gt gt;
    gt.reset();
    partial_sort_copy(s1, s1 + N, s2, s2 + P, gt);
    VERIFY(std::equal(s2, s2 + P, C));

    VERIFY(std::equal(s2, partial_sort_copy(s1, s1 + N, s2, s2 + 2*N), A));
}

// 25.3.2 nth_element()
void
test05()
{
    using std::nth_element;

    int s1[N];
    std::copy(B, B + N, s1);
    VERIFY(std::equal(s1, s1 + N, B));

    int* pn = s1 + (N / 2) - 1;
    nth_element(s1, pn, s1 + N);
    for (const int* i = pn; i < s1 + N; ++i) VERIFY(!(*i < *pn));

    CompLast pred;
    nth_element(s1, pn, s1 + N, pred);
    for (const int* i = pn; i < s1 + N; ++i) VERIFY(!pred(*i, *pn));
}

int
main(int argc, char* argv[])
{
    test01();
    test02();
    test03();
    test04();
    test05();

    return !test;
}
