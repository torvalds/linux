// PR middle-end/27337
// { dg-do run }

#include <omp.h>

extern "C" void abort (void);

struct S
{
  S ();
  ~S ();
  S (const S &);
  int i;
};

int n[3];

S::S () : i(18)
{
  if (omp_get_thread_num () != 0)
#pragma omp atomic
    n[0]++;
}

S::~S ()
{
  if (omp_get_thread_num () != 0)
#pragma omp atomic
    n[1]++;
}

S::S (const S &x)
{
  if (x.i != 18)
    abort ();
  i = 118;
  if (omp_get_thread_num () != 0)
#pragma omp atomic
    n[2]++;
}

S
foo ()
{
  int i;
  S ret;

#pragma omp parallel for firstprivate (ret) lastprivate (ret) \
			 schedule (static, 1) num_threads (4)
  for (i = 0; i < 4; i++)
    ret.i += omp_get_thread_num ();

  return ret;
}

S
bar ()
{
  int i;
  S ret;

#pragma omp parallel for num_threads (4)
  for (i = 0; i < 4; i++)
#pragma omp atomic
    ret.i += omp_get_thread_num () + 1;

  return ret;
}

S x;

int
main (void)
{
  omp_set_dynamic (false);
  x = foo ();
  if (n[0] != 0 || n[1] != 3 || n[2] != 3)
    abort ();
  if (x.i != 118 + 3)
    abort ();
  x = bar ();
  if (n[0] != 0 || n[1] != 3 || n[2] != 3)
    abort ();
  if (x.i != 18 + 0 + 1 + 2 + 3 + 4)
    abort ();
  return 0;
}
