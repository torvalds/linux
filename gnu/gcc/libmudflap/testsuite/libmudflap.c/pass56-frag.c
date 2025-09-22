#include <stdio.h>
#include <stdlib.h>

int main ()
{
  volatile int *k = (int *) malloc (sizeof (int));
  volatile int l;
  if (k == NULL) abort ();
  *k = 5;
  free ((void *) k);
  __mf_set_options ("-ignore-reads");
  l = *k; /* Should not trip, even though memory region just freed.  */
  return 0;
}
