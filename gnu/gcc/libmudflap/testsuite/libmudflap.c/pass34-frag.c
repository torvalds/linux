#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test (int *k)
{
  if (*k > 5) { *k --; }
}

int z;

int main ()
{
/* z is initialized, but not via a pointer, so not instrumented */
z = rand (); 
test (& z);
return 0;
}
