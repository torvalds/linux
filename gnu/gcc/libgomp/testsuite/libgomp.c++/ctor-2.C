// { dg-do run }

#include <omp.h>
#include <assert.h>

struct B
{
  static int ccount;
  static int dcount;
  static int xcount;
  static B *expected;

  B();
  B(int);
  B(const B &);
  ~B();
  B& operator=(const B &);
  void doit();
};

int B::ccount;
int B::dcount;
int B::xcount;
B * B::expected;

B::B(int)
{
  expected = this;
}

B::B(const B &b)
{
  #pragma omp atomic 
    ccount++;
  assert (&b == expected);
}

B::~B()
{
  #pragma omp atomic
    dcount++;
}

void B::doit()
{
  #pragma omp atomic
    xcount++;
  assert (this != expected);
}

static int nthreads;

void foo()
{
  B b(0);

  #pragma omp parallel firstprivate(b)
    {
      #pragma omp master
	nthreads = omp_get_num_threads ();
      b.doit();
    }
}

int main()
{
  omp_set_dynamic (0);
  omp_set_num_threads (4);
  foo();

  assert (B::xcount == nthreads);
  assert (B::ccount == nthreads);
  assert (B::dcount == nthreads+1);

  return 0;
}
