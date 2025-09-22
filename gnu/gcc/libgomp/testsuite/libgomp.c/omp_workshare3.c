/* { dg-do compile } */

/******************************************************************************
* OpenMP Example - Combined Parallel Loop Work-sharing - C/C++ Version
* FILE: omp_workshare3.c
* DESCRIPTION:
*   This example attempts to show use of the parallel for construct.  However
*   it will generate errors at compile time.  Try to determine what is causing
*   the error.  See omp_workshare4.c for a corrected version.
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

/* Some initializations */
for (i=0; i < N; i++)
  a[i] = b[i] = i * 1.0;
chunk = CHUNKSIZE;

#pragma omp parallel for     \
  shared(a,b,c,chunk)            \
  private(i,tid)             \
  schedule(static,chunk)
  {				/* { dg-error "expected" } */
  tid = omp_get_thread_num();
  for (i=0; i < N; i++)
    {
    c[i] = a[i] + b[i];
    printf("tid= %d i= %d c[i]= %f\n", tid, i, c[i]);
    }
  }  /* end of parallel for construct */

  return 0;
}
