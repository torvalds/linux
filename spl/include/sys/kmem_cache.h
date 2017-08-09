/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_KMEM_CACHE_H
#define	_SPL_KMEM_CACHE_H

#include <sys/taskq.h>

/*
 * Slab allocation interfaces.  The SPL slab differs from the standard
 * Linux SLAB or SLUB primarily in that each cache may be backed by slabs
 * allocated from the physical or virtal memory address space.  The virtual
 * slabs allow for good behavior when allocation large objects of identical
 * size.  This slab implementation also supports both constructors and
 * destructors which the Linux slab does not.
 */
enum {
	KMC_BIT_NOTOUCH		= 0,	/* Don't update ages */
	KMC_BIT_NODEBUG		= 1,	/* Default behavior */
	KMC_BIT_NOMAGAZINE	= 2,	/* XXX: Unsupported */
	KMC_BIT_NOHASH		= 3,	/* XXX: Unsupported */
	KMC_BIT_QCACHE		= 4,	/* XXX: Unsupported */
	KMC_BIT_KMEM		= 5,	/* Use kmem cache */
	KMC_BIT_VMEM		= 6,	/* Use vmem cache */
	KMC_BIT_SLAB		= 7,	/* Use Linux slab cache */
	KMC_BIT_OFFSLAB		= 8,	/* Objects not on slab */
	KMC_BIT_NOEMERGENCY	= 9,	/* Disable emergency objects */
	KMC_BIT_DEADLOCKED	= 14,	/* Deadlock detected */
	KMC_BIT_GROWING		= 15,	/* Growing in progress */
	KMC_BIT_REAPING		= 16,	/* Reaping in progress */
	KMC_BIT_DESTROY		= 17,	/* Destroy in progress */
	KMC_BIT_TOTAL		= 18,	/* Proc handler helper bit */
	KMC_BIT_ALLOC		= 19,	/* Proc handler helper bit */
	KMC_BIT_MAX		= 20,	/* Proc handler helper bit */
};

/* kmem move callback return values */
typedef enum kmem_cbrc {
	KMEM_CBRC_YES		= 0,	/* Object moved */
	KMEM_CBRC_NO		= 1,	/* Object not moved */
	KMEM_CBRC_LATER		= 2,	/* Object not moved, try again later */
	KMEM_CBRC_DONT_NEED	= 3,	/* Neither object is needed */
	KMEM_CBRC_DONT_KNOW	= 4,	/* Object unknown */
} kmem_cbrc_t;

#define	KMC_NOTOUCH		(1 << KMC_BIT_NOTOUCH)
#define	KMC_NODEBUG		(1 << KMC_BIT_NODEBUG)
#define	KMC_NOMAGAZINE		(1 << KMC_BIT_NOMAGAZINE)
#define	KMC_NOHASH		(1 << KMC_BIT_NOHASH)
#define	KMC_QCACHE		(1 << KMC_BIT_QCACHE)
#define	KMC_KMEM		(1 << KMC_BIT_KMEM)
#define	KMC_VMEM		(1 << KMC_BIT_VMEM)
#define	KMC_SLAB		(1 << KMC_BIT_SLAB)
#define	KMC_OFFSLAB		(1 << KMC_BIT_OFFSLAB)
#define	KMC_NOEMERGENCY		(1 << KMC_BIT_NOEMERGENCY)
#define	KMC_DEADLOCKED		(1 << KMC_BIT_DEADLOCKED)
#define	KMC_GROWING		(1 << KMC_BIT_GROWING)
#define	KMC_REAPING		(1 << KMC_BIT_REAPING)
#define	KMC_DESTROY		(1 << KMC_BIT_DESTROY)
#define	KMC_TOTAL		(1 << KMC_BIT_TOTAL)
#define	KMC_ALLOC		(1 << KMC_BIT_ALLOC)
#define	KMC_MAX			(1 << KMC_BIT_MAX)

#define	KMC_REAP_CHUNK		INT_MAX
#define	KMC_DEFAULT_SEEKS	1

#define	KMC_EXPIRE_AGE		0x1	/* Due to age */
#define	KMC_EXPIRE_MEM		0x2	/* Due to low memory */

#define	KMC_RECLAIM_ONCE	0x1	/* Force a single shrinker pass */

extern unsigned int spl_kmem_cache_expire;
extern struct list_head spl_kmem_cache_list;
extern struct rw_semaphore spl_kmem_cache_sem;

#define	SKM_MAGIC			0x2e2e2e2e
#define	SKO_MAGIC			0x20202020
#define	SKS_MAGIC			0x22222222
#define	SKC_MAGIC			0x2c2c2c2c

#define	SPL_KMEM_CACHE_DELAY		15	/* Minimum slab release age */
#define	SPL_KMEM_CACHE_REAP		0	/* Default reap everything */
#define	SPL_KMEM_CACHE_OBJ_PER_SLAB	8	/* Target objects per slab */
#define	SPL_KMEM_CACHE_OBJ_PER_SLAB_MIN	1	/* Minimum objects per slab */
#define	SPL_KMEM_CACHE_ALIGN		8	/* Default object alignment */
#ifdef _LP64
#define	SPL_KMEM_CACHE_MAX_SIZE		32	/* Max slab size in MB */
#else
#define	SPL_KMEM_CACHE_MAX_SIZE		4	/* Max slab size in MB */
#endif

#define	SPL_MAX_ORDER			(MAX_ORDER - 3)
#define	SPL_MAX_ORDER_NR_PAGES		(1 << (SPL_MAX_ORDER - 1))

#ifdef CONFIG_SLUB
#define	SPL_MAX_KMEM_CACHE_ORDER	PAGE_ALLOC_COSTLY_ORDER
#define	SPL_MAX_KMEM_ORDER_NR_PAGES	(1 << (SPL_MAX_KMEM_CACHE_ORDER - 1))
#else
#define	SPL_MAX_KMEM_ORDER_NR_PAGES	(KMALLOC_MAX_SIZE >> PAGE_SHIFT)
#endif

#define	POINTER_IS_VALID(p)		0	/* Unimplemented */
#define	POINTER_INVALIDATE(pp)			/* Unimplemented */

typedef int (*spl_kmem_ctor_t)(void *, void *, int);
typedef void (*spl_kmem_dtor_t)(void *, void *);
typedef void (*spl_kmem_reclaim_t)(void *);

typedef struct spl_kmem_magazine {
	uint32_t		skm_magic;	/* Sanity magic */
	uint32_t		skm_avail;	/* Available objects */
	uint32_t		skm_size;	/* Magazine size */
	uint32_t		skm_refill;	/* Batch refill size */
	struct spl_kmem_cache	*skm_cache;	/* Owned by cache */
	unsigned long		skm_age;	/* Last cache access */
	unsigned int		skm_cpu;	/* Owned by cpu */
	void			*skm_objs[0];	/* Object pointers */
} spl_kmem_magazine_t;

typedef struct spl_kmem_obj {
	uint32_t		sko_magic;	/* Sanity magic */
	void			*sko_addr;	/* Buffer address */
	struct spl_kmem_slab	*sko_slab;	/* Owned by slab */
	struct list_head	sko_list;	/* Free object list linkage */
} spl_kmem_obj_t;

typedef struct spl_kmem_slab {
	uint32_t		sks_magic;	/* Sanity magic */
	uint32_t		sks_objs;	/* Objects per slab */
	struct spl_kmem_cache	*sks_cache;	/* Owned by cache */
	struct list_head	sks_list;	/* Slab list linkage */
	struct list_head	sks_free_list;	/* Free object list */
	unsigned long		sks_age;	/* Last modify jiffie */
	uint32_t		sks_ref;	/* Ref count used objects */
} spl_kmem_slab_t;

typedef struct spl_kmem_alloc {
	struct spl_kmem_cache	*ska_cache;	/* Owned by cache */
	int			ska_flags;	/* Allocation flags */
	taskq_ent_t		ska_tqe;	/* Task queue entry */
} spl_kmem_alloc_t;

typedef struct spl_kmem_emergency {
	struct rb_node		ske_node;	/* Emergency tree linkage */
	unsigned long		ske_obj;	/* Buffer address */
} spl_kmem_emergency_t;

typedef struct spl_kmem_cache {
	uint32_t		skc_magic;	/* Sanity magic */
	uint32_t		skc_name_size;	/* Name length */
	char			*skc_name;	/* Name string */
	spl_kmem_magazine_t	**skc_mag;	/* Per-CPU warm cache */
	uint32_t		skc_mag_size;	/* Magazine size */
	uint32_t		skc_mag_refill;	/* Magazine refill count */
	spl_kmem_ctor_t		skc_ctor;	/* Constructor */
	spl_kmem_dtor_t		skc_dtor;	/* Destructor */
	spl_kmem_reclaim_t	skc_reclaim;	/* Reclaimator */
	void			*skc_private;	/* Private data */
	void			*skc_vmp;	/* Unused */
	struct kmem_cache	*skc_linux_cache; /* Linux slab cache if used */
	unsigned long		skc_flags;	/* Flags */
	uint32_t		skc_obj_size;	/* Object size */
	uint32_t		skc_obj_align;	/* Object alignment */
	uint32_t		skc_slab_objs;	/* Objects per slab */
	uint32_t		skc_slab_size;	/* Slab size */
	uint32_t		skc_delay;	/* Slab reclaim interval */
	uint32_t		skc_reap;	/* Slab reclaim count */
	atomic_t		skc_ref;	/* Ref count callers */
	taskqid_t		skc_taskqid;	/* Slab reclaim task */
	struct list_head	skc_list;	/* List of caches linkage */
	struct list_head	skc_complete_list; /* Completely alloc'ed */
	struct list_head	skc_partial_list;  /* Partially alloc'ed */
	struct rb_root		skc_emergency_tree; /* Min sized objects */
	spinlock_t		skc_lock;	/* Cache lock */
	spl_wait_queue_head_t	skc_waitq;	/* Allocation waiters */
	uint64_t		skc_slab_fail;	/* Slab alloc failures */
	uint64_t		skc_slab_create;  /* Slab creates */
	uint64_t		skc_slab_destroy; /* Slab destroys */
	uint64_t		skc_slab_total;	/* Slab total current */
	uint64_t		skc_slab_alloc;	/* Slab alloc current */
	uint64_t		skc_slab_max;	/* Slab max historic  */
	uint64_t		skc_obj_total;	/* Obj total current */
	uint64_t		skc_obj_alloc;	/* Obj alloc current */
	uint64_t		skc_obj_max;	/* Obj max historic */
	uint64_t		skc_obj_deadlock;  /* Obj emergency deadlocks */
	uint64_t		skc_obj_emergency; /* Obj emergency current */
	uint64_t		skc_obj_emergency_max; /* Obj emergency max */
} spl_kmem_cache_t;
#define	kmem_cache_t		spl_kmem_cache_t

extern spl_kmem_cache_t *spl_kmem_cache_create(char *name, size_t size,
    size_t align, spl_kmem_ctor_t ctor, spl_kmem_dtor_t dtor,
    spl_kmem_reclaim_t reclaim, void *priv, void *vmp, int flags);
extern void spl_kmem_cache_set_move(spl_kmem_cache_t *,
    kmem_cbrc_t (*)(void *, void *, size_t, void *));
extern void spl_kmem_cache_destroy(spl_kmem_cache_t *skc);
extern void *spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags);
extern void spl_kmem_cache_free(spl_kmem_cache_t *skc, void *obj);
extern void spl_kmem_cache_set_allocflags(spl_kmem_cache_t *skc, gfp_t flags);
extern void spl_kmem_cache_reap_now(spl_kmem_cache_t *skc, int count);
extern void spl_kmem_reap(void);

#define	kmem_cache_create(name, size, align, ctor, dtor, rclm, priv, vmp, fl) \
    spl_kmem_cache_create(name, size, align, ctor, dtor, rclm, priv, vmp, fl)
#define	kmem_cache_set_move(skc, move)	spl_kmem_cache_set_move(skc, move)
#define	kmem_cache_destroy(skc)		spl_kmem_cache_destroy(skc)
#define	kmem_cache_alloc(skc, flags)	spl_kmem_cache_alloc(skc, flags)
#define	kmem_cache_free(skc, obj)	spl_kmem_cache_free(skc, obj)
#define	kmem_cache_reap_now(skc)	\
    spl_kmem_cache_reap_now(skc, skc->skc_reap)
#define	kmem_reap()			spl_kmem_reap()

/*
 * The following functions are only available for internal use.
 */
extern int spl_kmem_cache_init(void);
extern void spl_kmem_cache_fini(void);

#endif	/* _SPL_KMEM_CACHE_H */
