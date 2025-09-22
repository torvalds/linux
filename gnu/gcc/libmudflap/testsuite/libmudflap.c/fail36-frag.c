#include <stdlib.h>

struct k
{
  int p;
  struct {
    int m : 31;
  } q;
};

int
main ()
{
  volatile struct k *l = malloc (sizeof (int)); /* make it only big enough for k.p */
  /* Confirm that we instrument this nested construct
     BIT_FIELD_REF(COMPONENT_REF(INDIRECT_REF)). */
  l->q.m = 5;
  return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*" } */
/* { dg-do run { xfail *-*-* } } */
