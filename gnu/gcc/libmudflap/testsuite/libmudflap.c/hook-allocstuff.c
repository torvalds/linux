#include <stdio.h>
#include <stdlib.h>

int main ()
{
  char *foo = (char *) malloc (10);
  strcpy (foo, "hello");
  foo = (char *) realloc (foo, 20);
  printf ("%s", foo);
  if (strcmp (foo, "hello"))
    abort ();
  free (foo);
  printf (" world\n");
  return 0;
}
/* { dg-output "hello world" } */
