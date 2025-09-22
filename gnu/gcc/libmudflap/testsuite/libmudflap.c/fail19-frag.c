#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct foo {
  int bar [10];
};

struct foo *k = (struct foo *) malloc (2 * sizeof(int));
k->bar[5] = 9;
free (k);
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
