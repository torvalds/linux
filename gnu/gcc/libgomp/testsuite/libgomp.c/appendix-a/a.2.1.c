/* { dg-do run } */

#include <stdio.h>
#include <omp.h>
extern void abort (void);
int
main ()
{
  int bad, x;
  x = 2;
  bad = 0;
#pragma omp parallel num_threads(2) shared(x, bad)
  {
    if (omp_get_thread_num () == 0)
      {
	volatile int i;
	for (i = 0; i < 100000000; i++)
	  x = 5;
      }
    else
      {
	/* Print 1: the following read of x has a race */
	if (x != 2 && x != 5)
	  bad = 1;
      }
#pragma omp barrier
    if (omp_get_thread_num () == 0)
      {
	/* x must be 5 now.  */
	if (x != 5)
	  bad = 1;
      }
    else
      {
	/* x must be 5 now.  */
	if (x != 5)
	  bad = 1;
      }
  }

  if (bad)
    abort ();

  return 0;
}
