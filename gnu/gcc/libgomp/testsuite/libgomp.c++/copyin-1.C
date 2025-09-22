// { dg-do run }
// { dg-require-effective-target tls_runtime }

#include <omp.h>

extern "C" void abort (void);

int thr = 32;
#pragma omp threadprivate (thr)

int
main (void)
{
  int l = 0;

  omp_set_dynamic (0);
  omp_set_num_threads (6);

#pragma omp parallel copyin (thr) reduction (||:l)
  {
    l = thr != 32;
    thr = omp_get_thread_num () + 11;
  }

  if (l || thr != 11)
    abort ();

#pragma omp parallel reduction (||:l)
  l = thr != omp_get_thread_num () + 11;

  if (l)
    abort ();
  return 0;
}
