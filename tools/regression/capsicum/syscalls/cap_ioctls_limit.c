/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/ioctl.h>
#include <sys/procdesc.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "misc.h"

static void
ioctl_tests_0(int fd)
{
	unsigned long cmds[2];

	CHECK(cap_ioctls_get(fd, NULL, 0) == CAP_IOCTLS_ALL);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(ioctl(fd, FIOCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(ioctl(fd, FIONCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	cmds[0] = FIOCLEX;
	cmds[1] = FIONCLEX;
	CHECK(cap_ioctls_limit(fd, cmds, nitems(cmds)) == 0);
	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == nitems(cmds));
	CHECK((cmds[0] == FIOCLEX && cmds[1] == FIONCLEX) ||
	    (cmds[0] == FIONCLEX && cmds[1] == FIOCLEX));
	cmds[0] = FIOCLEX;
	cmds[1] = FIONCLEX;
	CHECK(cap_ioctls_limit(fd, cmds, nitems(cmds)) == 0);
	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, 1) == nitems(cmds));
	CHECK(cmds[0] == FIOCLEX || cmds[0] == FIONCLEX);
	CHECK(cmds[1] == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(ioctl(fd, FIOCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(ioctl(fd, FIONCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	cmds[0] = FIOCLEX;
	CHECK(cap_ioctls_limit(fd, cmds, 1) == 0);
	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 1);
	CHECK(cmds[0] == FIOCLEX);
	cmds[0] = FIOCLEX;
	cmds[1] = FIONCLEX;
	errno = 0;
	CHECK(cap_ioctls_limit(fd, cmds, nitems(cmds)) == -1);
	CHECK(errno == ENOTCAPABLE);
	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 1);
	CHECK(cmds[0] == FIOCLEX);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(ioctl(fd, FIOCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	errno = 0;
	CHECK(ioctl(fd, FIONCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(cap_ioctls_limit(fd, NULL, 0) == 0);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);
	cmds[0] = FIOCLEX;
	errno = 0;
	CHECK(cap_ioctls_limit(fd, cmds, 1) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	errno = 0;
	CHECK(ioctl(fd, FIOCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	errno = 0;
	CHECK(ioctl(fd, FIONCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);
}

static void
ioctl_tests_1(int fd)
{
	unsigned long cmds[2];
	cap_rights_t rights;

	cmds[0] = FIOCLEX;
	CHECK(cap_ioctls_limit(fd, cmds, 1) == 0);
	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 1);
	CHECK(cmds[0] == FIOCLEX);
	CHECK(cmds[1] == 0);

	CAP_ALL(&rights);
	cap_rights_clear(&rights, CAP_IOCTL);

	CHECK(cap_rights_limit(fd, &rights) == 0);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);

	cmds[0] = FIOCLEX;
	cmds[1] = FIONCLEX;
	errno = 0;
	CHECK(cap_ioctls_limit(fd, cmds, nitems(cmds)) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);
	cmds[0] = FIOCLEX;
	errno = 0;
	CHECK(cap_ioctls_limit(fd, cmds, 1) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	errno = 0;
	CHECK(ioctl(fd, FIOCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	errno = 0;
	CHECK(ioctl(fd, FIONCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);
}

static void
ioctl_tests_2(int fd)
{
	unsigned long cmds[2];
	cap_rights_t rights;

	CAP_ALL(&rights);
	cap_rights_clear(&rights, CAP_IOCTL);

	CHECK(cap_rights_limit(fd, &rights) == 0);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);

	cmds[0] = FIOCLEX;
	cmds[1] = FIONCLEX;
	errno = 0;
	CHECK(cap_ioctls_limit(fd, cmds, nitems(cmds)) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);
	cmds[0] = FIOCLEX;
	errno = 0;
	CHECK(cap_ioctls_limit(fd, cmds, 1) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	errno = 0;
	CHECK(ioctl(fd, FIOCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	errno = 0;
	CHECK(ioctl(fd, FIONCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);
}

static void
ioctl_tests_send_0(int sock)
{
	unsigned long cmds[2];
	int fd;

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	cmds[0] = FIOCLEX;
	cmds[1] = FIONCLEX;
	CHECK(cap_ioctls_limit(fd, cmds, nitems(cmds)) == 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	cmds[0] = FIOCLEX;
	CHECK(cap_ioctls_limit(fd, cmds, 1) == 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(cap_ioctls_limit(fd, NULL, 0) == 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);
}

static void
ioctl_tests_recv_0(int sock)
{
	unsigned long cmds[2];
	int fd;

	CHECK(descriptor_recv(sock, &fd) == 0);

	CHECK(cap_ioctls_get(fd, NULL, 0) == CAP_IOCTLS_ALL);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(ioctl(fd, FIOCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(ioctl(fd, FIONCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(close(fd) == 0);

	CHECK(descriptor_recv(sock, &fd) == 0);

	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == nitems(cmds));
	CHECK((cmds[0] == FIOCLEX && cmds[1] == FIONCLEX) ||
	    (cmds[0] == FIONCLEX && cmds[1] == FIOCLEX));

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(ioctl(fd, FIOCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(ioctl(fd, FIONCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(close(fd) == 0);

	CHECK(descriptor_recv(sock, &fd) == 0);

	cmds[0] = cmds[1] = 0;
	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 1);
	CHECK(cmds[0] == FIOCLEX);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(ioctl(fd, FIOCLEX) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	errno = 0;
	CHECK(ioctl(fd, FIONCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(close(fd) == 0);

	CHECK(descriptor_recv(sock, &fd) == 0);

	CHECK(cap_ioctls_get(fd, cmds, nitems(cmds)) == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	errno = 0;
	CHECK(ioctl(fd, FIOCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	errno = 0;
	CHECK(ioctl(fd, FIONCLEX) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(close(fd) == 0);
}

int
main(void)
{
	int fd, pfd, sp[2];
	pid_t pid;

	printf("1..607\n");

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	ioctl_tests_0(fd);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	ioctl_tests_1(fd);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	ioctl_tests_2(fd);
	CHECK(close(fd) == 0);

	/* Child inherits descriptor and operates on it first. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork() failed");
	case 0:
		ioctl_tests_0(fd);
		CHECK(close(fd) == 0);
		exit(0);
	default:
		if (waitpid(pid, NULL, 0) == -1)
			err(1, "waitpid() failed");
		ioctl_tests_0(fd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor, but operates on it after parent. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork() failed");
	case 0:
		sleep(1);
		ioctl_tests_0(fd);
		CHECK(close(fd) == 0);
		exit(0);
	default:
		ioctl_tests_0(fd);
		if (waitpid(pid, NULL, 0) == -1)
			err(1, "waitpid() failed");
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor and operates on it first. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	pid = pdfork(&pfd, 0);
	switch (pid) {
	case -1:
		err(1, "pdfork() failed");
	case 0:
		ioctl_tests_1(fd);
		exit(0);
	default:
		if (pdwait(pfd) == -1)
			err(1, "pdwait() failed");
		close(pfd);
		ioctl_tests_1(fd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor, but operates on it after parent. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	pid = pdfork(&pfd, 0);
	switch (pid) {
	case -1:
		err(1, "pdfork() failed");
	case 0:
		sleep(1);
		ioctl_tests_1(fd);
		exit(0);
	default:
		ioctl_tests_1(fd);
		if (pdwait(pfd) == -1)
			err(1, "pdwait() failed");
		close(pfd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor and operates on it first. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork() failed");
	case 0:
		ioctl_tests_2(fd);
		exit(0);
	default:
		if (waitpid(pid, NULL, 0) == -1)
			err(1, "waitpid() failed");
		ioctl_tests_2(fd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor, but operates on it after parent. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	pid = fork();
	switch (pid) {
	case -1:
		err(1, "fork() failed");
	case 0:
		sleep(1);
		ioctl_tests_2(fd);
		exit(0);
	default:
		ioctl_tests_2(fd);
		if (waitpid(pid, NULL, 0) == -1)
			err(1, "waitpid() failed");
	}
	CHECK(close(fd) == 0);

	/* Send descriptors from parent to child. */
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		CHECK(close(sp[0]) == 0);
		ioctl_tests_recv_0(sp[1]);
		CHECK(close(sp[1]) == 0);
		exit(0);
	} else {
		CHECK(close(sp[1]) == 0);
		ioctl_tests_send_0(sp[0]);
		CHECK(waitpid(pid, NULL, 0) == pid);
		CHECK(close(sp[0]) == 0);
	}

	/* Send descriptors from child to parent. */
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		CHECK(close(sp[0]) == 0);
		ioctl_tests_send_0(sp[1]);
		CHECK(close(sp[1]) == 0);
		exit(0);
	} else {
		CHECK(close(sp[1]) == 0);
		ioctl_tests_recv_0(sp[0]);
		CHECK(waitpid(pid, NULL, 0) == pid);
		CHECK(close(sp[0]) == 0);
	}

	exit(0);
}
