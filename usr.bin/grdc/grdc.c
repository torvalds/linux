/*
 * Grand digital clock for curses compatible terminals
 * Usage: grdc [-st] [n]   -- run for n seconds (default infinity)
 * Flags: -s: scroll
 *        -t: output time in 12-hour format
 *
 *
 * modified 10-18-89 for curses (jrl)
 * 10-18-89 added signal handling
 *
 * modified 03-25-03 for 12 hour option
 *     - Samy Al Bahra <samy@kerneled.com>
 *
 * $FreeBSD$
 */

#include <err.h>
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define YBASE	10
#define XBASE	10
#define XLENGTH 58
#define YDEPTH  7

static struct timespec now;
static struct tm *tm;

static short disp[11] = {
	075557, 011111, 071747, 071717, 055711,
	074717, 074757, 071111, 075757, 075717, 002020
};
static long old[6], next[6], new[6], mask;

static volatile sig_atomic_t sigtermed;

static int hascolor = 0;

static void set(int, int);
static void standt(int);
static void movto(int, int);
static void sighndl(int);
static void usage(void);

static void
sighndl(int signo)
{

	sigtermed = signo;
}

int
main(int argc, char *argv[])
{
	struct timespec delay;
	time_t prev_sec;
	long t, a;
	int i, j, s, k;
	int n;
	int ch;
	int scrol;
	int t12;

	t12 = scrol = 0;

	while ((ch = getopt(argc, argv, "ts")) != -1)
	switch (ch) {
	case 's':
		scrol = 1;
		break;
	case 't':
		t12 = 1;
		break;
	case '?':
	default:
		usage();
		/* NOTREACHED */
	}
	argc -= optind;
	argv += optind;

	if (argc > 1) {
		usage();
		/* NOTREACHED */
	}

	if (argc > 0) {
		n = atoi(*argv) + 1;
		if (n < 1) {
			warnx("number of seconds is out of range");
			usage();
			/* NOTREACHED */
		}
	} else
		n = 0;

	initscr();

	signal(SIGINT,sighndl);
	signal(SIGTERM,sighndl);
	signal(SIGHUP,sighndl);

	cbreak();
	noecho();
	curs_set(0);

	hascolor = has_colors();

	if(hascolor) {
		start_color();
		init_pair(1, COLOR_BLACK, COLOR_RED);
		init_pair(2, COLOR_RED, COLOR_BLACK);
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		attrset(COLOR_PAIR(2));
	}

	clear();
	refresh();

	if(hascolor) {
		attrset(COLOR_PAIR(3));

		mvaddch(YBASE - 2,  XBASE - 3, ACS_ULCORNER);
		hline(ACS_HLINE, XLENGTH);
		mvaddch(YBASE - 2,  XBASE - 2 + XLENGTH, ACS_URCORNER);

		mvaddch(YBASE + YDEPTH - 1,  XBASE - 3, ACS_LLCORNER);
		hline(ACS_HLINE, XLENGTH);
		mvaddch(YBASE + YDEPTH - 1,  XBASE - 2 + XLENGTH, ACS_LRCORNER);

		move(YBASE - 1,  XBASE - 3);
		vline(ACS_VLINE, YDEPTH);

		move(YBASE - 1,  XBASE - 2 + XLENGTH);
		vline(ACS_VLINE, YDEPTH);

		attrset(COLOR_PAIR(2));
	}
	clock_gettime(CLOCK_REALTIME_FAST, &now);
	prev_sec = now.tv_sec;
	do {
		mask = 0;
		tm = localtime(&now.tv_sec);
		set(tm->tm_sec%10, 0);
		set(tm->tm_sec/10, 4);
		set(tm->tm_min%10, 10);
		set(tm->tm_min/10, 14);

		if (t12) {
			if (tm->tm_hour < 12) {
				if (tm->tm_hour == 0)
					tm->tm_hour = 12;
				mvaddstr(YBASE + 5, XBASE + 52, "AM");
			} else {
				if (tm->tm_hour > 12)
					tm->tm_hour -= 12;
				mvaddstr(YBASE + 5, XBASE + 52, "PM");
			}
		}

		set(tm->tm_hour%10, 20);
		set(tm->tm_hour/10, 24);
		set(10, 7);
		set(10, 17);
		for(k=0; k<6; k++) {
			if(scrol) {
				for(i=0; i<5; i++)
					new[i] = (new[i]&~mask) | (new[i+1]&mask);
				new[5] = (new[5]&~mask) | (next[k]&mask);
			} else
				new[k] = (new[k]&~mask) | (next[k]&mask);
			next[k] = 0;
			for(s=1; s>=0; s--) {
				standt(s);
				for(i=0; i<6; i++) {
					if((a = (new[i]^old[i])&(s ? new : old)[i]) != 0) {
						for(j=0,t=1<<26; t; t>>=1,j++) {
							if(a&t) {
								if(!(a&(t<<1))) {
									movto(YBASE + i, XBASE + 2*j);
								}
								addstr("  ");
							}
						}
					}
					if(!s) {
						old[i] = new[i];
					}
				}
				if(!s) {
					refresh();
				}
			}
		}
		movto(6, 0);
		refresh();
		clock_gettime(CLOCK_REALTIME_FAST, &now);
		if (now.tv_sec == prev_sec) {
			if (delay.tv_nsec > 0) {
				delay.tv_sec = 0;
				delay.tv_nsec = 1000000000 - now.tv_nsec;
			} else {
				delay.tv_sec = 1;
				delay.tv_nsec = 0;
			}
			nanosleep(&delay, NULL);
			clock_gettime(CLOCK_REALTIME_FAST, &now);
		}
		n -= now.tv_sec - prev_sec;
		prev_sec = now.tv_sec;
		if (sigtermed) {
			standend();
			clear();
			refresh();
			endwin();
			errx(1, "terminated by signal %d", (int)sigtermed);
		}
	} while (n);
	standend();
	clear();
	refresh();
	endwin();
	return(0);
}

static void
set(int t, int n)
{
	int i, m;

	m = 7<<n;
	for(i=0; i<5; i++) {
		next[i] |= ((disp[t]>>(4-i)*3)&07)<<n;
		mask |= (next[i]^old[i])&m;
	}
	if(mask&m)
		mask |= m;
}

static void
standt(int on)
{
	if (on) {
		if(hascolor) {
			attron(COLOR_PAIR(1));
		} else {
			attron(A_STANDOUT);
		}
	} else {
		if(hascolor) {
			attron(COLOR_PAIR(2));
		} else {
			attroff(A_STANDOUT);
		}
	}
}

static void
movto(int line, int col)
{
	move(line, col);
}

static void
usage(void)
{

	(void)fprintf(stderr, "usage: grdc [-st] [n]\n");
	exit(1);
}
