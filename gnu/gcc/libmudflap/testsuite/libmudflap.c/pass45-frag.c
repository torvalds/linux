#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void h (const char *p, const char *f);
int
main (void)
{
  h (0, "foo");
  return 0;
}

void
h (const char *p, const char *f)
{
  size_t pl = p == NULL ? 0 : strlen (p);
  size_t fl = strlen (f) + 1;
  char a[pl + 1 + fl];
  char *cp = a;
  char b[pl + 5 + fl * 2];
  char *cccp = b;
  if (p != NULL)
    {
      cp = memcpy (cp, p, pl);
      *cp++ = ':';
    }
  memcpy (cp, f, fl);
  strcpy (b, a);
  puts (a);
}
/* { dg-output "foo" } */
