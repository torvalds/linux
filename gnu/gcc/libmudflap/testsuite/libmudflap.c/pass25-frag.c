#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
int *foo = malloc (10 * sizeof(int));
int *bar = & foo[3];
/* Watching occurs at the object granularity, which is in this case
   the entire array.  */
__mf_watch (& foo[1], sizeof(foo[1]));
__mf_unwatch (& foo[6], sizeof(foo[6]));
*bar = 10;
free (foo); 
return 0;
}
