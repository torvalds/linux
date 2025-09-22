#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char zoo [10];

int main ()
{
int i = strlen ("012345") + strlen ("6789") + strlen ("01"); /* 11 */
zoo[i] = 'a';
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*zoo.*static.*" } */
/* { dg-do run { xfail *-*-* } } */
