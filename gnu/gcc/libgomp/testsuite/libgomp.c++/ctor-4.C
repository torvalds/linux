// { dg-do run }

#include <omp.h>
#include <assert.h>

struct B
{
  static int ccount;
  static int dcount;
  static int ecount;
  static B *e_inner;
  static B *e_outer;

  B();
  B(int);
  B(const B &);
  ~B();
  B& operator=(const B &);
  void doit();
};

int B::ccount;
int B::dcount;
int B::ecount;
B * B::e_inner;
B * B::e_outer;

B::B(int)
{
  e_outer = this;
}

B::B(const B &b)
{
  assert (&b == e_outer);
  #pragma omp atomic 
    ccount++;
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
    ecount++;
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

  #pragma omp parallel sections firstprivate(b) lastprivate(b)
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

  assert (B::ecount == 1);
  assert (B::ccount == nthreads);
  assert (B::dcount == nthreads+1);

  return 0;
}
