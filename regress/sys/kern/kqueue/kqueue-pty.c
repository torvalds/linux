/*	$OpenBSD: kqueue-pty.c,v 1.10 2019/03/04 19:35:28 anton Exp $	*/

/*	Written by Michael Shalayeff, 2003, Public Domain	*/

#include <sys/types.h>
#include <sys/time.h>
#include <sys/event.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "main.h"

static int
pty_check(int kq, struct kevent *ev, int n, int rm, int rs, int wm, int ws)
{
	struct timespec ts;
	int i;

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	if ((n = kevent(kq, NULL, 0, ev, n, &ts)) < 0)
		err(1, "slave: kevent");

	ASSX(n != 0);

	for (i = 0; i < n; i++, ev++) {
		if (ev->filter == EVFILT_READ) {
			ASSX(ev->ident != -rm);
			ASSX(ev->ident != -rs);
			if (ev->ident == rm)
				rm = 0;
			if (ev->ident == rs)
				rs = 0;
		} else if (ev->filter == EVFILT_WRITE) {
			ASSX(ev->ident != -wm);
			ASSX(ev->ident != -ws);
			if (ev->ident == wm)
				wm = 0;
			if (ev->ident == ws)
				ws = 0;
		} else
			errx(1, "unknown event");
	}
	ASSX(rm <= 0);
	ASSX(rs <= 0);
	ASSX(wm <= 0);
	ASSX(ws <= 0);

	return (0);
}

static int
pty_rdrw(void)
{
	struct kevent ev[4];
	struct termios tt;
	int fd, kq, massa, slave;
	char buf[1024];

	ASS((fd = open("/dev/console", O_RDONLY, &tt)) > 0,
	    warn("open /dev/console"));
	ASS(tcgetattr(fd, &tt) == 0,
	    warn("tcgetattr"));
	cfmakeraw(&tt);
	tt.c_lflag &= ~ECHO;
	if (openpty(&massa, &slave, NULL, &tt, NULL) < 0)
		err(1, "openpty");
	if (fcntl(massa, F_SETFL, O_NONBLOCK) < 0)
		err(1, "massa: fcntl");
	if (fcntl(slave, F_SETFL, O_NONBLOCK) < 0)
		err(1, "massa: fcntl");
	if ((kq = kqueue()) == -1)
		err(1, "kqueue");

	/* test the read from the slave works */
	EV_SET(&ev[0], massa, EVFILT_READ,  EV_ADD|EV_ENABLE, 0, 0, NULL);
	EV_SET(&ev[1], massa, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, NULL);
	EV_SET(&ev[2], slave, EVFILT_READ,  EV_ADD|EV_ENABLE, 0, 0, NULL);
	EV_SET(&ev[3], slave, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, NULL);
	if (kevent(kq, ev, 4, NULL, 0, NULL) < 0)
		err(1, "slave: kevent add");

	memset(buf, 0, sizeof(buf));

	ASSX(pty_check(kq, ev, 4, -massa, -slave, massa, slave) == 0);

	if (write(massa, " ", 1) != 1)
		err(1, "massa: write");

	ASSX(pty_check(kq, ev, 4, -massa, slave, massa, slave) == 0);

	read(slave, buf, sizeof(buf));

	ASSX(pty_check(kq, ev, 4, -massa, -slave, massa, slave) == 0);

	while (write(massa, buf, sizeof(buf)) > 0)
		continue;

	ASSX(pty_check(kq, ev, 4, -massa, slave, -massa, slave) == 0);

	read(slave, buf, 1);

	ASSX(pty_check(kq, ev, 4, -massa, slave, massa, slave) == 0);

	while (read(slave, buf, sizeof(buf)) > 0)
		continue;

	ASSX(pty_check(kq, ev, 4, -massa, -slave, massa, slave) == 0);

	return (0);
}

static int
pty_close(void)
{
	struct kevent ev[1];
	struct timespec ts;
	int kq, massa, n, slave;

	if (openpty(&massa, &slave, NULL, NULL, NULL) == -1)
		err(1, "openpty");

	kq = kqueue();
	if (kq == -1)
		err(1, "kqueue");

	EV_SET(&ev[0], massa, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, NULL);
	if (kevent(kq, ev, 1, NULL, 0, NULL) == -1)
		err(1, "kevent: add");

	close(slave);

	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	n = kevent(kq, NULL, 0, ev, 1, &ts);
	ASSX(n == 1);
	ASSX(ev[0].filter == EVFILT_READ);
	ASSX(ev[0].flags & EV_EOF);

	return 0;
}

int
do_pty(int n)
{
	switch (n) {
	case 1:
		return pty_rdrw();
	case 2:
		return pty_close();
	default:
		errx(1, "unknown pty test number %d", n);
	}
}
