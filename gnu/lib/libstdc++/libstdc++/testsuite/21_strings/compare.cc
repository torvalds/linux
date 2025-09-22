// 980930 bkoz work with libstdc++v3

// Copyright (C) 1998, 1999 Free Software Foundation, Inc.
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

// 21.3.6.8 basic_string::compare
// int compare(const basic_string& str) const;
// int compare(size_type pos1, size_type n1, const basic_string& str) const;
// int compare(size_type pos1, size_type n1, const basic_string& str,
//             size_type pos2, size_type n2) const;
// int compare(const charT* s) const;
// int compare(size_type pos1, size_type n1,
//             const charT* s, size_type n2 = npos) const;

// NB compare should be thought of as a lexographical compare, ie how
// things would be sorted in a dictionary.

#include <string>
#include <testsuite_hooks.h>

enum want_value {lt=0, z=1, gt=2};

int
test_value(int result, want_value expected);

int
test_value(int result, want_value expected)
{
  bool pass = false;

  switch (expected) {
  case lt:
    if (result < 0)
      pass = true;
    break;
  case z:
    if (!result)
      pass = true;
    break;
  case gt:
    if (result > 0)
      pass = true;
    break;
  default:
    pass = false; //should not get here
  }

#ifdef DEBUG_ASSERT
  assert(pass);
#endif
  
  return 0;
}
 

int 
test01()
{
  using namespace std;

  string 	str_0("costa rica");
  string 	str_1("costa marbella");
  string 	str_2;

  //sanity check
  test_value(strcmp("costa marbella", "costa rica"), lt); 
  test_value(strcmp("costa rica", "costa rica"), z);
  test_value(strcmp(str_1.data(), str_0.data()), lt);
  test_value(strcmp(str_0.data(), str_1.data()), gt);
  test_value(strncmp(str_1.data(), str_0.data(), 6), z);
  test_value(strncmp(str_1.data(), str_0.data(), 14), lt);
  test_value(memcmp(str_1.data(), str_0.data(), 6), z);
  test_value(memcmp(str_1.data(), str_0.data(), 14), lt);
  test_value(memcmp("costa marbella", "costa rica", 14), lt);

  // int compare(const basic_string& str) const;
  test_value(str_0.compare(str_1), gt); //because r>m
  test_value(str_1.compare(str_0), lt); //because m<r
  str_2 = str_0;
  test_value(str_2.compare(str_0), z);
  str_2 = "cost";
  test_value(str_2.compare(str_0), lt);
  str_2 = "costa ricans";
  test_value(str_2.compare(str_0), gt);

  // int compare(size_type pos1, size_type n1, const basic_string& str) const;
  test_value(str_1.compare(0, 6, str_0), lt);
  str_2 = "cost";
  test_value(str_1.compare(0, 4, str_2), z);
  test_value(str_1.compare(0, 5, str_2), gt);

  // int compare(size_type pos1, size_type n1, const basic_string& str, 
  //		 size_type pos2, size_type n2) const;	
  test_value(str_1.compare(0, 6, str_0, 0, 6), z);
  test_value(str_1.compare(0, 7, str_0, 0, 7), lt);
  test_value(str_0.compare(0, 7, str_1, 0, 7), gt);

  // int compare(const charT* s) const;
  test_value(str_0.compare("costa marbella"), gt);
  test_value(str_1.compare("costa rica"), lt);
  str_2 = str_0;
  test_value(str_2.compare("costa rica"), z);
  test_value(str_2.compare("cost"), gt);			
  test_value(str_2.compare("costa ricans"), lt);	   

  // int compare(size_type pos, size_type n1, const charT* str,
  //             size_type n2 = npos) const;
  test_value(str_1.compare(0, 6, "costa rica", 0, 6), z); 
  test_value(str_1.compare(0, 7, "costa rica", 0, 7), lt); 
  test_value(str_0.compare(0, 7, "costa marbella", 0, 7), gt); 

  return 0;
}


int 
main() 
{
  test01();
  
  return 0;
}
