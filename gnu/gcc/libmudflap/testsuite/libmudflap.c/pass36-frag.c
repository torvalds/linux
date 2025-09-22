#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

int main ()
{
char *k;
__mf_set_options ("-sigusr1-report -print-leaks");
k = (char *) malloc (100);
raise (SIGUSR1);
free (k);
return 0;
}
/* { dg-output "Leaked object.*name=.malloc region.*objects: 1" } */
