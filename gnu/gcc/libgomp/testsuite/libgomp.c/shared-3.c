/* { dg-do run } */

void abort (void);

int main()
{
  int x;
  int *p;

  p = &x;

  #pragma omp parallel
    {
      if (p != &x)
        abort ();
    }

  return 0;
}
