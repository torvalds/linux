#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
foo = (char *)malloc (10);
bar = (char *)malloc (15);

memcpy(foo, bar, 11);
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
