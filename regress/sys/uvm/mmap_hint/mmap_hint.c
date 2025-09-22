/*	$OpenBSD: mmap_hint.c,v 1.6 2021/12/13 16:56:50 deraadt Exp $	*/
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

#include <sys/param.h>	/* PAGE_SIZE PAGE_MASK */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/tree.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <err.h>
#include <sysexits.h>
#include <stdio.h>

#define MAX_HINT_DIST	(2UL * 1024 * 1024 * 1024)

int	errors = 0;

void *
mmap_hint(void *hint)
{
	void	*p;
	size_t	 delta;

	p = mmap(hint, 1, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		warn("mmap(addr=%p, len=1) failed", hint);
		errors++;
		return MAP_FAILED;
	} else if (p == NULL) {
		warnx("mmap(addr=%p, len=1) mapped at address 0", hint);
		errors++;
		return MAP_FAILED;
	} else
		fprintf(stderr, "    -> %p\n", p);

	if (hint > p)
		delta = hint - p;
	else
		delta = p - hint;

	if (hint != NULL && delta > MAX_HINT_DIST) {
		warnx("hinted allocation more than %#zx bytes away from hint: "
		    "hint %p, result %p", delta, hint, p);
		errors++;
		return MAP_FAILED;
	}
	return p;
}

int
main()
{
	void	*p, *hint;

	/* Check that unhinted allocation works properly. */
	hint = NULL;
	fprintf(stderr, "1: Checking hint %p mmap\n", hint);
	p = mmap_hint(hint);

	/* Check hinted allocation at top of map. */
	hint = (void *)((VM_MAXUSER_ADDRESS & ~PAGE_MASK) - PAGE_SIZE);
	fprintf(stderr, "2: Checking hint page below "
	    "VM_MAXUSER_ADDRESS %p mmap\n", hint);
	p = mmap_hint(hint);

	/* Check hinted allocation at bottom of map. */
	hint = (void *)VM_MIN_ADDRESS;
	fprintf(stderr, "3: Checking hint VM_MIN_ADDRESS %p mmap\n", hint);
	p = mmap_hint(hint);

	/*
	 * Check that hinted allocation doesn't overwrite existing allocation.
	 */
	if (p == MAP_FAILED) {
		fprintf(stderr, "4: Skipping test: required previous test "
		    "to succeed");
		goto skip4;
	}
	hint = p;
	fprintf(stderr, "4: Checking hint %p mmap, which is in use\n", hint);
	p = mmap_hint(hint);
	if (p == hint) {
		warnx("hinted allocation %p overwrites previous allocation %p",
		    hint, p);
		errors++;
	}

skip4:
	return errors;
}
