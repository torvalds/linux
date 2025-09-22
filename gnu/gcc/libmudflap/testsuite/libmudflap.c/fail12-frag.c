#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
int i = 10;
int *x = (int *) malloc (i * sizeof (int));

while (i--)
{
  ++x;
  *x = 0;
}
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
