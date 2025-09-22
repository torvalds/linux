#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char foo [15];
char bar [10];
memcpy(foo, bar, 11);
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. bar.*" } */
/* { dg-do run { xfail *-*-* } } */
