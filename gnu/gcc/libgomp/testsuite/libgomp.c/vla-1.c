/* { dg-do run } */

#include <omp.h>
#include <stdlib.h>
#include <string.h>

int
main (int argc, char **argv[])
{
  int n = argc < 5 ? 12 : 31, i, m, l;
  char a[n + 3];
  unsigned short b[n / 2 - 1];
  int c[n * 2 + 1];

  for (i = 0; i < n + 3; i++)
    a[i] = i;
  for (i = 0; i < n / 2 - 1; i++)
    b[i] = (i << 8) | i;
  for (i = 0; i < n * 2 + 1; i++)
    c[i] = (i << 24) | i;
  l = 0;
  m = n;
#pragma omp parallel default (shared) num_threads (4) \
  firstprivate (a, m) private (b, i) reduction (+:l)
  {
    for (i = 0; i < m + 3; i++)
      if (a[i] != i)
	l++;
    for (i = 0; i < m * 2 + 1; i++)
      if (c[i] != ((i << 24) | i))
	l++;
#pragma omp barrier
    memset (a, omp_get_thread_num (), m + 3);
    for (i = 0; i < m / 2 - 1; i++)
      b[i] = a[0] + 7;
#pragma omp master
    {
      for (i = 0; i < m * 2 + 1; i++)
	c[i] = a[0] + 16;
    }
#pragma omp barrier
    if (a[0] != omp_get_thread_num ())
      l++;
    for (i = 1; i < m + 3; i++)
      if (a[i] != a[0])
	l++;
    for (i = 0; i < m / 2 - 1; i++)
      if (b[i] != a[0] + 7)
	l++;
    for (i = 0; i < m * 2 + 1; i++)
      if (c[i] != 16)
	l++;
  }
  if (l)
    abort ();
  for (i = 0; i < n * 2 + 1; i++)
    if (c[i] != 16)
      l++;
  return 0;
}
