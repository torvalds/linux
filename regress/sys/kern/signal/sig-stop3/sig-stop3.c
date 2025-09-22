/*	$OpenBSD: sig-stop3.c,v 1.2 2025/09/16 09:35:39 jsg Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2007 Public Domain.
 *	Written by Claudio Jeker <claudio@openbsd.org> 2024 Public Domain.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <err.h>
#include <signal.h>
#include <pthread.h>

#define	THREAD_COUNT	4

volatile sig_atomic_t tstp_count, cont_count;
pid_t child;

static void
alrm_handler(int sig)
{
	kill(child, SIGKILL);
	dprintf(STDERR_FILENO, "timeout\n");
	_exit(2);
}


static void *
thread(void *arg)
{
	struct timespec ts = { .tv_sec = 2 };

	while (nanosleep(&ts, &ts) != 0)
		;

	return NULL;
}

static int
child_main(void)
{
	pthread_t self, pthread[THREAD_COUNT];
	sigset_t set;
	int i, r;

	for (i = 0; i < THREAD_COUNT; i++) {
		if ((r = pthread_create(&pthread[i], NULL, thread, NULL))) {
			warnc(r, "could not create thread");
			pthread[i] = self;
		}
	}

	/* terminate main process */
	pthread_exit(NULL);
}

int
main(int argc, char **argv)
{
	struct timespec ts = { .tv_nsec = 200 * 1000 * 1000 };
	int status;

	switch((child = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		exit(child_main());
	default:
		break;
	}

	signal(SIGALRM, alrm_handler);
	alarm(5);

	nanosleep(&ts, NULL);

	printf("sending SIGSTOP\n");
	if (kill(child, SIGSTOP) == -1)
		err(1, "kill");

	printf("waiting...\n");
	if (waitpid(child, &status, WCONTINUED|WUNTRACED) <= 0)
		err(1, "waitpid");

	if (!WIFSTOPPED(status))
		errx(1, "bad status, not stopped: %d", status);
	printf("got stopped notification\n");

	nanosleep(&ts, NULL);

	printf("killing child\n");
	if (kill(child, SIGKILL) == -1)
		err(1, "kill");

	if (waitpid(child, &status, 0) <= 0)
		err(1, "waitpid");

	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
		errx(1, "bad status: %d", status);

	printf("OK\n");
	return 0;
}
