#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
 int *bar = (int *) malloc (sizeof (int));
/* Make an access here to get &foo into the lookup cache.  */
*bar = 5;
__mf_watch (bar, sizeof(int));
/* This access should trigger the watch violation.  */
*bar = 10;
/* NOTREACHED */
return 0;
}
/* { dg-output "mudflap violation 1.*watch.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap object.*malloc region.*" } */
/* { dg-do run { xfail *-*-* } } */
