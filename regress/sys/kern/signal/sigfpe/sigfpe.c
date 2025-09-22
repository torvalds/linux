/* Placed in the public domain by Todd C. Miller on April 30, 2002 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void check_oflow(void);
void check_div0(int);
void timedout(int);
void div0_handler(int);
__dead void usage(void);

/*
 * Check signal(SIGFPE, SIG_IGN) for overflow and divide by zero.
 */
int
main(int argc, char **argv)
{
	int ch, oflag, zflag;

	oflag = zflag = 0;
	while ((ch = getopt(argc, argv, "oz")) != -1) {
		switch (ch) {
		case 'o':
			oflag = 1;
			break;
		case 'z':
			zflag = 1;
			break;
		}
	}

	if (oflag && zflag)
		usage();

	signal(SIGFPE, div0_handler);
	signal(SIGALRM, timedout);

	if (oflag)
		check_oflow();
	else
		check_div0(0);

	exit(0);
}

void
check_oflow(void)
{
	double d, od;

	od = 0;
	d = 256;
	do {
		od = d;
		alarm(10);
		d *= d;
		alarm(0);
	} while (d != od);
}

void
check_div0(int zero)
{
	int i;

	alarm(10);
	i = 1 / zero;
	alarm(0);
}

void
timedout(int sig)
{
	_exit(1);
}

void
div0_handler(int sig)
{
	_exit(0);
}

__dead void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr, "usage: %s -o | -z\n", __progname);
	exit(1);
}
