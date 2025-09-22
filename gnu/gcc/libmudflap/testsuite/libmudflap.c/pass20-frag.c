#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct bar {int stuff; int array[10]; };

struct bar *foo = (struct bar *) malloc (sizeof (struct bar));
foo->array[5] = 4;
free (foo);

return 0;
}
