// PR c++/26943
// { dg-do run }

#include <assert.h>
#include <unistd.h>

struct S
{
  public:
    int x;
    S () : x(-1) { }
    S (const S &);
    S& operator= (const S &);
    void test ();
};

static volatile int hold;

S::S (const S &s)
{
  #pragma omp master
    sleep (1);

  assert (s.x == -1);
  x = 0;
}

S&
S::operator= (const S& s)
{
  assert (s.x == 1);
  x = 2;
  return *this;
}

void
S::test ()
{
  assert (x == 0);
  x = 1;
}

static S x;

void
foo ()
{
  #pragma omp sections firstprivate(x) lastprivate(x)
  {
    x.test();
  }
}

int
main ()
{
  #pragma omp parallel num_threads(2)
    foo();

  assert (x.x == 2);
  return 0;
}
