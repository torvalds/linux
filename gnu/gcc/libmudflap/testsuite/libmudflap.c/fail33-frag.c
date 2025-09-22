#include <stdlib.h>

#define SIZE 16

char b[SIZE];
char a[SIZE];

int main ()
{
  int i, j=0, k;
  int a_before_b = (& a[0] < & b[0]);
  /* Rather than iterating linearly, which would allow loop unrolling
     and mapping to pointer manipulation, we traverse the "joined"
     arrays in some random order.  */
  for (i=0; i<SIZE*2; i++)
    {
      k = rand() % (SIZE*2);
      j += (a_before_b ? a[k] : b[k]);
    }
  return j;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*\[ab\]" } */
/* { dg-do run { xfail *-*-* } } */
