#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct a {
  int x;
  int y;
  int z : 10;
};

struct b {
  int x;
  int y;
};

volatile struct b k;
volatile struct a *p;

p = (struct a*) &k;

p->z = 'q';

return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. k.*" } */
/* { dg-do run { xfail *-*-* } } */
