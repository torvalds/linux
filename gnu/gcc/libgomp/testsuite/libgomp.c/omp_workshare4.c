/******************************************************************************
* OpenMP Example - Combined Parallel Loop Work-sharing - C/C++ Version
* FILE: omp_workshare4.c
* DESCRIPTION:
*   This is a corrected version of the omp_workshare3.c example. Corrections
*   include removing all statements between the parallel for construct and
*   the actual for loop, and introducing logic to preserve the ability to 
*   query a thread's id and print it from inside the for loop.
* SOURCE: Blaise Barney  5/99
* LAST REVISED: 03/03/2002
******************************************************************************/

#include <omp.h>
#include <stdio.h>
#define N       50
#define CHUNKSIZE   5

main ()  {

int i, chunk, tid;
float a[N], b[N], c[N];
char first_time;

/* Some initializations */
for (i=0; i < N; i++)
  a[i] = b[i] = i * 1.0;
chunk = CHUNKSIZE;
first_time = 'y';

#pragma omp parallel for     \
  shared(a,b,c,chunk)            \
  private(i,tid)             \
  schedule(static,chunk)     \
  firstprivate(first_time)

  for (i=0; i < N; i++)
    {
    if (first_time == 'y')
      {
      tid = omp_get_thread_num();
      first_time = 'n';
      }
    c[i] = a[i] + b[i];
    printf("tid= %d i= %d c[i]= %f\n", tid, i, c[i]);
    }

  return 0;
}
