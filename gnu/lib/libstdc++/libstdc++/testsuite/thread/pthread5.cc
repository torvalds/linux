// 2002-01-23  Loren J. Rittle <rittle@labs.mot.com> <ljrittle@acm.org>
// Adpated from libstdc++/5464 submitted by jjessel@amadeus.net
// Jean-Francois JESSEL (Amadeus SAS Development) 
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

#include <vector>
#include <list>
#include <string>

// Do not include <pthread.h> explicitly; if threads are properly
// configured for the port, then it is picked up free from STL headers.

#if __GTHREADS
#ifdef _GLIBCPP_HAVE_UNISTD_H
#include <unistd.h>	// To test for _POSIX_THREAD_PRIORITY_SCHEDULING
#endif

using namespace std;

#define NTHREADS 8
#define LOOPS 20

struct tt_t
{
  char buf[100];
  int  i;
};

void*
thread_function (void* arg)
{
  int myid = *(int*) arg;
  for (int i = 0; i < LOOPS; i++)
    {
      vector<tt_t> myvect1;

      for (int j = 0; j < 2000; j++)
	{
	  vector<tt_t> myvect2;
	  tt_t v;
	  v.i = j;
	  myvect1.push_back (v);
	  myvect2.push_back (v);
	  list<std::string *> mylist;
	  std::string string_array[4];
	  string_array[0] = "toto";
	  string_array[1] = "titi";
	  string_array[2] = "tata";
	  string_array[3] = "tutu";
	  for (int k = 0; k < 4; k++)
	    {
	      if (mylist.size ())
		{
		  list<std::string *>::iterator aIt;
		  for (aIt = mylist.begin (); aIt != mylist.end (); ++aIt)
		    {
		      if ((*aIt) == &(string_array[k]))
			abort ();
		    }
		}
	      mylist.push_back (&(string_array[k]));
	    }
	}
    }

  return arg;
}

int
main (int argc, char *argv[])
{
  int worker;
  pthread_t threads[NTHREADS];
  int ids[NTHREADS];
  void* status;

#if defined(__sun) && defined(__svr4__)
  pthread_setconcurrency (NTHREADS);
#endif

  pthread_attr_t tattr;
  int ret = pthread_attr_init (&tattr);
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
  ret = pthread_attr_setscope(&tattr, PTHREAD_SCOPE_SYSTEM);
#endif

  for (worker = 0; worker < NTHREADS; worker++)
    {
      ids[worker] = worker;
      if (pthread_create(&threads[worker], &tattr,
			 thread_function, &ids[worker]))
	abort ();
    }

  for (worker = 0; worker < NTHREADS; worker++)
    {
      if (pthread_join(threads[worker], static_cast<void **>(&status)))
	abort ();

      if (*((int *)status) != worker)
	abort ();
    }

  return (0);
}
#else
int main (void) {}
#endif
