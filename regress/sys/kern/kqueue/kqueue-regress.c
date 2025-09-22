/*	$OpenBSD: kqueue-regress.c,v 1.5 2022/03/29 19:04:19 millert Exp $	*/
/*
 *	Written by Anton Lindqvist <anton@openbsd.org> 2018 Public Domain
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

static int do_regress1(void);
static int do_regress2(void);
static int do_regress3(void);
static int do_regress4(void);
static int do_regress5(void);
static int do_regress6(void);

static void make_chain(int);

int
do_regress(int n)
{
	switch (n) {
	case 1:
		return do_regress1();
	case 2:
		return do_regress2();
	case 3:
		return do_regress3();
	case 4:
		return do_regress4();
	case 5:
		return do_regress5();
	case 6:
		return do_regress6();
	default:
		errx(1, "unknown regress test number %d", n);
	}
}

/*
 * Regression test for NULL-deref in knote_processexit().
 */
static int
do_regress1(void)
{
	struct kevent kev[2];
	int kq;

	ASS((kq = kqueue()) >= 0,
	    warn("kqueue"));

	EV_SET(&kev[0], kq, EVFILT_READ, EV_ADD, 0, 0, NULL);
	EV_SET(&kev[1], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
	ASS(kevent(kq, kev, 2, NULL, 0, NULL) == 0,
	    warn("can't register events on kqueue"));

	/* kq intentionally left open */

	return 0;
}

/*
 * Regression test for use-after-free in kqueue_close().
 */
static int
do_regress2(void)
{
	pid_t pid;
	int i, status;

	/* Run twice in order to trigger the panic faster, if still present. */
	for (i = 0; i < 2; i++) {
		pid = fork();
		if (pid == -1)
			err(1, "fork");

		if (pid == 0) {
			struct kevent kev[1];
			int p0[2], p1[2];
			int kq;

			if (pipe(p0) == -1)
				err(1, "pipe");
			if (pipe(p1) == -1)
				err(1, "pipe");

			kq = kqueue();
			if (kq == -1)
				err(1, "kqueue");

			EV_SET(&kev[0], p0[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
			if (kevent(kq, kev, 1, NULL, 0, NULL) == -1)
				err(1, "kevent");

			EV_SET(&kev[0], p1[1], EVFILT_READ, EV_ADD, 0, 0, NULL);
			if (kevent(kq, kev, 1, NULL, 0, NULL) == -1)
				err(1, "kevent");

			EV_SET(&kev[0], p1[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
			if (kevent(kq, kev, 1, NULL, 0, NULL) == -1)
				err(1, "kevent");

			_exit(0);
		}

		if (waitpid(pid, &status, 0) == -1)
			err(1, "waitpid");
		assert(WIFEXITED(status));
		assert(WEXITSTATUS(status) == 0);
	}

	return 0;
}

/*
 * Regression test for kernel stack exhaustion.
 */
static int
do_regress3(void)
{
	pid_t pid;
	int dir, status;

	for (dir = 0; dir < 2; dir++) {
		pid = fork();
		if (pid == -1)
			err(1, "fork");

		if (pid == 0) {
			make_chain(dir);
			_exit(0);
		}

		if (waitpid(pid, &status, 0) == -1)
			err(1, "waitpid");
		assert(WIFEXITED(status));
		assert(WEXITSTATUS(status) == 0);
	}

	return 0;
}

static void
make_chain(int dir)
{
	struct kevent kev[1];
	int i, kq, prev;

	/*
	 * Build a chain of kqueues and leave the files open.
	 * If the chain is long enough and properly oriented, a broken kernel
	 * can exhaust the stack when this process exits.
	 */
	for (i = 0, prev = -1; i < 120; i++, prev = kq) {
		kq = kqueue();
		if (kq == -1)
			err(1, "kqueue");
		if (prev == -1)
			continue;

		if (dir == 0) {
			EV_SET(&kev[0], prev, EVFILT_READ, EV_ADD, 0, 0, NULL);
			if (kevent(kq, kev, 1, NULL, 0, NULL) == -1)
				err(1, "kevent");
		} else {
			EV_SET(&kev[0], kq, EVFILT_READ, EV_ADD, 0, 0, NULL);
			if (kevent(prev, kev, 1, NULL, 0, NULL) == -1)
				err(1, "kevent");
		}
	}
}

/*
 * Regression test for kernel stack exhaustion.
 */
static int
do_regress4(void)
{
	static const int nkqueues = 500;
	struct kevent kev[1];
	struct rlimit rlim;
	struct timespec ts;
	int fds[2], i, kq = -1, prev;

	if (getrlimit(RLIMIT_NOFILE, &rlim) == -1)
		err(1, "getrlimit");
	if (rlim.rlim_cur < nkqueues + 8) {
		rlim.rlim_cur = nkqueues + 8;
		if (setrlimit(RLIMIT_NOFILE, &rlim) == -1) {
			printf("RLIMIT_NOFILE is too low and can't raise it\n");
			printf("SKIPPED\n");
			exit(0);
		}
	}

	if (pipe(fds) == -1)
		err(1, "pipe");

	/* Build a chain of kqueus. The first kqueue refers to the pipe. */
	for (i = 0, prev = fds[0]; i < nkqueues; i++, prev = kq) {
		kq = kqueue();
		if (kq == -1)
			err(1, "kqueue");

		EV_SET(&kev[0], prev, EVFILT_READ, EV_ADD, 0, 0, NULL);
		if (kevent(kq, kev, 1, NULL, 0, NULL) == -1)
			err(1, "kevent");
	}

	/*
	 * Trigger a cascading event through the chain.
	 * If the chain is long enough, a broken kernel can run out
	 * of kernel stack space.
	 */
	write(fds[1], "x", 1);

	/*
	 * Check that the event gets propagated.
	 * The propagation is not instantaneous, so allow a brief pause.
	 */
	ts.tv_sec = 5;
	ts.tv_nsec = 0;
	assert(kevent(kq, NULL, 0, kev, 1, NULL) == 1);

	return 0;
}

/*
 * Regression test for select and poll with kqueue.
 */
static int
do_regress5(void)
{
	fd_set fdset;
	struct kevent kev[1];
	struct pollfd pfd[1];
	struct timeval tv;
	int fds[2], kq, ret;

	if (pipe(fds) == -1)
		err(1, "pipe");

	kq = kqueue();
	if (kq == -1)
		err(1, "kqueue");
	EV_SET(&kev[0], fds[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(kq, kev, 1, NULL, 0, NULL) == -1)
		err(1, "kevent");

	/* Check that no event is reported. */

	FD_ZERO(&fdset);
	FD_SET(kq, &fdset);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	ret = select(kq + 1, &fdset, NULL, NULL, &tv);
	if (ret == -1)
		err(1, "select");
	assert(ret == 0);

	pfd[0].fd = kq;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	ret = poll(pfd, 1, 0);
	if (ret == -1)
		err(1, "poll");
	assert(ret == 0);

	/* Trigger an event. */
	write(fds[1], "x", 1);

	/* Check that the event gets reported. */

	FD_ZERO(&fdset);
	FD_SET(kq, &fdset);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	ret = select(kq + 1, &fdset, NULL, NULL, &tv);
	if (ret == -1)
		err(1, "select");
	assert(ret == 1);
	assert(FD_ISSET(kq, &fdset));

	pfd[0].fd = kq;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	ret = poll(pfd, 1, 5000);
	if (ret == -1)
		err(1, "poll");
	assert(ret == 1);
	assert(pfd[0].revents & POLLIN);

	return 0;
}

int
test_regress6(int kq, size_t len)
{
	const struct timespec nap_time = { 0, 1 };
	int i, kstatus, wstatus;
	struct kevent event;
	pid_t child, pid;
	void *addr;

	child = fork();
	switch (child) {
	case -1:
		warn("fork");
		return -1;
	case 0:
		/* fork a bunch of zombies to keep the reaper busy, then exit */
		signal(SIGCHLD, SIG_IGN);
		for (i = 0; i < 1000; i++) {
			if (fork() == 0) {
				/* Dirty some memory so uvm_exit has work. */
				addr = mmap(NULL, len, PROT_READ|PROT_WRITE,
				    MAP_ANON, -1, 0);
				if (addr == MAP_FAILED)
					err(1, "mmap");
				memset(addr, 'A', len);
				nanosleep(&nap_time, NULL);
				_exit(2);
			}
		}
		nanosleep(&nap_time, NULL);
		_exit(1);
	default:
		/* parent */
		break;
	}

	/* Register NOTE_EXIT and wait for child. */
	EV_SET(&event, child, EVFILT_PROC, EV_ADD|EV_ONESHOT, NOTE_EXIT, 0,
	    NULL);
	if (kevent(kq, &event, 1, &event, 1, NULL) != 1)
		err(1, "kevent");
	if (event.flags & EV_ERROR)
		errx(1, "kevent: %s", strerror(event.data));
	if (event.ident != child)
		errx(1, "expected child %d, got %lu", child, event.ident);
	kstatus = event.data;
	if (!WIFEXITED(kstatus))
		errx(1, "child did not exit?");

	pid = waitpid(child, &wstatus, WNOHANG);
	switch (pid) {
	case -1:
		err(1, "waitpid %d", child);
	case 0:
		printf("kevent: child %d exited %d\n", child,
		    WEXITSTATUS(kstatus));
		printf("waitpid: child %d not ready\n", child);
		break;
	default:
		if (wstatus != kstatus) {
			/* macOS has a bug where kstatus is 0 */
			warnx("kevent status 0x%x != waitpid status 0x%x",
			    kstatus, wstatus);
		}
		break;
	}

	return pid;
}

/*
 * Regression test for NOTE_EXIT waitability.
 */
static int
do_regress6(void)
{
	int i, kq, page_size, rc;
	struct rlimit rlim;

	/* Bump process limits since we fork a lot. */
	if (getrlimit(RLIMIT_NPROC, &rlim) == -1)
		err(1, "getrlimit(RLIMIT_NPROC)");
	rlim.rlim_cur = rlim.rlim_max;
	if (setrlimit(RLIMIT_NPROC, &rlim) == -1)
		err(1, "setrlimit(RLIMIT_NPROC)");

	kq = kqueue();
	if (kq == -1)
		err(1, "kqueue");

	page_size = getpagesize();

	/* This test is inherently racey but fails within a few iterations. */
	for (i = 0; i < 25; i++) {
		rc = test_regress6(kq, page_size);
		switch (rc) {
		case -1:
			goto done;
		case 0:
			printf("child not ready when NOTE_EXIT received");
			if (i != 0)
				printf(" (%d iterations)", i + 1);
			putchar('\n');
			goto done;
		default:
			/* keep trying */
			continue;
		}
	}
	printf("child exited as expected when NOTE_EXIT received\n");

done:
	close(kq);
	return rc <= 0;
}
