#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;

__mf_set_options ("-check-initialization");
foo = (char *)malloc (1);

/* These two operations each expand to a read-modify-write.
 * Even though the end result is that every bit of foo[0] is
 * eventually written to deterministically, the first read
 * triggers an uninit error.  Ideally, it shouldn't, so this
 * should be treated more like a regular XFAIL.  */
foo[0] &= 0xfe;
foo[0] |= 0x01;

return foo[0];
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*malloc region.*1r/0w.*" } */
/* { dg-do run { xfail *-*-* } } */
