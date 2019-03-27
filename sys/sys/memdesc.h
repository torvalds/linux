/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 EMC Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_MEMDESC_H_
#define	_SYS_MEMDESC_H_

struct bio;
struct bus_dma_segment;
struct uio;
struct mbuf;
union ccb;

/*
 * struct memdesc encapsulates various memory descriptors and provides
 * abstract access to them.
 */
struct memdesc {
	union {
		void			*md_vaddr;
		vm_paddr_t		md_paddr;
		struct bus_dma_segment	*md_list;
		struct bio		*md_bio;
		struct uio		*md_uio;
		struct mbuf		*md_mbuf;
		union ccb		*md_ccb;
	} u;
	size_t		md_opaque;	/* type specific data. */
	uint32_t	md_type;	/* Type of memory. */
};

#define	MEMDESC_VADDR	1	/* Contiguous virtual address. */
#define	MEMDESC_PADDR	2	/* Contiguous physical address. */
#define	MEMDESC_VLIST	3	/* scatter/gather list of kva addresses. */
#define	MEMDESC_PLIST	4	/* scatter/gather list of physical addresses. */
#define	MEMDESC_BIO	5	/* Pointer to a bio (block io). */
#define	MEMDESC_UIO	6	/* Pointer to a uio (any io). */
#define	MEMDESC_MBUF	7	/* Pointer to a mbuf (network io). */
#define	MEMDESC_CCB	8	/* Cam control block. (scsi/ata io). */

static inline struct memdesc
memdesc_vaddr(void *vaddr, size_t len)
{
	struct memdesc mem;

	mem.u.md_vaddr = vaddr;
	mem.md_opaque = len;
	mem.md_type = MEMDESC_VADDR;

	return (mem);
}

static inline struct memdesc
memdesc_paddr(vm_paddr_t paddr, size_t len)
{
	struct memdesc mem;

	mem.u.md_paddr = paddr;
	mem.md_opaque = len;
	mem.md_type = MEMDESC_PADDR;

	return (mem);
}

static inline struct memdesc
memdesc_vlist(struct bus_dma_segment *vlist, int sglist_cnt)
{
	struct memdesc mem;

	mem.u.md_list = vlist;
	mem.md_opaque = sglist_cnt;
	mem.md_type = MEMDESC_VLIST;

	return (mem);
}

static inline struct memdesc
memdesc_plist(struct bus_dma_segment *plist, int sglist_cnt)
{
	struct memdesc mem;

	mem.u.md_list = plist;
	mem.md_opaque = sglist_cnt;
	mem.md_type = MEMDESC_PLIST;

	return (mem);
}

static inline struct memdesc
memdesc_bio(struct bio *bio)
{
	struct memdesc mem;

	mem.u.md_bio = bio;
	mem.md_type = MEMDESC_BIO;

	return (mem);
}

static inline struct memdesc
memdesc_uio(struct uio *uio)
{
	struct memdesc mem;

	mem.u.md_uio = uio;
	mem.md_type = MEMDESC_UIO;

	return (mem);
}

static inline struct memdesc
memdesc_mbuf(struct mbuf *mbuf)
{
	struct memdesc mem;

	mem.u.md_mbuf = mbuf;
	mem.md_type = MEMDESC_MBUF;

	return (mem);
}

static inline struct memdesc
memdesc_ccb(union ccb *ccb)
{
	struct memdesc mem;

	mem.u.md_ccb = ccb;
	mem.md_type = MEMDESC_CCB;

	return (mem);
}
#endif /* _SYS_MEMDESC_H_ */
