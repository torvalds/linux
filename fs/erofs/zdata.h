/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#ifndef __EROFS_FS_ZDATA_H
#define __EROFS_FS_ZDATA_H

#include "internal.h"
#include "zpvec.h"

#define Z_EROFS_NR_INLINE_PAGEVECS      3

/*
 * Structure fields follow one of the following exclusion rules.
 *
 * I: Modifiable by initialization/destruction paths and read-only
 *    for everyone else;
 *
 * L: Field should be protected by pageset lock;
 *
 * A: Field should be accessed / updated in atomic for parallelized code.
 */
struct z_erofs_collection {
	struct mutex lock;

	/* I: page offset of start position of decompression */
	unsigned short pageofs;

	/* L: maximum relative page index in pagevec[] */
	unsigned short nr_pages;

	/* L: total number of pages in pagevec[] */
	unsigned int vcnt;

	union {
		/* L: inline a certain number of pagevecs for bootstrap */
		erofs_vtptr_t pagevec[Z_EROFS_NR_INLINE_PAGEVECS];

		/* I: can be used to free the pcluster by RCU. */
		struct rcu_head rcu;
	};
};

#define Z_EROFS_PCLUSTER_FULL_LENGTH    0x00000001
#define Z_EROFS_PCLUSTER_LENGTH_BIT     1

/*
 * let's leave a type here in case of introducing
 * another tagged pointer later.
 */
typedef void *z_erofs_next_pcluster_t;

struct z_erofs_pcluster {
	struct erofs_workgroup obj;
	struct z_erofs_collection primary_collection;

	/* A: point to next chained pcluster or TAILs */
	z_erofs_next_pcluster_t next;

	/* A: compressed pages (including multi-usage pages) */
	struct page *compressed_pages[Z_EROFS_CLUSTER_MAX_PAGES];

	/* A: lower limit of decompressed length and if full length or not */
	unsigned int length;

	/* I: compression algorithm format */
	unsigned char algorithmformat;
	/* I: bit shift of physical cluster size */
	unsigned char clusterbits;
};

#define z_erofs_primarycollection(pcluster) (&(pcluster)->primary_collection)

/* let's avoid the valid 32-bit kernel addresses */

/* the chained workgroup has't submitted io (still open) */
#define Z_EROFS_PCLUSTER_TAIL           ((void *)0x5F0ECAFE)
/* the chained workgroup has already submitted io */
#define Z_EROFS_PCLUSTER_TAIL_CLOSED    ((void *)0x5F0EDEAD)

#define Z_EROFS_PCLUSTER_NIL            (NULL)

#define Z_EROFS_WORKGROUP_SIZE  sizeof(struct z_erofs_pcluster)

struct z_erofs_decompressqueue {
	struct super_block *sb;
	atomic_t pending_bios;
	z_erofs_next_pcluster_t head;

	union {
		wait_queue_head_t wait;
		struct work_struct work;
	} u;
};

#define MNGD_MAPPING(sbi)	((sbi)->managed_cache->i_mapping)
static inline bool erofs_page_is_managed(const struct erofs_sb_info *sbi,
					 struct page *page)
{
	return page->mapping == MNGD_MAPPING(sbi);
}

#define Z_EROFS_ONLINEPAGE_COUNT_BITS   2
#define Z_EROFS_ONLINEPAGE_COUNT_MASK   ((1 << Z_EROFS_ONLINEPAGE_COUNT_BITS) - 1)
#define Z_EROFS_ONLINEPAGE_INDEX_SHIFT  (Z_EROFS_ONLINEPAGE_COUNT_BITS)

/*
 * waiters (aka. ongoing_packs): # to unlock the page
 * sub-index: 0 - for partial page, >= 1 full page sub-index
 */
typedef atomic_t z_erofs_onlinepage_t;

/* type punning */
union z_erofs_onlinepage_converter {
	z_erofs_onlinepage_t *o;
	unsigned long *v;
};

static inline unsigned int z_erofs_onlinepage_index(struct page *page)
{
	union z_erofs_onlinepage_converter u;

	DBG_BUGON(!PagePrivate(page));
	u.v = &page_private(page);

	return atomic_read(u.o) >> Z_EROFS_ONLINEPAGE_INDEX_SHIFT;
}

static inline void z_erofs_onlinepage_init(struct page *page)
{
	union {
		z_erofs_onlinepage_t o;
		unsigned long v;
	/* keep from being unlocked in advance */
	} u = { .o = ATOMIC_INIT(1) };

	set_page_private(page, u.v);
	smp_wmb();
	SetPagePrivate(page);
}

static inline void z_erofs_onlinepage_fixup(struct page *page,
	uintptr_t index, bool down)
{
	union z_erofs_onlinepage_converter u = { .v = &page_private(page) };
	int orig, orig_index, val;

repeat:
	orig = atomic_read(u.o);
	orig_index = orig >> Z_EROFS_ONLINEPAGE_INDEX_SHIFT;
	if (orig_index) {
		if (!index)
			return;

		DBG_BUGON(orig_index != index);
	}

	val = (index << Z_EROFS_ONLINEPAGE_INDEX_SHIFT) |
		((orig & Z_EROFS_ONLINEPAGE_COUNT_MASK) + (unsigned int)down);
	if (atomic_cmpxchg(u.o, orig, val) != orig)
		goto repeat;
}

static inline void z_erofs_onlinepage_endio(struct page *page)
{
	union z_erofs_onlinepage_converter u;
	unsigned int v;

	DBG_BUGON(!PagePrivate(page));
	u.v = &page_private(page);

	v = atomic_dec_return(u.o);
	if (!(v & Z_EROFS_ONLINEPAGE_COUNT_MASK)) {
		set_page_private(page, 0);
		ClearPagePrivate(page);
		if (!PageError(page))
			SetPageUptodate(page);
		unlock_page(page);
	}
	erofs_dbg("%s, page %p value %x", __func__, page, atomic_read(u.o));
}

#define Z_EROFS_VMAP_ONSTACK_PAGES	\
	min_t(unsigned int, THREAD_SIZE / 8 / sizeof(struct page *), 96U)
#define Z_EROFS_VMAP_GLOBAL_PAGES	2048

#endif

