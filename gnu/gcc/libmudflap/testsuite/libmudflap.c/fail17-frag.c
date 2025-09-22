#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{

char * x;
int foo;
x = (char *) malloc (10);
strcpy (x, "123456789");
foo = strlen (x+10);
x [foo] = 1; /* we just just use foo to force execution of strlen */
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
