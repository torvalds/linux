extern "C" void abort (void);

main()
{
  int i, a;

  a = 30;

#pragma omp parallel for firstprivate (a) lastprivate (a) \
	num_threads (2) schedule(static)
  for (i = 0; i < 10; i++)
    a = a + i;

  /* The thread that owns the last iteration will have computed
     30 + 5 + 6 + 7 + 8 + 9 = 65.  */
  if (a != 65)
    abort ();

  return 0;
}
