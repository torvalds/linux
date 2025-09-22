/* { dg-do run } */

#include <stdio.h>

float
work1 (int i)
{
  return 1.0 * i;
}

float
work2 (int i)
{
  return 2.0 * i;
}

void
a16 (float *x, float *y, int *index, int n)
{
  int i;
#pragma omp parallel for shared(x, y, index, n)
  for (i = 0; i < n; i++)
    {
#pragma omp atomic
      x[index[i]] += work1 (i);
      y[i] += work2 (i);
    }
}
int
main ()
{
  float x[1000];
  float y[10000];
  int index[10000];
  int i;
  for (i = 0; i < 10000; i++)
    {
      index[i] = i % 1000;
      y[i] = 0.0;
    }
  for (i = 0; i < 1000; i++)
    x[i] = 0.0;
  a16 (x, y, index, 10000);
  for (i = 0; i < 10; i++)
    printf ("x[%d] = %f, y[%d] = %f\n", i, x[i], i, y[i]);
  return 0;
}
