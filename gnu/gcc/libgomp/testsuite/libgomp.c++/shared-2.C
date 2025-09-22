extern "C" void abort (void);

void
parallel (int a, int b)
{
  int bad, LASTPRIV, LASTPRIV_SEC;
  int i;

  a = b = 3;

  bad = 0;

  #pragma omp parallel firstprivate (a,b) shared (bad) num_threads (5)
    {
      if (a != 3 || b != 3)
	bad = 1;

      #pragma omp for lastprivate (LASTPRIV)
      for (i = 0; i < 10; i++)
	LASTPRIV = i;

      #pragma omp sections lastprivate (LASTPRIV_SEC)
	{
	  #pragma omp section
	    { LASTPRIV_SEC = 3; }

	  #pragma omp section
	    { LASTPRIV_SEC = 42; }
	}

    }

  if (LASTPRIV != 9)
    abort ();

  if (LASTPRIV_SEC != 42)
    abort ();

  if (bad)
    abort ();
}

int main()
{
  parallel (1, 2);
  return 0;
}
