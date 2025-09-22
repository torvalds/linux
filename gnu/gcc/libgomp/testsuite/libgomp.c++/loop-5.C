extern "C" void abort ();

int check;
int f1() { check |= 1; return 1; }
int f2() { check |= 2; return 11; }
int f3() { check |= 4; return 2; }

int a[12];

int main()
{
  #pragma omp for
  for (int i = f1(); i <= f2(); i += f3())
    a[i] = 1;

  for (int i = 0; i < 12; ++i)
    if (a[i] != (i & 1))
      abort ();
}
