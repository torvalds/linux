/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Device driver optimized for the Symbios/LSI 53C896/53C895A/53C1010
 *  PCI-SCSI controllers.
 *
 *  Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 *  This driver also supports the following Symbios/LSI PCI-SCSI chips:
 *	53C810A, 53C825A, 53C860, 53C875, 53C876, 53C885, 53C895,
 *	53C810,  53C815,  53C825 and the 53C1510D is 53C8XX mode.
 *
 *
 *  This driver for FreeBSD-CAM is derived from the Linux sym53c8xx driver.
 *  Copyright (C) 1998-1999  Gerard Roudier
 *
 *  The sym53c8xx driver is derived from the ncr53c8xx driver that had been
 *  a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 *  The original ncr driver has been written for 386bsd and FreeBSD by
 *          Wolfgang Stanglmeier        <wolf@cologne.de>
 *          Stefan Esser                <se@mi.Uni-Koeln.de>
 *  Copyright (C) 1994  Wolfgang Stanglmeier
 *
 *  The initialisation code, and part of the code that addresses
 *  FreeBSD-CAM services is based on the aic7xxx driver for FreeBSD-CAM
 *  written by Justin T. Gibbs.
 *
 *  Other major contributions:
 *
 *  NVRAM detection and reading.
 *  Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define SYM_DRIVER_NAME	"sym-1.6.5-20000902"

/* #define SYM_DEBUG_GENERIC_SUPPORT */

#include <sys/param.h>

/*
 *  Driver configuration options.
 */
#include "opt_sym.h"
#include <dev/sym/sym_conf.h>

#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <sys/proc.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/atomic.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#endif

#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

/* Short and quite clear integer types */
typedef int8_t    s8;
typedef int16_t   s16;
typedef	int32_t   s32;
typedef u_int8_t  u8;
typedef u_int16_t u16;
typedef	u_int32_t u32;

/*
 *  Driver definitions.
 */
#include <dev/sym/sym_defs.h>
#include <dev/sym/sym_fw.h>

/*
 *  IA32 architecture does not reorder STORES and prevents
 *  LOADS from passing STORES. It is called `program order'
 *  by Intel and allows device drivers to deal with memory
 *  ordering by only ensuring that the code is not reordered
 *  by the compiler when ordering is required.
 *  Other architectures implement a weaker ordering that
 *  requires memory barriers (and also IO barriers when they
 *  make sense) to be used.
 */
#if	defined	__i386__ || defined __amd64__
#define MEMORY_BARRIER()	do { ; } while(0)
#elif	defined	__powerpc__
#define MEMORY_BARRIER()	__asm__ volatile("eieio; sync" : : : "memory")
#elif	defined	__sparc64__
#define MEMORY_BARRIER()	__asm__ volatile("membar #Sync" : : : "memory")
#elif	defined	__arm__
#define MEMORY_BARRIER()	dmb()
#elif	defined	__aarch64__
#define MEMORY_BARRIER()	dmb(sy)
#elif	defined __riscv
#define MEMORY_BARRIER()	fence()
#else
#error	"Not supported platform"
#endif

/*
 *  A la VMS/CAM-3 queue management.
 */
typedef struct sym_quehead {
	struct sym_quehead *flink;	/* Forward  pointer */
	struct sym_quehead *blink;	/* Backward pointer */
} SYM_QUEHEAD;

#define sym_que_init(ptr) do { \
	(ptr)->flink = (ptr); (ptr)->blink = (ptr); \
} while (0)

static __inline void __sym_que_add(struct sym_quehead * new,
	struct sym_quehead * blink,
	struct sym_quehead * flink)
{
	flink->blink	= new;
	new->flink	= flink;
	new->blink	= blink;
	blink->flink	= new;
}

static __inline void __sym_que_del(struct sym_quehead * blink,
	struct sym_quehead * flink)
{
	flink->blink = blink;
	blink->flink = flink;
}

static __inline int sym_que_empty(struct sym_quehead *head)
{
	return head->flink == head;
}

static __inline void sym_que_splice(struct sym_quehead *list,
	struct sym_quehead *head)
{
	struct sym_quehead *first = list->flink;

	if (first != list) {
		struct sym_quehead *last = list->blink;
		struct sym_quehead *at   = head->flink;

		first->blink = head;
		head->flink  = first;

		last->flink = at;
		at->blink   = last;
	}
}

#define sym_que_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(size_t)(&((type *)0)->member)))

#define sym_insque(new, pos)		__sym_que_add(new, pos, (pos)->flink)

#define sym_remque(el)			__sym_que_del((el)->blink, (el)->flink)

#define sym_insque_head(new, head)	__sym_que_add(new, head, (head)->flink)

static __inline struct sym_quehead *sym_remque_head(struct sym_quehead *head)
{
	struct sym_quehead *elem = head->flink;

	if (elem != head)
		__sym_que_del(head, elem->flink);
	else
		elem = NULL;
	return elem;
}

#define sym_insque_tail(new, head)	__sym_que_add(new, (head)->blink, head)

/*
 *  This one may be useful.
 */
#define FOR_EACH_QUEUED_ELEMENT(head, qp) \
	for (qp = (head)->flink; qp != (head); qp = qp->flink)
/*
 *  FreeBSD does not offer our kind of queue in the CAM CCB.
 *  So, we have to cast.
 */
#define sym_qptr(p)	((struct sym_quehead *) (p))

/*
 *  Simple bitmap operations.
 */
#define sym_set_bit(p, n)	(((u32 *)(p))[(n)>>5] |=  (1<<((n)&0x1f)))
#define sym_clr_bit(p, n)	(((u32 *)(p))[(n)>>5] &= ~(1<<((n)&0x1f)))
#define sym_is_bit(p, n)	(((u32 *)(p))[(n)>>5] &   (1<<((n)&0x1f)))

/*
 *  Number of tasks per device we want to handle.
 */
#if	SYM_CONF_MAX_TAG_ORDER > 8
#error	"more than 256 tags per logical unit not allowed."
#endif
#define	SYM_CONF_MAX_TASK	(1<<SYM_CONF_MAX_TAG_ORDER)

/*
 *  Donnot use more tasks that we can handle.
 */
#ifndef	SYM_CONF_MAX_TAG
#define	SYM_CONF_MAX_TAG	SYM_CONF_MAX_TASK
#endif
#if	SYM_CONF_MAX_TAG > SYM_CONF_MAX_TASK
#undef	SYM_CONF_MAX_TAG
#define	SYM_CONF_MAX_TAG	SYM_CONF_MAX_TASK
#endif

/*
 *    This one means 'NO TAG for this job'
 */
#define NO_TAG	(256)

/*
 *  Number of SCSI targets.
 */
#if	SYM_CONF_MAX_TARGET > 16
#error	"more than 16 targets not allowed."
#endif

/*
 *  Number of logical units per target.
 */
#if	SYM_CONF_MAX_LUN > 64
#error	"more than 64 logical units per target not allowed."
#endif

/*
 *    Asynchronous pre-scaler (ns). Shall be 40 for
 *    the SCSI timings to be compliant.
 */
#define	SYM_CONF_MIN_ASYNC (40)

/*
 *  Number of entries in the START and DONE queues.
 *
 *  We limit to 1 PAGE in order to succeed allocation of
 *  these queues. Each entry is 8 bytes long (2 DWORDS).
 */
#ifdef	SYM_CONF_MAX_START
#define	SYM_CONF_MAX_QUEUE (SYM_CONF_MAX_START+2)
#else
#define	SYM_CONF_MAX_QUEUE (7*SYM_CONF_MAX_TASK+2)
#define	SYM_CONF_MAX_START (SYM_CONF_MAX_QUEUE-2)
#endif

#if	SYM_CONF_MAX_QUEUE > PAGE_SIZE/8
#undef	SYM_CONF_MAX_QUEUE
#define	SYM_CONF_MAX_QUEUE   PAGE_SIZE/8
#undef	SYM_CONF_MAX_START
#define	SYM_CONF_MAX_START (SYM_CONF_MAX_QUEUE-2)
#endif

/*
 *  For this one, we want a short name :-)
 */
#define MAX_QUEUE	SYM_CONF_MAX_QUEUE

/*
 *  Active debugging tags and verbosity.
 */
#define DEBUG_ALLOC	(0x0001)
#define DEBUG_PHASE	(0x0002)
#define DEBUG_POLL	(0x0004)
#define DEBUG_QUEUE	(0x0008)
#define DEBUG_RESULT	(0x0010)
#define DEBUG_SCATTER	(0x0020)
#define DEBUG_SCRIPT	(0x0040)
#define DEBUG_TINY	(0x0080)
#define DEBUG_TIMING	(0x0100)
#define DEBUG_NEGO	(0x0200)
#define DEBUG_TAGS	(0x0400)
#define DEBUG_POINTER	(0x0800)

#if 0
static int sym_debug = 0;
	#define DEBUG_FLAGS sym_debug
#else
/*	#define DEBUG_FLAGS (0x0631) */
	#define DEBUG_FLAGS (0x0000)

#endif
#define sym_verbose	(np->verbose)

/*
 *  Insert a delay in micro-seconds and milli-seconds.
 */
static void UDELAY(int us) { DELAY(us); }
static void MDELAY(int ms) { while (ms--) UDELAY(1000); }

/*
 *  Simple power of two buddy-like allocator.
 *
 *  This simple code is not intended to be fast, but to
 *  provide power of 2 aligned memory allocations.
 *  Since the SCRIPTS processor only supplies 8 bit arithmetic,
 *  this allocator allows simple and fast address calculations
 *  from the SCRIPTS code. In addition, cache line alignment
 *  is guaranteed for power of 2 cache line size.
 *
 *  This allocator has been developed for the Linux sym53c8xx
 *  driver, since this O/S does not provide naturally aligned
 *  allocations.
 *  It has the advantage of allowing the driver to use private
 *  pages of memory that will be useful if we ever need to deal
 *  with IO MMUs for PCI.
 */
#define MEMO_SHIFT	4	/* 16 bytes minimum memory chunk */
#define MEMO_PAGE_ORDER	0	/* 1 PAGE  maximum */
#if 0
#define MEMO_FREE_UNUSED	/* Free unused pages immediately */
#endif
#define MEMO_WARN	1
#define MEMO_CLUSTER_SHIFT	(PAGE_SHIFT+MEMO_PAGE_ORDER)
#define MEMO_CLUSTER_SIZE	(1UL << MEMO_CLUSTER_SHIFT)
#define MEMO_CLUSTER_MASK	(MEMO_CLUSTER_SIZE-1)

#define get_pages()		malloc(MEMO_CLUSTER_SIZE, M_DEVBUF, M_NOWAIT)
#define free_pages(p)		free((p), M_DEVBUF)

typedef u_long m_addr_t;	/* Enough bits to bit-hack addresses */

typedef struct m_link {		/* Link between free memory chunks */
	struct m_link *next;
} m_link_s;

typedef struct m_vtob {		/* Virtual to Bus address translation */
	struct m_vtob	*next;
	bus_dmamap_t	dmamap;	/* Map for this chunk */
	m_addr_t	vaddr;	/* Virtual address */
	m_addr_t	baddr;	/* Bus physical address */
} m_vtob_s;
/* Hash this stuff a bit to speed up translations */
#define VTOB_HASH_SHIFT		5
#define VTOB_HASH_SIZE		(1UL << VTOB_HASH_SHIFT)
#define VTOB_HASH_MASK		(VTOB_HASH_SIZE-1)
#define VTOB_HASH_CODE(m)	\
	((((m_addr_t) (m)) >> MEMO_CLUSTER_SHIFT) & VTOB_HASH_MASK)

typedef struct m_pool {		/* Memory pool of a given kind */
	bus_dma_tag_t	 dev_dmat;	/* Identifies the pool */
	bus_dma_tag_t	 dmat;		/* Tag for our fixed allocations */
	m_addr_t (*getp)(struct m_pool *);
#ifdef	MEMO_FREE_UNUSED
	void (*freep)(struct m_pool *, m_addr_t);
#endif
#define M_GETP()		mp->getp(mp)
#define M_FREEP(p)		mp->freep(mp, p)
	int nump;
	m_vtob_s *(vtob[VTOB_HASH_SIZE]);
	struct m_pool *next;
	struct m_link h[MEMO_CLUSTER_SHIFT - MEMO_SHIFT + 1];
} m_pool_s;

static void *___sym_malloc(m_pool_s *mp, int size)
{
	int i = 0;
	int s = (1 << MEMO_SHIFT);
	int j;
	m_addr_t a;
	m_link_s *h = mp->h;

	if (size > MEMO_CLUSTER_SIZE)
		return NULL;

	while (size > s) {
		s <<= 1;
		++i;
	}

	j = i;
	while (!h[j].next) {
		if (s == MEMO_CLUSTER_SIZE) {
			h[j].next = (m_link_s *) M_GETP();
			if (h[j].next)
				h[j].next->next = NULL;
			break;
		}
		++j;
		s <<= 1;
	}
	a = (m_addr_t) h[j].next;
	if (a) {
		h[j].next = h[j].next->next;
		while (j > i) {
			j -= 1;
			s >>= 1;
			h[j].next = (m_link_s *) (a+s);
			h[j].next->next = NULL;
		}
	}
#ifdef DEBUG
	printf("___sym_malloc(%d) = %p\n", size, (void *) a);
#endif
	return (void *) a;
}

static void ___sym_mfree(m_pool_s *mp, void *ptr, int size)
{
	int i = 0;
	int s = (1 << MEMO_SHIFT);
	m_link_s *q;
	m_addr_t a, b;
	m_link_s *h = mp->h;

#ifdef DEBUG
	printf("___sym_mfree(%p, %d)\n", ptr, size);
#endif

	if (size > MEMO_CLUSTER_SIZE)
		return;

	while (size > s) {
		s <<= 1;
		++i;
	}

	a = (m_addr_t) ptr;

	while (1) {
#ifdef MEMO_FREE_UNUSED
		if (s == MEMO_CLUSTER_SIZE) {
			M_FREEP(a);
			break;
		}
#endif
		b = a ^ s;
		q = &h[i];
		while (q->next && q->next != (m_link_s *) b) {
			q = q->next;
		}
		if (!q->next) {
			((m_link_s *) a)->next = h[i].next;
			h[i].next = (m_link_s *) a;
			break;
		}
		q->next = q->next->next;
		a = a & b;
		s <<= 1;
		++i;
	}
}

static void *__sym_calloc2(m_pool_s *mp, int size, char *name, int uflags)
{
	void *p;

	p = ___sym_malloc(mp, size);

	if (DEBUG_FLAGS & DEBUG_ALLOC)
		printf ("new %-10s[%4d] @%p.\n", name, size, p);

	if (p)
		bzero(p, size);
	else if (uflags & MEMO_WARN)
		printf ("__sym_calloc2: failed to allocate %s[%d]\n", name, size);

	return p;
}

#define __sym_calloc(mp, s, n)	__sym_calloc2(mp, s, n, MEMO_WARN)

static void __sym_mfree(m_pool_s *mp, void *ptr, int size, char *name)
{
	if (DEBUG_FLAGS & DEBUG_ALLOC)
		printf ("freeing %-10s[%4d] @%p.\n", name, size, ptr);

	___sym_mfree(mp, ptr, size);

}

/*
 * Default memory pool we donnot need to involve in DMA.
 */
/*
 * With the `bus dma abstraction', we use a separate pool for
 * memory we donnot need to involve in DMA.
 */
static m_addr_t ___mp0_getp(m_pool_s *mp)
{
	m_addr_t m = (m_addr_t) get_pages();
	if (m)
		++mp->nump;
	return m;
}

#ifdef	MEMO_FREE_UNUSED
static void ___mp0_freep(m_pool_s *mp, m_addr_t m)
{
	free_pages(m);
	--mp->nump;
}
#endif

#ifdef	MEMO_FREE_UNUSED
static m_pool_s mp0 = {0, 0, ___mp0_getp, ___mp0_freep};
#else
static m_pool_s mp0 = {0, 0, ___mp0_getp};
#endif

/*
 * Actual memory allocation routine for non-DMAed memory.
 */
static void *sym_calloc(int size, char *name)
{
	void *m;
	/* Lock */
	m = __sym_calloc(&mp0, size, name);
	/* Unlock */
	return m;
}

/*
 * Actual memory allocation routine for non-DMAed memory.
 */
static void sym_mfree(void *ptr, int size, char *name)
{
	/* Lock */
	__sym_mfree(&mp0, ptr, size, name);
	/* Unlock */
}

/*
 * DMAable pools.
 */
/*
 * With `bus dma abstraction', we use a separate pool per parent
 * BUS handle. A reverse table (hashed) is maintained for virtual
 * to BUS address translation.
 */
static void getbaddrcb(void *arg, bus_dma_segment_t *segs, int nseg __unused,
    int error)
{
	bus_addr_t *baddr;

	KASSERT(nseg == 1, ("%s: too many DMA segments (%d)", __func__, nseg));

	baddr = (bus_addr_t *)arg;
	if (error)
		*baddr = 0;
	else
		*baddr = segs->ds_addr;
}

static m_addr_t ___dma_getp(m_pool_s *mp)
{
	m_vtob_s *vbp;
	void *vaddr = NULL;
	bus_addr_t baddr = 0;

	vbp = __sym_calloc(&mp0, sizeof(*vbp), "VTOB");
	if (!vbp)
		goto out_err;

	if (bus_dmamem_alloc(mp->dmat, &vaddr,
			BUS_DMA_COHERENT | BUS_DMA_WAITOK, &vbp->dmamap))
		goto out_err;
	bus_dmamap_load(mp->dmat, vbp->dmamap, vaddr,
			MEMO_CLUSTER_SIZE, getbaddrcb, &baddr, BUS_DMA_NOWAIT);
	if (baddr) {
		int hc = VTOB_HASH_CODE(vaddr);
		vbp->vaddr = (m_addr_t) vaddr;
		vbp->baddr = (m_addr_t) baddr;
		vbp->next = mp->vtob[hc];
		mp->vtob[hc] = vbp;
		++mp->nump;
		return (m_addr_t) vaddr;
	}
out_err:
	if (baddr)
		bus_dmamap_unload(mp->dmat, vbp->dmamap);
	if (vaddr)
		bus_dmamem_free(mp->dmat, vaddr, vbp->dmamap);
	if (vbp)
		__sym_mfree(&mp0, vbp, sizeof(*vbp), "VTOB");
	return 0;
}

#ifdef	MEMO_FREE_UNUSED
static void ___dma_freep(m_pool_s *mp, m_addr_t m)
{
	m_vtob_s **vbpp, *vbp;
	int hc = VTOB_HASH_CODE(m);

	vbpp = &mp->vtob[hc];
	while (*vbpp && (*vbpp)->vaddr != m)
		vbpp = &(*vbpp)->next;
	if (*vbpp) {
		vbp = *vbpp;
		*vbpp = (*vbpp)->next;
		bus_dmamap_unload(mp->dmat, vbp->dmamap);
		bus_dmamem_free(mp->dmat, (void *) vbp->vaddr, vbp->dmamap);
		__sym_mfree(&mp0, vbp, sizeof(*vbp), "VTOB");
		--mp->nump;
	}
}
#endif

static __inline m_pool_s *___get_dma_pool(bus_dma_tag_t dev_dmat)
{
	m_pool_s *mp;
	for (mp = mp0.next; mp && mp->dev_dmat != dev_dmat; mp = mp->next);
	return mp;
}

static m_pool_s *___cre_dma_pool(bus_dma_tag_t dev_dmat)
{
	m_pool_s *mp = NULL;

	mp = __sym_calloc(&mp0, sizeof(*mp), "MPOOL");
	if (mp) {
		mp->dev_dmat = dev_dmat;
		if (!bus_dma_tag_create(dev_dmat, 1, MEMO_CLUSTER_SIZE,
			       BUS_SPACE_MAXADDR_32BIT,
			       BUS_SPACE_MAXADDR,
			       NULL, NULL, MEMO_CLUSTER_SIZE, 1,
			       MEMO_CLUSTER_SIZE, 0,
			       NULL, NULL, &mp->dmat)) {
			mp->getp = ___dma_getp;
#ifdef	MEMO_FREE_UNUSED
			mp->freep = ___dma_freep;
#endif
			mp->next = mp0.next;
			mp0.next = mp;
			return mp;
		}
	}
	if (mp)
		__sym_mfree(&mp0, mp, sizeof(*mp), "MPOOL");
	return NULL;
}

#ifdef	MEMO_FREE_UNUSED
static void ___del_dma_pool(m_pool_s *p)
{
	struct m_pool **pp = &mp0.next;

	while (*pp && *pp != p)
		pp = &(*pp)->next;
	if (*pp) {
		*pp = (*pp)->next;
		bus_dma_tag_destroy(p->dmat);
		__sym_mfree(&mp0, p, sizeof(*p), "MPOOL");
	}
}
#endif

static void *__sym_calloc_dma(bus_dma_tag_t dev_dmat, int size, char *name)
{
	struct m_pool *mp;
	void *m = NULL;

	/* Lock */
	mp = ___get_dma_pool(dev_dmat);
	if (!mp)
		mp = ___cre_dma_pool(dev_dmat);
	if (mp)
		m = __sym_calloc(mp, size, name);
#ifdef	MEMO_FREE_UNUSED
	if (mp && !mp->nump)
		___del_dma_pool(mp);
#endif
	/* Unlock */

	return m;
}

static void
__sym_mfree_dma(bus_dma_tag_t dev_dmat, void *m, int size, char *name)
{
	struct m_pool *mp;

	/* Lock */
	mp = ___get_dma_pool(dev_dmat);
	if (mp)
		__sym_mfree(mp, m, size, name);
#ifdef	MEMO_FREE_UNUSED
	if (mp && !mp->nump)
		___del_dma_pool(mp);
#endif
	/* Unlock */
}

static m_addr_t __vtobus(bus_dma_tag_t dev_dmat, void *m)
{
	m_pool_s *mp;
	int hc = VTOB_HASH_CODE(m);
	m_vtob_s *vp = NULL;
	m_addr_t a = ((m_addr_t) m) & ~MEMO_CLUSTER_MASK;

	/* Lock */
	mp = ___get_dma_pool(dev_dmat);
	if (mp) {
		vp = mp->vtob[hc];
		while (vp && (m_addr_t) vp->vaddr != a)
			vp = vp->next;
	}
	/* Unlock */
	if (!vp)
		panic("sym: VTOBUS FAILED!\n");
	return vp ? vp->baddr + (((m_addr_t) m) - a) : 0;
}

/*
 * Verbs for DMAable memory handling.
 * The _uvptv_ macro avoids a nasty warning about pointer to volatile
 * being discarded.
 */
#define _uvptv_(p) ((void *)((vm_offset_t)(p)))
#define _sym_calloc_dma(np, s, n)	__sym_calloc_dma(np->bus_dmat, s, n)
#define _sym_mfree_dma(np, p, s, n)	\
				__sym_mfree_dma(np->bus_dmat, _uvptv_(p), s, n)
#define sym_calloc_dma(s, n)		_sym_calloc_dma(np, s, n)
#define sym_mfree_dma(p, s, n)		_sym_mfree_dma(np, p, s, n)
#define _vtobus(np, p)			__vtobus(np->bus_dmat, _uvptv_(p))
#define vtobus(p)			_vtobus(np, p)

/*
 *  Print a buffer in hexadecimal format.
 */
static void sym_printb_hex (u_char *p, int n)
{
	while (n-- > 0)
		printf (" %x", *p++);
}

/*
 *  Same with a label at beginning and .\n at end.
 */
static void sym_printl_hex (char *label, u_char *p, int n)
{
	printf ("%s", label);
	sym_printb_hex (p, n);
	printf (".\n");
}

/*
 *  Return a string for SCSI BUS mode.
 */
static const char *sym_scsi_bus_mode(int mode)
{
	switch(mode) {
	case SMODE_HVD:	return "HVD";
	case SMODE_SE:	return "SE";
	case SMODE_LVD: return "LVD";
	}
	return "??";
}

/*
 *  Some poor and bogus sync table that refers to Tekram NVRAM layout.
 */
#ifdef SYM_CONF_NVRAM_SUPPORT
static const u_char Tekram_sync[16] =
	{25,31,37,43, 50,62,75,125, 12,15,18,21, 6,7,9,10};
#endif

/*
 *  Union of supported NVRAM formats.
 */
struct sym_nvram {
	int type;
#define	SYM_SYMBIOS_NVRAM	(1)
#define	SYM_TEKRAM_NVRAM	(2)
#ifdef	SYM_CONF_NVRAM_SUPPORT
	union {
		Symbios_nvram Symbios;
		Tekram_nvram Tekram;
	} data;
#endif
};

/*
 *  This one is hopefully useless, but actually useful. :-)
 */
#ifndef assert
#define	assert(expression) { \
	if (!(expression)) { \
		(void)panic( \
			"assertion \"%s\" failed: file \"%s\", line %d\n", \
			#expression, \
			__FILE__, __LINE__); \
	} \
}
#endif

/*
 *  Some provision for a possible big endian mode supported by
 *  Symbios chips (never seen, by the way).
 *  For now, this stuff does not deserve any comments. :)
 */
#define sym_offb(o)	(o)
#define sym_offw(o)	(o)

/*
 *  Some provision for support for BIG ENDIAN CPU.
 */
#define cpu_to_scr(dw)	htole32(dw)
#define scr_to_cpu(dw)	le32toh(dw)

/*
 *  Access to the chip IO registers and on-chip RAM.
 *  We use the `bus space' interface under FreeBSD-4 and
 *  later kernel versions.
 */
#if defined(SYM_CONF_IOMAPPED)

#define INB_OFF(o)	bus_read_1(np->io_res, (o))
#define INW_OFF(o)	bus_read_2(np->io_res, (o))
#define INL_OFF(o)	bus_read_4(np->io_res, (o))

#define OUTB_OFF(o, v)	bus_write_1(np->io_res, (o), (v))
#define OUTW_OFF(o, v)	bus_write_2(np->io_res, (o), (v))
#define OUTL_OFF(o, v)	bus_write_4(np->io_res, (o), (v))

#else	/* Memory mapped IO */

#define INB_OFF(o)	bus_read_1(np->mmio_res, (o))
#define INW_OFF(o)	bus_read_2(np->mmio_res, (o))
#define INL_OFF(o)	bus_read_4(np->mmio_res, (o))

#define OUTB_OFF(o, v)	bus_write_1(np->mmio_res, (o), (v))
#define OUTW_OFF(o, v)	bus_write_2(np->mmio_res, (o), (v))
#define OUTL_OFF(o, v)	bus_write_4(np->mmio_res, (o), (v))

#endif	/* SYM_CONF_IOMAPPED */

#define OUTRAM_OFF(o, a, l)	\
	bus_write_region_1(np->ram_res, (o), (a), (l))

/*
 *  Common definitions for both bus space and legacy IO methods.
 */
#define INB(r)		INB_OFF(offsetof(struct sym_reg,r))
#define INW(r)		INW_OFF(offsetof(struct sym_reg,r))
#define INL(r)		INL_OFF(offsetof(struct sym_reg,r))

#define OUTB(r, v)	OUTB_OFF(offsetof(struct sym_reg,r), (v))
#define OUTW(r, v)	OUTW_OFF(offsetof(struct sym_reg,r), (v))
#define OUTL(r, v)	OUTL_OFF(offsetof(struct sym_reg,r), (v))

#define OUTONB(r, m)	OUTB(r, INB(r) | (m))
#define OUTOFFB(r, m)	OUTB(r, INB(r) & ~(m))
#define OUTONW(r, m)	OUTW(r, INW(r) | (m))
#define OUTOFFW(r, m)	OUTW(r, INW(r) & ~(m))
#define OUTONL(r, m)	OUTL(r, INL(r) | (m))
#define OUTOFFL(r, m)	OUTL(r, INL(r) & ~(m))

/*
 *  We normally want the chip to have a consistent view
 *  of driver internal data structures when we restart it.
 *  Thus these macros.
 */
#define OUTL_DSP(v)				\
	do {					\
		MEMORY_BARRIER();		\
		OUTL (nc_dsp, (v));		\
	} while (0)

#define OUTONB_STD()				\
	do {					\
		MEMORY_BARRIER();		\
		OUTONB (nc_dcntl, (STD|NOCOM));	\
	} while (0)

/*
 *  Command control block states.
 */
#define HS_IDLE		(0)
#define HS_BUSY		(1)
#define HS_NEGOTIATE	(2)	/* sync/wide data transfer*/
#define HS_DISCONNECT	(3)	/* Disconnected by target */
#define HS_WAIT		(4)	/* waiting for resource	  */

#define HS_DONEMASK	(0x80)
#define HS_COMPLETE	(4|HS_DONEMASK)
#define HS_SEL_TIMEOUT	(5|HS_DONEMASK)	/* Selection timeout      */
#define HS_UNEXPECTED	(6|HS_DONEMASK)	/* Unexpected disconnect  */
#define HS_COMP_ERR	(7|HS_DONEMASK)	/* Completed with error	  */

/*
 *  Software Interrupt Codes
 */
#define	SIR_BAD_SCSI_STATUS	(1)
#define	SIR_SEL_ATN_NO_MSG_OUT	(2)
#define	SIR_MSG_RECEIVED	(3)
#define	SIR_MSG_WEIRD		(4)
#define	SIR_NEGO_FAILED		(5)
#define	SIR_NEGO_PROTO		(6)
#define	SIR_SCRIPT_STOPPED	(7)
#define	SIR_REJECT_TO_SEND	(8)
#define	SIR_SWIDE_OVERRUN	(9)
#define	SIR_SODL_UNDERRUN	(10)
#define	SIR_RESEL_NO_MSG_IN	(11)
#define	SIR_RESEL_NO_IDENTIFY	(12)
#define	SIR_RESEL_BAD_LUN	(13)
#define	SIR_TARGET_SELECTED	(14)
#define	SIR_RESEL_BAD_I_T_L	(15)
#define	SIR_RESEL_BAD_I_T_L_Q	(16)
#define	SIR_ABORT_SENT		(17)
#define	SIR_RESEL_ABORTED	(18)
#define	SIR_MSG_OUT_DONE	(19)
#define	SIR_COMPLETE_ERROR	(20)
#define	SIR_DATA_OVERRUN	(21)
#define	SIR_BAD_PHASE		(22)
#define	SIR_MAX			(22)

/*
 *  Extended error bit codes.
 *  xerr_status field of struct sym_ccb.
 */
#define	XE_EXTRA_DATA	(1)	/* unexpected data phase	 */
#define	XE_BAD_PHASE	(1<<1)	/* illegal phase (4/5)		 */
#define	XE_PARITY_ERR	(1<<2)	/* unrecovered SCSI parity error */
#define	XE_SODL_UNRUN	(1<<3)	/* ODD transfer in DATA OUT phase */
#define	XE_SWIDE_OVRUN	(1<<4)	/* ODD transfer in DATA IN phase */

/*
 *  Negotiation status.
 *  nego_status field of struct sym_ccb.
 */
#define NS_SYNC		(1)
#define NS_WIDE		(2)
#define NS_PPR		(3)

/*
 *  A CCB hashed table is used to retrieve CCB address
 *  from DSA value.
 */
#define CCB_HASH_SHIFT		8
#define CCB_HASH_SIZE		(1UL << CCB_HASH_SHIFT)
#define CCB_HASH_MASK		(CCB_HASH_SIZE-1)
#define CCB_HASH_CODE(dsa)	(((dsa) >> 9) & CCB_HASH_MASK)

/*
 *  Device flags.
 */
#define SYM_DISC_ENABLED	(1)
#define SYM_TAGS_ENABLED	(1<<1)
#define SYM_SCAN_BOOT_DISABLED	(1<<2)
#define SYM_SCAN_LUNS_DISABLED	(1<<3)

/*
 *  Host adapter miscellaneous flags.
 */
#define SYM_AVOID_BUS_RESET	(1)
#define SYM_SCAN_TARGETS_HILO	(1<<1)

/*
 *  Device quirks.
 *  Some devices, for example the CHEETAH 2 LVD, disconnects without
 *  saving the DATA POINTER then reselects and terminates the IO.
 *  On reselection, the automatic RESTORE DATA POINTER makes the
 *  CURRENT DATA POINTER not point at the end of the IO.
 *  This behaviour just breaks our calculation of the residual.
 *  For now, we just force an AUTO SAVE on disconnection and will
 *  fix that in a further driver version.
 */
#define SYM_QUIRK_AUTOSAVE 1

/*
 *  Misc.
 */
#define	SYM_LOCK()		mtx_lock(&np->mtx)
#define	SYM_LOCK_ASSERT(_what)	mtx_assert(&np->mtx, (_what))
#define	SYM_LOCK_DESTROY()	mtx_destroy(&np->mtx)
#define	SYM_LOCK_INIT()		mtx_init(&np->mtx, "sym_lock", NULL, MTX_DEF)
#define	SYM_LOCK_INITIALIZED()	mtx_initialized(&np->mtx)
#define	SYM_UNLOCK()		mtx_unlock(&np->mtx)

#define SYM_SNOOP_TIMEOUT (10000000)
#define SYM_PCI_IO	PCIR_BAR(0)
#define SYM_PCI_MMIO	PCIR_BAR(1)
#define SYM_PCI_RAM	PCIR_BAR(2)
#define SYM_PCI_RAM64	PCIR_BAR(3)

/*
 *  Back-pointer from the CAM CCB to our data structures.
 */
#define sym_hcb_ptr	spriv_ptr0
/* #define sym_ccb_ptr	spriv_ptr1 */

/*
 *  We mostly have to deal with pointers.
 *  Thus these typedef's.
 */
typedef struct sym_tcb *tcb_p;
typedef struct sym_lcb *lcb_p;
typedef struct sym_ccb *ccb_p;
typedef struct sym_hcb *hcb_p;

/*
 *  Gather negotiable parameters value
 */
struct sym_trans {
	u8 scsi_version;
	u8 spi_version;
	u8 period;
	u8 offset;
	u8 width;
	u8 options;	/* PPR options */
};

struct sym_tinfo {
	struct sym_trans current;
	struct sym_trans goal;
	struct sym_trans user;
};

#define BUS_8_BIT	MSG_EXT_WDTR_BUS_8_BIT
#define BUS_16_BIT	MSG_EXT_WDTR_BUS_16_BIT

/*
 *  Global TCB HEADER.
 *
 *  Due to lack of indirect addressing on earlier NCR chips,
 *  this substructure is copied from the TCB to a global
 *  address after selection.
 *  For SYMBIOS chips that support LOAD/STORE this copy is
 *  not needed and thus not performed.
 */
struct sym_tcbh {
	/*
	 *  Scripts bus addresses of LUN table accessed from scripts.
	 *  LUN #0 is a special case, since multi-lun devices are rare,
	 *  and we we want to speed-up the general case and not waste
	 *  resources.
	 */
	u32	luntbl_sa;	/* bus address of this table	*/
	u32	lun0_sa;	/* bus address of LCB #0	*/
	/*
	 *  Actual SYNC/WIDE IO registers value for this target.
	 *  'sval', 'wval' and 'uval' are read from SCRIPTS and
	 *  so have alignment constraints.
	 */
/*0*/	u_char	uval;		/* -> SCNTL4 register		*/
/*1*/	u_char	sval;		/* -> SXFER  io register	*/
/*2*/	u_char	filler1;
/*3*/	u_char	wval;		/* -> SCNTL3 io register	*/
};

/*
 *  Target Control Block
 */
struct sym_tcb {
	/*
	 *  TCB header.
	 *  Assumed at offset 0.
	 */
/*0*/	struct sym_tcbh head;

	/*
	 *  LUN table used by the SCRIPTS processor.
	 *  An array of bus addresses is used on reselection.
	 */
	u32	*luntbl;	/* LCBs bus address table	*/

	/*
	 *  LUN table used by the C code.
	 */
	lcb_p	lun0p;		/* LCB of LUN #0 (usual case)	*/
#if SYM_CONF_MAX_LUN > 1
	lcb_p	*lunmp;		/* Other LCBs [1..MAX_LUN]	*/
#endif

	/*
	 *  Bitmap that tells about LUNs that succeeded at least
	 *  1 IO and therefore assumed to be a real device.
	 *  Avoid useless allocation of the LCB structure.
	 */
	u32	lun_map[(SYM_CONF_MAX_LUN+31)/32];

	/*
	 *  Bitmap that tells about LUNs that haven't yet an LCB
	 *  allocated (not discovered or LCB allocation failed).
	 */
	u32	busy0_map[(SYM_CONF_MAX_LUN+31)/32];

	/*
	 *  Transfer capabilities (SIP)
	 */
	struct sym_tinfo tinfo;

	/*
	 * Keep track of the CCB used for the negotiation in order
	 * to ensure that only 1 negotiation is queued at a time.
	 */
	ccb_p   nego_cp;	/* CCB used for the nego		*/

	/*
	 *  Set when we want to reset the device.
	 */
	u_char	to_reset;

	/*
	 *  Other user settable limits and options.
	 *  These limits are read from the NVRAM if present.
	 */
	u_char	usrflags;
	u_short	usrtags;
};

/*
 *  Assert some alignments required by the chip.
 */
CTASSERT(((offsetof(struct sym_reg, nc_sxfer) ^
    offsetof(struct sym_tcb, head.sval)) &3) == 0);
CTASSERT(((offsetof(struct sym_reg, nc_scntl3) ^
    offsetof(struct sym_tcb, head.wval)) &3) == 0);

/*
 *  Global LCB HEADER.
 *
 *  Due to lack of indirect addressing on earlier NCR chips,
 *  this substructure is copied from the LCB to a global
 *  address after selection.
 *  For SYMBIOS chips that support LOAD/STORE this copy is
 *  not needed and thus not performed.
 */
struct sym_lcbh {
	/*
	 *  SCRIPTS address jumped by SCRIPTS on reselection.
	 *  For not probed logical units, this address points to
	 *  SCRIPTS that deal with bad LU handling (must be at
	 *  offset zero of the LCB for that reason).
	 */
/*0*/	u32	resel_sa;

	/*
	 *  Task (bus address of a CCB) read from SCRIPTS that points
	 *  to the unique ITL nexus allowed to be disconnected.
	 */
	u32	itl_task_sa;

	/*
	 *  Task table bus address (read from SCRIPTS).
	 */
	u32	itlq_tbl_sa;
};

/*
 *  Logical Unit Control Block
 */
struct sym_lcb {
	/*
	 *  TCB header.
	 *  Assumed at offset 0.
	 */
/*0*/	struct sym_lcbh head;

	/*
	 *  Task table read from SCRIPTS that contains pointers to
	 *  ITLQ nexuses. The bus address read from SCRIPTS is
	 *  inside the header.
	 */
	u32	*itlq_tbl;	/* Kernel virtual address	*/

	/*
	 *  Busy CCBs management.
	 */
	u_short	busy_itlq;	/* Number of busy tagged CCBs	*/
	u_short	busy_itl;	/* Number of busy untagged CCBs	*/

	/*
	 *  Circular tag allocation buffer.
	 */
	u_short	ia_tag;		/* Tag allocation index		*/
	u_short	if_tag;		/* Tag release index		*/
	u_char	*cb_tags;	/* Circular tags buffer		*/

	/*
	 *  Set when we want to clear all tasks.
	 */
	u_char to_clear;

	/*
	 *  Capabilities.
	 */
	u_char	user_flags;
	u_char	current_flags;
};

/*
 *  Action from SCRIPTS on a task.
 *  Is part of the CCB, but is also used separately to plug
 *  error handling action to perform from SCRIPTS.
 */
struct sym_actscr {
	u32	start;		/* Jumped by SCRIPTS after selection	*/
	u32	restart;	/* Jumped by SCRIPTS on relection	*/
};

/*
 *  Phase mismatch context.
 *
 *  It is part of the CCB and is used as parameters for the
 *  DATA pointer. We need two contexts to handle correctly the
 *  SAVED DATA POINTER.
 */
struct sym_pmc {
	struct	sym_tblmove sg;	/* Updated interrupted SG block	*/
	u32	ret;		/* SCRIPT return address	*/
};

/*
 *  LUN control block lookup.
 *  We use a direct pointer for LUN #0, and a table of
 *  pointers which is only allocated for devices that support
 *  LUN(s) > 0.
 */
#if SYM_CONF_MAX_LUN <= 1
#define sym_lp(tp, lun) (!lun) ? (tp)->lun0p : 0
#else
#define sym_lp(tp, lun) \
	(!lun) ? (tp)->lun0p : (tp)->lunmp ? (tp)->lunmp[(lun)] : 0
#endif

/*
 *  Status are used by the host and the script processor.
 *
 *  The last four bytes (status[4]) are copied to the
 *  scratchb register (declared as scr0..scr3) just after the
 *  select/reselect, and copied back just after disconnecting.
 *  Inside the script the XX_REG are used.
 */

/*
 *  Last four bytes (script)
 */
#define  QU_REG	scr0
#define  HS_REG	scr1
#define  HS_PRT	nc_scr1
#define  SS_REG	scr2
#define  SS_PRT	nc_scr2
#define  HF_REG	scr3
#define  HF_PRT	nc_scr3

/*
 *  Last four bytes (host)
 */
#define  actualquirks  phys.head.status[0]
#define  host_status   phys.head.status[1]
#define  ssss_status   phys.head.status[2]
#define  host_flags    phys.head.status[3]

/*
 *  Host flags
 */
#define HF_IN_PM0	1u
#define HF_IN_PM1	(1u<<1)
#define HF_ACT_PM	(1u<<2)
#define HF_DP_SAVED	(1u<<3)
#define HF_SENSE	(1u<<4)
#define HF_EXT_ERR	(1u<<5)
#define HF_DATA_IN	(1u<<6)
#ifdef SYM_CONF_IARB_SUPPORT
#define HF_HINT_IARB	(1u<<7)
#endif

/*
 *  Global CCB HEADER.
 *
 *  Due to lack of indirect addressing on earlier NCR chips,
 *  this substructure is copied from the ccb to a global
 *  address after selection (or reselection) and copied back
 *  before disconnect.
 *  For SYMBIOS chips that support LOAD/STORE this copy is
 *  not needed and thus not performed.
 */
struct sym_ccbh {
	/*
	 *  Start and restart SCRIPTS addresses (must be at 0).
	 */
/*0*/	struct sym_actscr go;

	/*
	 *  SCRIPTS jump address that deal with data pointers.
	 *  'savep' points to the position in the script responsible
	 *  for the actual transfer of data.
	 *  It's written on reception of a SAVE_DATA_POINTER message.
	 */
	u32	savep;		/* Jump address to saved data pointer	*/
	u32	lastp;		/* SCRIPTS address at end of data	*/
	u32	goalp;		/* Not accessed for now from SCRIPTS	*/

	/*
	 *  Status fields.
	 */
	u8	status[4];
};

/*
 *  Data Structure Block
 *
 *  During execution of a ccb by the script processor, the
 *  DSA (data structure address) register points to this
 *  substructure of the ccb.
 */
struct sym_dsb {
	/*
	 *  CCB header.
	 *  Also assumed at offset 0 of the sym_ccb structure.
	 */
/*0*/	struct sym_ccbh head;

	/*
	 *  Phase mismatch contexts.
	 *  We need two to handle correctly the SAVED DATA POINTER.
	 *  MUST BOTH BE AT OFFSET < 256, due to using 8 bit arithmetic
	 *  for address calculation from SCRIPTS.
	 */
	struct sym_pmc pm0;
	struct sym_pmc pm1;

	/*
	 *  Table data for Script
	 */
	struct sym_tblsel  select;
	struct sym_tblmove smsg;
	struct sym_tblmove smsg_ext;
	struct sym_tblmove cmd;
	struct sym_tblmove sense;
	struct sym_tblmove wresid;
	struct sym_tblmove data [SYM_CONF_MAX_SG];
};

/*
 *  Our Command Control Block
 */
struct sym_ccb {
	/*
	 *  This is the data structure which is pointed by the DSA
	 *  register when it is executed by the script processor.
	 *  It must be the first entry.
	 */
	struct sym_dsb phys;

	/*
	 *  Pointer to CAM ccb and related stuff.
	 */
	struct callout ch;	/* callout handle		*/
	union ccb *cam_ccb;	/* CAM scsiio ccb		*/
	u8	cdb_buf[16];	/* Copy of CDB			*/
	u8	*sns_bbuf;	/* Bounce buffer for sense data	*/
#define SYM_SNS_BBUF_LEN	sizeof(struct scsi_sense_data)
	int	data_len;	/* Total data length		*/
	int	segments;	/* Number of SG segments	*/

	/*
	 *  Miscellaneous status'.
	 */
	u_char	nego_status;	/* Negotiation status		*/
	u_char	xerr_status;	/* Extended error flags		*/
	u32	extra_bytes;	/* Extraneous bytes transferred	*/

	/*
	 *  Message areas.
	 *  We prepare a message to be sent after selection.
	 *  We may use a second one if the command is rescheduled
	 *  due to CHECK_CONDITION or COMMAND TERMINATED.
	 *  Contents are IDENTIFY and SIMPLE_TAG.
	 *  While negotiating sync or wide transfer,
	 *  a SDTR or WDTR message is appended.
	 */
	u_char	scsi_smsg [12];
	u_char	scsi_smsg2[12];

	/*
	 *  Auto request sense related fields.
	 */
	u_char	sensecmd[6];	/* Request Sense command	*/
	u_char	sv_scsi_status;	/* Saved SCSI status 		*/
	u_char	sv_xerr_status;	/* Saved extended status	*/
	int	sv_resid;	/* Saved residual		*/

	/*
	 *  Map for the DMA of user data.
	 */
	void		*arg;	/* Argument for some callback	*/
	bus_dmamap_t	dmamap;	/* DMA map for user data	*/
	u_char		dmamapped;
#define SYM_DMA_NONE	0
#define SYM_DMA_READ	1
#define SYM_DMA_WRITE	2
	/*
	 *  Other fields.
	 */
	u32	ccb_ba;		/* BUS address of this CCB	*/
	u_short	tag;		/* Tag for this transfer	*/
				/*  NO_TAG means no tag		*/
	u_char	target;
	u_char	lun;
	ccb_p	link_ccbh;	/* Host adapter CCB hash chain	*/
	SYM_QUEHEAD
		link_ccbq;	/* Link to free/busy CCB queue	*/
	u32	startp;		/* Initial data pointer		*/
	int	ext_sg;		/* Extreme data pointer, used	*/
	int	ext_ofs;	/*  to calculate the residual.	*/
	u_char	to_abort;	/* Want this IO to be aborted	*/
};

#define CCB_BA(cp,lbl)	(cp->ccb_ba + offsetof(struct sym_ccb, lbl))

/*
 *  Host Control Block
 */
struct sym_hcb {
	struct mtx	mtx;

	/*
	 *  Global headers.
	 *  Due to poorness of addressing capabilities, earlier
	 *  chips (810, 815, 825) copy part of the data structures
	 *  (CCB, TCB and LCB) in fixed areas.
	 */
#ifdef	SYM_CONF_GENERIC_SUPPORT
	struct sym_ccbh	ccb_head;
	struct sym_tcbh	tcb_head;
	struct sym_lcbh	lcb_head;
#endif
	/*
	 *  Idle task and invalid task actions and
	 *  their bus addresses.
	 */
	struct sym_actscr idletask, notask, bad_itl, bad_itlq;
	vm_offset_t idletask_ba, notask_ba, bad_itl_ba, bad_itlq_ba;

	/*
	 *  Dummy lun table to protect us against target
	 *  returning bad lun number on reselection.
	 */
	u32	*badluntbl;	/* Table physical address	*/
	u32	badlun_sa;	/* SCRIPT handler BUS address	*/

	/*
	 *  Bus address of this host control block.
	 */
	u32	hcb_ba;

	/*
	 *  Bit 32-63 of the on-chip RAM bus address in LE format.
	 *  The START_RAM64 script loads the MMRS and MMWS from this
	 *  field.
	 */
	u32	scr_ram_seg;

	/*
	 *  Chip and controller indentification.
	 */
	device_t device;

	/*
	 *  Initial value of some IO register bits.
	 *  These values are assumed to have been set by BIOS, and may
	 *  be used to probe adapter implementation differences.
	 */
	u_char	sv_scntl0, sv_scntl3, sv_dmode, sv_dcntl, sv_ctest3, sv_ctest4,
		sv_ctest5, sv_gpcntl, sv_stest2, sv_stest4, sv_scntl4,
		sv_stest1;

	/*
	 *  Actual initial value of IO register bits used by the
	 *  driver. They are loaded at initialisation according to
	 *  features that are to be enabled/disabled.
	 */
	u_char	rv_scntl0, rv_scntl3, rv_dmode, rv_dcntl, rv_ctest3, rv_ctest4,
		rv_ctest5, rv_stest2, rv_ccntl0, rv_ccntl1, rv_scntl4;

	/*
	 *  Target data.
	 */
#ifdef __amd64__
	struct sym_tcb	*target;
#else
	struct sym_tcb	target[SYM_CONF_MAX_TARGET];
#endif

	/*
	 *  Target control block bus address array used by the SCRIPT
	 *  on reselection.
	 */
	u32		*targtbl;
	u32		targtbl_ba;

	/*
	 *  CAM SIM information for this instance.
	 */
	struct		cam_sim  *sim;
	struct		cam_path *path;

	/*
	 *  Allocated hardware resources.
	 */
	struct resource	*irq_res;
	struct resource	*io_res;
	struct resource	*mmio_res;
	struct resource	*ram_res;
	int		ram_id;
	void *intr;

	/*
	 *  Bus stuff.
	 *
	 *  My understanding of PCI is that all agents must share the
	 *  same addressing range and model.
	 *  But some hardware architecture guys provide complex and
	 *  brain-deaded stuff that makes shit.
	 *  This driver only support PCI compliant implementations and
	 *  deals with part of the BUS stuff complexity only to fit O/S
	 *  requirements.
	 */

	/*
	 *  DMA stuff.
	 */
	bus_dma_tag_t	bus_dmat;	/* DMA tag from parent BUS	*/
	bus_dma_tag_t	data_dmat;	/* DMA tag for user data	*/
	/*
	 *  BUS addresses of the chip
	 */
	vm_offset_t	mmio_ba;	/* MMIO BUS address		*/
	int		mmio_ws;	/* MMIO Window size		*/

	vm_offset_t	ram_ba;		/* RAM BUS address		*/
	int		ram_ws;		/* RAM window size		*/

	/*
	 *  SCRIPTS virtual and physical bus addresses.
	 *  'script'  is loaded in the on-chip RAM if present.
	 *  'scripth' stays in main memory for all chips except the
	 *  53C895A, 53C896 and 53C1010 that provide 8K on-chip RAM.
	 */
	u_char		*scripta0;	/* Copies of script and scripth	*/
	u_char		*scriptb0;	/* Copies of script and scripth	*/
	vm_offset_t	scripta_ba;	/* Actual script and scripth	*/
	vm_offset_t	scriptb_ba;	/*  bus addresses.		*/
	vm_offset_t	scriptb0_ba;
	u_short		scripta_sz;	/* Actual size of script A	*/
	u_short		scriptb_sz;	/* Actual size of script B	*/

	/*
	 *  Bus addresses, setup and patch methods for
	 *  the selected firmware.
	 */
	struct sym_fwa_ba fwa_bas;	/* Useful SCRIPTA bus addresses	*/
	struct sym_fwb_ba fwb_bas;	/* Useful SCRIPTB bus addresses	*/
	void		(*fw_setup)(hcb_p np, const struct sym_fw *fw);
	void		(*fw_patch)(hcb_p np);
	const char	*fw_name;

	/*
	 *  General controller parameters and configuration.
	 */
	u_short	device_id;	/* PCI device id		*/
	u_char	revision_id;	/* PCI device revision id	*/
	u_int	features;	/* Chip features map		*/
	u_char	myaddr;		/* SCSI id of the adapter	*/
	u_char	maxburst;	/* log base 2 of dwords burst	*/
	u_char	maxwide;	/* Maximum transfer width	*/
	u_char	minsync;	/* Min sync period factor (ST)	*/
	u_char	maxsync;	/* Max sync period factor (ST)	*/
	u_char	maxoffs;	/* Max scsi offset        (ST)	*/
	u_char	minsync_dt;	/* Min sync period factor (DT)	*/
	u_char	maxsync_dt;	/* Max sync period factor (DT)	*/
	u_char	maxoffs_dt;	/* Max scsi offset        (DT)	*/
	u_char	multiplier;	/* Clock multiplier (1,2,4)	*/
	u_char	clock_divn;	/* Number of clock divisors	*/
	u32	clock_khz;	/* SCSI clock frequency in KHz	*/
	u32	pciclk_khz;	/* Estimated PCI clock  in KHz	*/
	/*
	 *  Start queue management.
	 *  It is filled up by the host processor and accessed by the
	 *  SCRIPTS processor in order to start SCSI commands.
	 */
	volatile		/* Prevent code optimizations	*/
	u32	*squeue;	/* Start queue virtual address	*/
	u32	squeue_ba;	/* Start queue BUS address	*/
	u_short	squeueput;	/* Next free slot of the queue	*/
	u_short	actccbs;	/* Number of allocated CCBs	*/

	/*
	 *  Command completion queue.
	 *  It is the same size as the start queue to avoid overflow.
	 */
	u_short	dqueueget;	/* Next position to scan	*/
	volatile		/* Prevent code optimizations	*/
	u32	*dqueue;	/* Completion (done) queue	*/
	u32	dqueue_ba;	/* Done queue BUS address	*/

	/*
	 *  Miscellaneous buffers accessed by the scripts-processor.
	 *  They shall be DWORD aligned, because they may be read or
	 *  written with a script command.
	 */
	u_char		msgout[8];	/* Buffer for MESSAGE OUT 	*/
	u_char		msgin [8];	/* Buffer for MESSAGE IN	*/
	u32		lastmsg;	/* Last SCSI message sent	*/
	u_char		scratch;	/* Scratch for SCSI receive	*/

	/*
	 *  Miscellaneous configuration and status parameters.
	 */
	u_char		usrflags;	/* Miscellaneous user flags	*/
	u_char		scsi_mode;	/* Current SCSI BUS mode	*/
	u_char		verbose;	/* Verbosity for this controller*/
	u32		cache;		/* Used for cache test at init.	*/

	/*
	 *  CCB lists and queue.
	 */
	ccb_p ccbh[CCB_HASH_SIZE];	/* CCB hashed by DSA value	*/
	SYM_QUEHEAD	free_ccbq;	/* Queue of available CCBs	*/
	SYM_QUEHEAD	busy_ccbq;	/* Queue of busy CCBs		*/

	/*
	 *  During error handling and/or recovery,
	 *  active CCBs that are to be completed with
	 *  error or requeued are moved from the busy_ccbq
	 *  to the comp_ccbq prior to completion.
	 */
	SYM_QUEHEAD	comp_ccbq;

	/*
	 *  CAM CCB pending queue.
	 */
	SYM_QUEHEAD	cam_ccbq;

	/*
	 *  IMMEDIATE ARBITRATION (IARB) control.
	 *
	 *  We keep track in 'last_cp' of the last CCB that has been
	 *  queued to the SCRIPTS processor and clear 'last_cp' when
	 *  this CCB completes. If last_cp is not zero at the moment
	 *  we queue a new CCB, we set a flag in 'last_cp' that is
	 *  used by the SCRIPTS as a hint for setting IARB.
	 *  We donnot set more than 'iarb_max' consecutive hints for
	 *  IARB in order to leave devices a chance to reselect.
	 *  By the way, any non zero value of 'iarb_max' is unfair. :)
	 */
#ifdef SYM_CONF_IARB_SUPPORT
	u_short		iarb_max;	/* Max. # consecutive IARB hints*/
	u_short		iarb_count;	/* Actual # of these hints	*/
	ccb_p		last_cp;
#endif

	/*
	 *  Command abort handling.
	 *  We need to synchronize tightly with the SCRIPTS
	 *  processor in order to handle things correctly.
	 */
	u_char		abrt_msg[4];	/* Message to send buffer	*/
	struct sym_tblmove abrt_tbl;	/* Table for the MOV of it 	*/
	struct sym_tblsel  abrt_sel;	/* Sync params for selection	*/
	u_char		istat_sem;	/* Tells the chip to stop (SEM)	*/
};

#define HCB_BA(np, lbl)	    (np->hcb_ba      + offsetof(struct sym_hcb, lbl))

/*
 *  Return the name of the controller.
 */
static __inline const char *sym_name(hcb_p np)
{
	return device_get_nameunit(np->device);
}

/*--------------------------------------------------------------------------*/
/*------------------------------ FIRMWARES ---------------------------------*/
/*--------------------------------------------------------------------------*/

/*
 *  This stuff will be moved to a separate source file when
 *  the driver will be broken into several source modules.
 */

/*
 *  Macros used for all firmwares.
 */
#define	SYM_GEN_A(s, label)	((short) offsetof(s, label)),
#define	SYM_GEN_B(s, label)	((short) offsetof(s, label)),
#define	PADDR_A(label)		SYM_GEN_PADDR_A(struct SYM_FWA_SCR, label)
#define	PADDR_B(label)		SYM_GEN_PADDR_B(struct SYM_FWB_SCR, label)

#ifdef	SYM_CONF_GENERIC_SUPPORT
/*
 *  Allocate firmware #1 script area.
 */
#define	SYM_FWA_SCR		sym_fw1a_scr
#define	SYM_FWB_SCR		sym_fw1b_scr
#include <dev/sym/sym_fw1.h>
static const struct sym_fwa_ofs sym_fw1a_ofs = {
	SYM_GEN_FW_A(struct SYM_FWA_SCR)
};
static const struct sym_fwb_ofs sym_fw1b_ofs = {
	SYM_GEN_FW_B(struct SYM_FWB_SCR)
};
#undef	SYM_FWA_SCR
#undef	SYM_FWB_SCR
#endif	/* SYM_CONF_GENERIC_SUPPORT */

/*
 *  Allocate firmware #2 script area.
 */
#define	SYM_FWA_SCR		sym_fw2a_scr
#define	SYM_FWB_SCR		sym_fw2b_scr
#include <dev/sym/sym_fw2.h>
static const struct sym_fwa_ofs sym_fw2a_ofs = {
	SYM_GEN_FW_A(struct SYM_FWA_SCR)
};
static const struct sym_fwb_ofs sym_fw2b_ofs = {
	SYM_GEN_FW_B(struct SYM_FWB_SCR)
	SYM_GEN_B(struct SYM_FWB_SCR, start64)
	SYM_GEN_B(struct SYM_FWB_SCR, pm_handle)
};
#undef	SYM_FWA_SCR
#undef	SYM_FWB_SCR

#undef	SYM_GEN_A
#undef	SYM_GEN_B
#undef	PADDR_A
#undef	PADDR_B

#ifdef	SYM_CONF_GENERIC_SUPPORT
/*
 *  Patch routine for firmware #1.
 */
static void
sym_fw1_patch(hcb_p np)
{
	struct sym_fw1a_scr *scripta0;
	struct sym_fw1b_scr *scriptb0;

	scripta0 = (struct sym_fw1a_scr *) np->scripta0;
	scriptb0 = (struct sym_fw1b_scr *) np->scriptb0;

	/*
	 *  Remove LED support if not needed.
	 */
	if (!(np->features & FE_LED0)) {
		scripta0->idle[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->reselected[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->start[0]	= cpu_to_scr(SCR_NO_OP);
	}

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *    If user does not want to use IMMEDIATE ARBITRATION
	 *    when we are reselected while attempting to arbitrate,
	 *    patch the SCRIPTS accordingly with a SCRIPT NO_OP.
	 */
	if (!SYM_CONF_SET_IARB_ON_ARB_LOST)
		scripta0->ungetjob[0] = cpu_to_scr(SCR_NO_OP);
#endif
	/*
	 *  Patch some data in SCRIPTS.
	 *  - start and done queue initial bus address.
	 *  - target bus address table bus address.
	 */
	scriptb0->startpos[0]	= cpu_to_scr(np->squeue_ba);
	scriptb0->done_pos[0]	= cpu_to_scr(np->dqueue_ba);
	scriptb0->targtbl[0]	= cpu_to_scr(np->targtbl_ba);
}
#endif	/* SYM_CONF_GENERIC_SUPPORT */

/*
 *  Patch routine for firmware #2.
 */
static void
sym_fw2_patch(hcb_p np)
{
	struct sym_fw2a_scr *scripta0;
	struct sym_fw2b_scr *scriptb0;

	scripta0 = (struct sym_fw2a_scr *) np->scripta0;
	scriptb0 = (struct sym_fw2b_scr *) np->scriptb0;

	/*
	 *  Remove LED support if not needed.
	 */
	if (!(np->features & FE_LED0)) {
		scripta0->idle[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->reselected[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->start[0]	= cpu_to_scr(SCR_NO_OP);
	}

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *    If user does not want to use IMMEDIATE ARBITRATION
	 *    when we are reselected while attempting to arbitrate,
	 *    patch the SCRIPTS accordingly with a SCRIPT NO_OP.
	 */
	if (!SYM_CONF_SET_IARB_ON_ARB_LOST)
		scripta0->ungetjob[0] = cpu_to_scr(SCR_NO_OP);
#endif
	/*
	 *  Patch some variable in SCRIPTS.
	 *  - start and done queue initial bus address.
	 *  - target bus address table bus address.
	 */
	scriptb0->startpos[0]	= cpu_to_scr(np->squeue_ba);
	scriptb0->done_pos[0]	= cpu_to_scr(np->dqueue_ba);
	scriptb0->targtbl[0]	= cpu_to_scr(np->targtbl_ba);

	/*
	 *  Remove the load of SCNTL4 on reselection if not a C10.
	 */
	if (!(np->features & FE_C10)) {
		scripta0->resel_scntl4[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->resel_scntl4[1] = cpu_to_scr(0);
	}

	/*
	 *  Remove a couple of work-arounds specific to C1010 if
	 *  they are not desirable. See `sym_fw2.h' for more details.
	 */
	if (!(np->device_id == PCI_ID_LSI53C1010_2 &&
	      np->revision_id < 0x1 &&
	      np->pciclk_khz < 60000)) {
		scripta0->datao_phase[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->datao_phase[1] = cpu_to_scr(0);
	}
	if (!(np->device_id == PCI_ID_LSI53C1010 &&
	      /* np->revision_id < 0xff */ 1)) {
		scripta0->sel_done[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->sel_done[1] = cpu_to_scr(0);
	}

	/*
	 *  Patch some other variables in SCRIPTS.
	 *  These ones are loaded by the SCRIPTS processor.
	 */
	scriptb0->pm0_data_addr[0] =
		cpu_to_scr(np->scripta_ba +
			   offsetof(struct sym_fw2a_scr, pm0_data));
	scriptb0->pm1_data_addr[0] =
		cpu_to_scr(np->scripta_ba +
			   offsetof(struct sym_fw2a_scr, pm1_data));
}

/*
 *  Fill the data area in scripts.
 *  To be done for all firmwares.
 */
static void
sym_fw_fill_data (u32 *in, u32 *out)
{
	int	i;

	for (i = 0; i < SYM_CONF_MAX_SG; i++) {
		*in++  = SCR_CHMOV_TBL ^ SCR_DATA_IN;
		*in++  = offsetof (struct sym_dsb, data[i]);
		*out++ = SCR_CHMOV_TBL ^ SCR_DATA_OUT;
		*out++ = offsetof (struct sym_dsb, data[i]);
	}
}

/*
 *  Setup useful script bus addresses.
 *  To be done for all firmwares.
 */
static void
sym_fw_setup_bus_addresses(hcb_p np, const struct sym_fw *fw)
{
	u32 *pa;
	const u_short *po;
	int i;

	/*
	 *  Build the bus address table for script A
	 *  from the script A offset table.
	 */
	po = (const u_short *) fw->a_ofs;
	pa = (u32 *) &np->fwa_bas;
	for (i = 0 ; i < sizeof(np->fwa_bas)/sizeof(u32) ; i++)
		pa[i] = np->scripta_ba + po[i];

	/*
	 *  Same for script B.
	 */
	po = (const u_short *) fw->b_ofs;
	pa = (u32 *) &np->fwb_bas;
	for (i = 0 ; i < sizeof(np->fwb_bas)/sizeof(u32) ; i++)
		pa[i] = np->scriptb_ba + po[i];
}

#ifdef	SYM_CONF_GENERIC_SUPPORT
/*
 *  Setup routine for firmware #1.
 */
static void
sym_fw1_setup(hcb_p np, const struct sym_fw *fw)
{
	struct sym_fw1a_scr *scripta0;

	scripta0 = (struct sym_fw1a_scr *) np->scripta0;

	/*
	 *  Fill variable parts in scripts.
	 */
	sym_fw_fill_data(scripta0->data_in, scripta0->data_out);

	/*
	 *  Setup bus addresses used from the C code..
	 */
	sym_fw_setup_bus_addresses(np, fw);
}
#endif	/* SYM_CONF_GENERIC_SUPPORT */

/*
 *  Setup routine for firmware #2.
 */
static void
sym_fw2_setup(hcb_p np, const struct sym_fw *fw)
{
	struct sym_fw2a_scr *scripta0;

	scripta0 = (struct sym_fw2a_scr *) np->scripta0;

	/*
	 *  Fill variable parts in scripts.
	 */
	sym_fw_fill_data(scripta0->data_in, scripta0->data_out);

	/*
	 *  Setup bus addresses used from the C code..
	 */
	sym_fw_setup_bus_addresses(np, fw);
}

/*
 *  Allocate firmware descriptors.
 */
#ifdef	SYM_CONF_GENERIC_SUPPORT
static const struct sym_fw sym_fw1 = SYM_FW_ENTRY(sym_fw1, "NCR-generic");
#endif	/* SYM_CONF_GENERIC_SUPPORT */
static const struct sym_fw sym_fw2 = SYM_FW_ENTRY(sym_fw2, "LOAD/STORE-based");

/*
 *  Find the most appropriate firmware for a chip.
 */
static const struct sym_fw *
sym_find_firmware(const struct sym_pci_chip *chip)
{
	if (chip->features & FE_LDSTR)
		return &sym_fw2;
#ifdef	SYM_CONF_GENERIC_SUPPORT
	else if (!(chip->features & (FE_PFEN|FE_NOPM|FE_DAC)))
		return &sym_fw1;
#endif
	else
		return NULL;
}

/*
 *  Bind a script to physical addresses.
 */
static void sym_fw_bind_script (hcb_p np, u32 *start, int len)
{
	u32 opcode, new, old, tmp1, tmp2;
	u32 *end, *cur;
	int relocs;

	cur = start;
	end = start + len/4;

	while (cur < end) {

		opcode = *cur;

		/*
		 *  If we forget to change the length
		 *  in scripts, a field will be
		 *  padded with 0. This is an illegal
		 *  command.
		 */
		if (opcode == 0) {
			printf ("%s: ERROR0 IN SCRIPT at %d.\n",
				sym_name(np), (int) (cur-start));
			MDELAY (10000);
			++cur;
			continue;
		}

		/*
		 *  We use the bogus value 0xf00ff00f ;-)
		 *  to reserve data area in SCRIPTS.
		 */
		if (opcode == SCR_DATA_ZERO) {
			*cur++ = 0;
			continue;
		}

		if (DEBUG_FLAGS & DEBUG_SCRIPT)
			printf ("%d:  <%x>\n", (int) (cur-start),
				(unsigned)opcode);

		/*
		 *  We don't have to decode ALL commands
		 */
		switch (opcode >> 28) {
		case 0xf:
			/*
			 *  LOAD / STORE DSA relative, don't relocate.
			 */
			relocs = 0;
			break;
		case 0xe:
			/*
			 *  LOAD / STORE absolute.
			 */
			relocs = 1;
			break;
		case 0xc:
			/*
			 *  COPY has TWO arguments.
			 */
			relocs = 2;
			tmp1 = cur[1];
			tmp2 = cur[2];
			if ((tmp1 ^ tmp2) & 3) {
				printf ("%s: ERROR1 IN SCRIPT at %d.\n",
					sym_name(np), (int) (cur-start));
				MDELAY (10000);
			}
			/*
			 *  If PREFETCH feature not enabled, remove
			 *  the NO FLUSH bit if present.
			 */
			if ((opcode & SCR_NO_FLUSH) &&
			    !(np->features & FE_PFEN)) {
				opcode = (opcode & ~SCR_NO_FLUSH);
			}
			break;
		case 0x0:
			/*
			 *  MOVE/CHMOV (absolute address)
			 */
			if (!(np->features & FE_WIDE))
				opcode = (opcode | OPC_MOVE);
			relocs = 1;
			break;
		case 0x1:
			/*
			 *  MOVE/CHMOV (table indirect)
			 */
			if (!(np->features & FE_WIDE))
				opcode = (opcode | OPC_MOVE);
			relocs = 0;
			break;
		case 0x8:
			/*
			 *  JUMP / CALL
			 *  dont't relocate if relative :-)
			 */
			if (opcode & 0x00800000)
				relocs = 0;
			else if ((opcode & 0xf8400000) == 0x80400000)/*JUMP64*/
				relocs = 2;
			else
				relocs = 1;
			break;
		case 0x4:
		case 0x5:
		case 0x6:
		case 0x7:
			relocs = 1;
			break;
		default:
			relocs = 0;
			break;
		}

		/*
		 *  Scriptify:) the opcode.
		 */
		*cur++ = cpu_to_scr(opcode);

		/*
		 *  If no relocation, assume 1 argument
		 *  and just scriptize:) it.
		 */
		if (!relocs) {
			*cur = cpu_to_scr(*cur);
			++cur;
			continue;
		}

		/*
		 *  Otherwise performs all needed relocations.
		 */
		while (relocs--) {
			old = *cur;

			switch (old & RELOC_MASK) {
			case RELOC_REGISTER:
				new = (old & ~RELOC_MASK) + np->mmio_ba;
				break;
			case RELOC_LABEL_A:
				new = (old & ~RELOC_MASK) + np->scripta_ba;
				break;
			case RELOC_LABEL_B:
				new = (old & ~RELOC_MASK) + np->scriptb_ba;
				break;
			case RELOC_SOFTC:
				new = (old & ~RELOC_MASK) + np->hcb_ba;
				break;
			case 0:
				/*
				 *  Don't relocate a 0 address.
				 *  They are mostly used for patched or
				 *  script self-modified areas.
				 */
				if (old == 0) {
					new = old;
					break;
				}
				/* fall through */
			default:
				new = 0;
				panic("sym_fw_bind_script: "
				      "weird relocation %x\n", old);
				break;
			}

			*cur++ = cpu_to_scr(new);
		}
	}
}

/*---------------------------------------------------------------------------*/
/*--------------------------- END OF FIRMWARES  -----------------------------*/
/*---------------------------------------------------------------------------*/

/*
 *  Function prototypes.
 */
static void sym_save_initial_setting (hcb_p np);
static int  sym_prepare_setting (hcb_p np, struct sym_nvram *nvram);
static int  sym_prepare_nego (hcb_p np, ccb_p cp, int nego, u_char *msgptr);
static void sym_put_start_queue (hcb_p np, ccb_p cp);
static void sym_chip_reset (hcb_p np);
static void sym_soft_reset (hcb_p np);
static void sym_start_reset (hcb_p np);
static int  sym_reset_scsi_bus (hcb_p np, int enab_int);
static int  sym_wakeup_done (hcb_p np);
static void sym_flush_busy_queue (hcb_p np, int cam_status);
static void sym_flush_comp_queue (hcb_p np, int cam_status);
static void sym_init (hcb_p np, int reason);
static int  sym_getsync(hcb_p np, u_char dt, u_char sfac, u_char *divp,
		        u_char *fakp);
static void sym_setsync (hcb_p np, ccb_p cp, u_char ofs, u_char per,
			 u_char div, u_char fak);
static void sym_setwide (hcb_p np, ccb_p cp, u_char wide);
static void sym_setpprot(hcb_p np, ccb_p cp, u_char dt, u_char ofs,
			 u_char per, u_char wide, u_char div, u_char fak);
static void sym_settrans(hcb_p np, ccb_p cp, u_char dt, u_char ofs,
			 u_char per, u_char wide, u_char div, u_char fak);
static void sym_log_hard_error (hcb_p np, u_short sist, u_char dstat);
static void sym_intr (void *arg);
static void sym_poll (struct cam_sim *sim);
static void sym_recover_scsi_int (hcb_p np, u_char hsts);
static void sym_int_sto (hcb_p np);
static void sym_int_udc (hcb_p np);
static void sym_int_sbmc (hcb_p np);
static void sym_int_par (hcb_p np, u_short sist);
static void sym_int_ma (hcb_p np);
static int  sym_dequeue_from_squeue(hcb_p np, int i, int target, int lun,
				    int task);
static void sym_sir_bad_scsi_status (hcb_p np, ccb_p cp);
static int  sym_clear_tasks (hcb_p np, int status, int targ, int lun, int task);
static void sym_sir_task_recovery (hcb_p np, int num);
static int  sym_evaluate_dp (hcb_p np, ccb_p cp, u32 scr, int *ofs);
static void sym_modify_dp(hcb_p np, ccb_p cp, int ofs);
static int  sym_compute_residual (hcb_p np, ccb_p cp);
static int  sym_show_msg (u_char * msg);
static void sym_print_msg (ccb_p cp, char *label, u_char *msg);
static void sym_sync_nego (hcb_p np, tcb_p tp, ccb_p cp);
static void sym_ppr_nego (hcb_p np, tcb_p tp, ccb_p cp);
static void sym_wide_nego (hcb_p np, tcb_p tp, ccb_p cp);
static void sym_nego_default (hcb_p np, tcb_p tp, ccb_p cp);
static void sym_nego_rejected (hcb_p np, tcb_p tp, ccb_p cp);
static void sym_int_sir (hcb_p np);
static void sym_free_ccb (hcb_p np, ccb_p cp);
static ccb_p sym_get_ccb (hcb_p np, u_char tn, u_char ln, u_char tag_order);
static ccb_p sym_alloc_ccb (hcb_p np);
static ccb_p sym_ccb_from_dsa (hcb_p np, u32 dsa);
static lcb_p sym_alloc_lcb (hcb_p np, u_char tn, u_char ln);
static void sym_alloc_lcb_tags (hcb_p np, u_char tn, u_char ln);
static int  sym_snooptest (hcb_p np);
static void sym_selectclock(hcb_p np, u_char scntl3);
static void sym_getclock (hcb_p np, int mult);
static int  sym_getpciclock (hcb_p np);
static void sym_complete_ok (hcb_p np, ccb_p cp);
static void sym_complete_error (hcb_p np, ccb_p cp);
static void sym_callout (void *arg);
static int  sym_abort_scsiio (hcb_p np, union ccb *ccb, int timed_out);
static void sym_reset_dev (hcb_p np, union ccb *ccb);
static void sym_action (struct cam_sim *sim, union ccb *ccb);
static int  sym_setup_cdb (hcb_p np, struct ccb_scsiio *csio, ccb_p cp);
static void sym_setup_data_and_start (hcb_p np, struct ccb_scsiio *csio,
				      ccb_p cp);
static int sym_fast_scatter_sg_physical(hcb_p np, ccb_p cp,
					bus_dma_segment_t *psegs, int nsegs);
static int sym_scatter_sg_physical (hcb_p np, ccb_p cp,
				    bus_dma_segment_t *psegs, int nsegs);
static void sym_action2 (struct cam_sim *sim, union ccb *ccb);
static void sym_update_trans(hcb_p np, struct sym_trans *tip,
			      struct ccb_trans_settings *cts);
static void sym_update_dflags(hcb_p np, u_char *flags,
			      struct ccb_trans_settings *cts);

static const struct sym_pci_chip *sym_find_pci_chip (device_t dev);
static int  sym_pci_probe (device_t dev);
static int  sym_pci_attach (device_t dev);

static void sym_pci_free (hcb_p np);
static int  sym_cam_attach (hcb_p np);
static void sym_cam_free (hcb_p np);

static void sym_nvram_setup_host (hcb_p np, struct sym_nvram *nvram);
static void sym_nvram_setup_target (hcb_p np, int targ, struct sym_nvram *nvp);
static int sym_read_nvram (hcb_p np, struct sym_nvram *nvp);

/*
 *  Print something which allows to retrieve the controller type,
 *  unit, target, lun concerned by a kernel message.
 */
static void PRINT_TARGET (hcb_p np, int target)
{
	printf ("%s:%d:", sym_name(np), target);
}

static void PRINT_LUN(hcb_p np, int target, int lun)
{
	printf ("%s:%d:%d:", sym_name(np), target, lun);
}

static void PRINT_ADDR (ccb_p cp)
{
	if (cp && cp->cam_ccb)
		xpt_print_path(cp->cam_ccb->ccb_h.path);
}

/*
 *  Take into account this ccb in the freeze count.
 */
static void sym_freeze_cam_ccb(union ccb *ccb)
{
	if (!(ccb->ccb_h.flags & CAM_DEV_QFRZDIS)) {
		if (!(ccb->ccb_h.status & CAM_DEV_QFRZN)) {
			ccb->ccb_h.status |= CAM_DEV_QFRZN;
			xpt_freeze_devq(ccb->ccb_h.path, 1);
		}
	}
}

/*
 *  Set the status field of a CAM CCB.
 */
static __inline void sym_set_cam_status(union ccb *ccb, cam_status status)
{
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	ccb->ccb_h.status |= status;
}

/*
 *  Get the status field of a CAM CCB.
 */
static __inline int sym_get_cam_status(union ccb *ccb)
{
	return ccb->ccb_h.status & CAM_STATUS_MASK;
}

/*
 *  Enqueue a CAM CCB.
 */
static void sym_enqueue_cam_ccb(ccb_p cp)
{
	hcb_p np;
	union ccb *ccb;

	ccb = cp->cam_ccb;
	np = (hcb_p) cp->arg;

	assert(!(ccb->ccb_h.status & CAM_SIM_QUEUED));
	ccb->ccb_h.status = CAM_REQ_INPROG;

	callout_reset_sbt(&cp->ch, SBT_1MS * ccb->ccb_h.timeout, 0, sym_callout,
	    (caddr_t)ccb, 0);
	ccb->ccb_h.status |= CAM_SIM_QUEUED;
	ccb->ccb_h.sym_hcb_ptr = np;

	sym_insque_tail(sym_qptr(&ccb->ccb_h.sim_links), &np->cam_ccbq);
}

/*
 *  Complete a pending CAM CCB.
 */

static void sym_xpt_done(hcb_p np, union ccb *ccb, ccb_p cp)
{

	SYM_LOCK_ASSERT(MA_OWNED);

	if (ccb->ccb_h.status & CAM_SIM_QUEUED) {
		callout_stop(&cp->ch);
		sym_remque(sym_qptr(&ccb->ccb_h.sim_links));
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.sym_hcb_ptr = NULL;
	}
	xpt_done(ccb);
}

static void sym_xpt_done2(hcb_p np, union ccb *ccb, int cam_status)
{

	SYM_LOCK_ASSERT(MA_OWNED);

	sym_set_cam_status(ccb, cam_status);
	xpt_done(ccb);
}

/*
 *  SYMBIOS chip clock divisor table.
 *
 *  Divisors are multiplied by 10,000,000 in order to make
 *  calculations more simple.
 */
#define _5M 5000000
static const u32 div_10M[] =
	{2*_5M, 3*_5M, 4*_5M, 6*_5M, 8*_5M, 12*_5M, 16*_5M};

/*
 *  SYMBIOS chips allow burst lengths of 2, 4, 8, 16, 32, 64,
 *  128 transfers. All chips support at least 16 transfers
 *  bursts. The 825A, 875 and 895 chips support bursts of up
 *  to 128 transfers and the 895A and 896 support bursts of up
 *  to 64 transfers. All other chips support up to 16
 *  transfers bursts.
 *
 *  For PCI 32 bit data transfers each transfer is a DWORD.
 *  It is a QUADWORD (8 bytes) for PCI 64 bit data transfers.
 *
 *  We use log base 2 (burst length) as internal code, with
 *  value 0 meaning "burst disabled".
 */

/*
 *  Burst length from burst code.
 */
#define burst_length(bc) (!(bc))? 0 : 1 << (bc)

/*
 *  Burst code from io register bits.
 */
#define burst_code(dmode, ctest4, ctest5) \
	(ctest4) & 0x80? 0 : (((dmode) & 0xc0) >> 6) + ((ctest5) & 0x04) + 1

/*
 *  Set initial io register bits from burst code.
 */
static __inline void sym_init_burst(hcb_p np, u_char bc)
{
	np->rv_ctest4	&= ~0x80;
	np->rv_dmode	&= ~(0x3 << 6);
	np->rv_ctest5	&= ~0x4;

	if (!bc) {
		np->rv_ctest4	|= 0x80;
	}
	else {
		--bc;
		np->rv_dmode	|= ((bc & 0x3) << 6);
		np->rv_ctest5	|= (bc & 0x4);
	}
}

/*
 * Print out the list of targets that have some flag disabled by user.
 */
static void sym_print_targets_flag(hcb_p np, int mask, char *msg)
{
	int cnt;
	int i;

	for (cnt = 0, i = 0 ; i < SYM_CONF_MAX_TARGET ; i++) {
		if (i == np->myaddr)
			continue;
		if (np->target[i].usrflags & mask) {
			if (!cnt++)
				printf("%s: %s disabled for targets",
					sym_name(np), msg);
			printf(" %d", i);
		}
	}
	if (cnt)
		printf(".\n");
}

/*
 *  Save initial settings of some IO registers.
 *  Assumed to have been set by BIOS.
 *  We cannot reset the chip prior to reading the
 *  IO registers, since informations will be lost.
 *  Since the SCRIPTS processor may be running, this
 *  is not safe on paper, but it seems to work quite
 *  well. :)
 */
static void sym_save_initial_setting (hcb_p np)
{
	np->sv_scntl0	= INB(nc_scntl0) & 0x0a;
	np->sv_scntl3	= INB(nc_scntl3) & 0x07;
	np->sv_dmode	= INB(nc_dmode)  & 0xce;
	np->sv_dcntl	= INB(nc_dcntl)  & 0xa8;
	np->sv_ctest3	= INB(nc_ctest3) & 0x01;
	np->sv_ctest4	= INB(nc_ctest4) & 0x80;
	np->sv_gpcntl	= INB(nc_gpcntl);
	np->sv_stest1	= INB(nc_stest1);
	np->sv_stest2	= INB(nc_stest2) & 0x20;
	np->sv_stest4	= INB(nc_stest4);
	if (np->features & FE_C10) {	/* Always large DMA fifo + ultra3 */
		np->sv_scntl4	= INB(nc_scntl4);
		np->sv_ctest5	= INB(nc_ctest5) & 0x04;
	}
	else
		np->sv_ctest5	= INB(nc_ctest5) & 0x24;
}

/*
 *  Prepare io register values used by sym_init() according
 *  to selected and supported features.
 */
static int sym_prepare_setting(hcb_p np, struct sym_nvram *nvram)
{
	u_char	burst_max;
	u32	period;
	int i;

	/*
	 *  Wide ?
	 */
	np->maxwide	= (np->features & FE_WIDE)? 1 : 0;

	/*
	 *  Get the frequency of the chip's clock.
	 */
	if	(np->features & FE_QUAD)
		np->multiplier	= 4;
	else if	(np->features & FE_DBLR)
		np->multiplier	= 2;
	else
		np->multiplier	= 1;

	np->clock_khz	= (np->features & FE_CLK80)? 80000 : 40000;
	np->clock_khz	*= np->multiplier;

	if (np->clock_khz != 40000)
		sym_getclock(np, np->multiplier);

	/*
	 * Divisor to be used for async (timer pre-scaler).
	 */
	i = np->clock_divn - 1;
	while (--i >= 0) {
		if (10ul * SYM_CONF_MIN_ASYNC * np->clock_khz > div_10M[i]) {
			++i;
			break;
		}
	}
	np->rv_scntl3 = i+1;

	/*
	 * The C1010 uses hardwired divisors for async.
	 * So, we just throw away, the async. divisor.:-)
	 */
	if (np->features & FE_C10)
		np->rv_scntl3 = 0;

	/*
	 * Minimum synchronous period factor supported by the chip.
	 * Btw, 'period' is in tenths of nanoseconds.
	 */
	period = howmany(4 * div_10M[0], np->clock_khz);
	if	(period <= 250)		np->minsync = 10;
	else if	(period <= 303)		np->minsync = 11;
	else if	(period <= 500)		np->minsync = 12;
	else				np->minsync = howmany(period, 40);

	/*
	 * Check against chip SCSI standard support (SCSI-2,ULTRA,ULTRA2).
	 */
	if	(np->minsync < 25 &&
		 !(np->features & (FE_ULTRA|FE_ULTRA2|FE_ULTRA3)))
		np->minsync = 25;
	else if	(np->minsync < 12 &&
		 !(np->features & (FE_ULTRA2|FE_ULTRA3)))
		np->minsync = 12;

	/*
	 * Maximum synchronous period factor supported by the chip.
	 */
	period = (11 * div_10M[np->clock_divn - 1]) / (4 * np->clock_khz);
	np->maxsync = period > 2540 ? 254 : period / 10;

	/*
	 * If chip is a C1010, guess the sync limits in DT mode.
	 */
	if ((np->features & (FE_C10|FE_ULTRA3)) == (FE_C10|FE_ULTRA3)) {
		if (np->clock_khz == 160000) {
			np->minsync_dt = 9;
			np->maxsync_dt = 50;
			np->maxoffs_dt = 62;
		}
	}

	/*
	 *  64 bit addressing  (895A/896/1010) ?
	 */
	if (np->features & FE_DAC)
#ifdef __LP64__
		np->rv_ccntl1	|= (XTIMOD | EXTIBMV);
#else
		np->rv_ccntl1	|= (DDAC);
#endif

	/*
	 *  Phase mismatch handled by SCRIPTS (895A/896/1010) ?
  	 */
	if (np->features & FE_NOPM)
		np->rv_ccntl0	|= (ENPMJ);

 	/*
	 *  C1010 Errata.
	 *  In dual channel mode, contention occurs if internal cycles
	 *  are used. Disable internal cycles.
	 */
	if (np->device_id == PCI_ID_LSI53C1010 &&
	    np->revision_id < 0x2)
		np->rv_ccntl0	|=  DILS;

	/*
	 *  Select burst length (dwords)
	 */
	burst_max	= SYM_SETUP_BURST_ORDER;
	if (burst_max == 255)
		burst_max = burst_code(np->sv_dmode, np->sv_ctest4,
				       np->sv_ctest5);
	if (burst_max > 7)
		burst_max = 7;
	if (burst_max > np->maxburst)
		burst_max = np->maxburst;

	/*
	 *  DEL 352 - 53C810 Rev x11 - Part Number 609-0392140 - ITEM 2.
	 *  This chip and the 860 Rev 1 may wrongly use PCI cache line
	 *  based transactions on LOAD/STORE instructions. So we have
	 *  to prevent these chips from using such PCI transactions in
	 *  this driver. The generic ncr driver that does not use
	 *  LOAD/STORE instructions does not need this work-around.
	 */
	if ((np->device_id == PCI_ID_SYM53C810 &&
	     np->revision_id >= 0x10 && np->revision_id <= 0x11) ||
	    (np->device_id == PCI_ID_SYM53C860 &&
	     np->revision_id <= 0x1))
		np->features &= ~(FE_WRIE|FE_ERL|FE_ERMP);

	/*
	 *  Select all supported special features.
	 *  If we are using on-board RAM for scripts, prefetch (PFEN)
	 *  does not help, but burst op fetch (BOF) does.
	 *  Disabling PFEN makes sure BOF will be used.
	 */
	if (np->features & FE_ERL)
		np->rv_dmode	|= ERL;		/* Enable Read Line */
	if (np->features & FE_BOF)
		np->rv_dmode	|= BOF;		/* Burst Opcode Fetch */
	if (np->features & FE_ERMP)
		np->rv_dmode	|= ERMP;	/* Enable Read Multiple */
#if 1
	if ((np->features & FE_PFEN) && !np->ram_ba)
#else
	if (np->features & FE_PFEN)
#endif
		np->rv_dcntl	|= PFEN;	/* Prefetch Enable */
	if (np->features & FE_CLSE)
		np->rv_dcntl	|= CLSE;	/* Cache Line Size Enable */
	if (np->features & FE_WRIE)
		np->rv_ctest3	|= WRIE;	/* Write and Invalidate */
	if (np->features & FE_DFS)
		np->rv_ctest5	|= DFS;		/* Dma Fifo Size */

	/*
	 *  Select some other
	 */
	if (SYM_SETUP_PCI_PARITY)
		np->rv_ctest4	|= MPEE; /* Master parity checking */
	if (SYM_SETUP_SCSI_PARITY)
		np->rv_scntl0	|= 0x0a; /*  full arb., ena parity, par->ATN  */

	/*
	 *  Get parity checking, host ID and verbose mode from NVRAM
	 */
	np->myaddr = 255;
	sym_nvram_setup_host (np, nvram);
#ifdef __sparc64__
	np->myaddr = OF_getscsinitid(np->device);
#endif

	/*
	 *  Get SCSI addr of host adapter (set by bios?).
	 */
	if (np->myaddr == 255) {
		np->myaddr = INB(nc_scid) & 0x07;
		if (!np->myaddr)
			np->myaddr = SYM_SETUP_HOST_ID;
	}

	/*
	 *  Prepare initial io register bits for burst length
	 */
	sym_init_burst(np, burst_max);

	/*
	 *  Set SCSI BUS mode.
	 *  - LVD capable chips (895/895A/896/1010) report the
	 *    current BUS mode through the STEST4 IO register.
	 *  - For previous generation chips (825/825A/875),
	 *    user has to tell us how to check against HVD,
	 *    since a 100% safe algorithm is not possible.
	 */
	np->scsi_mode = SMODE_SE;
	if (np->features & (FE_ULTRA2|FE_ULTRA3))
		np->scsi_mode = (np->sv_stest4 & SMODE);
	else if	(np->features & FE_DIFF) {
		if (SYM_SETUP_SCSI_DIFF == 1) {
			if (np->sv_scntl3) {
				if (np->sv_stest2 & 0x20)
					np->scsi_mode = SMODE_HVD;
			}
			else if (nvram->type == SYM_SYMBIOS_NVRAM) {
				if (!(INB(nc_gpreg) & 0x08))
					np->scsi_mode = SMODE_HVD;
			}
		}
		else if	(SYM_SETUP_SCSI_DIFF == 2)
			np->scsi_mode = SMODE_HVD;
	}
	if (np->scsi_mode == SMODE_HVD)
		np->rv_stest2 |= 0x20;

	/*
	 *  Set LED support from SCRIPTS.
	 *  Ignore this feature for boards known to use a
	 *  specific GPIO wiring and for the 895A, 896
	 *  and 1010 that drive the LED directly.
	 */
	if ((SYM_SETUP_SCSI_LED ||
	     (nvram->type == SYM_SYMBIOS_NVRAM ||
	      (nvram->type == SYM_TEKRAM_NVRAM &&
	       np->device_id == PCI_ID_SYM53C895))) &&
	    !(np->features & FE_LEDC) && !(np->sv_gpcntl & 0x01))
		np->features |= FE_LED0;

	/*
	 *  Set irq mode.
	 */
	switch(SYM_SETUP_IRQ_MODE & 3) {
	case 2:
		np->rv_dcntl	|= IRQM;
		break;
	case 1:
		np->rv_dcntl	|= (np->sv_dcntl & IRQM);
		break;
	default:
		break;
	}

	/*
	 *  Configure targets according to driver setup.
	 *  If NVRAM present get targets setup from NVRAM.
	 */
	for (i = 0 ; i < SYM_CONF_MAX_TARGET ; i++) {
		tcb_p tp = &np->target[i];

		tp->tinfo.user.scsi_version = tp->tinfo.current.scsi_version= 2;
		tp->tinfo.user.spi_version  = tp->tinfo.current.spi_version = 2;
		tp->tinfo.user.period = np->minsync;
		if (np->features & FE_ULTRA3)
			tp->tinfo.user.period = np->minsync_dt;
		tp->tinfo.user.offset = np->maxoffs;
		tp->tinfo.user.width  = np->maxwide ? BUS_16_BIT : BUS_8_BIT;
		tp->usrflags |= (SYM_DISC_ENABLED | SYM_TAGS_ENABLED);
		tp->usrtags = SYM_SETUP_MAX_TAG;

		sym_nvram_setup_target (np, i, nvram);

		/*
		 *  For now, guess PPR/DT support from the period
		 *  and BUS width.
		 */
		if (np->features & FE_ULTRA3) {
			if (tp->tinfo.user.period <= 9	&&
			    tp->tinfo.user.width == BUS_16_BIT) {
				tp->tinfo.user.options |= PPR_OPT_DT;
				tp->tinfo.user.offset   = np->maxoffs_dt;
				tp->tinfo.user.spi_version = 3;
			}
		}

		if (!tp->usrtags)
			tp->usrflags &= ~SYM_TAGS_ENABLED;
	}

	/*
	 *  Let user know about the settings.
	 */
	i = nvram->type;
	printf("%s: %s NVRAM, ID %d, Fast-%d, %s, %s\n", sym_name(np),
		i  == SYM_SYMBIOS_NVRAM ? "Symbios" :
		(i == SYM_TEKRAM_NVRAM  ? "Tekram" : "No"),
		np->myaddr,
		(np->features & FE_ULTRA3) ? 80 :
		(np->features & FE_ULTRA2) ? 40 :
		(np->features & FE_ULTRA)  ? 20 : 10,
		sym_scsi_bus_mode(np->scsi_mode),
		(np->rv_scntl0 & 0xa)	? "parity checking" : "NO parity");
	/*
	 *  Tell him more on demand.
	 */
	if (sym_verbose) {
		printf("%s: %s IRQ line driver%s\n",
			sym_name(np),
			np->rv_dcntl & IRQM ? "totem pole" : "open drain",
			np->ram_ba ? ", using on-chip SRAM" : "");
		printf("%s: using %s firmware.\n", sym_name(np), np->fw_name);
		if (np->features & FE_NOPM)
			printf("%s: handling phase mismatch from SCRIPTS.\n",
			       sym_name(np));
	}
	/*
	 *  And still more.
	 */
	if (sym_verbose > 1) {
		printf ("%s: initial SCNTL3/DMODE/DCNTL/CTEST3/4/5 = "
			"(hex) %02x/%02x/%02x/%02x/%02x/%02x\n",
			sym_name(np), np->sv_scntl3, np->sv_dmode, np->sv_dcntl,
			np->sv_ctest3, np->sv_ctest4, np->sv_ctest5);

		printf ("%s: final   SCNTL3/DMODE/DCNTL/CTEST3/4/5 = "
			"(hex) %02x/%02x/%02x/%02x/%02x/%02x\n",
			sym_name(np), np->rv_scntl3, np->rv_dmode, np->rv_dcntl,
			np->rv_ctest3, np->rv_ctest4, np->rv_ctest5);
	}
	/*
	 *  Let user be aware of targets that have some disable flags set.
	 */
	sym_print_targets_flag(np, SYM_SCAN_BOOT_DISABLED, "SCAN AT BOOT");
	if (sym_verbose)
		sym_print_targets_flag(np, SYM_SCAN_LUNS_DISABLED,
				       "SCAN FOR LUNS");

	return 0;
}

/*
 *  Prepare the next negotiation message if needed.
 *
 *  Fill in the part of message buffer that contains the
 *  negotiation and the nego_status field of the CCB.
 *  Returns the size of the message in bytes.
 */
static int sym_prepare_nego(hcb_p np, ccb_p cp, int nego, u_char *msgptr)
{
	tcb_p tp = &np->target[cp->target];
	int msglen = 0;

	/*
	 *  Early C1010 chips need a work-around for DT
	 *  data transfer to work.
	 */
	if (!(np->features & FE_U3EN))
		tp->tinfo.goal.options = 0;
	/*
	 *  negotiate using PPR ?
	 */
	if (tp->tinfo.goal.options & PPR_OPT_MASK)
		nego = NS_PPR;
	/*
	 *  negotiate wide transfers ?
	 */
	else if (tp->tinfo.current.width != tp->tinfo.goal.width)
		nego = NS_WIDE;
	/*
	 *  negotiate synchronous transfers?
	 */
	else if (tp->tinfo.current.period != tp->tinfo.goal.period ||
		 tp->tinfo.current.offset != tp->tinfo.goal.offset)
		nego = NS_SYNC;

	switch (nego) {
	case NS_SYNC:
		msgptr[msglen++] = M_EXTENDED;
		msgptr[msglen++] = 3;
		msgptr[msglen++] = M_X_SYNC_REQ;
		msgptr[msglen++] = tp->tinfo.goal.period;
		msgptr[msglen++] = tp->tinfo.goal.offset;
		break;
	case NS_WIDE:
		msgptr[msglen++] = M_EXTENDED;
		msgptr[msglen++] = 2;
		msgptr[msglen++] = M_X_WIDE_REQ;
		msgptr[msglen++] = tp->tinfo.goal.width;
		break;
	case NS_PPR:
		msgptr[msglen++] = M_EXTENDED;
		msgptr[msglen++] = 6;
		msgptr[msglen++] = M_X_PPR_REQ;
		msgptr[msglen++] = tp->tinfo.goal.period;
		msgptr[msglen++] = 0;
		msgptr[msglen++] = tp->tinfo.goal.offset;
		msgptr[msglen++] = tp->tinfo.goal.width;
		msgptr[msglen++] = tp->tinfo.goal.options & PPR_OPT_DT;
		break;
	}

	cp->nego_status = nego;

	if (nego) {
		tp->nego_cp = cp; /* Keep track a nego will be performed */
		if (DEBUG_FLAGS & DEBUG_NEGO) {
			sym_print_msg(cp, nego == NS_SYNC ? "sync msgout" :
					  nego == NS_WIDE ? "wide msgout" :
					  "ppr msgout", msgptr);
		}
	}

	return msglen;
}

/*
 *  Insert a job into the start queue.
 */
static void sym_put_start_queue(hcb_p np, ccb_p cp)
{
	u_short	qidx;

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *  If the previously queued CCB is not yet done,
	 *  set the IARB hint. The SCRIPTS will go with IARB
	 *  for this job when starting the previous one.
	 *  We leave devices a chance to win arbitration by
	 *  not using more than 'iarb_max' consecutive
	 *  immediate arbitrations.
	 */
	if (np->last_cp && np->iarb_count < np->iarb_max) {
		np->last_cp->host_flags |= HF_HINT_IARB;
		++np->iarb_count;
	}
	else
		np->iarb_count = 0;
	np->last_cp = cp;
#endif

	/*
	 *  Insert first the idle task and then our job.
	 *  The MB should ensure proper ordering.
	 */
	qidx = np->squeueput + 2;
	if (qidx >= MAX_QUEUE*2) qidx = 0;

	np->squeue [qidx]	   = cpu_to_scr(np->idletask_ba);
	MEMORY_BARRIER();
	np->squeue [np->squeueput] = cpu_to_scr(cp->ccb_ba);

	np->squeueput = qidx;

	if (DEBUG_FLAGS & DEBUG_QUEUE)
		printf ("%s: queuepos=%d.\n", sym_name (np), np->squeueput);

	/*
	 *  Script processor may be waiting for reselect.
	 *  Wake it up.
	 */
	MEMORY_BARRIER();
	OUTB (nc_istat, SIGP|np->istat_sem);
}

/*
 *  Soft reset the chip.
 *
 *  Raising SRST when the chip is running may cause
 *  problems on dual function chips (see below).
 *  On the other hand, LVD devices need some delay
 *  to settle and report actual BUS mode in STEST4.
 */
static void sym_chip_reset (hcb_p np)
{
	OUTB (nc_istat, SRST);
	UDELAY (10);
	OUTB (nc_istat, 0);
	UDELAY(2000);	/* For BUS MODE to settle */
}

/*
 *  Soft reset the chip.
 *
 *  Some 896 and 876 chip revisions may hang-up if we set
 *  the SRST (soft reset) bit at the wrong time when SCRIPTS
 *  are running.
 *  So, we need to abort the current operation prior to
 *  soft resetting the chip.
 */
static void sym_soft_reset (hcb_p np)
{
	u_char istat;
	int i;

	OUTB (nc_istat, CABRT);
	for (i = 1000000 ; i ; --i) {
		istat = INB (nc_istat);
		if (istat & SIP) {
			INW (nc_sist);
			continue;
		}
		if (istat & DIP) {
			OUTB (nc_istat, 0);
			INB (nc_dstat);
			break;
		}
	}
	if (!i)
		printf("%s: unable to abort current chip operation.\n",
			sym_name(np));
	sym_chip_reset (np);
}

/*
 *  Start reset process.
 *
 *  The interrupt handler will reinitialize the chip.
 */
static void sym_start_reset(hcb_p np)
{
	(void) sym_reset_scsi_bus(np, 1);
}

static int sym_reset_scsi_bus(hcb_p np, int enab_int)
{
	u32 term;
	int retv = 0;

	sym_soft_reset(np);	/* Soft reset the chip */
	if (enab_int)
		OUTW (nc_sien, RST);
	/*
	 *  Enable Tolerant, reset IRQD if present and
	 *  properly set IRQ mode, prior to resetting the bus.
	 */
	OUTB (nc_stest3, TE);
	OUTB (nc_dcntl, (np->rv_dcntl & IRQM));
	OUTB (nc_scntl1, CRST);
	UDELAY (200);

	if (!SYM_SETUP_SCSI_BUS_CHECK)
		goto out;
	/*
	 *  Check for no terminators or SCSI bus shorts to ground.
	 *  Read SCSI data bus, data parity bits and control signals.
	 *  We are expecting RESET to be TRUE and other signals to be
	 *  FALSE.
	 */
	term =	INB(nc_sstat0);
	term =	((term & 2) << 7) + ((term & 1) << 17);	/* rst sdp0 */
	term |= ((INB(nc_sstat2) & 0x01) << 26) |	/* sdp1     */
		((INW(nc_sbdl) & 0xff)   << 9)  |	/* d7-0     */
		((INW(nc_sbdl) & 0xff00) << 10) |	/* d15-8    */
		INB(nc_sbcl);	/* req ack bsy sel atn msg cd io    */

	if (!(np->features & FE_WIDE))
		term &= 0x3ffff;

	if (term != (2<<7)) {
		printf("%s: suspicious SCSI data while resetting the BUS.\n",
			sym_name(np));
		printf("%s: %sdp0,d7-0,rst,req,ack,bsy,sel,atn,msg,c/d,i/o = "
			"0x%lx, expecting 0x%lx\n",
			sym_name(np),
			(np->features & FE_WIDE) ? "dp1,d15-8," : "",
			(u_long)term, (u_long)(2<<7));
		if (SYM_SETUP_SCSI_BUS_CHECK == 1)
			retv = 1;
	}
out:
	OUTB (nc_scntl1, 0);
	/* MDELAY(100); */
	return retv;
}

/*
 *  The chip may have completed jobs. Look at the DONE QUEUE.
 *
 *  On architectures that may reorder LOAD/STORE operations,
 *  a memory barrier may be needed after the reading of the
 *  so-called `flag' and prior to dealing with the data.
 */
static int sym_wakeup_done (hcb_p np)
{
	ccb_p cp;
	int i, n;
	u32 dsa;

	SYM_LOCK_ASSERT(MA_OWNED);

	n = 0;
	i = np->dqueueget;
	while (1) {
		dsa = scr_to_cpu(np->dqueue[i]);
		if (!dsa)
			break;
		np->dqueue[i] = 0;
		if ((i = i+2) >= MAX_QUEUE*2)
			i = 0;

		cp = sym_ccb_from_dsa(np, dsa);
		if (cp) {
			MEMORY_BARRIER();
			sym_complete_ok (np, cp);
			++n;
		}
		else
			printf ("%s: bad DSA (%x) in done queue.\n",
				sym_name(np), (u_int) dsa);
	}
	np->dqueueget = i;

	return n;
}

/*
 *  Complete all active CCBs with error.
 *  Used on CHIP/SCSI RESET.
 */
static void sym_flush_busy_queue (hcb_p np, int cam_status)
{
	/*
	 *  Move all active CCBs to the COMP queue
	 *  and flush this queue.
	 */
	sym_que_splice(&np->busy_ccbq, &np->comp_ccbq);
	sym_que_init(&np->busy_ccbq);
	sym_flush_comp_queue(np, cam_status);
}

/*
 *  Start chip.
 *
 *  'reason' means:
 *     0: initialisation.
 *     1: SCSI BUS RESET delivered or received.
 *     2: SCSI BUS MODE changed.
 */
static void sym_init (hcb_p np, int reason)
{
 	int	i;
	u32	phys;

	SYM_LOCK_ASSERT(MA_OWNED);

 	/*
	 *  Reset chip if asked, otherwise just clear fifos.
 	 */
	if (reason == 1)
		sym_soft_reset(np);
	else {
		OUTB (nc_stest3, TE|CSF);
		OUTONB (nc_ctest3, CLF);
	}

	/*
	 *  Clear Start Queue
	 */
	phys = np->squeue_ba;
	for (i = 0; i < MAX_QUEUE*2; i += 2) {
		np->squeue[i]   = cpu_to_scr(np->idletask_ba);
		np->squeue[i+1] = cpu_to_scr(phys + (i+2)*4);
	}
	np->squeue[MAX_QUEUE*2-1] = cpu_to_scr(phys);

	/*
	 *  Start at first entry.
	 */
	np->squeueput = 0;

	/*
	 *  Clear Done Queue
	 */
	phys = np->dqueue_ba;
	for (i = 0; i < MAX_QUEUE*2; i += 2) {
		np->dqueue[i]   = 0;
		np->dqueue[i+1] = cpu_to_scr(phys + (i+2)*4);
	}
	np->dqueue[MAX_QUEUE*2-1] = cpu_to_scr(phys);

	/*
	 *  Start at first entry.
	 */
	np->dqueueget = 0;

	/*
	 *  Install patches in scripts.
	 *  This also let point to first position the start
	 *  and done queue pointers used from SCRIPTS.
	 */
	np->fw_patch(np);

	/*
	 *  Wakeup all pending jobs.
	 */
	sym_flush_busy_queue(np, CAM_SCSI_BUS_RESET);

	/*
	 *  Init chip.
	 */
	OUTB (nc_istat,  0x00   );	/*  Remove Reset, abort */
	UDELAY (2000);	/* The 895 needs time for the bus mode to settle */

	OUTB (nc_scntl0, np->rv_scntl0 | 0xc0);
					/*  full arb., ena parity, par->ATN  */
	OUTB (nc_scntl1, 0x00);		/*  odd parity, and remove CRST!! */

	sym_selectclock(np, np->rv_scntl3);	/* Select SCSI clock */

	OUTB (nc_scid  , RRE|np->myaddr);	/* Adapter SCSI address */
	OUTW (nc_respid, 1ul<<np->myaddr);	/* Id to respond to */
	OUTB (nc_istat , SIGP	);		/*  Signal Process */
	OUTB (nc_dmode , np->rv_dmode);		/* Burst length, dma mode */
	OUTB (nc_ctest5, np->rv_ctest5);	/* Large fifo + large burst */

	OUTB (nc_dcntl , NOCOM|np->rv_dcntl);	/* Protect SFBR */
	OUTB (nc_ctest3, np->rv_ctest3);	/* Write and invalidate */
	OUTB (nc_ctest4, np->rv_ctest4);	/* Master parity checking */

	/* Extended Sreq/Sack filtering not supported on the C10 */
	if (np->features & FE_C10)
		OUTB (nc_stest2, np->rv_stest2);
	else
		OUTB (nc_stest2, EXT|np->rv_stest2);

	OUTB (nc_stest3, TE);			/* TolerANT enable */
	OUTB (nc_stime0, 0x0c);			/* HTH disabled  STO 0.25 sec */

	/*
	 *  For now, disable AIP generation on C1010-66.
	 */
	if (np->device_id == PCI_ID_LSI53C1010_2)
		OUTB (nc_aipcntl1, DISAIP);

	/*
	 *  C10101 Errata.
	 *  Errant SGE's when in narrow. Write bits 4 & 5 of
	 *  STEST1 register to disable SGE. We probably should do
	 *  that from SCRIPTS for each selection/reselection, but
	 *  I just don't want. :)
	 */
	if (np->device_id == PCI_ID_LSI53C1010 &&
	    /* np->revision_id < 0xff */ 1)
		OUTB (nc_stest1, INB(nc_stest1) | 0x30);

	/*
	 *  DEL 441 - 53C876 Rev 5 - Part Number 609-0392787/2788 - ITEM 2.
	 *  Disable overlapped arbitration for some dual function devices,
	 *  regardless revision id (kind of post-chip-design feature. ;-))
	 */
	if (np->device_id == PCI_ID_SYM53C875)
		OUTB (nc_ctest0, (1<<5));
	else if (np->device_id == PCI_ID_SYM53C896)
		np->rv_ccntl0 |= DPR;

	/*
	 *  Write CCNTL0/CCNTL1 for chips capable of 64 bit addressing
	 *  and/or hardware phase mismatch, since only such chips
	 *  seem to support those IO registers.
	 */
	if (np->features & (FE_DAC|FE_NOPM)) {
		OUTB (nc_ccntl0, np->rv_ccntl0);
		OUTB (nc_ccntl1, np->rv_ccntl1);
	}

	/*
	 *  If phase mismatch handled by scripts (895A/896/1010),
	 *  set PM jump addresses.
	 */
	if (np->features & FE_NOPM) {
		OUTL (nc_pmjad1, SCRIPTB_BA (np, pm_handle));
		OUTL (nc_pmjad2, SCRIPTB_BA (np, pm_handle));
	}

	/*
	 *    Enable GPIO0 pin for writing if LED support from SCRIPTS.
	 *    Also set GPIO5 and clear GPIO6 if hardware LED control.
	 */
	if (np->features & FE_LED0)
		OUTB(nc_gpcntl, INB(nc_gpcntl) & ~0x01);
	else if (np->features & FE_LEDC)
		OUTB(nc_gpcntl, (INB(nc_gpcntl) & ~0x41) | 0x20);

	/*
	 *      enable ints
	 */
	OUTW (nc_sien , STO|HTH|MA|SGE|UDC|RST|PAR);
	OUTB (nc_dien , MDPE|BF|SSI|SIR|IID);

	/*
	 *  For 895/6 enable SBMC interrupt and save current SCSI bus mode.
	 *  Try to eat the spurious SBMC interrupt that may occur when
	 *  we reset the chip but not the SCSI BUS (at initialization).
	 */
	if (np->features & (FE_ULTRA2|FE_ULTRA3)) {
		OUTONW (nc_sien, SBMC);
		if (reason == 0) {
			MDELAY(100);
			INW (nc_sist);
		}
		np->scsi_mode = INB (nc_stest4) & SMODE;
	}

	/*
	 *  Fill in target structure.
	 *  Reinitialize usrsync.
	 *  Reinitialize usrwide.
	 *  Prepare sync negotiation according to actual SCSI bus mode.
	 */
	for (i=0;i<SYM_CONF_MAX_TARGET;i++) {
		tcb_p tp = &np->target[i];

		tp->to_reset  = 0;
		tp->head.sval = 0;
		tp->head.wval = np->rv_scntl3;
		tp->head.uval = 0;

		tp->tinfo.current.period = 0;
		tp->tinfo.current.offset = 0;
		tp->tinfo.current.width  = BUS_8_BIT;
		tp->tinfo.current.options = 0;
	}

	/*
	 *  Download SCSI SCRIPTS to on-chip RAM if present,
	 *  and start script processor.
	 */
	if (np->ram_ba) {
		if (sym_verbose > 1)
			printf ("%s: Downloading SCSI SCRIPTS.\n",
				sym_name(np));
		if (np->ram_ws == 8192) {
			OUTRAM_OFF(4096, np->scriptb0, np->scriptb_sz);
			OUTL (nc_mmws, np->scr_ram_seg);
			OUTL (nc_mmrs, np->scr_ram_seg);
			OUTL (nc_sfs,  np->scr_ram_seg);
			phys = SCRIPTB_BA (np, start64);
		}
		else
			phys = SCRIPTA_BA (np, init);
		OUTRAM_OFF(0, np->scripta0, np->scripta_sz);
	}
	else
		phys = SCRIPTA_BA (np, init);

	np->istat_sem = 0;

	OUTL (nc_dsa, np->hcb_ba);
	OUTL_DSP (phys);

	/*
	 *  Notify the XPT about the RESET condition.
	 */
	if (reason != 0)
		xpt_async(AC_BUS_RESET, np->path, NULL);
}

/*
 *  Get clock factor and sync divisor for a given
 *  synchronous factor period.
 */
static int
sym_getsync(hcb_p np, u_char dt, u_char sfac, u_char *divp, u_char *fakp)
{
	u32	clk = np->clock_khz;	/* SCSI clock frequency in kHz	*/
	int	div = np->clock_divn;	/* Number of divisors supported	*/
	u32	fak;			/* Sync factor in sxfer		*/
	u32	per;			/* Period in tenths of ns	*/
	u32	kpc;			/* (per * clk)			*/
	int	ret;

	/*
	 *  Compute the synchronous period in tenths of nano-seconds
	 */
	if (dt && sfac <= 9)	per = 125;
	else if	(sfac <= 10)	per = 250;
	else if	(sfac == 11)	per = 303;
	else if	(sfac == 12)	per = 500;
	else			per = 40 * sfac;
	ret = per;

	kpc = per * clk;
	if (dt)
		kpc <<= 1;

	/*
	 *  For earliest C10 revision 0, we cannot use extra
	 *  clocks for the setting of the SCSI clocking.
	 *  Note that this limits the lowest sync data transfer
	 *  to 5 Mega-transfers per second and may result in
	 *  using higher clock divisors.
	 */
#if 1
	if ((np->features & (FE_C10|FE_U3EN)) == FE_C10) {
		/*
		 *  Look for the lowest clock divisor that allows an
		 *  output speed not faster than the period.
		 */
		while (div > 0) {
			--div;
			if (kpc > (div_10M[div] << 2)) {
				++div;
				break;
			}
		}
		fak = 0;			/* No extra clocks */
		if (div == np->clock_divn) {	/* Are we too fast ? */
			ret = -1;
		}
		*divp = div;
		*fakp = fak;
		return ret;
	}
#endif

	/*
	 *  Look for the greatest clock divisor that allows an
	 *  input speed faster than the period.
	 */
	while (div-- > 0)
		if (kpc >= (div_10M[div] << 2)) break;

	/*
	 *  Calculate the lowest clock factor that allows an output
	 *  speed not faster than the period, and the max output speed.
	 *  If fak >= 1 we will set both XCLKH_ST and XCLKH_DT.
	 *  If fak >= 2 we will also set XCLKS_ST and XCLKS_DT.
	 */
	if (dt) {
		fak = (kpc - 1) / (div_10M[div] << 1) + 1 - 2;
		/* ret = ((2+fak)*div_10M[div])/np->clock_khz; */
	}
	else {
		fak = (kpc - 1) / div_10M[div] + 1 - 4;
		/* ret = ((4+fak)*div_10M[div])/np->clock_khz; */
	}

	/*
	 *  Check against our hardware limits, or bugs :).
	 */
	if (fak > 2)	{fak = 2; ret = -1;}

	/*
	 *  Compute and return sync parameters.
	 */
	*divp = div;
	*fakp = fak;

	return ret;
}

/*
 *  Tell the SCSI layer about the new transfer parameters.
 */
static void
sym_xpt_async_transfer_neg(hcb_p np, int target, u_int spi_valid)
{
	struct ccb_trans_settings cts;
	struct cam_path *path;
	int sts;
	tcb_p tp = &np->target[target];

	sts = xpt_create_path(&path, NULL, cam_sim_path(np->sim), target,
	                      CAM_LUN_WILDCARD);
	if (sts != CAM_REQ_CMP)
		return;

	bzero(&cts, sizeof(cts));

#define	cts__scsi (cts.proto_specific.scsi)
#define	cts__spi  (cts.xport_specific.spi)

	cts.type      = CTS_TYPE_CURRENT_SETTINGS;
	cts.protocol  = PROTO_SCSI;
	cts.transport = XPORT_SPI;
	cts.protocol_version  = tp->tinfo.current.scsi_version;
	cts.transport_version = tp->tinfo.current.spi_version;

	cts__spi.valid = spi_valid;
	if (spi_valid & CTS_SPI_VALID_SYNC_RATE)
		cts__spi.sync_period = tp->tinfo.current.period;
	if (spi_valid & CTS_SPI_VALID_SYNC_OFFSET)
		cts__spi.sync_offset = tp->tinfo.current.offset;
	if (spi_valid & CTS_SPI_VALID_BUS_WIDTH)
		cts__spi.bus_width   = tp->tinfo.current.width;
	if (spi_valid & CTS_SPI_VALID_PPR_OPTIONS)
		cts__spi.ppr_options = tp->tinfo.current.options;
#undef cts__spi
#undef cts__scsi
	xpt_setup_ccb(&cts.ccb_h, path, /*priority*/1);
	xpt_async(AC_TRANSFER_NEG, path, &cts);
	xpt_free_path(path);
}

#define SYM_SPI_VALID_WDTR		\
	CTS_SPI_VALID_BUS_WIDTH |	\
	CTS_SPI_VALID_SYNC_RATE |	\
	CTS_SPI_VALID_SYNC_OFFSET
#define SYM_SPI_VALID_SDTR		\
	CTS_SPI_VALID_SYNC_RATE |	\
	CTS_SPI_VALID_SYNC_OFFSET
#define SYM_SPI_VALID_PPR		\
	CTS_SPI_VALID_PPR_OPTIONS |	\
	CTS_SPI_VALID_BUS_WIDTH |	\
	CTS_SPI_VALID_SYNC_RATE |	\
	CTS_SPI_VALID_SYNC_OFFSET

/*
 *  We received a WDTR.
 *  Let everything be aware of the changes.
 */
static void sym_setwide(hcb_p np, ccb_p cp, u_char wide)
{
	tcb_p tp = &np->target[cp->target];

	sym_settrans(np, cp, 0, 0, 0, wide, 0, 0);

	/*
	 *  Tell the SCSI layer about the new transfer parameters.
	 */
	tp->tinfo.goal.width = tp->tinfo.current.width = wide;
	tp->tinfo.current.offset = 0;
	tp->tinfo.current.period = 0;
	tp->tinfo.current.options = 0;

	sym_xpt_async_transfer_neg(np, cp->target, SYM_SPI_VALID_WDTR);
}

/*
 *  We received a SDTR.
 *  Let everything be aware of the changes.
 */
static void
sym_setsync(hcb_p np, ccb_p cp, u_char ofs, u_char per, u_char div, u_char fak)
{
	tcb_p tp = &np->target[cp->target];
	u_char wide = (cp->phys.select.sel_scntl3 & EWS) ? 1 : 0;

	sym_settrans(np, cp, 0, ofs, per, wide, div, fak);

	/*
	 *  Tell the SCSI layer about the new transfer parameters.
	 */
	tp->tinfo.goal.period	= tp->tinfo.current.period  = per;
	tp->tinfo.goal.offset	= tp->tinfo.current.offset  = ofs;
	tp->tinfo.goal.options	= tp->tinfo.current.options = 0;

	sym_xpt_async_transfer_neg(np, cp->target, SYM_SPI_VALID_SDTR);
}

/*
 *  We received a PPR.
 *  Let everything be aware of the changes.
 */
static void sym_setpprot(hcb_p np, ccb_p cp, u_char dt, u_char ofs,
			 u_char per, u_char wide, u_char div, u_char fak)
{
	tcb_p tp = &np->target[cp->target];

	sym_settrans(np, cp, dt, ofs, per, wide, div, fak);

	/*
	 *  Tell the SCSI layer about the new transfer parameters.
	 */
	tp->tinfo.goal.width	= tp->tinfo.current.width  = wide;
	tp->tinfo.goal.period	= tp->tinfo.current.period = per;
	tp->tinfo.goal.offset	= tp->tinfo.current.offset = ofs;
	tp->tinfo.goal.options	= tp->tinfo.current.options = dt;

	sym_xpt_async_transfer_neg(np, cp->target, SYM_SPI_VALID_PPR);
}

/*
 *  Switch trans mode for current job and it's target.
 */
static void sym_settrans(hcb_p np, ccb_p cp, u_char dt, u_char ofs,
			 u_char per, u_char wide, u_char div, u_char fak)
{
	SYM_QUEHEAD *qp;
	union	ccb *ccb;
	tcb_p tp;
	u_char target = INB (nc_sdid) & 0x0f;
	u_char sval, wval, uval;

	assert (cp);
	if (!cp) return;
	ccb = cp->cam_ccb;
	assert (ccb);
	if (!ccb) return;
	assert (target == (cp->target & 0xf));
	tp = &np->target[target];

	sval = tp->head.sval;
	wval = tp->head.wval;
	uval = tp->head.uval;

#if 0
	printf("XXXX sval=%x wval=%x uval=%x (%x)\n",
		sval, wval, uval, np->rv_scntl3);
#endif
	/*
	 *  Set the offset.
	 */
	if (!(np->features & FE_C10))
		sval = (sval & ~0x1f) | ofs;
	else
		sval = (sval & ~0x3f) | ofs;

	/*
	 *  Set the sync divisor and extra clock factor.
	 */
	if (ofs != 0) {
		wval = (wval & ~0x70) | ((div+1) << 4);
		if (!(np->features & FE_C10))
			sval = (sval & ~0xe0) | (fak << 5);
		else {
			uval = uval & ~(XCLKH_ST|XCLKH_DT|XCLKS_ST|XCLKS_DT);
			if (fak >= 1) uval |= (XCLKH_ST|XCLKH_DT);
			if (fak >= 2) uval |= (XCLKS_ST|XCLKS_DT);
		}
	}

	/*
	 *  Set the bus width.
	 */
	wval = wval & ~EWS;
	if (wide != 0)
		wval |= EWS;

	/*
	 *  Set misc. ultra enable bits.
	 */
	if (np->features & FE_C10) {
		uval = uval & ~(U3EN|AIPCKEN);
		if (dt)	{
			assert(np->features & FE_U3EN);
			uval |= U3EN;
		}
	}
	else {
		wval = wval & ~ULTRA;
		if (per <= 12)	wval |= ULTRA;
	}

	/*
	 *   Stop there if sync parameters are unchanged.
	 */
	if (tp->head.sval == sval &&
	    tp->head.wval == wval &&
	    tp->head.uval == uval)
		return;
	tp->head.sval = sval;
	tp->head.wval = wval;
	tp->head.uval = uval;

	/*
	 *  Disable extended Sreq/Sack filtering if per < 50.
	 *  Not supported on the C1010.
	 */
	if (per < 50 && !(np->features & FE_C10))
		OUTOFFB (nc_stest2, EXT);

	/*
	 *  set actual value and sync_status
	 */
	OUTB (nc_sxfer,  tp->head.sval);
	OUTB (nc_scntl3, tp->head.wval);

	if (np->features & FE_C10) {
		OUTB (nc_scntl4, tp->head.uval);
	}

	/*
	 *  patch ALL busy ccbs of this target.
	 */
	FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
		cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		if (cp->target != target)
			continue;
		cp->phys.select.sel_scntl3 = tp->head.wval;
		cp->phys.select.sel_sxfer  = tp->head.sval;
		if (np->features & FE_C10) {
			cp->phys.select.sel_scntl4 = tp->head.uval;
		}
	}
}

/*
 *  log message for real hard errors
 *
 *  sym0 targ 0?: ERROR (ds:si) (so-si-sd) (sxfer/scntl3) @ name (dsp:dbc).
 *  	      reg: r0 r1 r2 r3 r4 r5 r6 ..... rf.
 *
 *  exception register:
 *  	ds:	dstat
 *  	si:	sist
 *
 *  SCSI bus lines:
 *  	so:	control lines as driven by chip.
 *  	si:	control lines as seen by chip.
 *  	sd:	scsi data lines as seen by chip.
 *
 *  wide/fastmode:
 *  	sxfer:	(see the manual)
 *  	scntl3:	(see the manual)
 *
 *  current script command:
 *  	dsp:	script address (relative to start of script).
 *  	dbc:	first word of script command.
 *
 *  First 24 register of the chip:
 *  	r0..rf
 */
static void sym_log_hard_error(hcb_p np, u_short sist, u_char dstat)
{
	u32	dsp;
	int	script_ofs;
	int	script_size;
	char	*script_name;
	u_char	*script_base;
	int	i;

	dsp	= INL (nc_dsp);

	if	(dsp > np->scripta_ba &&
		 dsp <= np->scripta_ba + np->scripta_sz) {
		script_ofs	= dsp - np->scripta_ba;
		script_size	= np->scripta_sz;
		script_base	= (u_char *) np->scripta0;
		script_name	= "scripta";
	}
	else if (np->scriptb_ba < dsp &&
		 dsp <= np->scriptb_ba + np->scriptb_sz) {
		script_ofs	= dsp - np->scriptb_ba;
		script_size	= np->scriptb_sz;
		script_base	= (u_char *) np->scriptb0;
		script_name	= "scriptb";
	} else {
		script_ofs	= dsp;
		script_size	= 0;
		script_base	= NULL;
		script_name	= "mem";
	}

	printf ("%s:%d: ERROR (%x:%x) (%x-%x-%x) (%x/%x) @ (%s %x:%08x).\n",
		sym_name (np), (unsigned)INB (nc_sdid)&0x0f, dstat, sist,
		(unsigned)INB (nc_socl), (unsigned)INB (nc_sbcl),
		(unsigned)INB (nc_sbdl), (unsigned)INB (nc_sxfer),
		(unsigned)INB (nc_scntl3), script_name, script_ofs,
		(unsigned)INL (nc_dbc));

	if (((script_ofs & 3) == 0) &&
	    (unsigned)script_ofs < script_size) {
		printf ("%s: script cmd = %08x\n", sym_name(np),
			scr_to_cpu((int) *(u32 *)(script_base + script_ofs)));
	}

        printf ("%s: regdump:", sym_name(np));
        for (i=0; i<24;i++)
            printf (" %02x", (unsigned)INB_OFF(i));
        printf (".\n");

	/*
	 *  PCI BUS error, read the PCI ststus register.
	 */
	if (dstat & (MDPE|BF)) {
		u_short pci_sts;
		pci_sts = pci_read_config(np->device, PCIR_STATUS, 2);
		if (pci_sts & 0xf900) {
			pci_write_config(np->device, PCIR_STATUS, pci_sts, 2);
			printf("%s: PCI STATUS = 0x%04x\n",
				sym_name(np), pci_sts & 0xf900);
		}
	}
}

/*
 *  chip interrupt handler
 *
 *  In normal situations, interrupt conditions occur one at
 *  a time. But when something bad happens on the SCSI BUS,
 *  the chip may raise several interrupt flags before
 *  stopping and interrupting the CPU. The additionnal
 *  interrupt flags are stacked in some extra registers
 *  after the SIP and/or DIP flag has been raised in the
 *  ISTAT. After the CPU has read the interrupt condition
 *  flag from SIST or DSTAT, the chip unstacks the other
 *  interrupt flags and sets the corresponding bits in
 *  SIST or DSTAT. Since the chip starts stacking once the
 *  SIP or DIP flag is set, there is a small window of time
 *  where the stacking does not occur.
 *
 *  Typically, multiple interrupt conditions may happen in
 *  the following situations:
 *
 *  - SCSI parity error + Phase mismatch  (PAR|MA)
 *    When a parity error is detected in input phase
 *    and the device switches to msg-in phase inside a
 *    block MOV.
 *  - SCSI parity error + Unexpected disconnect (PAR|UDC)
 *    When a stupid device does not want to handle the
 *    recovery of an SCSI parity error.
 *  - Some combinations of STO, PAR, UDC, ...
 *    When using non compliant SCSI stuff, when user is
 *    doing non compliant hot tampering on the BUS, when
 *    something really bad happens to a device, etc ...
 *
 *  The heuristic suggested by SYMBIOS to handle
 *  multiple interrupts is to try unstacking all
 *  interrupts conditions and to handle them on some
 *  priority based on error severity.
 *  This will work when the unstacking has been
 *  successful, but we cannot be 100 % sure of that,
 *  since the CPU may have been faster to unstack than
 *  the chip is able to stack. Hmmm ... But it seems that
 *  such a situation is very unlikely to happen.
 *
 *  If this happen, for example STO caught by the CPU
 *  then UDC happenning before the CPU have restarted
 *  the SCRIPTS, the driver may wrongly complete the
 *  same command on UDC, since the SCRIPTS didn't restart
 *  and the DSA still points to the same command.
 *  We avoid this situation by setting the DSA to an
 *  invalid value when the CCB is completed and before
 *  restarting the SCRIPTS.
 *
 *  Another issue is that we need some section of our
 *  recovery procedures to be somehow uninterruptible but
 *  the SCRIPTS processor does not provides such a
 *  feature. For this reason, we handle recovery preferently
 *  from the C code and check against some SCRIPTS critical
 *  sections from the C code.
 *
 *  Hopefully, the interrupt handling of the driver is now
 *  able to resist to weird BUS error conditions, but donnot
 *  ask me for any guarantee that it will never fail. :-)
 *  Use at your own decision and risk.
 */
static void sym_intr1 (hcb_p np)
{
	u_char	istat, istatc;
	u_char	dstat;
	u_short	sist;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  interrupt on the fly ?
	 *
	 *  A `dummy read' is needed to ensure that the
	 *  clear of the INTF flag reaches the device
	 *  before the scanning of the DONE queue.
	 */
	istat = INB (nc_istat);
	if (istat & INTF) {
		OUTB (nc_istat, (istat & SIGP) | INTF | np->istat_sem);
		istat = INB (nc_istat);		/* DUMMY READ */
		if (DEBUG_FLAGS & DEBUG_TINY) printf ("F ");
		(void)sym_wakeup_done (np);
	}

	if (!(istat & (SIP|DIP)))
		return;

#if 0	/* We should never get this one */
	if (istat & CABRT)
		OUTB (nc_istat, CABRT);
#endif

	/*
	 *  PAR and MA interrupts may occur at the same time,
	 *  and we need to know of both in order to handle
	 *  this situation properly. We try to unstack SCSI
	 *  interrupts for that reason. BTW, I dislike a LOT
	 *  such a loop inside the interrupt routine.
	 *  Even if DMA interrupt stacking is very unlikely to
	 *  happen, we also try unstacking these ones, since
	 *  this has no performance impact.
	 */
	sist	= 0;
	dstat	= 0;
	istatc	= istat;
	do {
		if (istatc & SIP)
			sist  |= INW (nc_sist);
		if (istatc & DIP)
			dstat |= INB (nc_dstat);
		istatc = INB (nc_istat);
		istat |= istatc;
	} while (istatc & (SIP|DIP));

	if (DEBUG_FLAGS & DEBUG_TINY)
		printf ("<%d|%x:%x|%x:%x>",
			(int)INB(nc_scr0),
			dstat,sist,
			(unsigned)INL(nc_dsp),
			(unsigned)INL(nc_dbc));
	/*
	 *  On paper, a memory barrier may be needed here.
	 *  And since we are paranoid ... :)
	 */
	MEMORY_BARRIER();

	/*
	 *  First, interrupts we want to service cleanly.
	 *
	 *  Phase mismatch (MA) is the most frequent interrupt
	 *  for chip earlier than the 896 and so we have to service
	 *  it as quickly as possible.
	 *  A SCSI parity error (PAR) may be combined with a phase
	 *  mismatch condition (MA).
	 *  Programmed interrupts (SIR) are used to call the C code
	 *  from SCRIPTS.
	 *  The single step interrupt (SSI) is not used in this
	 *  driver.
	 */
	if (!(sist  & (STO|GEN|HTH|SGE|UDC|SBMC|RST)) &&
	    !(dstat & (MDPE|BF|ABRT|IID))) {
		if	(sist & PAR)	sym_int_par (np, sist);
		else if (sist & MA)	sym_int_ma (np);
		else if (dstat & SIR)	sym_int_sir (np);
		else if (dstat & SSI)	OUTONB_STD ();
		else			goto unknown_int;
		return;
	}

	/*
	 *  Now, interrupts that donnot happen in normal
	 *  situations and that we may need to recover from.
	 *
	 *  On SCSI RESET (RST), we reset everything.
	 *  On SCSI BUS MODE CHANGE (SBMC), we complete all
	 *  active CCBs with RESET status, prepare all devices
	 *  for negotiating again and restart the SCRIPTS.
	 *  On STO and UDC, we complete the CCB with the corres-
	 *  ponding status and restart the SCRIPTS.
	 */
	if (sist & RST) {
		xpt_print_path(np->path);
		printf("SCSI BUS reset detected.\n");
		sym_init (np, 1);
		return;
	}

	OUTB (nc_ctest3, np->rv_ctest3 | CLF);	/* clear dma fifo  */
	OUTB (nc_stest3, TE|CSF);		/* clear scsi fifo */

	if (!(sist  & (GEN|HTH|SGE)) &&
	    !(dstat & (MDPE|BF|ABRT|IID))) {
		if	(sist & SBMC)	sym_int_sbmc (np);
		else if (sist & STO)	sym_int_sto (np);
		else if (sist & UDC)	sym_int_udc (np);
		else			goto unknown_int;
		return;
	}

	/*
	 *  Now, interrupts we are not able to recover cleanly.
	 *
	 *  Log message for hard errors.
	 *  Reset everything.
	 */

	sym_log_hard_error(np, sist, dstat);

	if ((sist & (GEN|HTH|SGE)) ||
		(dstat & (MDPE|BF|ABRT|IID))) {
		sym_start_reset(np);
		return;
	}

unknown_int:
	/*
	 *  We just miss the cause of the interrupt. :(
	 *  Print a message. The timeout will do the real work.
	 */
	printf(	"%s: unknown interrupt(s) ignored, "
		"ISTAT=0x%x DSTAT=0x%x SIST=0x%x\n",
		sym_name(np), istat, dstat, sist);
}

static void sym_intr(void *arg)
{
	hcb_p np = arg;

	SYM_LOCK();

	if (DEBUG_FLAGS & DEBUG_TINY) printf ("[");
	sym_intr1((hcb_p) arg);
	if (DEBUG_FLAGS & DEBUG_TINY) printf ("]");

	SYM_UNLOCK();
}

static void sym_poll(struct cam_sim *sim)
{
	sym_intr1(cam_sim_softc(sim));
}

/*
 *  generic recovery from scsi interrupt
 *
 *  The doc says that when the chip gets an SCSI interrupt,
 *  it tries to stop in an orderly fashion, by completing
 *  an instruction fetch that had started or by flushing
 *  the DMA fifo for a write to memory that was executing.
 *  Such a fashion is not enough to know if the instruction
 *  that was just before the current DSP value has been
 *  executed or not.
 *
 *  There are some small SCRIPTS sections that deal with
 *  the start queue and the done queue that may break any
 *  assomption from the C code if we are interrupted
 *  inside, so we reset if this happens. Btw, since these
 *  SCRIPTS sections are executed while the SCRIPTS hasn't
 *  started SCSI operations, it is very unlikely to happen.
 *
 *  All the driver data structures are supposed to be
 *  allocated from the same 4 GB memory window, so there
 *  is a 1 to 1 relationship between DSA and driver data
 *  structures. Since we are careful :) to invalidate the
 *  DSA when we complete a command or when the SCRIPTS
 *  pushes a DSA into a queue, we can trust it when it
 *  points to a CCB.
 */
static void sym_recover_scsi_int (hcb_p np, u_char hsts)
{
	u32	dsp	= INL (nc_dsp);
	u32	dsa	= INL (nc_dsa);
	ccb_p cp	= sym_ccb_from_dsa(np, dsa);

	/*
	 *  If we haven't been interrupted inside the SCRIPTS
	 *  critical paths, we can safely restart the SCRIPTS
	 *  and trust the DSA value if it matches a CCB.
	 */
	if ((!(dsp > SCRIPTA_BA (np, getjob_begin) &&
	       dsp < SCRIPTA_BA (np, getjob_end) + 1)) &&
	    (!(dsp > SCRIPTA_BA (np, ungetjob) &&
	       dsp < SCRIPTA_BA (np, reselect) + 1)) &&
	    (!(dsp > SCRIPTB_BA (np, sel_for_abort) &&
	       dsp < SCRIPTB_BA (np, sel_for_abort_1) + 1)) &&
	    (!(dsp > SCRIPTA_BA (np, done) &&
	       dsp < SCRIPTA_BA (np, done_end) + 1))) {
		OUTB (nc_ctest3, np->rv_ctest3 | CLF);	/* clear dma fifo  */
		OUTB (nc_stest3, TE|CSF);		/* clear scsi fifo */
		/*
		 *  If we have a CCB, let the SCRIPTS call us back for
		 *  the handling of the error with SCRATCHA filled with
		 *  STARTPOS. This way, we will be able to freeze the
		 *  device queue and requeue awaiting IOs.
		 */
		if (cp) {
			cp->host_status = hsts;
			OUTL_DSP (SCRIPTA_BA (np, complete_error));
		}
		/*
		 *  Otherwise just restart the SCRIPTS.
		 */
		else {
			OUTL (nc_dsa, 0xffffff);
			OUTL_DSP (SCRIPTA_BA (np, start));
		}
	}
	else
		goto reset_all;

	return;

reset_all:
	sym_start_reset(np);
}

/*
 *  chip exception handler for selection timeout
 */
static void sym_int_sto (hcb_p np)
{
	u32 dsp	= INL (nc_dsp);

	if (DEBUG_FLAGS & DEBUG_TINY) printf ("T");

	if (dsp == SCRIPTA_BA (np, wf_sel_done) + 8)
		sym_recover_scsi_int(np, HS_SEL_TIMEOUT);
	else
		sym_start_reset(np);
}

/*
 *  chip exception handler for unexpected disconnect
 */
static void sym_int_udc (hcb_p np)
{
	printf ("%s: unexpected disconnect\n", sym_name(np));
	sym_recover_scsi_int(np, HS_UNEXPECTED);
}

/*
 *  chip exception handler for SCSI bus mode change
 *
 *  spi2-r12 11.2.3 says a transceiver mode change must
 *  generate a reset event and a device that detects a reset
 *  event shall initiate a hard reset. It says also that a
 *  device that detects a mode change shall set data transfer
 *  mode to eight bit asynchronous, etc...
 *  So, just reinitializing all except chip should be enough.
 */
static void sym_int_sbmc (hcb_p np)
{
	u_char scsi_mode = INB (nc_stest4) & SMODE;

	/*
	 *  Notify user.
	 */
	xpt_print_path(np->path);
	printf("SCSI BUS mode change from %s to %s.\n",
		sym_scsi_bus_mode(np->scsi_mode), sym_scsi_bus_mode(scsi_mode));

	/*
	 *  Should suspend command processing for a few seconds and
	 *  reinitialize all except the chip.
	 */
	sym_init (np, 2);
}

/*
 *  chip exception handler for SCSI parity error.
 *
 *  When the chip detects a SCSI parity error and is
 *  currently executing a (CH)MOV instruction, it does
 *  not interrupt immediately, but tries to finish the
 *  transfer of the current scatter entry before
 *  interrupting. The following situations may occur:
 *
 *  - The complete scatter entry has been transferred
 *    without the device having changed phase.
 *    The chip will then interrupt with the DSP pointing
 *    to the instruction that follows the MOV.
 *
 *  - A phase mismatch occurs before the MOV finished
 *    and phase errors are to be handled by the C code.
 *    The chip will then interrupt with both PAR and MA
 *    conditions set.
 *
 *  - A phase mismatch occurs before the MOV finished and
 *    phase errors are to be handled by SCRIPTS.
 *    The chip will load the DSP with the phase mismatch
 *    JUMP address and interrupt the host processor.
 */
static void sym_int_par (hcb_p np, u_short sist)
{
	u_char	hsts	= INB (HS_PRT);
	u32	dsp	= INL (nc_dsp);
	u32	dbc	= INL (nc_dbc);
	u32	dsa	= INL (nc_dsa);
	u_char	sbcl	= INB (nc_sbcl);
	u_char	cmd	= dbc >> 24;
	int phase	= cmd & 7;
	ccb_p	cp	= sym_ccb_from_dsa(np, dsa);

	printf("%s: SCSI parity error detected: SCR1=%d DBC=%x SBCL=%x\n",
		sym_name(np), hsts, dbc, sbcl);

	/*
	 *  Check that the chip is connected to the SCSI BUS.
	 */
	if (!(INB (nc_scntl1) & ISCON)) {
		sym_recover_scsi_int(np, HS_UNEXPECTED);
		return;
	}

	/*
	 *  If the nexus is not clearly identified, reset the bus.
	 *  We will try to do better later.
	 */
	if (!cp)
		goto reset_all;

	/*
	 *  Check instruction was a MOV, direction was INPUT and
	 *  ATN is asserted.
	 */
	if ((cmd & 0xc0) || !(phase & 1) || !(sbcl & 0x8))
		goto reset_all;

	/*
	 *  Keep track of the parity error.
	 */
	OUTONB (HF_PRT, HF_EXT_ERR);
	cp->xerr_status |= XE_PARITY_ERR;

	/*
	 *  Prepare the message to send to the device.
	 */
	np->msgout[0] = (phase == 7) ? M_PARITY : M_ID_ERROR;

	/*
	 *  If the old phase was DATA IN phase, we have to deal with
	 *  the 3 situations described above.
	 *  For other input phases (MSG IN and STATUS), the device
	 *  must resend the whole thing that failed parity checking
	 *  or signal error. So, jumping to dispatcher should be OK.
	 */
	if (phase == 1 || phase == 5) {
		/* Phase mismatch handled by SCRIPTS */
		if (dsp == SCRIPTB_BA (np, pm_handle))
			OUTL_DSP (dsp);
		/* Phase mismatch handled by the C code */
		else if (sist & MA)
			sym_int_ma (np);
		/* No phase mismatch occurred */
		else {
			OUTL (nc_temp, dsp);
			OUTL_DSP (SCRIPTA_BA (np, dispatch));
		}
	}
	else
		OUTL_DSP (SCRIPTA_BA (np, clrack));
	return;

reset_all:
	sym_start_reset(np);
}

/*
 *  chip exception handler for phase errors.
 *
 *  We have to construct a new transfer descriptor,
 *  to transfer the rest of the current block.
 */
static void sym_int_ma (hcb_p np)
{
	u32	dbc;
	u32	rest;
	u32	dsp;
	u32	dsa;
	u32	nxtdsp;
	u32	*vdsp;
	u32	oadr, olen;
	u32	*tblp;
        u32	newcmd;
	u_int	delta;
	u_char	cmd;
	u_char	hflags, hflags0;
	struct	sym_pmc *pm;
	ccb_p	cp;

	dsp	= INL (nc_dsp);
	dbc	= INL (nc_dbc);
	dsa	= INL (nc_dsa);

	cmd	= dbc >> 24;
	rest	= dbc & 0xffffff;
	delta	= 0;

	/*
	 *  locate matching cp if any.
	 */
	cp = sym_ccb_from_dsa(np, dsa);

	/*
	 *  Donnot take into account dma fifo and various buffers in
	 *  INPUT phase since the chip flushes everything before
	 *  raising the MA interrupt for interrupted INPUT phases.
	 *  For DATA IN phase, we will check for the SWIDE later.
	 */
	if ((cmd & 7) != 1 && (cmd & 7) != 5) {
		u_char ss0, ss2;

		if (np->features & FE_DFBC)
			delta = INW (nc_dfbc);
		else {
			u32 dfifo;

			/*
			 * Read DFIFO, CTEST[4-6] using 1 PCI bus ownership.
			 */
			dfifo = INL(nc_dfifo);

			/*
			 *  Calculate remaining bytes in DMA fifo.
			 *  (CTEST5 = dfifo >> 16)
			 */
			if (dfifo & (DFS << 16))
				delta = ((((dfifo >> 8) & 0x300) |
				          (dfifo & 0xff)) - rest) & 0x3ff;
			else
				delta = ((dfifo & 0xff) - rest) & 0x7f;
		}

		/*
		 *  The data in the dma fifo has not been transferred to
		 *  the target -> add the amount to the rest
		 *  and clear the data.
		 *  Check the sstat2 register in case of wide transfer.
		 */
		rest += delta;
		ss0  = INB (nc_sstat0);
		if (ss0 & OLF) rest++;
		if (!(np->features & FE_C10))
			if (ss0 & ORF) rest++;
		if (cp && (cp->phys.select.sel_scntl3 & EWS)) {
			ss2 = INB (nc_sstat2);
			if (ss2 & OLF1) rest++;
			if (!(np->features & FE_C10))
				if (ss2 & ORF1) rest++;
		}

		/*
		 *  Clear fifos.
		 */
		OUTB (nc_ctest3, np->rv_ctest3 | CLF);	/* dma fifo  */
		OUTB (nc_stest3, TE|CSF);		/* scsi fifo */
	}

	/*
	 *  log the information
	 */
	if (DEBUG_FLAGS & (DEBUG_TINY|DEBUG_PHASE))
		printf ("P%x%x RL=%d D=%d ", cmd&7, INB(nc_sbcl)&7,
			(unsigned) rest, (unsigned) delta);

	/*
	 *  try to find the interrupted script command,
	 *  and the address at which to continue.
	 */
	vdsp	= NULL;
	nxtdsp	= 0;
	if	(dsp >  np->scripta_ba &&
		 dsp <= np->scripta_ba + np->scripta_sz) {
		vdsp = (u32 *)((char*)np->scripta0 + (dsp-np->scripta_ba-8));
		nxtdsp = dsp;
	}
	else if	(dsp >  np->scriptb_ba &&
		 dsp <= np->scriptb_ba + np->scriptb_sz) {
		vdsp = (u32 *)((char*)np->scriptb0 + (dsp-np->scriptb_ba-8));
		nxtdsp = dsp;
	}

	/*
	 *  log the information
	 */
	if (DEBUG_FLAGS & DEBUG_PHASE) {
		printf ("\nCP=%p DSP=%x NXT=%x VDSP=%p CMD=%x ",
			cp, (unsigned)dsp, (unsigned)nxtdsp, vdsp, cmd);
	}

	if (!vdsp) {
		printf ("%s: interrupted SCRIPT address not found.\n",
			sym_name (np));
		goto reset_all;
	}

	if (!cp) {
		printf ("%s: SCSI phase error fixup: CCB already dequeued.\n",
			sym_name (np));
		goto reset_all;
	}

	/*
	 *  get old startaddress and old length.
	 */
	oadr = scr_to_cpu(vdsp[1]);

	if (cmd & 0x10) {	/* Table indirect */
		tblp = (u32 *) ((char*) &cp->phys + oadr);
		olen = scr_to_cpu(tblp[0]);
		oadr = scr_to_cpu(tblp[1]);
	} else {
		tblp = (u32 *) 0;
		olen = scr_to_cpu(vdsp[0]) & 0xffffff;
	}

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		printf ("OCMD=%x\nTBLP=%p OLEN=%x OADR=%x\n",
			(unsigned) (scr_to_cpu(vdsp[0]) >> 24),
			tblp,
			(unsigned) olen,
			(unsigned) oadr);
	}

	/*
	 *  check cmd against assumed interrupted script command.
	 *  If dt data phase, the MOVE instruction hasn't bit 4 of
	 *  the phase.
	 */
	if (((cmd & 2) ? cmd : (cmd & ~4)) != (scr_to_cpu(vdsp[0]) >> 24)) {
		PRINT_ADDR(cp);
		printf ("internal error: cmd=%02x != %02x=(vdsp[0] >> 24)\n",
			(unsigned)cmd, (unsigned)scr_to_cpu(vdsp[0]) >> 24);

		goto reset_all;
	}

	/*
	 *  if old phase not dataphase, leave here.
	 */
	if (cmd & 2) {
		PRINT_ADDR(cp);
		printf ("phase change %x-%x %d@%08x resid=%d.\n",
			cmd&7, INB(nc_sbcl)&7, (unsigned)olen,
			(unsigned)oadr, (unsigned)rest);
		goto unexpected_phase;
	}

	/*
	 *  Choose the correct PM save area.
	 *
	 *  Look at the PM_SAVE SCRIPT if you want to understand
	 *  this stuff. The equivalent code is implemented in
	 *  SCRIPTS for the 895A, 896 and 1010 that are able to
	 *  handle PM from the SCRIPTS processor.
	 */
	hflags0 = INB (HF_PRT);
	hflags = hflags0;

	if (hflags & (HF_IN_PM0 | HF_IN_PM1 | HF_DP_SAVED)) {
		if (hflags & HF_IN_PM0)
			nxtdsp = scr_to_cpu(cp->phys.pm0.ret);
		else if	(hflags & HF_IN_PM1)
			nxtdsp = scr_to_cpu(cp->phys.pm1.ret);

		if (hflags & HF_DP_SAVED)
			hflags ^= HF_ACT_PM;
	}

	if (!(hflags & HF_ACT_PM)) {
		pm = &cp->phys.pm0;
		newcmd = SCRIPTA_BA (np, pm0_data);
	}
	else {
		pm = &cp->phys.pm1;
		newcmd = SCRIPTA_BA (np, pm1_data);
	}

	hflags &= ~(HF_IN_PM0 | HF_IN_PM1 | HF_DP_SAVED);
	if (hflags != hflags0)
		OUTB (HF_PRT, hflags);

	/*
	 *  fillin the phase mismatch context
	 */
	pm->sg.addr = cpu_to_scr(oadr + olen - rest);
	pm->sg.size = cpu_to_scr(rest);
	pm->ret     = cpu_to_scr(nxtdsp);

	/*
	 *  If we have a SWIDE,
	 *  - prepare the address to write the SWIDE from SCRIPTS,
	 *  - compute the SCRIPTS address to restart from,
	 *  - move current data pointer context by one byte.
	 */
	nxtdsp = SCRIPTA_BA (np, dispatch);
	if ((cmd & 7) == 1 && cp && (cp->phys.select.sel_scntl3 & EWS) &&
	    (INB (nc_scntl2) & WSR)) {
		u32 tmp;

		/*
		 *  Set up the table indirect for the MOVE
		 *  of the residual byte and adjust the data
		 *  pointer context.
		 */
		tmp = scr_to_cpu(pm->sg.addr);
		cp->phys.wresid.addr = cpu_to_scr(tmp);
		pm->sg.addr = cpu_to_scr(tmp + 1);
		tmp = scr_to_cpu(pm->sg.size);
		cp->phys.wresid.size = cpu_to_scr((tmp&0xff000000) | 1);
		pm->sg.size = cpu_to_scr(tmp - 1);

		/*
		 *  If only the residual byte is to be moved,
		 *  no PM context is needed.
		 */
		if ((tmp&0xffffff) == 1)
			newcmd = pm->ret;

		/*
		 *  Prepare the address of SCRIPTS that will
		 *  move the residual byte to memory.
		 */
		nxtdsp = SCRIPTB_BA (np, wsr_ma_helper);
	}

	if (DEBUG_FLAGS & DEBUG_PHASE) {
		PRINT_ADDR(cp);
		printf ("PM %x %x %x / %x %x %x.\n",
			hflags0, hflags, newcmd,
			(unsigned)scr_to_cpu(pm->sg.addr),
			(unsigned)scr_to_cpu(pm->sg.size),
			(unsigned)scr_to_cpu(pm->ret));
	}

	/*
	 *  Restart the SCRIPTS processor.
	 */
	OUTL (nc_temp, newcmd);
	OUTL_DSP (nxtdsp);
	return;

	/*
	 *  Unexpected phase changes that occurs when the current phase
	 *  is not a DATA IN or DATA OUT phase are due to error conditions.
	 *  Such event may only happen when the SCRIPTS is using a
	 *  multibyte SCSI MOVE.
	 *
	 *  Phase change		Some possible cause
	 *
	 *  COMMAND  --> MSG IN	SCSI parity error detected by target.
	 *  COMMAND  --> STATUS	Bad command or refused by target.
	 *  MSG OUT  --> MSG IN     Message rejected by target.
	 *  MSG OUT  --> COMMAND    Bogus target that discards extended
	 *  			negotiation messages.
	 *
	 *  The code below does not care of the new phase and so
	 *  trusts the target. Why to annoy it ?
	 *  If the interrupted phase is COMMAND phase, we restart at
	 *  dispatcher.
	 *  If a target does not get all the messages after selection,
	 *  the code assumes blindly that the target discards extended
	 *  messages and clears the negotiation status.
	 *  If the target does not want all our response to negotiation,
	 *  we force a SIR_NEGO_PROTO interrupt (it is a hack that avoids
	 *  bloat for such a should_not_happen situation).
	 *  In all other situation, we reset the BUS.
	 *  Are these assumptions reasonnable ? (Wait and see ...)
	 */
unexpected_phase:
	dsp -= 8;
	nxtdsp = 0;

	switch (cmd & 7) {
	case 2:	/* COMMAND phase */
		nxtdsp = SCRIPTA_BA (np, dispatch);
		break;
#if 0
	case 3:	/* STATUS  phase */
		nxtdsp = SCRIPTA_BA (np, dispatch);
		break;
#endif
	case 6:	/* MSG OUT phase */
		/*
		 *  If the device may want to use untagged when we want
		 *  tagged, we prepare an IDENTIFY without disc. granted,
		 *  since we will not be able to handle reselect.
		 *  Otherwise, we just don't care.
		 */
		if	(dsp == SCRIPTA_BA (np, send_ident)) {
			if (cp->tag != NO_TAG && olen - rest <= 3) {
				cp->host_status = HS_BUSY;
				np->msgout[0] = M_IDENTIFY | cp->lun;
				nxtdsp = SCRIPTB_BA (np, ident_break_atn);
			}
			else
				nxtdsp = SCRIPTB_BA (np, ident_break);
		}
		else if	(dsp == SCRIPTB_BA (np, send_wdtr) ||
			 dsp == SCRIPTB_BA (np, send_sdtr) ||
			 dsp == SCRIPTB_BA (np, send_ppr)) {
			nxtdsp = SCRIPTB_BA (np, nego_bad_phase);
		}
		break;
#if 0
	case 7:	/* MSG IN  phase */
		nxtdsp = SCRIPTA_BA (np, clrack);
		break;
#endif
	}

	if (nxtdsp) {
		OUTL_DSP (nxtdsp);
		return;
	}

reset_all:
	sym_start_reset(np);
}

/*
 *  Dequeue from the START queue all CCBs that match
 *  a given target/lun/task condition (-1 means all),
 *  and move them from the BUSY queue to the COMP queue
 *  with CAM_REQUEUE_REQ status condition.
 *  This function is used during error handling/recovery.
 *  It is called with SCRIPTS not running.
 */
static int
sym_dequeue_from_squeue(hcb_p np, int i, int target, int lun, int task)
{
	int j;
	ccb_p cp;

	/*
	 *  Make sure the starting index is within range.
	 */
	assert((i >= 0) && (i < 2*MAX_QUEUE));

	/*
	 *  Walk until end of START queue and dequeue every job
	 *  that matches the target/lun/task condition.
	 */
	j = i;
	while (i != np->squeueput) {
		cp = sym_ccb_from_dsa(np, scr_to_cpu(np->squeue[i]));
		assert(cp);
#ifdef SYM_CONF_IARB_SUPPORT
		/* Forget hints for IARB, they may be no longer relevant */
		cp->host_flags &= ~HF_HINT_IARB;
#endif
		if ((target == -1 || cp->target == target) &&
		    (lun    == -1 || cp->lun    == lun)    &&
		    (task   == -1 || cp->tag    == task)) {
			sym_set_cam_status(cp->cam_ccb, CAM_REQUEUE_REQ);
			sym_remque(&cp->link_ccbq);
			sym_insque_tail(&cp->link_ccbq, &np->comp_ccbq);
		}
		else {
			if (i != j)
				np->squeue[j] = np->squeue[i];
			if ((j += 2) >= MAX_QUEUE*2) j = 0;
		}
		if ((i += 2) >= MAX_QUEUE*2) i = 0;
	}
	if (i != j)		/* Copy back the idle task if needed */
		np->squeue[j] = np->squeue[i];
	np->squeueput = j;	/* Update our current start queue pointer */

	return (i - j) / 2;
}

/*
 *  Complete all CCBs queued to the COMP queue.
 *
 *  These CCBs are assumed:
 *  - Not to be referenced either by devices or
 *    SCRIPTS-related queues and datas.
 *  - To have to be completed with an error condition
 *    or requeued.
 *
 *  The device queue freeze count is incremented
 *  for each CCB that does not prevent this.
 *  This function is called when all CCBs involved
 *  in error handling/recovery have been reaped.
 */
static void
sym_flush_comp_queue(hcb_p np, int cam_status)
{
	SYM_QUEHEAD *qp;
	ccb_p cp;

	while ((qp = sym_remque_head(&np->comp_ccbq)) != NULL) {
		union ccb *ccb;
		cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		sym_insque_tail(&cp->link_ccbq, &np->busy_ccbq);
		/* Leave quiet CCBs waiting for resources */
		if (cp->host_status == HS_WAIT)
			continue;
		ccb = cp->cam_ccb;
		if (cam_status)
			sym_set_cam_status(ccb, cam_status);
		sym_freeze_cam_ccb(ccb);
		sym_xpt_done(np, ccb, cp);
		sym_free_ccb(np, cp);
	}
}

/*
 *  chip handler for bad SCSI status condition
 *
 *  In case of bad SCSI status, we unqueue all the tasks
 *  currently queued to the controller but not yet started
 *  and then restart the SCRIPTS processor immediately.
 *
 *  QUEUE FULL and BUSY conditions are handled the same way.
 *  Basically all the not yet started tasks are requeued in
 *  device queue and the queue is frozen until a completion.
 *
 *  For CHECK CONDITION and COMMAND TERMINATED status, we use
 *  the CCB of the failed command to prepare a REQUEST SENSE
 *  SCSI command and queue it to the controller queue.
 *
 *  SCRATCHA is assumed to have been loaded with STARTPOS
 *  before the SCRIPTS called the C code.
 */
static void sym_sir_bad_scsi_status(hcb_p np, ccb_p cp)
{
	tcb_p tp	= &np->target[cp->target];
	u32		startp;
	u_char		s_status = cp->ssss_status;
	u_char		h_flags  = cp->host_flags;
	int		msglen;
	int		nego;
	int		i;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  Compute the index of the next job to start from SCRIPTS.
	 */
	i = (INL (nc_scratcha) - np->squeue_ba) / 4;

	/*
	 *  The last CCB queued used for IARB hint may be
	 *  no longer relevant. Forget it.
	 */
#ifdef SYM_CONF_IARB_SUPPORT
	if (np->last_cp)
		np->last_cp = NULL;
#endif

	/*
	 *  Now deal with the SCSI status.
	 */
	switch(s_status) {
	case S_BUSY:
	case S_QUEUE_FULL:
		if (sym_verbose >= 2) {
			PRINT_ADDR(cp);
			printf (s_status == S_BUSY ? "BUSY" : "QUEUE FULL\n");
		}
	default:	/* S_INT, S_INT_COND_MET, S_CONFLICT */
		sym_complete_error (np, cp);
		break;
	case S_TERMINATED:
	case S_CHECK_COND:
		/*
		 *  If we get an SCSI error when requesting sense, give up.
		 */
		if (h_flags & HF_SENSE) {
			sym_complete_error (np, cp);
			break;
		}

		/*
		 *  Dequeue all queued CCBs for that device not yet started,
		 *  and restart the SCRIPTS processor immediately.
		 */
		(void) sym_dequeue_from_squeue(np, i, cp->target, cp->lun, -1);
		OUTL_DSP (SCRIPTA_BA (np, start));

 		/*
		 *  Save some info of the actual IO.
		 *  Compute the data residual.
		 */
		cp->sv_scsi_status = cp->ssss_status;
		cp->sv_xerr_status = cp->xerr_status;
		cp->sv_resid = sym_compute_residual(np, cp);

		/*
		 *  Prepare all needed data structures for
		 *  requesting sense data.
		 */

		/*
		 *  identify message
		 */
		cp->scsi_smsg2[0] = M_IDENTIFY | cp->lun;
		msglen = 1;

		/*
		 *  If we are currently using anything different from
		 *  async. 8 bit data transfers with that target,
		 *  start a negotiation, since the device may want
		 *  to report us a UNIT ATTENTION condition due to
		 *  a cause we currently ignore, and we donnot want
		 *  to be stuck with WIDE and/or SYNC data transfer.
		 *
		 *  cp->nego_status is filled by sym_prepare_nego().
		 */
		cp->nego_status = 0;
		nego = 0;
		if	(tp->tinfo.current.options & PPR_OPT_MASK)
			nego = NS_PPR;
		else if	(tp->tinfo.current.width != BUS_8_BIT)
			nego = NS_WIDE;
		else if (tp->tinfo.current.offset != 0)
			nego = NS_SYNC;
		if (nego)
			msglen +=
			sym_prepare_nego (np,cp, nego, &cp->scsi_smsg2[msglen]);
		/*
		 *  Message table indirect structure.
		 */
		cp->phys.smsg.addr	= cpu_to_scr(CCB_BA (cp, scsi_smsg2));
		cp->phys.smsg.size	= cpu_to_scr(msglen);

		/*
		 *  sense command
		 */
		cp->phys.cmd.addr	= cpu_to_scr(CCB_BA (cp, sensecmd));
		cp->phys.cmd.size	= cpu_to_scr(6);

		/*
		 *  patch requested size into sense command
		 */
		cp->sensecmd[0]		= 0x03;
		cp->sensecmd[1]		= cp->lun << 5;
		if (tp->tinfo.current.scsi_version > 2 || cp->lun > 7)
			cp->sensecmd[1]	= 0;
		cp->sensecmd[4]		= SYM_SNS_BBUF_LEN;
		cp->data_len		= SYM_SNS_BBUF_LEN;

		/*
		 *  sense data
		 */
		bzero(cp->sns_bbuf, SYM_SNS_BBUF_LEN);
		cp->phys.sense.addr	= cpu_to_scr(vtobus(cp->sns_bbuf));
		cp->phys.sense.size	= cpu_to_scr(SYM_SNS_BBUF_LEN);

		/*
		 *  requeue the command.
		 */
		startp = SCRIPTB_BA (np, sdata_in);

		cp->phys.head.savep	= cpu_to_scr(startp);
		cp->phys.head.goalp	= cpu_to_scr(startp + 16);
		cp->phys.head.lastp	= cpu_to_scr(startp);
		cp->startp	= cpu_to_scr(startp);

		cp->actualquirks = SYM_QUIRK_AUTOSAVE;
		cp->host_status	= cp->nego_status ? HS_NEGOTIATE : HS_BUSY;
		cp->ssss_status = S_ILLEGAL;
		cp->host_flags	= (HF_SENSE|HF_DATA_IN);
		cp->xerr_status = 0;
		cp->extra_bytes = 0;

		cp->phys.head.go.start = cpu_to_scr(SCRIPTA_BA (np, select));

		/*
		 *  Requeue the command.
		 */
		sym_put_start_queue(np, cp);

		/*
		 *  Give back to upper layer everything we have dequeued.
		 */
		sym_flush_comp_queue(np, 0);
		break;
	}
}

/*
 *  After a device has accepted some management message
 *  as BUS DEVICE RESET, ABORT TASK, etc ..., or when
 *  a device signals a UNIT ATTENTION condition, some
 *  tasks are thrown away by the device. We are required
 *  to reflect that on our tasks list since the device
 *  will never complete these tasks.
 *
 *  This function move from the BUSY queue to the COMP
 *  queue all disconnected CCBs for a given target that
 *  match the following criteria:
 *  - lun=-1  means any logical UNIT otherwise a given one.
 *  - task=-1 means any task, otherwise a given one.
 */
static int
sym_clear_tasks(hcb_p np, int cam_status, int target, int lun, int task)
{
	SYM_QUEHEAD qtmp, *qp;
	int i = 0;
	ccb_p cp;

	/*
	 *  Move the entire BUSY queue to our temporary queue.
	 */
	sym_que_init(&qtmp);
	sym_que_splice(&np->busy_ccbq, &qtmp);
	sym_que_init(&np->busy_ccbq);

	/*
	 *  Put all CCBs that matches our criteria into
	 *  the COMP queue and put back other ones into
	 *  the BUSY queue.
	 */
	while ((qp = sym_remque_head(&qtmp)) != NULL) {
		union ccb *ccb;
		cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		ccb = cp->cam_ccb;
		if (cp->host_status != HS_DISCONNECT ||
		    cp->target != target	     ||
		    (lun  != -1 && cp->lun != lun)   ||
		    (task != -1 &&
			(cp->tag != NO_TAG && cp->scsi_smsg[2] != task))) {
			sym_insque_tail(&cp->link_ccbq, &np->busy_ccbq);
			continue;
		}
		sym_insque_tail(&cp->link_ccbq, &np->comp_ccbq);

		/* Preserve the software timeout condition */
		if (sym_get_cam_status(ccb) != CAM_CMD_TIMEOUT)
			sym_set_cam_status(ccb, cam_status);
		++i;
#if 0
printf("XXXX TASK @%p CLEARED\n", cp);
#endif
	}
	return i;
}

/*
 *  chip handler for TASKS recovery
 *
 *  We cannot safely abort a command, while the SCRIPTS
 *  processor is running, since we just would be in race
 *  with it.
 *
 *  As long as we have tasks to abort, we keep the SEM
 *  bit set in the ISTAT. When this bit is set, the
 *  SCRIPTS processor interrupts (SIR_SCRIPT_STOPPED)
 *  each time it enters the scheduler.
 *
 *  If we have to reset a target, clear tasks of a unit,
 *  or to perform the abort of a disconnected job, we
 *  restart the SCRIPTS for selecting the target. Once
 *  selected, the SCRIPTS interrupts (SIR_TARGET_SELECTED).
 *  If it loses arbitration, the SCRIPTS will interrupt again
 *  the next time it will enter its scheduler, and so on ...
 *
 *  On SIR_TARGET_SELECTED, we scan for the more
 *  appropriate thing to do:
 *
 *  - If nothing, we just sent a M_ABORT message to the
 *    target to get rid of the useless SCSI bus ownership.
 *    According to the specs, no tasks shall be affected.
 *  - If the target is to be reset, we send it a M_RESET
 *    message.
 *  - If a logical UNIT is to be cleared , we send the
 *    IDENTIFY(lun) + M_ABORT.
 *  - If an untagged task is to be aborted, we send the
 *    IDENTIFY(lun) + M_ABORT.
 *  - If a tagged task is to be aborted, we send the
 *    IDENTIFY(lun) + task attributes + M_ABORT_TAG.
 *
 *  Once our 'kiss of death' :) message has been accepted
 *  by the target, the SCRIPTS interrupts again
 *  (SIR_ABORT_SENT). On this interrupt, we complete
 *  all the CCBs that should have been aborted by the
 *  target according to our message.
 */
static void sym_sir_task_recovery(hcb_p np, int num)
{
	SYM_QUEHEAD *qp;
	ccb_p cp;
	tcb_p tp;
	int target=-1, lun=-1, task;
	int i, k;

	switch(num) {
	/*
	 *  The SCRIPTS processor stopped before starting
	 *  the next command in order to allow us to perform
	 *  some task recovery.
	 */
	case SIR_SCRIPT_STOPPED:
		/*
		 *  Do we have any target to reset or unit to clear ?
		 */
		for (i = 0 ; i < SYM_CONF_MAX_TARGET ; i++) {
			tp = &np->target[i];
			if (tp->to_reset ||
			    (tp->lun0p && tp->lun0p->to_clear)) {
				target = i;
				break;
			}
			if (!tp->lunmp)
				continue;
			for (k = 1 ; k < SYM_CONF_MAX_LUN ; k++) {
				if (tp->lunmp[k] && tp->lunmp[k]->to_clear) {
					target	= i;
					break;
				}
			}
			if (target != -1)
				break;
		}

		/*
		 *  If not, walk the busy queue for any
		 *  disconnected CCB to be aborted.
		 */
		if (target == -1) {
			FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
				cp = sym_que_entry(qp,struct sym_ccb,link_ccbq);
				if (cp->host_status != HS_DISCONNECT)
					continue;
				if (cp->to_abort) {
					target = cp->target;
					break;
				}
			}
		}

		/*
		 *  If some target is to be selected,
		 *  prepare and start the selection.
		 */
		if (target != -1) {
			tp = &np->target[target];
			np->abrt_sel.sel_id	= target;
			np->abrt_sel.sel_scntl3 = tp->head.wval;
			np->abrt_sel.sel_sxfer  = tp->head.sval;
			OUTL(nc_dsa, np->hcb_ba);
			OUTL_DSP (SCRIPTB_BA (np, sel_for_abort));
			return;
		}

		/*
		 *  Now look for a CCB to abort that haven't started yet.
		 *  Btw, the SCRIPTS processor is still stopped, so
		 *  we are not in race.
		 */
		i = 0;
		cp = NULL;
		FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
			cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
			if (cp->host_status != HS_BUSY &&
			    cp->host_status != HS_NEGOTIATE)
				continue;
			if (!cp->to_abort)
				continue;
#ifdef SYM_CONF_IARB_SUPPORT
			/*
			 *    If we are using IMMEDIATE ARBITRATION, we donnot
			 *    want to cancel the last queued CCB, since the
			 *    SCRIPTS may have anticipated the selection.
			 */
			if (cp == np->last_cp) {
				cp->to_abort = 0;
				continue;
			}
#endif
			i = 1;	/* Means we have found some */
			break;
		}
		if (!i) {
			/*
			 *  We are done, so we donnot need
			 *  to synchronize with the SCRIPTS anylonger.
			 *  Remove the SEM flag from the ISTAT.
			 */
			np->istat_sem = 0;
			OUTB (nc_istat, SIGP);
			break;
		}
		/*
		 *  Compute index of next position in the start
		 *  queue the SCRIPTS intends to start and dequeue
		 *  all CCBs for that device that haven't been started.
		 */
		i = (INL (nc_scratcha) - np->squeue_ba) / 4;
		i = sym_dequeue_from_squeue(np, i, cp->target, cp->lun, -1);

		/*
		 *  Make sure at least our IO to abort has been dequeued.
		 */
		assert(i && sym_get_cam_status(cp->cam_ccb) == CAM_REQUEUE_REQ);

		/*
		 *  Keep track in cam status of the reason of the abort.
		 */
		if (cp->to_abort == 2)
			sym_set_cam_status(cp->cam_ccb, CAM_CMD_TIMEOUT);
		else
			sym_set_cam_status(cp->cam_ccb, CAM_REQ_ABORTED);

		/*
		 *  Complete with error everything that we have dequeued.
	 	 */
		sym_flush_comp_queue(np, 0);
		break;
	/*
	 *  The SCRIPTS processor has selected a target
	 *  we may have some manual recovery to perform for.
	 */
	case SIR_TARGET_SELECTED:
		target = (INB (nc_sdid) & 0xf);
		tp = &np->target[target];

		np->abrt_tbl.addr = cpu_to_scr(vtobus(np->abrt_msg));

		/*
		 *  If the target is to be reset, prepare a
		 *  M_RESET message and clear the to_reset flag
		 *  since we donnot expect this operation to fail.
		 */
		if (tp->to_reset) {
			np->abrt_msg[0] = M_RESET;
			np->abrt_tbl.size = 1;
			tp->to_reset = 0;
			break;
		}

		/*
		 *  Otherwise, look for some logical unit to be cleared.
		 */
		if (tp->lun0p && tp->lun0p->to_clear)
			lun = 0;
		else if (tp->lunmp) {
			for (k = 1 ; k < SYM_CONF_MAX_LUN ; k++) {
				if (tp->lunmp[k] && tp->lunmp[k]->to_clear) {
					lun = k;
					break;
				}
			}
		}

		/*
		 *  If a logical unit is to be cleared, prepare
		 *  an IDENTIFY(lun) + ABORT MESSAGE.
		 */
		if (lun != -1) {
			lcb_p lp = sym_lp(tp, lun);
			lp->to_clear = 0; /* We donnot expect to fail here */
			np->abrt_msg[0] = M_IDENTIFY | lun;
			np->abrt_msg[1] = M_ABORT;
			np->abrt_tbl.size = 2;
			break;
		}

		/*
		 *  Otherwise, look for some disconnected job to
		 *  abort for this target.
		 */
		i = 0;
		cp = NULL;
		FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
			cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
			if (cp->host_status != HS_DISCONNECT)
				continue;
			if (cp->target != target)
				continue;
			if (!cp->to_abort)
				continue;
			i = 1;	/* Means we have some */
			break;
		}

		/*
		 *  If we have none, probably since the device has
		 *  completed the command before we won abitration,
		 *  send a M_ABORT message without IDENTIFY.
		 *  According to the specs, the device must just
		 *  disconnect the BUS and not abort any task.
		 */
		if (!i) {
			np->abrt_msg[0] = M_ABORT;
			np->abrt_tbl.size = 1;
			break;
		}

		/*
		 *  We have some task to abort.
		 *  Set the IDENTIFY(lun)
		 */
		np->abrt_msg[0] = M_IDENTIFY | cp->lun;

		/*
		 *  If we want to abort an untagged command, we
		 *  will send an IDENTIFY + M_ABORT.
		 *  Otherwise (tagged command), we will send
		 *  an IDENTIFY + task attributes + ABORT TAG.
		 */
		if (cp->tag == NO_TAG) {
			np->abrt_msg[1] = M_ABORT;
			np->abrt_tbl.size = 2;
		}
		else {
			np->abrt_msg[1] = cp->scsi_smsg[1];
			np->abrt_msg[2] = cp->scsi_smsg[2];
			np->abrt_msg[3] = M_ABORT_TAG;
			np->abrt_tbl.size = 4;
		}
		/*
		 *  Keep track of software timeout condition, since the
		 *  peripheral driver may not count retries on abort
		 *  conditions not due to timeout.
		 */
		if (cp->to_abort == 2)
			sym_set_cam_status(cp->cam_ccb, CAM_CMD_TIMEOUT);
		cp->to_abort = 0; /* We donnot expect to fail here */
		break;

	/*
	 *  The target has accepted our message and switched
	 *  to BUS FREE phase as we expected.
	 */
	case SIR_ABORT_SENT:
		target = (INB (nc_sdid) & 0xf);
		tp = &np->target[target];

		/*
		**  If we didn't abort anything, leave here.
		*/
		if (np->abrt_msg[0] == M_ABORT)
			break;

		/*
		 *  If we sent a M_RESET, then a hardware reset has
		 *  been performed by the target.
		 *  - Reset everything to async 8 bit
		 *  - Tell ourself to negotiate next time :-)
		 *  - Prepare to clear all disconnected CCBs for
		 *    this target from our task list (lun=task=-1)
		 */
		lun = -1;
		task = -1;
		if (np->abrt_msg[0] == M_RESET) {
			tp->head.sval = 0;
			tp->head.wval = np->rv_scntl3;
			tp->head.uval = 0;
			tp->tinfo.current.period = 0;
			tp->tinfo.current.offset = 0;
			tp->tinfo.current.width  = BUS_8_BIT;
			tp->tinfo.current.options = 0;
		}

		/*
		 *  Otherwise, check for the LUN and TASK(s)
		 *  concerned by the cancellation.
		 *  If it is not ABORT_TAG then it is CLEAR_QUEUE
		 *  or an ABORT message :-)
		 */
		else {
			lun = np->abrt_msg[0] & 0x3f;
			if (np->abrt_msg[1] == M_ABORT_TAG)
				task = np->abrt_msg[2];
		}

		/*
		 *  Complete all the CCBs the device should have
		 *  aborted due to our 'kiss of death' message.
		 */
		i = (INL (nc_scratcha) - np->squeue_ba) / 4;
		(void) sym_dequeue_from_squeue(np, i, target, lun, -1);
		(void) sym_clear_tasks(np, CAM_REQ_ABORTED, target, lun, task);
		sym_flush_comp_queue(np, 0);

		/*
		 *  If we sent a BDR, make uper layer aware of that.
		 */
		if (np->abrt_msg[0] == M_RESET)
			xpt_async(AC_SENT_BDR, np->path, NULL);
		break;
	}

	/*
	 *  Print to the log the message we intend to send.
	 */
	if (num == SIR_TARGET_SELECTED) {
		PRINT_TARGET(np, target);
		sym_printl_hex("control msgout:", np->abrt_msg,
			      np->abrt_tbl.size);
		np->abrt_tbl.size = cpu_to_scr(np->abrt_tbl.size);
	}

	/*
	 *  Let the SCRIPTS processor continue.
	 */
	OUTONB_STD ();
}

/*
 *  Gerard's alchemy:) that deals with with the data
 *  pointer for both MDP and the residual calculation.
 *
 *  I didn't want to bloat the code by more than 200
 *  lignes for the handling of both MDP and the residual.
 *  This has been achieved by using a data pointer
 *  representation consisting in an index in the data
 *  array (dp_sg) and a negative offset (dp_ofs) that
 *  have the following meaning:
 *
 *  - dp_sg = SYM_CONF_MAX_SG
 *    we are at the end of the data script.
 *  - dp_sg < SYM_CONF_MAX_SG
 *    dp_sg points to the next entry of the scatter array
 *    we want to transfer.
 *  - dp_ofs < 0
 *    dp_ofs represents the residual of bytes of the
 *    previous entry scatter entry we will send first.
 *  - dp_ofs = 0
 *    no residual to send first.
 *
 *  The function sym_evaluate_dp() accepts an arbitray
 *  offset (basically from the MDP message) and returns
 *  the corresponding values of dp_sg and dp_ofs.
 */
static int sym_evaluate_dp(hcb_p np, ccb_p cp, u32 scr, int *ofs)
{
	u32	dp_scr;
	int	dp_ofs, dp_sg, dp_sgmin;
	int	tmp;
	struct sym_pmc *pm;

	/*
	 *  Compute the resulted data pointer in term of a script
	 *  address within some DATA script and a signed byte offset.
	 */
	dp_scr = scr;
	dp_ofs = *ofs;
	if	(dp_scr == SCRIPTA_BA (np, pm0_data))
		pm = &cp->phys.pm0;
	else if (dp_scr == SCRIPTA_BA (np, pm1_data))
		pm = &cp->phys.pm1;
	else
		pm = NULL;

	if (pm) {
		dp_scr  = scr_to_cpu(pm->ret);
		dp_ofs -= scr_to_cpu(pm->sg.size);
	}

	/*
	 *  If we are auto-sensing, then we are done.
	 */
	if (cp->host_flags & HF_SENSE) {
		*ofs = dp_ofs;
		return 0;
	}

	/*
	 *  Deduce the index of the sg entry.
	 *  Keep track of the index of the first valid entry.
	 *  If result is dp_sg = SYM_CONF_MAX_SG, then we are at the
	 *  end of the data.
	 */
	tmp = scr_to_cpu(cp->phys.head.goalp);
	dp_sg = SYM_CONF_MAX_SG;
	if (dp_scr != tmp)
		dp_sg -= (tmp - 8 - (int)dp_scr) / (2*4);
	dp_sgmin = SYM_CONF_MAX_SG - cp->segments;

	/*
	 *  Move to the sg entry the data pointer belongs to.
	 *
	 *  If we are inside the data area, we expect result to be:
	 *
	 *  Either,
	 *      dp_ofs = 0 and dp_sg is the index of the sg entry
	 *      the data pointer belongs to (or the end of the data)
	 *  Or,
	 *      dp_ofs < 0 and dp_sg is the index of the sg entry
	 *      the data pointer belongs to + 1.
	 */
	if (dp_ofs < 0) {
		int n;
		while (dp_sg > dp_sgmin) {
			--dp_sg;
			tmp = scr_to_cpu(cp->phys.data[dp_sg].size);
			n = dp_ofs + (tmp & 0xffffff);
			if (n > 0) {
				++dp_sg;
				break;
			}
			dp_ofs = n;
		}
	}
	else if (dp_ofs > 0) {
		while (dp_sg < SYM_CONF_MAX_SG) {
			tmp = scr_to_cpu(cp->phys.data[dp_sg].size);
			dp_ofs -= (tmp & 0xffffff);
			++dp_sg;
			if (dp_ofs <= 0)
				break;
		}
	}

	/*
	 *  Make sure the data pointer is inside the data area.
	 *  If not, return some error.
	 */
	if	(dp_sg < dp_sgmin || (dp_sg == dp_sgmin && dp_ofs < 0))
		goto out_err;
	else if	(dp_sg > SYM_CONF_MAX_SG ||
		 (dp_sg == SYM_CONF_MAX_SG && dp_ofs > 0))
		goto out_err;

	/*
	 *  Save the extreme pointer if needed.
	 */
	if (dp_sg > cp->ext_sg ||
            (dp_sg == cp->ext_sg && dp_ofs > cp->ext_ofs)) {
		cp->ext_sg  = dp_sg;
		cp->ext_ofs = dp_ofs;
	}

	/*
	 *  Return data.
	 */
	*ofs = dp_ofs;
	return dp_sg;

out_err:
	return -1;
}

/*
 *  chip handler for MODIFY DATA POINTER MESSAGE
 *
 *  We also call this function on IGNORE WIDE RESIDUE
 *  messages that do not match a SWIDE full condition.
 *  Btw, we assume in that situation that such a message
 *  is equivalent to a MODIFY DATA POINTER (offset=-1).
 */
static void sym_modify_dp(hcb_p np, ccb_p cp, int ofs)
{
	int dp_ofs	= ofs;
	u32	dp_scr	= INL (nc_temp);
	u32	dp_ret;
	u32	tmp;
	u_char	hflags;
	int	dp_sg;
	struct	sym_pmc *pm;

	/*
	 *  Not supported for auto-sense.
	 */
	if (cp->host_flags & HF_SENSE)
		goto out_reject;

	/*
	 *  Apply our alchemy:) (see comments in sym_evaluate_dp()),
	 *  to the resulted data pointer.
	 */
	dp_sg = sym_evaluate_dp(np, cp, dp_scr, &dp_ofs);
	if (dp_sg < 0)
		goto out_reject;

	/*
	 *  And our alchemy:) allows to easily calculate the data
	 *  script address we want to return for the next data phase.
	 */
	dp_ret = cpu_to_scr(cp->phys.head.goalp);
	dp_ret = dp_ret - 8 - (SYM_CONF_MAX_SG - dp_sg) * (2*4);

	/*
	 *  If offset / scatter entry is zero we donnot need
	 *  a context for the new current data pointer.
	 */
	if (dp_ofs == 0) {
		dp_scr = dp_ret;
		goto out_ok;
	}

	/*
	 *  Get a context for the new current data pointer.
	 */
	hflags = INB (HF_PRT);

	if (hflags & HF_DP_SAVED)
		hflags ^= HF_ACT_PM;

	if (!(hflags & HF_ACT_PM)) {
		pm  = &cp->phys.pm0;
		dp_scr = SCRIPTA_BA (np, pm0_data);
	}
	else {
		pm = &cp->phys.pm1;
		dp_scr = SCRIPTA_BA (np, pm1_data);
	}

	hflags &= ~(HF_DP_SAVED);

	OUTB (HF_PRT, hflags);

	/*
	 *  Set up the new current data pointer.
	 *  ofs < 0 there, and for the next data phase, we
	 *  want to transfer part of the data of the sg entry
	 *  corresponding to index dp_sg-1 prior to returning
	 *  to the main data script.
	 */
	pm->ret = cpu_to_scr(dp_ret);
	tmp  = scr_to_cpu(cp->phys.data[dp_sg-1].addr);
	tmp += scr_to_cpu(cp->phys.data[dp_sg-1].size) + dp_ofs;
	pm->sg.addr = cpu_to_scr(tmp);
	pm->sg.size = cpu_to_scr(-dp_ofs);

out_ok:
	OUTL (nc_temp, dp_scr);
	OUTL_DSP (SCRIPTA_BA (np, clrack));
	return;

out_reject:
	OUTL_DSP (SCRIPTB_BA (np, msg_bad));
}

/*
 *  chip calculation of the data residual.
 *
 *  As I used to say, the requirement of data residual
 *  in SCSI is broken, useless and cannot be achieved
 *  without huge complexity.
 *  But most OSes and even the official CAM require it.
 *  When stupidity happens to be so widely spread inside
 *  a community, it gets hard to convince.
 *
 *  Anyway, I don't care, since I am not going to use
 *  any software that considers this data residual as
 *  a relevant information. :)
 */
static int sym_compute_residual(hcb_p np, ccb_p cp)
{
	int dp_sg, dp_sgmin, resid = 0;
	int dp_ofs = 0;

	/*
	 *  Check for some data lost or just thrown away.
	 *  We are not required to be quite accurate in this
	 *  situation. Btw, if we are odd for output and the
	 *  device claims some more data, it may well happen
	 *  than our residual be zero. :-)
	 */
	if (cp->xerr_status & (XE_EXTRA_DATA|XE_SODL_UNRUN|XE_SWIDE_OVRUN)) {
		if (cp->xerr_status & XE_EXTRA_DATA)
			resid -= cp->extra_bytes;
		if (cp->xerr_status & XE_SODL_UNRUN)
			++resid;
		if (cp->xerr_status & XE_SWIDE_OVRUN)
			--resid;
	}

	/*
	 *  If all data has been transferred,
	 *  there is no residual.
	 */
	if (cp->phys.head.lastp == cp->phys.head.goalp)
		return resid;

	/*
	 *  If no data transfer occurs, or if the data
	 *  pointer is weird, return full residual.
	 */
	if (cp->startp == cp->phys.head.lastp ||
	    sym_evaluate_dp(np, cp, scr_to_cpu(cp->phys.head.lastp),
			    &dp_ofs) < 0) {
		return cp->data_len;
	}

	/*
	 *  If we were auto-sensing, then we are done.
	 */
	if (cp->host_flags & HF_SENSE) {
		return -dp_ofs;
	}

	/*
	 *  We are now full comfortable in the computation
	 *  of the data residual (2's complement).
	 */
	dp_sgmin = SYM_CONF_MAX_SG - cp->segments;
	resid = -cp->ext_ofs;
	for (dp_sg = cp->ext_sg; dp_sg < SYM_CONF_MAX_SG; ++dp_sg) {
		u_int tmp = scr_to_cpu(cp->phys.data[dp_sg].size);
		resid += (tmp & 0xffffff);
	}

	/*
	 *  Hopefully, the result is not too wrong.
	 */
	return resid;
}

/*
 *  Print out the content of a SCSI message.
 */
static int sym_show_msg (u_char * msg)
{
	u_char i;
	printf ("%x",*msg);
	if (*msg==M_EXTENDED) {
		for (i=1;i<8;i++) {
			if (i-1>msg[1]) break;
			printf ("-%x",msg[i]);
		}
		return (i+1);
	} else if ((*msg & 0xf0) == 0x20) {
		printf ("-%x",msg[1]);
		return (2);
	}
	return (1);
}

static void sym_print_msg (ccb_p cp, char *label, u_char *msg)
{
	PRINT_ADDR(cp);
	if (label)
		printf ("%s: ", label);

	(void) sym_show_msg (msg);
	printf (".\n");
}

/*
 *  Negotiation for WIDE and SYNCHRONOUS DATA TRANSFER.
 *
 *  When we try to negotiate, we append the negotiation message
 *  to the identify and (maybe) simple tag message.
 *  The host status field is set to HS_NEGOTIATE to mark this
 *  situation.
 *
 *  If the target doesn't answer this message immediately
 *  (as required by the standard), the SIR_NEGO_FAILED interrupt
 *  will be raised eventually.
 *  The handler removes the HS_NEGOTIATE status, and sets the
 *  negotiated value to the default (async / nowide).
 *
 *  If we receive a matching answer immediately, we check it
 *  for validity, and set the values.
 *
 *  If we receive a Reject message immediately, we assume the
 *  negotiation has failed, and fall back to standard values.
 *
 *  If we receive a negotiation message while not in HS_NEGOTIATE
 *  state, it's a target initiated negotiation. We prepare a
 *  (hopefully) valid answer, set our parameters, and send back
 *  this answer to the target.
 *
 *  If the target doesn't fetch the answer (no message out phase),
 *  we assume the negotiation has failed, and fall back to default
 *  settings (SIR_NEGO_PROTO interrupt).
 *
 *  When we set the values, we adjust them in all ccbs belonging
 *  to this target, in the controller's register, and in the "phys"
 *  field of the controller's struct sym_hcb.
 */

/*
 *  chip handler for SYNCHRONOUS DATA TRANSFER REQUEST (SDTR) message.
 */
static void sym_sync_nego(hcb_p np, tcb_p tp, ccb_p cp)
{
	u_char	chg, ofs, per, fak, div;
	int	req = 1;

	/*
	 *  Synchronous request message received.
	 */
	if (DEBUG_FLAGS & DEBUG_NEGO) {
		sym_print_msg(cp, "sync msgin", np->msgin);
	}

	/*
	 * request or answer ?
	 */
	if (INB (HS_PRT) == HS_NEGOTIATE) {
		OUTB (HS_PRT, HS_BUSY);
		if (cp->nego_status && cp->nego_status != NS_SYNC)
			goto reject_it;
		req = 0;
	}

	/*
	 *  get requested values.
	 */
	chg = 0;
	per = np->msgin[3];
	ofs = np->msgin[4];

	/*
	 *  check values against our limits.
	 */
	if (ofs) {
		if (ofs > np->maxoffs)
			{chg = 1; ofs = np->maxoffs;}
		if (req) {
			if (ofs > tp->tinfo.user.offset)
				{chg = 1; ofs = tp->tinfo.user.offset;}
		}
	}

	if (ofs) {
		if (per < np->minsync)
			{chg = 1; per = np->minsync;}
		if (req) {
			if (per < tp->tinfo.user.period)
				{chg = 1; per = tp->tinfo.user.period;}
		}
	}

	div = fak = 0;
	if (ofs && sym_getsync(np, 0, per, &div, &fak) < 0)
		goto reject_it;

	if (DEBUG_FLAGS & DEBUG_NEGO) {
		PRINT_ADDR(cp);
		printf ("sdtr: ofs=%d per=%d div=%d fak=%d chg=%d.\n",
			ofs, per, div, fak, chg);
	}

	/*
	 *  This was an answer message
	 */
	if (req == 0) {
		if (chg) 	/* Answer wasn't acceptable. */
			goto reject_it;
		sym_setsync (np, cp, ofs, per, div, fak);
		OUTL_DSP (SCRIPTA_BA (np, clrack));
		return;
	}

	/*
	 *  It was a request. Set value and
	 *  prepare an answer message
	 */
	sym_setsync (np, cp, ofs, per, div, fak);

	np->msgout[0] = M_EXTENDED;
	np->msgout[1] = 3;
	np->msgout[2] = M_X_SYNC_REQ;
	np->msgout[3] = per;
	np->msgout[4] = ofs;

	cp->nego_status = NS_SYNC;

	if (DEBUG_FLAGS & DEBUG_NEGO) {
		sym_print_msg(cp, "sync msgout", np->msgout);
	}

	np->msgin [0] = M_NOOP;

	OUTL_DSP (SCRIPTB_BA (np, sdtr_resp));
	return;
reject_it:
	sym_setsync (np, cp, 0, 0, 0, 0);
	OUTL_DSP (SCRIPTB_BA (np, msg_bad));
}

/*
 *  chip handler for PARALLEL PROTOCOL REQUEST (PPR) message.
 */
static void sym_ppr_nego(hcb_p np, tcb_p tp, ccb_p cp)
{
	u_char	chg, ofs, per, fak, dt, div, wide;
	int	req = 1;

	/*
	 * Synchronous request message received.
	 */
	if (DEBUG_FLAGS & DEBUG_NEGO) {
		sym_print_msg(cp, "ppr msgin", np->msgin);
	}

	/*
	 *  get requested values.
	 */
	chg  = 0;
	per  = np->msgin[3];
	ofs  = np->msgin[5];
	wide = np->msgin[6];
	dt   = np->msgin[7] & PPR_OPT_DT;

	/*
	 * request or answer ?
	 */
	if (INB (HS_PRT) == HS_NEGOTIATE) {
		OUTB (HS_PRT, HS_BUSY);
		if (cp->nego_status && cp->nego_status != NS_PPR)
			goto reject_it;
		req = 0;
	}

	/*
	 *  check values against our limits.
	 */
	if (wide > np->maxwide)
		{chg = 1; wide = np->maxwide;}
	if (!wide || !(np->features & FE_ULTRA3))
		dt &= ~PPR_OPT_DT;
	if (req) {
		if (wide > tp->tinfo.user.width)
			{chg = 1; wide = tp->tinfo.user.width;}
	}

	if (!(np->features & FE_U3EN))	/* Broken U3EN bit not supported */
		dt &= ~PPR_OPT_DT;

	if (dt != (np->msgin[7] & PPR_OPT_MASK)) chg = 1;

	if (ofs) {
		if (dt) {
			if (ofs > np->maxoffs_dt)
				{chg = 1; ofs = np->maxoffs_dt;}
		}
		else if (ofs > np->maxoffs)
			{chg = 1; ofs = np->maxoffs;}
		if (req) {
			if (ofs > tp->tinfo.user.offset)
				{chg = 1; ofs = tp->tinfo.user.offset;}
		}
	}

	if (ofs) {
		if (dt) {
			if (per < np->minsync_dt)
				{chg = 1; per = np->minsync_dt;}
		}
		else if (per < np->minsync)
			{chg = 1; per = np->minsync;}
		if (req) {
			if (per < tp->tinfo.user.period)
				{chg = 1; per = tp->tinfo.user.period;}
		}
	}

	div = fak = 0;
	if (ofs && sym_getsync(np, dt, per, &div, &fak) < 0)
		goto reject_it;

	if (DEBUG_FLAGS & DEBUG_NEGO) {
		PRINT_ADDR(cp);
		printf ("ppr: "
			"dt=%x ofs=%d per=%d wide=%d div=%d fak=%d chg=%d.\n",
			dt, ofs, per, wide, div, fak, chg);
	}

	/*
	 *  It was an answer.
	 */
	if (req == 0) {
		if (chg) 	/* Answer wasn't acceptable */
			goto reject_it;
		sym_setpprot (np, cp, dt, ofs, per, wide, div, fak);
		OUTL_DSP (SCRIPTA_BA (np, clrack));
		return;
	}

	/*
	 *  It was a request. Set value and
	 *  prepare an answer message
	 */
	sym_setpprot (np, cp, dt, ofs, per, wide, div, fak);

	np->msgout[0] = M_EXTENDED;
	np->msgout[1] = 6;
	np->msgout[2] = M_X_PPR_REQ;
	np->msgout[3] = per;
	np->msgout[4] = 0;
	np->msgout[5] = ofs;
	np->msgout[6] = wide;
	np->msgout[7] = dt;

	cp->nego_status = NS_PPR;

	if (DEBUG_FLAGS & DEBUG_NEGO) {
		sym_print_msg(cp, "ppr msgout", np->msgout);
	}

	np->msgin [0] = M_NOOP;

	OUTL_DSP (SCRIPTB_BA (np, ppr_resp));
	return;
reject_it:
	sym_setpprot (np, cp, 0, 0, 0, 0, 0, 0);
	OUTL_DSP (SCRIPTB_BA (np, msg_bad));
	/*
	 *  If it was a device response that should result in
	 *  ST, we may want to try a legacy negotiation later.
	 */
	if (!req && !dt) {
		tp->tinfo.goal.options = 0;
		tp->tinfo.goal.width   = wide;
		tp->tinfo.goal.period  = per;
		tp->tinfo.goal.offset  = ofs;
	}
}

/*
 *  chip handler for WIDE DATA TRANSFER REQUEST (WDTR) message.
 */
static void sym_wide_nego(hcb_p np, tcb_p tp, ccb_p cp)
{
	u_char	chg, wide;
	int	req = 1;

	/*
	 *  Wide request message received.
	 */
	if (DEBUG_FLAGS & DEBUG_NEGO) {
		sym_print_msg(cp, "wide msgin", np->msgin);
	}

	/*
	 * Is it a request from the device?
	 */
	if (INB (HS_PRT) == HS_NEGOTIATE) {
		OUTB (HS_PRT, HS_BUSY);
		if (cp->nego_status && cp->nego_status != NS_WIDE)
			goto reject_it;
		req = 0;
	}

	/*
	 *  get requested values.
	 */
	chg  = 0;
	wide = np->msgin[3];

	/*
	 *  check values against driver limits.
	 */
	if (wide > np->maxwide)
		{chg = 1; wide = np->maxwide;}
	if (req) {
		if (wide > tp->tinfo.user.width)
			{chg = 1; wide = tp->tinfo.user.width;}
	}

	if (DEBUG_FLAGS & DEBUG_NEGO) {
		PRINT_ADDR(cp);
		printf ("wdtr: wide=%d chg=%d.\n", wide, chg);
	}

	/*
	 * This was an answer message
	 */
	if (req == 0) {
		if (chg)	/*  Answer wasn't acceptable. */
			goto reject_it;
		sym_setwide (np, cp, wide);

		/*
		 * Negotiate for SYNC immediately after WIDE response.
		 * This allows to negotiate for both WIDE and SYNC on
		 * a single SCSI command (Suggested by Justin Gibbs).
		 */
		if (tp->tinfo.goal.offset) {
			np->msgout[0] = M_EXTENDED;
			np->msgout[1] = 3;
			np->msgout[2] = M_X_SYNC_REQ;
			np->msgout[3] = tp->tinfo.goal.period;
			np->msgout[4] = tp->tinfo.goal.offset;

			if (DEBUG_FLAGS & DEBUG_NEGO) {
				sym_print_msg(cp, "sync msgout", np->msgout);
			}

			cp->nego_status = NS_SYNC;
			OUTB (HS_PRT, HS_NEGOTIATE);
			OUTL_DSP (SCRIPTB_BA (np, sdtr_resp));
			return;
		}

		OUTL_DSP (SCRIPTA_BA (np, clrack));
		return;
	}

	/*
	 *  It was a request, set value and
	 *  prepare an answer message
	 */
	sym_setwide (np, cp, wide);

	np->msgout[0] = M_EXTENDED;
	np->msgout[1] = 2;
	np->msgout[2] = M_X_WIDE_REQ;
	np->msgout[3] = wide;

	np->msgin [0] = M_NOOP;

	cp->nego_status = NS_WIDE;

	if (DEBUG_FLAGS & DEBUG_NEGO) {
		sym_print_msg(cp, "wide msgout", np->msgout);
	}

	OUTL_DSP (SCRIPTB_BA (np, wdtr_resp));
	return;
reject_it:
	OUTL_DSP (SCRIPTB_BA (np, msg_bad));
}

/*
 *  Reset SYNC or WIDE to default settings.
 *
 *  Called when a negotiation does not succeed either
 *  on rejection or on protocol error.
 *
 *  If it was a PPR that made problems, we may want to
 *  try a legacy negotiation later.
 */
static void sym_nego_default(hcb_p np, tcb_p tp, ccb_p cp)
{
	/*
	 *  any error in negotiation:
	 *  fall back to default mode.
	 */
	switch (cp->nego_status) {
	case NS_PPR:
#if 0
		sym_setpprot (np, cp, 0, 0, 0, 0, 0, 0);
#else
		tp->tinfo.goal.options = 0;
		if (tp->tinfo.goal.period < np->minsync)
			tp->tinfo.goal.period = np->minsync;
		if (tp->tinfo.goal.offset > np->maxoffs)
			tp->tinfo.goal.offset = np->maxoffs;
#endif
		break;
	case NS_SYNC:
		sym_setsync (np, cp, 0, 0, 0, 0);
		break;
	case NS_WIDE:
		sym_setwide (np, cp, 0);
		break;
	}
	np->msgin [0] = M_NOOP;
	np->msgout[0] = M_NOOP;
	cp->nego_status = 0;
}

/*
 *  chip handler for MESSAGE REJECT received in response to
 *  a WIDE or SYNCHRONOUS negotiation.
 */
static void sym_nego_rejected(hcb_p np, tcb_p tp, ccb_p cp)
{
	sym_nego_default(np, tp, cp);
	OUTB (HS_PRT, HS_BUSY);
}

/*
 *  chip exception handler for programmed interrupts.
 */
static void sym_int_sir (hcb_p np)
{
	u_char	num	= INB (nc_dsps);
	u32	dsa	= INL (nc_dsa);
	ccb_p	cp	= sym_ccb_from_dsa(np, dsa);
	u_char	target	= INB (nc_sdid) & 0x0f;
	tcb_p	tp	= &np->target[target];
	int	tmp;

	SYM_LOCK_ASSERT(MA_OWNED);

	if (DEBUG_FLAGS & DEBUG_TINY) printf ("I#%d", num);

	switch (num) {
	/*
	 *  Command has been completed with error condition
	 *  or has been auto-sensed.
	 */
	case SIR_COMPLETE_ERROR:
		sym_complete_error(np, cp);
		return;
	/*
	 *  The C code is currently trying to recover from something.
	 *  Typically, user want to abort some command.
	 */
	case SIR_SCRIPT_STOPPED:
	case SIR_TARGET_SELECTED:
	case SIR_ABORT_SENT:
		sym_sir_task_recovery(np, num);
		return;
	/*
	 *  The device didn't go to MSG OUT phase after having
	 *  been selected with ATN. We donnot want to handle
	 *  that.
	 */
	case SIR_SEL_ATN_NO_MSG_OUT:
		printf ("%s:%d: No MSG OUT phase after selection with ATN.\n",
			sym_name (np), target);
		goto out_stuck;
	/*
	 *  The device didn't switch to MSG IN phase after
	 *  having reseleted the initiator.
	 */
	case SIR_RESEL_NO_MSG_IN:
		printf ("%s:%d: No MSG IN phase after reselection.\n",
			sym_name (np), target);
		goto out_stuck;
	/*
	 *  After reselection, the device sent a message that wasn't
	 *  an IDENTIFY.
	 */
	case SIR_RESEL_NO_IDENTIFY:
		printf ("%s:%d: No IDENTIFY after reselection.\n",
			sym_name (np), target);
		goto out_stuck;
	/*
	 *  The device reselected a LUN we donnot know about.
	 */
	case SIR_RESEL_BAD_LUN:
		np->msgout[0] = M_RESET;
		goto out;
	/*
	 *  The device reselected for an untagged nexus and we
	 *  haven't any.
	 */
	case SIR_RESEL_BAD_I_T_L:
		np->msgout[0] = M_ABORT;
		goto out;
	/*
	 *  The device reselected for a tagged nexus that we donnot
	 *  have.
	 */
	case SIR_RESEL_BAD_I_T_L_Q:
		np->msgout[0] = M_ABORT_TAG;
		goto out;
	/*
	 *  The SCRIPTS let us know that the device has grabbed
	 *  our message and will abort the job.
	 */
	case SIR_RESEL_ABORTED:
		np->lastmsg = np->msgout[0];
		np->msgout[0] = M_NOOP;
		printf ("%s:%d: message %x sent on bad reselection.\n",
			sym_name (np), target, np->lastmsg);
		goto out;
	/*
	 *  The SCRIPTS let us know that a message has been
	 *  successfully sent to the device.
	 */
	case SIR_MSG_OUT_DONE:
		np->lastmsg = np->msgout[0];
		np->msgout[0] = M_NOOP;
		/* Should we really care of that */
		if (np->lastmsg == M_PARITY || np->lastmsg == M_ID_ERROR) {
			if (cp) {
				cp->xerr_status &= ~XE_PARITY_ERR;
				if (!cp->xerr_status)
					OUTOFFB (HF_PRT, HF_EXT_ERR);
			}
		}
		goto out;
	/*
	 *  The device didn't send a GOOD SCSI status.
	 *  We may have some work to do prior to allow
	 *  the SCRIPTS processor to continue.
	 */
	case SIR_BAD_SCSI_STATUS:
		if (!cp)
			goto out;
		sym_sir_bad_scsi_status(np, cp);
		return;
	/*
	 *  We are asked by the SCRIPTS to prepare a
	 *  REJECT message.
	 */
	case SIR_REJECT_TO_SEND:
		sym_print_msg(cp, "M_REJECT to send for ", np->msgin);
		np->msgout[0] = M_REJECT;
		goto out;
	/*
	 *  We have been ODD at the end of a DATA IN
	 *  transfer and the device didn't send a
	 *  IGNORE WIDE RESIDUE message.
	 *  It is a data overrun condition.
	 */
	case SIR_SWIDE_OVERRUN:
		if (cp) {
			OUTONB (HF_PRT, HF_EXT_ERR);
			cp->xerr_status |= XE_SWIDE_OVRUN;
		}
		goto out;
	/*
	 *  We have been ODD at the end of a DATA OUT
	 *  transfer.
	 *  It is a data underrun condition.
	 */
	case SIR_SODL_UNDERRUN:
		if (cp) {
			OUTONB (HF_PRT, HF_EXT_ERR);
			cp->xerr_status |= XE_SODL_UNRUN;
		}
		goto out;
	/*
	 *  The device wants us to transfer more data than
	 *  expected or in the wrong direction.
	 *  The number of extra bytes is in scratcha.
	 *  It is a data overrun condition.
	 */
	case SIR_DATA_OVERRUN:
		if (cp) {
			OUTONB (HF_PRT, HF_EXT_ERR);
			cp->xerr_status |= XE_EXTRA_DATA;
			cp->extra_bytes += INL (nc_scratcha);
		}
		goto out;
	/*
	 *  The device switched to an illegal phase (4/5).
	 */
	case SIR_BAD_PHASE:
		if (cp) {
			OUTONB (HF_PRT, HF_EXT_ERR);
			cp->xerr_status |= XE_BAD_PHASE;
		}
		goto out;
	/*
	 *  We received a message.
	 */
	case SIR_MSG_RECEIVED:
		if (!cp)
			goto out_stuck;
		switch (np->msgin [0]) {
		/*
		 *  We received an extended message.
		 *  We handle MODIFY DATA POINTER, SDTR, WDTR
		 *  and reject all other extended messages.
		 */
		case M_EXTENDED:
			switch (np->msgin [2]) {
			case M_X_MODIFY_DP:
				if (DEBUG_FLAGS & DEBUG_POINTER)
					sym_print_msg(cp,"modify DP",np->msgin);
				tmp = (np->msgin[3]<<24) + (np->msgin[4]<<16) +
				      (np->msgin[5]<<8)  + (np->msgin[6]);
				sym_modify_dp(np, cp, tmp);
				return;
			case M_X_SYNC_REQ:
				sym_sync_nego(np, tp, cp);
				return;
			case M_X_PPR_REQ:
				sym_ppr_nego(np, tp, cp);
				return;
			case M_X_WIDE_REQ:
				sym_wide_nego(np, tp, cp);
				return;
			default:
				goto out_reject;
			}
			break;
		/*
		 *  We received a 1/2 byte message not handled from SCRIPTS.
		 *  We are only expecting MESSAGE REJECT and IGNORE WIDE
		 *  RESIDUE messages that haven't been anticipated by
		 *  SCRIPTS on SWIDE full condition. Unanticipated IGNORE
		 *  WIDE RESIDUE messages are aliased as MODIFY DP (-1).
		 */
		case M_IGN_RESIDUE:
			if (DEBUG_FLAGS & DEBUG_POINTER)
				sym_print_msg(cp,"ign wide residue", np->msgin);
			sym_modify_dp(np, cp, -1);
			return;
		case M_REJECT:
			if (INB (HS_PRT) == HS_NEGOTIATE)
				sym_nego_rejected(np, tp, cp);
			else {
				PRINT_ADDR(cp);
				printf ("M_REJECT received (%x:%x).\n",
					scr_to_cpu(np->lastmsg), np->msgout[0]);
			}
			goto out_clrack;
			break;
		default:
			goto out_reject;
		}
		break;
	/*
	 *  We received an unknown message.
	 *  Ignore all MSG IN phases and reject it.
	 */
	case SIR_MSG_WEIRD:
		sym_print_msg(cp, "WEIRD message received", np->msgin);
		OUTL_DSP (SCRIPTB_BA (np, msg_weird));
		return;
	/*
	 *  Negotiation failed.
	 *  Target does not send us the reply.
	 *  Remove the HS_NEGOTIATE status.
	 */
	case SIR_NEGO_FAILED:
		OUTB (HS_PRT, HS_BUSY);
	/*
	 *  Negotiation failed.
	 *  Target does not want answer message.
	 */
	case SIR_NEGO_PROTO:
		sym_nego_default(np, tp, cp);
		goto out;
	}

out:
	OUTONB_STD ();
	return;
out_reject:
	OUTL_DSP (SCRIPTB_BA (np, msg_bad));
	return;
out_clrack:
	OUTL_DSP (SCRIPTA_BA (np, clrack));
	return;
out_stuck:
	return;
}

/*
 *  Acquire a control block
 */
static	ccb_p sym_get_ccb (hcb_p np, u_char tn, u_char ln, u_char tag_order)
{
	tcb_p tp = &np->target[tn];
	lcb_p lp = sym_lp(tp, ln);
	u_short tag = NO_TAG;
	SYM_QUEHEAD *qp;
	ccb_p cp = (ccb_p) NULL;

	/*
	 *  Look for a free CCB
	 */
	if (sym_que_empty(&np->free_ccbq))
		goto out;
	qp = sym_remque_head(&np->free_ccbq);
	if (!qp)
		goto out;
	cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);

	/*
	 *  If the LCB is not yet available and the LUN
	 *  has been probed ok, try to allocate the LCB.
	 */
	if (!lp && sym_is_bit(tp->lun_map, ln)) {
		lp = sym_alloc_lcb(np, tn, ln);
		if (!lp)
			goto out_free;
	}

	/*
	 *  If the LCB is not available here, then the
	 *  logical unit is not yet discovered. For those
	 *  ones only accept 1 SCSI IO per logical unit,
	 *  since we cannot allow disconnections.
	 */
	if (!lp) {
		if (!sym_is_bit(tp->busy0_map, ln))
			sym_set_bit(tp->busy0_map, ln);
		else
			goto out_free;
	} else {
		/*
		 *  If we have been asked for a tagged command.
		 */
		if (tag_order) {
			/*
			 *  Debugging purpose.
			 */
			assert(lp->busy_itl == 0);
			/*
			 *  Allocate resources for tags if not yet.
			 */
			if (!lp->cb_tags) {
				sym_alloc_lcb_tags(np, tn, ln);
				if (!lp->cb_tags)
					goto out_free;
			}
			/*
			 *  Get a tag for this SCSI IO and set up
			 *  the CCB bus address for reselection,
			 *  and count it for this LUN.
			 *  Toggle reselect path to tagged.
			 */
			if (lp->busy_itlq < SYM_CONF_MAX_TASK) {
				tag = lp->cb_tags[lp->ia_tag];
				if (++lp->ia_tag == SYM_CONF_MAX_TASK)
					lp->ia_tag = 0;
				lp->itlq_tbl[tag] = cpu_to_scr(cp->ccb_ba);
				++lp->busy_itlq;
				lp->head.resel_sa =
					cpu_to_scr(SCRIPTA_BA (np, resel_tag));
			}
			else
				goto out_free;
		}
		/*
		 *  This command will not be tagged.
		 *  If we already have either a tagged or untagged
		 *  one, refuse to overlap this untagged one.
		 */
		else {
			/*
			 *  Debugging purpose.
			 */
			assert(lp->busy_itl == 0 && lp->busy_itlq == 0);
			/*
			 *  Count this nexus for this LUN.
			 *  Set up the CCB bus address for reselection.
			 *  Toggle reselect path to untagged.
			 */
			if (++lp->busy_itl == 1) {
				lp->head.itl_task_sa = cpu_to_scr(cp->ccb_ba);
				lp->head.resel_sa =
				      cpu_to_scr(SCRIPTA_BA (np, resel_no_tag));
			}
			else
				goto out_free;
		}
	}
	/*
	 *  Put the CCB into the busy queue.
	 */
	sym_insque_tail(&cp->link_ccbq, &np->busy_ccbq);

	/*
	 *  Remember all informations needed to free this CCB.
	 */
	cp->to_abort = 0;
	cp->tag	   = tag;
	cp->target = tn;
	cp->lun    = ln;

	if (DEBUG_FLAGS & DEBUG_TAGS) {
		PRINT_LUN(np, tn, ln);
		printf ("ccb @%p using tag %d.\n", cp, tag);
	}

out:
	return cp;
out_free:
	sym_insque_head(&cp->link_ccbq, &np->free_ccbq);
	return NULL;
}

/*
 *  Release one control block
 */
static void sym_free_ccb(hcb_p np, ccb_p cp)
{
	tcb_p tp = &np->target[cp->target];
	lcb_p lp = sym_lp(tp, cp->lun);

	if (DEBUG_FLAGS & DEBUG_TAGS) {
		PRINT_LUN(np, cp->target, cp->lun);
		printf ("ccb @%p freeing tag %d.\n", cp, cp->tag);
	}

	/*
	 *  If LCB available,
	 */
	if (lp) {
		/*
		 *  If tagged, release the tag, set the relect path
		 */
		if (cp->tag != NO_TAG) {
			/*
			 *  Free the tag value.
			 */
			lp->cb_tags[lp->if_tag] = cp->tag;
			if (++lp->if_tag == SYM_CONF_MAX_TASK)
				lp->if_tag = 0;
			/*
			 *  Make the reselect path invalid,
			 *  and uncount this CCB.
			 */
			lp->itlq_tbl[cp->tag] = cpu_to_scr(np->bad_itlq_ba);
			--lp->busy_itlq;
		} else {	/* Untagged */
			/*
			 *  Make the reselect path invalid,
			 *  and uncount this CCB.
			 */
			lp->head.itl_task_sa = cpu_to_scr(np->bad_itl_ba);
			--lp->busy_itl;
		}
		/*
		 *  If no JOB active, make the LUN reselect path invalid.
		 */
		if (lp->busy_itlq == 0 && lp->busy_itl == 0)
			lp->head.resel_sa =
				cpu_to_scr(SCRIPTB_BA (np, resel_bad_lun));
	}
	/*
	 *  Otherwise, we only accept 1 IO per LUN.
	 *  Clear the bit that keeps track of this IO.
	 */
	else
		sym_clr_bit(tp->busy0_map, cp->lun);

	/*
	 *  We donnot queue more than 1 ccb per target
	 *  with negotiation at any time. If this ccb was
	 *  used for negotiation, clear this info in the tcb.
	 */
	if (cp == tp->nego_cp)
		tp->nego_cp = NULL;

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *  If we just complete the last queued CCB,
	 *  clear this info that is no longer relevant.
	 */
	if (cp == np->last_cp)
		np->last_cp = NULL;
#endif

	/*
	 *  Unmap user data from DMA map if needed.
	 */
	if (cp->dmamapped) {
		bus_dmamap_unload(np->data_dmat, cp->dmamap);
		cp->dmamapped = 0;
	}

	/*
	 *  Make this CCB available.
	 */
	cp->cam_ccb = NULL;
	cp->host_status = HS_IDLE;
	sym_remque(&cp->link_ccbq);
	sym_insque_head(&cp->link_ccbq, &np->free_ccbq);
}

/*
 *  Allocate a CCB from memory and initialize its fixed part.
 */
static ccb_p sym_alloc_ccb(hcb_p np)
{
	ccb_p cp = NULL;
	int hcode;

	SYM_LOCK_ASSERT(MA_NOTOWNED);

	/*
	 *  Prevent from allocating more CCBs than we can
	 *  queue to the controller.
	 */
	if (np->actccbs >= SYM_CONF_MAX_START)
		return NULL;

	/*
	 *  Allocate memory for this CCB.
	 */
	cp = sym_calloc_dma(sizeof(struct sym_ccb), "CCB");
	if (!cp)
		return NULL;

	/*
	 *  Allocate a bounce buffer for sense data.
	 */
	cp->sns_bbuf = sym_calloc_dma(SYM_SNS_BBUF_LEN, "SNS_BBUF");
	if (!cp->sns_bbuf)
		goto out_free;

	/*
	 *  Allocate a map for the DMA of user data.
	 */
	if (bus_dmamap_create(np->data_dmat, 0, &cp->dmamap))
		goto out_free;
	/*
	 *  Count it.
	 */
	np->actccbs++;

	/*
	 * Initialize the callout.
	 */
	callout_init(&cp->ch, 1);

	/*
	 *  Compute the bus address of this ccb.
	 */
	cp->ccb_ba = vtobus(cp);

	/*
	 *  Insert this ccb into the hashed list.
	 */
	hcode = CCB_HASH_CODE(cp->ccb_ba);
	cp->link_ccbh = np->ccbh[hcode];
	np->ccbh[hcode] = cp;

	/*
	 *  Initialize the start and restart actions.
	 */
	cp->phys.head.go.start   = cpu_to_scr(SCRIPTA_BA (np, idle));
	cp->phys.head.go.restart = cpu_to_scr(SCRIPTB_BA (np, bad_i_t_l));

 	/*
	 *  Initilialyze some other fields.
	 */
	cp->phys.smsg_ext.addr = cpu_to_scr(HCB_BA(np, msgin[2]));

	/*
	 *  Chain into free ccb queue.
	 */
	sym_insque_head(&cp->link_ccbq, &np->free_ccbq);

	return cp;
out_free:
	if (cp->sns_bbuf)
		sym_mfree_dma(cp->sns_bbuf, SYM_SNS_BBUF_LEN, "SNS_BBUF");
	sym_mfree_dma(cp, sizeof(*cp), "CCB");
	return NULL;
}

/*
 *  Look up a CCB from a DSA value.
 */
static ccb_p sym_ccb_from_dsa(hcb_p np, u32 dsa)
{
	int hcode;
	ccb_p cp;

	hcode = CCB_HASH_CODE(dsa);
	cp = np->ccbh[hcode];
	while (cp) {
		if (cp->ccb_ba == dsa)
			break;
		cp = cp->link_ccbh;
	}

	return cp;
}

/*
 *  Lun control block allocation and initialization.
 */
static lcb_p sym_alloc_lcb (hcb_p np, u_char tn, u_char ln)
{
	tcb_p tp = &np->target[tn];
	lcb_p lp = sym_lp(tp, ln);

	/*
	 *  Already done, just return.
	 */
	if (lp)
		return lp;
	/*
	 *  Check against some race.
	 */
	assert(!sym_is_bit(tp->busy0_map, ln));

	/*
	 *  Allocate the LCB bus address array.
	 *  Compute the bus address of this table.
	 */
	if (ln && !tp->luntbl) {
		int i;

		tp->luntbl = sym_calloc_dma(256, "LUNTBL");
		if (!tp->luntbl)
			goto fail;
		for (i = 0 ; i < 64 ; i++)
			tp->luntbl[i] = cpu_to_scr(vtobus(&np->badlun_sa));
		tp->head.luntbl_sa = cpu_to_scr(vtobus(tp->luntbl));
	}

	/*
	 *  Allocate the table of pointers for LUN(s) > 0, if needed.
	 */
	if (ln && !tp->lunmp) {
		tp->lunmp = sym_calloc(SYM_CONF_MAX_LUN * sizeof(lcb_p),
				   "LUNMP");
		if (!tp->lunmp)
			goto fail;
	}

	/*
	 *  Allocate the lcb.
	 *  Make it available to the chip.
	 */
	lp = sym_calloc_dma(sizeof(struct sym_lcb), "LCB");
	if (!lp)
		goto fail;
	if (ln) {
		tp->lunmp[ln] = lp;
		tp->luntbl[ln] = cpu_to_scr(vtobus(lp));
	}
	else {
		tp->lun0p = lp;
		tp->head.lun0_sa = cpu_to_scr(vtobus(lp));
	}

	/*
	 *  Let the itl task point to error handling.
	 */
	lp->head.itl_task_sa = cpu_to_scr(np->bad_itl_ba);

	/*
	 *  Set the reselect pattern to our default. :)
	 */
	lp->head.resel_sa = cpu_to_scr(SCRIPTB_BA (np, resel_bad_lun));

	/*
	 *  Set user capabilities.
	 */
	lp->user_flags = tp->usrflags & (SYM_DISC_ENABLED | SYM_TAGS_ENABLED);

fail:
	return lp;
}

/*
 *  Allocate LCB resources for tagged command queuing.
 */
static void sym_alloc_lcb_tags (hcb_p np, u_char tn, u_char ln)
{
	tcb_p tp = &np->target[tn];
	lcb_p lp = sym_lp(tp, ln);
	int i;

	/*
	 *  If LCB not available, try to allocate it.
	 */
	if (!lp && !(lp = sym_alloc_lcb(np, tn, ln)))
		return;

	/*
	 *  Allocate the task table and and the tag allocation
	 *  circular buffer. We want both or none.
	 */
	lp->itlq_tbl = sym_calloc_dma(SYM_CONF_MAX_TASK*4, "ITLQ_TBL");
	if (!lp->itlq_tbl)
		return;
	lp->cb_tags = sym_calloc(SYM_CONF_MAX_TASK, "CB_TAGS");
	if (!lp->cb_tags) {
		sym_mfree_dma(lp->itlq_tbl, SYM_CONF_MAX_TASK*4, "ITLQ_TBL");
		lp->itlq_tbl = NULL;
		return;
	}

	/*
	 *  Initialize the task table with invalid entries.
	 */
	for (i = 0 ; i < SYM_CONF_MAX_TASK ; i++)
		lp->itlq_tbl[i] = cpu_to_scr(np->notask_ba);

	/*
	 *  Fill up the tag buffer with tag numbers.
	 */
	for (i = 0 ; i < SYM_CONF_MAX_TASK ; i++)
		lp->cb_tags[i] = i;

	/*
	 *  Make the task table available to SCRIPTS,
	 *  And accept tagged commands now.
	 */
	lp->head.itlq_tbl_sa = cpu_to_scr(vtobus(lp->itlq_tbl));
}

/*
 *  Test the pci bus snoop logic :-(
 *
 *  Has to be called with interrupts disabled.
 */
#ifndef SYM_CONF_IOMAPPED
static int sym_regtest (hcb_p np)
{
	register volatile u32 data;
	/*
	 *  chip registers may NOT be cached.
	 *  write 0xffffffff to a read only register area,
	 *  and try to read it back.
	 */
	data = 0xffffffff;
	OUTL_OFF(offsetof(struct sym_reg, nc_dstat), data);
	data = INL_OFF(offsetof(struct sym_reg, nc_dstat));
#if 1
	if (data == 0xffffffff) {
#else
	if ((data & 0xe2f0fffd) != 0x02000080) {
#endif
		printf ("CACHE TEST FAILED: reg dstat-sstat2 readback %x.\n",
			(unsigned) data);
		return (0x10);
	}
	return (0);
}
#endif

static int sym_snooptest (hcb_p np)
{
	u32	sym_rd, sym_wr, sym_bk, host_rd, host_wr, pc, dstat;
	int	i, err=0;
#ifndef SYM_CONF_IOMAPPED
	err |= sym_regtest (np);
	if (err) return (err);
#endif
restart_test:
	/*
	 *  Enable Master Parity Checking as we intend
	 *  to enable it for normal operations.
	 */
	OUTB (nc_ctest4, (np->rv_ctest4 & MPEE));
	/*
	 *  init
	 */
	pc  = SCRIPTB0_BA (np, snooptest);
	host_wr = 1;
	sym_wr  = 2;
	/*
	 *  Set memory and register.
	 */
	np->cache = cpu_to_scr(host_wr);
	OUTL (nc_temp, sym_wr);
	/*
	 *  Start script (exchange values)
	 */
	OUTL (nc_dsa, np->hcb_ba);
	OUTL_DSP (pc);
	/*
	 *  Wait 'til done (with timeout)
	 */
	for (i=0; i<SYM_SNOOP_TIMEOUT; i++)
		if (INB(nc_istat) & (INTF|SIP|DIP))
			break;
	if (i>=SYM_SNOOP_TIMEOUT) {
		printf ("CACHE TEST FAILED: timeout.\n");
		return (0x20);
	}
	/*
	 *  Check for fatal DMA errors.
	 */
	dstat = INB (nc_dstat);
#if 1	/* Band aiding for broken hardwares that fail PCI parity */
	if ((dstat & MDPE) && (np->rv_ctest4 & MPEE)) {
		printf ("%s: PCI DATA PARITY ERROR DETECTED - "
			"DISABLING MASTER DATA PARITY CHECKING.\n",
			sym_name(np));
		np->rv_ctest4 &= ~MPEE;
		goto restart_test;
	}
#endif
	if (dstat & (MDPE|BF|IID)) {
		printf ("CACHE TEST FAILED: DMA error (dstat=0x%02x).", dstat);
		return (0x80);
	}
	/*
	 *  Save termination position.
	 */
	pc = INL (nc_dsp);
	/*
	 *  Read memory and register.
	 */
	host_rd = scr_to_cpu(np->cache);
	sym_rd  = INL (nc_scratcha);
	sym_bk  = INL (nc_temp);

	/*
	 *  Check termination position.
	 */
	if (pc != SCRIPTB0_BA (np, snoopend)+8) {
		printf ("CACHE TEST FAILED: script execution failed.\n");
		printf ("start=%08lx, pc=%08lx, end=%08lx\n",
			(u_long) SCRIPTB0_BA (np, snooptest), (u_long) pc,
			(u_long) SCRIPTB0_BA (np, snoopend) +8);
		return (0x40);
	}
	/*
	 *  Show results.
	 */
	if (host_wr != sym_rd) {
		printf ("CACHE TEST FAILED: host wrote %d, chip read %d.\n",
			(int) host_wr, (int) sym_rd);
		err |= 1;
	}
	if (host_rd != sym_wr) {
		printf ("CACHE TEST FAILED: chip wrote %d, host read %d.\n",
			(int) sym_wr, (int) host_rd);
		err |= 2;
	}
	if (sym_bk != sym_wr) {
		printf ("CACHE TEST FAILED: chip wrote %d, read back %d.\n",
			(int) sym_wr, (int) sym_bk);
		err |= 4;
	}

	return (err);
}

/*
 *  Determine the chip's clock frequency.
 *
 *  This is essential for the negotiation of the synchronous
 *  transfer rate.
 *
 *  Note: we have to return the correct value.
 *  THERE IS NO SAFE DEFAULT VALUE.
 *
 *  Most NCR/SYMBIOS boards are delivered with a 40 Mhz clock.
 *  53C860 and 53C875 rev. 1 support fast20 transfers but
 *  do not have a clock doubler and so are provided with a
 *  80 MHz clock. All other fast20 boards incorporate a doubler
 *  and so should be delivered with a 40 MHz clock.
 *  The recent fast40 chips (895/896/895A/1010) use a 40 Mhz base
 *  clock and provide a clock quadrupler (160 Mhz).
 */

/*
 *  Select SCSI clock frequency
 */
static void sym_selectclock(hcb_p np, u_char scntl3)
{
	/*
	 *  If multiplier not present or not selected, leave here.
	 */
	if (np->multiplier <= 1) {
		OUTB(nc_scntl3,	scntl3);
		return;
	}

	if (sym_verbose >= 2)
		printf ("%s: enabling clock multiplier\n", sym_name(np));

	OUTB(nc_stest1, DBLEN);	   /* Enable clock multiplier		  */
	/*
	 *  Wait for the LCKFRQ bit to be set if supported by the chip.
	 *  Otherwise wait 20 micro-seconds.
	 */
	if (np->features & FE_LCKFRQ) {
		int i = 20;
		while (!(INB(nc_stest4) & LCKFRQ) && --i > 0)
			UDELAY (20);
		if (!i)
			printf("%s: the chip cannot lock the frequency\n",
				sym_name(np));
	} else
		UDELAY (20);
	OUTB(nc_stest3, HSC);		/* Halt the scsi clock		*/
	OUTB(nc_scntl3,	scntl3);
	OUTB(nc_stest1, (DBLEN|DBLSEL));/* Select clock multiplier	*/
	OUTB(nc_stest3, 0x00);		/* Restart scsi clock 		*/
}

/*
 *  calculate SCSI clock frequency (in KHz)
 */
static unsigned getfreq (hcb_p np, int gen)
{
	unsigned int ms = 0;
	unsigned int f;

	/*
	 * Measure GEN timer delay in order
	 * to calculate SCSI clock frequency
	 *
	 * This code will never execute too
	 * many loop iterations (if DELAY is
	 * reasonably correct). It could get
	 * too low a delay (too high a freq.)
	 * if the CPU is slow executing the
	 * loop for some reason (an NMI, for
	 * example). For this reason we will
	 * if multiple measurements are to be
	 * performed trust the higher delay
	 * (lower frequency returned).
	 */
	OUTW (nc_sien , 0);	/* mask all scsi interrupts */
	(void) INW (nc_sist);	/* clear pending scsi interrupt */
	OUTB (nc_dien , 0);	/* mask all dma interrupts */
	(void) INW (nc_sist);	/* another one, just to be sure :) */
	OUTB (nc_scntl3, 4);	/* set pre-scaler to divide by 3 */
	OUTB (nc_stime1, 0);	/* disable general purpose timer */
	OUTB (nc_stime1, gen);	/* set to nominal delay of 1<<gen * 125us */
	while (!(INW(nc_sist) & GEN) && ms++ < 100000)
		UDELAY (1000);	/* count ms */
	OUTB (nc_stime1, 0);	/* disable general purpose timer */
 	/*
 	 * set prescaler to divide by whatever 0 means
 	 * 0 ought to choose divide by 2, but appears
 	 * to set divide by 3.5 mode in my 53c810 ...
 	 */
 	OUTB (nc_scntl3, 0);

  	/*
 	 * adjust for prescaler, and convert into KHz
  	 */
	f = ms ? ((1 << gen) * 4340) / ms : 0;

	if (sym_verbose >= 2)
		printf ("%s: Delay (GEN=%d): %u msec, %u KHz\n",
			sym_name(np), gen, ms, f);

	return f;
}

static unsigned sym_getfreq (hcb_p np)
{
	u_int f1, f2;
	int gen = 11;

	(void) getfreq (np, gen);	/* throw away first result */
	f1 = getfreq (np, gen);
	f2 = getfreq (np, gen);
	if (f1 > f2) f1 = f2;		/* trust lower result	*/
	return f1;
}

/*
 *  Get/probe chip SCSI clock frequency
 */
static void sym_getclock (hcb_p np, int mult)
{
	unsigned char scntl3 = np->sv_scntl3;
	unsigned char stest1 = np->sv_stest1;
	unsigned f1;

	/*
	 *  For the C10 core, assume 40 MHz.
	 */
	if (np->features & FE_C10) {
		np->multiplier = mult;
		np->clock_khz = 40000 * mult;
		return;
	}

	np->multiplier = 1;
	f1 = 40000;
	/*
	 *  True with 875/895/896/895A with clock multiplier selected
	 */
	if (mult > 1 && (stest1 & (DBLEN+DBLSEL)) == DBLEN+DBLSEL) {
		if (sym_verbose >= 2)
			printf ("%s: clock multiplier found\n", sym_name(np));
		np->multiplier = mult;
	}

	/*
	 *  If multiplier not found or scntl3 not 7,5,3,
	 *  reset chip and get frequency from general purpose timer.
	 *  Otherwise trust scntl3 BIOS setting.
	 */
	if (np->multiplier != mult || (scntl3 & 7) < 3 || !(scntl3 & 1)) {
		OUTB (nc_stest1, 0);		/* make sure doubler is OFF */
		f1 = sym_getfreq (np);

		if (sym_verbose)
			printf ("%s: chip clock is %uKHz\n", sym_name(np), f1);

		if	(f1 <	45000)		f1 =  40000;
		else if (f1 <	55000)		f1 =  50000;
		else				f1 =  80000;

		if (f1 < 80000 && mult > 1) {
			if (sym_verbose >= 2)
				printf ("%s: clock multiplier assumed\n",
					sym_name(np));
			np->multiplier	= mult;
		}
	} else {
		if	((scntl3 & 7) == 3)	f1 =  40000;
		else if	((scntl3 & 7) == 5)	f1 =  80000;
		else 				f1 = 160000;

		f1 /= np->multiplier;
	}

	/*
	 *  Compute controller synchronous parameters.
	 */
	f1		*= np->multiplier;
	np->clock_khz	= f1;
}

/*
 *  Get/probe PCI clock frequency
 */
static int sym_getpciclock (hcb_p np)
{
	int f = 0;

	/*
	 *  For the C1010-33, this doesn't work.
	 *  For the C1010-66, this will be tested when I'll have
	 *  such a beast to play with.
	 */
	if (!(np->features & FE_C10)) {
		OUTB (nc_stest1, SCLK);	/* Use the PCI clock as SCSI clock */
		f = (int) sym_getfreq (np);
		OUTB (nc_stest1, 0);
	}
	np->pciclk_khz = f;

	return f;
}

/*============= DRIVER ACTION/COMPLETION ====================*/

/*
 *  Print something that tells about extended errors.
 */
static void sym_print_xerr(ccb_p cp, int x_status)
{
	if (x_status & XE_PARITY_ERR) {
		PRINT_ADDR(cp);
		printf ("unrecovered SCSI parity error.\n");
	}
	if (x_status & XE_EXTRA_DATA) {
		PRINT_ADDR(cp);
		printf ("extraneous data discarded.\n");
	}
	if (x_status & XE_BAD_PHASE) {
		PRINT_ADDR(cp);
		printf ("illegal scsi phase (4/5).\n");
	}
	if (x_status & XE_SODL_UNRUN) {
		PRINT_ADDR(cp);
		printf ("ODD transfer in DATA OUT phase.\n");
	}
	if (x_status & XE_SWIDE_OVRUN) {
		PRINT_ADDR(cp);
		printf ("ODD transfer in DATA IN phase.\n");
	}
}

/*
 *  Choose the more appropriate CAM status if
 *  the IO encountered an extended error.
 */
static int sym_xerr_cam_status(int cam_status, int x_status)
{
	if (x_status) {
		if	(x_status & XE_PARITY_ERR)
			cam_status = CAM_UNCOR_PARITY;
		else if	(x_status &(XE_EXTRA_DATA|XE_SODL_UNRUN|XE_SWIDE_OVRUN))
			cam_status = CAM_DATA_RUN_ERR;
		else if	(x_status & XE_BAD_PHASE)
			cam_status = CAM_REQ_CMP_ERR;
		else
			cam_status = CAM_REQ_CMP_ERR;
	}
	return cam_status;
}

/*
 *  Complete execution of a SCSI command with extented
 *  error, SCSI status error, or having been auto-sensed.
 *
 *  The SCRIPTS processor is not running there, so we
 *  can safely access IO registers and remove JOBs from
 *  the START queue.
 *  SCRATCHA is assumed to have been loaded with STARTPOS
 *  before the SCRIPTS called the C code.
 */
static void sym_complete_error (hcb_p np, ccb_p cp)
{
	struct ccb_scsiio *csio;
	u_int cam_status;
	int i, sense_returned;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  Paranoid check. :)
	 */
	if (!cp || !cp->cam_ccb)
		return;

	if (DEBUG_FLAGS & (DEBUG_TINY|DEBUG_RESULT)) {
		printf ("CCB=%lx STAT=%x/%x/%x DEV=%d/%d\n", (unsigned long)cp,
			cp->host_status, cp->ssss_status, cp->host_flags,
			cp->target, cp->lun);
		MDELAY(100);
	}

	/*
	 *  Get CAM command pointer.
	 */
	csio = &cp->cam_ccb->csio;

	/*
	 *  Check for extended errors.
	 */
	if (cp->xerr_status) {
		if (sym_verbose)
			sym_print_xerr(cp, cp->xerr_status);
		if (cp->host_status == HS_COMPLETE)
			cp->host_status = HS_COMP_ERR;
	}

	/*
	 *  Calculate the residual.
	 */
	csio->sense_resid = 0;
	csio->resid = sym_compute_residual(np, cp);

	if (!SYM_CONF_RESIDUAL_SUPPORT) {/* If user does not want residuals */
		csio->resid  = 0;	/* throw them away. :)		   */
		cp->sv_resid = 0;
	}

	if (cp->host_flags & HF_SENSE) {		/* Auto sense     */
		csio->scsi_status = cp->sv_scsi_status;	/* Restore status */
		csio->sense_resid = csio->resid;	/* Swap residuals */
		csio->resid       = cp->sv_resid;
		cp->sv_resid	  = 0;
		if (sym_verbose && cp->sv_xerr_status)
			sym_print_xerr(cp, cp->sv_xerr_status);
		if (cp->host_status == HS_COMPLETE &&
		    cp->ssss_status == S_GOOD &&
		    cp->xerr_status == 0) {
			cam_status = sym_xerr_cam_status(CAM_SCSI_STATUS_ERROR,
							 cp->sv_xerr_status);
			cam_status |= CAM_AUTOSNS_VALID;
			/*
			 *  Bounce back the sense data to user and
			 *  fix the residual.
			 */
			bzero(&csio->sense_data, sizeof(csio->sense_data));
			sense_returned = SYM_SNS_BBUF_LEN - csio->sense_resid;
			if (sense_returned < csio->sense_len)
				csio->sense_resid = csio->sense_len -
				    sense_returned;
			else
				csio->sense_resid = 0;
			bcopy(cp->sns_bbuf, &csio->sense_data,
			    MIN(csio->sense_len, sense_returned));
#if 0
			/*
			 *  If the device reports a UNIT ATTENTION condition
			 *  due to a RESET condition, we should consider all
			 *  disconnect CCBs for this unit as aborted.
			 */
			if (1) {
				u_char *p;
				p  = (u_char *) csio->sense_data;
				if (p[0]==0x70 && p[2]==0x6 && p[12]==0x29)
					sym_clear_tasks(np, CAM_REQ_ABORTED,
							cp->target,cp->lun, -1);
			}
#endif
		}
		else
			cam_status = CAM_AUTOSENSE_FAIL;
	}
	else if (cp->host_status == HS_COMPLETE) {	/* Bad SCSI status */
		csio->scsi_status = cp->ssss_status;
		cam_status = CAM_SCSI_STATUS_ERROR;
	}
	else if (cp->host_status == HS_SEL_TIMEOUT)	/* Selection timeout */
		cam_status = CAM_SEL_TIMEOUT;
	else if (cp->host_status == HS_UNEXPECTED)	/* Unexpected BUS FREE*/
		cam_status = CAM_UNEXP_BUSFREE;
	else {						/* Extended error */
		if (sym_verbose) {
			PRINT_ADDR(cp);
			printf ("COMMAND FAILED (%x %x %x).\n",
				cp->host_status, cp->ssss_status,
				cp->xerr_status);
		}
		csio->scsi_status = cp->ssss_status;
		/*
		 *  Set the most appropriate value for CAM status.
		 */
		cam_status = sym_xerr_cam_status(CAM_REQ_CMP_ERR,
						 cp->xerr_status);
	}

	/*
	 *  Dequeue all queued CCBs for that device
	 *  not yet started by SCRIPTS.
	 */
	i = (INL (nc_scratcha) - np->squeue_ba) / 4;
	(void) sym_dequeue_from_squeue(np, i, cp->target, cp->lun, -1);

	/*
	 *  Restart the SCRIPTS processor.
	 */
	OUTL_DSP (SCRIPTA_BA (np, start));

	/*
	 *  Synchronize DMA map if needed.
	 */
	if (cp->dmamapped) {
		bus_dmamap_sync(np->data_dmat, cp->dmamap,
			(cp->dmamapped == SYM_DMA_READ ?
				BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE));
	}
	/*
	 *  Add this one to the COMP queue.
	 *  Complete all those commands with either error
	 *  or requeue condition.
	 */
	sym_set_cam_status((union ccb *) csio, cam_status);
	sym_remque(&cp->link_ccbq);
	sym_insque_head(&cp->link_ccbq, &np->comp_ccbq);
	sym_flush_comp_queue(np, 0);
}

/*
 *  Complete execution of a successful SCSI command.
 *
 *  Only successful commands go to the DONE queue,
 *  since we need to have the SCRIPTS processor
 *  stopped on any error condition.
 *  The SCRIPTS processor is running while we are
 *  completing successful commands.
 */
static void sym_complete_ok (hcb_p np, ccb_p cp)
{
	struct ccb_scsiio *csio;
	tcb_p tp;
	lcb_p lp;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  Paranoid check. :)
	 */
	if (!cp || !cp->cam_ccb)
		return;
	assert (cp->host_status == HS_COMPLETE);

	/*
	 *  Get command, target and lun pointers.
	 */
	csio = &cp->cam_ccb->csio;
	tp = &np->target[cp->target];
	lp = sym_lp(tp, cp->lun);

	/*
	 *  Assume device discovered on first success.
	 */
	if (!lp)
		sym_set_bit(tp->lun_map, cp->lun);

	/*
	 *  If all data have been transferred, given than no
	 *  extended error did occur, there is no residual.
	 */
	csio->resid = 0;
	if (cp->phys.head.lastp != cp->phys.head.goalp)
		csio->resid = sym_compute_residual(np, cp);

	/*
	 *  Wrong transfer residuals may be worse than just always
	 *  returning zero. User can disable this feature from
	 *  sym_conf.h. Residual support is enabled by default.
	 */
	if (!SYM_CONF_RESIDUAL_SUPPORT)
		csio->resid  = 0;

	/*
	 *  Synchronize DMA map if needed.
	 */
	if (cp->dmamapped) {
		bus_dmamap_sync(np->data_dmat, cp->dmamap,
			(cp->dmamapped == SYM_DMA_READ ?
				BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE));
	}
	/*
	 *  Set status and complete the command.
	 */
	csio->scsi_status = cp->ssss_status;
	sym_set_cam_status((union ccb *) csio, CAM_REQ_CMP);
	sym_xpt_done(np, (union ccb *) csio, cp);
	sym_free_ccb(np, cp);
}

/*
 *  Our callout handler
 */
static void sym_callout(void *arg)
{
	union ccb *ccb = (union ccb *) arg;
	hcb_p np = ccb->ccb_h.sym_hcb_ptr;

	/*
	 *  Check that the CAM CCB is still queued.
	 */
	if (!np)
		return;

	SYM_LOCK();

	switch(ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
		(void) sym_abort_scsiio(np, ccb, 1);
		break;
	default:
		break;
	}

	SYM_UNLOCK();
}

/*
 *  Abort an SCSI IO.
 */
static int sym_abort_scsiio(hcb_p np, union ccb *ccb, int timed_out)
{
	ccb_p cp;
	SYM_QUEHEAD *qp;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  Look up our CCB control block.
	 */
	cp = NULL;
	FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
		ccb_p cp2 = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		if (cp2->cam_ccb == ccb) {
			cp = cp2;
			break;
		}
	}
	if (!cp || cp->host_status == HS_WAIT)
		return -1;

	/*
	 *  If a previous abort didn't succeed in time,
	 *  perform a BUS reset.
	 */
	if (cp->to_abort) {
		sym_reset_scsi_bus(np, 1);
		return 0;
	}

	/*
	 *  Mark the CCB for abort and allow time for.
	 */
	cp->to_abort = timed_out ? 2 : 1;
	callout_reset(&cp->ch, 10 * hz, sym_callout, (caddr_t) ccb);

	/*
	 *  Tell the SCRIPTS processor to stop and synchronize with us.
	 */
	np->istat_sem = SEM;
	OUTB (nc_istat, SIGP|SEM);
	return 0;
}

/*
 *  Reset a SCSI device (all LUNs of a target).
 */
static void sym_reset_dev(hcb_p np, union ccb *ccb)
{
	tcb_p tp;
	struct ccb_hdr *ccb_h = &ccb->ccb_h;

	SYM_LOCK_ASSERT(MA_OWNED);

	if (ccb_h->target_id   == np->myaddr ||
	    ccb_h->target_id   >= SYM_CONF_MAX_TARGET ||
	    ccb_h->target_lun  >= SYM_CONF_MAX_LUN) {
		sym_xpt_done2(np, ccb, CAM_DEV_NOT_THERE);
		return;
	}

	tp = &np->target[ccb_h->target_id];

	tp->to_reset = 1;
	sym_xpt_done2(np, ccb, CAM_REQ_CMP);

	np->istat_sem = SEM;
	OUTB (nc_istat, SIGP|SEM);
}

/*
 *  SIM action entry point.
 */
static void sym_action(struct cam_sim *sim, union ccb *ccb)
{
	hcb_p	np;
	tcb_p	tp;
	lcb_p	lp;
	ccb_p	cp;
	int 	tmp;
	u_char	idmsg, *msgptr;
	u_int   msglen;
	struct	ccb_scsiio *csio;
	struct	ccb_hdr  *ccb_h;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("sym_action\n"));

	/*
	 *  Retrieve our controller data structure.
	 */
	np = (hcb_p) cam_sim_softc(sim);

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  The common case is SCSI IO.
	 *  We deal with other ones elsewhere.
	 */
	if (ccb->ccb_h.func_code != XPT_SCSI_IO) {
		sym_action2(sim, ccb);
		return;
	}
	csio  = &ccb->csio;
	ccb_h = &csio->ccb_h;

	/*
	 *  Work around races.
	 */
	if ((ccb_h->status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
		xpt_done(ccb);
		return;
	}

	/*
	 *  Minimal checkings, so that we will not
	 *  go outside our tables.
	 */
	if (ccb_h->target_id   == np->myaddr ||
	    ccb_h->target_id   >= SYM_CONF_MAX_TARGET ||
	    ccb_h->target_lun  >= SYM_CONF_MAX_LUN) {
		sym_xpt_done2(np, ccb, CAM_DEV_NOT_THERE);
		return;
        }

	/*
	 *  Retrieve the target and lun descriptors.
	 */
	tp = &np->target[ccb_h->target_id];
	lp = sym_lp(tp, ccb_h->target_lun);

	/*
	 *  Complete the 1st INQUIRY command with error
	 *  condition if the device is flagged NOSCAN
	 *  at BOOT in the NVRAM. This may speed up
	 *  the boot and maintain coherency with BIOS
	 *  device numbering. Clearing the flag allows
	 *  user to rescan skipped devices later.
	 *  We also return error for devices not flagged
	 *  for SCAN LUNS in the NVRAM since some mono-lun
	 *  devices behave badly when asked for some non
	 *  zero LUN. Btw, this is an absolute hack.:-)
	 */
	if (!(ccb_h->flags & CAM_CDB_PHYS) &&
	    (0x12 == ((ccb_h->flags & CAM_CDB_POINTER) ?
		  csio->cdb_io.cdb_ptr[0] : csio->cdb_io.cdb_bytes[0]))) {
		if ((tp->usrflags & SYM_SCAN_BOOT_DISABLED) ||
		    ((tp->usrflags & SYM_SCAN_LUNS_DISABLED) &&
		     ccb_h->target_lun != 0)) {
			tp->usrflags &= ~SYM_SCAN_BOOT_DISABLED;
			sym_xpt_done2(np, ccb, CAM_DEV_NOT_THERE);
			return;
		}
	}

	/*
	 *  Get a control block for this IO.
	 */
	tmp = ((ccb_h->flags & CAM_TAG_ACTION_VALID) != 0);
	cp = sym_get_ccb(np, ccb_h->target_id, ccb_h->target_lun, tmp);
	if (!cp) {
		sym_xpt_done2(np, ccb, CAM_RESRC_UNAVAIL);
		return;
	}

	/*
	 *  Keep track of the IO in our CCB.
	 */
	cp->cam_ccb = ccb;

	/*
	 *  Build the IDENTIFY message.
	 */
	idmsg = M_IDENTIFY | cp->lun;
	if (cp->tag != NO_TAG || (lp && (lp->current_flags & SYM_DISC_ENABLED)))
		idmsg |= 0x40;

	msgptr = cp->scsi_smsg;
	msglen = 0;
	msgptr[msglen++] = idmsg;

	/*
	 *  Build the tag message if present.
	 */
	if (cp->tag != NO_TAG) {
		u_char order = csio->tag_action;

		switch(order) {
		case M_ORDERED_TAG:
			break;
		case M_HEAD_TAG:
			break;
		default:
			order = M_SIMPLE_TAG;
		}
		msgptr[msglen++] = order;

		/*
		 *  For less than 128 tags, actual tags are numbered
		 *  1,3,5,..2*MAXTAGS+1,since we may have to deal
		 *  with devices that have problems with #TAG 0 or too
		 *  great #TAG numbers. For more tags (up to 256),
		 *  we use directly our tag number.
		 */
#if SYM_CONF_MAX_TASK > (512/4)
		msgptr[msglen++] = cp->tag;
#else
		msgptr[msglen++] = (cp->tag << 1) + 1;
#endif
	}

	/*
	 *  Build a negotiation message if needed.
	 *  (nego_status is filled by sym_prepare_nego())
	 */
	cp->nego_status = 0;
	if (tp->tinfo.current.width   != tp->tinfo.goal.width  ||
	    tp->tinfo.current.period  != tp->tinfo.goal.period ||
	    tp->tinfo.current.offset  != tp->tinfo.goal.offset ||
	    tp->tinfo.current.options != tp->tinfo.goal.options) {
		if (!tp->nego_cp && lp)
			msglen += sym_prepare_nego(np, cp, 0, msgptr + msglen);
	}

	/*
	 *  Fill in our ccb
	 */

	/*
	 *  Startqueue
	 */
	cp->phys.head.go.start   = cpu_to_scr(SCRIPTA_BA (np, select));
	cp->phys.head.go.restart = cpu_to_scr(SCRIPTA_BA (np, resel_dsa));

	/*
	 *  select
	 */
	cp->phys.select.sel_id		= cp->target;
	cp->phys.select.sel_scntl3	= tp->head.wval;
	cp->phys.select.sel_sxfer	= tp->head.sval;
	cp->phys.select.sel_scntl4	= tp->head.uval;

	/*
	 *  message
	 */
	cp->phys.smsg.addr	= cpu_to_scr(CCB_BA (cp, scsi_smsg));
	cp->phys.smsg.size	= cpu_to_scr(msglen);

	/*
	 *  command
	 */
	if (sym_setup_cdb(np, csio, cp) < 0) {
		sym_xpt_done(np, ccb, cp);
		sym_free_ccb(np, cp);
		return;
	}

	/*
	 *  status
	 */
#if	0	/* Provision */
	cp->actualquirks	= tp->quirks;
#endif
	cp->actualquirks	= SYM_QUIRK_AUTOSAVE;
	cp->host_status		= cp->nego_status ? HS_NEGOTIATE : HS_BUSY;
	cp->ssss_status		= S_ILLEGAL;
	cp->xerr_status		= 0;
	cp->host_flags		= 0;
	cp->extra_bytes		= 0;

	/*
	 *  extreme data pointer.
	 *  shall be positive, so -1 is lower than lowest.:)
	 */
	cp->ext_sg  = -1;
	cp->ext_ofs = 0;

	/*
	 *  Build the data descriptor block
	 *  and start the IO.
	 */
	sym_setup_data_and_start(np, csio, cp);
}

/*
 *  Setup buffers and pointers that address the CDB.
 *  I bet, physical CDBs will never be used on the planet,
 *  since they can be bounced without significant overhead.
 */
static int sym_setup_cdb(hcb_p np, struct ccb_scsiio *csio, ccb_p cp)
{
	struct ccb_hdr *ccb_h;
	u32	cmd_ba;
	int	cmd_len;

	SYM_LOCK_ASSERT(MA_OWNED);

	ccb_h = &csio->ccb_h;

	/*
	 *  CDB is 16 bytes max.
	 */
	if (csio->cdb_len > sizeof(cp->cdb_buf)) {
		sym_set_cam_status(cp->cam_ccb, CAM_REQ_INVALID);
		return -1;
	}
	cmd_len = csio->cdb_len;

	if (ccb_h->flags & CAM_CDB_POINTER) {
		/* CDB is a pointer */
		if (!(ccb_h->flags & CAM_CDB_PHYS)) {
			/* CDB pointer is virtual */
			bcopy(csio->cdb_io.cdb_ptr, cp->cdb_buf, cmd_len);
			cmd_ba = CCB_BA (cp, cdb_buf[0]);
		} else {
			/* CDB pointer is physical */
#if 0
			cmd_ba = ((u32)csio->cdb_io.cdb_ptr) & 0xffffffff;
#else
			sym_set_cam_status(cp->cam_ccb, CAM_REQ_INVALID);
			return -1;
#endif
		}
	} else {
		/* CDB is in the CAM ccb (buffer) */
		bcopy(csio->cdb_io.cdb_bytes, cp->cdb_buf, cmd_len);
		cmd_ba = CCB_BA (cp, cdb_buf[0]);
	}

	cp->phys.cmd.addr	= cpu_to_scr(cmd_ba);
	cp->phys.cmd.size	= cpu_to_scr(cmd_len);

	return 0;
}

/*
 *  Set up data pointers used by SCRIPTS.
 */
static void __inline
sym_setup_data_pointers(hcb_p np, ccb_p cp, int dir)
{
	u32 lastp, goalp;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  No segments means no data.
	 */
	if (!cp->segments)
		dir = CAM_DIR_NONE;

	/*
	 *  Set the data pointer.
	 */
	switch(dir) {
	case CAM_DIR_OUT:
		goalp = SCRIPTA_BA (np, data_out2) + 8;
		lastp = goalp - 8 - (cp->segments * (2*4));
		break;
	case CAM_DIR_IN:
		cp->host_flags |= HF_DATA_IN;
		goalp = SCRIPTA_BA (np, data_in2) + 8;
		lastp = goalp - 8 - (cp->segments * (2*4));
		break;
	case CAM_DIR_NONE:
	default:
		lastp = goalp = SCRIPTB_BA (np, no_data);
		break;
	}

	cp->phys.head.lastp = cpu_to_scr(lastp);
	cp->phys.head.goalp = cpu_to_scr(goalp);
	cp->phys.head.savep = cpu_to_scr(lastp);
	cp->startp	    = cp->phys.head.savep;
}

/*
 *  Call back routine for the DMA map service.
 *  If bounce buffers are used (why ?), we may sleep and then
 *  be called there in another context.
 */
static void
sym_execute_ccb(void *arg, bus_dma_segment_t *psegs, int nsegs, int error)
{
	ccb_p	cp;
	hcb_p	np;
	union	ccb *ccb;

	cp  = (ccb_p) arg;
	ccb = cp->cam_ccb;
	np  = (hcb_p) cp->arg;

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  Deal with weird races.
	 */
	if (sym_get_cam_status(ccb) != CAM_REQ_INPROG)
		goto out_abort;

	/*
	 *  Deal with weird errors.
	 */
	if (error) {
		cp->dmamapped = 0;
		sym_set_cam_status(cp->cam_ccb, CAM_REQ_ABORTED);
		goto out_abort;
	}

	/*
	 *  Build the data descriptor for the chip.
	 */
	if (nsegs) {
		int retv;
		/* 896 rev 1 requires to be careful about boundaries */
		if (np->device_id == PCI_ID_SYM53C896 && np->revision_id <= 1)
			retv = sym_scatter_sg_physical(np, cp, psegs, nsegs);
		else
			retv = sym_fast_scatter_sg_physical(np,cp, psegs,nsegs);
		if (retv < 0) {
			sym_set_cam_status(cp->cam_ccb, CAM_REQ_TOO_BIG);
			goto out_abort;
		}
	}

	/*
	 *  Synchronize the DMA map only if we have
	 *  actually mapped the data.
	 */
	if (cp->dmamapped) {
		bus_dmamap_sync(np->data_dmat, cp->dmamap,
			(cp->dmamapped == SYM_DMA_READ ?
				BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE));
	}

	/*
	 *  Set host status to busy state.
	 *  May have been set back to HS_WAIT to avoid a race.
	 */
	cp->host_status	= cp->nego_status ? HS_NEGOTIATE : HS_BUSY;

	/*
	 *  Set data pointers.
	 */
	sym_setup_data_pointers(np, cp,  (ccb->ccb_h.flags & CAM_DIR_MASK));

	/*
	 *  Enqueue this IO in our pending queue.
	 */
	sym_enqueue_cam_ccb(cp);

	/*
	 *  When `#ifed 1', the code below makes the driver
	 *  panic on the first attempt to write to a SCSI device.
	 *  It is the first test we want to do after a driver
	 *  change that does not seem obviously safe. :)
	 */
#if 0
	switch (cp->cdb_buf[0]) {
	case 0x0A: case 0x2A: case 0xAA:
		panic("XXXXXXXXXXXXX WRITE NOT YET ALLOWED XXXXXXXXXXXXXX\n");
		MDELAY(10000);
		break;
	default:
		break;
	}
#endif
	/*
	 *  Activate this job.
	 */
	sym_put_start_queue(np, cp);
	return;
out_abort:
	sym_xpt_done(np, ccb, cp);
	sym_free_ccb(np, cp);
}

/*
 *  How complex it gets to deal with the data in CAM.
 *  The Bus Dma stuff makes things still more complex.
 */
static void
sym_setup_data_and_start(hcb_p np, struct ccb_scsiio *csio, ccb_p cp)
{
	struct ccb_hdr *ccb_h;
	int dir, retv;

	SYM_LOCK_ASSERT(MA_OWNED);

	ccb_h = &csio->ccb_h;

	/*
	 *  Now deal with the data.
	 */
	cp->data_len = csio->dxfer_len;
	cp->arg      = np;

	/*
	 *  No direction means no data.
	 */
	dir = (ccb_h->flags & CAM_DIR_MASK);
	if (dir == CAM_DIR_NONE) {
		sym_execute_ccb(cp, NULL, 0, 0);
		return;
	}

	cp->dmamapped = (dir == CAM_DIR_IN) ?  SYM_DMA_READ : SYM_DMA_WRITE;
	retv = bus_dmamap_load_ccb(np->data_dmat, cp->dmamap,
			       (union ccb *)csio, sym_execute_ccb, cp, 0);
	if (retv == EINPROGRESS) {
		cp->host_status	= HS_WAIT;
		xpt_freeze_simq(np->sim, 1);
		csio->ccb_h.status |= CAM_RELEASE_SIMQ;
	}
}

/*
 *  Move the scatter list to our data block.
 */
static int
sym_fast_scatter_sg_physical(hcb_p np, ccb_p cp,
			     bus_dma_segment_t *psegs, int nsegs)
{
	struct sym_tblmove *data;
	bus_dma_segment_t *psegs2;

	SYM_LOCK_ASSERT(MA_OWNED);

	if (nsegs > SYM_CONF_MAX_SG)
		return -1;

	data   = &cp->phys.data[SYM_CONF_MAX_SG-1];
	psegs2 = &psegs[nsegs-1];
	cp->segments = nsegs;

	while (1) {
		data->addr = cpu_to_scr(psegs2->ds_addr);
		data->size = cpu_to_scr(psegs2->ds_len);
		if (DEBUG_FLAGS & DEBUG_SCATTER) {
			printf ("%s scatter: paddr=%lx len=%ld\n",
				sym_name(np), (long) psegs2->ds_addr,
				(long) psegs2->ds_len);
		}
		if (psegs2 != psegs) {
			--data;
			--psegs2;
			continue;
		}
		break;
	}
	return 0;
}

/*
 *  Scatter a SG list with physical addresses into bus addressable chunks.
 */
static int
sym_scatter_sg_physical(hcb_p np, ccb_p cp, bus_dma_segment_t *psegs, int nsegs)
{
	u_long	ps, pe, pn;
	u_long	k;
	int s, t;

	SYM_LOCK_ASSERT(MA_OWNED);

	s  = SYM_CONF_MAX_SG - 1;
	t  = nsegs - 1;
	ps = psegs[t].ds_addr;
	pe = ps + psegs[t].ds_len;

	while (s >= 0) {
		pn = rounddown2(pe - 1, SYM_CONF_DMA_BOUNDARY);
		if (pn <= ps)
			pn = ps;
		k = pe - pn;
		if (DEBUG_FLAGS & DEBUG_SCATTER) {
			printf ("%s scatter: paddr=%lx len=%ld\n",
				sym_name(np), pn, k);
		}
		cp->phys.data[s].addr = cpu_to_scr(pn);
		cp->phys.data[s].size = cpu_to_scr(k);
		--s;
		if (pn == ps) {
			if (--t < 0)
				break;
			ps = psegs[t].ds_addr;
			pe = ps + psegs[t].ds_len;
		}
		else
			pe = pn;
	}

	cp->segments = SYM_CONF_MAX_SG - 1 - s;

	return t >= 0 ? -1 : 0;
}

/*
 *  SIM action for non performance critical stuff.
 */
static void sym_action2(struct cam_sim *sim, union ccb *ccb)
{
	union ccb *abort_ccb;
	struct ccb_hdr *ccb_h;
	struct ccb_pathinq *cpi;
	struct ccb_trans_settings *cts;
	struct sym_trans *tip;
	hcb_p	np;
	tcb_p	tp;
	lcb_p	lp;
	u_char dflags;

	/*
	 *  Retrieve our controller data structure.
	 */
	np = (hcb_p) cam_sim_softc(sim);

	SYM_LOCK_ASSERT(MA_OWNED);

	ccb_h = &ccb->ccb_h;

	switch (ccb_h->func_code) {
	case XPT_SET_TRAN_SETTINGS:
		cts  = &ccb->cts;
		tp = &np->target[ccb_h->target_id];

		/*
		 *  Update SPI transport settings in TARGET control block.
		 *  Update SCSI device settings in LUN control block.
		 */
		lp = sym_lp(tp, ccb_h->target_lun);
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
			sym_update_trans(np, &tp->tinfo.goal, cts);
			if (lp)
				sym_update_dflags(np, &lp->current_flags, cts);
		}
		if (cts->type == CTS_TYPE_USER_SETTINGS) {
			sym_update_trans(np, &tp->tinfo.user, cts);
			if (lp)
				sym_update_dflags(np, &lp->user_flags, cts);
		}

		sym_xpt_done2(np, ccb, CAM_REQ_CMP);
		break;
	case XPT_GET_TRAN_SETTINGS:
		cts = &ccb->cts;
		tp = &np->target[ccb_h->target_id];
		lp = sym_lp(tp, ccb_h->target_lun);

#define	cts__scsi (&cts->proto_specific.scsi)
#define	cts__spi  (&cts->xport_specific.spi)
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
			tip = &tp->tinfo.current;
			dflags = lp ? lp->current_flags : 0;
		}
		else {
			tip = &tp->tinfo.user;
			dflags = lp ? lp->user_flags : tp->usrflags;
		}

		cts->protocol  = PROTO_SCSI;
		cts->transport = XPORT_SPI;
		cts->protocol_version  = tip->scsi_version;
		cts->transport_version = tip->spi_version;

		cts__spi->sync_period = tip->period;
		cts__spi->sync_offset = tip->offset;
		cts__spi->bus_width   = tip->width;
		cts__spi->ppr_options = tip->options;

		cts__spi->valid = CTS_SPI_VALID_SYNC_RATE
		                | CTS_SPI_VALID_SYNC_OFFSET
		                | CTS_SPI_VALID_BUS_WIDTH
		                | CTS_SPI_VALID_PPR_OPTIONS;

		cts__spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
		if (dflags & SYM_DISC_ENABLED)
			cts__spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
		cts__spi->valid |= CTS_SPI_VALID_DISC;

		cts__scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
		if (dflags & SYM_TAGS_ENABLED)
			cts__scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
		cts__scsi->valid |= CTS_SCSI_VALID_TQ;
#undef	cts__spi
#undef	cts__scsi
		sym_xpt_done2(np, ccb, CAM_REQ_CMP);
		break;
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, /*extended*/1);
		sym_xpt_done2(np, ccb, CAM_REQ_CMP);
		break;
	case XPT_PATH_INQ:
		cpi = &ccb->cpi;
		cpi->version_num = 1;
		cpi->hba_inquiry = PI_MDP_ABLE|PI_SDTR_ABLE|PI_TAG_ABLE;
		if ((np->features & FE_WIDE) != 0)
			cpi->hba_inquiry |= PI_WIDE_16;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_UNMAPPED;
		if (np->usrflags & SYM_SCAN_TARGETS_HILO)
			cpi->hba_misc |= PIM_SCANHILO;
		if (np->usrflags & SYM_AVOID_BUS_RESET)
			cpi->hba_misc |= PIM_NOBUSRESET;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = (np->features & FE_WIDE) ? 15 : 7;
		/* Semantic problem:)LUN number max = max number of LUNs - 1 */
		cpi->max_lun = SYM_CONF_MAX_LUN-1;
		if (SYM_SETUP_MAX_LUN < SYM_CONF_MAX_LUN)
			cpi->max_lun = SYM_SETUP_MAX_LUN-1;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->initiator_id = np->myaddr;
		cpi->base_transfer_speed = 3300;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "Symbios", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);

		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->xport_specific.spi.ppr_options = SID_SPI_CLOCK_ST;
		if (np->features & FE_ULTRA3) {
			cpi->transport_version = 3;
			cpi->xport_specific.spi.ppr_options =
			    SID_SPI_CLOCK_DT_ST;
		}
		cpi->maxio = SYM_CONF_MAX_SG * PAGE_SIZE;
		sym_xpt_done2(np, ccb, CAM_REQ_CMP);
		break;
	case XPT_ABORT:
		abort_ccb = ccb->cab.abort_ccb;
		switch(abort_ccb->ccb_h.func_code) {
		case XPT_SCSI_IO:
			if (sym_abort_scsiio(np, abort_ccb, 0) == 0) {
				sym_xpt_done2(np, ccb, CAM_REQ_CMP);
				break;
			}
		default:
			sym_xpt_done2(np, ccb, CAM_UA_ABORT);
			break;
		}
		break;
	case XPT_RESET_DEV:
		sym_reset_dev(np, ccb);
		break;
	case XPT_RESET_BUS:
		sym_reset_scsi_bus(np, 0);
		if (sym_verbose) {
			xpt_print_path(np->path);
			printf("SCSI BUS reset delivered.\n");
		}
		sym_init (np, 1);
		sym_xpt_done2(np, ccb, CAM_REQ_CMP);
		break;
	case XPT_TERM_IO:
	default:
		sym_xpt_done2(np, ccb, CAM_REQ_INVALID);
		break;
	}
}

/*
 *  Asynchronous notification handler.
 */
static void
sym_async(void *cb_arg, u32 code, struct cam_path *path, void *args __unused)
{
	hcb_p np;
	struct cam_sim *sim;
	u_int tn;
	tcb_p tp;

	sim = (struct cam_sim *) cb_arg;
	np  = (hcb_p) cam_sim_softc(sim);

	SYM_LOCK_ASSERT(MA_OWNED);

	switch (code) {
	case AC_LOST_DEVICE:
		tn = xpt_path_target_id(path);
		if (tn >= SYM_CONF_MAX_TARGET)
			break;

		tp = &np->target[tn];

		tp->to_reset  = 0;
		tp->head.sval = 0;
		tp->head.wval = np->rv_scntl3;
		tp->head.uval = 0;

		tp->tinfo.current.period  = tp->tinfo.goal.period = 0;
		tp->tinfo.current.offset  = tp->tinfo.goal.offset = 0;
		tp->tinfo.current.width   = tp->tinfo.goal.width  = BUS_8_BIT;
		tp->tinfo.current.options = tp->tinfo.goal.options = 0;

		break;
	default:
		break;
	}
}

/*
 *  Update transfer settings of a target.
 */
static void sym_update_trans(hcb_p np, struct sym_trans *tip,
    struct ccb_trans_settings *cts)
{

	SYM_LOCK_ASSERT(MA_OWNED);

	/*
	 *  Update the infos.
	 */
#define cts__spi (&cts->xport_specific.spi)
	if ((cts__spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0)
		tip->width = cts__spi->bus_width;
	if ((cts__spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0)
		tip->offset = cts__spi->sync_offset;
	if ((cts__spi->valid & CTS_SPI_VALID_SYNC_RATE) != 0)
		tip->period = cts__spi->sync_period;
	if ((cts__spi->valid & CTS_SPI_VALID_PPR_OPTIONS) != 0)
		tip->options = (cts__spi->ppr_options & PPR_OPT_DT);
	if (cts->protocol_version != PROTO_VERSION_UNSPECIFIED &&
	    cts->protocol_version != PROTO_VERSION_UNKNOWN)
		tip->scsi_version = cts->protocol_version;
	if (cts->transport_version != XPORT_VERSION_UNSPECIFIED &&
	    cts->transport_version != XPORT_VERSION_UNKNOWN)
		tip->spi_version = cts->transport_version;
#undef cts__spi
	/*
	 *  Scale against driver configuration limits.
	 */
	if (tip->width  > SYM_SETUP_MAX_WIDE) tip->width  = SYM_SETUP_MAX_WIDE;
	if (tip->period && tip->offset) {
		if (tip->offset > SYM_SETUP_MAX_OFFS) tip->offset = SYM_SETUP_MAX_OFFS;
		if (tip->period < SYM_SETUP_MIN_SYNC) tip->period = SYM_SETUP_MIN_SYNC;
	} else {
		tip->offset = 0;
		tip->period = 0;
	}

	/*
	 *  Scale against actual controller BUS width.
	 */
	if (tip->width > np->maxwide)
		tip->width  = np->maxwide;

	/*
	 *  Only accept DT if controller supports and SYNC/WIDE asked.
	 */
	if (!((np->features & (FE_C10|FE_ULTRA3)) == (FE_C10|FE_ULTRA3)) ||
	    !(tip->width == BUS_16_BIT && tip->offset)) {
		tip->options &= ~PPR_OPT_DT;
	}

	/*
	 *  Scale period factor and offset against controller limits.
	 */
	if (tip->offset && tip->period) {
		if (tip->options & PPR_OPT_DT) {
			if (tip->period < np->minsync_dt)
				tip->period = np->minsync_dt;
			if (tip->period > np->maxsync_dt)
				tip->period = np->maxsync_dt;
			if (tip->offset > np->maxoffs_dt)
				tip->offset = np->maxoffs_dt;
		}
		else {
			if (tip->period < np->minsync)
				tip->period = np->minsync;
			if (tip->period > np->maxsync)
				tip->period = np->maxsync;
			if (tip->offset > np->maxoffs)
				tip->offset = np->maxoffs;
		}
	}
}

/*
 *  Update flags for a device (logical unit).
 */
static void
sym_update_dflags(hcb_p np, u_char *flags, struct ccb_trans_settings *cts)
{

	SYM_LOCK_ASSERT(MA_OWNED);

#define	cts__scsi (&cts->proto_specific.scsi)
#define	cts__spi  (&cts->xport_specific.spi)
	if ((cts__spi->valid & CTS_SPI_VALID_DISC) != 0) {
		if ((cts__spi->flags & CTS_SPI_FLAGS_DISC_ENB) != 0)
			*flags |= SYM_DISC_ENABLED;
		else
			*flags &= ~SYM_DISC_ENABLED;
	}

	if ((cts__scsi->valid & CTS_SCSI_VALID_TQ) != 0) {
		if ((cts__scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0)
			*flags |= SYM_TAGS_ENABLED;
		else
			*flags &= ~SYM_TAGS_ENABLED;
	}
#undef	cts__spi
#undef	cts__scsi
}

/*============= DRIVER INITIALISATION ==================*/

static device_method_t sym_pci_methods[] = {
	DEVMETHOD(device_probe,	 sym_pci_probe),
	DEVMETHOD(device_attach, sym_pci_attach),
	DEVMETHOD_END
};

static driver_t sym_pci_driver = {
	"sym",
	sym_pci_methods,
	1	/* no softc */
};

static devclass_t sym_devclass;

DRIVER_MODULE(sym, pci, sym_pci_driver, sym_devclass, NULL, NULL);
MODULE_DEPEND(sym, cam, 1, 1, 1);
MODULE_DEPEND(sym, pci, 1, 1, 1);

static const struct sym_pci_chip sym_pci_dev_table[] = {
 {PCI_ID_SYM53C810, 0x0f, "810", 4, 8, 4, 64,
 FE_ERL}
 ,
#ifdef SYM_DEBUG_GENERIC_SUPPORT
 {PCI_ID_SYM53C810, 0xff, "810a", 4,  8, 4, 1,
 FE_BOF}
 ,
#else
 {PCI_ID_SYM53C810, 0xff, "810a", 4,  8, 4, 1,
 FE_CACHE_SET|FE_LDSTR|FE_PFEN|FE_BOF}
 ,
#endif
 {PCI_ID_SYM53C815, 0xff, "815", 4,  8, 4, 64,
 FE_BOF|FE_ERL}
 ,
 {PCI_ID_SYM53C825, 0x0f, "825", 6,  8, 4, 64,
 FE_WIDE|FE_BOF|FE_ERL|FE_DIFF}
 ,
 {PCI_ID_SYM53C825, 0xff, "825a", 6,  8, 4, 2,
 FE_WIDE|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|FE_RAM|FE_DIFF}
 ,
 {PCI_ID_SYM53C860, 0xff, "860", 4,  8, 5, 1,
 FE_ULTRA|FE_CLK80|FE_CACHE_SET|FE_BOF|FE_LDSTR|FE_PFEN}
 ,
 {PCI_ID_SYM53C875, 0x01, "875", 6, 16, 5, 2,
 FE_WIDE|FE_ULTRA|FE_CLK80|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_DIFF}
 ,
 {PCI_ID_SYM53C875, 0xff, "875", 6, 16, 5, 2,
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_DIFF}
 ,
 {PCI_ID_SYM53C875_2, 0xff, "875", 6, 16, 5, 2,
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_DIFF}
 ,
 {PCI_ID_SYM53C885, 0xff, "885", 6, 16, 5, 2,
 FE_WIDE|FE_ULTRA|FE_DBLR|FE_CACHE0_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_DIFF}
 ,
#ifdef SYM_DEBUG_GENERIC_SUPPORT
 {PCI_ID_SYM53C895, 0xff, "895", 6, 31, 7, 2,
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|
 FE_RAM|FE_LCKFRQ}
 ,
#else
 {PCI_ID_SYM53C895, 0xff, "895", 6, 31, 7, 2,
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_LCKFRQ}
 ,
#endif
 {PCI_ID_SYM53C896, 0xff, "896", 6, 31, 7, 4,
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_RAM8K|FE_64BIT|FE_DAC|FE_IO256|FE_NOPM|FE_LEDC|FE_LCKFRQ}
 ,
 {PCI_ID_SYM53C895A, 0xff, "895a", 6, 31, 7, 4,
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_RAM8K|FE_DAC|FE_IO256|FE_NOPM|FE_LEDC|FE_LCKFRQ}
 ,
 {PCI_ID_LSI53C1010, 0x00, "1010-33", 6, 31, 7, 8,
 FE_WIDE|FE_ULTRA3|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFBC|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_RAM8K|FE_64BIT|FE_DAC|FE_IO256|FE_NOPM|FE_LEDC|FE_CRC|
 FE_C10}
 ,
 {PCI_ID_LSI53C1010, 0xff, "1010-33", 6, 31, 7, 8,
 FE_WIDE|FE_ULTRA3|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFBC|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_RAM8K|FE_64BIT|FE_DAC|FE_IO256|FE_NOPM|FE_LEDC|FE_CRC|
 FE_C10|FE_U3EN}
 ,
 {PCI_ID_LSI53C1010_2, 0xff, "1010-66", 6, 31, 7, 8,
 FE_WIDE|FE_ULTRA3|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFBC|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_RAM8K|FE_64BIT|FE_DAC|FE_IO256|FE_NOPM|FE_LEDC|FE_66MHZ|FE_CRC|
 FE_C10|FE_U3EN}
 ,
 {PCI_ID_LSI53C1510D, 0xff, "1510d", 6, 31, 7, 4,
 FE_WIDE|FE_ULTRA2|FE_QUAD|FE_CACHE_SET|FE_BOF|FE_DFS|FE_LDSTR|FE_PFEN|
 FE_RAM|FE_IO256|FE_LEDC}
};

/*
 *  Look up the chip table.
 *
 *  Return a pointer to the chip entry if found,
 *  zero otherwise.
 */
static const struct sym_pci_chip *
sym_find_pci_chip(device_t dev)
{
	const struct	sym_pci_chip *chip;
	int	i;
	u_short	device_id;
	u_char	revision;

	if (pci_get_vendor(dev) != PCI_VENDOR_NCR)
		return NULL;

	device_id = pci_get_device(dev);
	revision  = pci_get_revid(dev);

	for (i = 0; i < nitems(sym_pci_dev_table); i++) {
		chip = &sym_pci_dev_table[i];
		if (device_id != chip->device_id)
			continue;
		if (revision > chip->revision_id)
			continue;
		return chip;
	}

	return NULL;
}

/*
 *  Tell upper layer if the chip is supported.
 */
static int
sym_pci_probe(device_t dev)
{
	const struct	sym_pci_chip *chip;

	chip = sym_find_pci_chip(dev);
	if (chip && sym_find_firmware(chip)) {
		device_set_desc(dev, chip->name);
		return BUS_PROBE_DEFAULT;
	}
	return ENXIO;
}

/*
 *  Attach a sym53c8xx device.
 */
static int
sym_pci_attach(device_t dev)
{
	const struct	sym_pci_chip *chip;
	u_short	command;
	u_char	cachelnsz;
	struct	sym_hcb *np = NULL;
	struct	sym_nvram nvram;
	const struct	sym_fw *fw = NULL;
	int 	i;
	bus_dma_tag_t	bus_dmat;

	bus_dmat = bus_get_dma_tag(dev);

	/*
	 *  Only probed devices should be attached.
	 *  We just enjoy being paranoid. :)
	 */
	chip = sym_find_pci_chip(dev);
	if (chip == NULL || (fw = sym_find_firmware(chip)) == NULL)
		return (ENXIO);

	/*
	 *  Allocate immediately the host control block,
	 *  since we are only expecting to succeed. :)
	 *  We keep track in the HCB of all the resources that
	 *  are to be released on error.
	 */
	np = __sym_calloc_dma(bus_dmat, sizeof(*np), "HCB");
	if (np)
		np->bus_dmat = bus_dmat;
	else
		return (ENXIO);
	device_set_softc(dev, np);

	SYM_LOCK_INIT();

	/*
	 *  Copy some useful infos to the HCB.
	 */
	np->hcb_ba	 = vtobus(np);
	np->verbose	 = bootverbose;
	np->device	 = dev;
	np->device_id	 = pci_get_device(dev);
	np->revision_id  = pci_get_revid(dev);
	np->features	 = chip->features;
	np->clock_divn	 = chip->nr_divisor;
	np->maxoffs	 = chip->offset_max;
	np->maxburst	 = chip->burst_max;
	np->scripta_sz	 = fw->a_size;
	np->scriptb_sz	 = fw->b_size;
	np->fw_setup	 = fw->setup;
	np->fw_patch	 = fw->patch;
	np->fw_name	 = fw->name;

#ifdef __amd64__
	np->target = sym_calloc_dma(SYM_CONF_MAX_TARGET * sizeof(*(np->target)),
			"TARGET");
	if (!np->target)
		goto attach_failed;
#endif

	/*
	 *  Initialize the CCB free and busy queues.
	 */
	sym_que_init(&np->free_ccbq);
	sym_que_init(&np->busy_ccbq);
	sym_que_init(&np->comp_ccbq);
	sym_que_init(&np->cam_ccbq);

	/*
	 *  Allocate a tag for the DMA of user data.
	 */
	if (bus_dma_tag_create(np->bus_dmat, 1, SYM_CONF_DMA_BOUNDARY,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, SYM_CONF_MAX_SG, SYM_CONF_DMA_BOUNDARY,
	    0, busdma_lock_mutex, &np->mtx, &np->data_dmat)) {
		device_printf(dev, "failed to create DMA tag.\n");
		goto attach_failed;
	}

	/*
	 *  Read and apply some fix-ups to the PCI COMMAND
	 *  register. We want the chip to be enabled for:
	 *  - BUS mastering
	 *  - PCI parity checking (reporting would also be fine)
	 *  - Write And Invalidate.
	 */
	command = pci_read_config(dev, PCIR_COMMAND, 2);
	command |= PCIM_CMD_BUSMASTEREN | PCIM_CMD_PERRESPEN |
	    PCIM_CMD_MWRICEN;
	pci_write_config(dev, PCIR_COMMAND, command, 2);

	/*
	 *  Let the device know about the cache line size,
	 *  if it doesn't yet.
	 */
	cachelnsz = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	if (!cachelnsz) {
		cachelnsz = 8;
		pci_write_config(dev, PCIR_CACHELNSZ, cachelnsz, 1);
	}

	/*
	 *  Alloc/get/map/retrieve everything that deals with MMIO.
	 */
	i = SYM_PCI_MMIO;
	np->mmio_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &i,
	    RF_ACTIVE);
	if (!np->mmio_res) {
		device_printf(dev, "failed to allocate MMIO resources\n");
		goto attach_failed;
	}
	np->mmio_ba = rman_get_start(np->mmio_res);

	/*
	 *  Allocate the IRQ.
	 */
	i = 0;
	np->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &i,
					     RF_ACTIVE | RF_SHAREABLE);
	if (!np->irq_res) {
		device_printf(dev, "failed to allocate IRQ resource\n");
		goto attach_failed;
	}

#ifdef	SYM_CONF_IOMAPPED
	/*
	 *  User want us to use normal IO with PCI.
	 *  Alloc/get/map/retrieve everything that deals with IO.
	 */
	i = SYM_PCI_IO;
	np->io_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &i, RF_ACTIVE);
	if (!np->io_res) {
		device_printf(dev, "failed to allocate IO resources\n");
		goto attach_failed;
	}

#endif /* SYM_CONF_IOMAPPED */

	/*
	 *  If the chip has RAM.
	 *  Alloc/get/map/retrieve the corresponding resources.
	 */
	if (np->features & (FE_RAM|FE_RAM8K)) {
		int regs_id = SYM_PCI_RAM;
		if (np->features & FE_64BIT)
			regs_id = SYM_PCI_RAM64;
		np->ram_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
						     &regs_id, RF_ACTIVE);
		if (!np->ram_res) {
			device_printf(dev,"failed to allocate RAM resources\n");
			goto attach_failed;
		}
		np->ram_id  = regs_id;
		np->ram_ba = rman_get_start(np->ram_res);
	}

	/*
	 *  Save setting of some IO registers, so we will
	 *  be able to probe specific implementations.
	 */
	sym_save_initial_setting (np);

	/*
	 *  Reset the chip now, since it has been reported
	 *  that SCSI clock calibration may not work properly
	 *  if the chip is currently active.
	 */
	sym_chip_reset (np);

	/*
	 *  Try to read the user set-up.
	 */
	(void) sym_read_nvram(np, &nvram);

	/*
	 *  Prepare controller and devices settings, according
	 *  to chip features, user set-up and driver set-up.
	 */
	(void) sym_prepare_setting(np, &nvram);

	/*
	 *  Check the PCI clock frequency.
	 *  Must be performed after prepare_setting since it destroys
	 *  STEST1 that is used to probe for the clock doubler.
	 */
	i = sym_getpciclock(np);
	if (i > 37000)
		device_printf(dev, "PCI BUS clock seems too high: %u KHz.\n",i);

	/*
	 *  Allocate the start queue.
	 */
	np->squeue = (u32 *) sym_calloc_dma(sizeof(u32)*(MAX_QUEUE*2),"SQUEUE");
	if (!np->squeue)
		goto attach_failed;
	np->squeue_ba = vtobus(np->squeue);

	/*
	 *  Allocate the done queue.
	 */
	np->dqueue = (u32 *) sym_calloc_dma(sizeof(u32)*(MAX_QUEUE*2),"DQUEUE");
	if (!np->dqueue)
		goto attach_failed;
	np->dqueue_ba = vtobus(np->dqueue);

	/*
	 *  Allocate the target bus address array.
	 */
	np->targtbl = (u32 *) sym_calloc_dma(256, "TARGTBL");
	if (!np->targtbl)
		goto attach_failed;
	np->targtbl_ba = vtobus(np->targtbl);

	/*
	 *  Allocate SCRIPTS areas.
	 */
	np->scripta0 = sym_calloc_dma(np->scripta_sz, "SCRIPTA0");
	np->scriptb0 = sym_calloc_dma(np->scriptb_sz, "SCRIPTB0");
	if (!np->scripta0 || !np->scriptb0)
		goto attach_failed;

	/*
	 *  Allocate the CCBs. We need at least ONE.
	 */
	for (i = 0; sym_alloc_ccb(np) != NULL; i++)
		;
	if (i < 1)
		goto attach_failed;

	/*
	 *  Calculate BUS addresses where we are going
	 *  to load the SCRIPTS.
	 */
	np->scripta_ba	= vtobus(np->scripta0);
	np->scriptb_ba	= vtobus(np->scriptb0);
	np->scriptb0_ba	= np->scriptb_ba;

	if (np->ram_ba) {
		np->scripta_ba	= np->ram_ba;
		if (np->features & FE_RAM8K) {
			np->ram_ws = 8192;
			np->scriptb_ba = np->scripta_ba + 4096;
#ifdef __LP64__
			np->scr_ram_seg = cpu_to_scr(np->scripta_ba >> 32);
#endif
		}
		else
			np->ram_ws = 4096;
	}

	/*
	 *  Copy scripts to controller instance.
	 */
	bcopy(fw->a_base, np->scripta0, np->scripta_sz);
	bcopy(fw->b_base, np->scriptb0, np->scriptb_sz);

	/*
	 *  Setup variable parts in scripts and compute
	 *  scripts bus addresses used from the C code.
	 */
	np->fw_setup(np, fw);

	/*
	 *  Bind SCRIPTS with physical addresses usable by the
	 *  SCRIPTS processor (as seen from the BUS = BUS addresses).
	 */
	sym_fw_bind_script(np, (u32 *) np->scripta0, np->scripta_sz);
	sym_fw_bind_script(np, (u32 *) np->scriptb0, np->scriptb_sz);

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *    If user wants IARB to be set when we win arbitration
	 *    and have other jobs, compute the max number of consecutive
	 *    settings of IARB hints before we leave devices a chance to
	 *    arbitrate for reselection.
	 */
#ifdef	SYM_SETUP_IARB_MAX
	np->iarb_max = SYM_SETUP_IARB_MAX;
#else
	np->iarb_max = 4;
#endif
#endif

	/*
	 *  Prepare the idle and invalid task actions.
	 */
	np->idletask.start	= cpu_to_scr(SCRIPTA_BA (np, idle));
	np->idletask.restart	= cpu_to_scr(SCRIPTB_BA (np, bad_i_t_l));
	np->idletask_ba		= vtobus(&np->idletask);

	np->notask.start	= cpu_to_scr(SCRIPTA_BA (np, idle));
	np->notask.restart	= cpu_to_scr(SCRIPTB_BA (np, bad_i_t_l));
	np->notask_ba		= vtobus(&np->notask);

	np->bad_itl.start	= cpu_to_scr(SCRIPTA_BA (np, idle));
	np->bad_itl.restart	= cpu_to_scr(SCRIPTB_BA (np, bad_i_t_l));
	np->bad_itl_ba		= vtobus(&np->bad_itl);

	np->bad_itlq.start	= cpu_to_scr(SCRIPTA_BA (np, idle));
	np->bad_itlq.restart	= cpu_to_scr(SCRIPTB_BA (np,bad_i_t_l_q));
	np->bad_itlq_ba		= vtobus(&np->bad_itlq);

	/*
	 *  Allocate and prepare the lun JUMP table that is used
	 *  for a target prior the probing of devices (bad lun table).
	 *  A private table will be allocated for the target on the
	 *  first INQUIRY response received.
	 */
	np->badluntbl = sym_calloc_dma(256, "BADLUNTBL");
	if (!np->badluntbl)
		goto attach_failed;

	np->badlun_sa = cpu_to_scr(SCRIPTB_BA (np, resel_bad_lun));
	for (i = 0 ; i < 64 ; i++)	/* 64 luns/target, no less */
		np->badluntbl[i] = cpu_to_scr(vtobus(&np->badlun_sa));

	/*
	 *  Prepare the bus address array that contains the bus
	 *  address of each target control block.
	 *  For now, assume all logical units are wrong. :)
	 */
	for (i = 0 ; i < SYM_CONF_MAX_TARGET ; i++) {
		np->targtbl[i] = cpu_to_scr(vtobus(&np->target[i]));
		np->target[i].head.luntbl_sa =
				cpu_to_scr(vtobus(np->badluntbl));
		np->target[i].head.lun0_sa =
				cpu_to_scr(vtobus(&np->badlun_sa));
	}

	/*
	 *  Now check the cache handling of the pci chipset.
	 */
	if (sym_snooptest (np)) {
		device_printf(dev, "CACHE INCORRECTLY CONFIGURED.\n");
		goto attach_failed;
	}

	/*
	 *  Now deal with CAM.
	 *  Hopefully, we will succeed with that one.:)
	 */
	if (!sym_cam_attach(np))
		goto attach_failed;

	/*
	 *  Sigh! we are done.
	 */
	return 0;

	/*
	 *  We have failed.
	 *  We will try to free all the resources we have
	 *  allocated, but if we are a boot device, this
	 *  will not help that much.;)
	 */
attach_failed:
	if (np)
		sym_pci_free(np);
	return ENXIO;
}

/*
 *  Free everything that have been allocated for this device.
 */
static void sym_pci_free(hcb_p np)
{
	SYM_QUEHEAD *qp;
	ccb_p cp;
	tcb_p tp;
	lcb_p lp;
	int target, lun;

	/*
	 *  First free CAM resources.
	 */
	sym_cam_free(np);

	/*
	 *  Now every should be quiet for us to
	 *  free other resources.
	 */
	if (np->ram_res)
		bus_release_resource(np->device, SYS_RES_MEMORY,
				     np->ram_id, np->ram_res);
	if (np->mmio_res)
		bus_release_resource(np->device, SYS_RES_MEMORY,
				     SYM_PCI_MMIO, np->mmio_res);
	if (np->io_res)
		bus_release_resource(np->device, SYS_RES_IOPORT,
				     SYM_PCI_IO, np->io_res);
	if (np->irq_res)
		bus_release_resource(np->device, SYS_RES_IRQ,
				     0, np->irq_res);

	if (np->scriptb0)
		sym_mfree_dma(np->scriptb0, np->scriptb_sz, "SCRIPTB0");
	if (np->scripta0)
		sym_mfree_dma(np->scripta0, np->scripta_sz, "SCRIPTA0");
	if (np->squeue)
		sym_mfree_dma(np->squeue, sizeof(u32)*(MAX_QUEUE*2), "SQUEUE");
	if (np->dqueue)
		sym_mfree_dma(np->dqueue, sizeof(u32)*(MAX_QUEUE*2), "DQUEUE");

	while ((qp = sym_remque_head(&np->free_ccbq)) != NULL) {
		cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		bus_dmamap_destroy(np->data_dmat, cp->dmamap);
		sym_mfree_dma(cp->sns_bbuf, SYM_SNS_BBUF_LEN, "SNS_BBUF");
		sym_mfree_dma(cp, sizeof(*cp), "CCB");
	}

	if (np->badluntbl)
		sym_mfree_dma(np->badluntbl, 256,"BADLUNTBL");

	for (target = 0; target < SYM_CONF_MAX_TARGET ; target++) {
		tp = &np->target[target];
		for (lun = 0 ; lun < SYM_CONF_MAX_LUN ; lun++) {
			lp = sym_lp(tp, lun);
			if (!lp)
				continue;
			if (lp->itlq_tbl)
				sym_mfree_dma(lp->itlq_tbl, SYM_CONF_MAX_TASK*4,
				       "ITLQ_TBL");
			if (lp->cb_tags)
				sym_mfree(lp->cb_tags, SYM_CONF_MAX_TASK,
				       "CB_TAGS");
			sym_mfree_dma(lp, sizeof(*lp), "LCB");
		}
#if SYM_CONF_MAX_LUN > 1
		if (tp->lunmp)
			sym_mfree(tp->lunmp, SYM_CONF_MAX_LUN*sizeof(lcb_p),
			       "LUNMP");
#endif
	}
#ifdef __amd64__
	if (np->target)
		sym_mfree_dma(np->target,
			SYM_CONF_MAX_TARGET * sizeof(*(np->target)), "TARGET");
#endif
	if (np->targtbl)
		sym_mfree_dma(np->targtbl, 256, "TARGTBL");
	if (np->data_dmat)
		bus_dma_tag_destroy(np->data_dmat);
	if (SYM_LOCK_INITIALIZED() != 0)
		SYM_LOCK_DESTROY();
	device_set_softc(np->device, NULL);
	sym_mfree_dma(np, sizeof(*np), "HCB");
}

/*
 *  Allocate CAM resources and register a bus to CAM.
 */
static int sym_cam_attach(hcb_p np)
{
	struct cam_devq *devq = NULL;
	struct cam_sim *sim = NULL;
	struct cam_path *path = NULL;
	int err;

	/*
	 *  Establish our interrupt handler.
	 */
	err = bus_setup_intr(np->device, np->irq_res,
			INTR_ENTROPY | INTR_MPSAFE | INTR_TYPE_CAM,
			NULL, sym_intr, np, &np->intr);
	if (err) {
		device_printf(np->device, "bus_setup_intr() failed: %d\n",
			      err);
		goto fail;
	}

	/*
	 *  Create the device queue for our sym SIM.
	 */
	devq = cam_simq_alloc(SYM_CONF_MAX_START);
	if (!devq)
		goto fail;

	/*
	 *  Construct our SIM entry.
	 */
	sim = cam_sim_alloc(sym_action, sym_poll, "sym", np,
			device_get_unit(np->device),
			&np->mtx, 1, SYM_SETUP_MAX_TAG, devq);
	if (!sim)
		goto fail;

	SYM_LOCK();

	if (xpt_bus_register(sim, np->device, 0) != CAM_SUCCESS)
		goto fail;
	np->sim = sim;
	sim = NULL;

	if (xpt_create_path(&path, NULL,
			    cam_sim_path(np->sim), CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		goto fail;
	}
	np->path = path;

	/*
	 *  Establish our async notification handler.
	 */
	if (xpt_register_async(AC_LOST_DEVICE, sym_async, np->sim, path) !=
	    CAM_REQ_CMP)
		goto fail;

	/*
	 *  Start the chip now, without resetting the BUS, since
	 *  it seems that this must stay under control of CAM.
	 *  With LVD/SE capable chips and BUS in SE mode, we may
	 *  get a spurious SMBC interrupt.
	 */
	sym_init (np, 0);

	SYM_UNLOCK();

	return 1;
fail:
	if (sim)
		cam_sim_free(sim, FALSE);
	if (devq)
		cam_simq_free(devq);

	SYM_UNLOCK();

	sym_cam_free(np);

	return 0;
}

/*
 *  Free everything that deals with CAM.
 */
static void sym_cam_free(hcb_p np)
{

	SYM_LOCK_ASSERT(MA_NOTOWNED);

	if (np->intr) {
		bus_teardown_intr(np->device, np->irq_res, np->intr);
		np->intr = NULL;
	}

	SYM_LOCK();

	if (np->sim) {
		xpt_bus_deregister(cam_sim_path(np->sim));
		cam_sim_free(np->sim, /*free_devq*/ TRUE);
		np->sim = NULL;
	}
	if (np->path) {
		xpt_free_path(np->path);
		np->path = NULL;
	}

	SYM_UNLOCK();
}

/*============ OPTIONNAL NVRAM SUPPORT =================*/

/*
 *  Get host setup from NVRAM.
 */
static void sym_nvram_setup_host (hcb_p np, struct sym_nvram *nvram)
{
#ifdef SYM_CONF_NVRAM_SUPPORT
	/*
	 *  Get parity checking, host ID, verbose mode
	 *  and miscellaneous host flags from NVRAM.
	 */
	switch(nvram->type) {
	case SYM_SYMBIOS_NVRAM:
		if (!(nvram->data.Symbios.flags & SYMBIOS_PARITY_ENABLE))
			np->rv_scntl0  &= ~0x0a;
		np->myaddr = nvram->data.Symbios.host_id & 0x0f;
		if (nvram->data.Symbios.flags & SYMBIOS_VERBOSE_MSGS)
			np->verbose += 1;
		if (nvram->data.Symbios.flags1 & SYMBIOS_SCAN_HI_LO)
			np->usrflags |= SYM_SCAN_TARGETS_HILO;
		if (nvram->data.Symbios.flags2 & SYMBIOS_AVOID_BUS_RESET)
			np->usrflags |= SYM_AVOID_BUS_RESET;
		break;
	case SYM_TEKRAM_NVRAM:
		np->myaddr = nvram->data.Tekram.host_id & 0x0f;
		break;
	default:
		break;
	}
#endif
}

/*
 *  Get target setup from NVRAM.
 */
#ifdef SYM_CONF_NVRAM_SUPPORT
static void sym_Symbios_setup_target(hcb_p np,int target, Symbios_nvram *nvram);
static void sym_Tekram_setup_target(hcb_p np,int target, Tekram_nvram *nvram);
#endif

static void
sym_nvram_setup_target (hcb_p np, int target, struct sym_nvram *nvp)
{
#ifdef SYM_CONF_NVRAM_SUPPORT
	switch(nvp->type) {
	case SYM_SYMBIOS_NVRAM:
		sym_Symbios_setup_target (np, target, &nvp->data.Symbios);
		break;
	case SYM_TEKRAM_NVRAM:
		sym_Tekram_setup_target (np, target, &nvp->data.Tekram);
		break;
	default:
		break;
	}
#endif
}

#ifdef SYM_CONF_NVRAM_SUPPORT
/*
 *  Get target set-up from Symbios format NVRAM.
 */
static void
sym_Symbios_setup_target(hcb_p np, int target, Symbios_nvram *nvram)
{
	tcb_p tp = &np->target[target];
	Symbios_target *tn = &nvram->target[target];

	tp->tinfo.user.period = tn->sync_period ? (tn->sync_period + 3) / 4 : 0;
	tp->tinfo.user.width  = tn->bus_width == 0x10 ? BUS_16_BIT : BUS_8_BIT;
	tp->usrtags =
		(tn->flags & SYMBIOS_QUEUE_TAGS_ENABLED)? SYM_SETUP_MAX_TAG : 0;

	if (!(tn->flags & SYMBIOS_DISCONNECT_ENABLE))
		tp->usrflags &= ~SYM_DISC_ENABLED;
	if (!(tn->flags & SYMBIOS_SCAN_AT_BOOT_TIME))
		tp->usrflags |= SYM_SCAN_BOOT_DISABLED;
	if (!(tn->flags & SYMBIOS_SCAN_LUNS))
		tp->usrflags |= SYM_SCAN_LUNS_DISABLED;
}

/*
 *  Get target set-up from Tekram format NVRAM.
 */
static void
sym_Tekram_setup_target(hcb_p np, int target, Tekram_nvram *nvram)
{
	tcb_p tp = &np->target[target];
	struct Tekram_target *tn = &nvram->target[target];
	int i;

	if (tn->flags & TEKRAM_SYNC_NEGO) {
		i = tn->sync_index & 0xf;
		tp->tinfo.user.period = Tekram_sync[i];
	}

	tp->tinfo.user.width =
		(tn->flags & TEKRAM_WIDE_NEGO) ? BUS_16_BIT : BUS_8_BIT;

	if (tn->flags & TEKRAM_TAGGED_COMMANDS) {
		tp->usrtags = 2 << nvram->max_tags_index;
	}

	if (tn->flags & TEKRAM_DISCONNECT_ENABLE)
		tp->usrflags |= SYM_DISC_ENABLED;

	/* If any device does not support parity, we will not use this option */
	if (!(tn->flags & TEKRAM_PARITY_CHECK))
		np->rv_scntl0  &= ~0x0a; /* SCSI parity checking disabled */
}

#ifdef	SYM_CONF_DEBUG_NVRAM
/*
 *  Dump Symbios format NVRAM for debugging purpose.
 */
static void sym_display_Symbios_nvram(hcb_p np, Symbios_nvram *nvram)
{
	int i;

	/* display Symbios nvram host data */
	printf("%s: HOST ID=%d%s%s%s%s%s%s\n",
		sym_name(np), nvram->host_id & 0x0f,
		(nvram->flags  & SYMBIOS_SCAM_ENABLE)	? " SCAM"	:"",
		(nvram->flags  & SYMBIOS_PARITY_ENABLE)	? " PARITY"	:"",
		(nvram->flags  & SYMBIOS_VERBOSE_MSGS)	? " VERBOSE"	:"",
		(nvram->flags  & SYMBIOS_CHS_MAPPING)	? " CHS_ALT"	:"",
		(nvram->flags2 & SYMBIOS_AVOID_BUS_RESET)?" NO_RESET"	:"",
		(nvram->flags1 & SYMBIOS_SCAN_HI_LO)	? " HI_LO"	:"");

	/* display Symbios nvram drive data */
	for (i = 0 ; i < 15 ; i++) {
		struct Symbios_target *tn = &nvram->target[i];
		printf("%s-%d:%s%s%s%s WIDTH=%d SYNC=%d TMO=%d\n",
		sym_name(np), i,
		(tn->flags & SYMBIOS_DISCONNECT_ENABLE)	? " DISC"	: "",
		(tn->flags & SYMBIOS_SCAN_AT_BOOT_TIME)	? " SCAN_BOOT"	: "",
		(tn->flags & SYMBIOS_SCAN_LUNS)		? " SCAN_LUNS"	: "",
		(tn->flags & SYMBIOS_QUEUE_TAGS_ENABLED)? " TCQ"	: "",
		tn->bus_width,
		tn->sync_period / 4,
		tn->timeout);
	}
}

/*
 *  Dump TEKRAM format NVRAM for debugging purpose.
 */
static const u_char Tekram_boot_delay[7] = {3, 5, 10, 20, 30, 60, 120};
static void sym_display_Tekram_nvram(hcb_p np, Tekram_nvram *nvram)
{
	int i, tags, boot_delay;
	char *rem;

	/* display Tekram nvram host data */
	tags = 2 << nvram->max_tags_index;
	boot_delay = 0;
	if (nvram->boot_delay_index < 6)
		boot_delay = Tekram_boot_delay[nvram->boot_delay_index];
	switch((nvram->flags & TEKRAM_REMOVABLE_FLAGS) >> 6) {
	default:
	case 0:	rem = "";			break;
	case 1: rem = " REMOVABLE=boot device";	break;
	case 2: rem = " REMOVABLE=all";		break;
	}

	printf("%s: HOST ID=%d%s%s%s%s%s%s%s%s%s BOOT DELAY=%d tags=%d\n",
		sym_name(np), nvram->host_id & 0x0f,
		(nvram->flags1 & SYMBIOS_SCAM_ENABLE)	? " SCAM"	:"",
		(nvram->flags & TEKRAM_MORE_THAN_2_DRIVES) ? " >2DRIVES"	:"",
		(nvram->flags & TEKRAM_DRIVES_SUP_1GB)	? " >1GB"	:"",
		(nvram->flags & TEKRAM_RESET_ON_POWER_ON) ? " RESET"	:"",
		(nvram->flags & TEKRAM_ACTIVE_NEGATION)	? " ACT_NEG"	:"",
		(nvram->flags & TEKRAM_IMMEDIATE_SEEK)	? " IMM_SEEK"	:"",
		(nvram->flags & TEKRAM_SCAN_LUNS)	? " SCAN_LUNS"	:"",
		(nvram->flags1 & TEKRAM_F2_F6_ENABLED)	? " F2_F6"	:"",
		rem, boot_delay, tags);

	/* display Tekram nvram drive data */
	for (i = 0; i <= 15; i++) {
		int sync, j;
		struct Tekram_target *tn = &nvram->target[i];
		j = tn->sync_index & 0xf;
		sync = Tekram_sync[j];
		printf("%s-%d:%s%s%s%s%s%s PERIOD=%d\n",
		sym_name(np), i,
		(tn->flags & TEKRAM_PARITY_CHECK)	? " PARITY"	: "",
		(tn->flags & TEKRAM_SYNC_NEGO)		? " SYNC"	: "",
		(tn->flags & TEKRAM_DISCONNECT_ENABLE)	? " DISC"	: "",
		(tn->flags & TEKRAM_START_CMD)		? " START"	: "",
		(tn->flags & TEKRAM_TAGGED_COMMANDS)	? " TCQ"	: "",
		(tn->flags & TEKRAM_WIDE_NEGO)		? " WIDE"	: "",
		sync);
	}
}
#endif	/* SYM_CONF_DEBUG_NVRAM */
#endif	/* SYM_CONF_NVRAM_SUPPORT */

/*
 *  Try reading Symbios or Tekram NVRAM
 */
#ifdef SYM_CONF_NVRAM_SUPPORT
static int sym_read_Symbios_nvram (hcb_p np, Symbios_nvram *nvram);
static int sym_read_Tekram_nvram  (hcb_p np, Tekram_nvram *nvram);
#endif

static int sym_read_nvram(hcb_p np, struct sym_nvram *nvp)
{
#ifdef SYM_CONF_NVRAM_SUPPORT
	/*
	 *  Try to read SYMBIOS nvram.
	 *  Try to read TEKRAM nvram if Symbios nvram not found.
	 */
	if	(SYM_SETUP_SYMBIOS_NVRAM &&
		 !sym_read_Symbios_nvram (np, &nvp->data.Symbios)) {
		nvp->type = SYM_SYMBIOS_NVRAM;
#ifdef SYM_CONF_DEBUG_NVRAM
		sym_display_Symbios_nvram(np, &nvp->data.Symbios);
#endif
	}
	else if	(SYM_SETUP_TEKRAM_NVRAM &&
		 !sym_read_Tekram_nvram (np, &nvp->data.Tekram)) {
		nvp->type = SYM_TEKRAM_NVRAM;
#ifdef SYM_CONF_DEBUG_NVRAM
		sym_display_Tekram_nvram(np, &nvp->data.Tekram);
#endif
	}
	else
		nvp->type = 0;
#else
	nvp->type = 0;
#endif
	return nvp->type;
}

#ifdef SYM_CONF_NVRAM_SUPPORT
/*
 *  24C16 EEPROM reading.
 *
 *  GPOI0 - data in/data out
 *  GPIO1 - clock
 *  Symbios NVRAM wiring now also used by Tekram.
 */

#define SET_BIT 0
#define CLR_BIT 1
#define SET_CLK 2
#define CLR_CLK 3

/*
 *  Set/clear data/clock bit in GPIO0
 */
static void S24C16_set_bit(hcb_p np, u_char write_bit, u_char *gpreg,
			  int bit_mode)
{
	UDELAY (5);
	switch (bit_mode){
	case SET_BIT:
		*gpreg |= write_bit;
		break;
	case CLR_BIT:
		*gpreg &= 0xfe;
		break;
	case SET_CLK:
		*gpreg |= 0x02;
		break;
	case CLR_CLK:
		*gpreg &= 0xfd;
		break;

	}
	OUTB (nc_gpreg, *gpreg);
	UDELAY (5);
}

/*
 *  Send START condition to NVRAM to wake it up.
 */
static void S24C16_start(hcb_p np, u_char *gpreg)
{
	S24C16_set_bit(np, 1, gpreg, SET_BIT);
	S24C16_set_bit(np, 0, gpreg, SET_CLK);
	S24C16_set_bit(np, 0, gpreg, CLR_BIT);
	S24C16_set_bit(np, 0, gpreg, CLR_CLK);
}

/*
 *  Send STOP condition to NVRAM - puts NVRAM to sleep... ZZzzzz!!
 */
static void S24C16_stop(hcb_p np, u_char *gpreg)
{
	S24C16_set_bit(np, 0, gpreg, SET_CLK);
	S24C16_set_bit(np, 1, gpreg, SET_BIT);
}

/*
 *  Read or write a bit to the NVRAM,
 *  read if GPIO0 input else write if GPIO0 output
 */
static void S24C16_do_bit(hcb_p np, u_char *read_bit, u_char write_bit,
			 u_char *gpreg)
{
	S24C16_set_bit(np, write_bit, gpreg, SET_BIT);
	S24C16_set_bit(np, 0, gpreg, SET_CLK);
	if (read_bit)
		*read_bit = INB (nc_gpreg);
	S24C16_set_bit(np, 0, gpreg, CLR_CLK);
	S24C16_set_bit(np, 0, gpreg, CLR_BIT);
}

/*
 *  Output an ACK to the NVRAM after reading,
 *  change GPIO0 to output and when done back to an input
 */
static void S24C16_write_ack(hcb_p np, u_char write_bit, u_char *gpreg,
			    u_char *gpcntl)
{
	OUTB (nc_gpcntl, *gpcntl & 0xfe);
	S24C16_do_bit(np, 0, write_bit, gpreg);
	OUTB (nc_gpcntl, *gpcntl);
}

/*
 *  Input an ACK from NVRAM after writing,
 *  change GPIO0 to input and when done back to an output
 */
static void S24C16_read_ack(hcb_p np, u_char *read_bit, u_char *gpreg,
			   u_char *gpcntl)
{
	OUTB (nc_gpcntl, *gpcntl | 0x01);
	S24C16_do_bit(np, read_bit, 1, gpreg);
	OUTB (nc_gpcntl, *gpcntl);
}

/*
 *  WRITE a byte to the NVRAM and then get an ACK to see it was accepted OK,
 *  GPIO0 must already be set as an output
 */
static void S24C16_write_byte(hcb_p np, u_char *ack_data, u_char write_data,
			     u_char *gpreg, u_char *gpcntl)
{
	int x;

	for (x = 0; x < 8; x++)
		S24C16_do_bit(np, 0, (write_data >> (7 - x)) & 0x01, gpreg);

	S24C16_read_ack(np, ack_data, gpreg, gpcntl);
}

/*
 *  READ a byte from the NVRAM and then send an ACK to say we have got it,
 *  GPIO0 must already be set as an input
 */
static void S24C16_read_byte(hcb_p np, u_char *read_data, u_char ack_data,
			    u_char *gpreg, u_char *gpcntl)
{
	int x;
	u_char read_bit;

	*read_data = 0;
	for (x = 0; x < 8; x++) {
		S24C16_do_bit(np, &read_bit, 1, gpreg);
		*read_data |= ((read_bit & 0x01) << (7 - x));
	}

	S24C16_write_ack(np, ack_data, gpreg, gpcntl);
}

/*
 *  Read 'len' bytes starting at 'offset'.
 */
static int sym_read_S24C16_nvram (hcb_p np, int offset, u_char *data, int len)
{
	u_char	gpcntl, gpreg;
	u_char	old_gpcntl, old_gpreg;
	u_char	ack_data;
	int	retv = 1;
	int	x;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB (nc_gpreg);
	old_gpcntl	= INB (nc_gpcntl);
	gpcntl		= old_gpcntl & 0x1c;

	/* set up GPREG & GPCNTL to set GPIO0 and GPIO1 in to known state */
	OUTB (nc_gpreg,  old_gpreg);
	OUTB (nc_gpcntl, gpcntl);

	/* this is to set NVRAM into a known state with GPIO0/1 both low */
	gpreg = old_gpreg;
	S24C16_set_bit(np, 0, &gpreg, CLR_CLK);
	S24C16_set_bit(np, 0, &gpreg, CLR_BIT);

	/* now set NVRAM inactive with GPIO0/1 both high */
	S24C16_stop(np, &gpreg);

	/* activate NVRAM */
	S24C16_start(np, &gpreg);

	/* write device code and random address MSB */
	S24C16_write_byte(np, &ack_data,
		0xa0 | ((offset >> 7) & 0x0e), &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* write random address LSB */
	S24C16_write_byte(np, &ack_data,
		offset & 0xff, &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* regenerate START state to set up for reading */
	S24C16_start(np, &gpreg);

	/* rewrite device code and address MSB with read bit set (lsb = 0x01) */
	S24C16_write_byte(np, &ack_data,
		0xa1 | ((offset >> 7) & 0x0e), &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* now set up GPIO0 for inputting data */
	gpcntl |= 0x01;
	OUTB (nc_gpcntl, gpcntl);

	/* input all requested data - only part of total NVRAM */
	for (x = 0; x < len; x++)
		S24C16_read_byte(np, &data[x], (x == (len-1)), &gpreg, &gpcntl);

	/* finally put NVRAM back in inactive mode */
	gpcntl &= 0xfe;
	OUTB (nc_gpcntl, gpcntl);
	S24C16_stop(np, &gpreg);
	retv = 0;
out:
	/* return GPIO0/1 to original states after having accessed NVRAM */
	OUTB (nc_gpcntl, old_gpcntl);
	OUTB (nc_gpreg,  old_gpreg);

	return retv;
}

#undef SET_BIT /* 0 */
#undef CLR_BIT /* 1 */
#undef SET_CLK /* 2 */
#undef CLR_CLK /* 3 */

/*
 *  Try reading Symbios NVRAM.
 *  Return 0 if OK.
 */
static int sym_read_Symbios_nvram (hcb_p np, Symbios_nvram *nvram)
{
	static u_char Symbios_trailer[6] = {0xfe, 0xfe, 0, 0, 0, 0};
	u_char *data = (u_char *) nvram;
	int len  = sizeof(*nvram);
	u_short	csum;
	int x;

	/* probe the 24c16 and read the SYMBIOS 24c16 area */
	if (sym_read_S24C16_nvram (np, SYMBIOS_NVRAM_ADDRESS, data, len))
		return 1;

	/* check valid NVRAM signature, verify byte count and checksum */
	if (nvram->type != 0 ||
	    bcmp(nvram->trailer, Symbios_trailer, 6) ||
	    nvram->byte_count != len - 12)
		return 1;

	/* verify checksum */
	for (x = 6, csum = 0; x < len - 6; x++)
		csum += data[x];
	if (csum != nvram->checksum)
		return 1;

	return 0;
}

/*
 *  93C46 EEPROM reading.
 *
 *  GPOI0 - data in
 *  GPIO1 - data out
 *  GPIO2 - clock
 *  GPIO4 - chip select
 *
 *  Used by Tekram.
 */

/*
 *  Pulse clock bit in GPIO0
 */
static void T93C46_Clk(hcb_p np, u_char *gpreg)
{
	OUTB (nc_gpreg, *gpreg | 0x04);
	UDELAY (2);
	OUTB (nc_gpreg, *gpreg);
}

/*
 *  Read bit from NVRAM
 */
static void T93C46_Read_Bit(hcb_p np, u_char *read_bit, u_char *gpreg)
{
	UDELAY (2);
	T93C46_Clk(np, gpreg);
	*read_bit = INB (nc_gpreg);
}

/*
 *  Write bit to GPIO0
 */
static void T93C46_Write_Bit(hcb_p np, u_char write_bit, u_char *gpreg)
{
	if (write_bit & 0x01)
		*gpreg |= 0x02;
	else
		*gpreg &= 0xfd;

	*gpreg |= 0x10;

	OUTB (nc_gpreg, *gpreg);
	UDELAY (2);

	T93C46_Clk(np, gpreg);
}

/*
 *  Send STOP condition to NVRAM - puts NVRAM to sleep... ZZZzzz!!
 */
static void T93C46_Stop(hcb_p np, u_char *gpreg)
{
	*gpreg &= 0xef;
	OUTB (nc_gpreg, *gpreg);
	UDELAY (2);

	T93C46_Clk(np, gpreg);
}

/*
 *  Send read command and address to NVRAM
 */
static void T93C46_Send_Command(hcb_p np, u_short write_data,
				u_char *read_bit, u_char *gpreg)
{
	int x;

	/* send 9 bits, start bit (1), command (2), address (6)  */
	for (x = 0; x < 9; x++)
		T93C46_Write_Bit(np, (u_char) (write_data >> (8 - x)), gpreg);

	*read_bit = INB (nc_gpreg);
}

/*
 *  READ 2 bytes from the NVRAM
 */
static void T93C46_Read_Word(hcb_p np, u_short *nvram_data, u_char *gpreg)
{
	int x;
	u_char read_bit;

	*nvram_data = 0;
	for (x = 0; x < 16; x++) {
		T93C46_Read_Bit(np, &read_bit, gpreg);

		if (read_bit & 0x01)
			*nvram_data |=  (0x01 << (15 - x));
		else
			*nvram_data &= ~(0x01 << (15 - x));
	}
}

/*
 *  Read Tekram NvRAM data.
 */
static int T93C46_Read_Data(hcb_p np, u_short *data,int len,u_char *gpreg)
{
	u_char	read_bit;
	int	x;

	for (x = 0; x < len; x++)  {

		/* output read command and address */
		T93C46_Send_Command(np, 0x180 | x, &read_bit, gpreg);
		if (read_bit & 0x01)
			return 1; /* Bad */
		T93C46_Read_Word(np, &data[x], gpreg);
		T93C46_Stop(np, gpreg);
	}

	return 0;
}

/*
 *  Try reading 93C46 Tekram NVRAM.
 */
static int sym_read_T93C46_nvram (hcb_p np, Tekram_nvram *nvram)
{
	u_char gpcntl, gpreg;
	u_char old_gpcntl, old_gpreg;
	int retv = 1;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB (nc_gpreg);
	old_gpcntl	= INB (nc_gpcntl);

	/* set up GPREG & GPCNTL to set GPIO0/1/2/4 in to known state, 0 in,
	   1/2/4 out */
	gpreg = old_gpreg & 0xe9;
	OUTB (nc_gpreg, gpreg);
	gpcntl = (old_gpcntl & 0xe9) | 0x09;
	OUTB (nc_gpcntl, gpcntl);

	/* input all of NVRAM, 64 words */
	retv = T93C46_Read_Data(np, (u_short *) nvram,
				sizeof(*nvram) / sizeof(short), &gpreg);

	/* return GPIO0/1/2/4 to original states after having accessed NVRAM */
	OUTB (nc_gpcntl, old_gpcntl);
	OUTB (nc_gpreg,  old_gpreg);

	return retv;
}

/*
 *  Try reading Tekram NVRAM.
 *  Return 0 if OK.
 */
static int sym_read_Tekram_nvram (hcb_p np, Tekram_nvram *nvram)
{
	u_char *data = (u_char *) nvram;
	int len = sizeof(*nvram);
	u_short	csum;
	int x;

	switch (np->device_id) {
	case PCI_ID_SYM53C885:
	case PCI_ID_SYM53C895:
	case PCI_ID_SYM53C896:
		x = sym_read_S24C16_nvram(np, TEKRAM_24C16_NVRAM_ADDRESS,
					  data, len);
		break;
	case PCI_ID_SYM53C875:
		x = sym_read_S24C16_nvram(np, TEKRAM_24C16_NVRAM_ADDRESS,
					  data, len);
		if (!x)
			break;
	default:
		x = sym_read_T93C46_nvram(np, nvram);
		break;
	}
	if (x)
		return 1;

	/* verify checksum */
	for (x = 0, csum = 0; x < len - 1; x += 2)
		csum += data[x] + (data[x+1] << 8);
	if (csum != 0x1234)
		return 1;

	return 0;
}

#endif	/* SYM_CONF_NVRAM_SUPPORT */
