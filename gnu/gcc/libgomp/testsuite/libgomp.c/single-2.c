#include <stdlib.h>

int
main (void)
{
  int i;
  i = 4;
#pragma omp single copyprivate (i)
  {
    i = 6;
  }
  if (i != 6)
    abort ();
  return 0;
}
