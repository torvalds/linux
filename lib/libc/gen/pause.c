/*	$OpenBSD: pause.c,v 1.7 2019/01/25 00:19:25 millert Exp $	*/

/*
 * Written by Todd C. Miller <millert@openbsd.org>
 * Public domain.
 */

#include <signal.h>
#include <unistd.h>

/*
 * Backwards compatible pause(3).
 */
int
pause(void)
{
	sigset_t mask;

	return (sigprocmask(SIG_BLOCK, NULL, &mask) ? -1 : sigsuspend(&mask));
}
