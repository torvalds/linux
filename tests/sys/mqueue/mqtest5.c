/* $FreeBSD$ */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <err.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "freebsd_test_suite/macros.h"

#define MQNAME	"/mytstqueue5"
#define LOOPS	1000
#define PRIO	10

static void
sighandler(int sig __unused)
{
	write(1, "timeout\n", 8);
	_exit(1);
}

int
main(void)
{
	int status;
	struct mq_attr attr;
	struct sigaction sa;
	sigset_t set;
	siginfo_t info;
	mqd_t mq;
	pid_t pid;

	PLAIN_REQUIRE_KERNEL_MODULE("mqueuefs", 0);

	mq_unlink(MQNAME);

	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);
	sigprocmask(SIG_BLOCK, &set, NULL);
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = (void *) SIG_DFL;
	sigaction(SIGRTMIN, &sa, NULL);

	attr.mq_maxmsg  = 5;
	attr.mq_msgsize = 128;
	mq = mq_open(MQNAME, O_CREAT | O_RDWR | O_EXCL, 0666, &attr);
	if (mq == (mqd_t)-1)
		err(1, "mq_open()");
	status = mq_getattr(mq, &attr);
	if (status)
		err(1, "mq_getattr()");
	pid = fork();
	if (pid == 0) { /* child */
		int prio, j, i;
		char *buf;
		struct sigevent sigev;

		signal(SIGALRM, sighandler);

		sigev.sigev_notify = SIGEV_SIGNAL;
		sigev.sigev_signo = SIGRTMIN;
		sigev.sigev_value.sival_int = 2;

		mq_close(mq);
		mq = mq_open(MQNAME, O_RDWR | O_NONBLOCK);
		if (mq == (mqd_t)-1)
			err(1, "child: mq_open");
		buf = malloc(attr.mq_msgsize);
		for (j = 0; j < LOOPS; ++j) {
			alarm(3);
			status = mq_notify(mq, &sigev);
			if (status)
				err(1, "child: mq_notify");
			status = sigwaitinfo(&set, &info);
			if (status == -1)
				err(1, "child: sigwaitinfo");
			if (info.si_value.sival_int != 2)
				err(1, "child: sival_int");
			status = mq_receive(mq, buf, attr.mq_msgsize, &prio);
			if (status == -1)
				err(2, "child: mq_receive");
			for (i = 0; i < attr.mq_msgsize; ++i)
				if (buf[i] != i)
					err(3, "child: message data corrupted");
			if (prio != PRIO)
				err(4, "child: priority is incorrect: %d",
					 prio);
		}
		alarm(0);
		free(buf);
		mq_close(mq);
		return (0);
	} else if (pid == -1) {
		err(1, "fork()");
	} else {
		char *buf;
		int i, j;

		signal(SIGALRM, sighandler);
		buf = malloc(attr.mq_msgsize);
		for (j = 0; j < LOOPS; ++j) {
			for (i = 0; i < attr.mq_msgsize; ++i) {
				buf[i] = i;
			}
			alarm(3);
			status = mq_send(mq, buf, attr.mq_msgsize, PRIO);
			if (status) {
				kill(pid, SIGKILL);
				err(2, "mq_send()");
			}
		}
		alarm(3);
		wait(&status);
		alarm(0);
	}
	status = mq_close(mq);
	if (status)
		err(1, "mq_close");
	mq_unlink(MQNAME);
	return (0);
}
