/* { dg-do run } */

#include <omp.h>
#include <stdlib.h>
void
do_by_16 (float *x, int iam, int ipoints)
{
}

void
a36 (float *x, int npoints)
{
  int iam, ipoints;
  omp_set_dynamic (0);
  omp_set_num_threads (16);
#pragma omp parallel shared(x, npoints) private(iam, ipoints)
  {
    if (omp_get_num_threads () != 16)
      abort ();
    iam = omp_get_thread_num ();
    ipoints = npoints / 16;
    do_by_16 (x, iam, ipoints);
  }
}

int main()
{
  float a[10];
  a36 (a, 10);
  return 0;
}
