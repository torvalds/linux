// { dg-do run }
// { dg-require-effective-target tls_runtime }

#include <omp.h>

extern "C" void abort (void);

struct S { int t; char buf[64]; } thr = { 32, "" };
#pragma omp threadprivate (thr)

int
main (void)
{
  int l = 0;

  omp_set_dynamic (0);
  omp_set_num_threads (6);

#pragma omp parallel copyin (thr) reduction (||:l)
  {
    l = thr.t != 32;
    thr.t = omp_get_thread_num () + 11;
  }

  if (l || thr.t != 11)
    abort ();

#pragma omp parallel reduction (||:l)
  l = thr.t != omp_get_thread_num () + 11;

  if (l)
    abort ();
  return 0;
}
