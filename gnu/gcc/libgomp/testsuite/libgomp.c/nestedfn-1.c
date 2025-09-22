/* { dg-do run } */

#include <omp.h>
#include <stdlib.h>

int
main (void)
{
  int a = 1, b = 2, c = 3;
  void
  foo (void)
  {
    int l = 0;
#pragma omp parallel shared (a) private (b) firstprivate (c) \
		     num_threads (2) reduction (||:l)
    {
      if (a != 1 || c != 3) l = 1;
#pragma omp barrier
      if (omp_get_thread_num () == 0)
	{
	  a = 4;
	  b = 5;
	  c = 6;
	}
#pragma omp barrier
      if (omp_get_thread_num () == 1)
	{
	  if (a != 4 || c != 3) l = 1;
	  a = 7;
	  b = 8;
	  c = 9;
	}
      else if (omp_get_num_threads () == 1)
	a = 7;
#pragma omp barrier
      if (omp_get_thread_num () == 0)
	if (a != 7 || b != 5 || c != 6) l = 1;
#pragma omp barrier
      if (omp_get_thread_num () == 1)
	if (a != 7 || b != 8 || c != 9) l = 1;
    }
    if (l)
      abort ();
  }
  foo ();
  if (a != 7)
    abort ();
  return 0;
}
