/*	$OpenBSD: kqueue-process.c,v 1.13 2018/08/03 15:19:44 visa Exp $	*/
/*
 *	Written by Artur Grabowski <art@openbsd.org> 2002 Public Domain
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "main.h"

static int process_child(void);

static int pfd1[2];
static int pfd2[2];

int
do_process(void)
{
	struct kevent ke;
	struct timespec ts;
	int kq, status;
	pid_t pid, pid2;
	int didfork, didchild;
	int i;
	char ch = 0;

	/*
	 * Timeout in case something doesn't work.
	 */
	ts.tv_sec = 10;
	ts.tv_nsec = 0;

	ASS((kq = kqueue()) >= 0,
	    warn("kqueue"));

	/* Open pipes for synchronizing the children with the parent. */
	if (pipe(pfd1) == -1)
		err(1, "pipe 1");
	if (pipe(pfd2) == -1)
		err(1, "pipe 2");

	switch ((pid = fork())) {
	case -1:
		err(1, "fork");
	case 0:
		_exit(process_child());
	}

	EV_SET(&ke, pid, EVFILT_PROC, EV_ADD|EV_ENABLE|EV_CLEAR,
	    NOTE_EXIT|NOTE_FORK|NOTE_EXEC|NOTE_TRACK, 0, NULL);
	ASS(kevent(kq, &ke, 1, NULL, 0, NULL) == 0,
	    warn("can't register events on kqueue"));

	/* negative case */
	EV_SET(&ke, pid + (1ULL << 30), EVFILT_PROC, EV_ADD|EV_ENABLE|EV_CLEAR,
	    NOTE_EXIT|NOTE_FORK|NOTE_EXEC|NOTE_TRACK, 0, NULL);
	ASS(kevent(kq, &ke, 1, NULL, 0, NULL) != 0,
	    warnx("can register bogus pid on kqueue"));
	ASS(errno == ESRCH,
	    warn("register bogus pid on kqueue returned wrong error"));

	ASS(write(pfd1[1], &ch, 1) == 1,
	    warn("write sync 1"));

	didfork = didchild = 0;

	pid2 = -1;
	for (i = 0; i < 2; i++) {
		ASS(kevent(kq, NULL, 0, &ke, 1, &ts) == 1,
		    warnx("didn't receive event"));
		ASSX(ke.filter == EVFILT_PROC);
		switch (ke.fflags) {
		case NOTE_CHILD:
			didchild = 1;
			ASSX((pid_t)ke.data == pid);
			pid2 = ke.ident;
			fprintf(stderr, "child %d (from %d)\n", pid2, pid);
			break;
		case NOTE_FORK:
			didfork = 1;
			ASSX(ke.ident == pid);
			fprintf(stderr, "fork\n");
			break;
		case NOTE_TRACKERR:
			errx(1, "child tracking failed due to resource shortage");
		default:
			errx(1, "kevent returned weird event 0x%x pid %d",
			    ke.fflags, (pid_t)ke.ident);
		}
	}

	ASSX(pid2 != -1);

	/* Both children now sleeping. */

	ASSX(didchild == 1);
	ASSX(didfork == 1);

	ASS(write(pfd2[1], &ch, 1) == 1,
	    warn("write sync 2.1"));
	ASS(write(pfd1[1], &ch, 1) == 1,
	    warn("write sync 2"));

	/*
	 * Wait for child's exit. It also implies child-child has exited.
	 * This should ensure that NOTE_EXIT has been posted for both children.
	 * Child-child's events should get aggregated.
	 */
	if (wait(&status) < 0)
		err(1, "wait");

	for (i = 0; i < 2; i++) {
		ASS(kevent(kq, NULL, 0, &ke, 1, &ts) == 1,
		    warnx("didn't receive event"));
		ASSX(ke.filter == EVFILT_PROC);
		switch (ke.fflags) {
		case NOTE_EXIT:
			ASSX((pid_t)ke.ident == pid);
			fprintf(stderr, "child exit %d\n", pid);
			break;
		case NOTE_EXEC | NOTE_EXIT:
			ASSX((pid_t)ke.ident == pid2);
			fprintf(stderr, "child-child exec/exit %d\n", pid2);
			break;
		default:
			errx(1, "kevent returned weird event 0x%x pid %d",
			    ke.fflags, (pid_t)ke.ident);
		}
	}

	if (!WIFEXITED(status))
		errx(1, "child didn't exit?");

	close(kq);
	return (WEXITSTATUS(status) != 0);
}

static int
process_child(void)
{
	int status;
	char ch;

	ASS(read(pfd1[0], &ch, 1) == 1,
	    warn("read sync 1"));

	/* fork and see if tracking works. */
	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		ASS(read(pfd2[0], &ch, 1) == 1,
		    warn("read sync 2.1"));
		execl("/usr/bin/true", "true", (char *)NULL);
		err(1, "execl(true)");
	}

	ASS(read(pfd1[0], &ch, 1) == 1,
	    warn("read sync 2"));

	if (wait(&status) < 0)
		err(1, "wait 2");
	if (!WIFEXITED(status))
		errx(1, "child-child didn't exit?");

	return 0;
}
