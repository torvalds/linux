// 2002-01-23  Loren J. Rittle <rittle@labs.mot.com> <ljrittle@acm.org>
// Adpated from libstdc++/5444 submitted by markus.breuer@materna.de
//
// Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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

// { dg-do run { target *-*-openbsd* *-*-freebsd* *-*-netbsd* *-*-linux* *-*-solaris* *-*-cygwin *-*-darwin* } }
// { dg-options "-pthread" { target *-*-openbsd* *-*-freebsd* *-*-netbsd* *-*-linux* } }
// { dg-options "-pthreads" { target *-*-solaris* } }

#include <string>
#include <map>
#include <vector>

// Do not include <pthread.h> explicitly; if threads are properly
// configured for the port, then it is picked up free from STL headers.

#if __GTHREADS

const int max_thread_count = 8;
const int loops = 100000;

const char* my_default = "Hallo Welt!";

const int upper_limit = 2500;
const int lower_limit = 1000;

typedef char charT;

typedef std::string String;

typedef String MyType;

void*
thread_main (void*)
{
  typedef std::map<unsigned int,MyType> Map;
  typedef Map::value_type Value_Pair;
  Map myMap;

  for (int loop = 0; loop < loops; loop++)
    {
      String& str = myMap[loop];
      str.append (my_default);
      myMap.insert (Value_Pair (loop, str));
      
      if (myMap.size () > upper_limit)
	{
	  while (myMap.size () > lower_limit)
	    {
	      Map::iterator it = myMap.begin ();
	      myMap.erase (it);
	    }
	}
    }

  return 0;
}

int
main (void)
{
  pthread_t tid[max_thread_count];

#if defined(__sun) && defined(__svr4__)
  pthread_setconcurrency (max_thread_count);
#endif

  for (int i = 0; i < max_thread_count; i++)
    pthread_create (&tid[i], NULL, thread_main, 0);

  for (int i = 0; i < max_thread_count; i++)
    pthread_join (tid[i], NULL);

  return 0;
}
#else
int main (void) {}
#endif
