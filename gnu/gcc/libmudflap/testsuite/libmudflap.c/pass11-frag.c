#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
int i = 10;
char *x = (char *) malloc (i * sizeof (char));

while (--i)
{
  ++x;
  *x = 0;
}
return 0;
}
