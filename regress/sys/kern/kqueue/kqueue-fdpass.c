/*	$OpenBSD: kqueue-fdpass.c,v 1.3 2016/09/20 23:05:27 bluhm Exp $	*/
/*
 *	Written by Philip Guenther <guenther@openbsd.org> 2011 Public Domain
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

int
do_fdpass(void)
{
	struct msghdr msg;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	struct kevent ke;
	struct cmsghdr *cmp;
	pid_t pid;
	int pfd[2], fd, status;

	ASS(socketpair(PF_LOCAL, SOCK_STREAM, 0, pfd) == 0,
	    warn("socketpair"));

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		close(pfd[0]);

		/* a kqueue with event to pass */
		fd = kqueue();
		EV_SET(&ke, SIGHUP, EVFILT_SIGNAL, EV_ADD|EV_ENABLE,
		    0, 0, NULL);
		if (kevent(fd, &ke, 1, NULL, 0, NULL) != 0)
			err(1, "can't register events on kqueue");

		memset(&cmsgbuf.buf, 0, sizeof cmsgbuf.buf);
		memset(&msg, 0, sizeof msg);
		msg.msg_control = &cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf);

		cmp = CMSG_FIRSTHDR(&msg);
		cmp->cmsg_len = CMSG_LEN(sizeof(int));
		cmp->cmsg_level = SOL_SOCKET;
		cmp->cmsg_type = SCM_RIGHTS;

		*(int *)CMSG_DATA(cmp) = fd;

		if (sendmsg(pfd[1], &msg, 0) == 0)
			errx(1, "sendmsg succeeded when it shouldn't");
		if (errno != EINVAL)
			err(1, "child sendmsg");
		printf("sendmsg failed with EINVAL as expected\n");
		close(pfd[1]);
		exit(0);
	}

	close(pfd[1]);
	wait(&status);
	close(pfd[0]);

	return (0);
}
