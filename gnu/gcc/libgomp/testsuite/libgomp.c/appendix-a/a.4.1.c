/* { dg-do run } */

#include <omp.h>
extern void abort (void);
void
subdomain (float *x, int istart, int ipoints)
{
  int i;
  for (i = 0; i < ipoints; i++)
    x[istart + i] = 123.456;
}

void
sub (float *x, int npoints)
{
  int iam, nt, ipoints, istart;
#pragma omp parallel default(shared) private(iam,nt,ipoints,istart)
  {
    iam = omp_get_thread_num ();
    nt = omp_get_num_threads ();
    ipoints = npoints / nt;	/* size of partition */
    istart = iam * ipoints;	/* starting array index */
    if (iam == nt - 1)		/* last thread may do more */
      ipoints = npoints - istart;
    subdomain (x, istart, ipoints);
  }
}
int
main ()
{
  int i;
  float array[10000];
  sub (array, 10000);
  for (i = 0; i < 10000; i++)
    if (array[i] < 123.45 || array[i] > 123.46)
      abort ();
  return 0;
}
