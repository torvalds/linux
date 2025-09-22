extern "C" void abort (void);

struct X
{
  int a;
  char b;
  int c;
};

main()
{
  int i = 0;
  struct X x;
  int bad = 0;

  #pragma omp parallel private (i, x) shared (bad)
    {
      i = 5;

      #pragma omp single copyprivate (i, x)
	{
	  i++;
	  x.a = 23;
	  x.b = 42;
	  x.c = 26;
	}

      if (i != 6 || x.a != 23 || x.b != 42 || x.c != 26)
	bad = 1;
    }

  if (bad)
    abort ();

  return 0;
}
