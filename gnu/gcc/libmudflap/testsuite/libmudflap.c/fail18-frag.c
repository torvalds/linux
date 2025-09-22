#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
/* One cannot redeclare __mf_lc_mask in proper C from instrumented
   code, because of the way the instrumentation code emits its decls.  */
extern unsigned foo __asm__ ("__mf_lc_mask");
unsigned * volatile bar = &foo;
*bar = 4;
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*.__mf_lc_mask.*no-access.*" } */
/* { dg-do run { xfail *-*-* } } */
