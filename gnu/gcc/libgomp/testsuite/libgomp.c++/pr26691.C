// PR c++/26691

struct A
{
  int n;
  A (int i = 3) : n (i) {}
};

int
main ()
{
  A a;
  int err = 0;
#pragma omp parallel private (a) reduction (+:err)
  if (a.n != 3)
    err++;

  return err;
 }

