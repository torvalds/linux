/*	$OpenBSD: malloc_errs.c,v 1.6 2025/05/24 06:40:29 otto Exp $	*/
/*
 * Copyright (c) 2023 Otto Moerbeek <otto@drijf.net>
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

#include <sys/resource.h>
#include <sys/wait.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

/* Test erroneous use of API and heap that malloc should catch */

void
clearq(void *p)
{
	int i;
	void *q;

	/* Clear delayed free queue */
	for (i = 0; i < 400; i++) {
		q = malloc(100);
		free(q);
		if (p == q) {
			fprintf(stderr, "Re-use\n");
			abort();
		}
	}
}

/* test the test setup */
void
t0(void)
{
	abort();
}

/* double free >= page size */
void
t1(void)
{
	void *p = malloc(10000);
	free(p);
	free(p);
}

/* double free chunks are different, have a delayed free list */
void
t2(void)
{
	void *p, *q;
	int i;

	p = malloc(100);
	free(p);
	clearq(p);
	free(p);
}

/* double free without clearing delayed free list, needs F */
void
t3(void)
{
	void *p = malloc(100);
	free(p);
	free(p);
}

/* free without prior allocation */
void
t4(void)
{
	free((void*)1);
}

/* realloc of bogus pointer */
void
t5(void)
{
	realloc((void*)1, 10);
}

/* write after free for chunk */
void
t6(void)
{
	char *p = malloc(32);
	free(p);
	p[0] = ~p[0];
	clearq(NULL);
}

/* write after free large alloction */
void
t7(void)
{
	char *p, *q;
	int i;

	p = malloc(10000);
	free(p);
	p[0] = ~p[0];
	/* force re-use from the cache */
	for (i = 0; i < 100; i++) {
		q = malloc(10000);
		free(q);
	}
}

/* write after free for chunk, no clearing of delayed free queue */
void
t8(void)
{
	char *p, *q;

	p = malloc(32);
	q = malloc(32);
	free(p);
	p[0] = ~p[0];
	free(q);
}

/* canary check */
void
t9(void)
{
	char *p = malloc(100);
	p[100] = 0;
	free(p);
}

/* t10 is the same as t9 with different flags */

/* modified chunk pointer */
void
t11(void)
{
	char *p = malloc(100);
	free(p + 1);
}

/* free chunk pointer */
void
t12(void)
{
	char *p = malloc(16);
	free(p + 16);
}

/* freezero with wrong size */
void
t13(void)
{
	char *p = malloc(16);
	freezero(p, 17);
}

/* freezero with wrong size 2 */
void
t14(void)
{
	char *p = malloc(15);
	freezero(p, 16);
}

/* freezero with wrong size, pages */
void
t15(void)
{
	char *p = malloc(getpagesize());
	freezero(p, getpagesize() + 1);
}

/* recallocarray with wrong size */
void
t16(void)
{
	char *p = recallocarray(NULL, 0, 16, 1);
	char *q = recallocarray(p, 2, 3, 16);
}

/* recallocarray with wrong size 2 */
void
t17(void)
{
	char *p = recallocarray(NULL, 0, 15, 1);
	char *q = recallocarray(p, 2, 3, 15);
}

/* recallocarray with wrong size, pages */
void
t18(void)
{
	char *p = recallocarray(NULL, 0, 1, getpagesize());
	char *q = recallocarray(p, 2, 3, getpagesize());
}

/* recallocarray with wrong size, pages */
void
t19(void)
{
	char *p = recallocarray(NULL, 0, 1, 10 * getpagesize());
	char *q = recallocarray(p, 1, 2, 4 * getpagesize());
}

/* canary check pages */
void
t20(void)
{
	char *p = malloc(2*getpagesize() - 100);
	p[2*getpagesize() - 100] = 0;
	free(p);
}

/* out-of-bound write preceding chunk */
void
t22(void)
{
	int i, j;
	unsigned char *p;
	while (1) {
		uintptr_t address;
		p = malloc(32);
		address = (uintptr_t)(void *)p;
		/* we don't want to have a chunk on the last slot of a page */
		if (address / getpagesize() == (address + 32) / getpagesize())
			break;
		free(p);
	}
	p[32] = 0;
	for (i = 0; i < 10000; i++)
		p = malloc(32);
}

struct test {
	void (*test)(void);
	const char *flags;
};

struct test tests[] = {
	{ t0, "" },
	{ t1, "" },
	{ t2, "" },
	{ t3, "F" },
	{ t4, "" },
	{ t5, "" },
	{ t6, "J" },
	{ t7, "JJ" },
	{ t8, "FJ" },
	{ t9, "C" },
	{ t9, "JC" }, /* t10 re-uses code from t9 */
	{ t11, "" },
	{ t12, "" },
	{ t13, "" },
	{ t14, "C" },
	{ t15, "" },
	{ t16, "" },
	{ t17, "C" },
	{ t18, "" },
	{ t19, "" },
	{ t20, "C" },
	{ t8, "FJD" }, /* t21 re-uses code from t8 */
	{ t22, "J" },
	{ t22, "JD" }, /* t23 re-uses code from t22 */
};

int main(int argc, char *argv[])
{

	const struct rlimit lim = {0, 0};
	int i, status;
	pid_t pid;
	char num[10];
	char options[40];
	char const *env[2];

	if (argc == 2) {
		/* prevent coredumps */
		setrlimit(RLIMIT_CORE, &lim);
		i = atoi(argv[1]);
		fprintf(stderr, "Test %d\n", i);
		(*tests[i].test)();
		return 0;
	}

	for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		pid = fork();
		switch (pid) {
		case 0:
			snprintf(options, sizeof(options), "MALLOC_OPTIONS=us%s", tests[i].flags);
			snprintf(num, sizeof(num), "%d", i);
			env[0] = options;
			env[1] = NULL;
			execle(argv[0], argv[0], num, NULL, env);
			err(1, "exec");
		break;
		case -1:
			err(1, "fork");
		break;
		default: 
			if (waitpid(pid, &status, 0) == -1)
				err(1, "wait");
			if (!WIFSIGNALED(status) || 
			    WTERMSIG(status) != SIGABRT)
			errx(1, "Test %d did not abort", i);
		break;
		}
	}
	return 0;
}

