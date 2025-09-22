// 2002-01-23  Loren J. Rittle <rittle@labs.mot.com> <ljrittle@acm.org>
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

// This multi-threading C++/STL/POSIX code adheres to rules outlined here:
// http://www.sgi.com/tech/stl/thread_safety.html
//
// It is believed to exercise the allocation code in a manner that
// should reveal memory leaks (and, under rare cases, race conditions,
// if the STL threading support is fubar'd).

#include <list>

// Do not include <pthread.h> explicitly; if threads are properly
// configured for the port, then it is picked up free from STL headers.

#if __GTHREADS
using namespace std;

const int thread_cycles = 10;
const int thread_pairs = 10;
const unsigned max_size = 100;
const int iters = 10000;

class task_queue
{
public:
  task_queue ()
  {
    pthread_mutex_init (&fooLock, NULL);
    pthread_cond_init (&fooCond1, NULL);
    pthread_cond_init (&fooCond2, NULL);
  }
  ~task_queue ()
  {
    pthread_mutex_destroy (&fooLock);
    pthread_cond_destroy (&fooCond1);
    pthread_cond_destroy (&fooCond2);
  }
  list<int> foo;
  pthread_mutex_t fooLock;
  pthread_cond_t fooCond1;
  pthread_cond_t fooCond2;
};

void*
produce (void* t)
{
  task_queue& tq = *(static_cast<task_queue*> (t));
  int num = 0;
  while (num < iters)
    {
      pthread_mutex_lock (&tq.fooLock);
      while (tq.foo.size () >= max_size)
	pthread_cond_wait (&tq.fooCond1, &tq.fooLock);
      tq.foo.push_back (num++);
      pthread_cond_signal (&tq.fooCond2);
      pthread_mutex_unlock (&tq.fooLock);
    }
  return 0;
}

void*
consume (void* t)
{
  task_queue& tq = *(static_cast<task_queue*> (t));
  int num = 0;
  while (num < iters)
    {
      pthread_mutex_lock (&tq.fooLock);
      while (tq.foo.size () == 0)
	pthread_cond_wait (&tq.fooCond2, &tq.fooLock);
      if (tq.foo.front () != num++)
	abort ();
      tq.foo.pop_front ();
      pthread_cond_signal (&tq.fooCond1);
      pthread_mutex_unlock (&tq.fooLock);
    }
  return 0;
}

int
main (int argc, char** argv)
{
  pthread_t prod[thread_pairs];
  pthread_t cons[thread_pairs];

  task_queue* tq[thread_pairs];

#if defined(__sun) && defined(__svr4__)
  pthread_setconcurrency (thread_pairs * 2);
#endif

  for (int j = 0; j < thread_cycles; j++)
    {
      for (int i = 0; i < thread_pairs; i++)
	{
	  tq[i] = new task_queue;
	  pthread_create (&prod[i], NULL, produce, static_cast<void*> (tq[i]));
	  pthread_create (&cons[i], NULL, consume, static_cast<void*> (tq[i]));
	}

      for (int i = 0; i < thread_pairs; i++)
	{
	  pthread_join (prod[i], NULL);
	  pthread_join (cons[i], NULL);
#if defined(__FreeBSD__) && __FreeBSD__ < 5
	  // These lines are not required by POSIX since a successful
	  // join is suppose to detach as well...
	  pthread_detach (prod[i]);
	  pthread_detach (cons[i]);
	  // ...but they are according to the FreeBSD 4.X code base
	  // or else you get a memory leak.
#endif
	  delete tq[i];
	}
    }

  return 0;
}
#else
int main (void) {}
#endif
