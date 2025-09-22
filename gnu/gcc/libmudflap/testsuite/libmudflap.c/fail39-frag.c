#include <stdio.h>
#include <stdlib.h>

int main ()
{
  volatile int *k = (int *) malloc (sizeof (int));
  volatile int l;
  if (k == NULL) abort ();
  *k = 5;
  free ((void *) k);
  __mf_set_options ("-ignore-reads");
  l = *k; /* Should not trip, even though memory region just freed.  */
  __mf_set_options ("-no-ignore-reads");
  l = *k; /* Should trip now.  */
  return 0;
}
/* { dg-output "mudflap violation 1.*check/read.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap dead object.*malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
