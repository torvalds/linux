#include <stdlib.h>
#include <omp.h>

int
main (void)
{
  double d, e;
  int l;
  omp_lock_t lck;
  omp_nest_lock_t nlck;

  d = omp_get_wtime ();

  omp_init_lock (&lck);
  omp_set_lock (&lck);
  if (omp_test_lock (&lck))
    abort ();
  omp_unset_lock (&lck);
  if (! omp_test_lock (&lck))
    abort ();
  if (omp_test_lock (&lck))
    abort ();
  omp_unset_lock (&lck);
  omp_destroy_lock (&lck);

  omp_init_nest_lock (&nlck);
  if (omp_test_nest_lock (&nlck) != 1)
    abort ();
  omp_set_nest_lock (&nlck);
  if (omp_test_nest_lock (&nlck) != 3)
    abort ();
  omp_unset_nest_lock (&nlck);
  omp_unset_nest_lock (&nlck);
  if (omp_test_nest_lock (&nlck) != 2)
    abort ();
  omp_unset_nest_lock (&nlck);
  omp_unset_nest_lock (&nlck);
  omp_destroy_nest_lock (&nlck);

  omp_set_dynamic (1);
  if (! omp_get_dynamic ())
    abort ();
  omp_set_dynamic (0);
  if (omp_get_dynamic ())
    abort ();

  omp_set_nested (1);
  if (! omp_get_nested ())
    abort ();
  omp_set_nested (0);
  if (omp_get_nested ())
    abort ();

  omp_set_num_threads (5);
  if (omp_get_num_threads () != 1)
    abort ();
  if (omp_get_max_threads () != 5)
    abort ();
  if (omp_get_thread_num () != 0)
    abort ();
  omp_set_num_threads (3);
  if (omp_get_num_threads () != 1)
    abort ();
  if (omp_get_max_threads () != 3)
    abort ();
  if (omp_get_thread_num () != 0)
    abort ();
  l = 0;
#pragma omp parallel reduction (|:l)
  {
    l = omp_get_num_threads () != 3;
    l |= omp_get_thread_num () < 0;
    l |= omp_get_thread_num () >= 3;
#pragma omp master
    l |= omp_get_thread_num () != 0;
  }
  if (l)
    abort ();

  if (omp_get_num_procs () <= 0)
    abort ();
  if (omp_in_parallel ())
    abort ();
#pragma omp parallel reduction (|:l)
  l = ! omp_in_parallel ();
#pragma omp parallel reduction (|:l) if (1)
  l = ! omp_in_parallel ();

  e = omp_get_wtime ();
  if (d > e)
    abort ();
  d = omp_get_wtick ();
  /* Negative precision is definitely wrong,
     bigger than 1s clock resolution is also strange.  */
  if (d <= 0 || d > 1)
    abort ();

  return 0;
}
