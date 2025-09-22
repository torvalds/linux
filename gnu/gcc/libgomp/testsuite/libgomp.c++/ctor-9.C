// { dg-do run }
// { dg-require-effective-target tls_runtime }

#include <omp.h>
#include <assert.h>

#define N 10
#define THR 4

struct B
{
  B& operator=(const B &);
};

static B *base;
static B *threadbase;
static int singlethread;
#pragma omp threadprivate(threadbase)

static unsigned cmask[THR];

B& B::operator= (const B &b)
{
  unsigned sindex = &b - base;
  unsigned tindex = this - threadbase;
  assert(sindex < N);
  assert(sindex == tindex);
  cmask[omp_get_thread_num ()] |= 1u << tindex;
  return *this;
}

void foo()
{
  #pragma omp parallel
    {
      B b[N];
      threadbase = b;
      #pragma omp single copyprivate(b)
	{
	  assert(omp_get_num_threads () == THR);
	  singlethread = omp_get_thread_num ();
	  base = b;
	}
    }
}

int main()
{
  omp_set_dynamic (0);
  omp_set_num_threads (THR);
  foo();

  for (int i = 0; i < THR; ++i)
    if (i == singlethread)
      assert(cmask[singlethread] == 0);
    else
      assert(cmask[i] == (1u << N) - 1);

  return 0;
}
