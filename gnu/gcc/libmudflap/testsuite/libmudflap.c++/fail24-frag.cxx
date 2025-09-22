#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char zoo [10];

int main ()
{
int i = strlen ("twelve") + strlen ("zero") + strlen ("seventeen");
zoo[i] = 'a';
return 0;
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.*zoo.*static.*" } */
/* { dg-do run { xfail *-*-* } } */
