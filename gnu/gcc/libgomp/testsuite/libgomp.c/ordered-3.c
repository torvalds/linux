#include <stdlib.h>

int cnt;

void
check (int x)
{
  if (cnt++ != x)
    abort ();
}

int
main (void)
{
  int j;

  cnt = 0;
#pragma omp parallel for ordered schedule (static, 1) num_threads (4) if (0)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (static, 1) num_threads (4) if (1)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (runtime) num_threads (4) if (0)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (runtime) num_threads (4) if (1)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (dynamic) num_threads (4) if (0)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (dynamic) num_threads (4) if (1)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (guided) num_threads (4) if (0)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  cnt = 0;
#pragma omp parallel for ordered schedule (guided) num_threads (4) if (1)
  for (j = 0; j < 1000; j++)
    {
#pragma omp ordered
      check (j);
    }

  return 0;
}
