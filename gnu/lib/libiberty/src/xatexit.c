/*
 * Copyright (c) 1990 Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */


/*

@deftypefun int xatexit (void (*@var{fn}) (void))

Behaves as the standard @code{atexit} function, but with no limit on
the number of registered functions.  Returns 0 on success, or @minus{}1 on
failure.  If you use @code{xatexit} to register functions, you must use
@code{xexit} to terminate your program.

@end deftypefun

*/

/* Adapted from newlib/libc/stdlib/{,at}exit.[ch].
   If you use xatexit, you must call xexit instead of exit.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"

#include <stdio.h>

#include <stddef.h>

#if VMS
#include <stdlib.h>
#include <unixlib.h>
#else
/* For systems with larger pointers than ints, this must be declared.  */
PTR malloc (size_t);
#endif

static void xatexit_cleanup (void);

/* Pointer to function run by xexit.  */
extern void (*_xexit_cleanup) (void);

#define	XATEXIT_SIZE 32

struct xatexit {
	struct	xatexit *next;		/* next in list */
	int	ind;			/* next index in this table */
	void	(*fns[XATEXIT_SIZE]) (void);	/* the table itself */
};

/* Allocate one struct statically to guarantee that we can register
   at least a few handlers.  */
static struct xatexit xatexit_first;

/* Points to head of LIFO stack.  */
static struct xatexit *xatexit_head = &xatexit_first;

/* Register function FN to be run by xexit.
   Return 0 if successful, -1 if not.  */

int
xatexit (void (*fn) (void))
{
  register struct xatexit *p;

  /* Tell xexit to call xatexit_cleanup.  */
  if (!_xexit_cleanup)
    _xexit_cleanup = xatexit_cleanup;

  p = xatexit_head;
  if (p->ind >= XATEXIT_SIZE)
    {
      if ((p = (struct xatexit *) malloc (sizeof *p)) == NULL)
	return -1;
      p->ind = 0;
      p->next = xatexit_head;
      xatexit_head = p;
    }
  p->fns[p->ind++] = fn;
  return 0;
}

/* Call any cleanup functions.  */

static void
xatexit_cleanup (void)
{
  register struct xatexit *p;
  register int n;

  for (p = xatexit_head; p; p = p->next)
    for (n = p->ind; --n >= 0;)
      (*p->fns[n]) ();
}
