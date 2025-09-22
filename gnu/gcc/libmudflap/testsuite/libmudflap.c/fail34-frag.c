#include <stdlib.h>

struct s
{
  int a1[4];
};

struct s a, b;
int idx = 7; /* should pass to the next object */

int
main ()
{
  int i, j=0;
  int a_before_b = (& a < & b);
  j = (a_before_b ? a.a1[idx] : b.a1[idx]);
  return j;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*\[ab\]" } */
/* { dg-do run { xfail *-*-* } } */
