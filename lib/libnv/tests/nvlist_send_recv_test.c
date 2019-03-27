/*-
 * Copyright (c) 2013 The FreeBSD Foundation
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/nv.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int ntest = 1;

#define	CHECK(expr)	do {						\
	if ((expr))							\
		printf("ok # %d %s:%u\n", ntest, __FILE__, __LINE__);	\
	else								\
		printf("not ok # %d %s:%u\n", ntest, __FILE__, __LINE__);\
	ntest++;							\
} while (0)

#define	fd_is_valid(fd)	(fcntl((fd), F_GETFL) != -1 || errno != EBADF)

static void
child(int sock)
{
	nvlist_t *nvl;
	nvlist_t *empty;

	nvl = nvlist_create(0);
	empty = nvlist_create(0);

	nvlist_add_bool(nvl, "nvlist/bool/true", true);
	nvlist_add_bool(nvl, "nvlist/bool/false", false);
	nvlist_add_number(nvl, "nvlist/number/0", 0);
	nvlist_add_number(nvl, "nvlist/number/1", 1);
	nvlist_add_number(nvl, "nvlist/number/-1", -1);
	nvlist_add_number(nvl, "nvlist/number/UINT64_MAX", UINT64_MAX);
	nvlist_add_number(nvl, "nvlist/number/INT64_MIN", INT64_MIN);
	nvlist_add_number(nvl, "nvlist/number/INT64_MAX", INT64_MAX);
	nvlist_add_string(nvl, "nvlist/string/", "");
	nvlist_add_string(nvl, "nvlist/string/x", "x");
	nvlist_add_string(nvl, "nvlist/string/abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz");
	nvlist_add_descriptor(nvl, "nvlist/descriptor/STDERR_FILENO", STDERR_FILENO);
	nvlist_add_binary(nvl, "nvlist/binary/x", "x", 1);
	nvlist_add_binary(nvl, "nvlist/binary/abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwxyz", sizeof("abcdefghijklmnopqrstuvwxyz"));
	nvlist_move_nvlist(nvl, "nvlist/nvlist/empty", empty);
	nvlist_add_nvlist(nvl, "nvlist/nvlist", nvl);

	nvlist_send(sock, nvl);

	nvlist_destroy(nvl);
}

static void
parent(int sock)
{
	nvlist_t *nvl;
	const nvlist_t *cnvl, *empty;
	const char *name, *cname;
	void *cookie, *ccookie;
	int type, ctype;
	size_t size;

	nvl = nvlist_recv(sock, 0);
	CHECK(nvlist_error(nvl) == 0);
	if (nvlist_error(nvl) != 0)
		err(1, "nvlist_recv() failed");

	cookie = NULL;

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_BOOL);
	CHECK(strcmp(name, "nvlist/bool/true") == 0);
	CHECK(nvlist_get_bool(nvl, name) == true);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_BOOL);
	CHECK(strcmp(name, "nvlist/bool/false") == 0);
	CHECK(nvlist_get_bool(nvl, name) == false);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NUMBER);
	CHECK(strcmp(name, "nvlist/number/0") == 0);
	CHECK(nvlist_get_number(nvl, name) == 0);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NUMBER);
	CHECK(strcmp(name, "nvlist/number/1") == 0);
	CHECK(nvlist_get_number(nvl, name) == 1);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NUMBER);
	CHECK(strcmp(name, "nvlist/number/-1") == 0);
	CHECK((int)nvlist_get_number(nvl, name) == -1);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NUMBER);
	CHECK(strcmp(name, "nvlist/number/UINT64_MAX") == 0);
	CHECK(nvlist_get_number(nvl, name) == UINT64_MAX);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NUMBER);
	CHECK(strcmp(name, "nvlist/number/INT64_MIN") == 0);
	CHECK((int64_t)nvlist_get_number(nvl, name) == INT64_MIN);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NUMBER);
	CHECK(strcmp(name, "nvlist/number/INT64_MAX") == 0);
	CHECK((int64_t)nvlist_get_number(nvl, name) == INT64_MAX);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_STRING);
	CHECK(strcmp(name, "nvlist/string/") == 0);
	CHECK(strcmp(nvlist_get_string(nvl, name), "") == 0);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_STRING);
	CHECK(strcmp(name, "nvlist/string/x") == 0);
	CHECK(strcmp(nvlist_get_string(nvl, name), "x") == 0);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_STRING);
	CHECK(strcmp(name, "nvlist/string/abcdefghijklmnopqrstuvwxyz") == 0);
	CHECK(strcmp(nvlist_get_string(nvl, name), "abcdefghijklmnopqrstuvwxyz") == 0);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_DESCRIPTOR);
	CHECK(strcmp(name, "nvlist/descriptor/STDERR_FILENO") == 0);
	CHECK(fd_is_valid(nvlist_get_descriptor(nvl, name)));

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_BINARY);
	CHECK(strcmp(name, "nvlist/binary/x") == 0);
	CHECK(memcmp(nvlist_get_binary(nvl, name, NULL), "x", 1) == 0);
	CHECK(memcmp(nvlist_get_binary(nvl, name, &size), "x", 1) == 0);
	CHECK(size == 1);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_BINARY);
	CHECK(strcmp(name, "nvlist/binary/abcdefghijklmnopqrstuvwxyz") == 0);
	CHECK(memcmp(nvlist_get_binary(nvl, name, NULL), "abcdefghijklmnopqrstuvwxyz", sizeof("abcdefghijklmnopqrstuvwxyz")) == 0);
	CHECK(memcmp(nvlist_get_binary(nvl, name, &size), "abcdefghijklmnopqrstuvwxyz", sizeof("abcdefghijklmnopqrstuvwxyz")) == 0);
	CHECK(size == sizeof("abcdefghijklmnopqrstuvwxyz"));

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NVLIST);
	CHECK(strcmp(name, "nvlist/nvlist/empty") == 0);
	cnvl = nvlist_get_nvlist(nvl, name);
	CHECK(nvlist_empty(cnvl));

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name != NULL);
	CHECK(type == NV_TYPE_NVLIST);
	CHECK(strcmp(name, "nvlist/nvlist") == 0);
	cnvl = nvlist_get_nvlist(nvl, name);

	ccookie = NULL;

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_BOOL);
	CHECK(strcmp(cname, "nvlist/bool/true") == 0);
	CHECK(nvlist_get_bool(cnvl, cname) == true);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_BOOL);
	CHECK(strcmp(cname, "nvlist/bool/false") == 0);
	CHECK(nvlist_get_bool(cnvl, cname) == false);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NUMBER);
	CHECK(strcmp(cname, "nvlist/number/0") == 0);
	CHECK(nvlist_get_number(cnvl, cname) == 0);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NUMBER);
	CHECK(strcmp(cname, "nvlist/number/1") == 0);
	CHECK(nvlist_get_number(cnvl, cname) == 1);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NUMBER);
	CHECK(strcmp(cname, "nvlist/number/-1") == 0);
	CHECK((int)nvlist_get_number(cnvl, cname) == -1);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NUMBER);
	CHECK(strcmp(cname, "nvlist/number/UINT64_MAX") == 0);
	CHECK(nvlist_get_number(cnvl, cname) == UINT64_MAX);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NUMBER);
	CHECK(strcmp(cname, "nvlist/number/INT64_MIN") == 0);
	CHECK((int64_t)nvlist_get_number(cnvl, cname) == INT64_MIN);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NUMBER);
	CHECK(strcmp(cname, "nvlist/number/INT64_MAX") == 0);
	CHECK((int64_t)nvlist_get_number(cnvl, cname) == INT64_MAX);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_STRING);
	CHECK(strcmp(cname, "nvlist/string/") == 0);
	CHECK(strcmp(nvlist_get_string(cnvl, cname), "") == 0);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_STRING);
	CHECK(strcmp(cname, "nvlist/string/x") == 0);
	CHECK(strcmp(nvlist_get_string(cnvl, cname), "x") == 0);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_STRING);
	CHECK(strcmp(cname, "nvlist/string/abcdefghijklmnopqrstuvwxyz") == 0);
	CHECK(strcmp(nvlist_get_string(cnvl, cname), "abcdefghijklmnopqrstuvwxyz") == 0);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_DESCRIPTOR);
	CHECK(strcmp(cname, "nvlist/descriptor/STDERR_FILENO") == 0);
	CHECK(fd_is_valid(nvlist_get_descriptor(cnvl, cname)));

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_BINARY);
	CHECK(strcmp(cname, "nvlist/binary/x") == 0);
	CHECK(memcmp(nvlist_get_binary(cnvl, cname, NULL), "x", 1) == 0);
	CHECK(memcmp(nvlist_get_binary(cnvl, cname, &size), "x", 1) == 0);
	CHECK(size == 1);

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_BINARY);
	CHECK(strcmp(cname, "nvlist/binary/abcdefghijklmnopqrstuvwxyz") == 0);
	CHECK(memcmp(nvlist_get_binary(cnvl, cname, NULL), "abcdefghijklmnopqrstuvwxyz", sizeof("abcdefghijklmnopqrstuvwxyz")) == 0);
	CHECK(memcmp(nvlist_get_binary(cnvl, cname, &size), "abcdefghijklmnopqrstuvwxyz", sizeof("abcdefghijklmnopqrstuvwxyz")) == 0);
	CHECK(size == sizeof("abcdefghijklmnopqrstuvwxyz"));

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname != NULL);
	CHECK(ctype == NV_TYPE_NVLIST);
	CHECK(strcmp(cname, "nvlist/nvlist/empty") == 0);
	empty = nvlist_get_nvlist(cnvl, cname);
	CHECK(nvlist_empty(empty));

	cname = nvlist_next(cnvl, &ctype, &ccookie);
	CHECK(cname == NULL);

	name = nvlist_next(nvl, &type, &cookie);
	CHECK(name == NULL);

	nvlist_destroy(nvl);
}

static void
send_nvlist(void)
{
	int status, socks[2];
	pid_t pid;

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, socks) < 0)
		err(1, "socketpair() failed");
	pid = fork();
	switch (pid) {
	case -1:
		/* Failure. */
		err(1, "unable to fork");
	case 0:
		/* Child. */
		close(socks[0]);
		child(socks[1]);
		_exit(0);
	default:
		/* Parent. */
		close(socks[1]);
		parent(socks[0]);
		break;
	}

	if (waitpid(pid, &status, 0) < 0)
		err(1, "waitpid() failed");
}

static void
send_closed_fd(void)
{
	nvlist_t *nvl;
	int error, socks[2];

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, socks) < 0)
		err(1, "socketpair() failed");

	nvl = nvlist_create(0);
	nvlist_add_descriptor(nvl, "fd", 12345);
	error = nvlist_error(nvl);
	CHECK(error == EBADF);

	error = nvlist_send(socks[1], nvl);
	CHECK(error != 0 && errno == EBADF);
}

int
main(void)
{

	printf("1..136\n");
	fflush(stdout);

	send_nvlist();
	send_closed_fd();

	return (0);
}
