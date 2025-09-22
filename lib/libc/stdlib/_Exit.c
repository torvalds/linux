/*	$OpenBSD: _Exit.c,v 1.3 2013/04/03 03:39:29 guenther Exp $	*/

/*
 * Placed in the public domain by Todd C. Miller on January 21, 2004.
 */

#include <stdlib.h>
#include <unistd.h>

/*
 * _Exit() is the ISO/ANSI C99 equivalent of the POSIX _exit() function.
 * No atexit() handlers are called and no signal handlers are run.
 * Whether or not stdio buffers are flushed or temporary files are removed
 * is implementation-dependent in C99.  Indeed, POSIX specifies that
 * _Exit() must *not* flush stdio buffers or remove temporary files, but
 * rather must behave exactly like _exit()
 */
void
_Exit(int status)
{
	_exit(status);
}
