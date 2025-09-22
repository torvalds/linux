#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char foo [10];
strcpy(foo, "1234567890");
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. foo.*" } */
/* { dg-do run { xfail *-*-* } } */
