#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
volatile char *p = (char *) 0;
*p = 5;
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.NULL.*" } */
/* { dg-do run { xfail *-*-* } } */
