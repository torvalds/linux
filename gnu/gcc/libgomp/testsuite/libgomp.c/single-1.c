/* Trivial test of single.  */

/* { dg-require-effective-target sync_int_long } */

#include <omp.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include "libgomp_g.h"


static int test;

static void f_nocopy (void *dummy)
{
  if (GOMP_single_start ())
    {
      int iam = omp_get_thread_num ();
      int old = __sync_lock_test_and_set (&test, iam);
      assert (old == -1);
    }
}

static void f_copy (void *dummy)
{
  int *x = GOMP_single_copy_start ();
  if (x == NULL)
    {
      int iam = omp_get_thread_num ();
      int old = __sync_lock_test_and_set (&test, iam);
      assert (old == -1);
      GOMP_single_copy_end (&test);
    }
  else
    assert (x == &test);
}

int main()
{
  omp_set_dynamic (0);

  test = -1;
  GOMP_parallel_start (f_nocopy, NULL, 3);
  f_nocopy (NULL);
  GOMP_parallel_end ();

  test = -1;
  GOMP_parallel_start (f_copy, NULL, 3);
  f_copy (NULL);
  GOMP_parallel_end ();

  return 0;
}
