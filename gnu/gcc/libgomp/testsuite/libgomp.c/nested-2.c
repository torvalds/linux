#include <omp.h>
#include <stdlib.h>

int
main (void)
{
  int i = -1, j = -1;

  omp_set_nested (0);
  omp_set_dynamic (0);
#pragma omp parallel num_threads (4)
  {
#pragma omp single
    {
      i = omp_get_thread_num () + omp_get_num_threads () * 256;
#pragma omp parallel num_threads (2)
      {
#pragma omp single
        {
          j = omp_get_thread_num () + omp_get_num_threads () * 256;
        }
      }
    }
  }
  if (i < 4 * 256 || i >= 4 * 256 + 4)
    abort ();
  if (j != 256 + 0)
    abort ();
  return 0;
}
