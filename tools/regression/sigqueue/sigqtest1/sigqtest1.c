/* $FreeBSD$ */
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

int received;

void
handler(int sig, siginfo_t *si, void *ctx)
{
	if (si->si_code != SI_QUEUE)
		errx(1, "si_code != SI_QUEUE");
	if (si->si_value.sival_int != received)
		errx(1, "signal is out of order");
	received++;
}

int
main()
{
	struct sigaction sa;
	union sigval val;
	int ret;
	int i;
	sigset_t set;

	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;
	sigaction(SIGRTMIN, &sa, NULL);
	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	sigprocmask(SIG_BLOCK, &set, NULL);
	i = 0;
	for (;;) {
		val.sival_int = i;
		ret = sigqueue(getpid(), SIGRTMIN, val);
		if (ret == -1) {
			if (errno != EAGAIN) {
				errx(1, "errno != EAGAIN");
			}
			break;
		}
		i++;
	}
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	if (received != i)
		errx(1, "error, signal lost");
	printf("OK\n");
}
