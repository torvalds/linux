void foo (int k)
{
  volatile int *b = & k;
  b++;
  *b = 5;
}

int main ()
{
  foo (5);
  return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*k" } */
/* { dg-do run { xfail *-*-* } } */
