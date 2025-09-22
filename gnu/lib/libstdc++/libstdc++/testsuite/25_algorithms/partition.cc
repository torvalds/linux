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

// 25.2.12 [lib.alg.partitions] Partitions.

#include <algorithm>
#include <functional>
#include <testsuite_hooks.h>

bool test = true;

const int A[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
const int B[] = {2, 4, 6, 8, 10, 12, 14, 16, 1, 3, 5, 7, 9, 11, 13, 15, 17};
const int N = sizeof(A) / sizeof(int);

struct Pred
{
    bool
    operator()(const int& x) const
    { return (x % 2) == 0; }
};

// 25.2.12 partition()
void
test01()
{
    using std::partition;

    int s1[N];
    std::copy(A, A + N, s1);

    Pred pred;
    int* m = partition(s1, s1 + N, pred);
    for (const int* i = s1; i < m; ++i) VERIFY(pred(*i));
    for (const int* i = m; i < s1 + N; ++i) VERIFY(!pred(*i));
}

// 25.2.12 stable_partition()
void
test02()
{
    using std::stable_partition;

    int s1[N];
    std::copy(A, A + N, s1);

    stable_partition(s1, s1 + N, Pred());
    VERIFY(std::equal(s1, s1 + N, B));
}

int
main(int argc, char* argv[])
{
    test01();
    test02();

    return !test;
}
