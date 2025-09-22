#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int foo (int *u, int i)
{
   return u[i];  /* this dereference should be instrumented */
}

int main ()
{
int *k = malloc (6);
return foo (k, 8);
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*malloc region.*alloc" } */
/* { dg-do run { xfail *-*-* } } */
