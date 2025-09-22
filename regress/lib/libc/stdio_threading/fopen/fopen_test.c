/*	$OpenBSD: fopen_test.c,v 1.2 2015/01/20 04:41:01 krw Exp $	*/
/*
 * Copyright (c) 2008 Bret S. Lambert <blambert@openbsd.org>
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

#include <stdio.h>
#include <pthread.h>
#include "local.h"

int
writefn(void *cookie, const char *buf, int size)
{
	return 0;
}

void
fopen_thread(void *v)
{
	FILE *file;
	int i;

	for (i = 0; i < 4096; i++) {
		file = fwopen(&i, writefn);
		if (file != NULL) {
			fputc('0', file);
			pthread_yield();
			fclose(file);
		}
	}
}

int
main(void)
{
	run_threads(fopen_thread, NULL);
	exit(0);
}
