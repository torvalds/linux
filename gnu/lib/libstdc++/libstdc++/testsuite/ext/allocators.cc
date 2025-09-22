// 2001-11-25  Phil Edwards  <pme@gcc.gnu.org>
//
// Copyright (C) 2001 Free Software Foundation, Inc.
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

// 20.4.1.1 allocator members

#include <memory>
#include <cstdlib>
#include <testsuite_hooks.h>

typedef std::__malloc_alloc_template<3>             weird_alloc;
template class std::__malloc_alloc_template<3>;

typedef std::__debug_alloc<weird_alloc>             debug_weird_alloc;
template class std::__debug_alloc<weird_alloc>;

typedef std::__default_alloc_template<true, 3>      unshared_normal_alloc;
template class std::__default_alloc_template<true, 3>;

typedef std::__default_alloc_template<false, 3>     unshared_singlethreaded;
template class std::__default_alloc_template<false, 3>;

//std::malloc_alloc test_malloc_alloc;

struct big
{
    long f[15];
};


bool         new_called;
bool         delete_called;
std::size_t  requested;

void* 
operator new(std::size_t n) throw(std::bad_alloc)
{
  new_called = true;
  requested = n;
  return std::malloc(n);
}

void
operator delete(void *v) throw()
{
  delete_called = true;
  return std::free(v);
}


template <typename arbitrary_SGIstyle_allocator,
          bool uses_global_new_and_delete>
void test()
{
  new_called = false;
  delete_called = false;
  requested = 0;

  std::__allocator<big, arbitrary_SGIstyle_allocator>   a;
  big *p = a.allocate(10);
  if (uses_global_new_and_delete)  VERIFY (requested >= (10*15*sizeof(long)));
  // Touch the far end of supposedly-allocated memory to check that we got
  // all of it.  Why "3"?  Because it's my favorite integer between e and pi.
  p[9].f[14] = 3;
  VERIFY (new_called == uses_global_new_and_delete );
  a.deallocate(p,10);
  VERIFY (delete_called == uses_global_new_and_delete );
}


// These just help tracking down error messages.
void test01() { test<weird_alloc,false>(); }
void test02() { test<debug_weird_alloc,false>(); }
void test03() { test<unshared_normal_alloc,true>(); }
void test04() { test<unshared_singlethreaded,true>(); }

int main()
{
  test01();
  test02();
  test03();
  test04();

  return 0;
}

