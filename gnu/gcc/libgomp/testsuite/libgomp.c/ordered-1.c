/* Test that all loop iterations are touched.  This doesn't verify 
   scheduling order, merely coverage.  */
/* Note that we never call GOMP_ordered_start in here.  AFAICS, this is
   valid; the only requirement is "not more than once per iteration".  */

/* { dg-require-effective-target sync_int_long } */

#include <omp.h>
#include <string.h>
#include <assert.h>
#include "libgomp_g.h"


#define N 1000
static int S, E, INCR, CHUNK, NTHR;
static int data[N];

static void clean_data (void)
{
  memset (data, -1, sizeof (data));
}

static void test_data (void)
{
  int i, j;

  for (i = 0; i < S; ++i)
    assert (data[i] == -1);

  for (j = 0; i < E; ++i, j = (j + 1) % INCR)
    if (j == 0)
      assert (data[i] != -1);
    else
      assert (data[i] == -1);

  for (; i < N; ++i)
    assert (data[i] == -1);
}

static void set_data (long i, int val)
{
  int old;
  assert (i >= 0 && i < N);
  old = __sync_lock_test_and_set (data+i, val);
  assert (old == -1);
}
  

#define TMPL_1(sched)							\
static void f_##sched##_1 (void *dummy)					\
{									\
  int iam = omp_get_thread_num ();					\
  long s0, e0, i;							\
  if (GOMP_loop_ordered_##sched##_start (S, E, INCR, CHUNK, &s0, &e0))	\
    do									\
      {									\
	for (i = s0; i < e0; i += INCR)					\
	  set_data (i, iam);						\
      }									\
    while (GOMP_loop_ordered_##sched##_next (&s0, &e0));		\
  GOMP_loop_end ();							\
}									\
static void t_##sched##_1 (void)					\
{									\
  clean_data ();							\
  GOMP_parallel_start (f_##sched##_1, NULL, NTHR);			\
  f_##sched##_1 (NULL);							\
  GOMP_parallel_end ();							\
  test_data ();								\
}

TMPL_1(static)
TMPL_1(dynamic)
TMPL_1(guided)

static void test (void)
{
  t_static_1 ();
  t_dynamic_1 ();
  t_guided_1 ();
}

int main()
{
  omp_set_dynamic (0);

  NTHR = 4;

  S = 0, E = N, INCR = 1, CHUNK = 4;
  test ();

  S = 0, E = N, INCR = 2, CHUNK = 4;
  test ();

  S = 1, E = N-1, INCR = 1, CHUNK = 5;
  test ();

  S = 1, E = N-1, INCR = 2, CHUNK = 5;
  test ();

  S = 2, E = 4, INCR = 1, CHUNK = 1;
  test ();

  S = 0, E = N, INCR = 1, CHUNK = 0;
  t_static_1 ();

  S = 1, E = N-1, INCR = 1, CHUNK = 0;
  t_static_1 ();

  NTHR = 10;
  S = 1, E = 9, INCR = 1, CHUNK = 0;
  t_static_1 ();

  return 0;
}
