/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_KMEM_H__
#define __XFS_SUPPORT_KMEM_H__

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>

/*
 * memory management routines
 */
#define KM_SLEEP	0x0001u
#define KM_NOSLEEP	0x0002u
#define KM_NOFS		0x0004u
#define KM_MAYFAIL	0x0008u

#define	kmem_zone	kmem_cache_s
#define kmem_zone_t	kmem_cache_t

typedef unsigned long xfs_pflags_t;

#define PFLAGS_TEST_NOIO()              (current->flags & PF_NOIO)
#define PFLAGS_TEST_FSTRANS()           (current->flags & PF_FSTRANS)

#define PFLAGS_SET_NOIO() do {		\
	current->flags |= PF_NOIO;	\
} while (0)

#define PFLAGS_CLEAR_NOIO() do {	\
	current->flags &= ~PF_NOIO;	\
} while (0)

/* these could be nested, so we save state */
#define PFLAGS_SET_FSTRANS(STATEP) do {	\
	*(STATEP) = current->flags;	\
	current->flags |= PF_FSTRANS;	\
} while (0)

#define PFLAGS_CLEAR_FSTRANS(STATEP) do { \
	*(STATEP) = current->flags;	\
	current->flags &= ~PF_FSTRANS;	\
} while (0)

/* Restore the PF_FSTRANS state to what was saved in STATEP */
#define PFLAGS_RESTORE_FSTRANS(STATEP) do {     		\
	current->flags = ((current->flags & ~PF_FSTRANS) |	\
			  (*(STATEP) & PF_FSTRANS));		\
} while (0)

#define PFLAGS_DUP(OSTATEP, NSTATEP) do { \
	*(NSTATEP) = *(OSTATEP);	\
} while (0)

static __inline gfp_t kmem_flags_convert(unsigned int __nocast flags)
{
	gfp_t lflags = __GFP_NOWARN;	/* we'll report problems, if need be */

#ifdef DEBUG
	if (unlikely(flags & ~(KM_SLEEP|KM_NOSLEEP|KM_NOFS|KM_MAYFAIL))) {
		printk(KERN_WARNING
		    "XFS: memory allocation with wrong flags (%x)\n", flags);
		BUG();
	}
#endif

	if (flags & KM_NOSLEEP) {
		lflags |= GFP_ATOMIC;
	} else {
		lflags |= GFP_KERNEL;

		/* avoid recusive callbacks to filesystem during transactions */
		if (PFLAGS_TEST_FSTRANS() || (flags & KM_NOFS))
			lflags &= ~__GFP_FS;
	}
        
        return lflags;
}

static __inline kmem_zone_t *
kmem_zone_init(int size, char *zone_name)
{
	return kmem_cache_create(zone_name, size, 0, 0, NULL, NULL);
}

static __inline void
kmem_zone_free(kmem_zone_t *zone, void *ptr)
{
	kmem_cache_free(zone, ptr);
}

static __inline void
kmem_zone_destroy(kmem_zone_t *zone)
{
	if (zone && kmem_cache_destroy(zone))
		BUG();
}

extern void	    *kmem_zone_zalloc(kmem_zone_t *, unsigned int __nocast);
extern void	    *kmem_zone_alloc(kmem_zone_t *, unsigned int __nocast);

extern void	    *kmem_alloc(size_t, unsigned int __nocast);
extern void	    *kmem_realloc(void *, size_t, size_t, unsigned int __nocast);
extern void	    *kmem_zalloc(size_t, unsigned int __nocast);
extern void         kmem_free(void *, size_t);

typedef struct shrinker *kmem_shaker_t;
typedef int (*kmem_shake_func_t)(int, gfp_t);

static __inline kmem_shaker_t
kmem_shake_register(kmem_shake_func_t sfunc)
{
	return set_shrinker(DEFAULT_SEEKS, sfunc);
}

static __inline void
kmem_shake_deregister(kmem_shaker_t shrinker)
{
	remove_shrinker(shrinker);
}

static __inline int
kmem_shake_allow(gfp_t gfp_mask)
{
	return (gfp_mask & __GFP_WAIT);
}

#endif /* __XFS_SUPPORT_KMEM_H__ */
