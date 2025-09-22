/* Trivial test of ordered.  */

/* { dg-require-effective-target sync_int_long } */

#include <omp.h>
#include <string.h>
#include <assert.h>
#include "libgomp_g.h"


#define N 100
static int next;
static int CHUNK, NTHR;

static void clean_data (void)
{
  next = 0;
}

static void set_data (long i)
{
  int n = __sync_fetch_and_add (&next, 1);
  assert (n == i);
}


#define TMPL_1(sched)							\
static void f_##sched##_1 (void *dummy)					\
{									\
  long s0, e0, i;							\
  if (GOMP_loop_ordered_##sched##_start (0, N, 1, CHUNK, &s0, &e0))	\
    do									\
      {									\
	for (i = s0; i < e0; ++i)					\
	  {								\
	    GOMP_ordered_start ();					\
	    set_data (i);						\
	    GOMP_ordered_end ();					\
	  }								\
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

  CHUNK = 1;
  test ();

  CHUNK = 5;
  test ();

  CHUNK = 7;
  test ();

  CHUNK = 0;
  t_static_1 ();

  return 0;
}
