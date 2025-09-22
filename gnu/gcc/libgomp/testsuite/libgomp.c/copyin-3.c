/* { dg-do run } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target tls_runtime } */

#include <omp.h>
#include <stdlib.h>

int thr;
#pragma omp threadprivate (thr)

int
test (int l)
{
  return l || (thr != omp_get_thread_num () * 2);
}

int
main (void)
{
  int l = 0;

  omp_set_dynamic (0);
  omp_set_num_threads (6);

  thr = 8;
  /* Broadcast the value to all threads.  */
#pragma omp parallel copyin (thr)
  ;

#pragma omp parallel reduction (||:l)
  {
    /* Now test if the broadcast succeeded.  */
    l = thr != 8;
    thr = omp_get_thread_num () * 2;
#pragma omp barrier
    l = test (l);
  }

  if (l)
    abort ();
  return 0;
}
