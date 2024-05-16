/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IO_PAGECACHE_H
#define _BCACHEFS_FS_IO_PAGECACHE_H

#include <linux/pagemap.h>

typedef DARRAY(struct folio *) folios;

int bch2_filemap_get_contig_folios_d(struct address_space *, loff_t,
				     u64, fgf_t, gfp_t, folios *);
int bch2_write_invalidate_inode_pages_range(struct address_space *, loff_t, loff_t);

/*
 * Use u64 for the end pos and sector helpers because if the folio covers the
 * max supported range of the mapping, the start offset of the next folio
 * overflows loff_t. This breaks much of the range based processing in the
 * buffered write path.
 */
static inline u64 folio_end_pos(struct folio *folio)
{
	return folio_pos(folio) + folio_size(folio);
}

static inline size_t folio_sectors(struct folio *folio)
{
	return PAGE_SECTORS << folio_order(folio);
}

static inline loff_t folio_sector(struct folio *folio)
{
	return folio_pos(folio) >> 9;
}

static inline u64 folio_end_sector(struct folio *folio)
{
	return folio_end_pos(folio) >> 9;
}

#define BCH_FOLIO_SECTOR_STATE()	\
	x(unallocated)			\
	x(reserved)			\
	x(dirty)			\
	x(dirty_reserved)		\
	x(allocated)

enum bch_folio_sector_state {
#define x(n)	SECTOR_##n,
	BCH_FOLIO_SECTOR_STATE()
#undef x
};

struct bch_folio_sector {
	/* Uncompressed, fully allocated replicas (or on disk reservation): */
	u8			nr_replicas:4,
	/* Owns PAGE_SECTORS * replicas_reserved sized in memory reservation: */
				replicas_reserved:4;
	u8			state;
};

struct bch_folio {
	spinlock_t		lock;
	atomic_t		write_count;
	/*
	 * Is the sector state up to date with the btree?
	 * (Not the data itself)
	 */
	bool			uptodate;
	struct bch_folio_sector	s[];
};

/* Helper for when we need to add debug instrumentation: */
static inline void bch2_folio_sector_set(struct folio *folio,
			     struct bch_folio *s,
			     unsigned i, unsigned n)
{
	s->s[i].state = n;
}

/* file offset (to folio offset) to bch_folio_sector index */
static inline int folio_pos_to_s(struct folio *folio, loff_t pos)
{
	u64 f_offset = pos - folio_pos(folio);

	BUG_ON(pos < folio_pos(folio) || pos >= folio_end_pos(folio));
	return f_offset >> SECTOR_SHIFT;
}

/* for newly allocated folios: */
static inline void __bch2_folio_release(struct folio *folio)
{
	kfree(folio_detach_private(folio));
}

static inline void bch2_folio_release(struct folio *folio)
{
	EBUG_ON(!folio_test_locked(folio));
	__bch2_folio_release(folio);
}

static inline struct bch_folio *__bch2_folio(struct folio *folio)
{
	return folio_has_private(folio)
		? (struct bch_folio *) folio_get_private(folio)
		: NULL;
}

static inline struct bch_folio *bch2_folio(struct folio *folio)
{
	EBUG_ON(!folio_test_locked(folio));

	return __bch2_folio(folio);
}

struct bch_folio *__bch2_folio_create(struct folio *, gfp_t);
struct bch_folio *bch2_folio_create(struct folio *, gfp_t);

struct bch2_folio_reservation {
	struct disk_reservation	disk;
	struct quota_res	quota;
};

static inline unsigned inode_nr_replicas(struct bch_fs *c, struct bch_inode_info *inode)
{
	/* XXX: this should not be open coded */
	return inode->ei_inode.bi_data_replicas
		? inode->ei_inode.bi_data_replicas - 1
		: c->opts.data_replicas;
}

static inline void bch2_folio_reservation_init(struct bch_fs *c,
			struct bch_inode_info *inode,
			struct bch2_folio_reservation *res)
{
	memset(res, 0, sizeof(*res));

	res->disk.nr_replicas = inode_nr_replicas(c, inode);
}

int bch2_folio_set(struct bch_fs *, subvol_inum, struct folio **, unsigned);
void bch2_bio_page_state_set(struct bio *, struct bkey_s_c);

void bch2_mark_pagecache_unallocated(struct bch_inode_info *, u64, u64);
int bch2_mark_pagecache_reserved(struct bch_inode_info *, u64 *, u64, bool);

int bch2_get_folio_disk_reservation(struct bch_fs *,
				struct bch_inode_info *,
				struct folio *, bool);

void bch2_folio_reservation_put(struct bch_fs *,
			struct bch_inode_info *,
			struct bch2_folio_reservation *);
int bch2_folio_reservation_get(struct bch_fs *,
			struct bch_inode_info *,
			struct folio *,
			struct bch2_folio_reservation *,
			unsigned, unsigned);

void bch2_set_folio_dirty(struct bch_fs *,
			  struct bch_inode_info *,
			  struct folio *,
			  struct bch2_folio_reservation *,
			  unsigned, unsigned);

vm_fault_t bch2_page_fault(struct vm_fault *);
vm_fault_t bch2_page_mkwrite(struct vm_fault *);
void bch2_invalidate_folio(struct folio *, size_t, size_t);
bool bch2_release_folio(struct folio *, gfp_t);

loff_t bch2_seek_pagecache_data(struct inode *, loff_t, loff_t, unsigned, bool);
loff_t bch2_seek_pagecache_hole(struct inode *, loff_t, loff_t, unsigned, bool);
int bch2_clamp_data_hole(struct inode *, u64 *, u64 *, unsigned, bool);

#endif /* _BCACHEFS_FS_IO_PAGECACHE_H */
