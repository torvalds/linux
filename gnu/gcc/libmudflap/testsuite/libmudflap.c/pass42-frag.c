#include <stdio.h>

void
foo ()
{
  putc ('h', stdout);
  putc ('i', stdout);
  putc ('\n', stdout);
}

int
main (int argc, char *argv[])
{
  foo ();
  return 0;
}
/* { dg-output "hi" } */
