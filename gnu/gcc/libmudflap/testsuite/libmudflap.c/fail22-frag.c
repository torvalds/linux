#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct boo { int a; };
int c;
struct boo *b = malloc (sizeof (struct boo));
__mf_set_options ("-check-initialization");
c = b->a;
(void) malloc (c); /* some dummy use of c */
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.malloc region.*1r/0w.*" } */
/* { dg-do run { xfail *-*-* } } */
