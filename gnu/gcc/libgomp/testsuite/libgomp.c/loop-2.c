/* Validate static scheduling iteration dispatch.  We only test with
   even thread distributions here; there are multiple valid solutions
   for uneven thread distributions.  */

/* { dg-require-effective-target sync_int_long } */

#include <omp.h>
#include <string.h>
#include <assert.h>
#include "libgomp_g.h"


#define N 360
static int data[N][2];
static int INCR, NTHR, CHUNK;

static void clean_data (void)
{
  memset (data, -1, sizeof (data));
}

static void test_data (void)
{
  int n, i, c, thr, iter, chunk;

  chunk = CHUNK;
  if (chunk == 0)
    chunk = N / INCR / NTHR;

  thr = iter = c = i = 0;

  for (n = 0; n < N; ++n)
    {
      if (i == 0)
	{
	  assert (data[n][0] == thr);
	  assert (data[n][1] == iter);
	}
      else
	{
	  assert (data[n][0] == -1);
	  assert (data[n][1] == -1);
	}

      if (++i == INCR)
	{
	  i = 0;
	  if (++c == chunk)
	    {
	      c = 0;
	      if (++thr == NTHR)
		{
		  thr = 0;
		  ++iter;
		}
	    }
	}
    }
}

static void set_data (long i, int thr, int iter)
{
  int old;
  assert (i >= 0 && i < N);
  old = __sync_lock_test_and_set (&data[i][0], thr);
  assert (old == -1);
  old = __sync_lock_test_and_set (&data[i][1], iter);
  assert (old == -1);
}
  
static void f_static_1 (void *dummy)
{
  int iam = omp_get_thread_num ();
  long s0, e0, i, count = 0;
  if (GOMP_loop_static_start (0, N, INCR, CHUNK, &s0, &e0))
    do
      {
	for (i = s0; i < e0; i += INCR)
	  set_data (i, iam, count);
	++count;	
      }
    while (GOMP_loop_static_next (&s0, &e0));
  GOMP_loop_end ();
}

static void test (void)
{
  clean_data ();
  GOMP_parallel_start (f_static_1, NULL, NTHR);
  f_static_1 (NULL);
  GOMP_parallel_end ();
  test_data ();
}

int main()
{
  omp_set_dynamic (0);

  NTHR = 5;

  INCR = 1, CHUNK = 0;	/* chunk = 360 / 5 = 72 */
  test ();

  INCR = 4, CHUNK = 0;	/* chunk = 360 / 4 / 5 = 18 */
  test ();

  INCR = 1, CHUNK = 4;	/* 1 * 4 * 5 = 20 -> 360 / 20 = 18 iterations.  */
  test ();

  INCR = 3, CHUNK = 4;	/* 3 * 4 * 5 = 60 -> 360 / 60 = 6 iterations.  */
  test ();

  return 0;
}
