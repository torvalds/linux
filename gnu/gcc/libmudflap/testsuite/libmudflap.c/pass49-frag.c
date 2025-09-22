#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

int foo (int a, ...)
{
  va_list args;
  char *a1;
  int a2;
  int k;

  va_start (args, a);
  for (k = 0; k < a; k++)
    {
      if ((k % 2) == 0)
        {
          char *b = va_arg (args, char *);
          printf ("%s", b);
        }
      else
        {
          int b = va_arg (args, int);
          printf ("%d", b);
        }
    }
  va_end (args);
  return a;
}

int main ()
{
  foo (7, "hello ", 5, " ", 3, " world ", 9, "\n");
  return 0;
}
/* { dg-output "hello 5 3 world 9" } */
