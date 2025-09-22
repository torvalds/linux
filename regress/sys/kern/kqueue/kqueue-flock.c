/*	$OpenBSD: kqueue-flock.c,v 1.4 2016/09/20 23:05:27 bluhm Exp $	*/
/*
 *	Written by Philip Guenther <guenther@openbsd.org> 2012 Public Domain
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

#define FILE "lock.test"

static void
check_lock(int fd, const char *msg)
{
	pid_t pid = fork();
	int status;

	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		struct flock fl;

		memset(&fl, 0, sizeof fl);
		fl.l_type = F_WRLCK;
		if (fcntl(fd, F_SETLK, &fl) == 0) {
			printf("lock succeeded %s\n", msg);
			_exit(1);
		}
		if (errno != EAGAIN)
			err(1, "fcntl(SETLK)");
		if (fcntl(fd, F_GETLK, &fl))
			err(1, "fcntl(GETLK)");
		if (fl.l_type != F_WRLCK) {
			printf("lock not found %s\n", msg);
			_exit(1);
		}
		close(fd);
		_exit(0);
	}

	waitpid(pid, &status, 0);
	if (! WIFEXITED(status) || WEXITSTATUS(status) != 0)
		exit(1);
}

int
do_flock(void)
{
	int fd, kq;
	struct kevent kev;
	struct flock fl;

	fd = open(FILE, O_CREAT|O_RDWR, 0666);
	if (fd < 0)
		err(1, "open");
	memset(&fl, 0, sizeof fl);
	fl.l_type = F_WRLCK;
	if (fcntl(fd, F_SETLK, &fl))
		err(1, "fcntl(SETLK)");

	check_lock(fd, "before");

	kq = kqueue();
	EV_SET(&kev, fd, EVFILT_VNODE, EV_ADD, NOTE_LINK, 0, NULL);
	if (kevent(kq, &kev, 1, NULL, 0, NULL))
		err(1, "kevent");

	check_lock(fd, "after add");

	EV_SET(&kev, fd, EVFILT_VNODE, EV_DELETE, NOTE_LINK, 0, NULL);
	if (kevent(kq, &kev, 1, NULL, 0, NULL))
		err(1, "kevent");

	check_lock(fd, "after delete");

	close(kq);

	check_lock(fd, "after kq close");

	close(fd);
	unlink(FILE);
	return (0);
}
