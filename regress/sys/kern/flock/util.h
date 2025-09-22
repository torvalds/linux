/*	$OpenBSD: util.h,v 1.1 2018/11/06 18:11:11 anton Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
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

#define FAIL(test) do {							\
	if (test) {							\
		if (verbose)						\
			printf("%s: %d: FAIL (%s)\n",			\
			    __func__, __LINE__, #test);			\
		return -1;						\
	}								\
} while (0)

#define SUCCEED do {							\
	if (verbose)							\
		printf("SUCCEED\n");					\
	return 0;							\
} while (0)

struct test {
	int (*testfn)(int);	/* function to perform the test */
	int intr;		/* non-zero if the test interrupts a lock */
};

int		make_file(off_t size);
int		safe_waitpid(pid_t pid);
__dead void	usage(void);

extern int verbose;
