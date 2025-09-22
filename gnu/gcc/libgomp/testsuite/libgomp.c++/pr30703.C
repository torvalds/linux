// PR c++/30703
// { dg-do run }

#include <omp.h>

extern "C" void abort ();

int ctor, cctor, dtor;

struct A
{
  A();
  A(const A &);
  ~A();
  int i;
};

A::A()
{
#pragma omp atomic
  ctor++;
}

A::A(const A &r)
{
  i = r.i;
#pragma omp atomic
  cctor++;
}

A::~A()
{
#pragma omp atomic
  dtor++;
}

void
foo (A a, A b)
{
  int i, j = 0;
#pragma omp parallel for firstprivate (a) lastprivate (a) private (b) schedule (static, 1) num_threads (5)
  for (i = 0; i < 5; i++)
    {
      b.i = 5;
      if (a.i != 6)
	#pragma omp atomic
	  j += 1;
      a.i = b.i + i + 6;
    }

  if (j || a.i != 15)
    abort ();
}

void
bar ()
{
  A a, b;
  a.i = 6;
  b.i = 7;
  foo (a, b);
}

int
main ()
{
  omp_set_dynamic (false);
  if (ctor || cctor || dtor)
    abort ();
  bar ();
  if (ctor + cctor != dtor)
    abort ();
}
