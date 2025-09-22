#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct foo { char z[10]; };

char * get_z (struct foo *this)
{
  return & this->z[0] /* the `this' pointer is not dereferenced! */;
}

int main ()
{
struct foo k;
char *n = get_z (& k);
srand ((int)(__mf_uintptr_t) n); /* use the pointer value */
return 0;
}
