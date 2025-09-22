/* { dg-do run } */

extern void abort (void);

volatile int count;
static int test(void)
{
  return ++count > 0;
}

int main()
{
  int i;
  #pragma omp for
  for (i = 0; i < 10; ++i)
    {
      if (test())
	continue;
      abort ();
    }
  if (i != count)
    abort ();
  return 0;
}
