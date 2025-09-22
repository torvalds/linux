// 24.1.5 Random access iterators
// 24.3.1 Iterator traits
// (basic_string and vector implementations)
//
// Copyright (C) 1999  Philip Martin
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or 
// (at your option) any later version.                             
//                                                         
// This program is distributed in the hope that it will be useful,   
// but WITHOUT ANY WARRANTY; without even the implied warranty of    
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software       
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
// USA


#include <string>
#include <vector>
#include <testsuite_hooks.h>

int 
string_stuff()
{
   int failures(0);

   std::string s("abcde");

   std::string::iterator i1(s.begin());
   if (*i1 != 'a')
      ++failures;

   ++i1;
   if (*i1 != 'b')
      ++failures;

   if (*i1++ != 'b')
       ++failures;
   if (*i1 != 'c')
      ++failures;

   ++ ++i1;
   if (*i1 != 'e')
      ++failures;

   --i1;
   if (*i1 != 'd')
      ++failures;

   if (*i1-- != 'd')
      ++failures;
   if (*i1 != 'c')
      ++failures;

   -- --i1;
   if (*i1 != 'a')
      ++failures;

   std::string::iterator i2;
   i2 = s.end();
   std::iterator_traits<std::string::iterator>::difference_type d1;
   d1 = i2 - i1;
   if (d1 != 5)
      ++failures;

   std::iterator_traits<std::string::iterator>::value_type v1;
   v1 = i1[0];
   if (v1 != 'a')
      ++failures;

   std::iterator_traits<std::string::iterator>::reference r1(i1[0]);
   if (r1 != 'a')
      ++failures;
   r1 = 'x';
   if (r1 != 'x')
      ++failures;
   r1 = 'a';

   if ((i1 != i2) != true)
      ++failures;
   if ((i1 == i2) != false)
      ++failures;
   if ((i1 <  i2) != true)
      ++failures;
   if ((i1 >  i2) != false)
      ++failures;
   if ((i1 <= i2) != true)
      ++failures;
   if ((i1 >= i2) != false)
      ++failures;

   std::string::iterator i3;
   i3 = i1;
   if ((i3 == i1) != true)
      ++failures;

   i3 += 5;
   if ((i3 == i2) != true)
      ++failures;

   i3 -= 5;
   if ((i3 == i1) != true)
      ++failures;

   if (i3 + 5 != i2)
      ++failures;

   if (5 + i3 != i2)
      ++failures;

   if (i2 - 5 != i3)
      ++failures;

   if (i1[0] != 'a')
      ++failures;

   i1[4] = 'x';
   if (i2[-1] != 'x')
      ++failures;
   i1[4] = 'e';

   i1[2] = 'x';
   if (i2[-3] != 'x')
      ++failures;
   i1[2] = 'c';

   std::string::const_iterator ci1(s.begin());
   if (*ci1 != 'a')
      ++failures;

   ++ci1;
   if (*ci1 != 'b')
      ++failures;

   if (*ci1++ != 'b')
      ++failures;
   if (*ci1 != 'c')
      ++failures;

   ++ ++ci1;
   if (*ci1 != 'e')
      ++failures;

   --ci1;
   if (*ci1 != 'd')
      ++failures;

   if (*ci1-- != 'd')
      ++failures;
   if (*ci1 != 'c')
      ++failures;

   -- --ci1;
   if (*ci1 != 'a')
      ++failures;

   std::string::const_iterator ci2;
   ci2 = s.end();
   std::iterator_traits<std::string::const_iterator>::difference_type d2;
   d2 = ci2 - ci1;
   if (d2 != 5)
     ++failures;

   std::iterator_traits<std::string::const_iterator>::value_type v2;
   v2 = ci1[0];
   if (v2 != 'a')
     ++failures;

   std::iterator_traits<std::string::const_iterator>::reference r2(ci1[0]);
   if (r2 != 'a')
      ++failures;

   if ((ci1 != ci2) != true)
      ++failures;
   if ((ci1 == ci2) != false)
      ++failures;
   if ((ci1 <  ci2) != true)
      ++failures;
   if ((ci1 >  ci2) != false)
      ++failures;
   if ((ci1 <= ci2) != true)
      ++failures;
   if ((ci1 >= ci2) != false)
      ++failures;

   std::string::const_iterator ci3;
   ci3 = ci1;
   if ((ci3 == ci1) != true)
      ++failures;

   ci3 += 5;
   if ((ci3 == ci2) != true)
      ++failures;

   ci3 -= 5;
   if ((ci3 == ci1) != true)
      ++failures;

   if (ci3 + 5 != ci2)
      ++failures;

   if (5 + ci3 != ci2)
      ++failures;

   if (ci2 - 5 != ci3)
      ++failures;

   if (ci1[2] != 'c')
      ++failures;

   if (ci2[-1] != 'e')
      ++failures;

   // iterator and const_iterator
   std::string::const_iterator ci4(i1);
   if ((ci4 == i1) != true)
      ++failures;
   if ((ci4 != i1) != false)
      ++failures;
   if ((ci4 < i1)  != false)
     ++failures;
   if ((ci4 > i1)  != false)
     ++failures;
   if ((ci4 <= i1) != true)
     ++failures;
   if ((ci4 >= i1) != true)
     ++failures;
   ci4 = i2;
   if ((i2 == ci4) != true)
     ++failures;
   if ((i2 < ci4)  != false)
     ++failures;
   if ((i2 > ci4)  != false)
     ++failures;
   if ((i2 <= ci4) != true)
     ++failures;
   if ((i2 >= ci4) != true)
     ++failures;

   const std::string cs("ABCDE");
   std::string::const_iterator ci5(cs.begin());
   if (ci5[0] != 'A')
      ++failures;

   return failures;
}

int 
vector_stuff()
{
   int failures(0);

   std::vector<int> v;
   v.push_back(int(1));
   v.push_back(int(2));
   v.push_back(int(3));
   v.push_back(int(4));
   v.push_back(int(5));

   std::vector<int>::iterator i1(v.begin());
   if (*i1 != 1)
      ++failures;

   ++i1;
   if (*i1 != 2)
      ++failures;

   if (*i1++ != 2)
      ++failures;
   if (*i1 != 3)
      ++failures;

   ++ ++i1;
   if (*i1 != 5)
      ++failures;

   --i1;
   if (*i1 != 4)
      ++failures;

   if (*i1-- != 4)
      ++failures;
   if (*i1 != 3)
      ++failures;

   -- --i1;
   if (*i1 != 1)
      ++failures;

   std::vector<int>::iterator i2;
   i2 = v.end();
   std::iterator_traits<std::vector<int>::iterator>::difference_type d1;
   d1 = i2 - i1;
   if (d1 != 5)
      ++failures;

   std::iterator_traits<std::vector<int>::iterator>::value_type v1;
   v1 = i1[0];
   if (v1 != 1)
      ++failures;

   std::iterator_traits<std::vector<int>::iterator>::reference r1(i1[0]);
   if (r1 != 1)
      ++failures;
   r1 = 9;
   if (r1 != 9)
      ++failures;
   r1 = 1;

   if ((i1 != i2) != true)
      ++failures;
   if ((i1 == i2) != false)
      ++failures;
   if ((i1 <  i2) != true)
      ++failures;
   if ((i1 >  i2) != false)
      ++failures;
   if ((i1 <= i2) != true)
      ++failures;
   if ((i1 >= i2) != false)
      ++failures;

   std::vector<int>::iterator i3;
   i3 = i1;
   if ((i3 == i1) != true)
      ++failures;

   i3 += 5;
   if ((i3 == i2) != true)
      ++failures;

   i3 -= 5;
   if ((i3 == i1) != true)
      ++failures;

   if (i3 + 5 != i2)
      ++failures;

   if (5 + i3 != i2)
      ++failures;

   if (i2 - 5 != i3)
      ++failures;

   if (i1[0] != 1)
      ++failures;

   i1[4] = 9;
   if (i2[-1] != 9)
      ++failures;
   i1[4] = 5;

   i1[2] = 9;
   if (i2[-3] != 9)
      ++failures;
   i1[2] = 3;

   std::vector<int>::const_iterator ci1(v.begin());
   if (*ci1 != 1)
      ++failures;

   ++ci1;
   if (*ci1 != 2)
      ++failures;

   if (*ci1++ != 2)
      ++failures;
   if (*ci1 != 3)
      ++failures;

   ++ ++ci1;
   if (*ci1 != 5)
      ++failures;

   --ci1;
   if (*ci1 != 4)
      ++failures;

   if (*ci1-- != 4)
      ++failures;
   if (*ci1 != 3)
      ++failures;

   -- --ci1;
   if (*ci1 != 1)
      ++failures;

   std::vector<int>::const_iterator ci2;
   ci2 = v.end();
   std::iterator_traits<std::vector<int>::const_iterator>::difference_type d2;
   d2 = ci2 - ci1;
   if (d2 != 5)
      ++failures;

   std::iterator_traits<std::vector<int>::const_iterator>::value_type v2;
   v2 = ci1[0];
   if (v2 != 1)
      ++failures;

   std::iterator_traits<std::vector<int>::const_iterator>::reference
      r2(ci1[0]);
   if (r2 != 1)
      ++failures;

   if ((ci1 != ci2) != true)
      ++failures;
   if ((ci1 == ci2) != false)
      ++failures;
   if ((ci1 <  ci2) != true)
      ++failures;
   if ((ci1 >  ci2) != false)
      ++failures;
   if ((ci1 <= ci2) != true)
      ++failures;
   if ((ci1 >= ci2) != false)
      ++failures;

   std::vector<int>::const_iterator ci3;
   ci3 = ci1;
   if ((ci3 == ci1) != true)
      ++failures;

   ci3 += 5;
   if ((ci3 == ci2) != true)
      ++failures;

   ci3 -= 5;
   if ((ci3 == ci1) != true)
      ++failures;

   if (ci3 + 5 != ci2)
      ++failures;

   if (5 + ci3 != ci2)
      ++failures;

   if (ci2 - 5 != ci3)
      ++failures;

   if (ci1[2] != 3)
      ++failures;

   if (ci2[-1] != 5)
      ++failures;

   // iterator to const_iterator
   std::vector<int>::const_iterator ci4(i1);
   if ((ci4 == i1) != true)
      ++failures;
   if ((ci4 != i1) != false)
      ++failures;
   if ((ci4 < i1)  != false)
     ++failures;
   if ((ci4 > i1)  != false)
     ++failures;
   if ((ci4 <= i1) != true)
     ++failures;
   if ((ci4 >= i1) != true)
     ++failures;
   ci4 = i2;
   if ((i2 == ci4) != true)
     ++failures;
   if ((i2 < ci4)  != false)
     ++failures;
   if ((i2 > ci4)  != false)
     ++failures;
   if ((i2 <= ci4) != true)
     ++failures;
   if ((i2 >= ci4) != true)
     ++failures;

   const std::vector<int> cv(v);
   std::vector<int>::const_iterator ci5(cv.begin());
   if (ci5[0] != 1)
      ++failures;

   std::vector<std::string> vs;
   vs.push_back(std::string("abc"));
   std::vector<std::string>::iterator ivs(vs.begin());
   if (ivs->c_str()[1] != 'b')
      ++failures;

   return failures;
}

int 
reverse_stuff()
{
   int failures(0);

   std::string s("abcde");

   std::string::reverse_iterator ri(s.rbegin());
   if (*ri != 'e')
      ++failures;

   std::iterator_traits<std::string::reverse_iterator>::difference_type d;
   d = s.rend() - ri;
   if (d != 5)
      ++failures;

   const std::string cs("abcde");
   std::string::const_reverse_iterator cri(cs.rend());
   if (cri - 5 != cs.rbegin())
      ++failures;

   return failures;
}

// the following should be compiler errors
// flag runtime errors in case they slip through the compiler
int 
wrong_stuff()
{
   int failures(0);

#ifdef ITER24_F1
   extern void f(std::vector<std::string*>::iterator);
   std::vector<std::string*> vs[2];
   f(vs);                       // address of array is not an iterator
   failures++;
#endif

#ifdef ITER24_F2
   std::string s;
   char *i = s.begin();         // begin() doesn't return a pointer
   failures++;
#endif

#ifdef ITER24_F3
   std::string::const_iterator ci;
   std::string::iterator i;
   if (i - ci)                  // remove const_ is a warning
      i++;
   // failures++;  only a warning
#endif

#ifdef ITER24_F4
   std::vector<char>::iterator iv;
   std::string::iterator is(iv);// vector<char> is not string
   failures++;
#endif

#ifdef ITER24_F5
   std::vector<char>::iterator iv;
   std::string::iterator is;
   if (iv == is)                // vector<char> is not string
      ++iv;
   failures++;
#endif

#ifdef ITER24_F6
   std::vector<char>::const_iterator ci;
   std::vector<char>::iterator i = ci;  // remove const_ is a warning
   ++i;
   // failures++; only a warning
#endif

#ifdef ITER24_F7
   std::vector<int> v(1);
   std::vector<int>::const_iterator ci(v.begin());
   *ci = 1;                     // cannot assign through const_iterator
   failures++;
#endif

#ifdef ITER24_F8
   std::vector<const int> v(1);
   std::vector<const int>::reference r(v.begin()[0]);
   r = 1;                       // cannot assign through reference to const
   failures++;
#endif

   return failures;
}

// libstdc++/6642
int
test6642()
{
   std::string s;
   std::string::iterator it = s.begin();
   std::string::const_iterator cit = s.begin();

   return it - cit;
}

int
main(int argc, char **argv)
{
   int failures(0);

   failures += string_stuff();

   failures += vector_stuff();

   failures += reverse_stuff();

   failures += wrong_stuff();

   failures += test6642();

#ifdef DEBUG_ASSERT
   assert (failures == 0);
#endif

   return 0;
}
