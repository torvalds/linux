/*	$OpenBSD: dup2_self.c,v 1.3 2003/07/31 21:48:08 deraadt Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <fcntl.h>

/*
 * We're testing a small tweak in dup2 semantics. Normally dup and dup2
 * will clear the close-on-exec flag on the new fd (which appears to be
 * an implementation mistake from start and not some planned behavior).
 * In todays implementations of dup and dup2 we have to make an effort
 * to really clear that flag. But all tested implementations of dup2 have
 * another tweak. If we dup2(old, new) when old == new, the syscall
 * short-circuits and returns early (because there is no need to do all
 * the work (and there is a risk for serious mistakes)). So although the
 * docs say that dup2 should "take 'old', close 'new' perform a dup(2) of
 * 'old' into 'new'" the docs are not really followed because close-on-exec
 * is not cleared on 'new'.
 *
 * Since everyone has this bug, we pretend that this is the way it is
 * supposed to be and test here that it really works that way.
 *
 * This is a fine example on where two separate implementation fuckups
 * take out each other and make the end-result the way it was meant to be.
 */

int
main(int argc, char *argv[])
{
	int orgfd, fd1, fd2;
	char temp[] = "/tmp/dup2XXXXXXXXX";

	if ((orgfd = mkstemp(temp)) < 0)
		err(1, "mkstemp");
	remove(temp);

	if (ftruncate(orgfd, 1024) != 0)
		err(1, "ftruncate");

	if ((fd1 = dup(orgfd)) < 0)
		err(1, "dup");

	/* Set close-on-exec */
	if (fcntl(fd1, F_SETFD, 1) != 0)
		err(1, "fcntl(F_SETFD)");

	if ((fd2 = dup2(fd1, fd1)) < 0)
		err(1, "dup2");

	/* Test 1: Do we get the right fd? */
	if (fd2 != fd1)
		errx(1, "dup2 didn't give us the right fd");

	/* Test 2: Was close-on-exec cleared? */
	if (fcntl(fd2, F_GETFD) == 0)
		errx(1, "dup2 cleared close-on-exec");

	return 0;
}
