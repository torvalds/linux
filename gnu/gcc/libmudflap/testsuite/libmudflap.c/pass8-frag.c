#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char *foo;
char *bar;
foo = (char *)malloc (10);
bar = (char *)malloc (10);

free(bar);
bar = (char *)malloc (10);
bar[6] = 'k'; /* touch memcpy source */
memcpy(foo, bar, 10);
return 0;
}
