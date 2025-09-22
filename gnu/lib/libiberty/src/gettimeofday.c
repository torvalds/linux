#include "config.h"
#include "libiberty.h"
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* 

@deftypefn Supplemental int gettimeofday (struct timeval *@var{tp}, void *@var{tz})

Writes the current time to @var{tp}.  This implementation requires
that @var{tz} be NULL.  Returns 0 on success, -1 on failure.

@end deftypefn

*/ 

int
gettimeofday (struct timeval *tp, void *tz)
{
  if (tz)
    abort ();
  tp->tv_usec = 0;
  if (time (&tp->tv_sec) == (time_t) -1)
    return -1;
  return 0;
}
