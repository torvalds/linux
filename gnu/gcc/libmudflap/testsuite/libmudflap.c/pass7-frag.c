#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
foo = (char *)malloc (10);
bar = (char *)malloc (10);
bar[2] = 'z'; /* touch memcpy source */
memcpy(foo+1, bar+1, 9);
return 0;
}
