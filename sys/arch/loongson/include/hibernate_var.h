/*	$OpenBSD: hibernate_var.h,v 1.1 2013/06/02 21:46:04 pirofti Exp $	*/

/*
 * Copyright (c) 2013 Paul Irofti.
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

/* Loongson hibernate support definitions */

#define PAGE_MASK_4M ((256 * PAGE_SIZE) - 1)

#define PIGLET_PAGE_MASK ~((paddr_t)PAGE_MASK_4M)

/*
 * Steal hibernate pages right after the first page which is reserved
 * for the exception area.
 * */
#define HIBERNATE_STACK_PAGE	(PAGE_SIZE * 1)
#define HIBERNATE_INFLATE_PAGE	(PAGE_SIZE * 2)
#define HIBERNATE_COPY_PAGE	(PAGE_SIZE * 3)
#define HIBERNATE_HIBALLOC_PAGE	(PAGE_SIZE * 4)

#define HIBERNATE_RESERVED_PAGES	4

/* Use 4MB hibernation chunks */
#define HIBERNATE_CHUNK_SIZE		0x400000

#define HIBERNATE_CHUNK_TABLE_SIZE	0x100000
