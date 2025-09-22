/*	$OpenBSD: strlcpytest.c,v 1.5 2021/09/27 19:33:58 millert Exp $ */

/*
 * Copyright (c) 2014 Todd C. Miller <millert@openbsd.org>
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
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

volatile sig_atomic_t got_signal;
sigjmp_buf jmpenv;

void
handler(int signo)
{
	got_signal = signo;
	siglongjmp(jmpenv, 1);
}

int
main(int argc, char *argv[])
{
	char *buf, *buf2, *cp, *ep;
	struct sigaction sa;
	size_t len, bufsize;
	volatile int failures = 0;

	bufsize = getpagesize(); /* trigger guard pages easily */
	buf = malloc(bufsize);
	buf2 = malloc(bufsize);
	if (buf == NULL || buf2 == NULL) {
		fprintf(stderr, "unable to allocate memory\n");
		return 1;
	}
	memset(buf, 'z', bufsize);
	ep = buf + bufsize;

	/* Test copying to a zero-length NULL buffer. */
	len = strlcpy(NULL, "abcd", 0);
	if (len != 4) {
		fprintf(stderr,
		    "strlcpy: failed zero-length buffer test (1a)\n");
		failures++;
	}

	/* Test copying small string to a large buffer. */
	len = strlcpy(buf, "abcd", bufsize);
	if (len != 4) {
		fprintf(stderr, "strlcpy: failed large buffer test (2a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	if (memcmp(buf, "abcd", sizeof("abcd")) != 0) {
		fprintf(stderr, "strlcpy: failed large buffer test (2b)\n");
		failures++;
	}
	for (cp = buf + len + 1; cp < ep; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcpy: failed large buffer test (2c)\n");
			failures++;
			break;
		}
	}

	/* Test copying large string to a small buffer. */
	memset(buf, 'z', bufsize);
	memset(buf2, 'x', bufsize - 1);
	buf2[bufsize - 1] = '\0';
	len = strlcpy(buf, buf2, bufsize / 2);
	if (len != bufsize - 1) {
		fprintf(stderr, "strlcpy: failed small buffer test (3a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	len = (bufsize / 2) - 1;
	if (memcmp(buf, buf2, len) != 0 || buf[len] != '\0') {
		fprintf(stderr, "strlcpy: failed small buffer test (3b)\n");
		failures++;
	}
	for (cp = buf + len + 1; cp < ep; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcpy: failed small buffer test (3c)\n");
			failures++;
			break;
		}
	}

	/* Test copying to a 1-byte buffer. */
	memset(buf, 'z', bufsize);
	len = strlcpy(buf, "abcd", 1);
	if (len != 4) {
		fprintf(stderr, "strlcpy: failed 1-byte buffer test (4a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	if (buf[0] != '\0') {
		fprintf(stderr, "strlcpy: failed 1-byte buffer test (4b)\n");
		failures++;
	}
	for (cp = buf + 1; cp < ep; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcpy: failed 1-byte buffer test (4c)\n");
			failures++;
			break;
		}
	}

	/*
	 * The following tests should result in SIGSEGV, however some
	 * systems may erroneously report SIGBUS.
	 * These tests assume that strlcpy() is signal-safe.
	 */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handler;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);

	/* Test copying to a NULL buffer with non-zero size. */
	got_signal = 0;
	if (sigsetjmp(jmpenv, 1) == 0) {
		len = strlcpy(NULL, "abcd", sizeof(buf));
		fprintf(stderr, "strlcpy: failed NULL dst test (5a), "
		    "expected signal %d, got len %zu\n", SIGSEGV, len);
		failures++;
	} else if (got_signal != SIGSEGV) {
		fprintf(stderr, "strlcpy: failed NULL dst test (5b), "
		    "expected signal %d, got %d\n", SIGSEGV, got_signal);
		failures++;
	}

	/* Test copying from a NULL src. */
	got_signal = 0;
	if (sigsetjmp(jmpenv, 1) == 0) {
		len = strlcpy(buf, NULL, sizeof(buf));
		fprintf(stderr, "strlcpy: failed NULL src test (6a), "
		    "expected signal %d, got len %zu\n", SIGSEGV, len);
		failures++;
	} else if (got_signal != SIGSEGV) {
		fprintf(stderr, "strlcpy: failed NULL src test (6b), "
		    "expected signal %d, got %d\n", SIGSEGV, got_signal);
		failures++;
	}

	return failures;
}
