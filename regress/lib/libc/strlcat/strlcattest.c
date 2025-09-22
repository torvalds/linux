/*	$OpenBSD: strlcattest.c,v 1.5 2021/09/27 19:33:58 millert Exp $ */

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
#include <setjmp.h>
#include <signal.h>
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
	char *buf, *cp, *ep;
	struct sigaction sa;
	size_t len, bufsize;
	volatile int failures = 0;

	bufsize = getpagesize(); /* trigger guard pages easily */
	buf = malloc(bufsize);
	if (buf == NULL) {
		fprintf(stderr, "unable to allocate memory\n");
		return 1;
	}
	memset(buf, 'z', bufsize);
	ep = buf + bufsize;

	/* Test appending to an unterminated string. */
	len = strlcat(buf, "abcd", bufsize);
	if (len != 4 + bufsize) {
		fprintf(stderr,
		    "strlcat: failed unterminated buffer test (1a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	for (cp = buf; cp < ep; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcat: failed unterminated buffer test (1b)\n");
			failures++;
			break;
		}
	}

	/* Test appending to a full string. */
	ep[-1] = '\0';
	len = strlcat(buf, "abcd", bufsize);
	if (len != 4 + bufsize - 1) {
		fprintf(stderr, "strlcat: failed full buffer test (2a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	for (cp = buf; cp < ep - 1; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcat: failed full buffer test (2b)\n");
			failures++;
			break;
		}
	}

	/* Test appending to an empty string. */
	ep[-1] = 'z';
	buf[0] = '\0';
	len = strlcat(buf, "abcd", bufsize);
	if (len != 4) {
		fprintf(stderr, "strlcat: failed empty buffer test (3a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	if (memcmp(buf, "abcd", sizeof("abcd")) != 0) {
		fprintf(stderr, "strlcat: failed empty buffer test (3b)\n");
		failures++;
	}
	for (cp = buf + len + 1; cp < ep; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcat: failed empty buffer test (3c)\n");
			failures++;
			break;
		}
	}

	/* Test appending to a NUL-terminated string. */
	memcpy(buf, "abcd", sizeof("abcd"));
	len = strlcat(buf, "efgh", bufsize);
	if (len != 8) {
		fprintf(stderr, "strlcat: failed empty buffer test (4a)\n");
		failures++;
	}
	/* Make sure we only wrote where expected. */
	if (memcmp(buf, "abcdefgh", sizeof("abcdefgh")) != 0) {
		fprintf(stderr, "strlcat: failed empty buffer test (4b)\n");
		failures++;
	}
	for (cp = buf + len + 1; cp < ep; cp++) {
		if (*cp != 'z') {
			fprintf(stderr,
			    "strlcat: failed empty buffer test (4c)\n");
			failures++;
			break;
		}
	}

        /*
         * The following tests should result in SIGSEGV, however some
         * systems may erroneously report SIGBUS.
         * These tests assume that strlcat() is signal-safe.
         */
        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = handler;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);

        /* Test copying to a NULL buffer with non-zero size. */
        got_signal = 0;
        if (sigsetjmp(jmpenv, 1) == 0) {
                len = strlcat(NULL, "abcd", sizeof(buf));
                fprintf(stderr, "strlcat: failed NULL dst test (5a), "
                    "expected signal %d, got len %zu\n", SIGSEGV, len);
                failures++;
        } else if (got_signal != SIGSEGV) {
                fprintf(stderr, "strlcat: failed NULL dst test (5b), "
                    "expected signal %d, got %d\n", SIGSEGV, got_signal);
                failures++;
        }

        /* Test copying from a NULL src. */
	memcpy(buf, "abcd", sizeof("abcd"));
        got_signal = 0;
        if (sigsetjmp(jmpenv, 1) == 0) {
                len = strlcat(buf, NULL, sizeof(buf));
                fprintf(stderr, "strlcat: failed NULL src test (6a), "
                    "expected signal %d, got len %zu\n", SIGSEGV, len);
                failures++;
        } else if (got_signal != SIGSEGV) {
                fprintf(stderr, "strlcat: failed NULL src test (6b), "
                    "expected signal %d, got %d\n", SIGSEGV, got_signal);
                failures++;
        }

	return failures;
}
