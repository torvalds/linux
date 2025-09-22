/* PR middle-end/30494 */
/* { dg-do run } */

#include <omp.h>

int errors;

int
check (int m, int i, int *v, int *w)
{
  int j;
  int n = omp_get_thread_num ();
  for (j = 0; j < m; j++)
    if (v[j] != j + n)
      #pragma omp atomic
	errors += 1;
  for (j = 0; j < m * 3 + i; j++)
    if (w[j] != j + 10 + n)
      #pragma omp atomic
	errors += 1;
}

int
foo (int n, int m)
{
  int i;
#pragma omp for
  for (i = 0; i < 6; i++)
    {
      int v[n], w[n * 3 + i], j;
      for (j = 0; j < n; j++)
	v[j] = j + omp_get_thread_num ();
      for (j = 0; j < n * 3 + i; j++)
	w[j] = j + 10 + omp_get_thread_num ();
      check (m, i, v, w);
    }
  return 0;
}

int
bar (int n, int m)
{
  int i;
#pragma omp parallel for num_threads (4)
  for (i = 0; i < 6; i++)
    {
      int v[n], w[n * 3 + i], j;
      for (j = 0; j < n; j++)
	v[j] = j + omp_get_thread_num ();
      for (j = 0; j < n * 3 + i; j++)
	w[j] = j + 10 + omp_get_thread_num ();
      check (m, i, v, w);
    }
  return 0;
}

int
main (void)
{
#pragma omp parallel num_threads (3)
  foo (128, 128);
  bar (256, 256);
  return 0;
}
