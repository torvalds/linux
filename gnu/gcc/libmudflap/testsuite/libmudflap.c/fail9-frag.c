#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
foo = (char *)malloc (10);
bar = (char *)malloc (10);

free(foo);

bar[4] = 'a'; /* touch source buffer */
memcpy(foo, bar, 10);
return 0;
}

/* { dg-output "mudflap violation 1.*memcpy dest.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*malloc region.*alloc time.*dealloc time.*" } */
/* { dg-do run { xfail *-*-* } } */
