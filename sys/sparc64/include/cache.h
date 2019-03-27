/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
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
 *	from: @(#)cache.h	8.1 (Berkeley) 6/11/93
 *	from: NetBSD: cache.h,v 1.3 2000/08/01 00:28:02 eeh Exp
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CACHE_H_
#define	_MACHINE_CACHE_H_

#define	DCACHE_COLOR_BITS	(1)
#define	DCACHE_COLORS		(1 << DCACHE_COLOR_BITS)
#define	DCACHE_COLOR_MASK	(DCACHE_COLORS - 1)
#define	DCACHE_COLOR(va)	(((va) >> PAGE_SHIFT) & DCACHE_COLOR_MASK)
#define	DCACHE_OTHER_COLOR(color)					\
	((color) ^ DCACHE_COLOR_BITS)

#define	DC_TAG_SHIFT	2
#define	DC_VALID_SHIFT	0

#define	DC_TAG_BITS	28
#define	DC_VALID_BITS	2

#define	DC_TAG_MASK	((1 << DC_TAG_BITS) - 1)
#define	DC_VALID_MASK	((1 << DC_VALID_BITS) - 1)

#define	IC_TAG_SHIFT	7
#define	IC_VALID_SHIFT	36

#define	IC_TAG_BITS	28
#define	IC_VALID_BITS	1

#define	IC_TAG_MASK	((1 << IC_TAG_BITS) - 1)
#define	IC_VALID_MASK	((1 << IC_VALID_BITS) - 1)

#ifndef LOCORE

/*
 * Cache control information
 */
struct cacheinfo {
	u_int	ic_size;		/* instruction cache */
	u_int	ic_assoc;
	u_int	ic_linesize;
	u_int	dc_size;		/* data cache */
	u_int	dc_assoc;
	u_int	dc_linesize;
	u_int	ec_size;		/* external cache info */
	u_int	ec_assoc;
	u_int	ec_linesize;
};

#ifdef _KERNEL

extern u_int dcache_color_ignore;

struct pcpu;

typedef void cache_enable_t(u_int cpu_impl);
typedef void cache_flush_t(void);
typedef void dcache_page_inval_t(vm_paddr_t pa);
typedef void icache_page_inval_t(vm_paddr_t pa);

void cache_init(struct pcpu *pcpu);

cache_enable_t cheetah_cache_enable;
cache_flush_t cheetah_cache_flush;
dcache_page_inval_t cheetah_dcache_page_inval;
icache_page_inval_t cheetah_icache_page_inval;

cache_enable_t spitfire_cache_enable;
cache_flush_t spitfire_cache_flush;
dcache_page_inval_t spitfire_dcache_page_inval;
icache_page_inval_t spitfire_icache_page_inval;

cache_enable_t zeus_cache_enable;
cache_flush_t zeus_cache_flush;
dcache_page_inval_t zeus_dcache_page_inval;
icache_page_inval_t zeus_icache_page_inval;

extern cache_enable_t *cache_enable;
extern cache_flush_t *cache_flush;
extern dcache_page_inval_t *dcache_page_inval;
extern icache_page_inval_t *icache_page_inval;

#endif /* KERNEL */

#endif /* !LOCORE */

#endif /* !_MACHINE_CACHE_H_ */
