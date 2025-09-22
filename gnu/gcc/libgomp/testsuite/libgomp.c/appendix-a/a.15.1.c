/* { dg-do run } */

#include <stdio.h>

void
work (int n)
{
  printf ("[%d of %d], nested = %d, n = %d\n", omp_get_thread_num (), omp_get_num_threads(), omp_get_nested (), n);
}

void
sub3 (int n)
{
  work (n);
#pragma omp barrier
  work (n);
}

void
sub2 (int k)
{
#pragma omp parallel shared(k)
  sub3 (k);
}

void
sub1 (int n)
{
  int i;
#pragma omp parallel private(i) shared(n)
  {
#pragma omp for
    for (i = 0; i < n; i++)
      sub2 (i);
  }
}
int
main ()
{
  sub1 (2);
  sub2 (15);
  sub3 (20);
  return 0;
}
