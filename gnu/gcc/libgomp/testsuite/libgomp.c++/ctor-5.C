// { dg-do run }
// { dg-require-effective-target tls_runtime }

#include <omp.h>
#include <assert.h>

struct B
{
  static int count;
  static B *expected;

  B& operator=(const B &);
};

int B::count;
B * B::expected;

static B thr;
#pragma omp threadprivate(thr)

B& B::operator= (const B &b)
{
  assert (&b == expected);
  assert (this != expected);
  #pragma omp atomic
    count++;
  return *this;
}

static int nthreads;

void foo()
{
  B::expected = &thr;

  #pragma omp parallel copyin(thr)
    {
    #pragma omp master
      nthreads = omp_get_num_threads ();
    }
}

int main()
{
  omp_set_dynamic (0);
  omp_set_num_threads (4);
  foo();

  assert (B::count == nthreads-1);

  return 0;
}
