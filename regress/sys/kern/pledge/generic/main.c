/*	$OpenBSD: main.c,v 1.11 2021/10/24 21:24:20 deraadt Exp $ */
/*
 * Copyright (c) 2015 Sebastien Marie <semarie@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "manager.h"

void test_request_stdio(void);
void test_request_tty(void);

static void
test_nop()
{
	/* nop */
}

static void
test_inet()
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int saved_errno = errno;
	close(fd);
	errno = saved_errno ? saved_errno : errno;
}

static void
test_kill()
{
	kill(0, SIGINT);
}

static void
test_pledge()
{
	if (pledge("stdio rpath", NULL) != 0)
		_exit(errno);
}

static void
test_rpath()
{
	int fd;
	char data[512];

	if ((fd = open("/dev/zero", O_RDONLY)) == -1)
		_exit(errno);

	if (read(fd, data, sizeof(data)) == -1)
		_exit(errno);

	close(fd);
}

static void
test_wpath()
{
	int fd;
	char data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };

	if ((fd = open("/dev/null", O_WRONLY)) == -1)
		_exit(errno);

	if (write(fd, data, sizeof(data)) == -1)
		_exit(errno);

	close(fd);
}

static void
test_cpath()
{
	const char filename[] = "/tmp/generic-test-cpath";

	if (mkdir(filename, S_IRWXU) == -1)
		_exit(errno);

	if (rmdir(filename) == -1)
		_exit(errno);
}

int
main(int argc, char *argv[])
{
	int ret = EXIT_SUCCESS;

	if (argc != 1)
		errx(1, "usage: %s", argv[0]);

	/*
	 * testsuite
	 */

	/* _exit is always allowed, and nothing else under flags=0 */
	start_test(&ret, "", test_nop);
	start_test(&ret, "", test_inet);

	/* test coredump */
	start_test(&ret, "abort", test_inet);

	/* inet under inet is ok (stdio is needed of close(2)) */
	start_test(&ret, "stdio", test_inet);
	start_test(&ret, "inet", test_inet);
	start_test(&ret, "stdio inet", test_inet);

	/* kill under fattr is forbidden */
	start_test(&ret, "fattr", test_kill);

	/* kill under stdio is allowed */
	start_test(&ret, "stdio", test_kill);

	/* stdio for open(2) */
	start_test(&ret, "stdio rpath", test_rpath);
	start_test(&ret, "stdio wpath", test_wpath);
	start_test(&ret, "cpath", test_cpath);

	/*
	 * test pledge(2) arguments
	 */
	/* same request */
	start_test(&ret, "stdio rpath", test_pledge);
	/* reduce request */
	start_test(&ret, "stdio rpath wpath", test_pledge);
	/* add request */
	start_test(&ret, "stdio", test_pledge);
	/* change request */
	start_test(&ret, "stdio unix", test_pledge);

	/* stdio */
	start_test(&ret, NULL, test_request_stdio);

	/* tty */
	start_test(&ret, NULL, test_request_tty);

	return (ret);
}
