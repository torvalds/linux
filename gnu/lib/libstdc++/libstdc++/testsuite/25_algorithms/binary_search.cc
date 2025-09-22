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

// 25.3.3 [lib.alg.binary.search] Binary search algorithms.

#include <algorithm>
#include <testsuite_hooks.h>

bool test = true;

const int A[] = {1, 2, 3, 3, 3, 5, 8};
const int C[] = {8, 5, 3, 3, 3, 2, 1};
const int N = sizeof(A) / sizeof(int);

// A comparison, equalivalent to std::greater<int> without the
// dependency on <functional>.

struct gt
{
    bool
    operator()(const int& x, const int& y) const
    { return x > y; }
};

// Each test performs general-case, bookend, not-found condition,
// and predicate functional checks.

// 25.3.3.1 lower_bound, with and without comparison predicate
void
test01()
{
    using std::lower_bound;

    const int first = A[0];
    const int last = A[N - 1];

    const int* p = lower_bound(A, A + N, 3);
    VERIFY(p == A + 2);

    const int* q = lower_bound(A, A + N, first);
    VERIFY(q == A + 0);

    const int* r = lower_bound(A, A + N, last);
    VERIFY(r == A + N - 1);

    const int* s = lower_bound(A, A + N, 4);
    VERIFY(s == A + 5);

    const int* t = lower_bound(C, C + N, 3, gt());
    VERIFY(t == C + 2);

    const int* u = lower_bound(C, C + N, first, gt());
    VERIFY(u == C + N - 1);

    const int* v = lower_bound(C, C + N, last, gt());
    VERIFY(v == C + 0);

    const int* w = lower_bound(C, C + N, 4, gt());
    VERIFY(w == C + 2);
}

// 25.3.3.2 upper_bound, with and without comparison predicate
void
test02()
{
    using std::upper_bound;

    const int first = A[0];
    const int last = A[N - 1];

    const int* p = upper_bound(A, A + N, 3);
    VERIFY(p == A + 5);

    const int* q = upper_bound(A, A + N, first);
    VERIFY(q == A + 1);

    const int* r = upper_bound(A, A + N, last);
    VERIFY(r == A + N);

    const int* s = upper_bound(A, A + N, 4);
    VERIFY(s == A + 5);

    const int* t = upper_bound(C, C + N, 3, gt());
    VERIFY(t == C + 5);

    const int* u = upper_bound(C, C + N, first, gt());
    VERIFY(u == C + N);

    const int* v = upper_bound(C, C + N, last, gt());
    VERIFY(v == C + 1);

    const int* w = upper_bound(C, C + N, 4, gt());
    VERIFY(w == C + 2);
}

// 25.3.3.3 equal_range, with and without comparison predicate
void
test03()
{
    using std::equal_range;
    typedef std::pair<const int*, const int*> Ipair;
    
    const int first = A[0];
    const int last = A[N - 1];

    Ipair p = equal_range(A, A + N, 3);
    VERIFY(p.first == A + 2);
    VERIFY(p.second == A + 5);
    
    Ipair q = equal_range(A, A + N, first);
    VERIFY(q.first == A + 0);
    VERIFY(q.second == A + 1);
    
    Ipair r = equal_range(A, A + N, last);
    VERIFY(r.first == A + N - 1);
    VERIFY(r.second == A + N);
    
    Ipair s = equal_range(A, A + N, 4);
    VERIFY(s.first == A + 5);
    VERIFY(s.second == A + 5);
    
    Ipair t = equal_range(C, C + N, 3, gt());
    VERIFY(t.first == C + 2);
    VERIFY(t.second == C + 5);
    
    Ipair u = equal_range(C, C + N, first, gt());
    VERIFY(u.first == C + N - 1);
    VERIFY(u.second == C + N);
    
    Ipair v = equal_range(C, C + N, last, gt());
    VERIFY(v.first == C + 0);
    VERIFY(v.second == C + 1);
    
    Ipair w = equal_range(C, C + N, 4, gt());
    VERIFY(w.first == C + 2);
    VERIFY(w.second == C + 2);
}

// 25.3.3.4 binary_search, with and without comparison predicate
void
test04()
{
    using std::binary_search;
    
    const int first = A[0];
    const int last = A[N - 1];

    VERIFY(binary_search(A, A + N, 5));
    VERIFY(binary_search(A, A + N, first));
    VERIFY(binary_search(A, A + N, last));
    VERIFY(!binary_search(A, A + N, 4));

    VERIFY(binary_search(C, C + N, 5, gt()));
    VERIFY(binary_search(C, C + N, first, gt()));
    VERIFY(binary_search(C, C + N, last, gt()));
    VERIFY(!binary_search(C, C + N, 4, gt()));
}

int
main(int argc, char* argv[])
{
    test01();
    test02();
    test03();
    test04();

    return !test;
}
