/*	$OpenBSD: pmap.h,v 1.22 2025/05/20 12:46:52 jsg Exp $	*/
/*	$NetBSD: pmap.h,v 1.28 2006/04/10 23:12:11 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * OpenBSD/sh pmap:
 *	pmap.pm_ptp[512] ... 512 slot of page table page
 *	page table page contains 1024 PTEs. (PAGE_SIZE / sizeof(pt_entry_t))
 *	  | PTP 11bit | PTOFSET 10bit | PAGE_MASK 12bit |
 */

#ifndef _SH_PMAP_H_
#define	_SH_PMAP_H_

#include <sh/pte.h>

#ifdef _KERNEL
#include <sys/queue.h>

#define PMAP_CHECK_COPYIN	1

#define	PMAP_STEAL_MEMORY
#define	PMAP_GROWKERNEL

#define	__PMAP_PTP_N	512	/* # of page table page maps 2GB. */
typedef struct pmap {
	pt_entry_t **pm_ptp;
	int pm_asid;
	int pm_refcnt;
	struct pmap_statistics	pm_stats;	/* pmap statistics */
} *pmap_t;
extern struct pmap __pmap_kernel;

void pmap_bootstrap(void);
#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define	pmap_unuse_final(p)		do { /* nothing */ } while (0)
#define	pmap_remove_holes(vm)		do { /* nothing */ } while (0)
#define	pmap_kernel()			(&__pmap_kernel)
#define	pmap_deactivate(pmap)		do { /* nothing */ } while (0)
#define	pmap_update(pmap)		do { /* nothing */ } while (0)
#define	pmap_wired_count(pmap)		((pmap)->pm_stats.wired_count)
#define	pmap_resident_count(pmap)	((pmap)->pm_stats.resident_count)

/*
 * Avoid virtual cache aliases on SH4 CPUs
 * which have the virtually-indexed cache.
 */
#ifdef SH4
#define	PMAP_PREFER
vaddr_t	pmap_prefer_align(void);
vaddr_t	pmap_prefer_offset(vaddr_t);

/* pmap prefer alignment */
#define PMAP_PREFER_ALIGN()		pmap_prefer_align()
/* pmap prefer offset in alignment */
#define PMAP_PREFER_OFFSET(of)		pmap_prefer_offset(of)
#endif /* SH4 */

#define	__HAVE_PMAP_DIRECT
vaddr_t	pmap_map_direct(vm_page_t);
vm_page_t pmap_unmap_direct(vaddr_t);

/* MD pmap utils. */
pt_entry_t *__pmap_pte_lookup(pmap_t, vaddr_t);
pt_entry_t *__pmap_kpte_lookup(vaddr_t);
boolean_t __pmap_pte_load(pmap_t, vaddr_t, int);

#endif /* !_KERNEL */

#define	PG_PMAP_REF		PG_PMAP0
#define	PG_PMAP_MOD		PG_PMAP1

#ifndef _LOCORE
struct pv_entry;
struct vm_page_md {
	SLIST_HEAD(, pv_entry) pvh_head;
};

#define	VM_MDPAGE_INIT(pg)						\
do {									\
	struct vm_page_md *pvh = &(pg)->mdpage;				\
	SLIST_INIT(&pvh->pvh_head);					\
} while (/*CONSTCOND*/0)
#endif /* _LOCORE */

#endif /* !_SH_PMAP_H_ */
