// { dg-do run }

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
  #pragma omp parallel
    {
      B b;
      #pragma omp single copyprivate(b)
	{
	  nthreads = omp_get_num_threads ();
	  B::expected = &b;
	}
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
