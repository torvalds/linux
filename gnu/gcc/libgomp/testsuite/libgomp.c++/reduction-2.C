#include <omp.h>
#include <stdlib.h>

int
main (void)
{
  int i = 0, j = 0, k = ~0, l;
  double d = 1.0;
#pragma omp parallel num_threads(4)
  {
#pragma omp single
    {
      i = 16;
      k ^= (1 << 16);
      d += 32.0;
    }

#pragma omp for reduction(+:i) reduction(*:d) reduction(&:k)
    for (l = 0; l < 4; l++)
      {
	if (omp_get_num_threads () == 4 && (i != 0 || d != 1.0 || k != ~0))
#pragma omp atomic
	  j |= 1;
  
	if (l == omp_get_thread_num ())
	  {
	    i = omp_get_thread_num ();
	    d = i + 1;
	    k = ~(1 << (2 * i));
	  }
      }

    if (omp_get_num_threads () == 4)
      {
	if (i != (16 + 0 + 1 + 2 + 3))
#pragma omp atomic
	  j |= 2;
	if (d != (33.0 * 1.0 * 2.0 * 3.0 * 4.0))
#pragma omp atomic
	  j |= 4;
	if (k != (~0 ^ 0x55 ^ (1 << 16)))
#pragma omp atomic
	  j |= 8;
      }
  }

  if (j)
    abort ();
  return 0;
}
