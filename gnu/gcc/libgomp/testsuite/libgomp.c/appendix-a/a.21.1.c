/* { dg-do run } */

#include <stdio.h>
void
work (int k)
{
#pragma omp ordered
  printf (" %d\n", k);
}

void
a21 (int lb, int ub, int stride)
{
  int i;
#pragma omp parallel for ordered schedule(dynamic)
  for (i = lb; i < ub; i += stride)
    work (i);
}

int
main ()
{
  a21 (0, 100, 5);
  return 0;
}
