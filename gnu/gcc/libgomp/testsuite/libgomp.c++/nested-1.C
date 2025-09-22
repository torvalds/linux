// { dg-do run }

extern "C" void abort(void);
#define N 1000

int foo()
{
  int i = 0, j;

  #pragma omp parallel for num_threads(2) shared (i)
  for (j = 0; j < N; ++j)
    {
      #pragma omp parallel num_threads(1) shared (i)
      {
	#pragma omp atomic
	i++;
      }
    }

  return i;
}

int main()
{
  if (foo() != N)
    abort ();
  return 0;
}
