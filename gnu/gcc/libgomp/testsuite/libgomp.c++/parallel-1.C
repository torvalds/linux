#include <omp.h>

extern "C" void abort (void);

int
foo (void)
{
  return 10;
}

main ()
{
  int A = 0;

  #pragma omp parallel if (foo () > 10) shared (A)
    {
      A = omp_get_num_threads ();
    }

  if (A != 1)
    abort ();

  #pragma omp parallel if (foo () == 10) num_threads (3) shared (A)
    {
      A = omp_get_num_threads ();
    }

  if (A != 3)
    abort ();

  #pragma omp parallel if (foo () == 10) num_threads (foo ()) shared (A)
    {
      A = omp_get_num_threads ();
    }

  if (A != 10)
    abort ();

  return 0;
}
