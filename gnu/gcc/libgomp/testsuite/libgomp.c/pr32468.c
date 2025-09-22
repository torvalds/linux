/* PR libgomp/32468 */
/* { dg-do run } */

#include <omp.h>
#include <stdlib.h>

int
main (void)
{
  int res[2] = { -1, -1 };
  omp_set_dynamic (0);
  omp_set_num_threads (4);
#pragma omp parallel
  {
    #pragma omp sections
      {
	#pragma omp section
	res[0] = omp_get_num_threads () != 4;
	#pragma omp section
	res[1] = omp_get_num_threads () != 4;
      }
  }
  if (res[0] != 0 || res[1] != 0)
    abort ();
  return 0;
}
