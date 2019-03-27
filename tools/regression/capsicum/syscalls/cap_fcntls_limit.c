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

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/procdesc.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "misc.h"

static void
fcntl_tests_0(int fd)
{
	uint32_t fcntlrights;

	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_ALL);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
	CHECK(fcntl(fd, F_GETFL) == (O_RDWR | O_NONBLOCK));
	CHECK(fcntl(fd, F_SETFL, 0) == 0);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);

	errno = 0;
	CHECK(cap_fcntls_limit(fd, ~CAP_FCNTL_ALL) == -1);
	CHECK(errno == EINVAL);
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == 0);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == (CAP_FCNTL_GETFL | CAP_FCNTL_SETFL));
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == 0);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == (CAP_FCNTL_GETFL | CAP_FCNTL_SETFL));

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
	CHECK(fcntl(fd, F_GETFL) == (O_RDWR | O_NONBLOCK));
	CHECK(fcntl(fd, F_SETFL, 0) == 0);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);

	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == 0);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_GETFL);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_GETFL);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, 0) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);

	CHECK(cap_fcntls_limit(fd, 0) == 0);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, 0) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
}

static void
fcntl_tests_1(int fd)
{
	uint32_t fcntlrights;
	cap_rights_t rights;

	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == 0);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_GETFL);

	CAP_ALL(&rights);
	cap_rights_clear(&rights, CAP_FCNTL);
	CHECK(cap_rights_limit(fd, &rights) == 0);

	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);

	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, 0) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
}

static void
fcntl_tests_2(int fd)
{
	uint32_t fcntlrights;
	cap_rights_t rights;

	CAP_ALL(&rights);
	cap_rights_clear(&rights, CAP_FCNTL);
	CHECK(cap_rights_limit(fd, &rights) == 0);

	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);

	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = CAP_FCNTL_ALL;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, 0) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
}

static void
fcntl_tests_send_0(int sock)
{
	int fd;

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK(cap_fcntls_limit(fd, 0) == 0);
	CHECK(descriptor_send(sock, fd) == 0);
	CHECK(close(fd) == 0);
}

static void
fcntl_tests_recv_0(int sock)
{
	uint32_t fcntlrights;
	int fd;

	CHECK(descriptor_recv(sock, &fd) == 0);

	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_ALL);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
	CHECK(fcntl(fd, F_GETFL) == (O_RDWR | O_NONBLOCK));
	CHECK(fcntl(fd, F_SETFL, 0) == 0);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);

	CHECK(close(fd) == 0);

	CHECK(descriptor_recv(sock, &fd) == 0);

	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == (CAP_FCNTL_GETFL | CAP_FCNTL_SETFL));
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == 0);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == (CAP_FCNTL_GETFL | CAP_FCNTL_SETFL));

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == 0);
	CHECK(fcntl(fd, F_GETFL) == (O_RDWR | O_NONBLOCK));
	CHECK(fcntl(fd, F_SETFL, 0) == 0);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);

	CHECK(close(fd) == 0);

	CHECK(descriptor_recv(sock, &fd) == 0);

	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_GETFL);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_GETFL);
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == 0);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == CAP_FCNTL_GETFL);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, 0) == -1);
	CHECK(errno == ENOTCAPABLE);
	CHECK(fcntl(fd, F_GETFL) == O_RDWR);

	CHECK(close(fd) == 0);

	CHECK(descriptor_recv(sock, &fd) == 0);

	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL | CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);
	errno = 0;
	CHECK(cap_fcntls_limit(fd, CAP_FCNTL_SETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	fcntlrights = 0;
	CHECK(cap_fcntls_get(fd, &fcntlrights) == 0);
	CHECK(fcntlrights == 0);

	CHECK(fcntl(fd, F_GETFD) == 0);
	CHECK(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0);
	CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
	CHECK(fcntl(fd, F_SETFD, 0) == 0);
	CHECK(fcntl(fd, F_GETFD) == 0);

	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, O_NONBLOCK) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_SETFL, 0) == -1);
	CHECK(errno == ENOTCAPABLE);
	errno = 0;
	CHECK(fcntl(fd, F_GETFL) == -1);
	CHECK(errno == ENOTCAPABLE);

	CHECK(close(fd) == 0);
}

int
main(void)
{
	int fd, pfd, sp[2];
	pid_t pid;

	printf("1..870\n");

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	fcntl_tests_0(fd);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	fcntl_tests_1(fd);
	CHECK(close(fd) == 0);

	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	fcntl_tests_2(fd);
	CHECK(close(fd) == 0);

	/* Child inherits descriptor and operates on it first. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		fcntl_tests_0(fd);
		CHECK(close(fd) == 0);
		exit(0);
	} else {
		CHECK(waitpid(pid, NULL, 0) == pid);
		fcntl_tests_0(fd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor, but operates on it after parent. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		sleep(1);
		fcntl_tests_0(fd);
		CHECK(close(fd) == 0);
		exit(0);
	} else {
		fcntl_tests_0(fd);
		CHECK(waitpid(pid, NULL, 0) == pid);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor and operates on it first. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK((pid = pdfork(&pfd, 0)) >= 0);
	if (pid == 0) {
		fcntl_tests_1(fd);
		exit(0);
	} else {
		CHECK(pdwait(pfd) == 0);
/*
		It fails with EBADF, which I believe is a bug.
		CHECK(close(pfd) == 0);
*/
		fcntl_tests_1(fd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor, but operates on it after parent. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK((pid = pdfork(&pfd, 0)) >= 0);
	if (pid == 0) {
		sleep(1);
		fcntl_tests_1(fd);
		exit(0);
	} else {
		fcntl_tests_1(fd);
		CHECK(pdwait(pfd) == 0);
/*
		It fails with EBADF, which I believe is a bug.
		CHECK(close(pfd) == 0);
*/
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor and operates on it first. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		fcntl_tests_2(fd);
		exit(0);
	} else {
		CHECK(waitpid(pid, NULL, 0) == pid);
		fcntl_tests_2(fd);
	}
	CHECK(close(fd) == 0);

	/* Child inherits descriptor, but operates on it after parent. */
	CHECK((fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		sleep(1);
		fcntl_tests_2(fd);
		exit(0);
	} else {
		fcntl_tests_2(fd);
		CHECK(waitpid(pid, NULL, 0) == pid);
	}
	CHECK(close(fd) == 0);

	/* Send descriptors from parent to child. */
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		CHECK(close(sp[0]) == 0);
		fcntl_tests_recv_0(sp[1]);
		CHECK(close(sp[1]) == 0);
		exit(0);
	} else {
		CHECK(close(sp[1]) == 0);
		fcntl_tests_send_0(sp[0]);
		CHECK(waitpid(pid, NULL, 0) == pid);
		CHECK(close(sp[0]) == 0);
	}

	/* Send descriptors from child to parent. */
	CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0);
	CHECK((pid = fork()) >= 0);
	if (pid == 0) {
		CHECK(close(sp[0]) == 0);
		fcntl_tests_send_0(sp[1]);
		CHECK(close(sp[1]) == 0);
		exit(0);
	} else {
		CHECK(close(sp[1]) == 0);
		fcntl_tests_recv_0(sp[0]);
		CHECK(waitpid(pid, NULL, 0) == pid);
		CHECK(close(sp[0]) == 0);
	}

	exit(0);
}
