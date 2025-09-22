#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
foo = (char *)malloc (12);
bar = (char *)malloc (10);

memcpy(foo+1, bar+1, 10);
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
