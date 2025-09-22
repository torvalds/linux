// { dg-do run }
// Test several constructs within a parallel.  At one point in development,
// the critical directive clobbered the shared clause of the parallel.

#include <omp.h>
#include <stdlib.h>

#define N       2000

int main()
{
  int A[N];
  int nthreads;
  int i;

#pragma omp parallel shared (A, nthreads)
  {
    #pragma omp master
      nthreads = omp_get_num_threads ();

    #pragma omp for
      for (i = 0; i < N; i++)
        A[i] = 0;

    #pragma omp critical
      for (i = 0; i < N; i++)
        A[i] += 1;
  }

  for (i = 0; i < N; i++)
    if (A[i] != nthreads)
      abort ();

  return 0;
}
