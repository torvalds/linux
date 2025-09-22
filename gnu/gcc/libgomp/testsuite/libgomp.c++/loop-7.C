// PR c++/24502
// { dg-do run }

extern "C" void abort ();

template <typename T> T
foo (T r)
{
  T i;
#pragma omp for
  for (i = 0; i < 10; i++)
    r += i;
  return r;
}

int
main ()
{
  if (foo (0) != 10 * 9 / 2 || foo (2L) != 10L * 9 / 2 + 2)
    abort ();
  return 0;
}
