#include <omp.h>
#include <stdlib.h>

int
main (void)
{
  int i = 0, j = 0, k = 0, l = 0;
#pragma omp parallel num_threads(4) reduction(-:i) reduction(|:k) \
		     reduction(^:l)
  {
    if (i != 0 || k != 0 || l != 0)
#pragma omp atomic
      j |= 1;
  
    if (omp_get_num_threads () != 4)
#pragma omp atomic
      j |= 2;

    i = omp_get_thread_num ();
    k = 1 << (2 * i);
    l = 0xea << (3 * i);
  }

  if (j & 1)
    abort ();
  if ((j & 2) == 0)
    {
      if (i != (0 + 1 + 2 + 3))
	abort ();
      if (k != 0x55)
	abort ();
      if (l != 0x1e93a)
	abort ();
    }
  return 0;
}
