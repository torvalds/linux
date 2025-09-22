#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct foo {int base; char variable[1]; }; /* a common idiom for variable-size structs */

struct foo * b = (struct foo *) malloc (sizeof (int)); /* enough for base */
b->base = 4;
return 0;
}
