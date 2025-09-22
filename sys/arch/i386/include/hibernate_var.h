/*	$OpenBSD: hibernate_var.h,v 1.16 2025/05/21 04:05:22 mlarkin Exp $	*/

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

#define HIB_PD_MASK		0xffc00000
#define PIGLET_PAGE_MASK 	(HIB_PD_MASK)

#define HIBERNATE_PD_PAGE	(PAGE_SIZE * 21)
#define HIBERNATE_PT_PAGE	(PAGE_SIZE * 22)
/* 2 pages for stack */
#define HIBERNATE_STACK_PAGE	(PAGE_SIZE * 24)
#define HIBERNATE_INFLATE_PAGE	(PAGE_SIZE * 25)
/* HIBERNATE_HIBALLOC_PAGE must be the last stolen page (see machdep.c) */
#define HIBERNATE_HIBALLOC_PAGE (PAGE_SIZE * 26)

/* Use 4MB hibernation chunks */
#define HIBERNATE_CHUNK_SIZE		0x400000

#define HIBERNATE_CHUNK_TABLE_SIZE	0x100000

#define HIBERNATE_STACK_OFFSET	0x0F00

#define atop_4m(x) ((x) >> PDSHIFT)
#define atop_4k(x) ((x) >> PAGE_SHIFT)
#define s4pde_4m(va) ((pt_entry_t *)HIBERNATE_PD_PAGE + atop_4m(va))
#define s4pde_4k(va) ((pt_entry_t *)HIBERNATE_PD_PAGE + atop_4k(va))
#define s4pte_4k(va) ((pt_entry_t *)HIBERNATE_PT_PAGE + atop_4k(va))

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
