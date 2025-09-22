#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
foo = (char *)malloc (10);
bar = (char *)malloc (10);

free(foo);
foo = (char *)malloc (10);
bar[3] = 'w'; /* touch memcpy source */
memcpy(foo, bar, 10);
return 0;
}
