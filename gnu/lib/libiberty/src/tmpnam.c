/*

@deftypefn Supplemental char* tmpnam (char *@var{s})

This function attempts to create a name for a temporary file, which
will be a valid file name yet not exist when @code{tmpnam} checks for
it.  @var{s} must point to a buffer of at least @code{L_tmpnam} bytes,
or be @code{NULL}.  Use of this function creates a security risk, and it must
not be used in new projects.  Use @code{mkstemp} instead.

@end deftypefn

*/

#include <stdio.h>

#ifndef L_tmpnam
#define L_tmpnam 100
#endif
#ifndef P_tmpdir
#define P_tmpdir "/usr/tmp"
#endif

static char tmpnam_buffer[L_tmpnam];
static int tmpnam_counter;

extern int getpid (void);

char *
tmpnam (char *s)
{
  int pid = getpid ();

  if (s == NULL)
    s = tmpnam_buffer;

  /*  Generate the filename and make sure that there isn't one called
      it already.  */

  while (1)
    {
      FILE *f;
      sprintf (s, "%s/%s%x.%x", P_tmpdir, "t", pid, tmpnam_counter);
      f = fopen (s, "r");
      if (f == NULL)
	break;
      tmpnam_counter++;
      fclose (f);
    }

  return s;
}
