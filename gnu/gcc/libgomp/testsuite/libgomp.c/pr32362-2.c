/* PR middle-end/32362 */
/* { dg-do run } */
/* { dg-options "-O2" } */

#include <omp.h>
#include <stdlib.h>

int a = 2, b = 4;

int
main ()
{
  int n[4] = { -1, -1, -1, -1 };
  omp_set_num_threads (4);
  omp_set_dynamic (0);
  omp_set_nested (1);
#pragma omp parallel private(b)
  {
    b = omp_get_thread_num ();
#pragma omp parallel firstprivate(a)
    {
      a = (omp_get_thread_num () + a) + 1;
      if (b == omp_get_thread_num ())
	n[omp_get_thread_num ()] = a + (b << 4);
    }
  }
  if (n[0] != 3)
    abort ();
  if (n[3] != -1
      && (n[1] != 0x14 || n[2] != 0x25 || n[3] != 0x36))
    abort ();
  return 0;
}
