// Copyright (C) 2000, 2002 Free Software Foundation
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

// 20.4.5 Template class auto_ptr [lib.auto.ptr]

#include <memory>
#include <testsuite_hooks.h>

struct A
{
  A() { ++ctor_count; }
  virtual ~A() { ++dtor_count; }
  static long ctor_count;
  static long dtor_count;
};
long A::ctor_count = 0;
long A::dtor_count = 0;

struct B : A
{
  B() { ++ctor_count; }
  virtual ~B() { ++dtor_count; }
  static long ctor_count;
  static long dtor_count;
};
long B::ctor_count = 0;
long B::dtor_count = 0;


struct reset_count_struct
{
  ~reset_count_struct()
  {
    A::ctor_count = 0;
    A::dtor_count = 0;
    B::ctor_count = 0;
    B::dtor_count = 0;
  }
};


// 20.4.5.1 auto_ptr constructors [lib.auto.ptr.cons]

// Construction from pointer
int
test01()
{
  reset_count_struct reset;
  bool test = true;

  std::auto_ptr<A> A_default;
  VERIFY( A_default.get() == 0 );
  VERIFY( A::ctor_count == 0 );
  VERIFY( A::dtor_count == 0 );
  VERIFY( B::ctor_count == 0 );
  VERIFY( B::dtor_count == 0 );

  std::auto_ptr<A> A_from_A(new A);
  VERIFY( A_from_A.get() != 0 );
  VERIFY( A::ctor_count == 1 );
  VERIFY( A::dtor_count == 0 );
  VERIFY( B::ctor_count == 0 );
  VERIFY( B::dtor_count == 0 );

  std::auto_ptr<A> A_from_B(new B);
  VERIFY( A_from_B.get() != 0 );
  VERIFY( A::ctor_count == 2 );
  VERIFY( A::dtor_count == 0 );
  VERIFY( B::ctor_count == 1 );
  VERIFY( B::dtor_count == 0 );

  return 0;
}

// Construction from std::auto_ptr
int
test02()
{
  reset_count_struct reset;
  bool test = true;

  std::auto_ptr<A> A_from_A(new A);
  std::auto_ptr<B> B_from_B(new B);

  std::auto_ptr<A> A_from_ptr_A(A_from_A);
  std::auto_ptr<A> A_from_ptr_B(B_from_B);
  VERIFY( A_from_A.get() == 0 );
  VERIFY( B_from_B.get() == 0 );
  VERIFY( A_from_ptr_A.get() != 0 );
  VERIFY( A_from_ptr_B.get() != 0 );
  VERIFY( A::ctor_count == 2 );
  VERIFY( A::dtor_count == 0 );
  VERIFY( B::ctor_count == 1 );
  VERIFY( B::dtor_count == 0 );

  return 0;
}

// Assignment from std::auto_ptr
int
test03()
{
  reset_count_struct reset;
  bool test = true;

  std::auto_ptr<A> A_from_ptr_A;
  std::auto_ptr<A> A_from_ptr_B;
  std::auto_ptr<A> A_from_A(new A);
  std::auto_ptr<B> B_from_B(new B);

  A_from_ptr_A = A_from_A;
  A_from_ptr_B = B_from_B;
  VERIFY( A_from_A.get() == 0 );
  VERIFY( B_from_B.get() == 0 );
  VERIFY( A_from_ptr_A.get() != 0 );
  VERIFY( A_from_ptr_B.get() != 0 );
  VERIFY( A::ctor_count == 2 );
  VERIFY( A::dtor_count == 0 );
  VERIFY( B::ctor_count == 1 );
  VERIFY( B::dtor_count == 0 );

  return 0;
}

// Destruction
int
test04()
{
  reset_count_struct reset;
  bool test = true;

  {/*lifetine scope*/
    std::auto_ptr<A> A_from_A(new A);
    std::auto_ptr<A> A_from_B(new B);
    std::auto_ptr<B> B_from_B(new B);
  }/*destructors called here*/

  VERIFY( A::ctor_count == 3 );
  VERIFY( A::dtor_count == 3 );
  VERIFY( B::ctor_count == 2 );
  VERIFY( B::dtor_count == 2 );

  return 0;
}

// Class member construction/destruction
template <typename T>
class pimpl
{
public:
  pimpl() : p_impl(new T) {}
private:
  std::auto_ptr<T> p_impl;
};

int 
test05()
{
  bool test = true;
  reset_count_struct reset;

  pimpl<A>();
  pimpl<B>();
  VERIFY( A::ctor_count == 2 );
  VERIFY( A::dtor_count == 2 );
  VERIFY( B::ctor_count == 1 );
  VERIFY( B::dtor_count == 1 );
  return 0;
}


// 20.4.5.2 auto_ptr members [lib.auto.ptr.members]

// Member access
int
test06()
{
  reset_count_struct reset;
  bool test = true;

  std::auto_ptr<A> A_from_A(new A);
  std::auto_ptr<A> A_from_A_ptr(A_from_A.release());
  VERIFY( A_from_A.get() == 0 );
  VERIFY( A_from_A_ptr.get() != 0 );
  VERIFY( A_from_A->ctor_count == 1 );
  VERIFY( (*A_from_A).dtor_count == 0 );

  A* A_ptr = A_from_A_ptr.get();

  A_from_A_ptr.reset(A_ptr);
  VERIFY( A_from_A_ptr.get() == A_ptr );
  VERIFY( A_from_A_ptr->ctor_count == 1 );
  VERIFY( (*A_from_A_ptr).dtor_count == 0 );

  A_from_A_ptr.reset(new A);
  VERIFY( A_from_A_ptr.get() != A_ptr );
  VERIFY( A_from_A_ptr->ctor_count == 2 );
  VERIFY( (*A_from_A_ptr).dtor_count == 1 );
  return 0;
}


// 20.4.5.3 auto_ptr conversions [lib.auto.ptr.conv]

// Parameters and return values
template <typename T>
static std::auto_ptr<T> source()
{
  return std::auto_ptr<T>(new T);
}

template <typename T>
static void drain(std::auto_ptr<T>)
{}

int
test07()
{
  bool test = true;
  reset_count_struct reset;

  drain(source<A>());
  // The resolution of core issue 84, now a DR, breaks this call.
  // drain<A>(source<B>());
  drain(source<B>());
  VERIFY( A::ctor_count == 2 );
  VERIFY( A::dtor_count == 2 );
  VERIFY( B::ctor_count == 1 );
  VERIFY( B::dtor_count == 1 );
  return 0;
}

// libstdc++/3946
// http://gcc.gnu.org/ml/libstdc++/2002-07/msg00024.html
struct Base { };
struct Derived : public Base { };

std::auto_ptr<Derived> 
conversiontest08() { return std::auto_ptr<Derived>(new Derived); }

void
test08()
{
  std::auto_ptr<Base> ptr;
  ptr = conversiontest08();
}


int 
main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
  test07();
  test08();

  return 0;
}
