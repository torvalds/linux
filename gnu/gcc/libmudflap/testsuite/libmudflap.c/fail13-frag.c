#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct a {
  int x;
  int y;
  char z;
};

struct b {
  int x;
  int y;
};

struct b k;

(*((volatile struct a *) &k)).z = 'q';

return 0;
}
/* { dg-output "mudflap violation 1..check/write.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. k.*" } */
/* { dg-do run { xfail *-*-* } } */
