/*	$OpenBSD: realpathtest.c,v 1.13 2019/08/06 11:38:16 bluhm Exp $ */

/*
 * Copyright (c) 2019 Bob Beck <beck@openbsd.org>
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
#include <sys/stat.h>

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>

/*
 * Reference copy of userland realpath(3) implementation.
 * Assumed to be correct.
 */
extern char *realpath3(const char *pathname, char *resolved);

struct rp_compare {
	char * resolv2;
	char * resolv3;
	char * r2;
	char * r3;
	int e2;
	int e3;
};

static struct rp_compare
rpcompare(const char *pathname, char *resolv2,
    char *resolv3) {
	struct rp_compare ret = {0};

	errno = 0;
	ret.r2 = realpath(pathname, resolv2);
	ret.e2 = errno;
	ret.resolv2 = resolv2;
	errno = 0;
	ret.r3 = realpath3(pathname, resolv3);
	ret.e3 = errno;
	ret.resolv3 = resolv3;
	errno = 0;
	return ret;
}

#define RP_SHOULD_SUCCEED(A, B, C) do {					\
	struct rp_compare rc = rpcompare(A, B, C);			\
	if (rc.r2 == NULL)  {						\
		errno = rc.e2;						\
		err(1, "%s:%d - realpath of '%s' failed", __FILE__,	\
		    __LINE__, (A));					\
	}								\
	if (rc.r3 == NULL)  {						\
		errno = rc.e3;						\
		err(1, "%s:%d - realpath3 of '%s' failed", __FILE__,	\
		    __LINE__, (A));					\
	}								\
	if (strcmp(rc.r2, rc.r3) != 0)					\
		errx(1, "%s:%d - realpath of '%s' result '%s', "	\
		    "expected '%s", __FILE__, __LINE__, (A), rc.r2,	\
		    rc.r3);						\
} while(0);

#define RP_SHOULD_FAIL(A, B, C) do {					\
	struct rp_compare rc = rpcompare(A, B, C);			\
	if (rc.r2 != NULL)						\
		errx(1, "%s:%d - realpath of '%s' should have failed, "	\
		    "returned '%s'", __FILE__, __LINE__, (A), rc.r2);	\
	if (rc.r3 != NULL)						\
		errx(1, "%s:%d - realpath3 of '%s' should have failed, "\
		    "returned '%s'", __FILE__, __LINE__, (A), rc.r3);	\
	if (rc.e2 != rc.e3)						\
		errx(1, "%s:%d - realpath of '%s' errno %d does not "	\
		    "match realpath3 errno %d", __FILE__, __LINE__, (A),\
		    rc.e2, rc.e3 );					\
} while(0);

int
main(int argc, char *argv[])
{
	int i, j;
	char big[PATH_MAX+PATH_MAX];
	char r2[PATH_MAX];
	char r3[PATH_MAX];

	/* some basics */
	RP_SHOULD_FAIL(NULL, NULL, NULL);
	RP_SHOULD_FAIL("", NULL, NULL);
	RP_SHOULD_SUCCEED("/", NULL, NULL);
	RP_SHOULD_SUCCEED("//", NULL, NULL);
	RP_SHOULD_SUCCEED("/./", NULL, NULL);
	RP_SHOULD_SUCCEED("/./.", NULL, NULL);
	RP_SHOULD_SUCCEED("/./..", NULL, NULL);
	RP_SHOULD_SUCCEED("/../../", NULL, NULL);
	RP_SHOULD_SUCCEED("/tmp", NULL, NULL);
	RP_SHOULD_FAIL("/tmp/noreallydoesntexist", NULL, NULL);
	RP_SHOULD_FAIL("/tmp/noreallydoesntexist/stillnope", NULL, NULL);
	RP_SHOULD_SUCCEED("/bin", NULL, NULL);
	RP_SHOULD_FAIL("/bin/herp", NULL, NULL);
	RP_SHOULD_SUCCEED("////usr/bin", NULL, NULL);
	RP_SHOULD_FAIL("//.//usr/bin/.././../herp", r2, r3);
	RP_SHOULD_SUCCEED("/usr/include/machine/setjmp.h", r2, r3);
	RP_SHOULD_FAIL("//.//usr/bin/.././../herp/derp", r2, r3);
	RP_SHOULD_FAIL("/../.../usr/bin", r2, r3);
	RP_SHOULD_FAIL("/bsd/herp", r2, r3);

	/* relative paths */
	if (mkdir("hoobla", 0755) == -1) {
		if (errno != EEXIST)
			err(1, "mkdir");
	}
	RP_SHOULD_SUCCEED("hoobla", r2, r3);
	RP_SHOULD_FAIL("hoobla/porkrind", r2, r3);
	RP_SHOULD_FAIL("hoobla/porkrind/peepee", r2, r3);

	/* total size */
	memset(big, '/', PATH_MAX + 1);
	RP_SHOULD_FAIL(big, r2, r3);

	/* component size */
	memset(big, 'a', PATH_MAX + 1);
	big[0] = '/';
	big[NAME_MAX+1] = '\0';
	RP_SHOULD_FAIL(big, r2, r3);
	memset(big, 'a', PATH_MAX + 1);
	big[0] = '/';
	big[NAME_MAX+2] = '\0';
	RP_SHOULD_FAIL(big, r2, r3);

	/* long relatives back to root */
	for (i = 0; i < (PATH_MAX - 4); i += 3) {
		big[i] = '.';
		big[i+1] = '.';
		big[i+2] = '/';
	}
	i-= 3;
	strlcpy(big+i, "bsd", 4);
	RP_SHOULD_SUCCEED(big, r2, r3);

	for (i = 0; i < (PATH_MAX - 5); i += 3) {
		big[i] = '.';
		big[i+1] = '.';
		big[i+2] = '/';
	}
	i-= 3;
	strlcpy(big+i, "bsd/", 5);
	RP_SHOULD_FAIL(big, r2, r3);

	for (i = 0; i < (PATH_MAX - 5); i += 3) {
		big[i] = '.';
		big[i+1] = '.';
		big[i+2] = '/';
	}
	i-= 3;
	strlcpy(big+i, "derp", 5);
	RP_SHOULD_FAIL(big, r2, r3);

	for (i = 0; i < (PATH_MAX - 6); i += 3) {
		big[i] = '.';
		big[i+1] = '.';
		big[i+2] = '/';
	}
	i-= 3;
	strlcpy(big+i, "derp/", 6);
	RP_SHOULD_FAIL(big, r2, r3);

	for (i = 0; i < (PATH_MAX - 4); i += 3) {
		big[i] = '.';
		big[i+1] = '.';
		big[i+2] = '/';
	}
	i-= 3;
	strlcpy(big+i, "xxx", 4);
	RP_SHOULD_FAIL(big, r2, r3);

	for (i = 0; i < (PATH_MAX - 8); i += 3) {
		big[i] = '.';
		big[i+1] = '.';
		big[i+2] = '/';
	}
	i-= 3;
	strlcpy(big+i, "xxx/../", 8);
	RP_SHOULD_FAIL(big, r2, r3);

	return (0);
}
