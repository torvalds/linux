/* Test that all sections are touched.  */

/* { dg-require-effective-target sync_int_long } */

#include <omp.h>
#include <string.h>
#include <assert.h>
#include "libgomp_g.h"


#define N 100
static int data[N];
static int NTHR;

static void clean_data (void)
{
  memset (data, -1, sizeof (data));
}

static void test_data (void)
{
  int i;

  for (i = 0; i < N; ++i)
    assert (data[i] != -1);
}

static void set_data (unsigned i, int val)
{
  int old;
  assert (i >= 1 && i <= N);
  old = __sync_lock_test_and_set (data+i-1, val);
  assert (old == -1);
}
  

static void f_1 (void *dummy)
{
  int iam = omp_get_thread_num ();
  unsigned long s;

  for (s = GOMP_sections_start (N); s ; s = GOMP_sections_next ())
    set_data (s, iam);
  GOMP_sections_end ();
}

static void test_1 (void)
{
  clean_data ();
  GOMP_parallel_start (f_1, NULL, NTHR);
  f_1 (NULL);
  GOMP_parallel_end ();
  test_data ();
}

static void f_2 (void *dummy)
{
  int iam = omp_get_thread_num ();
  unsigned s;

  while ((s = GOMP_sections_next ()))
    set_data (s, iam);
  GOMP_sections_end_nowait ();
}

static void test_2 (void)
{
  clean_data ();
  GOMP_parallel_sections_start (f_2, NULL, NTHR, N);
  f_2 (NULL);
  GOMP_parallel_end ();
  test_data ();
}

int main()
{
  omp_set_dynamic (0);

  NTHR = 4;

  test_1 ();
  test_2 ();

  return 0;
}
