/*
 * Copyright (c) 2015  Philip Guenther <guenther@openbsd.org>
 *
 * Public domain.
 *
 * Verify that SIGTHR can't be blocked or caught by applications.
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

void sighandler(int sig) { }

int
main(void)
{
	struct sigaction sa;
	sigset_t set, oset;

	/*
	 * check sigprocmask
	 */
	if (sigprocmask(SIG_BLOCK, NULL, &set))
		err(1, "sigprocmask");
	if (sigismember(&set, SIGTHR))
		errx(1, "SIGTHR already blocked");
	sigaddset(&set, SIGTHR);
	if (sigprocmask(SIG_BLOCK, &set, NULL))
		err(1, "sigprocmask");
	if (sigprocmask(SIG_SETMASK, &set, &oset))
		err(1, "sigprocmask");
	if (sigismember(&oset, SIGTHR))
		errx(1, "SIGTHR blocked with SIG_BLOCK");
	if (sigprocmask(SIG_BLOCK, NULL, &oset))
		err(1, "sigprocmask");
	if (sigismember(&oset, SIGTHR))
		errx(1, "SIGTHR blocked with SIG_SETMASK");

	/*
	 * check sigaction
	 */
	if (sigaction(SIGTHR, NULL, &sa) == 0)
		errx(1, "sigaction(SIGTHR) succeeded");
	else if (errno != EINVAL)
		err(1, "sigaction(SIGTHR) didn't fail with EINVAL");
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sighandler;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGTHR, &sa, NULL) == 0)
		errx(1, "sigaction(SIGTHR) succeeded");
	else if (errno != EINVAL)
		err(1, "sigaction(SIGTHR) didn't fail with EINVAL");
	if (sigaction(SIGUSR1, &sa, NULL))
		err(1, "sigaction(SIGUSR1)");
	if (sigaction(SIGUSR1, NULL, &sa))
		err(1, "sigaction(SIGUSR1)");
	if (sigismember(&sa.sa_mask, SIGTHR))
		errx(1, "SIGTHR blocked with sigaction");

	return 0;
}
