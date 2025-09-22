/*	$OpenBSD: test_stdio.c,v 1.2 2016/04/27 13:05:05 semarie Exp $ */
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

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include <errno.h>
#include <unistd.h>

void
test_request_stdio()
{
	if (pledge("stdio", NULL) == -1)
		_exit(errno);

	clock_getres(CLOCK_MONOTONIC, NULL);
	{ struct timespec tp; clock_gettime(CLOCK_MONOTONIC, &tp); }
	/* fchdir(); */
	getdtablecount();
	getegid();
	geteuid();
	getgid();
	getgroups(0, NULL);
	{ struct itimerval v; getitimer(ITIMER_REAL, &v); }
	getlogin();
	getpgid(0);
	getpgrp();
	getpid();
	getppid();
	/* getresgid(); */
	/* getresuid(); */
	{ struct rlimit rl; getrlimit(RLIMIT_CORE, &rl); }
	getsid(0);
	getthrid();
	{ struct timeval tp; gettimeofday(&tp, NULL); }
	getuid();
	geteuid();
	issetugid();
	/* nanosleep(); */
	/* sigreturn(); */
	umask(0000);
	/* wait4(); */
}
