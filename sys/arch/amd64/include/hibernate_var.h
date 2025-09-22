/*	$OpenBSD: hibernate_var.h,v 1.14 2025/09/13 13:43:47 mlarkin Exp $	*/

/*
 * Copyright (c) 2011 Mike Larkin <mlarkin@openbsd.org>
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

#define PIGLET_PAGE_MASK (L2_FRAME)

/*
 * PML4 table for resume
 */
#define HIBERNATE_PML4T		(PAGE_SIZE * 21)

/*
 * amd64 uses a PDPT to map the first 512GB phys mem plus one more
 * to map any ranges of phys mem past 512GB (if needed)
 */
#define HIBERNATE_PDPT_LOW	(PAGE_SIZE * 22)
#define HIBERNATE_PDPT_HI	(PAGE_SIZE * 23)

/*
 * amd64 uses one PD to map the first 1GB phys mem plus one more to map any
 * other 1GB ranges within the first 512GB phys, plus one more to map any
 * 1GB range in any subsequent 512GB range
 */
#define HIBERNATE_PD_LOW	(PAGE_SIZE * 24)
#define HIBERNATE_PD_LOW2	(PAGE_SIZE * 25)
#define HIBERNATE_PD_HI		(PAGE_SIZE * 26)

/*
 * amd64 uses one PT to map the first 2MB phys mem plus one more to map any
 * other 2MB range within the first 1GB, plus one more to map any 2MB range
 * in any subsequent 512GB range.
 */
#define HIBERNATE_PT_LOW	(PAGE_SIZE * 27)
#define HIBERNATE_PT_LOW2	(PAGE_SIZE * 28)
#define HIBERNATE_PT_HI		(PAGE_SIZE * 29)

/* 3 pages for stack */
#define HIBERNATE_STACK_PAGE	(PAGE_SIZE * 32)

#define HIBERNATE_INFLATE_PAGE	(PAGE_SIZE * 33)
/* HIBERNATE_HIBALLOC_PAGE must be the last stolen page (see machdep.c) */
#define HIBERNATE_HIBALLOC_PAGE	(PAGE_SIZE * 34)

/* Use 4MB hibernation chunks */
#define HIBERNATE_CHUNK_SIZE		0x400000

#define HIBERNATE_CHUNK_TABLE_SIZE	0x200000

#define HIBERNATE_STACK_OFFSET	0x0F00

/*
 * Minimum amount of memory for hibernate support. This is used in early boot
 * when deciding if we can preallocate the piglet. If the machine does not
 * have at least HIBERNATE_MIN_MEMORY RAM, we won't support hibernate. This
 * avoids late allocation issues due to fragmented memory and failure to
 * hibernate. We need to be able to allocate 16MB contiguous memory, aligned
 * to 2MB.
 *
 * The default minimum required memory is 512MB (1ULL << 29).
 */
#define HIBERNATE_MIN_MEMORY	(1ULL << 29)
