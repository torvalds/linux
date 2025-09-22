/*	$OpenBSD: cache.h,v 1.7 2024/03/29 21:11:32 miod Exp $	*/
/*	$NetBSD: cache.h,v 1.3 2000/08/01 00:28:02 eeh Exp $ */

/*
 * Copyright (c) 1996
 * 	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Aaron Brown and
 *	Harvard University.
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)cache.h	8.1 (Berkeley) 6/11/93
 */

/*
 * The spitfire has a 16K two-way set associative level-1 I$ and a separate
 * 16K level-1 D$.  The I$ can be invalidated using the FLUSH instructions,
 * so we don't really need to worry about it much.  The D$ is 16K write-through
 * direct mapped virtually addressed cache with two 16-byte sub-blocks per line.
 * The E$ is a 512KB-4MB direct mapped physically indexed physically tagged
 * cache.
 * Since the level-1 caches are write-through, they don't need flushing and can
 * be invalidated directly.
 *
 * The spitfire sees virtual addresses as:
 *
 *	struct cache_va {
 *		u_int64_t	:22,	(unused; we only have 40-bit addresses)
 *			cva_tag:28,	(tag ID)
 *			cva_line:9,	(cache line number)
 *			cva_byte:5;	(byte within line)
 *	};
 *
 * Since there is one bit of overlap between the page offset and the line index,
 * all we need to do is make sure that bit 14 of the va remains constant and we
 * have no aliasing problems.
 *
 * Let me try again.  Page size is 8K, cache size is 16K so if (va1&0x3fff !=
 * va2&0x3fff) we have a problem.  Bit 14 *must* be the same for all mappings
 * of a page to be cacheable in the D$.  (The I$ is 16K 2-way associative--each
 * bank is 8K.  No conflict there.)
 */

/*
 * Routines for dealing with the cache.
 */

/* The following are for D$ flushes and are in locore.s */
#define dcache_flush_page(pa) cacheinfo.c_dcache_flush_page(pa)
void 	us_dcache_flush_page(paddr_t);	/* flush page from D$ */
void 	us3_dcache_flush_page(paddr_t);	/* flush page from D$ */
void	no_dcache_flush_page(paddr_t);

/* The following flush a range from the D$ and I$ but not E$. */
void	cache_flush_virt(vaddr_t, vsize_t);

/*
 * Cache control information.
 */
struct cacheinfo {
	void	(*c_dcache_flush_page)(paddr_t);

	int 	ic_totalsize;		/* instruction cache */
	int 	ic_linesize;
	int 	dc_totalsize;		/* data cache */
	int 	dc_linesize;
	int	ec_totalsize;		/* external cache info */
	int	ec_linesize;
};
extern struct cacheinfo cacheinfo;
