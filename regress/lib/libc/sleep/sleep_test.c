/*	$OpenBSD: sleep_test.c,v 1.2 2015/10/18 23:27:43 guenther Exp $ */

/*
 * Copyright (c) 2009 Philip Guenther
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Test whether sleep returns the correct value
 */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
handler(int sig)
{
	return;
}

static void *
garbage(void)
{
	char buf[20];
	strlcpy(buf, "012354678901235467890123546789", sizeof buf);
	return buf;
}

int
main(int argc, char *argv[])
{
	struct sigaction	sa;
	char const	*errstr;
	int	i;

	if (argc != 2)
		return (1);
	errno = 0;
	i = strtonum(argv[1], 0, INT_MAX, &errstr);
	if (i == 0 && errno != 0) {
		fprintf(stderr, "%s\n", errstr);
		return (1);
	}

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = &handler;
	if (sigaction(SIGTERM, &sa, NULL))
		err(3, "sigaction");
	garbage();
	printf("%d\n", sleep(i));
	return (0);
}

