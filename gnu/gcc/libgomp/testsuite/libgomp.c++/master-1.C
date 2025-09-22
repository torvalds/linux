// PR c++/24734
// { dg-do run }

extern "C" void abort ();
int i;

template<int> void
foo ()
{
  #pragma omp parallel
    {
    #pragma omp master
      i++;
    }
}

int
main ()
{
  foo<0> ();
  if (i != 1)
    abort ();
  return 0;
}
