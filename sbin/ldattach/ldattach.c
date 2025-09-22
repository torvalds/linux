/*	$OpenBSD: ldattach.c,v 1.20 2023/04/19 12:58:15 jsg Exp $	*/

/*
 * Copyright (c) 2007, 2008 Marc Balmer <mbalmer@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Attach a line disciplines to a tty(4) device either from the commandline
 * or from init(8) (using entries in /etc/ttys).  Optionally pass the data
 * received on the tty(4) device to the master device of a pty(4) pair.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/limits.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "atomicio.h"

__dead void	usage(void);
void		relay(int, int);
void		coroner(int);

volatile sig_atomic_t dying = 0;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-27dehmop] [-s baudrate] "
	    "[-t cond] discipline device\n", __progname);
	exit(1);
}

/* relay data between two file descriptors */
void
relay(int device, int pty)
{
	struct pollfd pfd[2];
	int nfds, n, nread;
	char buf[128];

	pfd[0].fd = device;
	pfd[1].fd = pty;

	while (!dying) {
		pfd[0].events = POLLRDNORM;
		pfd[1].events = POLLRDNORM;
		nfds = poll(pfd, 2, INFTIM);
		if (nfds == -1) {
			syslog(LOG_ERR, "polling error");
			exit(1);
		}
		if (nfds == 0)	/* should not happen */
			continue;

		if (pfd[1].revents & POLLHUP) {	/* slave device not connected */
			sleep(1);
			continue;
		}

		for (n = 0; n < 2; n++) {
			if (!(pfd[n].revents & POLLRDNORM))
				continue;

			nread = read(pfd[n].fd, buf, sizeof(buf));
			if (nread == -1) {
				syslog(LOG_ERR, "error reading from %s: %m",
				    n ? "pty" : "device");
				exit(1);
			}
			if (nread == 0) {
				syslog(LOG_ERR, "eof during read from %s: %m",
				     n ? "pty" : "device");
				exit(1);
			}
			atomicio(vwrite, pfd[1 - n].fd, buf, nread);
		}
	}
}

int
main(int argc, char *argv[])
{
	struct termios tty;
	struct tstamps tstamps;
	const char *errstr;
	sigset_t sigset;
	pid_t ppid;
	int ch, fd, master = -1, slave, pty = 0, ldisc, nodaemon = 0;
	int bits = 0, parity = 0, stop = 0, flowcl = 0, hupcl = 1;
	speed_t speed = 0;
	char devn[32], ptyn[32], *dev, *disc;

	tstamps.ts_set = tstamps.ts_clr = 0;

	if ((ppid = getppid()) == 1)
		nodaemon = 1;

	while ((ch = getopt(argc, argv, "27dehmops:t:")) != -1) {
		switch (ch) {
		case '2':
			stop = 2;
			break;
		case '7':
			bits = 7;
			break;
		case 'd':
			nodaemon = 1;
			break;
		case 'e':
			parity = 'e';
			break;
		case 'h':
			flowcl = 1;
			break;
		case 'm':
			hupcl = 0;
			break;
		case 'o':
			parity = 'o';
			break;
		case 'p':
			pty = 1;
			break;
		case 's':
			speed = (speed_t)strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr) {
				if (ppid != 1)
					errx(1,  "speed is %s: %s", errstr,
					    optarg);
				else
					goto bail_out;
			}
			break;
		case 't':
			if (!strcasecmp(optarg, "dcd"))
				tstamps.ts_set |= TIOCM_CAR;
			else if (!strcasecmp(optarg, "!dcd"))
				tstamps.ts_clr |= TIOCM_CAR;
			else if (!strcasecmp(optarg, "cts"))
				tstamps.ts_set |= TIOCM_CTS;
			else if (!strcasecmp(optarg, "!cts"))
				tstamps.ts_clr |= TIOCM_CTS;
			else {
				if (ppid != 1)
					errx(1, "'%s' not supported for "
					    "timestamping", optarg);
				else
					goto bail_out;
			}
			break;
		default:
			if (ppid != -1)
				usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (ppid != 1 && argc != 2)
		usage();

	disc = *argv++;
	dev = *argv;
	if (strncmp(_PATH_DEV, dev, sizeof(_PATH_DEV) - 1)) {
		(void)snprintf(devn, sizeof(devn), "%s%s", _PATH_DEV, dev);
		dev = devn;
	}

	if (!strcmp(disc, "nmea")) {
		ldisc = NMEADISC;
		if (speed == 0)
			speed = B4800;	/* default is 4800 baud for nmea */
	} else if (!strcmp(disc, "msts")) {
		ldisc = MSTSDISC;
	} else if (!strcmp(disc, "endrun")) {
		ldisc = ENDRUNDISC;
	} else {
		syslog(LOG_ERR, "unknown line discipline %s", disc);
		goto bail_out;
	}

	if ((fd = open(dev, O_RDWR)) == -1) {
		syslog(LOG_ERR, "can't open %s", dev);
		goto bail_out;
	}

	/*
	 * Get the current line attributes, modify only values that are
	 * either requested on the command line or that are needed by
	 * the line discipline (e.g. nmea has a default baudrate of
	 * 4800 instead of 9600).
	 */
	if (tcgetattr(fd, &tty) == -1) {
		if (ppid != 1)
			warnx("tcgetattr");
		goto bail_out;
	}


	if (bits == 7) {
		tty.c_cflag &= ~CS8;
		tty.c_cflag |= CS7;
	} else if (bits == 8) {
		tty.c_cflag &= ~CS7;
		tty.c_cflag |= CS8;
	}

	if (parity != 0)
		tty.c_cflag |= PARENB;
	if (parity == 'o')
		tty.c_cflag |= PARODD;
	else
		tty.c_cflag &= ~PARODD;

	if (stop == 2)
		tty.c_cflag |= CSTOPB;
	else
		tty.c_cflag &= ~CSTOPB;

	if (flowcl)
		tty.c_cflag |= CRTSCTS;

	if (hupcl == 0)
		tty.c_cflag &= ~HUPCL;

	if (speed != 0)
		cfsetspeed(&tty, speed);

	/* setup common to all line disciplines */
	if (ioctl(fd, TIOCSDTR, 0) == -1)
		warn("TIOCSDTR");
	if (ioctl(fd, TIOCSETD, &ldisc) == -1) {
		syslog(LOG_ERR, "can't attach %s line discipline on %s", disc,
		    dev);
		goto bail_out;
	}

	/* line discpline specific setup */
	switch (ldisc) {
	case NMEADISC:
	case MSTSDISC:
	case ENDRUNDISC:
		if (ioctl(fd, TIOCSTSTAMP, &tstamps) == -1) {
			warnx("TIOCSTSTAMP");
			goto bail_out;
		}
		tty.c_cflag |= CLOCAL;
		tty.c_iflag = 0;
		tty.c_lflag = 0;
		tty.c_oflag = 0;
		tty.c_cc[VMIN] = 1;
		tty.c_cc[VTIME] = 0;
		break;
	}

	/* finally set the line attributes */
	if (tcsetattr(fd, TCSADRAIN, &tty) == -1) {
		if (ppid != 1)
			warnx("tcsetattr");
		goto bail_out;
	}

	/*
	 * open a pty(4) pair to pass the data if the -p option has been
	 * given on the commandline.
	 */
	if (pty) {
		if (openpty(&master, &slave, ptyn, NULL, NULL))
			errx(1, "can't open a pty");
		close(slave);
		printf("%s\n", ptyn);
		fflush(stdout);
	}
	if (nodaemon)
		openlog("ldattach", LOG_PID | LOG_CONS | LOG_PERROR,
		    LOG_DAEMON);
	else {
		openlog("ldattach", LOG_PID | LOG_CONS, LOG_DAEMON);
		if (daemon(0, 0))
			errx(1, "can't daemonize");
	}

	syslog(LOG_INFO, "attach %s on %s", disc, dev);
	signal(SIGHUP, coroner);
	signal(SIGTERM, coroner);

	if (master != -1) {
		syslog(LOG_INFO, "passing data to %s", ptyn);
		relay(fd, master);
	} else {
		sigemptyset(&sigset);

		while (!dying)
			sigsuspend(&sigset);
	}

bail_out:
	if (ppid == 1)
		sleep(30);	/* delay restart when called from init */

	return 0;
}

void
coroner(int useless)
{
	dying = 1;
}
