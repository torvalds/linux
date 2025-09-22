/*	$OpenBSD: kqueue-random.c,v 1.13 2021/12/14 06:26:15 anton Exp $	*/
/*	Written by Michael Shalayeff, 2002, Public Domain	*/

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

#define MINIMUM(a, b)    (((a) < (b)) ? (a) : (b))

int
do_random(void)
{
	int n, fd, kq;
	struct timespec ts;
	struct kevent ev;
	u_int32_t buf[BUFSIZ];

	ASS((fd = open("/dev/random", O_RDONLY)) >= 0,
	    warn("open: /dev/random"));
	ASS(fcntl(fd, F_SETFL, O_NONBLOCK) == 0,
	    warn("fcntl"));

	ASS((kq = kqueue()) >= 0,
	    warn("kqueue"));

	memset(&ev, 0, sizeof(ev));
	ev.ident = fd;
	ev.filter = EVFILT_READ;
	ev.flags = EV_ADD | EV_ENABLE;
	n = kevent(kq, &ev, 1, NULL, 0, NULL);
	ASSX(n != -1);

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	n = kevent(kq, NULL, 0, &ev, 1, &ts);
	ASSX(n >= 0);

	n = MINIMUM((ev.data + 7) / 8, sizeof(buf));
	ASSX(read(fd, buf, n) > 0);

	close(kq);
	close(fd);

	return (0);
}
