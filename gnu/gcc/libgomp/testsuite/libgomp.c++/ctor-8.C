// { dg-do run }
// { dg-require-effective-target tls_runtime }

#include <omp.h>
#include <assert.h>

#define N 10
#define THR 4

struct B
{
  B();
  B(const B &);
  ~B();
  B& operator=(const B &);
  void doit();
};

static B *base;
static B *threadbase;
static unsigned cmask[THR];
static unsigned dmask[THR];

#pragma omp threadprivate(threadbase)

B::B()
{
  assert (base == 0);
}

B::B(const B &b)
{
  unsigned index = &b - base;
  assert (index < N);
  cmask[omp_get_thread_num()] |= 1u << index;
}

B::~B()
{
  if (threadbase)
    {
      unsigned index = this - threadbase;
      assert (index < N);
      dmask[omp_get_thread_num()] |= 1u << index;
    }
}

void foo()
{
  B b[N];

  base = b;

  #pragma omp parallel firstprivate(b)
    {
      assert (omp_get_num_threads () == THR);
      threadbase = b;
    }

  threadbase = 0;
}

int main()
{
  omp_set_dynamic (0);
  omp_set_num_threads (THR);
  foo();

  for (int i = 0; i < THR; ++i)
    {
      unsigned xmask = (1u << N) - 1;
      assert (cmask[i] == xmask);
      assert (dmask[i] == xmask);
    }

  return 0;
}
