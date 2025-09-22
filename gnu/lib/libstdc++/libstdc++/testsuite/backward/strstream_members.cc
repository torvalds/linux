// Copyright (C) 2002 Free Software Foundation, Inc.
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

// backward strstream members

#include <strstream>
#include <testsuite_hooks.h>

// { dg-options "-Wno-deprecated" }

int test01()
{
   std::strstream s;
   for (unsigned i=0 ; i!= 1000 ; ++i)
      s << i << std::endl;
   s << std::ends;
   return 0;
}


int test02()
{
  std::ostrstream buf;
  buf << std::ends;
  char *s = buf.str ();
  delete [] s;
}

int main()
{
  test01();
  test02();
  return 0;
}
