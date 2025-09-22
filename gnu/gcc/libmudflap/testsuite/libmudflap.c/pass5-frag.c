#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
char foo [10];
char bar [10];
bar[4] = 'k'; /* touch memcpy source */
memcpy(foo, bar, 10);
return 0;
}
