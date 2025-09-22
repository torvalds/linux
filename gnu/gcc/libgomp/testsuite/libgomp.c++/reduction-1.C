#include <omp.h>
#include <stdlib.h>

int
main (void)
{
  int i = 0, j = 0, k = ~0;
  double d = 1.0;
#pragma omp parallel num_threads(4) reduction(+:i) reduction(*:d) reduction(&:k)
  {
    if (i != 0 || d != 1.0 || k != ~0)
#pragma omp atomic
      j |= 1;
  
    if (omp_get_num_threads () != 4)
#pragma omp atomic
      j |= 2;

    i = omp_get_thread_num ();
    d = i + 1;
    k = ~(1 << (2 * i));
  }

  if (j & 1)
    abort ();
  if ((j & 2) == 0)
    {
      if (i != (0 + 1 + 2 + 3))
	abort ();
      if (d != (1.0 * 2.0 * 3.0 * 4.0))
	abort ();
      if (k != (~0 ^ 0x55))
	abort ();
    }
  return 0;
}
