/*	$OpenBSD: pollretval.c,v 1.2 2021/12/26 13:32:05 bluhm Exp $	*/

#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>

int
main(void)
{
	struct pollfd pfd[100];
	int i, r, r2 = 0;

	for (i = 0; i < 100; i++) {
	    pfd[i].fd = 0;
	    pfd[i].events = arc4random() % 0x177;
	}

	r = poll(pfd, 100, INFTIM);

	if (r == -1)
		errx(1, "poll failed unexpectedly");

	for (i = 0; i < 100; i++)
		if (pfd[i].revents)
			r2++;
	if (r != r2)
		errx(1, "poll return value %d miscounts .revents %d", r, r2);

	return 0;
}
