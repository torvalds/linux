/* { dg-do run } */

#include <omp.h>

extern void abort (void);

int
main (void)
{
  int i = 5, l = 0;
  int foo (void) { return i == 6; }
  int bar (void) { return i - 3; }

  omp_set_dynamic (0);

#pragma omp parallel if (foo ()) num_threads (bar ()) reduction (|:l)
  if (omp_get_num_threads () != 1)
    l = 1;

  i++;

#pragma omp parallel if (foo ()) num_threads (bar ()) reduction (|:l)
  if (omp_get_num_threads () != 3)
    l = 1;

  i++;

#pragma omp master
  if (bar () != 4)
    abort ();

#pragma omp single
  {
    if (foo ())
      abort ();
    i--;
    if (! foo ())
      abort ();
  }

  if (l)
    abort ();

  i = 8;
#pragma omp atomic
  l += bar ();

  if (l != 5)
    abort ();

  return 0;
}
