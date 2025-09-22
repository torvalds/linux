#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
volatile int foo[10];
int sz = sizeof (int);

volatile char *bar = (char *)foo;
bar [sz * 10] = 0;
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.main. foo.*" } */
/* { dg-do run { xfail *-*-* } } */
