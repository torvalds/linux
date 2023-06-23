/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 */
#ifndef __EROFS_FS_ZDATA_H
#define __EROFS_FS_ZDATA_H

#include "internal.h"
#include "tagptr.h"

#define Z_EROFS_PCLUSTER_MAX_PAGES	(Z_EROFS_PCLUSTER_MAX_SIZE / PAGE_SIZE)
#define Z_EROFS_INLINE_BVECS		2

/*
 * let's leave a type here in case of introducing
 * another tagged pointer later.
 */
typedef void *z_erofs_next_pcluster_t;

struct z_erofs_bvec {
	struct page *page;
	int offset;
	unsigned int end;
};

#define __Z_EROFS_BVSET(name, total) \
struct name { \
	/* point to the next page which contains the following bvecs */ \
	struct page *nextpage; \
	struct z_erofs_bvec bvec[total]; \
}
__Z_EROFS_BVSET(z_erofs_bvset,);
__Z_EROFS_BVSET(z_erofs_bvset_inline, Z_EROFS_INLINE_BVECS);

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only
 *    for everyone else;
 *
 * L: Field should be protected by the pcluster lock;
 *
 * A: Field should be accessed / updated in atomic for parallelized code.
 */
struct z_erofs_pcluster {
	struct erofs_workgroup obj;
	struct mutex lock;

	/* A: point to next chained pcluster or TAILs */
	z_erofs_next_pcluster_t next;

	/* L: the maximum decompression size of this round */
	unsigned int length;

	/* L: total number of bvecs */
	unsigned int vcnt;

	/* I: page offset of start position of decompression */
	unsigned short pageofs_out;

	/* I: page offset of inline compressed data */
	unsigned short pageofs_in;

	union {
		/* L: inline a certain number of bvec for bootstrap */
		struct z_erofs_bvset_inline bvset;

		/* I: can be used to free the pcluster by RCU. */
		struct rcu_head rcu;
	};

	union {
		/* I: physical cluster size in pages */
		unsigned short pclusterpages;

		/* I: tailpacking inline compressed size */
		unsigned short tailpacking_size;
	};

	/* I: compression algorithm format */
	unsigned char algorithmformat;

	/* L: whether partial decompression or not */
	bool partial;

	/* L: indicate several pageofs_outs or not */
	bool multibases;

	/* A: compressed bvecs (can be cached or inplaced pages) */
	struct z_erofs_bvec compressed_bvecs[];
};

/* let's avoid the valid 32-bit kernel addresses */

/* the chained workgroup has't submitted io (still open) */
#define Z_EROFS_PCLUSTER_TAIL           ((void *)0x5F0ECAFE)
/* the chained workgroup has already submitted io */
#define Z_EROFS_PCLUSTER_TAIL_CLOSED    ((void *)0x5F0EDEAD)

#define Z_EROFS_PCLUSTER_NIL            (NULL)

struct z_erofs_decompressqueue {
	struct super_block *sb;
	atomic_t pending_bios;
	z_erofs_next_pcluster_t head;

	union {
		struct completion done;
		struct work_struct work;
	} u;

	bool eio;
};

static inline bool z_erofs_is_inline_pcluster(struct z_erofs_pcluster *pcl)
{
	return !pcl->obj.index;
}

static inline unsigned int z_erofs_pclusterpages(struct z_erofs_pcluster *pcl)
{
	if (z_erofs_is_inline_pcluster(pcl))
		return 1;
	return pcl->pclusterpages;
}

/*
 * bit 30: I/O error occurred on this page
 * bit 0 - 29: remaining parts to complete this page
 */
#define Z_EROFS_PAGE_EIO			(1 << 30)

static inline void z_erofs_onlinepage_init(struct page *page)
{
	union {
		atomic_t o;
		unsigned long v;
	} u = { .o = ATOMIC_INIT(1) };

	set_page_private(page, u.v);
	smp_wmb();
	SetPagePrivate(page);
}

static inline void z_erofs_onlinepage_split(struct page *page)
{
	atomic_inc((atomic_t *)&page->private);
}

static inline void z_erofs_page_mark_eio(struct page *page)
{
	int orig;

	do {
		orig = atomic_read((atomic_t *)&page->private);
	} while (atomic_cmpxchg((atomic_t *)&page->private, orig,
				orig | Z_EROFS_PAGE_EIO) != orig);
}

static inline void z_erofs_onlinepage_endio(struct page *page)
{
	unsigned int v;

	DBG_BUGON(!PagePrivate(page));
	v = atomic_dec_return((atomic_t *)&page->private);
	if (!(v & ~Z_EROFS_PAGE_EIO)) {
		set_page_private(page, 0);
		ClearPagePrivate(page);
		if (!(v & Z_EROFS_PAGE_EIO))
			SetPageUptodate(page);
		unlock_page(page);
	}
}

#define Z_EROFS_ONSTACK_PAGES		32

#endif
