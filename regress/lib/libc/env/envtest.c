/*	$OpenBSD: envtest.c,v 1.3 2021/09/01 09:26:32 jasper Exp $ */

/*
 * Copyright (c) 2010 Todd C. Miller <millert@openbsd.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static int
count_instances(const char *name)
{
	int count = 0;
	size_t namelen;
	char **ep;

	namelen = strlen(name);
	for (ep = environ; *ep != NULL; ep++) {
		if (strncmp(name, *ep, namelen) == 0 && (*ep)[namelen] == '=')
			count++;
	}

	return count;
}

static void
fake_env(void)
{
	static char *fakenv[7];

	fakenv[0] = "HOME=/root";
	fakenv[1] = "USER=root";
	fakenv[2] = "LOGNAME=root";
	fakenv[3] = "SHELL=/bin/sh";
	fakenv[4] = "USER=root";
	fakenv[5] = NULL;

	environ = fakenv;
}

int
main(int argc, char *argv[])
{
	char *buf;
	int n, failures = 0;
	size_t len, bufsize;

	fake_env();
	n = count_instances("USER");
	if (n != 2) {
		fprintf(stderr, "initial: %d instances of USER, expected %d\n",
		    n, 2);
		failures++;
	}

	if (unsetenv("USER") != 0) {
		fprintf(stderr, "unsetenv: failed to remove USER\n");
		failures++;
	}
	n = count_instances("USER");
	if (n != 0) {
		fprintf(stderr, "unsetenv: %d instances of USER, expected %d\n",
		    n, 0);
		failures++;
	}

	fake_env();
	if (setenv("USER", "nobody", 0) != 0) {
		fprintf(stderr, "setenv: failed to set USER\n");
		failures++;
	}
	n = count_instances("USER");
	if (n != 2) {
		fprintf(stderr, "setenv: %d instances of USER, expected %d\n",
		    n, 2);
		failures++;
	}

	fake_env();
	if (setenv("USER", "nobody", 1) != 0) {
		fprintf(stderr, "setenv: failed to set USER\n");
		failures++;
	}
	n = count_instances("USER");
	if (n != 1) {
		fprintf(stderr, "setenv: %d instances of USER, expected %d\n",
		    n, 1);
		failures++;
	}

	fake_env();
	if (putenv("USER=nobody") != 0) {
		fprintf(stderr, "putenv: failed to set USER\n");
		failures++;
	}
	n = count_instances("USER");
	if (n != 1) {
		fprintf(stderr, "putenv: %d instances of USER, expected %d\n",
		    n, 1);
		failures++;
	}

	return failures;
}
