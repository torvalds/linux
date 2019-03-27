/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2014 Matteo Landi
 * Copyright (C) 2012-2016 Luigi Rizzo
 * Copyright (C) 2012-2016 Giuseppe Lettieri
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 */

#ifdef linux
#include "bsd_glue.h"
#endif /* linux */

#ifdef __APPLE__
#include "osx_glue.h"
#endif /* __APPLE__ */

#ifdef __FreeBSD__
#include <sys/cdefs.h> /* prerequisite */
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/kernel.h>		/* MALLOC_DEFINE */
#include <sys/proc.h>
#include <vm/vm.h>	/* vtophys */
#include <vm/pmap.h>	/* vtophys */
#include <sys/socket.h> /* sockaddrs */
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>
#include <machine/bus.h>	/* bus_dmamap_* */

/* M_NETMAP only used in here */
MALLOC_DECLARE(M_NETMAP);
MALLOC_DEFINE(M_NETMAP, "netmap", "Network memory map");

#endif /* __FreeBSD__ */

#ifdef _WIN32
#include <win_glue.h>
#endif

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <net/netmap_virt.h>
#include "netmap_mem2.h"

#ifdef _WIN32_USE_SMALL_GENERIC_DEVICES_MEMORY
#define NETMAP_BUF_MAX_NUM  8*4096      /* if too big takes too much time to allocate */
#else
#define NETMAP_BUF_MAX_NUM 20*4096*2	/* large machine */
#endif

#define NETMAP_POOL_MAX_NAMSZ	32


enum {
	NETMAP_IF_POOL   = 0,
	NETMAP_RING_POOL,
	NETMAP_BUF_POOL,
	NETMAP_POOLS_NR
};


struct netmap_obj_params {
	u_int size;
	u_int num;

	u_int last_size;
	u_int last_num;
};

struct netmap_obj_pool {
	char name[NETMAP_POOL_MAX_NAMSZ];	/* name of the allocator */

	/* ---------------------------------------------------*/
	/* these are only meaningful if the pool is finalized */
	/* (see 'finalized' field in netmap_mem_d)            */
	u_int objtotal;         /* actual total number of objects. */
	u_int memtotal;		/* actual total memory space */
	u_int numclusters;	/* actual number of clusters */

	u_int objfree;          /* number of free objects. */

	struct lut_entry *lut;  /* virt,phys addresses, objtotal entries */
	uint32_t *bitmap;       /* one bit per buffer, 1 means free */
	uint32_t *invalid_bitmap;/* one bit per buffer, 1 means invalid */
	uint32_t bitmap_slots;	/* number of uint32 entries in bitmap */
	int	alloc_done;	/* we have allocated the memory */
	/* ---------------------------------------------------*/

	/* limits */
	u_int objminsize;	/* minimum object size */
	u_int objmaxsize;	/* maximum object size */
	u_int nummin;		/* minimum number of objects */
	u_int nummax;		/* maximum number of objects */

	/* these are changed only by config */
	u_int _objtotal;	/* total number of objects */
	u_int _objsize;		/* object size */
	u_int _clustsize;       /* cluster size */
	u_int _clustentries;    /* objects per cluster */
	u_int _numclusters;	/* number of clusters */

	/* requested values */
	u_int r_objtotal;
	u_int r_objsize;
};

#define NMA_LOCK_T		NM_MTX_T
#define NMA_LOCK_INIT(n)	NM_MTX_INIT((n)->nm_mtx)
#define NMA_LOCK_DESTROY(n)	NM_MTX_DESTROY((n)->nm_mtx)
#define NMA_LOCK(n)		NM_MTX_LOCK((n)->nm_mtx)
#define NMA_SPINLOCK(n)         NM_MTX_SPINLOCK((n)->nm_mtx)
#define NMA_UNLOCK(n)		NM_MTX_UNLOCK((n)->nm_mtx)

struct netmap_mem_ops {
	int (*nmd_get_lut)(struct netmap_mem_d *, struct netmap_lut*);
	int  (*nmd_get_info)(struct netmap_mem_d *, uint64_t *size,
			u_int *memflags, uint16_t *id);

	vm_paddr_t (*nmd_ofstophys)(struct netmap_mem_d *, vm_ooffset_t);
	int (*nmd_config)(struct netmap_mem_d *);
	int (*nmd_finalize)(struct netmap_mem_d *);
	void (*nmd_deref)(struct netmap_mem_d *);
	ssize_t  (*nmd_if_offset)(struct netmap_mem_d *, const void *vaddr);
	void (*nmd_delete)(struct netmap_mem_d *);

	struct netmap_if * (*nmd_if_new)(struct netmap_adapter *,
					 struct netmap_priv_d *);
	void (*nmd_if_delete)(struct netmap_adapter *, struct netmap_if *);
	int  (*nmd_rings_create)(struct netmap_adapter *);
	void (*nmd_rings_delete)(struct netmap_adapter *);
};

struct netmap_mem_d {
	NMA_LOCK_T nm_mtx;  /* protect the allocator */
	u_int nm_totalsize; /* shorthand */

	u_int flags;
#define NETMAP_MEM_FINALIZED	0x1	/* preallocation done */
#define NETMAP_MEM_HIDDEN	0x8	/* beeing prepared */
	int lasterr;		/* last error for curr config */
	int active;		/* active users */
	int refcount;
	/* the three allocators */
	struct netmap_obj_pool pools[NETMAP_POOLS_NR];

	nm_memid_t nm_id;	/* allocator identifier */
	int nm_grp;	/* iommu groupd id */

	/* list of all existing allocators, sorted by nm_id */
	struct netmap_mem_d *prev, *next;

	struct netmap_mem_ops *ops;

	struct netmap_obj_params params[NETMAP_POOLS_NR];

#define NM_MEM_NAMESZ	16
	char name[NM_MEM_NAMESZ];
};

int
netmap_mem_get_lut(struct netmap_mem_d *nmd, struct netmap_lut *lut)
{
	int rv;

	NMA_LOCK(nmd);
	rv = nmd->ops->nmd_get_lut(nmd, lut);
	NMA_UNLOCK(nmd);

	return rv;
}

int
netmap_mem_get_info(struct netmap_mem_d *nmd, uint64_t *size,
		u_int *memflags, nm_memid_t *memid)
{
	int rv;

	NMA_LOCK(nmd);
	rv = nmd->ops->nmd_get_info(nmd, size, memflags, memid);
	NMA_UNLOCK(nmd);

	return rv;
}

vm_paddr_t
netmap_mem_ofstophys(struct netmap_mem_d *nmd, vm_ooffset_t off)
{
	vm_paddr_t pa;

#if defined(__FreeBSD__)
	/* This function is called by netmap_dev_pager_fault(), which holds a
	 * non-sleepable lock since FreeBSD 12. Since we cannot sleep, we
	 * spin on the trylock. */
	NMA_SPINLOCK(nmd);
#else
	NMA_LOCK(nmd);
#endif
	pa = nmd->ops->nmd_ofstophys(nmd, off);
	NMA_UNLOCK(nmd);

	return pa;
}

static int
netmap_mem_config(struct netmap_mem_d *nmd)
{
	if (nmd->active) {
		/* already in use. Not fatal, but we
		 * cannot change the configuration
		 */
		return 0;
	}

	return nmd->ops->nmd_config(nmd);
}

ssize_t
netmap_mem_if_offset(struct netmap_mem_d *nmd, const void *off)
{
	ssize_t rv;

	NMA_LOCK(nmd);
	rv = nmd->ops->nmd_if_offset(nmd, off);
	NMA_UNLOCK(nmd);

	return rv;
}

static void
netmap_mem_delete(struct netmap_mem_d *nmd)
{
	nmd->ops->nmd_delete(nmd);
}

struct netmap_if *
netmap_mem_if_new(struct netmap_adapter *na, struct netmap_priv_d *priv)
{
	struct netmap_if *nifp;
	struct netmap_mem_d *nmd = na->nm_mem;

	NMA_LOCK(nmd);
	nifp = nmd->ops->nmd_if_new(na, priv);
	NMA_UNLOCK(nmd);

	return nifp;
}

void
netmap_mem_if_delete(struct netmap_adapter *na, struct netmap_if *nif)
{
	struct netmap_mem_d *nmd = na->nm_mem;

	NMA_LOCK(nmd);
	nmd->ops->nmd_if_delete(na, nif);
	NMA_UNLOCK(nmd);
}

int
netmap_mem_rings_create(struct netmap_adapter *na)
{
	int rv;
	struct netmap_mem_d *nmd = na->nm_mem;

	NMA_LOCK(nmd);
	rv = nmd->ops->nmd_rings_create(na);
	NMA_UNLOCK(nmd);

	return rv;
}

void
netmap_mem_rings_delete(struct netmap_adapter *na)
{
	struct netmap_mem_d *nmd = na->nm_mem;

	NMA_LOCK(nmd);
	nmd->ops->nmd_rings_delete(na);
	NMA_UNLOCK(nmd);
}

static int netmap_mem_map(struct netmap_obj_pool *, struct netmap_adapter *);
static int netmap_mem_unmap(struct netmap_obj_pool *, struct netmap_adapter *);
static int nm_mem_assign_group(struct netmap_mem_d *, struct device *);
static void nm_mem_release_id(struct netmap_mem_d *);

nm_memid_t
netmap_mem_get_id(struct netmap_mem_d *nmd)
{
	return nmd->nm_id;
}

#ifdef NM_DEBUG_MEM_PUTGET
#define NM_DBG_REFC(nmd, func, line)	\
	nm_prinf("%d mem[%d] -> %d", line, (nmd)->nm_id, (nmd)->refcount);
#else
#define NM_DBG_REFC(nmd, func, line)
#endif

/* circular list of all existing allocators */
static struct netmap_mem_d *netmap_last_mem_d = &nm_mem;
NM_MTX_T nm_mem_list_lock;

struct netmap_mem_d *
__netmap_mem_get(struct netmap_mem_d *nmd, const char *func, int line)
{
	NM_MTX_LOCK(nm_mem_list_lock);
	nmd->refcount++;
	NM_DBG_REFC(nmd, func, line);
	NM_MTX_UNLOCK(nm_mem_list_lock);
	return nmd;
}

void
__netmap_mem_put(struct netmap_mem_d *nmd, const char *func, int line)
{
	int last;
	NM_MTX_LOCK(nm_mem_list_lock);
	last = (--nmd->refcount == 0);
	if (last)
		nm_mem_release_id(nmd);
	NM_DBG_REFC(nmd, func, line);
	NM_MTX_UNLOCK(nm_mem_list_lock);
	if (last)
		netmap_mem_delete(nmd);
}

int
netmap_mem_finalize(struct netmap_mem_d *nmd, struct netmap_adapter *na)
{
	int lasterr = 0;
	if (nm_mem_assign_group(nmd, na->pdev) < 0) {
		return ENOMEM;
	}

	NMA_LOCK(nmd);

	if (netmap_mem_config(nmd))
		goto out;

	nmd->active++;

	nmd->lasterr = nmd->ops->nmd_finalize(nmd);

	if (!nmd->lasterr && na->pdev) {
		nmd->lasterr = netmap_mem_map(&nmd->pools[NETMAP_BUF_POOL], na);
	}

out:
	lasterr = nmd->lasterr;
	NMA_UNLOCK(nmd);

	if (lasterr)
		netmap_mem_deref(nmd, na);

	return lasterr;
}

static int
nm_isset(uint32_t *bitmap, u_int i)
{
	return bitmap[ (i>>5) ] & ( 1U << (i & 31U) );
}


static int
netmap_init_obj_allocator_bitmap(struct netmap_obj_pool *p)
{
	u_int n, j;

	if (p->bitmap == NULL) {
		/* Allocate the bitmap */
		n = (p->objtotal + 31) / 32;
		p->bitmap = nm_os_malloc(sizeof(p->bitmap[0]) * n);
		if (p->bitmap == NULL) {
			nm_prerr("Unable to create bitmap (%d entries) for allocator '%s'", (int)n,
			    p->name);
			return ENOMEM;
		}
		p->bitmap_slots = n;
	} else {
		memset(p->bitmap, 0, p->bitmap_slots * sizeof(p->bitmap[0]));
	}

	p->objfree = 0;
	/*
	 * Set all the bits in the bitmap that have
	 * corresponding buffers to 1 to indicate they are
	 * free.
	 */
	for (j = 0; j < p->objtotal; j++) {
		if (p->invalid_bitmap && nm_isset(p->invalid_bitmap, j)) {
			if (netmap_debug & NM_DEBUG_MEM)
				nm_prinf("skipping %s %d", p->name, j);
			continue;
		}
		p->bitmap[ (j>>5) ] |=  ( 1U << (j & 31U) );
		p->objfree++;
	}

	if (netmap_verbose)
		nm_prinf("%s free %u", p->name, p->objfree);
	if (p->objfree == 0) {
		if (netmap_verbose)
			nm_prerr("%s: no objects available", p->name);
		return ENOMEM;
	}

	return 0;
}

static int
netmap_mem_init_bitmaps(struct netmap_mem_d *nmd)
{
	int i, error = 0;

	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		struct netmap_obj_pool *p = &nmd->pools[i];

		error = netmap_init_obj_allocator_bitmap(p);
		if (error)
			return error;
	}

	/*
	 * buffers 0 and 1 are reserved
	 */
	if (nmd->pools[NETMAP_BUF_POOL].objfree < 2) {
		nm_prerr("%s: not enough buffers", nmd->pools[NETMAP_BUF_POOL].name);
		return ENOMEM;
	}

	nmd->pools[NETMAP_BUF_POOL].objfree -= 2;
	if (nmd->pools[NETMAP_BUF_POOL].bitmap) {
		/* XXX This check is a workaround that prevents a
		 * NULL pointer crash which currently happens only
		 * with ptnetmap guests.
		 * Removed shared-info --> is the bug still there? */
		nmd->pools[NETMAP_BUF_POOL].bitmap[0] = ~3U;
	}
	return 0;
}

int
netmap_mem_deref(struct netmap_mem_d *nmd, struct netmap_adapter *na)
{
	int last_user = 0;
	NMA_LOCK(nmd);
	if (na->active_fds <= 0)
		netmap_mem_unmap(&nmd->pools[NETMAP_BUF_POOL], na);
	if (nmd->active == 1) {
		last_user = 1;
		/*
		 * Reset the allocator when it falls out of use so that any
		 * pool resources leaked by unclean application exits are
		 * reclaimed.
		 */
		netmap_mem_init_bitmaps(nmd);
	}
	nmd->ops->nmd_deref(nmd);

	nmd->active--;
	if (last_user) {
		nmd->nm_grp = -1;
		nmd->lasterr = 0;
	}

	NMA_UNLOCK(nmd);
	return last_user;
}


/* accessor functions */
static int
netmap_mem2_get_lut(struct netmap_mem_d *nmd, struct netmap_lut *lut)
{
	lut->lut = nmd->pools[NETMAP_BUF_POOL].lut;
#ifdef __FreeBSD__
	lut->plut = lut->lut;
#endif
	lut->objtotal = nmd->pools[NETMAP_BUF_POOL].objtotal;
	lut->objsize = nmd->pools[NETMAP_BUF_POOL]._objsize;

	return 0;
}

static struct netmap_obj_params netmap_min_priv_params[NETMAP_POOLS_NR] = {
	[NETMAP_IF_POOL] = {
		.size = 1024,
		.num  = 2,
	},
	[NETMAP_RING_POOL] = {
		.size = 5*PAGE_SIZE,
		.num  = 4,
	},
	[NETMAP_BUF_POOL] = {
		.size = 2048,
		.num  = 4098,
	},
};


/*
 * nm_mem is the memory allocator used for all physical interfaces
 * running in netmap mode.
 * Virtual (VALE) ports will have each its own allocator.
 */
extern struct netmap_mem_ops netmap_mem_global_ops; /* forward */
struct netmap_mem_d nm_mem = {	/* Our memory allocator. */
	.pools = {
		[NETMAP_IF_POOL] = {
			.name 	= "netmap_if",
			.objminsize = sizeof(struct netmap_if),
			.objmaxsize = 4096,
			.nummin     = 10,	/* don't be stingy */
			.nummax	    = 10000,	/* XXX very large */
		},
		[NETMAP_RING_POOL] = {
			.name 	= "netmap_ring",
			.objminsize = sizeof(struct netmap_ring),
			.objmaxsize = 32*PAGE_SIZE,
			.nummin     = 2,
			.nummax	    = 1024,
		},
		[NETMAP_BUF_POOL] = {
			.name	= "netmap_buf",
			.objminsize = 64,
			.objmaxsize = 65536,
			.nummin     = 4,
			.nummax	    = 1000000, /* one million! */
		},
	},

	.params = {
		[NETMAP_IF_POOL] = {
			.size = 1024,
			.num  = 100,
		},
		[NETMAP_RING_POOL] = {
			.size = 9*PAGE_SIZE,
			.num  = 200,
		},
		[NETMAP_BUF_POOL] = {
			.size = 2048,
			.num  = NETMAP_BUF_MAX_NUM,
		},
	},

	.nm_id = 1,
	.nm_grp = -1,

	.prev = &nm_mem,
	.next = &nm_mem,

	.ops = &netmap_mem_global_ops,

	.name = "1"
};


/* blueprint for the private memory allocators */
/* XXX clang is not happy about using name as a print format */
static const struct netmap_mem_d nm_blueprint = {
	.pools = {
		[NETMAP_IF_POOL] = {
			.name 	= "%s_if",
			.objminsize = sizeof(struct netmap_if),
			.objmaxsize = 4096,
			.nummin     = 1,
			.nummax	    = 100,
		},
		[NETMAP_RING_POOL] = {
			.name 	= "%s_ring",
			.objminsize = sizeof(struct netmap_ring),
			.objmaxsize = 32*PAGE_SIZE,
			.nummin     = 2,
			.nummax	    = 1024,
		},
		[NETMAP_BUF_POOL] = {
			.name	= "%s_buf",
			.objminsize = 64,
			.objmaxsize = 65536,
			.nummin     = 4,
			.nummax	    = 1000000, /* one million! */
		},
	},

	.nm_grp = -1,

	.flags = NETMAP_MEM_PRIVATE,

	.ops = &netmap_mem_global_ops,
};

/* memory allocator related sysctls */

#define STRINGIFY(x) #x


#define DECLARE_SYSCTLS(id, name) \
	SYSBEGIN(mem2_ ## name); \
	SYSCTL_INT(_dev_netmap, OID_AUTO, name##_size, \
	    CTLFLAG_RW, &nm_mem.params[id].size, 0, "Requested size of netmap " STRINGIFY(name) "s"); \
	SYSCTL_INT(_dev_netmap, OID_AUTO, name##_curr_size, \
	    CTLFLAG_RD, &nm_mem.pools[id]._objsize, 0, "Current size of netmap " STRINGIFY(name) "s"); \
	SYSCTL_INT(_dev_netmap, OID_AUTO, name##_num, \
	    CTLFLAG_RW, &nm_mem.params[id].num, 0, "Requested number of netmap " STRINGIFY(name) "s"); \
	SYSCTL_INT(_dev_netmap, OID_AUTO, name##_curr_num, \
	    CTLFLAG_RD, &nm_mem.pools[id].objtotal, 0, "Current number of netmap " STRINGIFY(name) "s"); \
	SYSCTL_INT(_dev_netmap, OID_AUTO, priv_##name##_size, \
	    CTLFLAG_RW, &netmap_min_priv_params[id].size, 0, \
	    "Default size of private netmap " STRINGIFY(name) "s"); \
	SYSCTL_INT(_dev_netmap, OID_AUTO, priv_##name##_num, \
	    CTLFLAG_RW, &netmap_min_priv_params[id].num, 0, \
	    "Default number of private netmap " STRINGIFY(name) "s");	\
	SYSEND

SYSCTL_DECL(_dev_netmap);
DECLARE_SYSCTLS(NETMAP_IF_POOL, if);
DECLARE_SYSCTLS(NETMAP_RING_POOL, ring);
DECLARE_SYSCTLS(NETMAP_BUF_POOL, buf);

/* call with nm_mem_list_lock held */
static int
nm_mem_assign_id_locked(struct netmap_mem_d *nmd)
{
	nm_memid_t id;
	struct netmap_mem_d *scan = netmap_last_mem_d;
	int error = ENOMEM;

	do {
		/* we rely on unsigned wrap around */
		id = scan->nm_id + 1;
		if (id == 0) /* reserve 0 as error value */
			id = 1;
		scan = scan->next;
		if (id != scan->nm_id) {
			nmd->nm_id = id;
			nmd->prev = scan->prev;
			nmd->next = scan;
			scan->prev->next = nmd;
			scan->prev = nmd;
			netmap_last_mem_d = nmd;
			nmd->refcount = 1;
			NM_DBG_REFC(nmd, __FUNCTION__, __LINE__);
			error = 0;
			break;
		}
	} while (scan != netmap_last_mem_d);

	return error;
}

/* call with nm_mem_list_lock *not* held */
static int
nm_mem_assign_id(struct netmap_mem_d *nmd)
{
	int ret;

	NM_MTX_LOCK(nm_mem_list_lock);
	ret = nm_mem_assign_id_locked(nmd);
	NM_MTX_UNLOCK(nm_mem_list_lock);

	return ret;
}

/* call with nm_mem_list_lock held */
static void
nm_mem_release_id(struct netmap_mem_d *nmd)
{
	nmd->prev->next = nmd->next;
	nmd->next->prev = nmd->prev;

	if (netmap_last_mem_d == nmd)
		netmap_last_mem_d = nmd->prev;

	nmd->prev = nmd->next = NULL;
}

struct netmap_mem_d *
netmap_mem_find(nm_memid_t id)
{
	struct netmap_mem_d *nmd;

	NM_MTX_LOCK(nm_mem_list_lock);
	nmd = netmap_last_mem_d;
	do {
		if (!(nmd->flags & NETMAP_MEM_HIDDEN) && nmd->nm_id == id) {
			nmd->refcount++;
			NM_DBG_REFC(nmd, __FUNCTION__, __LINE__);
			NM_MTX_UNLOCK(nm_mem_list_lock);
			return nmd;
		}
		nmd = nmd->next;
	} while (nmd != netmap_last_mem_d);
	NM_MTX_UNLOCK(nm_mem_list_lock);
	return NULL;
}

static int
nm_mem_assign_group(struct netmap_mem_d *nmd, struct device *dev)
{
	int err = 0, id;
	id = nm_iommu_group_id(dev);
	if (netmap_debug & NM_DEBUG_MEM)
		nm_prinf("iommu_group %d", id);

	NMA_LOCK(nmd);

	if (nmd->nm_grp < 0)
		nmd->nm_grp = id;

	if (nmd->nm_grp != id) {
		if (netmap_verbose)
			nm_prerr("iommu group mismatch: %u vs %u",
					nmd->nm_grp, id);
		nmd->lasterr = err = ENOMEM;
	}

	NMA_UNLOCK(nmd);
	return err;
}

static struct lut_entry *
nm_alloc_lut(u_int nobj)
{
	size_t n = sizeof(struct lut_entry) * nobj;
	struct lut_entry *lut;
#ifdef linux
	lut = vmalloc(n);
#else
	lut = nm_os_malloc(n);
#endif
	return lut;
}

static void
nm_free_lut(struct lut_entry *lut, u_int objtotal)
{
	bzero(lut, sizeof(struct lut_entry) * objtotal);
#ifdef linux
	vfree(lut);
#else
	nm_os_free(lut);
#endif
}

#if defined(linux) || defined(_WIN32)
static struct plut_entry *
nm_alloc_plut(u_int nobj)
{
	size_t n = sizeof(struct plut_entry) * nobj;
	struct plut_entry *lut;
	lut = vmalloc(n);
	return lut;
}

static void
nm_free_plut(struct plut_entry * lut)
{
	vfree(lut);
}
#endif /* linux or _WIN32 */


/*
 * First, find the allocator that contains the requested offset,
 * then locate the cluster through a lookup table.
 */
static vm_paddr_t
netmap_mem2_ofstophys(struct netmap_mem_d* nmd, vm_ooffset_t offset)
{
	int i;
	vm_ooffset_t o = offset;
	vm_paddr_t pa;
	struct netmap_obj_pool *p;

	p = nmd->pools;

	for (i = 0; i < NETMAP_POOLS_NR; offset -= p[i].memtotal, i++) {
		if (offset >= p[i].memtotal)
			continue;
		// now lookup the cluster's address
#ifndef _WIN32
		pa = vtophys(p[i].lut[offset / p[i]._objsize].vaddr) +
			offset % p[i]._objsize;
#else
		pa = vtophys(p[i].lut[offset / p[i]._objsize].vaddr);
		pa.QuadPart += offset % p[i]._objsize;
#endif
		return pa;
	}
	/* this is only in case of errors */
	nm_prerr("invalid ofs 0x%x out of 0x%x 0x%x 0x%x", (u_int)o,
		p[NETMAP_IF_POOL].memtotal,
		p[NETMAP_IF_POOL].memtotal
			+ p[NETMAP_RING_POOL].memtotal,
		p[NETMAP_IF_POOL].memtotal
			+ p[NETMAP_RING_POOL].memtotal
			+ p[NETMAP_BUF_POOL].memtotal);
#ifndef _WIN32
	return 0; /* bad address */
#else
	vm_paddr_t res;
	res.QuadPart = 0;
	return res;
#endif
}

#ifdef _WIN32

/*
 * win32_build_virtual_memory_for_userspace
 *
 * This function get all the object making part of the pools and maps
 * a contiguous virtual memory space for the userspace
 * It works this way
 * 1 - allocate a Memory Descriptor List wide as the sum
 *		of the memory needed for the pools
 * 2 - cycle all the objects in every pool and for every object do
 *
 *		2a - cycle all the objects in every pool, get the list
 *				of the physical address descriptors
 *		2b - calculate the offset in the array of pages desciptor in the
 *				main MDL
 *		2c - copy the descriptors of the object in the main MDL
 *
 * 3 - return the resulting MDL that needs to be mapped in userland
 *
 * In this way we will have an MDL that describes all the memory for the
 * objects in a single object
*/

PMDL
win32_build_user_vm_map(struct netmap_mem_d* nmd)
{
	u_int memflags, ofs = 0;
	PMDL mainMdl, tempMdl;
	uint64_t memsize;
	int i, j;

	if (netmap_mem_get_info(nmd, &memsize, &memflags, NULL)) {
		nm_prerr("memory not finalised yet");
		return NULL;
	}

	mainMdl = IoAllocateMdl(NULL, memsize, FALSE, FALSE, NULL);
	if (mainMdl == NULL) {
		nm_prerr("failed to allocate mdl");
		return NULL;
	}

	NMA_LOCK(nmd);
	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		struct netmap_obj_pool *p = &nmd->pools[i];
		int clsz = p->_clustsize;
		int clobjs = p->_clustentries; /* objects per cluster */
		int mdl_len = sizeof(PFN_NUMBER) * BYTES_TO_PAGES(clsz);
		PPFN_NUMBER pSrc, pDst;

		/* each pool has a different cluster size so we need to reallocate */
		tempMdl = IoAllocateMdl(p->lut[0].vaddr, clsz, FALSE, FALSE, NULL);
		if (tempMdl == NULL) {
			NMA_UNLOCK(nmd);
			nm_prerr("fail to allocate tempMdl");
			IoFreeMdl(mainMdl);
			return NULL;
		}
		pSrc = MmGetMdlPfnArray(tempMdl);
		/* create one entry per cluster, the lut[] has one entry per object */
		for (j = 0; j < p->numclusters; j++, ofs += clsz) {
			pDst = &MmGetMdlPfnArray(mainMdl)[BYTES_TO_PAGES(ofs)];
			MmInitializeMdl(tempMdl, p->lut[j*clobjs].vaddr, clsz);
			MmBuildMdlForNonPagedPool(tempMdl); /* compute physical page addresses */
			RtlCopyMemory(pDst, pSrc, mdl_len); /* copy the page descriptors */
			mainMdl->MdlFlags = tempMdl->MdlFlags; /* XXX what is in here ? */
		}
		IoFreeMdl(tempMdl);
	}
	NMA_UNLOCK(nmd);
	return mainMdl;
}

#endif /* _WIN32 */

/*
 * helper function for OS-specific mmap routines (currently only windows).
 * Given an nmd and a pool index, returns the cluster size and number of clusters.
 * Returns 0 if memory is finalised and the pool is valid, otherwise 1.
 * It should be called under NMA_LOCK(nmd) otherwise the underlying info can change.
 */

int
netmap_mem2_get_pool_info(struct netmap_mem_d* nmd, u_int pool, u_int *clustsize, u_int *numclusters)
{
	if (!nmd || !clustsize || !numclusters || pool >= NETMAP_POOLS_NR)
		return 1; /* invalid arguments */
	// NMA_LOCK_ASSERT(nmd);
	if (!(nmd->flags & NETMAP_MEM_FINALIZED)) {
		*clustsize = *numclusters = 0;
		return 1; /* not ready yet */
	}
	*clustsize = nmd->pools[pool]._clustsize;
	*numclusters = nmd->pools[pool].numclusters;
	return 0; /* success */
}

static int
netmap_mem2_get_info(struct netmap_mem_d* nmd, uint64_t* size,
			u_int *memflags, nm_memid_t *id)
{
	int error = 0;
	error = netmap_mem_config(nmd);
	if (error)
		goto out;
	if (size) {
		if (nmd->flags & NETMAP_MEM_FINALIZED) {
			*size = nmd->nm_totalsize;
		} else {
			int i;
			*size = 0;
			for (i = 0; i < NETMAP_POOLS_NR; i++) {
				struct netmap_obj_pool *p = nmd->pools + i;
				*size += (p->_numclusters * p->_clustsize);
			}
		}
	}
	if (memflags)
		*memflags = nmd->flags;
	if (id)
		*id = nmd->nm_id;
out:
	return error;
}

/*
 * we store objects by kernel address, need to find the offset
 * within the pool to export the value to userspace.
 * Algorithm: scan until we find the cluster, then add the
 * actual offset in the cluster
 */
static ssize_t
netmap_obj_offset(struct netmap_obj_pool *p, const void *vaddr)
{
	int i, k = p->_clustentries, n = p->objtotal;
	ssize_t ofs = 0;

	for (i = 0; i < n; i += k, ofs += p->_clustsize) {
		const char *base = p->lut[i].vaddr;
		ssize_t relofs = (const char *) vaddr - base;

		if (relofs < 0 || relofs >= p->_clustsize)
			continue;

		ofs = ofs + relofs;
		nm_prdis("%s: return offset %d (cluster %d) for pointer %p",
		    p->name, ofs, i, vaddr);
		return ofs;
	}
	nm_prerr("address %p is not contained inside any cluster (%s)",
	    vaddr, p->name);
	return 0; /* An error occurred */
}

/* Helper functions which convert virtual addresses to offsets */
#define netmap_if_offset(n, v)					\
	netmap_obj_offset(&(n)->pools[NETMAP_IF_POOL], (v))

#define netmap_ring_offset(n, v)				\
    ((n)->pools[NETMAP_IF_POOL].memtotal + 			\
	netmap_obj_offset(&(n)->pools[NETMAP_RING_POOL], (v)))

static ssize_t
netmap_mem2_if_offset(struct netmap_mem_d *nmd, const void *addr)
{
	return netmap_if_offset(nmd, addr);
}

/*
 * report the index, and use start position as a hint,
 * otherwise buffer allocation becomes terribly expensive.
 */
static void *
netmap_obj_malloc(struct netmap_obj_pool *p, u_int len, uint32_t *start, uint32_t *index)
{
	uint32_t i = 0;			/* index in the bitmap */
	uint32_t mask, j = 0;		/* slot counter */
	void *vaddr = NULL;

	if (len > p->_objsize) {
		nm_prerr("%s request size %d too large", p->name, len);
		return NULL;
	}

	if (p->objfree == 0) {
		nm_prerr("no more %s objects", p->name);
		return NULL;
	}
	if (start)
		i = *start;

	/* termination is guaranteed by p->free, but better check bounds on i */
	while (vaddr == NULL && i < p->bitmap_slots)  {
		uint32_t cur = p->bitmap[i];
		if (cur == 0) { /* bitmask is fully used */
			i++;
			continue;
		}
		/* locate a slot */
		for (j = 0, mask = 1; (cur & mask) == 0; j++, mask <<= 1)
			;

		p->bitmap[i] &= ~mask; /* mark object as in use */
		p->objfree--;

		vaddr = p->lut[i * 32 + j].vaddr;
		if (index)
			*index = i * 32 + j;
	}
	nm_prdis("%s allocator: allocated object @ [%d][%d]: vaddr %p",p->name, i, j, vaddr);

	if (start)
		*start = i;
	return vaddr;
}


/*
 * free by index, not by address.
 * XXX should we also cleanup the content ?
 */
static int
netmap_obj_free(struct netmap_obj_pool *p, uint32_t j)
{
	uint32_t *ptr, mask;

	if (j >= p->objtotal) {
		nm_prerr("invalid index %u, max %u", j, p->objtotal);
		return 1;
	}
	ptr = &p->bitmap[j / 32];
	mask = (1 << (j % 32));
	if (*ptr & mask) {
		nm_prerr("ouch, double free on buffer %d", j);
		return 1;
	} else {
		*ptr |= mask;
		p->objfree++;
		return 0;
	}
}

/*
 * free by address. This is slow but is only used for a few
 * objects (rings, nifp)
 */
static void
netmap_obj_free_va(struct netmap_obj_pool *p, void *vaddr)
{
	u_int i, j, n = p->numclusters;

	for (i = 0, j = 0; i < n; i++, j += p->_clustentries) {
		void *base = p->lut[i * p->_clustentries].vaddr;
		ssize_t relofs = (ssize_t) vaddr - (ssize_t) base;

		/* Given address, is out of the scope of the current cluster.*/
		if (base == NULL || vaddr < base || relofs >= p->_clustsize)
			continue;

		j = j + relofs / p->_objsize;
		/* KASSERT(j != 0, ("Cannot free object 0")); */
		netmap_obj_free(p, j);
		return;
	}
	nm_prerr("address %p is not contained inside any cluster (%s)",
	    vaddr, p->name);
}

unsigned
netmap_mem_bufsize(struct netmap_mem_d *nmd)
{
	return nmd->pools[NETMAP_BUF_POOL]._objsize;
}

#define netmap_if_malloc(n, len)	netmap_obj_malloc(&(n)->pools[NETMAP_IF_POOL], len, NULL, NULL)
#define netmap_if_free(n, v)		netmap_obj_free_va(&(n)->pools[NETMAP_IF_POOL], (v))
#define netmap_ring_malloc(n, len)	netmap_obj_malloc(&(n)->pools[NETMAP_RING_POOL], len, NULL, NULL)
#define netmap_ring_free(n, v)		netmap_obj_free_va(&(n)->pools[NETMAP_RING_POOL], (v))
#define netmap_buf_malloc(n, _pos, _index)			\
	netmap_obj_malloc(&(n)->pools[NETMAP_BUF_POOL], netmap_mem_bufsize(n), _pos, _index)


#if 0 /* currently unused */
/* Return the index associated to the given packet buffer */
#define netmap_buf_index(n, v)						\
    (netmap_obj_offset(&(n)->pools[NETMAP_BUF_POOL], (v)) / NETMAP_BDG_BUF_SIZE(n))
#endif

/*
 * allocate extra buffers in a linked list.
 * returns the actual number.
 */
uint32_t
netmap_extra_alloc(struct netmap_adapter *na, uint32_t *head, uint32_t n)
{
	struct netmap_mem_d *nmd = na->nm_mem;
	uint32_t i, pos = 0; /* opaque, scan position in the bitmap */

	NMA_LOCK(nmd);

	*head = 0;	/* default, 'null' index ie empty list */
	for (i = 0 ; i < n; i++) {
		uint32_t cur = *head;	/* save current head */
		uint32_t *p = netmap_buf_malloc(nmd, &pos, head);
		if (p == NULL) {
			nm_prerr("no more buffers after %d of %d", i, n);
			*head = cur; /* restore */
			break;
		}
		nm_prdis(5, "allocate buffer %d -> %d", *head, cur);
		*p = cur; /* link to previous head */
	}

	NMA_UNLOCK(nmd);

	return i;
}

static void
netmap_extra_free(struct netmap_adapter *na, uint32_t head)
{
	struct lut_entry *lut = na->na_lut.lut;
	struct netmap_mem_d *nmd = na->nm_mem;
	struct netmap_obj_pool *p = &nmd->pools[NETMAP_BUF_POOL];
	uint32_t i, cur, *buf;

	nm_prdis("freeing the extra list");
	for (i = 0; head >=2 && head < p->objtotal; i++) {
		cur = head;
		buf = lut[head].vaddr;
		head = *buf;
		*buf = 0;
		if (netmap_obj_free(p, cur))
			break;
	}
	if (head != 0)
		nm_prerr("breaking with head %d", head);
	if (netmap_debug & NM_DEBUG_MEM)
		nm_prinf("freed %d buffers", i);
}


/* Return nonzero on error */
static int
netmap_new_bufs(struct netmap_mem_d *nmd, struct netmap_slot *slot, u_int n)
{
	struct netmap_obj_pool *p = &nmd->pools[NETMAP_BUF_POOL];
	u_int i = 0;	/* slot counter */
	uint32_t pos = 0;	/* slot in p->bitmap */
	uint32_t index = 0;	/* buffer index */

	for (i = 0; i < n; i++) {
		void *vaddr = netmap_buf_malloc(nmd, &pos, &index);
		if (vaddr == NULL) {
			nm_prerr("no more buffers after %d of %d", i, n);
			goto cleanup;
		}
		slot[i].buf_idx = index;
		slot[i].len = p->_objsize;
		slot[i].flags = 0;
		slot[i].ptr = 0;
	}

	nm_prdis("%s: allocated %d buffers, %d available, first at %d", p->name, n, p->objfree, pos);
	return (0);

cleanup:
	while (i > 0) {
		i--;
		netmap_obj_free(p, slot[i].buf_idx);
	}
	bzero(slot, n * sizeof(slot[0]));
	return (ENOMEM);
}

static void
netmap_mem_set_ring(struct netmap_mem_d *nmd, struct netmap_slot *slot, u_int n, uint32_t index)
{
	struct netmap_obj_pool *p = &nmd->pools[NETMAP_BUF_POOL];
	u_int i;

	for (i = 0; i < n; i++) {
		slot[i].buf_idx = index;
		slot[i].len = p->_objsize;
		slot[i].flags = 0;
	}
}


static void
netmap_free_buf(struct netmap_mem_d *nmd, uint32_t i)
{
	struct netmap_obj_pool *p = &nmd->pools[NETMAP_BUF_POOL];

	if (i < 2 || i >= p->objtotal) {
		nm_prerr("Cannot free buf#%d: should be in [2, %d[", i, p->objtotal);
		return;
	}
	netmap_obj_free(p, i);
}


static void
netmap_free_bufs(struct netmap_mem_d *nmd, struct netmap_slot *slot, u_int n)
{
	u_int i;

	for (i = 0; i < n; i++) {
		if (slot[i].buf_idx > 1)
			netmap_free_buf(nmd, slot[i].buf_idx);
	}
	nm_prdis("%s: released some buffers, available: %u",
			p->name, p->objfree);
}

static void
netmap_reset_obj_allocator(struct netmap_obj_pool *p)
{

	if (p == NULL)
		return;
	if (p->bitmap)
		nm_os_free(p->bitmap);
	p->bitmap = NULL;
	if (p->invalid_bitmap)
		nm_os_free(p->invalid_bitmap);
	p->invalid_bitmap = NULL;
	if (!p->alloc_done) {
		/* allocation was done by somebody else.
		 * Let them clean up after themselves.
		 */
		return;
	}
	if (p->lut) {
		u_int i;

		/*
		 * Free each cluster allocated in
		 * netmap_finalize_obj_allocator().  The cluster start
		 * addresses are stored at multiples of p->_clusterentries
		 * in the lut.
		 */
		for (i = 0; i < p->objtotal; i += p->_clustentries) {
			contigfree(p->lut[i].vaddr, p->_clustsize, M_NETMAP);
		}
		nm_free_lut(p->lut, p->objtotal);
	}
	p->lut = NULL;
	p->objtotal = 0;
	p->memtotal = 0;
	p->numclusters = 0;
	p->objfree = 0;
	p->alloc_done = 0;
}

/*
 * Free all resources related to an allocator.
 */
static void
netmap_destroy_obj_allocator(struct netmap_obj_pool *p)
{
	if (p == NULL)
		return;
	netmap_reset_obj_allocator(p);
}

/*
 * We receive a request for objtotal objects, of size objsize each.
 * Internally we may round up both numbers, as we allocate objects
 * in small clusters multiple of the page size.
 * We need to keep track of objtotal and clustentries,
 * as they are needed when freeing memory.
 *
 * XXX note -- userspace needs the buffers to be contiguous,
 *	so we cannot afford gaps at the end of a cluster.
 */


/* call with NMA_LOCK held */
static int
netmap_config_obj_allocator(struct netmap_obj_pool *p, u_int objtotal, u_int objsize)
{
	int i;
	u_int clustsize;	/* the cluster size, multiple of page size */
	u_int clustentries;	/* how many objects per entry */

	/* we store the current request, so we can
	 * detect configuration changes later */
	p->r_objtotal = objtotal;
	p->r_objsize = objsize;

#define MAX_CLUSTSIZE	(1<<22)		// 4 MB
#define LINE_ROUND	NM_CACHE_ALIGN	// 64
	if (objsize >= MAX_CLUSTSIZE) {
		/* we could do it but there is no point */
		nm_prerr("unsupported allocation for %d bytes", objsize);
		return EINVAL;
	}
	/* make sure objsize is a multiple of LINE_ROUND */
	i = (objsize & (LINE_ROUND - 1));
	if (i) {
		nm_prinf("aligning object by %d bytes", LINE_ROUND - i);
		objsize += LINE_ROUND - i;
	}
	if (objsize < p->objminsize || objsize > p->objmaxsize) {
		nm_prerr("requested objsize %d out of range [%d, %d]",
			objsize, p->objminsize, p->objmaxsize);
		return EINVAL;
	}
	if (objtotal < p->nummin || objtotal > p->nummax) {
		nm_prerr("requested objtotal %d out of range [%d, %d]",
			objtotal, p->nummin, p->nummax);
		return EINVAL;
	}
	/*
	 * Compute number of objects using a brute-force approach:
	 * given a max cluster size,
	 * we try to fill it with objects keeping track of the
	 * wasted space to the next page boundary.
	 */
	for (clustentries = 0, i = 1;; i++) {
		u_int delta, used = i * objsize;
		if (used > MAX_CLUSTSIZE)
			break;
		delta = used % PAGE_SIZE;
		if (delta == 0) { // exact solution
			clustentries = i;
			break;
		}
	}
	/* exact solution not found */
	if (clustentries == 0) {
		nm_prerr("unsupported allocation for %d bytes", objsize);
		return EINVAL;
	}
	/* compute clustsize */
	clustsize = clustentries * objsize;
	if (netmap_debug & NM_DEBUG_MEM)
		nm_prinf("objsize %d clustsize %d objects %d",
			objsize, clustsize, clustentries);

	/*
	 * The number of clusters is n = ceil(objtotal/clustentries)
	 * objtotal' = n * clustentries
	 */
	p->_clustentries = clustentries;
	p->_clustsize = clustsize;
	p->_numclusters = (objtotal + clustentries - 1) / clustentries;

	/* actual values (may be larger than requested) */
	p->_objsize = objsize;
	p->_objtotal = p->_numclusters * clustentries;

	return 0;
}

/* call with NMA_LOCK held */
static int
netmap_finalize_obj_allocator(struct netmap_obj_pool *p)
{
	int i; /* must be signed */
	size_t n;

	if (p->lut) {
		/* if the lut is already there we assume that also all the
		 * clusters have already been allocated, possibily by somebody
		 * else (e.g., extmem). In the latter case, the alloc_done flag
		 * will remain at zero, so that we will not attempt to
		 * deallocate the clusters by ourselves in
		 * netmap_reset_obj_allocator.
		 */
		return 0;
	}

	/* optimistically assume we have enough memory */
	p->numclusters = p->_numclusters;
	p->objtotal = p->_objtotal;
	p->alloc_done = 1;

	p->lut = nm_alloc_lut(p->objtotal);
	if (p->lut == NULL) {
		nm_prerr("Unable to create lookup table for '%s'", p->name);
		goto clean;
	}

	/*
	 * Allocate clusters, init pointers
	 */

	n = p->_clustsize;
	for (i = 0; i < (int)p->objtotal;) {
		int lim = i + p->_clustentries;
		char *clust;

		/*
		 * XXX Note, we only need contigmalloc() for buffers attached
		 * to native interfaces. In all other cases (nifp, netmap rings
		 * and even buffers for VALE ports or emulated interfaces) we
		 * can live with standard malloc, because the hardware will not
		 * access the pages directly.
		 */
		clust = contigmalloc(n, M_NETMAP, M_NOWAIT | M_ZERO,
		    (size_t)0, -1UL, PAGE_SIZE, 0);
		if (clust == NULL) {
			/*
			 * If we get here, there is a severe memory shortage,
			 * so halve the allocated memory to reclaim some.
			 */
			nm_prerr("Unable to create cluster at %d for '%s' allocator",
			    i, p->name);
			if (i < 2) /* nothing to halve */
				goto out;
			lim = i / 2;
			for (i--; i >= lim; i--) {
				if (i % p->_clustentries == 0 && p->lut[i].vaddr)
					contigfree(p->lut[i].vaddr,
						n, M_NETMAP);
				p->lut[i].vaddr = NULL;
			}
		out:
			p->objtotal = i;
			/* we may have stopped in the middle of a cluster */
			p->numclusters = (i + p->_clustentries - 1) / p->_clustentries;
			break;
		}
		/*
		 * Set lut state for all buffers in the current cluster.
		 *
		 * [i, lim) is the set of buffer indexes that cover the
		 * current cluster.
		 *
		 * 'clust' is really the address of the current buffer in
		 * the current cluster as we index through it with a stride
		 * of p->_objsize.
		 */
		for (; i < lim; i++, clust += p->_objsize) {
			p->lut[i].vaddr = clust;
#if !defined(linux) && !defined(_WIN32)
			p->lut[i].paddr = vtophys(clust);
#endif
		}
	}
	p->memtotal = p->numclusters * p->_clustsize;
	if (netmap_verbose)
		nm_prinf("Pre-allocated %d clusters (%d/%dKB) for '%s'",
		    p->numclusters, p->_clustsize >> 10,
		    p->memtotal >> 10, p->name);

	return 0;

clean:
	netmap_reset_obj_allocator(p);
	return ENOMEM;
}

/* call with lock held */
static int
netmap_mem_params_changed(struct netmap_obj_params* p)
{
	int i, rv = 0;

	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		if (p[i].last_size != p[i].size || p[i].last_num != p[i].num) {
			p[i].last_size = p[i].size;
			p[i].last_num = p[i].num;
			rv = 1;
		}
	}
	return rv;
}

static void
netmap_mem_reset_all(struct netmap_mem_d *nmd)
{
	int i;

	if (netmap_debug & NM_DEBUG_MEM)
		nm_prinf("resetting %p", nmd);
	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		netmap_reset_obj_allocator(&nmd->pools[i]);
	}
	nmd->flags  &= ~NETMAP_MEM_FINALIZED;
}

static int
netmap_mem_unmap(struct netmap_obj_pool *p, struct netmap_adapter *na)
{
	int i, lim = p->objtotal;
	struct netmap_lut *lut = &na->na_lut;

	if (na == NULL || na->pdev == NULL)
		return 0;

#if defined(__FreeBSD__)
	/* On FreeBSD mapping and unmapping is performed by the txsync
	 * and rxsync routine, packet by packet. */
	(void)i;
	(void)lim;
	(void)lut;
#elif defined(_WIN32)
	(void)i;
	(void)lim;
	(void)lut;
	nm_prerr("unsupported on Windows");
#else /* linux */
	nm_prdis("unmapping and freeing plut for %s", na->name);
	if (lut->plut == NULL)
		return 0;
	for (i = 0; i < lim; i += p->_clustentries) {
		if (lut->plut[i].paddr)
			netmap_unload_map(na, (bus_dma_tag_t) na->pdev, &lut->plut[i].paddr, p->_clustsize);
	}
	nm_free_plut(lut->plut);
	lut->plut = NULL;
#endif /* linux */

	return 0;
}

static int
netmap_mem_map(struct netmap_obj_pool *p, struct netmap_adapter *na)
{
	int error = 0;
	int i, lim = p->objtotal;
	struct netmap_lut *lut = &na->na_lut;

	if (na->pdev == NULL)
		return 0;

#if defined(__FreeBSD__)
	/* On FreeBSD mapping and unmapping is performed by the txsync
	 * and rxsync routine, packet by packet. */
	(void)i;
	(void)lim;
	(void)lut;
#elif defined(_WIN32)
	(void)i;
	(void)lim;
	(void)lut;
	nm_prerr("unsupported on Windows");
#else /* linux */

	if (lut->plut != NULL) {
		nm_prdis("plut already allocated for %s", na->name);
		return 0;
	}

	nm_prdis("allocating physical lut for %s", na->name);
	lut->plut = nm_alloc_plut(lim);
	if (lut->plut == NULL) {
		nm_prerr("Failed to allocate physical lut for %s", na->name);
		return ENOMEM;
	}

	for (i = 0; i < lim; i += p->_clustentries) {
		lut->plut[i].paddr = 0;
	}

	for (i = 0; i < lim; i += p->_clustentries) {
		int j;

		if (p->lut[i].vaddr == NULL)
			continue;

		error = netmap_load_map(na, (bus_dma_tag_t) na->pdev, &lut->plut[i].paddr,
				p->lut[i].vaddr, p->_clustsize);
		if (error) {
			nm_prerr("Failed to map cluster #%d from the %s pool", i, p->name);
			break;
		}

		for (j = 1; j < p->_clustentries; j++) {
			lut->plut[i + j].paddr = lut->plut[i + j - 1].paddr + p->_objsize;
		}
	}

	if (error)
		netmap_mem_unmap(p, na);

#endif /* linux */

	return error;
}

static int
netmap_mem_finalize_all(struct netmap_mem_d *nmd)
{
	int i;
	if (nmd->flags & NETMAP_MEM_FINALIZED)
		return 0;
	nmd->lasterr = 0;
	nmd->nm_totalsize = 0;
	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		nmd->lasterr = netmap_finalize_obj_allocator(&nmd->pools[i]);
		if (nmd->lasterr)
			goto error;
		nmd->nm_totalsize += nmd->pools[i].memtotal;
	}
	nmd->lasterr = netmap_mem_init_bitmaps(nmd);
	if (nmd->lasterr)
		goto error;

	nmd->flags |= NETMAP_MEM_FINALIZED;

	if (netmap_verbose)
		nm_prinf("interfaces %d KB, rings %d KB, buffers %d MB",
		    nmd->pools[NETMAP_IF_POOL].memtotal >> 10,
		    nmd->pools[NETMAP_RING_POOL].memtotal >> 10,
		    nmd->pools[NETMAP_BUF_POOL].memtotal >> 20);

	if (netmap_verbose)
		nm_prinf("Free buffers: %d", nmd->pools[NETMAP_BUF_POOL].objfree);


	return 0;
error:
	netmap_mem_reset_all(nmd);
	return nmd->lasterr;
}

/*
 * allocator for private memory
 */
static void *
_netmap_mem_private_new(size_t size, struct netmap_obj_params *p,
		struct netmap_mem_ops *ops, int *perr)
{
	struct netmap_mem_d *d = NULL;
	int i, err = 0;

	d = nm_os_malloc(size);
	if (d == NULL) {
		err = ENOMEM;
		goto error;
	}

	*d = nm_blueprint;
	d->ops = ops;

	err = nm_mem_assign_id(d);
	if (err)
		goto error_free;
	snprintf(d->name, NM_MEM_NAMESZ, "%d", d->nm_id);

	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		snprintf(d->pools[i].name, NETMAP_POOL_MAX_NAMSZ,
				nm_blueprint.pools[i].name,
				d->name);
		d->params[i].num = p[i].num;
		d->params[i].size = p[i].size;
	}

	NMA_LOCK_INIT(d);

	err = netmap_mem_config(d);
	if (err)
		goto error_rel_id;

	d->flags &= ~NETMAP_MEM_FINALIZED;

	return d;

error_rel_id:
	NMA_LOCK_DESTROY(d);
	nm_mem_release_id(d);
error_free:
	nm_os_free(d);
error:
	if (perr)
		*perr = err;
	return NULL;
}

struct netmap_mem_d *
netmap_mem_private_new(u_int txr, u_int txd, u_int rxr, u_int rxd,
		u_int extra_bufs, u_int npipes, int *perr)
{
	struct netmap_mem_d *d = NULL;
	struct netmap_obj_params p[NETMAP_POOLS_NR];
	int i;
	u_int v, maxd;
	/* account for the fake host rings */
	txr++;
	rxr++;

	/* copy the min values */
	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		p[i] = netmap_min_priv_params[i];
	}

	/* possibly increase them to fit user request */
	v = sizeof(struct netmap_if) + sizeof(ssize_t) * (txr + rxr);
	if (p[NETMAP_IF_POOL].size < v)
		p[NETMAP_IF_POOL].size = v;
	v = 2 + 4 * npipes;
	if (p[NETMAP_IF_POOL].num < v)
		p[NETMAP_IF_POOL].num = v;
	maxd = (txd > rxd) ? txd : rxd;
	v = sizeof(struct netmap_ring) + sizeof(struct netmap_slot) * maxd;
	if (p[NETMAP_RING_POOL].size < v)
		p[NETMAP_RING_POOL].size = v;
	/* each pipe endpoint needs two tx rings (1 normal + 1 host, fake)
	 * and two rx rings (again, 1 normal and 1 fake host)
	 */
	v = txr + rxr + 8 * npipes;
	if (p[NETMAP_RING_POOL].num < v)
		p[NETMAP_RING_POOL].num = v;
	/* for each pipe we only need the buffers for the 4 "real" rings.
	 * On the other end, the pipe ring dimension may be different from
	 * the parent port ring dimension. As a compromise, we allocate twice the
	 * space actually needed if the pipe rings were the same size as the parent rings
	 */
	v = (4 * npipes + rxr) * rxd + (4 * npipes + txr) * txd + 2 + extra_bufs;
		/* the +2 is for the tx and rx fake buffers (indices 0 and 1) */
	if (p[NETMAP_BUF_POOL].num < v)
		p[NETMAP_BUF_POOL].num = v;

	if (netmap_verbose)
		nm_prinf("req if %d*%d ring %d*%d buf %d*%d",
			p[NETMAP_IF_POOL].num,
			p[NETMAP_IF_POOL].size,
			p[NETMAP_RING_POOL].num,
			p[NETMAP_RING_POOL].size,
			p[NETMAP_BUF_POOL].num,
			p[NETMAP_BUF_POOL].size);

	d = _netmap_mem_private_new(sizeof(*d), p, &netmap_mem_global_ops, perr);

	return d;
}


/* call with lock held */
static int
netmap_mem2_config(struct netmap_mem_d *nmd)
{
	int i;

	if (!netmap_mem_params_changed(nmd->params))
		goto out;

	nm_prdis("reconfiguring");

	if (nmd->flags & NETMAP_MEM_FINALIZED) {
		/* reset previous allocation */
		for (i = 0; i < NETMAP_POOLS_NR; i++) {
			netmap_reset_obj_allocator(&nmd->pools[i]);
		}
		nmd->flags &= ~NETMAP_MEM_FINALIZED;
	}

	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		nmd->lasterr = netmap_config_obj_allocator(&nmd->pools[i],
				nmd->params[i].num, nmd->params[i].size);
		if (nmd->lasterr)
			goto out;
	}

out:

	return nmd->lasterr;
}

static int
netmap_mem2_finalize(struct netmap_mem_d *nmd)
{
	if (nmd->flags & NETMAP_MEM_FINALIZED)
		goto out;

	if (netmap_mem_finalize_all(nmd))
		goto out;

	nmd->lasterr = 0;

out:
	return nmd->lasterr;
}

static void
netmap_mem2_delete(struct netmap_mem_d *nmd)
{
	int i;

	for (i = 0; i < NETMAP_POOLS_NR; i++) {
	    netmap_destroy_obj_allocator(&nmd->pools[i]);
	}

	NMA_LOCK_DESTROY(nmd);
	if (nmd != &nm_mem)
		nm_os_free(nmd);
}

#ifdef WITH_EXTMEM
/* doubly linekd list of all existing external allocators */
static struct netmap_mem_ext *netmap_mem_ext_list = NULL;
NM_MTX_T nm_mem_ext_list_lock;
#endif /* WITH_EXTMEM */

int
netmap_mem_init(void)
{
	NM_MTX_INIT(nm_mem_list_lock);
	NMA_LOCK_INIT(&nm_mem);
	netmap_mem_get(&nm_mem);
#ifdef WITH_EXTMEM
	NM_MTX_INIT(nm_mem_ext_list_lock);
#endif /* WITH_EXTMEM */
	return (0);
}

void
netmap_mem_fini(void)
{
	netmap_mem_put(&nm_mem);
}

static void
netmap_free_rings(struct netmap_adapter *na)
{
	enum txrx t;

	for_rx_tx(t) {
		u_int i;
		for (i = 0; i < netmap_all_rings(na, t); i++) {
			struct netmap_kring *kring = NMR(na, t)[i];
			struct netmap_ring *ring = kring->ring;

			if (ring == NULL || kring->users > 0 || (kring->nr_kflags & NKR_NEEDRING)) {
				if (netmap_debug & NM_DEBUG_MEM)
					nm_prinf("NOT deleting ring %s (ring %p, users %d neekring %d)",
						kring->name, ring, kring->users, kring->nr_kflags & NKR_NEEDRING);
				continue;
			}
			if (netmap_debug & NM_DEBUG_MEM)
				nm_prinf("deleting ring %s", kring->name);
			if (!(kring->nr_kflags & NKR_FAKERING)) {
				nm_prdis("freeing bufs for %s", kring->name);
				netmap_free_bufs(na->nm_mem, ring->slot, kring->nkr_num_slots);
			} else {
				nm_prdis("NOT freeing bufs for %s", kring->name);
			}
			netmap_ring_free(na->nm_mem, ring);
			kring->ring = NULL;
		}
	}
}

/* call with NMA_LOCK held *
 *
 * Allocate netmap rings and buffers for this card
 * The rings are contiguous, but have variable size.
 * The kring array must follow the layout described
 * in netmap_krings_create().
 */
static int
netmap_mem2_rings_create(struct netmap_adapter *na)
{
	enum txrx t;

	for_rx_tx(t) {
		u_int i;

		for (i = 0; i < netmap_all_rings(na, t); i++) {
			struct netmap_kring *kring = NMR(na, t)[i];
			struct netmap_ring *ring = kring->ring;
			u_int len, ndesc;

			if (ring || (!kring->users && !(kring->nr_kflags & NKR_NEEDRING))) {
				/* uneeded, or already created by somebody else */
				if (netmap_debug & NM_DEBUG_MEM)
					nm_prinf("NOT creating ring %s (ring %p, users %d neekring %d)",
						kring->name, ring, kring->users, kring->nr_kflags & NKR_NEEDRING);
				continue;
			}
			if (netmap_debug & NM_DEBUG_MEM)
				nm_prinf("creating %s", kring->name);
			ndesc = kring->nkr_num_slots;
			len = sizeof(struct netmap_ring) +
				  ndesc * sizeof(struct netmap_slot);
			ring = netmap_ring_malloc(na->nm_mem, len);
			if (ring == NULL) {
				nm_prerr("Cannot allocate %s_ring", nm_txrx2str(t));
				goto cleanup;
			}
			nm_prdis("txring at %p", ring);
			kring->ring = ring;
			*(uint32_t *)(uintptr_t)&ring->num_slots = ndesc;
			*(int64_t *)(uintptr_t)&ring->buf_ofs =
			    (na->nm_mem->pools[NETMAP_IF_POOL].memtotal +
				na->nm_mem->pools[NETMAP_RING_POOL].memtotal) -
				netmap_ring_offset(na->nm_mem, ring);

			/* copy values from kring */
			ring->head = kring->rhead;
			ring->cur = kring->rcur;
			ring->tail = kring->rtail;
			*(uint32_t *)(uintptr_t)&ring->nr_buf_size =
				netmap_mem_bufsize(na->nm_mem);
			nm_prdis("%s h %d c %d t %d", kring->name,
				ring->head, ring->cur, ring->tail);
			nm_prdis("initializing slots for %s_ring", nm_txrx2str(t));
			if (!(kring->nr_kflags & NKR_FAKERING)) {
				/* this is a real ring */
				if (netmap_debug & NM_DEBUG_MEM)
					nm_prinf("allocating buffers for %s", kring->name);
				if (netmap_new_bufs(na->nm_mem, ring->slot, ndesc)) {
					nm_prerr("Cannot allocate buffers for %s_ring", nm_txrx2str(t));
					goto cleanup;
				}
			} else {
				/* this is a fake ring, set all indices to 0 */
				if (netmap_debug & NM_DEBUG_MEM)
					nm_prinf("NOT allocating buffers for %s", kring->name);
				netmap_mem_set_ring(na->nm_mem, ring->slot, ndesc, 0);
			}
		        /* ring info */
		        *(uint16_t *)(uintptr_t)&ring->ringid = kring->ring_id;
		        *(uint16_t *)(uintptr_t)&ring->dir = kring->tx;
		}
	}

	return 0;

cleanup:
	/* we cannot actually cleanup here, since we don't own kring->users
	 * and kring->nr_klags & NKR_NEEDRING. The caller must decrement
	 * the first or zero-out the second, then call netmap_free_rings()
	 * to do the cleanup
	 */

	return ENOMEM;
}

static void
netmap_mem2_rings_delete(struct netmap_adapter *na)
{
	/* last instance, release bufs and rings */
	netmap_free_rings(na);
}


/* call with NMA_LOCK held */
/*
 * Allocate the per-fd structure netmap_if.
 *
 * We assume that the configuration stored in na
 * (number of tx/rx rings and descs) does not change while
 * the interface is in netmap mode.
 */
static struct netmap_if *
netmap_mem2_if_new(struct netmap_adapter *na, struct netmap_priv_d *priv)
{
	struct netmap_if *nifp;
	ssize_t base; /* handy for relative offsets between rings and nifp */
	u_int i, len, n[NR_TXRX], ntot;
	enum txrx t;

	ntot = 0;
	for_rx_tx(t) {
		/* account for the (eventually fake) host rings */
		n[t] = netmap_all_rings(na, t);
		ntot += n[t];
	}
	/*
	 * the descriptor is followed inline by an array of offsets
	 * to the tx and rx rings in the shared memory region.
	 */

	len = sizeof(struct netmap_if) + (ntot * sizeof(ssize_t));
	nifp = netmap_if_malloc(na->nm_mem, len);
	if (nifp == NULL) {
		NMA_UNLOCK(na->nm_mem);
		return NULL;
	}

	/* initialize base fields -- override const */
	*(u_int *)(uintptr_t)&nifp->ni_tx_rings = na->num_tx_rings;
	*(u_int *)(uintptr_t)&nifp->ni_rx_rings = na->num_rx_rings;
	*(u_int *)(uintptr_t)&nifp->ni_host_tx_rings =
		(na->num_host_tx_rings ? na->num_host_tx_rings : 1);
	*(u_int *)(uintptr_t)&nifp->ni_host_rx_rings =
		(na->num_host_rx_rings ? na->num_host_rx_rings : 1);
	strlcpy(nifp->ni_name, na->name, sizeof(nifp->ni_name));

	/*
	 * fill the slots for the rx and tx rings. They contain the offset
	 * between the ring and nifp, so the information is usable in
	 * userspace to reach the ring from the nifp.
	 */
	base = netmap_if_offset(na->nm_mem, nifp);
	for (i = 0; i < n[NR_TX]; i++) {
		/* XXX instead of ofs == 0 maybe use the offset of an error
		 * ring, like we do for buffers? */
		ssize_t ofs = 0;

		if (na->tx_rings[i]->ring != NULL && i >= priv->np_qfirst[NR_TX]
				&& i < priv->np_qlast[NR_TX]) {
			ofs = netmap_ring_offset(na->nm_mem,
						 na->tx_rings[i]->ring) - base;
		}
		*(ssize_t *)(uintptr_t)&nifp->ring_ofs[i] = ofs;
	}
	for (i = 0; i < n[NR_RX]; i++) {
		/* XXX instead of ofs == 0 maybe use the offset of an error
		 * ring, like we do for buffers? */
		ssize_t ofs = 0;

		if (na->rx_rings[i]->ring != NULL && i >= priv->np_qfirst[NR_RX]
				&& i < priv->np_qlast[NR_RX]) {
			ofs = netmap_ring_offset(na->nm_mem,
						 na->rx_rings[i]->ring) - base;
		}
		*(ssize_t *)(uintptr_t)&nifp->ring_ofs[i+n[NR_TX]] = ofs;
	}

	return (nifp);
}

static void
netmap_mem2_if_delete(struct netmap_adapter *na, struct netmap_if *nifp)
{
	if (nifp == NULL)
		/* nothing to do */
		return;
	if (nifp->ni_bufs_head)
		netmap_extra_free(na, nifp->ni_bufs_head);
	netmap_if_free(na->nm_mem, nifp);
}

static void
netmap_mem2_deref(struct netmap_mem_d *nmd)
{

	if (netmap_debug & NM_DEBUG_MEM)
		nm_prinf("active = %d", nmd->active);

}

struct netmap_mem_ops netmap_mem_global_ops = {
	.nmd_get_lut = netmap_mem2_get_lut,
	.nmd_get_info = netmap_mem2_get_info,
	.nmd_ofstophys = netmap_mem2_ofstophys,
	.nmd_config = netmap_mem2_config,
	.nmd_finalize = netmap_mem2_finalize,
	.nmd_deref = netmap_mem2_deref,
	.nmd_delete = netmap_mem2_delete,
	.nmd_if_offset = netmap_mem2_if_offset,
	.nmd_if_new = netmap_mem2_if_new,
	.nmd_if_delete = netmap_mem2_if_delete,
	.nmd_rings_create = netmap_mem2_rings_create,
	.nmd_rings_delete = netmap_mem2_rings_delete
};

int
netmap_mem_pools_info_get(struct nmreq_pools_info *req,
				struct netmap_mem_d *nmd)
{
	int ret;

	ret = netmap_mem_get_info(nmd, &req->nr_memsize, NULL,
					&req->nr_mem_id);
	if (ret) {
		return ret;
	}

	NMA_LOCK(nmd);
	req->nr_if_pool_offset = 0;
	req->nr_if_pool_objtotal = nmd->pools[NETMAP_IF_POOL].objtotal;
	req->nr_if_pool_objsize = nmd->pools[NETMAP_IF_POOL]._objsize;

	req->nr_ring_pool_offset = nmd->pools[NETMAP_IF_POOL].memtotal;
	req->nr_ring_pool_objtotal = nmd->pools[NETMAP_RING_POOL].objtotal;
	req->nr_ring_pool_objsize = nmd->pools[NETMAP_RING_POOL]._objsize;

	req->nr_buf_pool_offset = nmd->pools[NETMAP_IF_POOL].memtotal +
			     nmd->pools[NETMAP_RING_POOL].memtotal;
	req->nr_buf_pool_objtotal = nmd->pools[NETMAP_BUF_POOL].objtotal;
	req->nr_buf_pool_objsize = nmd->pools[NETMAP_BUF_POOL]._objsize;
	NMA_UNLOCK(nmd);

	return 0;
}

#ifdef WITH_EXTMEM
struct netmap_mem_ext {
	struct netmap_mem_d up;

	struct nm_os_extmem *os;
	struct netmap_mem_ext *next, *prev;
};

/* call with nm_mem_list_lock held */
static void
netmap_mem_ext_register(struct netmap_mem_ext *e)
{
	NM_MTX_LOCK(nm_mem_ext_list_lock);
	if (netmap_mem_ext_list)
		netmap_mem_ext_list->prev = e;
	e->next = netmap_mem_ext_list;
	netmap_mem_ext_list = e;
	e->prev = NULL;
	NM_MTX_UNLOCK(nm_mem_ext_list_lock);
}

/* call with nm_mem_list_lock held */
static void
netmap_mem_ext_unregister(struct netmap_mem_ext *e)
{
	if (e->prev)
		e->prev->next = e->next;
	else
		netmap_mem_ext_list = e->next;
	if (e->next)
		e->next->prev = e->prev;
	e->prev = e->next = NULL;
}

static struct netmap_mem_ext *
netmap_mem_ext_search(struct nm_os_extmem *os)
{
	struct netmap_mem_ext *e;

	NM_MTX_LOCK(nm_mem_ext_list_lock);
	for (e = netmap_mem_ext_list; e; e = e->next) {
		if (nm_os_extmem_isequal(e->os, os)) {
			netmap_mem_get(&e->up);
			break;
		}
	}
	NM_MTX_UNLOCK(nm_mem_ext_list_lock);
	return e;
}


static void
netmap_mem_ext_delete(struct netmap_mem_d *d)
{
	int i;
	struct netmap_mem_ext *e =
		(struct netmap_mem_ext *)d;

	netmap_mem_ext_unregister(e);

	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		struct netmap_obj_pool *p = &d->pools[i];

		if (p->lut) {
			nm_free_lut(p->lut, p->objtotal);
			p->lut = NULL;
		}
	}
	if (e->os)
		nm_os_extmem_delete(e->os);
	netmap_mem2_delete(d);
}

static int
netmap_mem_ext_config(struct netmap_mem_d *nmd)
{
	return 0;
}

struct netmap_mem_ops netmap_mem_ext_ops = {
	.nmd_get_lut = netmap_mem2_get_lut,
	.nmd_get_info = netmap_mem2_get_info,
	.nmd_ofstophys = netmap_mem2_ofstophys,
	.nmd_config = netmap_mem_ext_config,
	.nmd_finalize = netmap_mem2_finalize,
	.nmd_deref = netmap_mem2_deref,
	.nmd_delete = netmap_mem_ext_delete,
	.nmd_if_offset = netmap_mem2_if_offset,
	.nmd_if_new = netmap_mem2_if_new,
	.nmd_if_delete = netmap_mem2_if_delete,
	.nmd_rings_create = netmap_mem2_rings_create,
	.nmd_rings_delete = netmap_mem2_rings_delete
};

struct netmap_mem_d *
netmap_mem_ext_create(uint64_t usrptr, struct nmreq_pools_info *pi, int *perror)
{
	int error = 0;
	int i, j;
	struct netmap_mem_ext *nme;
	char *clust;
	size_t off;
	struct nm_os_extmem *os = NULL;
	int nr_pages;

	// XXX sanity checks
	if (pi->nr_if_pool_objtotal == 0)
		pi->nr_if_pool_objtotal = netmap_min_priv_params[NETMAP_IF_POOL].num;
	if (pi->nr_if_pool_objsize == 0)
		pi->nr_if_pool_objsize = netmap_min_priv_params[NETMAP_IF_POOL].size;
	if (pi->nr_ring_pool_objtotal == 0)
		pi->nr_ring_pool_objtotal = netmap_min_priv_params[NETMAP_RING_POOL].num;
	if (pi->nr_ring_pool_objsize == 0)
		pi->nr_ring_pool_objsize = netmap_min_priv_params[NETMAP_RING_POOL].size;
	if (pi->nr_buf_pool_objtotal == 0)
		pi->nr_buf_pool_objtotal = netmap_min_priv_params[NETMAP_BUF_POOL].num;
	if (pi->nr_buf_pool_objsize == 0)
		pi->nr_buf_pool_objsize = netmap_min_priv_params[NETMAP_BUF_POOL].size;
	if (netmap_verbose & NM_DEBUG_MEM)
		nm_prinf("if %d %d ring %d %d buf %d %d",
			pi->nr_if_pool_objtotal, pi->nr_if_pool_objsize,
			pi->nr_ring_pool_objtotal, pi->nr_ring_pool_objsize,
			pi->nr_buf_pool_objtotal, pi->nr_buf_pool_objsize);

	os = nm_os_extmem_create(usrptr, pi, &error);
	if (os == NULL) {
		nm_prerr("os extmem creation failed");
		goto out;
	}

	nme = netmap_mem_ext_search(os);
	if (nme) {
		nm_os_extmem_delete(os);
		return &nme->up;
	}
	if (netmap_verbose & NM_DEBUG_MEM)
		nm_prinf("not found, creating new");

	nme = _netmap_mem_private_new(sizeof(*nme),
			(struct netmap_obj_params[]){
				{ pi->nr_if_pool_objsize, pi->nr_if_pool_objtotal },
				{ pi->nr_ring_pool_objsize, pi->nr_ring_pool_objtotal },
				{ pi->nr_buf_pool_objsize, pi->nr_buf_pool_objtotal }},
			&netmap_mem_ext_ops,
			&error);
	if (nme == NULL)
		goto out_unmap;

	nr_pages = nm_os_extmem_nr_pages(os);

	/* from now on pages will be released by nme destructor;
	 * we let res = 0 to prevent release in out_unmap below
	 */
	nme->os = os;
	os = NULL; /* pass ownership */

	clust = nm_os_extmem_nextpage(nme->os);
	off = 0;
	for (i = 0; i < NETMAP_POOLS_NR; i++) {
		struct netmap_obj_pool *p = &nme->up.pools[i];
		struct netmap_obj_params *o = &nme->up.params[i];

		p->_objsize = o->size;
		p->_clustsize = o->size;
		p->_clustentries = 1;

		p->lut = nm_alloc_lut(o->num);
		if (p->lut == NULL) {
			error = ENOMEM;
			goto out_delete;
		}

		p->bitmap_slots = (o->num + sizeof(uint32_t) - 1) / sizeof(uint32_t);
		p->invalid_bitmap = nm_os_malloc(sizeof(uint32_t) * p->bitmap_slots);
		if (p->invalid_bitmap == NULL) {
			error = ENOMEM;
			goto out_delete;
		}

		if (nr_pages == 0) {
			p->objtotal = 0;
			p->memtotal = 0;
			p->objfree = 0;
			continue;
		}

		for (j = 0; j < o->num && nr_pages > 0; j++) {
			size_t noff;

			p->lut[j].vaddr = clust + off;
#if !defined(linux) && !defined(_WIN32)
			p->lut[j].paddr = vtophys(p->lut[j].vaddr);
#endif
			nm_prdis("%s %d at %p", p->name, j, p->lut[j].vaddr);
			noff = off + p->_objsize;
			if (noff < PAGE_SIZE) {
				off = noff;
				continue;
			}
			nm_prdis("too big, recomputing offset...");
			while (noff >= PAGE_SIZE) {
				char *old_clust = clust;
				noff -= PAGE_SIZE;
				clust = nm_os_extmem_nextpage(nme->os);
				nr_pages--;
				nm_prdis("noff %zu page %p nr_pages %d", noff,
						page_to_virt(*pages), nr_pages);
				if (noff > 0 && !nm_isset(p->invalid_bitmap, j) &&
					(nr_pages == 0 ||
					 old_clust + PAGE_SIZE != clust))
				{
					/* out of space or non contiguous,
					 * drop this object
					 * */
					p->invalid_bitmap[ (j>>5) ] |= 1U << (j & 31U);
					nm_prdis("non contiguous at off %zu, drop", noff);
				}
				if (nr_pages == 0)
					break;
			}
			off = noff;
		}
		p->objtotal = j;
		p->numclusters = p->objtotal;
		p->memtotal = j * p->_objsize;
		nm_prdis("%d memtotal %u", j, p->memtotal);
	}

	netmap_mem_ext_register(nme);

	return &nme->up;

out_delete:
	netmap_mem_put(&nme->up);
out_unmap:
	if (os)
		nm_os_extmem_delete(os);
out:
	if (perror)
		*perror = error;
	return NULL;

}
#endif /* WITH_EXTMEM */


#ifdef WITH_PTNETMAP
struct mem_pt_if {
	struct mem_pt_if *next;
	struct ifnet *ifp;
	unsigned int nifp_offset;
};

/* Netmap allocator for ptnetmap guests. */
struct netmap_mem_ptg {
	struct netmap_mem_d up;

	vm_paddr_t nm_paddr;            /* physical address in the guest */
	void *nm_addr;                  /* virtual address in the guest */
	struct netmap_lut buf_lut;      /* lookup table for BUF pool in the guest */
	nm_memid_t host_mem_id;         /* allocator identifier in the host */
	struct ptnetmap_memdev *ptn_dev;/* ptnetmap memdev */
	struct mem_pt_if *pt_ifs;	/* list of interfaces in passthrough */
};

/* Link a passthrough interface to a passthrough netmap allocator. */
static int
netmap_mem_pt_guest_ifp_add(struct netmap_mem_d *nmd, struct ifnet *ifp,
			    unsigned int nifp_offset)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;
	struct mem_pt_if *ptif = nm_os_malloc(sizeof(*ptif));

	if (!ptif) {
		return ENOMEM;
	}

	NMA_LOCK(nmd);

	ptif->ifp = ifp;
	ptif->nifp_offset = nifp_offset;

	if (ptnmd->pt_ifs) {
		ptif->next = ptnmd->pt_ifs;
	}
	ptnmd->pt_ifs = ptif;

	NMA_UNLOCK(nmd);

	nm_prinf("ifp=%s,nifp_offset=%u",
		ptif->ifp->if_xname, ptif->nifp_offset);

	return 0;
}

/* Called with NMA_LOCK(nmd) held. */
static struct mem_pt_if *
netmap_mem_pt_guest_ifp_lookup(struct netmap_mem_d *nmd, struct ifnet *ifp)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;
	struct mem_pt_if *curr;

	for (curr = ptnmd->pt_ifs; curr; curr = curr->next) {
		if (curr->ifp == ifp) {
			return curr;
		}
	}

	return NULL;
}

/* Unlink a passthrough interface from a passthrough netmap allocator. */
int
netmap_mem_pt_guest_ifp_del(struct netmap_mem_d *nmd, struct ifnet *ifp)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;
	struct mem_pt_if *prev = NULL;
	struct mem_pt_if *curr;
	int ret = -1;

	NMA_LOCK(nmd);

	for (curr = ptnmd->pt_ifs; curr; curr = curr->next) {
		if (curr->ifp == ifp) {
			if (prev) {
				prev->next = curr->next;
			} else {
				ptnmd->pt_ifs = curr->next;
			}
			nm_prinf("removed (ifp=%p,nifp_offset=%u)",
			  curr->ifp, curr->nifp_offset);
			nm_os_free(curr);
			ret = 0;
			break;
		}
		prev = curr;
	}

	NMA_UNLOCK(nmd);

	return ret;
}

static int
netmap_mem_pt_guest_get_lut(struct netmap_mem_d *nmd, struct netmap_lut *lut)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;

	if (!(nmd->flags & NETMAP_MEM_FINALIZED)) {
		return EINVAL;
	}

	*lut = ptnmd->buf_lut;
	return 0;
}

static int
netmap_mem_pt_guest_get_info(struct netmap_mem_d *nmd, uint64_t *size,
			     u_int *memflags, uint16_t *id)
{
	int error = 0;

	error = nmd->ops->nmd_config(nmd);
	if (error)
		goto out;

	if (size)
		*size = nmd->nm_totalsize;
	if (memflags)
		*memflags = nmd->flags;
	if (id)
		*id = nmd->nm_id;

out:

	return error;
}

static vm_paddr_t
netmap_mem_pt_guest_ofstophys(struct netmap_mem_d *nmd, vm_ooffset_t off)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;
	vm_paddr_t paddr;
	/* if the offset is valid, just return csb->base_addr + off */
	paddr = (vm_paddr_t)(ptnmd->nm_paddr + off);
	nm_prdis("off %lx padr %lx", off, (unsigned long)paddr);
	return paddr;
}

static int
netmap_mem_pt_guest_config(struct netmap_mem_d *nmd)
{
	/* nothing to do, we are configured on creation
	 * and configuration never changes thereafter
	 */
	return 0;
}

static int
netmap_mem_pt_guest_finalize(struct netmap_mem_d *nmd)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;
	uint64_t mem_size;
	uint32_t bufsize;
	uint32_t nbuffers;
	uint32_t poolofs;
	vm_paddr_t paddr;
	char *vaddr;
	int i;
	int error = 0;

	if (nmd->flags & NETMAP_MEM_FINALIZED)
		goto out;

	if (ptnmd->ptn_dev == NULL) {
		nm_prerr("ptnetmap memdev not attached");
		error = ENOMEM;
		goto out;
	}
	/* Map memory through ptnetmap-memdev BAR. */
	error = nm_os_pt_memdev_iomap(ptnmd->ptn_dev, &ptnmd->nm_paddr,
				      &ptnmd->nm_addr, &mem_size);
	if (error)
		goto out;

	/* Initialize the lut using the information contained in the
	 * ptnetmap memory device. */
	bufsize = nm_os_pt_memdev_ioread(ptnmd->ptn_dev,
					 PTNET_MDEV_IO_BUF_POOL_OBJSZ);
	nbuffers = nm_os_pt_memdev_ioread(ptnmd->ptn_dev,
					 PTNET_MDEV_IO_BUF_POOL_OBJNUM);

	/* allocate the lut */
	if (ptnmd->buf_lut.lut == NULL) {
		nm_prinf("allocating lut");
		ptnmd->buf_lut.lut = nm_alloc_lut(nbuffers);
		if (ptnmd->buf_lut.lut == NULL) {
			nm_prerr("lut allocation failed");
			return ENOMEM;
		}
	}

	/* we have physically contiguous memory mapped through PCI BAR */
	poolofs = nm_os_pt_memdev_ioread(ptnmd->ptn_dev,
					 PTNET_MDEV_IO_BUF_POOL_OFS);
	vaddr = (char *)(ptnmd->nm_addr) + poolofs;
	paddr = ptnmd->nm_paddr + poolofs;

	for (i = 0; i < nbuffers; i++) {
		ptnmd->buf_lut.lut[i].vaddr = vaddr;
		vaddr += bufsize;
		paddr += bufsize;
	}

	ptnmd->buf_lut.objtotal = nbuffers;
	ptnmd->buf_lut.objsize = bufsize;
	nmd->nm_totalsize = (unsigned int)mem_size;

	/* Initialize these fields as are needed by
	 * netmap_mem_bufsize().
	 * XXX please improve this, why do we need this
	 * replication? maybe we nmd->pools[] should no be
	 * there for the guest allocator? */
	nmd->pools[NETMAP_BUF_POOL]._objsize = bufsize;
	nmd->pools[NETMAP_BUF_POOL]._objtotal = nbuffers;

	nmd->flags |= NETMAP_MEM_FINALIZED;
out:
	return error;
}

static void
netmap_mem_pt_guest_deref(struct netmap_mem_d *nmd)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;

	if (nmd->active == 1 &&
		(nmd->flags & NETMAP_MEM_FINALIZED)) {
	    nmd->flags  &= ~NETMAP_MEM_FINALIZED;
	    /* unmap ptnetmap-memdev memory */
	    if (ptnmd->ptn_dev) {
		nm_os_pt_memdev_iounmap(ptnmd->ptn_dev);
	    }
	    ptnmd->nm_addr = NULL;
	    ptnmd->nm_paddr = 0;
	}
}

static ssize_t
netmap_mem_pt_guest_if_offset(struct netmap_mem_d *nmd, const void *vaddr)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)nmd;

	return (const char *)(vaddr) - (char *)(ptnmd->nm_addr);
}

static void
netmap_mem_pt_guest_delete(struct netmap_mem_d *nmd)
{
	if (nmd == NULL)
		return;
	if (netmap_verbose)
		nm_prinf("deleting %p", nmd);
	if (nmd->active > 0)
		nm_prerr("bug: deleting mem allocator with active=%d!", nmd->active);
	if (netmap_verbose)
		nm_prinf("done deleting %p", nmd);
	NMA_LOCK_DESTROY(nmd);
	nm_os_free(nmd);
}

static struct netmap_if *
netmap_mem_pt_guest_if_new(struct netmap_adapter *na, struct netmap_priv_d *priv)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)na->nm_mem;
	struct mem_pt_if *ptif;
	struct netmap_if *nifp = NULL;

	ptif = netmap_mem_pt_guest_ifp_lookup(na->nm_mem, na->ifp);
	if (ptif == NULL) {
		nm_prerr("interface %s is not in passthrough", na->name);
		goto out;
	}

	nifp = (struct netmap_if *)((char *)(ptnmd->nm_addr) +
				    ptif->nifp_offset);
out:
	return nifp;
}

static void
netmap_mem_pt_guest_if_delete(struct netmap_adapter *na, struct netmap_if *nifp)
{
	struct mem_pt_if *ptif;

	ptif = netmap_mem_pt_guest_ifp_lookup(na->nm_mem, na->ifp);
	if (ptif == NULL) {
		nm_prerr("interface %s is not in passthrough", na->name);
	}
}

static int
netmap_mem_pt_guest_rings_create(struct netmap_adapter *na)
{
	struct netmap_mem_ptg *ptnmd = (struct netmap_mem_ptg *)na->nm_mem;
	struct mem_pt_if *ptif;
	struct netmap_if *nifp;
	int i, error = -1;

	ptif = netmap_mem_pt_guest_ifp_lookup(na->nm_mem, na->ifp);
	if (ptif == NULL) {
		nm_prerr("interface %s is not in passthrough", na->name);
		goto out;
	}


	/* point each kring to the corresponding backend ring */
	nifp = (struct netmap_if *)((char *)ptnmd->nm_addr + ptif->nifp_offset);
	for (i = 0; i < netmap_all_rings(na, NR_TX); i++) {
		struct netmap_kring *kring = na->tx_rings[i];
		if (kring->ring)
			continue;
		kring->ring = (struct netmap_ring *)
			((char *)nifp + nifp->ring_ofs[i]);
	}
	for (i = 0; i < netmap_all_rings(na, NR_RX); i++) {
		struct netmap_kring *kring = na->rx_rings[i];
		if (kring->ring)
			continue;
		kring->ring = (struct netmap_ring *)
			((char *)nifp +
			 nifp->ring_ofs[netmap_all_rings(na, NR_TX) + i]);
	}

	error = 0;
out:
	return error;
}

static void
netmap_mem_pt_guest_rings_delete(struct netmap_adapter *na)
{
#if 0
	enum txrx t;

	for_rx_tx(t) {
		u_int i;
		for (i = 0; i < nma_get_nrings(na, t) + 1; i++) {
			struct netmap_kring *kring = &NMR(na, t)[i];

			kring->ring = NULL;
		}
	}
#endif
}

static struct netmap_mem_ops netmap_mem_pt_guest_ops = {
	.nmd_get_lut = netmap_mem_pt_guest_get_lut,
	.nmd_get_info = netmap_mem_pt_guest_get_info,
	.nmd_ofstophys = netmap_mem_pt_guest_ofstophys,
	.nmd_config = netmap_mem_pt_guest_config,
	.nmd_finalize = netmap_mem_pt_guest_finalize,
	.nmd_deref = netmap_mem_pt_guest_deref,
	.nmd_if_offset = netmap_mem_pt_guest_if_offset,
	.nmd_delete = netmap_mem_pt_guest_delete,
	.nmd_if_new = netmap_mem_pt_guest_if_new,
	.nmd_if_delete = netmap_mem_pt_guest_if_delete,
	.nmd_rings_create = netmap_mem_pt_guest_rings_create,
	.nmd_rings_delete = netmap_mem_pt_guest_rings_delete
};

/* Called with nm_mem_list_lock held. */
static struct netmap_mem_d *
netmap_mem_pt_guest_find_memid(nm_memid_t mem_id)
{
	struct netmap_mem_d *mem = NULL;
	struct netmap_mem_d *scan = netmap_last_mem_d;

	do {
		/* find ptnetmap allocator through host ID */
		if (scan->ops->nmd_deref == netmap_mem_pt_guest_deref &&
			((struct netmap_mem_ptg *)(scan))->host_mem_id == mem_id) {
			mem = scan;
			mem->refcount++;
			NM_DBG_REFC(mem, __FUNCTION__, __LINE__);
			break;
		}
		scan = scan->next;
	} while (scan != netmap_last_mem_d);

	return mem;
}

/* Called with nm_mem_list_lock held. */
static struct netmap_mem_d *
netmap_mem_pt_guest_create(nm_memid_t mem_id)
{
	struct netmap_mem_ptg *ptnmd;
	int err = 0;

	ptnmd = nm_os_malloc(sizeof(struct netmap_mem_ptg));
	if (ptnmd == NULL) {
		err = ENOMEM;
		goto error;
	}

	ptnmd->up.ops = &netmap_mem_pt_guest_ops;
	ptnmd->host_mem_id = mem_id;
	ptnmd->pt_ifs = NULL;

	/* Assign new id in the guest (We have the lock) */
	err = nm_mem_assign_id_locked(&ptnmd->up);
	if (err)
		goto error;

	ptnmd->up.flags &= ~NETMAP_MEM_FINALIZED;
	ptnmd->up.flags |= NETMAP_MEM_IO;

	NMA_LOCK_INIT(&ptnmd->up);

	snprintf(ptnmd->up.name, NM_MEM_NAMESZ, "%d", ptnmd->up.nm_id);


	return &ptnmd->up;
error:
	netmap_mem_pt_guest_delete(&ptnmd->up);
	return NULL;
}

/*
 * find host id in guest allocators and create guest allocator
 * if it is not there
 */
static struct netmap_mem_d *
netmap_mem_pt_guest_get(nm_memid_t mem_id)
{
	struct netmap_mem_d *nmd;

	NM_MTX_LOCK(nm_mem_list_lock);
	nmd = netmap_mem_pt_guest_find_memid(mem_id);
	if (nmd == NULL) {
		nmd = netmap_mem_pt_guest_create(mem_id);
	}
	NM_MTX_UNLOCK(nm_mem_list_lock);

	return nmd;
}

/*
 * The guest allocator can be created by ptnetmap_memdev (during the device
 * attach) or by ptnetmap device (ptnet), during the netmap_attach.
 *
 * The order is not important (we have different order in LINUX and FreeBSD).
 * The first one, creates the device, and the second one simply attaches it.
 */

/* Called when ptnetmap_memdev is attaching, to attach a new allocator in
 * the guest */
struct netmap_mem_d *
netmap_mem_pt_guest_attach(struct ptnetmap_memdev *ptn_dev, nm_memid_t mem_id)
{
	struct netmap_mem_d *nmd;
	struct netmap_mem_ptg *ptnmd;

	nmd = netmap_mem_pt_guest_get(mem_id);

	/* assign this device to the guest allocator */
	if (nmd) {
		ptnmd = (struct netmap_mem_ptg *)nmd;
		ptnmd->ptn_dev = ptn_dev;
	}

	return nmd;
}

/* Called when ptnet device is attaching */
struct netmap_mem_d *
netmap_mem_pt_guest_new(struct ifnet *ifp,
			unsigned int nifp_offset,
			unsigned int memid)
{
	struct netmap_mem_d *nmd;

	if (ifp == NULL) {
		return NULL;
	}

	nmd = netmap_mem_pt_guest_get((nm_memid_t)memid);

	if (nmd) {
		netmap_mem_pt_guest_ifp_add(nmd, ifp, nifp_offset);
	}

	return nmd;
}

#endif /* WITH_PTNETMAP */
