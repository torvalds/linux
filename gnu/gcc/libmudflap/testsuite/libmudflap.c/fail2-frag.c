#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
volatile int foo [10][10];
foo[10][0] = 0;
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. foo.*" } */
/* { dg-do run { xfail *-*-* } } */
