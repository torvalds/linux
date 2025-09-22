/*	$OpenBSD: grdc.c,v 1.37 2022/09/27 03:01:42 deraadt Exp $	*/
/*
 *
 * Copyright 2002 Amos Shapir.  Public domain.
 *
 * Grand digital clock for curses compatible terminals
 * Usage: grdc [-s] [n]   -- run for n seconds (default infinity)
 * Flags: -s: scroll
 *
 * modified 10-18-89 for curses (jrl)
 * 10-18-89 added signal handling
 */

#include <sys/ioctl.h>

#include <curses.h>
#include <err.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define XLENGTH 58
#define YDEPTH  7

struct timespec now;
struct tm *tm;

short disp[11] = {
	075557, 011111, 071747, 071717, 055711,
	074717, 074757, 071111, 075757, 075717, 002020
};
long old[6], next[6], new[6], mask;

volatile sig_atomic_t sigalrmed = 0;
volatile sig_atomic_t sigtermed = 0;
volatile sig_atomic_t sigwinched = 0;

int hascolor = 0;

void getwinsize(int *, int *);
void set(int, int);
void standt(int);
void __dead usage(void);

void
sigalrm(int signo)
{
	sigalrmed = signo;
}

void
sighndl(int signo)
{
	sigtermed = signo;
}

void
sigresize(int signo)
{
	sigwinched = signo;
}

int
main(int argc, char *argv[])
{
	long t, a;
	int i, j, s, k, rv;
	int scrol;
	unsigned int n = 0;
	struct timespec delay;
	struct pollfd pfd;
	const char *errstr;
	long scroldelay = 50000000;
	int xbase;
	int ybase;
	int wintoosmall;
	int tz_len = 0;
	int h, m;
	int prev_tm_gmtoff;
	char *tz;

	tz = getenv("TZ");
	if (tz != NULL)
		tz_len = strlen(tz);

	scrol = wintoosmall = 0;
	while ((i = getopt(argc, argv, "sh")) != -1) {
		switch (i) {
		case 's':
			scrol = 1;
			break;
		case 'h':
		default:
			usage();
		}
	}
	argv += optind;
	argc -= optind;

	if (argc > 1)
		usage();
	if (argc == 1) {
		n = strtonum(*argv, 1, UINT_MAX, &errstr);
		if (errstr) {
			warnx("number of seconds is %s", errstr);
			usage();
		}
	}

	initscr();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

	signal(SIGINT, sighndl);
	signal(SIGTERM, sighndl);
	signal(SIGHUP, sighndl);
	signal(SIGWINCH, sigresize);
	signal(SIGCONT, sigresize);	/* for resizes during suspend */

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;

	cbreak();
	noecho();

	hascolor = has_colors();

	if (hascolor) {
		start_color();
		init_pair(1, COLOR_BLACK, COLOR_RED);
		init_pair(2, COLOR_RED, COLOR_BLACK);
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		attrset(COLOR_PAIR(2));
	}

	curs_set(0);
	sigwinched = 1;	/* force initial sizing */
	prev_tm_gmtoff = 24 * 3600; /* force initial header printing */

	clock_gettime(CLOCK_REALTIME, &now);
	if (n) {
		signal(SIGALRM, sigalrm);
		alarm(n);
	}
	do {
		mask = 0;
		tm = localtime(&now.tv_sec);
		set(tm->tm_sec % 10, 0);
		set(tm->tm_sec / 10, 4);
		set(tm->tm_min % 10, 10);
		set(tm->tm_min / 10, 14);
		set(tm->tm_hour % 10, 20);
		set(tm->tm_hour / 10, 24);
		set(10, 7);
		set(10, 17);
		/* force repaint if window size changed or DST changed */
		if (sigwinched || prev_tm_gmtoff != tm->tm_gmtoff) {
			sigwinched = 0;
			wintoosmall = 0;
			prev_tm_gmtoff = tm->tm_gmtoff;
			getwinsize(&i, &j);
			if (i >= XLENGTH + 2)
				xbase = (i - XLENGTH) / 2;
			else
				wintoosmall = 1;
			if (j >= YDEPTH + 2)
				ybase = (j - YDEPTH) / 2;
			else
				wintoosmall = 1;
			resizeterm(j, i);
			clear();
			refresh();
			if (hascolor && !wintoosmall) {
				attrset(COLOR_PAIR(3));

				mvaddch(ybase - 1, xbase - 1, ACS_ULCORNER);
				hline(ACS_HLINE, XLENGTH);
				mvaddch(ybase - 1, xbase + XLENGTH, ACS_URCORNER);

				mvaddch(ybase + YDEPTH, xbase - 1, ACS_LLCORNER);
				hline(ACS_HLINE, XLENGTH);
				mvaddch(ybase + YDEPTH, xbase + XLENGTH, ACS_LRCORNER);

				move(ybase, xbase - 1);
				vline(ACS_VLINE, YDEPTH);

				move(ybase, xbase + XLENGTH);
				vline(ACS_VLINE, YDEPTH);

				move(ybase - 1, xbase);

				h = tm->tm_gmtoff / 3600;
				m = abs((int)tm->tm_gmtoff % 3600 / 60);

				if (tz_len > 0 && tz_len <= XLENGTH -
				    strlen("[  () +0000 ]") -
				    strlen(tm->tm_zone))
					printw("[ %s (%s) %+2.2d%02d ]", tz,
					    tm->tm_zone, h, m);
				else
					printw("[ %s %+2.2d%02d ]",
					    tm->tm_zone, h, m);

				attrset(COLOR_PAIR(2));
			}
			for (k = 0; k < 6; k++)
				old[k] = 0;
		}
		if (wintoosmall) {
			move(0, 0);
			printw("%02d:%02d:%02d", tm->tm_hour, tm->tm_min,
			    tm->tm_sec);
		} else for (k = 0; k < 6; k++) {
			if (scrol) {
				for(i = 0; i < 5; i++)
					new[i] = (new[i] & ~mask) | (new[i + 1] & mask);
				new[5] = (new[5] & ~mask) | (next[k] & mask);
			} else
				new[k] = (new[k] & ~mask) | (next[k] & mask);
			next[k] = 0;
			for (s = 1; s >= 0; s--) {
				standt(s);
				for (i = 0; i < 6; i++) {
					if ((a = (new[i] ^ old[i]) & (s ? new : old)[i]) != 0) {
						for (j = 0, t = 1 << 26; t; t >>= 1, j++) {
							if (a & t) {
								if (!(a & (t << 1))) {
									move(ybase + i + 1, xbase + 2 * (j + 1));
								}
								addstr("  ");
							}
						}
					}
					if (!s) {
						old[i] = new[i];
					}
				}
				if (!s) {
					refresh();
				}
			}
			if (scrol && k <= 4) {
				clock_gettime(CLOCK_REALTIME, &now);
				delay.tv_sec = 0;
				delay.tv_nsec = 1000000000 - now.tv_nsec
				    - (4 - k) * scroldelay;
				if (delay.tv_nsec <= scroldelay &&
				    delay.tv_nsec > 0)
					nanosleep(&delay, NULL);
			}
		}
		move(6, 0);
		refresh();
		clock_gettime(CLOCK_REALTIME, &now);
		delay.tv_sec = 0;
		delay.tv_nsec = (1000000000 - now.tv_nsec);
		/* want scrolling to END on the second */
		if (scrol && !wintoosmall)
			delay.tv_nsec -= 5 * scroldelay;
		rv = ppoll(&pfd, 1, &delay, NULL);
		if (rv == 1) {
			char q = 0;
			read(STDIN_FILENO, &q, 1);
			if (q == 'q')
				sigalrmed = 1;
		}
		now.tv_sec++;

		if (sigtermed) {
			standend();
			clear();
			refresh();
			endwin();
			exit(0);
		}
	} while (!sigalrmed);
	standend();
	clear();
	refresh();
	endwin();
	return 0;
}

void
set(int t, int n)
{
	int i, m;

	m = 7 << n;
	for (i = 0; i < 5; i++) {
		next[i] |= ((disp[t] >> (4 - i) * 3) & 07) << n;
		mask |= (next[i] ^ old[i]) & m;
	}
	if (mask & m)
		mask |= m;
}

void
standt(int on)
{
	if (on) {
		if (hascolor) {
			attron(COLOR_PAIR(1));
		} else {
			attron(A_STANDOUT);
		}
	} else {
		if (hascolor) {
			attron(COLOR_PAIR(2));
		} else {
			attroff(A_STANDOUT);
		}
	}
}

void
getwinsize(int *wid, int *ht)
{
	struct winsize size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1) {
		*wid = 80;     /* Default */
		*ht = 24;
	} else {
		*wid = size.ws_col;
		*ht = size.ws_row;
	}
}

void __dead
usage(void)
{
	fprintf(stderr, "usage: %s [-s] [number]\n", getprogname());
	exit(1);
}
