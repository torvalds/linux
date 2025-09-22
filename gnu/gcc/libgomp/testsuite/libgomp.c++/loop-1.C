#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <omp.h>

#define MAX	1000

void main1()
{
  int i, N1, N2, step;
  int a[MAX], b[MAX];

  N1 = rand () % 13;
  N2 = rand () % (MAX - 51) + 50;
  step = rand () % 7 + 1;

  printf ("N1 = %d\nN2 = %d\nstep = %d\n", N1, N2, step);

  for (i = N1; i <= N2; i += step)
    a[i] = 42+ i;

  /* COUNTING UP (<).  Fill in array 'b' in parallel.  */
  memset (b, 0, sizeof b);
#pragma omp parallel shared(a,b,N1,N2,step) private(i)
  {
#pragma omp for
    for (i = N1; i < N2; i += step)
      b[i] = a[i];
  }

  /* COUNTING UP (<).  Check that all the cells were filled in properly.  */
  for (i = N1; i < N2; i += step)
    if (a[i] != b[i])
      abort ();

  printf ("for (i = %d; i < %d; i += %d) [OK]\n", N1, N2, step);

  /* COUNTING UP (<=).  Fill in array 'b' in parallel.  */
  memset (b, 0, sizeof b);
#pragma omp parallel shared(a,b,N1,N2,step) private(i)
  {
#pragma omp for
    for (i = N1; i <= N2; i += step)
      b[i] = a[i];
  }

  /* COUNTING UP (<=).  Check that all the cells were filled in properly.  */
  for (i = N1; i <= N2; i += step)
    if (a[i] != b[i])
      abort ();

  printf ("for (i = %d; i <= %d; i += %d) [OK]\n", N1, N2, step);

  /* COUNTING DOWN (>).  Fill in array 'b' in parallel.  */
  memset (b, 0, sizeof b);
#pragma omp parallel shared(a,b,N1,N2,step) private(i)
  {
#pragma omp for
    for (i = N2; i > N1; i -= step)
      b[i] = a[i];
  }

  /* COUNTING DOWN (>).  Check that all the cells were filled in properly.  */
  for (i = N2; i > N1; i -= step)
    if (a[i] != b[i])
      abort ();

  printf ("for (i = %d; i > %d; i -= %d) [OK]\n", N2, N1, step);

  /* COUNTING DOWN (>=).  Fill in array 'b' in parallel.  */
  memset (b, 0, sizeof b);
#pragma omp parallel shared(a,b,N1,N2,step) private(i)
  {
#pragma omp for
    for (i = N2; i >= N1; i -= step)
      b[i] = a[i];
  }

  /* COUNTING DOWN (>=).  Check that all the cells were filled in properly.  */
  for (i = N2; i >= N1; i -= step)
    if (a[i] != b[i])
      abort ();

  printf ("for (i = %d; i >= %d; i -= %d) [OK]\n", N2, N1, step);
}

int
main ()
{
  int i;

  srand (0);
  for (i = 0; i < 10; ++i)
    main1();
  return 0;
}
