/* Trivial test of critical sections.  */

/* { dg-require-effective-target sync_int_long } */

#include <omp.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include "libgomp_g.h"


static volatile int test = -1;

static void function(void *dummy)
{
  int iam = omp_get_thread_num ();
  int old;

  GOMP_critical_start ();

  old = __sync_lock_test_and_set (&test, iam);
  assert (old == -1);

  usleep (10);
  test = -1;

  GOMP_critical_end ();
}

int main()
{
  omp_set_dynamic (0);

  GOMP_parallel_start (function, NULL, 3);
  function (NULL);
  GOMP_parallel_end ();

  return 0;
}
