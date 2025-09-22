/* Wrapper to implement ANSI C's atexit using SunOS's on_exit. */
/* This function is in the public domain.  --Mike Stump. */

/*

@deftypefn Supplemental int atexit (void (*@var{f})())

Causes function @var{f} to be called at exit.  Returns 0.

@end deftypefn

*/

#include "config.h"

#ifdef HAVE_ON_EXIT

int
atexit(void (*f)(void))
{
  /* If the system doesn't provide a definition for atexit, use on_exit
     if the system provides that.  */
  on_exit (f, 0);
  return 0;
}

#endif
