extern "C" void abort (void);

main()
{
  int i = 0;

  #pragma omp parallel shared (i)
    {
      #pragma omp single
	{
	  i++;
	}
    }

  if (i != 1)
    abort ();

  return 0;
}
