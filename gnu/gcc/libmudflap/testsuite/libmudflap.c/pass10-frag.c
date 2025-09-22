#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
int foo[10];
int sz = sizeof (int);

char *bar = (char *)foo;
bar [sz * 9] = 0;
return 0;
}
