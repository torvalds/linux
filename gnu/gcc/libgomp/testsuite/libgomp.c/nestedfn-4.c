/* PR middle-end/25261 */
/* { dg-do run } */

#include <omp.h>

extern void abort (void);

int
main (void)
{
  int i = 5, j, l = 0;
  int foo (void)
  {
    return i == 6;
  }
  int bar (void)
  {
    return i - 3;
  }

  omp_set_dynamic (0);

#pragma omp parallel if (foo ()) num_threads (2)
  if (omp_get_num_threads () != 1)
#pragma omp atomic
    l++;

#pragma omp parallel for schedule (static, bar ()) num_threads (2) \
		     reduction (|:l)
  for (j = 0; j < 4; j++)
    if (omp_get_thread_num () != (j >= 2))
#pragma omp atomic
      l++;

  i++;

#pragma omp parallel if (foo ()) num_threads (2)
  if (omp_get_num_threads () != 2)
#pragma omp atomic
    l++;

#pragma omp parallel for schedule (static, bar ()) num_threads (2) \
		     reduction (|:l)
  for (j = 0; j < 6; j++)
    if (omp_get_thread_num () != (j >= 3))
#pragma omp atomic
      l++;

#pragma omp parallel num_threads (4) reduction (|:l)
  if (!foo () || bar () != 3)
#pragma omp atomic
      l++;

  i++;

#pragma omp parallel num_threads (4) reduction (|:l)
  if (foo () || bar () != 4)
#pragma omp atomic
      l++;

  if (l)
    abort ();

  return 0;
}
