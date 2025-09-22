/******************************************************************************
* FILE: omp_workshare2.c
* DESCRIPTION:
*   OpenMP Example - Sections Work-sharing - C/C++ Version
*   In this example, the OpenMP SECTION directive is used to assign
*   different array operations to threads that execute a SECTION. Each 
*   thread receives its own copy of the result array to work with.
* AUTHOR: Blaise Barney  5/99
* LAST REVISED: 04/06/05
******************************************************************************/
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#define N     50

int main (int argc, char *argv[]) {

int i, nthreads, tid;
float a[N], b[N], c[N];

/* Some initializations */
for (i=0; i<N; i++)
  a[i] = b[i] = i * 1.0;

#pragma omp parallel shared(a,b,nthreads) private(c,i,tid)
  {
  tid = omp_get_thread_num();
  if (tid == 0)
    {
    nthreads = omp_get_num_threads();
    printf("Number of threads = %d\n", nthreads);
    }
  printf("Thread %d starting...\n",tid);

  #pragma omp sections nowait
    {
    #pragma omp section
      {
      printf("Thread %d doing section 1\n",tid);
      for (i=0; i<N; i++)
        {
        c[i] = a[i] + b[i];
        printf("Thread %d: c[%d]= %f\n",tid,i,c[i]);
        }
      }

    #pragma omp section
      {
      printf("Thread %d doing section 2\n",tid);
      for (i=0; i<N; i++)
        {
        c[i] = a[i] * b[i];
        printf("Thread %d: c[%d]= %f\n",tid,i,c[i]);
        }
      }

    }  /* end of sections */

    printf("Thread %d done.\n",tid); 

  }  /* end of parallel section */

  return 0;
}
