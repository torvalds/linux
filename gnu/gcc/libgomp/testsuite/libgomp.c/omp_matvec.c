/******************************************************************************
* OpenMP Example - Matrix-vector multiplication - C/C++ Version
* FILE: omp_matvec.c
* DESCRIPTION:
*   This example multiplies all row i elements of matrix A with vector
*   element b(i) and stores the summed products in vector c(i).  A total is
*   maintained for the entire matrix.  Performed by using the OpenMP loop
*   work-sharing construct.  The update of the shared global total is
*   serialized by using the OpenMP critical directive.
* SOURCE: Blaise Barney  5/99
* LAST REVISED:
******************************************************************************/

#include <omp.h>
#include <stdio.h>
#define SIZE 10


main ()
{

float A[SIZE][SIZE], b[SIZE], c[SIZE], total;
int i, j, tid;

/* Initializations */
total = 0.0;
for (i=0; i < SIZE; i++)
  {
  for (j=0; j < SIZE; j++)
    A[i][j] = (j+1) * 1.0;
  b[i] = 1.0 * (i+1);
  c[i] = 0.0;
  }
printf("\nStarting values of matrix A and vector b:\n");
for (i=0; i < SIZE; i++)
  {
  printf("  A[%d]= ",i);
  for (j=0; j < SIZE; j++)
    printf("%.1f ",A[i][j]);
  printf("  b[%d]= %.1f\n",i,b[i]);
  }
printf("\nResults by thread/row:\n");

/* Create a team of threads and scope variables */
#pragma omp parallel shared(A,b,c,total) private(tid,i)
  {
  tid = omp_get_thread_num();

/* Loop work-sharing construct - distribute rows of matrix */
#pragma omp for private(j)
  for (i=0; i < SIZE; i++)
    {
    for (j=0; j < SIZE; j++)
      c[i] += (A[i][j] * b[i]);

    /* Update and display of running total must be serialized */
    #pragma omp critical
      {
      total = total + c[i];
      printf("  thread %d did row %d\t c[%d]=%.2f\t",tid,i,i,c[i]);
      printf("Running total= %.2f\n",total);
      }

    }   /* end of parallel i loop */

  } /* end of parallel construct */

printf("\nMatrix-vector total - sum of all c[] = %.2f\n\n",total);

  return 0;
}

