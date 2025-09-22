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

// 25.?? algorithms, rotate()

#include <algorithm>
#include <testsuite_hooks.h>
#include <list>

bool test = true;

int A[] = {1, 2, 3, 4, 5, 6, 7};
int B[] = {2, 3, 4, 5, 6, 7, 1};
int C[] = {1, 2, 3, 4, 5, 6, 7};
int D[] = {5, 6, 7, 1, 2, 3, 4};
const int N = sizeof(A) / sizeof(int);

/* need a test for a forward iterator -- can't think of one that makes sense */

/* biderectional iterator */
void
test02()
{
    using std::rotate;
    typedef std::list<int> Container;

    Container a(A, A + N);
    VERIFY(std::equal(a.begin(), a.end(), A));

    Container::iterator i = a.begin();
    rotate(a.begin(), ++i, a.end());
    VERIFY(std::equal(a.begin(), a.end(), B));

    i = a.end();
    rotate(a.begin(), --i, a.end());
    VERIFY(std::equal(a.begin(), a.end(), C));

    i = a.begin();
    std::advance(i, 3);
    rotate(a.begin(), ++i, a.end());
    VERIFY(std::equal(a.begin(), a.end(), D));
}

/* random iterator */
void
test03()
{
    using std::rotate;
    rotate(A, A + 1, A + N);
    VERIFY(std::equal(A, A + N, B));

    rotate(A, A + N - 1, A + N);
    VERIFY(std::equal(A, A + N, C));

    rotate(A, A + 4, A + N);
    VERIFY(std::equal(A, A + N, D));
}

int
main(int argc, char* argv[])
{
    test02();
    test03();
    return !test;
}
