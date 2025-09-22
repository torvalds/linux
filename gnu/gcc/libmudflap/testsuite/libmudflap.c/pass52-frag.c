#include <stdio.h>


void writestuff (FILE *f)
{
  fprintf (f, "hello world\n");
  fputc ('y', f);
  putc ('e', f);
}

void readstuff (FILE *f)
{
  int c, d;
  char stuff[100], *s;
  c = fgetc (f);
  ungetc (c, f);
  d = fgetc (f);
  s = fgets (stuff, sizeof(stuff), f);
}

int main ()
{
  FILE *f;
  writestuff (stdout);
  writestuff (stderr);
  f = fopen ("/dev/null", "w");
  writestuff (f);
  fclose (f);
  f = fopen ("/dev/zero", "r");
  readstuff (f);
  f = freopen ("/dev/null", "w", f);
  writestuff (f);
  fclose (f);

  return 0;
}
