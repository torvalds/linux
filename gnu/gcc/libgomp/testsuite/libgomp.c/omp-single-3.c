extern void abort (void);

void
single (int a, int b)
{
  #pragma omp single copyprivate(a) copyprivate(b)
    {
      a = b = 5;
    }

  if (a != b)
    abort ();
}

int main()
{
  #pragma omp parallel
    single (1, 2);

  return 0;
}
