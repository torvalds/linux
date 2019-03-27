/*
 * This file is in the public domain.  Written by Garrett A. Wollman,
 * 2002-09-07.
 *
 * $FreeBSD$
 */

#include <stdlib.h>
#include <unistd.h>

/*
 * ISO C99 added this function to provide for Standard C applications
 * which needed something like POSIX _exit().  A new interface was created
 * in case it turned out that _exit() was insufficient to meet the
 * requirements of ISO C.  (That's probably not the case, but here
 * is where you would put the extra code if it were.)
 */
void
_Exit(int code)
{
	_exit(code);
}
