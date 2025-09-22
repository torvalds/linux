#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
int i = 10;
int *x = (int *) malloc (i * sizeof (int));

while (--i)
{
  ++x;
  *x = 0;
}
return 0;
}
