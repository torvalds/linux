/*	$OpenBSD: mmap0.c,v 1.3 2021/10/24 21:24:20 deraadt Exp $	*/
/*
 * Copyright (c) 2011 Ariane van der Steldt <ariane@stack.nl>
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
#include <sys/mman.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sysexits.h>


/*
 * Mmap allocations with len=0 must fail with EINVAL.
 *
 * Posix says so and the vmmap implementation may not deal well with them
 * either.
 */
int
main()
{
	void	*ptr;
	int	 errors = 0;
	int	 fd;

	fd = open("/dev/zero", O_RDWR);
	if (fd == -1)
		err(EX_OSERR, "open");

	ptr = mmap(NULL, 0, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	if (ptr != MAP_FAILED) {
		warn("mmap(len=0, MAP_ANON) return %p, expected MAP_FAILED",
		    ptr);
		errors += 1;
	} else if (errno != EINVAL) {
		warn("mmap(len=0, MAP_ANON) errno %d, expected %d",
		    errno, EINVAL);
	}

	ptr = mmap(NULL, 0, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		warn("mmap(len=0, fd=\"/dev/zero\") returned %p, "
		    "expected MAP_FAILED", ptr);
		errors += 1;
	} else if (errno != EINVAL) {
		warn("mmap(len=0, fd=\"/dev/zero\") errno %d, expected %d",
		    errno, EINVAL);
	}

	return errors;
}
