/*      $OpenBSD: atexit_test.c,v 1.1 2014/11/23 08:46:49 guenther Exp $       */

/*
 * Copyright (c) 2014 Philip Guenther <guenther@openbsd.org>
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

#include <dlfcn.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>

void *libaa, *libab;

#define	CALLBACK(name)	static void name(void) { printf("exe "#name"\n"); }

CALLBACK(cleanup1)
CALLBACK(cleanup2)
CALLBACK(cleanup3)

static void
cleanup_dlclose(void)
{
	printf("exe cleanup_dlclose begin\n");
	dlclose(libaa);
	dlclose(libab);
	printf("exe cleanup_dlclose end\n");
}

static void
aa(void)
{
	void (*func)(void) = dlsym(libaa, "aa");
	if (func == NULL)
		errx(1, "dlsym(libaa, aa): %s", dlerror());
	func();
}

static void
ab(void)
{
	void (*func)(void) = dlsym(libab, "ab");
	if (func == NULL)
		errx(1, "dlsym(libab, ab): %s", dlerror());
	func();
}

int
main(int argc, char **argv)
{
	int test;

	libaa = dlopen(LIBAA, RTLD_LAZY);
	if (libaa == NULL)
               	errx(1, "dlopen(%s, RTLD_LAZY): %s", LIBAA, dlerror());

	libab = dlopen(LIBAB, RTLD_LAZY);
	if (libab == NULL)
               	errx(1, "dlopen(%s, RTLD_LAZY): %s", LIBAB, dlerror());

	if (argc != 2)
		test = 0;
	else
		test = atoi(argv[1]);

	switch (test) {
	case 0:
		/* 1, aa, 2, ab, 3, then exit */
		atexit(cleanup1);
		aa();
		atexit(cleanup2);
		ab();
		atexit(cleanup3);
		exit(0);

	case 1:
		/* 1, aa, 2, ab, 3, then dlclose aa, then bb */
		atexit(cleanup1);
		aa();
		atexit(cleanup2);
		ab();
		atexit(cleanup3);
		dlclose(libaa);
		dlclose(libab);
		exit(0);

	case 2:
		/* 1, aa, cleanup_dlclose, ab, 3, then exit */
		atexit(cleanup1);
		aa();
		atexit(cleanup_dlclose);
		ab();
		atexit(cleanup3);
		exit(0);

	case 3:
		/* 1, aa, 2, ab, cleanup_dlclose, then exit */
		atexit(cleanup1);
		aa();
		atexit(cleanup2);
		ab();
		atexit(cleanup_dlclose);
		exit(0);

	}

	return (0);
}
