#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
__mf_set_options ("-check-initialization");
foo = (char *)malloc (10);
bar = (char *)malloc (10);
/* bar[2] = 'z'; */ /* don't touch memcpy source */
memcpy(foo+1, bar+1, 9);
return 0;
}
/* { dg-output "mudflap violation 1.*check.read.*memcpy source.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*malloc region.*alloc time.*" } */
/* { dg-do run { xfail *-*-* } } */
