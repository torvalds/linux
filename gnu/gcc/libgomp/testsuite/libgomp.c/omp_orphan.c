/******************************************************************************
* FILE: omp_orphan.c
* DESCRIPTION:
*   OpenMP Example - Parallel region with an orphaned directive - C/C++ Version
*   This example demonstrates a dot product  being performed by an orphaned
*   loop reduction construct.  Scoping of the reduction variable is critical.
* AUTHOR: Blaise Barney  5/99
* LAST REVISED: 04/06/05
******************************************************************************/
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#define VECLEN 100

float a[VECLEN], b[VECLEN], sum;

float dotprod ()
{
int i,tid;

tid = omp_get_thread_num();
#pragma omp for reduction(+:sum)
  for (i=0; i < VECLEN; i++)
    {
    sum = sum + (a[i]*b[i]);
    printf("  tid= %d i=%d\n",tid,i);
    }

return(sum);
}


int main (int argc, char *argv[]) 
{
int i;

for (i=0; i < VECLEN; i++)
  a[i] = b[i] = 1.0 * i;
sum = 0.0;

#pragma omp parallel
  sum = dotprod();

printf("Sum = %f\n",sum);

  return 0;
}
