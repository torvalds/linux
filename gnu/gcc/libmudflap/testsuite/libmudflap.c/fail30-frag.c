#include <stdio.h>
#include <stdlib.h>

int foo (int u)
{
   return u*u;
}

int main ()
{
int *k = malloc(5);
int j = foo (k[8]);  /* this call argument should be instrumented */
return j;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*malloc region.*alloc" } */
/* { dg-do run { xfail *-*-* } } */
