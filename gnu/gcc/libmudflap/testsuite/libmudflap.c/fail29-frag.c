#include <stdio.h>
#include <stdlib.h>

int foo (int u[10])
{
   return u[8];  /* this dereference should be instrumented */
}

int main ()
{
int *k = malloc (6);
return foo (k);
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*malloc region.*alloc" } */
/* { dg-do run { xfail *-*-* } } */
