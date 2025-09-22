void foo (int k)
{
  volatile int *b = & k;
  *b = 5;
}

int main ()
{
  foo (5);
  return 0;
}
