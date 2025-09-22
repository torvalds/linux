/*	$OpenBSD: atexit_test.c,v 1.10 2024/03/05 19:27:47 miod Exp $ */

/*
 * Copyright (c) 2002 Daniel Hartmeier
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/*
 * XXX Provide a prototype for aligned_alloc on pre-C11 compilers to prevent
 * inclusion of hidden/stdlib.h below to cause a missing prototype error.
 */
#if (__ISO_C_VISIBLE - 0) < 2011
void *aligned_alloc(size_t, size_t);
#endif

#include "include/namespace.h"
#include "hidden/stdlib.h"
#include "stdlib/atexit.h"

void	handle_first(void);
void	handle_middle(void);
void	handle_last(void);
void	handle_invalid(void *);
void	handle_cleanup(void);
void	handle_signal(int);

static int counter;

int
main(int argc, char *argv[])
{
	int i;

	if (argc != 2 || (strcmp(argv[1], "-valid") &&
	    strcmp(argv[1], "-invalid-atexit") &&
	    strcmp(argv[1], "-invalid-cleanup"))) {
		fprintf(stderr, "%s -valid/-invalid-atexit/-invalid-cleanup\n",
		    argv[0]);
		return (1);
	}
	fprintf(stderr, "main()\n");
	if (atexit(handle_last)) {
		perror("atexit(handle_last) failed");
		return (1);
	}
	for (i = 0; i < 65535; ++i) {
		if (atexit(handle_middle)) {
			perror("atexit(handle_middle) failed");
			return (1);
		}
	}
	if (atexit(handle_first)) {
		perror("atexit(handle_first) failed");
		return (1);
	}
	/* this is supposed to segfault */
	if (!strcmp(argv[1], "-invalid-atexit")) {
		signal(SIGSEGV, handle_signal);
		__atexit->fns[0].fn_ptr = handle_invalid;
	} else if (!strcmp(argv[1], "-invalid-cleanup")) {
		struct atexit *p = __atexit;

		signal(SIGSEGV, handle_signal);
		while (p != NULL && p->next != NULL)
			p = p->next;
		if (p == NULL)
			fprintf(stderr, "p == NULL, no page found\n");
		p->fns[0].fn_ptr = handle_invalid;
	}
	__atexit_register_cleanup(handle_cleanup);
	counter = 0;
	fprintf(stderr, "main() returns\n");
	return (0);
}

void
handle_first(void)
{
	fprintf(stderr, "handle_first() counter == %i\n", counter);
}

void
handle_middle(void)
{
	counter++;
}

void
handle_last(void)
{
	fprintf(stderr, "handle_last() counter == %i\n", counter);
}

void
handle_cleanup(void)
{
	fprintf(stderr, "handle_cleanup()\n");
}

void
handle_invalid(void *arg)
{
	fprintf(stderr, "handle_invalid() THIS SHOULD HAVE SEGFAULTED INSTEAD!\n");
}

void
handle_signal(int sigraised)
{
	switch (sigraised) {
	case SIGSEGV:
		dprintf(STDERR_FILENO, "SIGSEGV\n");
		exit(0);
	default:
		dprintf(STDERR_FILENO, "unexpected signal caught\n");
		exit(1);
	}
}
