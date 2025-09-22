/*	$OpenBSD: set_memory.h,v 1.5 2023/01/01 01:34:58 jsg Exp $	*/
/*
 * Copyright (c) 2013, 2014, 2015 Mark Kettenis
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

#ifndef _ASM_SET_MEMORY_H
#define _ASM_SET_MEMORY_H

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/atomic.h>

#include <sys/param.h>		/* for PAGE_SIZE on i386 */
#include <uvm/uvm_extern.h>

#include <machine/pmap.h>

#if defined(__amd64__) || defined(__i386__)

static inline int
set_pages_array_wb(struct vm_page **pages, int addrinarray)
{
	int i;

	for (i = 0; i < addrinarray; i++)
		atomic_clearbits_int(&pages[i]->pg_flags, PG_PMAP_WC);

	return 0;
}

static inline int
set_pages_array_wc(struct vm_page **pages, int addrinarray)
{
	int i;

	for (i = 0; i < addrinarray; i++)
		atomic_setbits_int(&pages[i]->pg_flags, PG_PMAP_WC);

	return 0;
}

static inline int
set_pages_array_uc(struct vm_page **pages, int addrinarray)
{
	/* XXX */
	return 0;
}

static inline int
set_pages_wb(struct vm_page *page, int numpages)
{
	struct vm_page *pg;
	paddr_t start = VM_PAGE_TO_PHYS(page);
	int i;

	for (i = 0; i < numpages; i++) {
		pg = PHYS_TO_VM_PAGE(start + (i * PAGE_SIZE));
		if (pg != NULL)
			atomic_clearbits_int(&pg->pg_flags, PG_PMAP_WC);
	}

	return 0;
}

static inline int
set_pages_uc(struct vm_page *page, int numpages)
{
	/* XXX */
	return 0;
}

#endif /* defined(__amd64__) || defined(__i386__) */

#endif
