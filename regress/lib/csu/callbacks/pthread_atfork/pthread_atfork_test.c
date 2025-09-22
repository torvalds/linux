/*      $OpenBSD: pthread_atfork_test.c,v 1.2 2015/04/07 01:27:07 guenther Exp $       */

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

#include <sys/wait.h>
#include <dlfcn.h>
#include <err.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void *libaa, *libab;

FILE *otherf;

#define CALLBACK(file, name)					\
	static void name(void)					\
	{							\
		fprintf(file, "testp "#name"\n");		\
		fflush(file);					\
	}

#define	ATFORK_CALLBACKS(name)	\
	CALLBACK(stdout, name##_prepare) \
	CALLBACK(stdout, name##_parent) \
	CALLBACK(otherf, name##_child)

ATFORK_CALLBACKS(atfork1)
ATFORK_CALLBACKS(atfork2)
ATFORK_CALLBACKS(atfork3)

static void
atfork_dlclose(void)
{
	printf("exe atfork_dlclose begin\n");
	dlclose(libaa);
	dlclose(libab);
	printf("exe atfork_dlclose end\n");
	fflush(stdout);
}

static void
aa_atfork(void)
{
	void (*func)(FILE *) = dlsym(libaa, "aa_atfork");
	if (func == NULL)
		errx(1, "dlsym(libaa, aa_atfork): %s", dlerror());
	func(otherf);
}

static void
ab_atfork(void)
{
	void (*func)(FILE *) = dlsym(libab, "ab_atfork");
	if (func == NULL)
		errx(1, "dlsym(libab, ab_atfork): %s", dlerror());
	func(otherf);
}

#define	REGISTER(prep, parent, child)					\
	do {								\
		int _r = pthread_atfork(prep, parent, child);		\
		if (_r)							\
			errc(1, _r, "pthread_atfork(%s,%s,%s)",		\
			    #prep, #parent, #child);			\
	} while (0)

#define	REGISTER_ALL(name) \
	REGISTER(name##_prepare, name##_parent, name##_child)

int
main(int argc, char **argv)
{
	pid_t pid;
	int test, status;

	otherf = fdopen(3, "w");
	if (otherf == NULL)
		otherf = stderr;

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
		/* 1, aa, 2, ab, 3, then fork */
		REGISTER_ALL(atfork1);
		aa_atfork();
		REGISTER_ALL(atfork2);
		ab_atfork();
		REGISTER_ALL(atfork3);
		break;

	case 1:
		/* 1, aa, 2, ab, 3, then dlclose aa and bb, then fork */
		REGISTER_ALL(atfork1);
		aa_atfork();
		REGISTER_ALL(atfork2);
		ab_atfork();
		REGISTER_ALL(atfork3);
		dlclose(libaa);
		dlclose(libab);
		break;

	case 2:
		/* 1, aa, atfork_dlclose, ab, 3, then fork */
		REGISTER_ALL(atfork1);
		aa_atfork();
		REGISTER(atfork_dlclose, NULL, NULL);
		ab_atfork();
		REGISTER_ALL(atfork3);
		break;

	case 3:
		/* 1, aa, 2, ab, atfork_dlclose, then fork */
		REGISTER_ALL(atfork1);
		aa_atfork();
		REGISTER_ALL(atfork2);
		ab_atfork();
		REGISTER(atfork_dlclose, NULL, NULL);
		break;

	}

	fflush(stdout);
	fflush(otherf);
	pid = fork();

	waitpid(pid, &status, 0);
	return (0);
}
