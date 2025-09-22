#include <stdio.h>

void
foo ()
{
  return; /* accept value-less return statement */
}

int
main (int argc, char *argv[])
{
  foo ();
  return 0;
}
