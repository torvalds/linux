// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

// Some members need to be explicitly instantiated, so that users can build
// their own code with -fno-implicit-templates and not suffer from a zillion
// link errors.

// { dg-options "-fno-implicit-templates" }

#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <testsuite_hooks.h>


// PR libstdc++/3829
void
test01()
{
  using namespace std;
  bool test = true;
  string x ("   this is text");
  istringstream  sin (x);
  ostringstream  sout;

  // same order as in bits/std_iomanip.h
  sin >> resetiosflags(ios_base::dec)
      >> setiosflags(ios_base::dec)
      >> setbase(ios_base::dec)
      >> setfill('c')
      >> setprecision(5)
      >> setw(20)
      >> ws;
  VERIFY(sin.good());

  sout << resetiosflags(ios_base::dec)
       << setiosflags(ios_base::dec)
       << setbase(ios_base::dec)
       << setfill('c')
       << setprecision(5)
       << setw(20)
       << ends << flush << endl;
  VERIFY(sout.good());
}


int
main()
{
  test01();
  return 0;
}
