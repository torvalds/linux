/*	$OpenBSD: fpsig.c,v 1.4 2022/10/28 16:06:54 miod Exp $	*/

/*
 * Public domain.  2005, Otto Moerbeek
 *
 * Try to check if fp registers are properly saved and restored while
 * calling a signal hander.  This is not supposed to catch all that
 * can go wrong, but trashed fp registers will typically get caught.
 */
 
#include <err.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#define LIMIT	11.1

volatile sig_atomic_t count;

volatile double g1;
volatile double g2;

void
handler(int signo)
{
	double a, b, c = 0.0;

	for (a = 0.0; a < LIMIT; a += 1.1)
		for (b = 0.0; b < LIMIT; b += 1.1)
			c += a * a + b * b;

	if (signo) {
		g1 = c;
		count++;
	} else
		g2 = c;
}

int
main()
{
	struct itimerval it = {
		.it_interval =  { .tv_sec = 0, .tv_usec = 10000 },
		.it_value =  { .tv_sec = 0, .tv_usec = 10000 }
	};

	/* initialize global vars */
	handler(0);
	handler(1);

	signal(SIGALRM, handler);
	setitimer(ITIMER_REAL, &it, NULL);
	
	while (count < 10000) {
		handler(0);

		double a, b, h1 = g1, h2 = g2;

		for (a = 0.0; a < LIMIT; a += 1.1)
			for (b = 0.0; b < LIMIT; b += 1.1)
				h1 += a * a + b * b;
		for (a = 0.0; a < LIMIT; a += 1.1)
			for (b = 0.0; b < LIMIT; b += 1.1)
				h2 += a * a + b * b;

		if (h1 != h2)
			errx(1, "%f %f", g1, g2);
	}
	return (0);
}
