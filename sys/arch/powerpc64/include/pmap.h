/*	$OpenBSD: pmap.h,v 1.19 2023/12/11 22:12:53 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#ifndef _MACHINE_PMAP_H_
#define _MACHINE_PMAP_H_

#include <sys/mutex.h>
#include <sys/queue.h>

#ifdef _KERNEL

#include <machine/pte.h>

/* V->P mapping data */
#define VP_IDX1_CNT	256
#define VP_IDX1_MASK	(VP_IDX1_CNT - 1)
#define VP_IDX1_POS 	20
#define VP_IDX2_CNT	256
#define VP_IDX2_MASK	(VP_IDX2_CNT - 1)
#define VP_IDX2_POS 	12

struct pmap {
	LIST_HEAD(,slb_desc)	pm_slbd;
	int			pm_refs;
	struct pmap_statistics	pm_stats;
	struct mutex		pm_mtx;
};

typedef struct pmap *pmap_t;

#define PG_PMAP_MOD	PG_PMAP0
#define PG_PMAP_REF	PG_PMAP1
#define PG_PMAP_EXE	PG_PMAP2
#define PG_PMAP_UC	PG_PMAP3

#define PMAP_CACHE_DEFAULT	0
#define PMAP_CACHE_CI		1	/* cache inhibit */
#define PMAP_CACHE_WB		3	/* write-back cached */

/*
 * MD flags that we use for pmap_enter (in the pa):
 */
#define PMAP_PA_MASK	~((paddr_t)PAGE_MASK) /* to remove the flags */
#define PMAP_NOCACHE	0x1		/* map uncached */

extern struct pmap kernel_pmap_store;

#define pmap_kernel()	(&kernel_pmap_store)
#define pmap_resident_count(pm) ((pm)->pm_stats.resident_count)
#define pmap_wired_count(pm)	((pm)->pm_stats.wired_count)

#define pmap_init_percpu()		do { /* nothing */ } while (0)
#define pmap_unuse_final(p)
#define pmap_remove_holes(vm)
#define pmap_update(pm)

void	pmap_bootstrap(void);
void	pmap_bootstrap_cpu(void);

int	pmap_slbd_fault(pmap_t, vaddr_t);
int	pmap_slbd_enter(pmap_t, vaddr_t);
int	pmap_set_user_slb(pmap_t, vaddr_t, vaddr_t *, vsize_t *);
void	pmap_clear_user_slb(void);
void	pmap_unset_user_slb(void);

#ifdef DDB
struct pte;
struct pte *pmap_get_kernel_pte(vaddr_t);
#endif

#endif	/* _KERNEL */

struct vm_page_md {
	struct mutex pv_mtx;
	LIST_HEAD(,pte_desc) pv_list;
};

#define VM_MDPAGE_INIT(pg) do {                 \
	mtx_init(&(pg)->mdpage.pv_mtx, IPL_VM); \
	LIST_INIT(&((pg)->mdpage.pv_list)); 	\
} while (0)

#endif /* _MACHINE_PMAP_H_ */
