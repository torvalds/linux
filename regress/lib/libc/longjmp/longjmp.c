/*	$OpenBSD: longjmp.c,v 1.4 2002/02/18 11:27:45 art Exp $	*/
/*
 *	Artur Grabowski <art@openbsd.org>, 2002 Public Domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <err.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>


jmp_buf buf;

/*
 * When longjmp is passed the incorrect arg (0), it should translate it into
 * something better.
 *
 * The rlimit is here in case we start spinning.
 */
int
main(int argc, char **argv)
{
	struct rlimit rl;
	volatile int i, expect;
	int (*sj)(jmp_buf);
	void (*lj)(jmp_buf, int);
	int ch;
	extern char *__progname;

	sj = setjmp;
	lj = longjmp;

	while ((ch = getopt(argc, argv, "_")) != -1) {
		switch (ch) {
		case '_':
			sj = _setjmp;
			lj = _longjmp;
			break;
		default:
			fprintf(stderr, "Usage: %s [-_]\n", __progname);
			exit(1);
		}
	}

	rl.rlim_cur = 2;
	rl.rlim_max = 2;
	if (setrlimit(RLIMIT_CPU, &rl) < 0)
		err(1, "setrlimit");

	expect = 0;
	i = (*sj)(buf);
	if (i == 0 && expect != 0)
		errx(1, "setjmp returns 0 on longjmp(.., 0)");
	if (expect == 0) {
		expect = -1;
		(*lj)(buf, 0);
	}

	expect = 0;
	i = (*sj)(buf);
	if (i != expect)
		errx(1, "bad return from setjmp %d/%d", expect, i);
	if (expect < 1000)
		(*lj)(buf, expect += 2);

	return 0;
}
