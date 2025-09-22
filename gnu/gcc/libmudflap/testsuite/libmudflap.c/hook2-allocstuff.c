/* Generates recursive malloc call on i386-freebsd4.10 with -fmudflap.  */
#include <stdlib.h>

int
main (void)
{
  char *p = malloc (1<<24);
  return 0;
}
