extern "C" void abort (void);
int a;

void
foo ()
{
  int i;
  a = 30;
#pragma omp barrier
#pragma omp for lastprivate (a)
  for (i = 0; i < 1024; i++)
    {
      a = i;
    }
  if (a != 1023)
    abort ();
}

int
main (void)
{
#pragma omp parallel num_threads (64)
  foo ();

  return 0;
}
