// { dg-do run }

#include <omp.h>
#include <assert.h>

struct B
{
  static int icount;
  static int dcount;
  static int ccount;
  static B *e_inner;
  static B *e_outer;

  B();
  B(int);
  B(const B &);
  ~B();
  B& operator=(const B &);
  void doit();
};

int B::icount;
int B::dcount;
int B::ccount;
B * B::e_inner;
B * B::e_outer;

B::B()
{
  #pragma omp atomic 
    icount++;
}

B::B(int)
{
  e_outer = this;
}

B::~B()
{
  #pragma omp atomic
    dcount++;
}

B& B::operator= (const B &b)
{
  assert (&b == e_inner);
  assert (this == e_outer);
  #pragma omp atomic
    ccount++;
  return *this;
}

void B::doit()
{
  #pragma omp critical
    {
      assert (e_inner == 0);
      e_inner = this;
    }
}

static int nthreads;

void foo()
{
  B b(0);

  #pragma omp parallel sections lastprivate(b)
    {
    #pragma omp section
      nthreads = omp_get_num_threads ();
    #pragma omp section
      b.doit ();
    }
}

int main()
{
  omp_set_dynamic (0);
  omp_set_num_threads (4);
  foo();

  assert (B::ccount == 1);
  assert (B::icount == nthreads);
  assert (B::dcount == nthreads+1);

  return 0;
}
