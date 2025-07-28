// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/node.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/mpage.h>
#include <linux/sched/mm.h>
#include <linux/blkdev.h>
#include <linux/pagevec.h>
#include <linux/swap.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "iostat.h"
#include <trace/events/f2fs.h>

#define on_f2fs_build_free_nids(nm_i) mutex_is_locked(&(nm_i)->build_lock)

static struct kmem_cache *nat_entry_slab;
static struct kmem_cache *free_nid_slab;
static struct kmem_cache *nat_entry_set_slab;
static struct kmem_cache *fsync_node_entry_slab;

/*
 * Check whether the given nid is within node id range.
 */
int f2fs_check_nid_range(struct f2fs_sb_info *sbi, nid_t nid)
{
	if (unlikely(nid < F2FS_ROOT_INO(sbi) || nid >= NM_I(sbi)->max_nid)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: out-of-range nid=%x, run fsck to fix.",
			  __func__, nid);
		f2fs_handle_error(sbi, ERROR_CORRUPTED_INODE);
		return -EFSCORRUPTED;
	}
	return 0;
}

bool f2fs_available_free_memory(struct f2fs_sb_info *sbi, int type)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct discard_cmd_control *dcc = SM_I(sbi)->dcc_info;
	struct sysinfo val;
	unsigned long avail_ram;
	unsigned long mem_size = 0;
	bool res = false;

	if (!nm_i)
		return true;

	si_meminfo(&val);

	/* only uses low memory */
	avail_ram = val.totalram - val.totalhigh;

	/*
	 * give 25%, 25%, 50%, 50%, 25%, 25% memory for each components respectively
	 */
	if (type == FREE_NIDS) {
		mem_size = (nm_i->nid_cnt[FREE_NID] *
				sizeof(struct free_nid)) >> PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 2);
	} else if (type == NAT_ENTRIES) {
		mem_size = (nm_i->nat_cnt[TOTAL_NAT] *
				sizeof(struct nat_entry)) >> PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 2);
		if (excess_cached_nats(sbi))
			res = false;
	} else if (type == DIRTY_DENTS) {
		if (sbi->sb->s_bdi->wb.dirty_exceeded)
			return false;
		mem_size = get_pages(sbi, F2FS_DIRTY_DENTS);
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 1);
	} else if (type == INO_ENTRIES) {
		int i;

		for (i = 0; i < MAX_INO_ENTRY; i++)
			mem_size += sbi->im[i].ino_num *
						sizeof(struct ino_entry);
		mem_size >>= PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 1);
	} else if (type == READ_EXTENT_CACHE || type == AGE_EXTENT_CACHE) {
		enum extent_type etype = type == READ_EXTENT_CACHE ?
						EX_READ : EX_BLOCK_AGE;
		struct extent_tree_info *eti = &sbi->extent_tree[etype];

		mem_size = (atomic_read(&eti->total_ext_tree) *
				sizeof(struct extent_tree) +
				atomic_read(&eti->total_ext_node) *
				sizeof(struct extent_node)) >> PAGE_SHIFT;
		res = mem_size < ((avail_ram * nm_i->ram_thresh / 100) >> 2);
	} else if (type == DISCARD_CACHE) {
		mem_size = (atomic_read(&dcc->discard_cmd_cnt) *
				sizeof(struct discard_cmd)) >> PAGE_SHIFT;
		res = mem_size < (avail_ram * nm_i->ram_thresh / 100);
	} else if (type == COMPRESS_PAGE) {
#ifdef CONFIG_F2FS_FS_COMPRESSION
		unsigned long free_ram = val.freeram;

		/*
		 * free memory is lower than watermark or cached page count
		 * exceed threshold, deny caching compress page.
		 */
		res = (free_ram > avail_ram * sbi->compress_watermark / 100) &&
			(COMPRESS_MAPPING(sbi)->nrpages <
			 free_ram * sbi->compress_percent / 100);
#else
		res = false;
#endif
	} else {
		if (!sbi->sb->s_bdi->wb.dirty_exceeded)
			return true;
	}
	return res;
}

static void clear_node_folio_dirty(struct folio *folio)
{
	if (folio_test_dirty(folio)) {
		f2fs_clear_page_cache_dirty_tag(folio);
		folio_clear_dirty_for_io(folio);
		dec_page_count(F2FS_F_SB(folio), F2FS_DIRTY_NODES);
	}
	folio_clear_uptodate(folio);
}

static struct folio *get_current_nat_folio(struct f2fs_sb_info *sbi, nid_t nid)
{
	return f2fs_get_meta_folio_retry(sbi, current_nat_addr(sbi, nid));
}

static struct page *get_next_nat_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct folio *src_folio;
	struct folio *dst_folio;
	pgoff_t dst_off;
	void *src_addr;
	void *dst_addr;
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	dst_off = next_nat_addr(sbi, current_nat_addr(sbi, nid));

	/* get current nat block page with lock */
	src_folio = get_current_nat_folio(sbi, nid);
	if (IS_ERR(src_folio))
		return &src_folio->page;
	dst_folio = f2fs_grab_meta_folio(sbi, dst_off);
	f2fs_bug_on(sbi, folio_test_dirty(src_folio));

	src_addr = folio_address(src_folio);
	dst_addr = folio_address(dst_folio);
	memcpy(dst_addr, src_addr, PAGE_SIZE);
	folio_mark_dirty(dst_folio);
	f2fs_folio_put(src_folio, true);

	set_to_next_nat(nm_i, nid);

	return &dst_folio->page;
}

static struct nat_entry *__alloc_nat_entry(struct f2fs_sb_info *sbi,
						nid_t nid, bool no_fail)
{
	struct nat_entry *new;

	new = f2fs_kmem_cache_alloc(nat_entry_slab,
					GFP_F2FS_ZERO, no_fail, sbi);
	if (new) {
		nat_set_nid(new, nid);
		nat_reset_flag(new);
	}
	return new;
}

static void __free_nat_entry(struct nat_entry *e)
{
	kmem_cache_free(nat_entry_slab, e);
}

/* must be locked by nat_tree_lock */
static struct nat_entry *__init_nat_entry(struct f2fs_nm_info *nm_i,
	struct nat_entry *ne, struct f2fs_nat_entry *raw_ne, bool no_fail)
{
	if (no_fail)
		f2fs_radix_tree_insert(&nm_i->nat_root, nat_get_nid(ne), ne);
	else if (radix_tree_insert(&nm_i->nat_root, nat_get_nid(ne), ne))
		return NULL;

	if (raw_ne)
		node_info_from_raw_nat(&ne->ni, raw_ne);

	spin_lock(&nm_i->nat_list_lock);
	list_add_tail(&ne->list, &nm_i->nat_entries);
	spin_unlock(&nm_i->nat_list_lock);

	nm_i->nat_cnt[TOTAL_NAT]++;
	nm_i->nat_cnt[RECLAIMABLE_NAT]++;
	return ne;
}

static struct nat_entry *__lookup_nat_cache(struct f2fs_nm_info *nm_i, nid_t n)
{
	struct nat_entry *ne;

	ne = radix_tree_lookup(&nm_i->nat_root, n);

	/* for recent accessed nat entry, move it to tail of lru list */
	if (ne && !get_nat_flag(ne, IS_DIRTY)) {
		spin_lock(&nm_i->nat_list_lock);
		if (!list_empty(&ne->list))
			list_move_tail(&ne->list, &nm_i->nat_entries);
		spin_unlock(&nm_i->nat_list_lock);
	}

	return ne;
}

static unsigned int __gang_lookup_nat_cache(struct f2fs_nm_info *nm_i,
		nid_t start, unsigned int nr, struct nat_entry **ep)
{
	return radix_tree_gang_lookup(&nm_i->nat_root, (void **)ep, start, nr);
}

static void __del_from_nat_cache(struct f2fs_nm_info *nm_i, struct nat_entry *e)
{
	radix_tree_delete(&nm_i->nat_root, nat_get_nid(e));
	nm_i->nat_cnt[TOTAL_NAT]--;
	nm_i->nat_cnt[RECLAIMABLE_NAT]--;
	__free_nat_entry(e);
}

static struct nat_entry_set *__grab_nat_entry_set(struct f2fs_nm_info *nm_i,
							struct nat_entry *ne)
{
	nid_t set = NAT_BLOCK_OFFSET(ne->ni.nid);
	struct nat_entry_set *head;

	head = radix_tree_lookup(&nm_i->nat_set_root, set);
	if (!head) {
		head = f2fs_kmem_cache_alloc(nat_entry_set_slab,
						GFP_NOFS, true, NULL);

		INIT_LIST_HEAD(&head->entry_list);
		INIT_LIST_HEAD(&head->set_list);
		head->set = set;
		head->entry_cnt = 0;
		f2fs_radix_tree_insert(&nm_i->nat_set_root, set, head);
	}
	return head;
}

static void __set_nat_cache_dirty(struct f2fs_nm_info *nm_i,
						struct nat_entry *ne)
{
	struct nat_entry_set *head;
	bool new_ne = nat_get_blkaddr(ne) == NEW_ADDR;

	if (!new_ne)
		head = __grab_nat_entry_set(nm_i, ne);

	/*
	 * update entry_cnt in below condition:
	 * 1. update NEW_ADDR to valid block address;
	 * 2. update old block address to new one;
	 */
	if (!new_ne && (get_nat_flag(ne, IS_PREALLOC) ||
				!get_nat_flag(ne, IS_DIRTY)))
		head->entry_cnt++;

	set_nat_flag(ne, IS_PREALLOC, new_ne);

	if (get_nat_flag(ne, IS_DIRTY))
		goto refresh_list;

	nm_i->nat_cnt[DIRTY_NAT]++;
	nm_i->nat_cnt[RECLAIMABLE_NAT]--;
	set_nat_flag(ne, IS_DIRTY, true);
refresh_list:
	spin_lock(&nm_i->nat_list_lock);
	if (new_ne)
		list_del_init(&ne->list);
	else
		list_move_tail(&ne->list, &head->entry_list);
	spin_unlock(&nm_i->nat_list_lock);
}

static void __clear_nat_cache_dirty(struct f2fs_nm_info *nm_i,
		struct nat_entry_set *set, struct nat_entry *ne)
{
	spin_lock(&nm_i->nat_list_lock);
	list_move_tail(&ne->list, &nm_i->nat_entries);
	spin_unlock(&nm_i->nat_list_lock);

	set_nat_flag(ne, IS_DIRTY, false);
	set->entry_cnt--;
	nm_i->nat_cnt[DIRTY_NAT]--;
	nm_i->nat_cnt[RECLAIMABLE_NAT]++;
}

static unsigned int __gang_lookup_nat_set(struct f2fs_nm_info *nm_i,
		nid_t start, unsigned int nr, struct nat_entry_set **ep)
{
	return radix_tree_gang_lookup(&nm_i->nat_set_root, (void **)ep,
							start, nr);
}

bool f2fs_in_warm_node_list(struct f2fs_sb_info *sbi, struct folio *folio)
{
	return is_node_folio(folio) && IS_DNODE(&folio->page) &&
					is_cold_node(&folio->page);
}

void f2fs_init_fsync_node_info(struct f2fs_sb_info *sbi)
{
	spin_lock_init(&sbi->fsync_node_lock);
	INIT_LIST_HEAD(&sbi->fsync_node_list);
	sbi->fsync_seg_id = 0;
	sbi->fsync_node_num = 0;
}

static unsigned int f2fs_add_fsync_node_entry(struct f2fs_sb_info *sbi,
		struct folio *folio)
{
	struct fsync_node_entry *fn;
	unsigned long flags;
	unsigned int seq_id;

	fn = f2fs_kmem_cache_alloc(fsync_node_entry_slab,
					GFP_NOFS, true, NULL);

	folio_get(folio);
	fn->folio = folio;
	INIT_LIST_HEAD(&fn->list);

	spin_lock_irqsave(&sbi->fsync_node_lock, flags);
	list_add_tail(&fn->list, &sbi->fsync_node_list);
	fn->seq_id = sbi->fsync_seg_id++;
	seq_id = fn->seq_id;
	sbi->fsync_node_num++;
	spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);

	return seq_id;
}

void f2fs_del_fsync_node_entry(struct f2fs_sb_info *sbi, struct folio *folio)
{
	struct fsync_node_entry *fn;
	unsigned long flags;

	spin_lock_irqsave(&sbi->fsync_node_lock, flags);
	list_for_each_entry(fn, &sbi->fsync_node_list, list) {
		if (fn->folio == folio) {
			list_del(&fn->list);
			sbi->fsync_node_num--;
			spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
			kmem_cache_free(fsync_node_entry_slab, fn);
			folio_put(folio);
			return;
		}
	}
	spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
	f2fs_bug_on(sbi, 1);
}

void f2fs_reset_fsync_node_info(struct f2fs_sb_info *sbi)
{
	unsigned long flags;

	spin_lock_irqsave(&sbi->fsync_node_lock, flags);
	sbi->fsync_seg_id = 0;
	spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
}

int f2fs_need_dentry_mark(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	bool need = false;

	f2fs_down_read(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (e) {
		if (!get_nat_flag(e, IS_CHECKPOINTED) &&
				!get_nat_flag(e, HAS_FSYNCED_INODE))
			need = true;
	}
	f2fs_up_read(&nm_i->nat_tree_lock);
	return need;
}

bool f2fs_is_checkpointed_node(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	bool is_cp = true;

	f2fs_down_read(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (e && !get_nat_flag(e, IS_CHECKPOINTED))
		is_cp = false;
	f2fs_up_read(&nm_i->nat_tree_lock);
	return is_cp;
}

bool f2fs_need_inode_block_update(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	bool need_update = true;

	f2fs_down_read(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, ino);
	if (e && get_nat_flag(e, HAS_LAST_FSYNC) &&
			(get_nat_flag(e, IS_CHECKPOINTED) ||
			 get_nat_flag(e, HAS_FSYNCED_INODE)))
		need_update = false;
	f2fs_up_read(&nm_i->nat_tree_lock);
	return need_update;
}

/* must be locked by nat_tree_lock */
static void cache_nat_entry(struct f2fs_sb_info *sbi, nid_t nid,
						struct f2fs_nat_entry *ne)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *new, *e;

	/* Let's mitigate lock contention of nat_tree_lock during checkpoint */
	if (f2fs_rwsem_is_locked(&sbi->cp_global_sem))
		return;

	new = __alloc_nat_entry(sbi, nid, false);
	if (!new)
		return;

	f2fs_down_write(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (!e)
		e = __init_nat_entry(nm_i, new, ne, false);
	else
		f2fs_bug_on(sbi, nat_get_ino(e) != le32_to_cpu(ne->ino) ||
				nat_get_blkaddr(e) !=
					le32_to_cpu(ne->block_addr) ||
				nat_get_version(e) != ne->version);
	f2fs_up_write(&nm_i->nat_tree_lock);
	if (e != new)
		__free_nat_entry(new);
}

static void set_node_addr(struct f2fs_sb_info *sbi, struct node_info *ni,
			block_t new_blkaddr, bool fsync_done)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct nat_entry *e;
	struct nat_entry *new = __alloc_nat_entry(sbi, ni->nid, true);

	f2fs_down_write(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, ni->nid);
	if (!e) {
		e = __init_nat_entry(nm_i, new, NULL, true);
		copy_node_info(&e->ni, ni);
		f2fs_bug_on(sbi, ni->blk_addr == NEW_ADDR);
	} else if (new_blkaddr == NEW_ADDR) {
		/*
		 * when nid is reallocated,
		 * previous nat entry can be remained in nat cache.
		 * So, reinitialize it with new information.
		 */
		copy_node_info(&e->ni, ni);
		f2fs_bug_on(sbi, ni->blk_addr != NULL_ADDR);
	}
	/* let's free early to reduce memory consumption */
	if (e != new)
		__free_nat_entry(new);

	/* sanity check */
	f2fs_bug_on(sbi, nat_get_blkaddr(e) != ni->blk_addr);
	f2fs_bug_on(sbi, nat_get_blkaddr(e) == NULL_ADDR &&
			new_blkaddr == NULL_ADDR);
	f2fs_bug_on(sbi, nat_get_blkaddr(e) == NEW_ADDR &&
			new_blkaddr == NEW_ADDR);
	f2fs_bug_on(sbi, __is_valid_data_blkaddr(nat_get_blkaddr(e)) &&
			new_blkaddr == NEW_ADDR);

	/* increment version no as node is removed */
	if (nat_get_blkaddr(e) != NEW_ADDR && new_blkaddr == NULL_ADDR) {
		unsigned char version = nat_get_version(e);

		nat_set_version(e, inc_node_version(version));
	}

	/* change address */
	nat_set_blkaddr(e, new_blkaddr);
	if (!__is_valid_data_blkaddr(new_blkaddr))
		set_nat_flag(e, IS_CHECKPOINTED, false);
	__set_nat_cache_dirty(nm_i, e);

	/* update fsync_mark if its inode nat entry is still alive */
	if (ni->nid != ni->ino)
		e = __lookup_nat_cache(nm_i, ni->ino);
	if (e) {
		if (fsync_done && ni->nid == ni->ino)
			set_nat_flag(e, HAS_FSYNCED_INODE, true);
		set_nat_flag(e, HAS_LAST_FSYNC, fsync_done);
	}
	f2fs_up_write(&nm_i->nat_tree_lock);
}

int f2fs_try_to_free_nats(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int nr = nr_shrink;

	if (!f2fs_down_write_trylock(&nm_i->nat_tree_lock))
		return 0;

	spin_lock(&nm_i->nat_list_lock);
	while (nr_shrink) {
		struct nat_entry *ne;

		if (list_empty(&nm_i->nat_entries))
			break;

		ne = list_first_entry(&nm_i->nat_entries,
					struct nat_entry, list);
		list_del(&ne->list);
		spin_unlock(&nm_i->nat_list_lock);

		__del_from_nat_cache(nm_i, ne);
		nr_shrink--;

		spin_lock(&nm_i->nat_list_lock);
	}
	spin_unlock(&nm_i->nat_list_lock);

	f2fs_up_write(&nm_i->nat_tree_lock);
	return nr - nr_shrink;
}

int f2fs_get_node_info(struct f2fs_sb_info *sbi, nid_t nid,
				struct node_info *ni, bool checkpoint_context)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_journal *journal = curseg->journal;
	nid_t start_nid = START_NID(nid);
	struct f2fs_nat_block *nat_blk;
	struct folio *folio = NULL;
	struct f2fs_nat_entry ne;
	struct nat_entry *e;
	pgoff_t index;
	block_t blkaddr;
	int i;

	ni->flag = 0;
	ni->nid = nid;
retry:
	/* Check nat cache */
	f2fs_down_read(&nm_i->nat_tree_lock);
	e = __lookup_nat_cache(nm_i, nid);
	if (e) {
		ni->ino = nat_get_ino(e);
		ni->blk_addr = nat_get_blkaddr(e);
		ni->version = nat_get_version(e);
		f2fs_up_read(&nm_i->nat_tree_lock);
		return 0;
	}

	/*
	 * Check current segment summary by trying to grab journal_rwsem first.
	 * This sem is on the critical path on the checkpoint requiring the above
	 * nat_tree_lock. Therefore, we should retry, if we failed to grab here
	 * while not bothering checkpoint.
	 */
	if (!f2fs_rwsem_is_locked(&sbi->cp_global_sem) || checkpoint_context) {
		down_read(&curseg->journal_rwsem);
	} else if (f2fs_rwsem_is_contended(&nm_i->nat_tree_lock) ||
				!down_read_trylock(&curseg->journal_rwsem)) {
		f2fs_up_read(&nm_i->nat_tree_lock);
		goto retry;
	}

	i = f2fs_lookup_journal_in_cursum(journal, NAT_JOURNAL, nid, 0);
	if (i >= 0) {
		ne = nat_in_journal(journal, i);
		node_info_from_raw_nat(ni, &ne);
	}
	up_read(&curseg->journal_rwsem);
	if (i >= 0) {
		f2fs_up_read(&nm_i->nat_tree_lock);
		goto cache;
	}

	/* Fill node_info from nat page */
	index = current_nat_addr(sbi, nid);
	f2fs_up_read(&nm_i->nat_tree_lock);

	folio = f2fs_get_meta_folio(sbi, index);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	nat_blk = folio_address(folio);
	ne = nat_blk->entries[nid - start_nid];
	node_info_from_raw_nat(ni, &ne);
	f2fs_folio_put(folio, true);
cache:
	blkaddr = le32_to_cpu(ne.block_addr);
	if (__is_valid_data_blkaddr(blkaddr) &&
		!f2fs_is_valid_blkaddr(sbi, blkaddr, DATA_GENERIC_ENHANCE))
		return -EFAULT;

	/* cache nat entry */
	cache_nat_entry(sbi, nid, &ne);
	return 0;
}

/*
 * readahead MAX_RA_NODE number of node pages.
 */
static void f2fs_ra_node_pages(struct folio *parent, int start, int n)
{
	struct f2fs_sb_info *sbi = F2FS_F_SB(parent);
	struct blk_plug plug;
	int i, end;
	nid_t nid;

	blk_start_plug(&plug);

	/* Then, try readahead for siblings of the desired node */
	end = start + n;
	end = min(end, (int)NIDS_PER_BLOCK);
	for (i = start; i < end; i++) {
		nid = get_nid(&parent->page, i, false);
		f2fs_ra_node_page(sbi, nid);
	}

	blk_finish_plug(&plug);
}

pgoff_t f2fs_get_next_page_offset(struct dnode_of_data *dn, pgoff_t pgofs)
{
	const long direct_index = ADDRS_PER_INODE(dn->inode);
	const long direct_blks = ADDRS_PER_BLOCK(dn->inode);
	const long indirect_blks = ADDRS_PER_BLOCK(dn->inode) * NIDS_PER_BLOCK;
	unsigned int skipped_unit = ADDRS_PER_BLOCK(dn->inode);
	int cur_level = dn->cur_level;
	int max_level = dn->max_level;
	pgoff_t base = 0;

	if (!dn->max_level)
		return pgofs + 1;

	while (max_level-- > cur_level)
		skipped_unit *= NIDS_PER_BLOCK;

	switch (dn->max_level) {
	case 3:
		base += 2 * indirect_blks;
		fallthrough;
	case 2:
		base += 2 * direct_blks;
		fallthrough;
	case 1:
		base += direct_index;
		break;
	default:
		f2fs_bug_on(F2FS_I_SB(dn->inode), 1);
	}

	return ((pgofs - base) / skipped_unit + 1) * skipped_unit + base;
}

/*
 * The maximum depth is four.
 * Offset[0] will have raw inode offset.
 */
static int get_node_path(struct inode *inode, long block,
				int offset[4], unsigned int noffset[4])
{
	const long direct_index = ADDRS_PER_INODE(inode);
	const long direct_blks = ADDRS_PER_BLOCK(inode);
	const long dptrs_per_blk = NIDS_PER_BLOCK;
	const long indirect_blks = ADDRS_PER_BLOCK(inode) * NIDS_PER_BLOCK;
	const long dindirect_blks = indirect_blks * NIDS_PER_BLOCK;
	int n = 0;
	int level = 0;

	noffset[0] = 0;

	if (block < direct_index) {
		offset[n] = block;
		goto got;
	}
	block -= direct_index;
	if (block < direct_blks) {
		offset[n++] = NODE_DIR1_BLOCK;
		noffset[n] = 1;
		offset[n] = block;
		level = 1;
		goto got;
	}
	block -= direct_blks;
	if (block < direct_blks) {
		offset[n++] = NODE_DIR2_BLOCK;
		noffset[n] = 2;
		offset[n] = block;
		level = 1;
		goto got;
	}
	block -= direct_blks;
	if (block < indirect_blks) {
		offset[n++] = NODE_IND1_BLOCK;
		noffset[n] = 3;
		offset[n++] = block / direct_blks;
		noffset[n] = 4 + offset[n - 1];
		offset[n] = block % direct_blks;
		level = 2;
		goto got;
	}
	block -= indirect_blks;
	if (block < indirect_blks) {
		offset[n++] = NODE_IND2_BLOCK;
		noffset[n] = 4 + dptrs_per_blk;
		offset[n++] = block / direct_blks;
		noffset[n] = 5 + dptrs_per_blk + offset[n - 1];
		offset[n] = block % direct_blks;
		level = 2;
		goto got;
	}
	block -= indirect_blks;
	if (block < dindirect_blks) {
		offset[n++] = NODE_DIND_BLOCK;
		noffset[n] = 5 + (dptrs_per_blk * 2);
		offset[n++] = block / indirect_blks;
		noffset[n] = 6 + (dptrs_per_blk * 2) +
			      offset[n - 1] * (dptrs_per_blk + 1);
		offset[n++] = (block / direct_blks) % dptrs_per_blk;
		noffset[n] = 7 + (dptrs_per_blk * 2) +
			      offset[n - 2] * (dptrs_per_blk + 1) +
			      offset[n - 1];
		offset[n] = block % direct_blks;
		level = 3;
		goto got;
	} else {
		return -E2BIG;
	}
got:
	return level;
}

static struct folio *f2fs_get_node_folio_ra(struct folio *parent, int start);

/*
 * Caller should call f2fs_put_dnode(dn).
 * Also, it should grab and release a rwsem by calling f2fs_lock_op() and
 * f2fs_unlock_op() only if mode is set with ALLOC_NODE.
 */
int f2fs_get_dnode_of_data(struct dnode_of_data *dn, pgoff_t index, int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct folio *nfolio[4];
	struct folio *parent = NULL;
	int offset[4];
	unsigned int noffset[4];
	nid_t nids[4];
	int level, i = 0;
	int err = 0;

	level = get_node_path(dn->inode, index, offset, noffset);
	if (level < 0)
		return level;

	nids[0] = dn->inode->i_ino;

	if (!dn->inode_folio) {
		nfolio[0] = f2fs_get_inode_folio(sbi, nids[0]);
		if (IS_ERR(nfolio[0]))
			return PTR_ERR(nfolio[0]);
	} else {
		nfolio[0] = dn->inode_folio;
	}

	/* if inline_data is set, should not report any block indices */
	if (f2fs_has_inline_data(dn->inode) && index) {
		err = -ENOENT;
		f2fs_folio_put(nfolio[0], true);
		goto release_out;
	}

	parent = nfolio[0];
	if (level != 0)
		nids[1] = get_nid(&parent->page, offset[0], true);
	dn->inode_folio = nfolio[0];
	dn->inode_folio_locked = true;

	/* get indirect or direct nodes */
	for (i = 1; i <= level; i++) {
		bool done = false;

		if (!nids[i] && mode == ALLOC_NODE) {
			/* alloc new node */
			if (!f2fs_alloc_nid(sbi, &(nids[i]))) {
				err = -ENOSPC;
				goto release_pages;
			}

			dn->nid = nids[i];
			nfolio[i] = f2fs_new_node_folio(dn, noffset[i]);
			if (IS_ERR(nfolio[i])) {
				f2fs_alloc_nid_failed(sbi, nids[i]);
				err = PTR_ERR(nfolio[i]);
				goto release_pages;
			}

			set_nid(parent, offset[i - 1], nids[i], i == 1);
			f2fs_alloc_nid_done(sbi, nids[i]);
			done = true;
		} else if (mode == LOOKUP_NODE_RA && i == level && level > 1) {
			nfolio[i] = f2fs_get_node_folio_ra(parent, offset[i - 1]);
			if (IS_ERR(nfolio[i])) {
				err = PTR_ERR(nfolio[i]);
				goto release_pages;
			}
			done = true;
		}
		if (i == 1) {
			dn->inode_folio_locked = false;
			folio_unlock(parent);
		} else {
			f2fs_folio_put(parent, true);
		}

		if (!done) {
			nfolio[i] = f2fs_get_node_folio(sbi, nids[i]);
			if (IS_ERR(nfolio[i])) {
				err = PTR_ERR(nfolio[i]);
				f2fs_folio_put(nfolio[0], false);
				goto release_out;
			}
		}
		if (i < level) {
			parent = nfolio[i];
			nids[i + 1] = get_nid(&parent->page, offset[i], false);
		}
	}
	dn->nid = nids[level];
	dn->ofs_in_node = offset[level];
	dn->node_folio = nfolio[level];
	dn->data_blkaddr = f2fs_data_blkaddr(dn);

	if (is_inode_flag_set(dn->inode, FI_COMPRESSED_FILE) &&
					f2fs_sb_has_readonly(sbi)) {
		unsigned int cluster_size = F2FS_I(dn->inode)->i_cluster_size;
		unsigned int ofs_in_node = dn->ofs_in_node;
		pgoff_t fofs = index;
		unsigned int c_len;
		block_t blkaddr;

		/* should align fofs and ofs_in_node to cluster_size */
		if (fofs % cluster_size) {
			fofs = round_down(fofs, cluster_size);
			ofs_in_node = round_down(ofs_in_node, cluster_size);
		}

		c_len = f2fs_cluster_blocks_are_contiguous(dn, ofs_in_node);
		if (!c_len)
			goto out;

		blkaddr = data_blkaddr(dn->inode, dn->node_folio, ofs_in_node);
		if (blkaddr == COMPRESS_ADDR)
			blkaddr = data_blkaddr(dn->inode, dn->node_folio,
						ofs_in_node + 1);

		f2fs_update_read_extent_tree_range_compressed(dn->inode,
					fofs, blkaddr, cluster_size, c_len);
	}
out:
	return 0;

release_pages:
	f2fs_folio_put(parent, true);
	if (i > 1)
		f2fs_folio_put(nfolio[0], false);
release_out:
	dn->inode_folio = NULL;
	dn->node_folio = NULL;
	if (err == -ENOENT) {
		dn->cur_level = i;
		dn->max_level = level;
		dn->ofs_in_node = offset[level];
	}
	return err;
}

static int truncate_node(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct node_info ni;
	int err;
	pgoff_t index;

	err = f2fs_get_node_info(sbi, dn->nid, &ni, false);
	if (err)
		return err;

	if (ni.blk_addr != NEW_ADDR &&
		!f2fs_is_valid_blkaddr(sbi, ni.blk_addr, DATA_GENERIC_ENHANCE)) {
		f2fs_err_ratelimited(sbi,
			"nat entry is corrupted, run fsck to fix it, ino:%u, "
			"nid:%u, blkaddr:%u", ni.ino, ni.nid, ni.blk_addr);
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_handle_error(sbi, ERROR_INCONSISTENT_NAT);
		return -EFSCORRUPTED;
	}

	/* Deallocate node address */
	f2fs_invalidate_blocks(sbi, ni.blk_addr, 1);
	dec_valid_node_count(sbi, dn->inode, dn->nid == dn->inode->i_ino);
	set_node_addr(sbi, &ni, NULL_ADDR, false);

	if (dn->nid == dn->inode->i_ino) {
		f2fs_remove_orphan_inode(sbi, dn->nid);
		dec_valid_inode_count(sbi);
		f2fs_inode_synced(dn->inode);
	}

	clear_node_folio_dirty(dn->node_folio);
	set_sbi_flag(sbi, SBI_IS_DIRTY);

	index = dn->node_folio->index;
	f2fs_folio_put(dn->node_folio, true);

	invalidate_mapping_pages(NODE_MAPPING(sbi),
			index, index);

	dn->node_folio = NULL;
	trace_f2fs_truncate_node(dn->inode, dn->nid, ni.blk_addr);

	return 0;
}

static int truncate_dnode(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct folio *folio;
	int err;

	if (dn->nid == 0)
		return 1;

	/* get direct node */
	folio = f2fs_get_node_folio(sbi, dn->nid);
	if (PTR_ERR(folio) == -ENOENT)
		return 1;
	else if (IS_ERR(folio))
		return PTR_ERR(folio);

	if (IS_INODE(&folio->page) || ino_of_node(&folio->page) != dn->inode->i_ino) {
		f2fs_err(sbi, "incorrect node reference, ino: %lu, nid: %u, ino_of_node: %u",
				dn->inode->i_ino, dn->nid, ino_of_node(&folio->page));
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_handle_error(sbi, ERROR_INVALID_NODE_REFERENCE);
		f2fs_folio_put(folio, true);
		return -EFSCORRUPTED;
	}

	/* Make dnode_of_data for parameter */
	dn->node_folio = folio;
	dn->ofs_in_node = 0;
	f2fs_truncate_data_blocks_range(dn, ADDRS_PER_BLOCK(dn->inode));
	err = truncate_node(dn);
	if (err) {
		f2fs_folio_put(folio, true);
		return err;
	}

	return 1;
}

static int truncate_nodes(struct dnode_of_data *dn, unsigned int nofs,
						int ofs, int depth)
{
	struct dnode_of_data rdn = *dn;
	struct folio *folio;
	struct f2fs_node *rn;
	nid_t child_nid;
	unsigned int child_nofs;
	int freed = 0;
	int i, ret;

	if (dn->nid == 0)
		return NIDS_PER_BLOCK + 1;

	trace_f2fs_truncate_nodes_enter(dn->inode, dn->nid, dn->data_blkaddr);

	folio = f2fs_get_node_folio(F2FS_I_SB(dn->inode), dn->nid);
	if (IS_ERR(folio)) {
		trace_f2fs_truncate_nodes_exit(dn->inode, PTR_ERR(folio));
		return PTR_ERR(folio);
	}

	f2fs_ra_node_pages(folio, ofs, NIDS_PER_BLOCK);

	rn = F2FS_NODE(&folio->page);
	if (depth < 3) {
		for (i = ofs; i < NIDS_PER_BLOCK; i++, freed++) {
			child_nid = le32_to_cpu(rn->in.nid[i]);
			if (child_nid == 0)
				continue;
			rdn.nid = child_nid;
			ret = truncate_dnode(&rdn);
			if (ret < 0)
				goto out_err;
			if (set_nid(folio, i, 0, false))
				dn->node_changed = true;
		}
	} else {
		child_nofs = nofs + ofs * (NIDS_PER_BLOCK + 1) + 1;
		for (i = ofs; i < NIDS_PER_BLOCK; i++) {
			child_nid = le32_to_cpu(rn->in.nid[i]);
			if (child_nid == 0) {
				child_nofs += NIDS_PER_BLOCK + 1;
				continue;
			}
			rdn.nid = child_nid;
			ret = truncate_nodes(&rdn, child_nofs, 0, depth - 1);
			if (ret == (NIDS_PER_BLOCK + 1)) {
				if (set_nid(folio, i, 0, false))
					dn->node_changed = true;
				child_nofs += ret;
			} else if (ret < 0 && ret != -ENOENT) {
				goto out_err;
			}
		}
		freed = child_nofs;
	}

	if (!ofs) {
		/* remove current indirect node */
		dn->node_folio = folio;
		ret = truncate_node(dn);
		if (ret)
			goto out_err;
		freed++;
	} else {
		f2fs_folio_put(folio, true);
	}
	trace_f2fs_truncate_nodes_exit(dn->inode, freed);
	return freed;

out_err:
	f2fs_folio_put(folio, true);
	trace_f2fs_truncate_nodes_exit(dn->inode, ret);
	return ret;
}

static int truncate_partial_nodes(struct dnode_of_data *dn,
			struct f2fs_inode *ri, int *offset, int depth)
{
	struct folio *folios[2];
	nid_t nid[3];
	nid_t child_nid;
	int err = 0;
	int i;
	int idx = depth - 2;

	nid[0] = get_nid(&dn->inode_folio->page, offset[0], true);
	if (!nid[0])
		return 0;

	/* get indirect nodes in the path */
	for (i = 0; i < idx + 1; i++) {
		/* reference count'll be increased */
		folios[i] = f2fs_get_node_folio(F2FS_I_SB(dn->inode), nid[i]);
		if (IS_ERR(folios[i])) {
			err = PTR_ERR(folios[i]);
			idx = i - 1;
			goto fail;
		}
		nid[i + 1] = get_nid(&folios[i]->page, offset[i + 1], false);
	}

	f2fs_ra_node_pages(folios[idx], offset[idx + 1], NIDS_PER_BLOCK);

	/* free direct nodes linked to a partial indirect node */
	for (i = offset[idx + 1]; i < NIDS_PER_BLOCK; i++) {
		child_nid = get_nid(&folios[idx]->page, i, false);
		if (!child_nid)
			continue;
		dn->nid = child_nid;
		err = truncate_dnode(dn);
		if (err < 0)
			goto fail;
		if (set_nid(folios[idx], i, 0, false))
			dn->node_changed = true;
	}

	if (offset[idx + 1] == 0) {
		dn->node_folio = folios[idx];
		dn->nid = nid[idx];
		err = truncate_node(dn);
		if (err)
			goto fail;
	} else {
		f2fs_folio_put(folios[idx], true);
	}
	offset[idx]++;
	offset[idx + 1] = 0;
	idx--;
fail:
	for (i = idx; i >= 0; i--)
		f2fs_folio_put(folios[i], true);

	trace_f2fs_truncate_partial_nodes(dn->inode, nid, depth, err);

	return err;
}

/*
 * All the block addresses of data and nodes should be nullified.
 */
int f2fs_truncate_inode_blocks(struct inode *inode, pgoff_t from)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int err = 0, cont = 1;
	int level, offset[4], noffset[4];
	unsigned int nofs = 0;
	struct f2fs_inode *ri;
	struct dnode_of_data dn;
	struct folio *folio;

	trace_f2fs_truncate_inode_blocks_enter(inode, from);

	level = get_node_path(inode, from, offset, noffset);
	if (level <= 0) {
		if (!level) {
			level = -EFSCORRUPTED;
			f2fs_err(sbi, "%s: inode ino=%lx has corrupted node block, from:%lu addrs:%u",
					__func__, inode->i_ino,
					from, ADDRS_PER_INODE(inode));
			set_sbi_flag(sbi, SBI_NEED_FSCK);
		}
		trace_f2fs_truncate_inode_blocks_exit(inode, level);
		return level;
	}

	folio = f2fs_get_inode_folio(sbi, inode->i_ino);
	if (IS_ERR(folio)) {
		trace_f2fs_truncate_inode_blocks_exit(inode, PTR_ERR(folio));
		return PTR_ERR(folio);
	}

	set_new_dnode(&dn, inode, folio, NULL, 0);
	folio_unlock(folio);

	ri = F2FS_INODE(&folio->page);
	switch (level) {
	case 0:
	case 1:
		nofs = noffset[1];
		break;
	case 2:
		nofs = noffset[1];
		if (!offset[level - 1])
			goto skip_partial;
		err = truncate_partial_nodes(&dn, ri, offset, level);
		if (err < 0 && err != -ENOENT)
			goto fail;
		nofs += 1 + NIDS_PER_BLOCK;
		break;
	case 3:
		nofs = 5 + 2 * NIDS_PER_BLOCK;
		if (!offset[level - 1])
			goto skip_partial;
		err = truncate_partial_nodes(&dn, ri, offset, level);
		if (err < 0 && err != -ENOENT)
			goto fail;
		break;
	default:
		BUG();
	}

skip_partial:
	while (cont) {
		dn.nid = get_nid(&folio->page, offset[0], true);
		switch (offset[0]) {
		case NODE_DIR1_BLOCK:
		case NODE_DIR2_BLOCK:
			err = truncate_dnode(&dn);
			break;

		case NODE_IND1_BLOCK:
		case NODE_IND2_BLOCK:
			err = truncate_nodes(&dn, nofs, offset[1], 2);
			break;

		case NODE_DIND_BLOCK:
			err = truncate_nodes(&dn, nofs, offset[1], 3);
			cont = 0;
			break;

		default:
			BUG();
		}
		if (err == -ENOENT) {
			set_sbi_flag(F2FS_F_SB(folio), SBI_NEED_FSCK);
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			f2fs_err_ratelimited(sbi,
				"truncate node fail, ino:%lu, nid:%u, "
				"offset[0]:%d, offset[1]:%d, nofs:%d",
				inode->i_ino, dn.nid, offset[0],
				offset[1], nofs);
			err = 0;
		}
		if (err < 0)
			goto fail;
		if (offset[1] == 0 && get_nid(&folio->page, offset[0], true)) {
			folio_lock(folio);
			BUG_ON(!is_node_folio(folio));
			set_nid(folio, offset[0], 0, true);
			folio_unlock(folio);
		}
		offset[1] = 0;
		offset[0]++;
		nofs += err;
	}
fail:
	f2fs_folio_put(folio, false);
	trace_f2fs_truncate_inode_blocks_exit(inode, err);
	return err > 0 ? 0 : err;
}

/* caller must lock inode page */
int f2fs_truncate_xattr_node(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t nid = F2FS_I(inode)->i_xattr_nid;
	struct dnode_of_data dn;
	struct folio *nfolio;
	int err;

	if (!nid)
		return 0;

	nfolio = f2fs_get_xnode_folio(sbi, nid);
	if (IS_ERR(nfolio))
		return PTR_ERR(nfolio);

	set_new_dnode(&dn, inode, NULL, nfolio, nid);
	err = truncate_node(&dn);
	if (err) {
		f2fs_folio_put(nfolio, true);
		return err;
	}

	f2fs_i_xnid_write(inode, 0);

	return 0;
}

/*
 * Caller should grab and release a rwsem by calling f2fs_lock_op() and
 * f2fs_unlock_op().
 */
int f2fs_remove_inode_page(struct inode *inode)
{
	struct dnode_of_data dn;
	int err;

	set_new_dnode(&dn, inode, NULL, NULL, inode->i_ino);
	err = f2fs_get_dnode_of_data(&dn, 0, LOOKUP_NODE);
	if (err)
		return err;

	err = f2fs_truncate_xattr_node(inode);
	if (err) {
		f2fs_put_dnode(&dn);
		return err;
	}

	/* remove potential inline_data blocks */
	if (!IS_DEVICE_ALIASING(inode) &&
	    (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	     S_ISLNK(inode->i_mode)))
		f2fs_truncate_data_blocks_range(&dn, 1);

	/* 0 is possible, after f2fs_new_inode() has failed */
	if (unlikely(f2fs_cp_error(F2FS_I_SB(inode)))) {
		f2fs_put_dnode(&dn);
		return -EIO;
	}

	if (unlikely(inode->i_blocks != 0 && inode->i_blocks != 8)) {
		f2fs_warn(F2FS_I_SB(inode),
			"f2fs_remove_inode_page: inconsistent i_blocks, ino:%lu, iblocks:%llu",
			inode->i_ino, (unsigned long long)inode->i_blocks);
		set_sbi_flag(F2FS_I_SB(inode), SBI_NEED_FSCK);
	}

	/* will put inode & node pages */
	err = truncate_node(&dn);
	if (err) {
		f2fs_put_dnode(&dn);
		return err;
	}
	return 0;
}

struct folio *f2fs_new_inode_folio(struct inode *inode)
{
	struct dnode_of_data dn;

	/* allocate inode page for new inode */
	set_new_dnode(&dn, inode, NULL, NULL, inode->i_ino);

	/* caller should f2fs_folio_put(folio, true); */
	return f2fs_new_node_folio(&dn, 0);
}

struct folio *f2fs_new_node_folio(struct dnode_of_data *dn, unsigned int ofs)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct node_info new_ni;
	struct folio *folio;
	int err;

	if (unlikely(is_inode_flag_set(dn->inode, FI_NO_ALLOC)))
		return ERR_PTR(-EPERM);

	folio = f2fs_grab_cache_folio(NODE_MAPPING(sbi), dn->nid, false);
	if (IS_ERR(folio))
		return folio;

	if (unlikely((err = inc_valid_node_count(sbi, dn->inode, !ofs))))
		goto fail;

#ifdef CONFIG_F2FS_CHECK_FS
	err = f2fs_get_node_info(sbi, dn->nid, &new_ni, false);
	if (err) {
		dec_valid_node_count(sbi, dn->inode, !ofs);
		goto fail;
	}
	if (unlikely(new_ni.blk_addr != NULL_ADDR)) {
		err = -EFSCORRUPTED;
		dec_valid_node_count(sbi, dn->inode, !ofs);
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn_ratelimited(sbi,
			"f2fs_new_node_folio: inconsistent nat entry, "
			"ino:%u, nid:%u, blkaddr:%u, ver:%u, flag:%u",
			new_ni.ino, new_ni.nid, new_ni.blk_addr,
			new_ni.version, new_ni.flag);
		f2fs_handle_error(sbi, ERROR_INCONSISTENT_NAT);
		goto fail;
	}
#endif
	new_ni.nid = dn->nid;
	new_ni.ino = dn->inode->i_ino;
	new_ni.blk_addr = NULL_ADDR;
	new_ni.flag = 0;
	new_ni.version = 0;
	set_node_addr(sbi, &new_ni, NEW_ADDR, false);

	f2fs_folio_wait_writeback(folio, NODE, true, true);
	fill_node_footer(&folio->page, dn->nid, dn->inode->i_ino, ofs, true);
	set_cold_node(&folio->page, S_ISDIR(dn->inode->i_mode));
	if (!folio_test_uptodate(folio))
		folio_mark_uptodate(folio);
	if (folio_mark_dirty(folio))
		dn->node_changed = true;

	if (f2fs_has_xattr_block(ofs))
		f2fs_i_xnid_write(dn->inode, dn->nid);

	if (ofs == 0)
		inc_valid_inode_count(sbi);
	return folio;
fail:
	clear_node_folio_dirty(folio);
	f2fs_folio_put(folio, true);
	return ERR_PTR(err);
}

/*
 * Caller should do after getting the following values.
 * 0: f2fs_folio_put(folio, false)
 * LOCKED_PAGE or error: f2fs_folio_put(folio, true)
 */
static int read_node_folio(struct folio *folio, blk_opf_t op_flags)
{
	struct f2fs_sb_info *sbi = F2FS_F_SB(folio);
	struct node_info ni;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.type = NODE,
		.op = REQ_OP_READ,
		.op_flags = op_flags,
		.page = &folio->page,
		.encrypted_page = NULL,
	};
	int err;

	if (folio_test_uptodate(folio)) {
		if (!f2fs_inode_chksum_verify(sbi, folio)) {
			folio_clear_uptodate(folio);
			return -EFSBADCRC;
		}
		return LOCKED_PAGE;
	}

	err = f2fs_get_node_info(sbi, folio->index, &ni, false);
	if (err)
		return err;

	/* NEW_ADDR can be seen, after cp_error drops some dirty node pages */
	if (unlikely(ni.blk_addr == NULL_ADDR || ni.blk_addr == NEW_ADDR)) {
		folio_clear_uptodate(folio);
		return -ENOENT;
	}

	fio.new_blkaddr = fio.old_blkaddr = ni.blk_addr;

	err = f2fs_submit_page_bio(&fio);

	if (!err)
		f2fs_update_iostat(sbi, NULL, FS_NODE_READ_IO, F2FS_BLKSIZE);

	return err;
}

/*
 * Readahead a node page
 */
void f2fs_ra_node_page(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct folio *afolio;
	int err;

	if (!nid)
		return;
	if (f2fs_check_nid_range(sbi, nid))
		return;

	afolio = xa_load(&NODE_MAPPING(sbi)->i_pages, nid);
	if (afolio)
		return;

	afolio = f2fs_grab_cache_folio(NODE_MAPPING(sbi), nid, false);
	if (IS_ERR(afolio))
		return;

	err = read_node_folio(afolio, REQ_RAHEAD);
	f2fs_folio_put(afolio, err ? true : false);
}

static int sanity_check_node_footer(struct f2fs_sb_info *sbi,
					struct folio *folio, pgoff_t nid,
					enum node_type ntype)
{
	struct page *page = &folio->page;

	if (unlikely(nid != nid_of_node(page) ||
		(ntype == NODE_TYPE_INODE && !IS_INODE(page)) ||
		(ntype == NODE_TYPE_XATTR &&
		!f2fs_has_xattr_block(ofs_of_node(page))) ||
		time_to_inject(sbi, FAULT_INCONSISTENT_FOOTER))) {
		f2fs_warn(sbi, "inconsistent node block, node_type:%d, nid:%lu, "
			  "node_footer[nid:%u,ino:%u,ofs:%u,cpver:%llu,blkaddr:%u]",
			  ntype, nid, nid_of_node(page), ino_of_node(page),
			  ofs_of_node(page), cpver_of_node(page),
			  next_blkaddr_of_node(folio));
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_handle_error(sbi, ERROR_INCONSISTENT_FOOTER);
		return -EFSCORRUPTED;
	}
	return 0;
}

static struct folio *__get_node_folio(struct f2fs_sb_info *sbi, pgoff_t nid,
		struct folio *parent, int start, enum node_type ntype)
{
	struct folio *folio;
	int err;

	if (!nid)
		return ERR_PTR(-ENOENT);
	if (f2fs_check_nid_range(sbi, nid))
		return ERR_PTR(-EINVAL);
repeat:
	folio = f2fs_grab_cache_folio(NODE_MAPPING(sbi), nid, false);
	if (IS_ERR(folio))
		return folio;

	err = read_node_folio(folio, 0);
	if (err < 0)
		goto out_put_err;
	if (err == LOCKED_PAGE)
		goto page_hit;

	if (parent)
		f2fs_ra_node_pages(parent, start + 1, MAX_RA_NODE);

	folio_lock(folio);

	if (unlikely(!is_node_folio(folio))) {
		f2fs_folio_put(folio, true);
		goto repeat;
	}

	if (unlikely(!folio_test_uptodate(folio))) {
		err = -EIO;
		goto out_err;
	}

	if (!f2fs_inode_chksum_verify(sbi, folio)) {
		err = -EFSBADCRC;
		goto out_err;
	}
page_hit:
	err = sanity_check_node_footer(sbi, folio, nid, ntype);
	if (!err)
		return folio;
out_err:
	folio_clear_uptodate(folio);
out_put_err:
	/* ENOENT comes from read_node_folio which is not an error. */
	if (err != -ENOENT)
		f2fs_handle_page_eio(sbi, folio, NODE);
	f2fs_folio_put(folio, true);
	return ERR_PTR(err);
}

struct folio *f2fs_get_node_folio(struct f2fs_sb_info *sbi, pgoff_t nid)
{
	return __get_node_folio(sbi, nid, NULL, 0, NODE_TYPE_REGULAR);
}

struct folio *f2fs_get_inode_folio(struct f2fs_sb_info *sbi, pgoff_t ino)
{
	return __get_node_folio(sbi, ino, NULL, 0, NODE_TYPE_INODE);
}

struct folio *f2fs_get_xnode_folio(struct f2fs_sb_info *sbi, pgoff_t xnid)
{
	return __get_node_folio(sbi, xnid, NULL, 0, NODE_TYPE_XATTR);
}

static struct folio *f2fs_get_node_folio_ra(struct folio *parent, int start)
{
	struct f2fs_sb_info *sbi = F2FS_F_SB(parent);
	nid_t nid = get_nid(&parent->page, start, false);

	return __get_node_folio(sbi, nid, parent, start, NODE_TYPE_REGULAR);
}

static void flush_inline_data(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct inode *inode;
	struct folio *folio;
	int ret;

	/* should flush inline_data before evict_inode */
	inode = ilookup(sbi->sb, ino);
	if (!inode)
		return;

	folio = f2fs_filemap_get_folio(inode->i_mapping, 0,
					FGP_LOCK|FGP_NOWAIT, 0);
	if (IS_ERR(folio))
		goto iput_out;

	if (!folio_test_uptodate(folio))
		goto folio_out;

	if (!folio_test_dirty(folio))
		goto folio_out;

	if (!folio_clear_dirty_for_io(folio))
		goto folio_out;

	ret = f2fs_write_inline_data(inode, folio);
	inode_dec_dirty_pages(inode);
	f2fs_remove_dirty_inode(inode);
	if (ret)
		folio_mark_dirty(folio);
folio_out:
	f2fs_folio_put(folio, true);
iput_out:
	iput(inode);
}

static struct folio *last_fsync_dnode(struct f2fs_sb_info *sbi, nid_t ino)
{
	pgoff_t index;
	struct folio_batch fbatch;
	struct folio *last_folio = NULL;
	int nr_folios;

	folio_batch_init(&fbatch);
	index = 0;

	while ((nr_folios = filemap_get_folios_tag(NODE_MAPPING(sbi), &index,
					(pgoff_t)-1, PAGECACHE_TAG_DIRTY,
					&fbatch))) {
		int i;

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			if (unlikely(f2fs_cp_error(sbi))) {
				f2fs_folio_put(last_folio, false);
				folio_batch_release(&fbatch);
				return ERR_PTR(-EIO);
			}

			if (!IS_DNODE(&folio->page) || !is_cold_node(&folio->page))
				continue;
			if (ino_of_node(&folio->page) != ino)
				continue;

			folio_lock(folio);

			if (unlikely(!is_node_folio(folio))) {
continue_unlock:
				folio_unlock(folio);
				continue;
			}
			if (ino_of_node(&folio->page) != ino)
				goto continue_unlock;

			if (!folio_test_dirty(folio)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			if (last_folio)
				f2fs_folio_put(last_folio, false);

			folio_get(folio);
			last_folio = folio;
			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
	return last_folio;
}

static bool __write_node_folio(struct folio *folio, bool atomic, bool *submitted,
				struct writeback_control *wbc, bool do_balance,
				enum iostat_type io_type, unsigned int *seq_id)
{
	struct f2fs_sb_info *sbi = F2FS_F_SB(folio);
	nid_t nid;
	struct node_info ni;
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.ino = ino_of_node(&folio->page),
		.type = NODE,
		.op = REQ_OP_WRITE,
		.op_flags = wbc_to_write_flags(wbc),
		.page = &folio->page,
		.encrypted_page = NULL,
		.submitted = 0,
		.io_type = io_type,
		.io_wbc = wbc,
	};
	unsigned int seq;

	trace_f2fs_writepage(folio, NODE);

	if (unlikely(f2fs_cp_error(sbi))) {
		/* keep node pages in remount-ro mode */
		if (F2FS_OPTION(sbi).errors == MOUNT_ERRORS_READONLY)
			goto redirty_out;
		folio_clear_uptodate(folio);
		dec_page_count(sbi, F2FS_DIRTY_NODES);
		folio_unlock(folio);
		return true;
	}

	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto redirty_out;

	if (!is_sbi_flag_set(sbi, SBI_CP_DISABLED) &&
			wbc->sync_mode == WB_SYNC_NONE &&
			IS_DNODE(&folio->page) && is_cold_node(&folio->page))
		goto redirty_out;

	/* get old block addr of this node page */
	nid = nid_of_node(&folio->page);
	f2fs_bug_on(sbi, folio->index != nid);

	if (f2fs_get_node_info(sbi, nid, &ni, !do_balance))
		goto redirty_out;

	f2fs_down_read(&sbi->node_write);

	/* This page is already truncated */
	if (unlikely(ni.blk_addr == NULL_ADDR)) {
		folio_clear_uptodate(folio);
		dec_page_count(sbi, F2FS_DIRTY_NODES);
		f2fs_up_read(&sbi->node_write);
		folio_unlock(folio);
		return true;
	}

	if (__is_valid_data_blkaddr(ni.blk_addr) &&
		!f2fs_is_valid_blkaddr(sbi, ni.blk_addr,
					DATA_GENERIC_ENHANCE)) {
		f2fs_up_read(&sbi->node_write);
		goto redirty_out;
	}

	if (atomic && !test_opt(sbi, NOBARRIER))
		fio.op_flags |= REQ_PREFLUSH | REQ_FUA;

	/* should add to global list before clearing PAGECACHE status */
	if (f2fs_in_warm_node_list(sbi, folio)) {
		seq = f2fs_add_fsync_node_entry(sbi, folio);
		if (seq_id)
			*seq_id = seq;
	}

	folio_start_writeback(folio);

	fio.old_blkaddr = ni.blk_addr;
	f2fs_do_write_node_page(nid, &fio);
	set_node_addr(sbi, &ni, fio.new_blkaddr, is_fsync_dnode(&folio->page));
	dec_page_count(sbi, F2FS_DIRTY_NODES);
	f2fs_up_read(&sbi->node_write);

	folio_unlock(folio);

	if (unlikely(f2fs_cp_error(sbi))) {
		f2fs_submit_merged_write(sbi, NODE);
		submitted = NULL;
	}
	if (submitted)
		*submitted = fio.submitted;

	if (do_balance)
		f2fs_balance_fs(sbi, false);
	return true;

redirty_out:
	folio_redirty_for_writepage(wbc, folio);
	folio_unlock(folio);
	return false;
}

int f2fs_move_node_folio(struct folio *node_folio, int gc_type)
{
	int err = 0;

	if (gc_type == FG_GC) {
		struct writeback_control wbc = {
			.sync_mode = WB_SYNC_ALL,
			.nr_to_write = 1,
		};

		f2fs_folio_wait_writeback(node_folio, NODE, true, true);

		folio_mark_dirty(node_folio);

		if (!folio_clear_dirty_for_io(node_folio)) {
			err = -EAGAIN;
			goto out_page;
		}

		if (!__write_node_folio(node_folio, false, NULL,
					&wbc, false, FS_GC_NODE_IO, NULL))
			err = -EAGAIN;
		goto release_page;
	} else {
		/* set page dirty and write it */
		if (!folio_test_writeback(node_folio))
			folio_mark_dirty(node_folio);
	}
out_page:
	folio_unlock(node_folio);
release_page:
	f2fs_folio_put(node_folio, false);
	return err;
}

int f2fs_fsync_node_pages(struct f2fs_sb_info *sbi, struct inode *inode,
			struct writeback_control *wbc, bool atomic,
			unsigned int *seq_id)
{
	pgoff_t index;
	struct folio_batch fbatch;
	int ret = 0;
	struct folio *last_folio = NULL;
	bool marked = false;
	nid_t ino = inode->i_ino;
	int nr_folios;
	int nwritten = 0;

	if (atomic) {
		last_folio = last_fsync_dnode(sbi, ino);
		if (IS_ERR_OR_NULL(last_folio))
			return PTR_ERR_OR_ZERO(last_folio);
	}
retry:
	folio_batch_init(&fbatch);
	index = 0;

	while ((nr_folios = filemap_get_folios_tag(NODE_MAPPING(sbi), &index,
					(pgoff_t)-1, PAGECACHE_TAG_DIRTY,
					&fbatch))) {
		int i;

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];
			bool submitted = false;

			if (unlikely(f2fs_cp_error(sbi))) {
				f2fs_folio_put(last_folio, false);
				folio_batch_release(&fbatch);
				ret = -EIO;
				goto out;
			}

			if (!IS_DNODE(&folio->page) || !is_cold_node(&folio->page))
				continue;
			if (ino_of_node(&folio->page) != ino)
				continue;

			folio_lock(folio);

			if (unlikely(!is_node_folio(folio))) {
continue_unlock:
				folio_unlock(folio);
				continue;
			}
			if (ino_of_node(&folio->page) != ino)
				goto continue_unlock;

			if (!folio_test_dirty(folio) && folio != last_folio) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			f2fs_folio_wait_writeback(folio, NODE, true, true);

			set_fsync_mark(&folio->page, 0);
			set_dentry_mark(&folio->page, 0);

			if (!atomic || folio == last_folio) {
				set_fsync_mark(&folio->page, 1);
				percpu_counter_inc(&sbi->rf_node_block_count);
				if (IS_INODE(&folio->page)) {
					if (is_inode_flag_set(inode,
								FI_DIRTY_INODE))
						f2fs_update_inode(inode, folio);
					set_dentry_mark(&folio->page,
						f2fs_need_dentry_mark(sbi, ino));
				}
				/* may be written by other thread */
				if (!folio_test_dirty(folio))
					folio_mark_dirty(folio);
			}

			if (!folio_clear_dirty_for_io(folio))
				goto continue_unlock;

			if (!__write_node_folio(folio, atomic &&
						folio == last_folio,
						&submitted, wbc, true,
						FS_NODE_IO, seq_id)) {
				f2fs_folio_put(last_folio, false);
				folio_batch_release(&fbatch);
				ret = -EIO;
				goto out;
			}
			if (submitted)
				nwritten++;

			if (folio == last_folio) {
				f2fs_folio_put(folio, false);
				folio_batch_release(&fbatch);
				marked = true;
				goto out;
			}
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
	if (atomic && !marked) {
		f2fs_debug(sbi, "Retry to write fsync mark: ino=%u, idx=%lx",
			   ino, last_folio->index);
		folio_lock(last_folio);
		f2fs_folio_wait_writeback(last_folio, NODE, true, true);
		folio_mark_dirty(last_folio);
		folio_unlock(last_folio);
		goto retry;
	}
out:
	if (nwritten)
		f2fs_submit_merged_write_cond(sbi, NULL, NULL, ino, NODE);
	return ret;
}

static int f2fs_match_ino(struct inode *inode, unsigned long ino, void *data)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	bool clean;

	if (inode->i_ino != ino)
		return 0;

	if (!is_inode_flag_set(inode, FI_DIRTY_INODE))
		return 0;

	spin_lock(&sbi->inode_lock[DIRTY_META]);
	clean = list_empty(&F2FS_I(inode)->gdirty_list);
	spin_unlock(&sbi->inode_lock[DIRTY_META]);

	if (clean)
		return 0;

	inode = igrab(inode);
	if (!inode)
		return 0;
	return 1;
}

static bool flush_dirty_inode(struct folio *folio)
{
	struct f2fs_sb_info *sbi = F2FS_F_SB(folio);
	struct inode *inode;
	nid_t ino = ino_of_node(&folio->page);

	inode = find_inode_nowait(sbi->sb, ino, f2fs_match_ino, NULL);
	if (!inode)
		return false;

	f2fs_update_inode(inode, folio);
	folio_unlock(folio);

	iput(inode);
	return true;
}

void f2fs_flush_inline_data(struct f2fs_sb_info *sbi)
{
	pgoff_t index = 0;
	struct folio_batch fbatch;
	int nr_folios;

	folio_batch_init(&fbatch);

	while ((nr_folios = filemap_get_folios_tag(NODE_MAPPING(sbi), &index,
					(pgoff_t)-1, PAGECACHE_TAG_DIRTY,
					&fbatch))) {
		int i;

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];

			if (!IS_INODE(&folio->page))
				continue;

			folio_lock(folio);

			if (unlikely(!is_node_folio(folio)))
				goto unlock;
			if (!folio_test_dirty(folio))
				goto unlock;

			/* flush inline_data, if it's async context. */
			if (page_private_inline(&folio->page)) {
				clear_page_private_inline(&folio->page);
				folio_unlock(folio);
				flush_inline_data(sbi, ino_of_node(&folio->page));
				continue;
			}
unlock:
			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

int f2fs_sync_node_pages(struct f2fs_sb_info *sbi,
				struct writeback_control *wbc,
				bool do_balance, enum iostat_type io_type)
{
	pgoff_t index;
	struct folio_batch fbatch;
	int step = 0;
	int nwritten = 0;
	int ret = 0;
	int nr_folios, done = 0;

	folio_batch_init(&fbatch);

next_step:
	index = 0;

	while (!done && (nr_folios = filemap_get_folios_tag(NODE_MAPPING(sbi),
				&index, (pgoff_t)-1, PAGECACHE_TAG_DIRTY,
				&fbatch))) {
		int i;

		for (i = 0; i < nr_folios; i++) {
			struct folio *folio = fbatch.folios[i];
			bool submitted = false;

			/* give a priority to WB_SYNC threads */
			if (atomic_read(&sbi->wb_sync_req[NODE]) &&
					wbc->sync_mode == WB_SYNC_NONE) {
				done = 1;
				break;
			}

			/*
			 * flushing sequence with step:
			 * 0. indirect nodes
			 * 1. dentry dnodes
			 * 2. file dnodes
			 */
			if (step == 0 && IS_DNODE(&folio->page))
				continue;
			if (step == 1 && (!IS_DNODE(&folio->page) ||
						is_cold_node(&folio->page)))
				continue;
			if (step == 2 && (!IS_DNODE(&folio->page) ||
						!is_cold_node(&folio->page)))
				continue;
lock_node:
			if (wbc->sync_mode == WB_SYNC_ALL)
				folio_lock(folio);
			else if (!folio_trylock(folio))
				continue;

			if (unlikely(!is_node_folio(folio))) {
continue_unlock:
				folio_unlock(folio);
				continue;
			}

			if (!folio_test_dirty(folio)) {
				/* someone wrote it for us */
				goto continue_unlock;
			}

			/* flush inline_data/inode, if it's async context. */
			if (!do_balance)
				goto write_node;

			/* flush inline_data */
			if (page_private_inline(&folio->page)) {
				clear_page_private_inline(&folio->page);
				folio_unlock(folio);
				flush_inline_data(sbi, ino_of_node(&folio->page));
				goto lock_node;
			}

			/* flush dirty inode */
			if (IS_INODE(&folio->page) && flush_dirty_inode(folio))
				goto lock_node;
write_node:
			f2fs_folio_wait_writeback(folio, NODE, true, true);

			if (!folio_clear_dirty_for_io(folio))
				goto continue_unlock;

			set_fsync_mark(&folio->page, 0);
			set_dentry_mark(&folio->page, 0);

			if (!__write_node_folio(folio, false, &submitted,
					wbc, do_balance, io_type, NULL)) {
				folio_batch_release(&fbatch);
				ret = -EIO;
				goto out;
			}
			if (submitted)
				nwritten++;

			if (--wbc->nr_to_write == 0)
				break;
		}
		folio_batch_release(&fbatch);
		cond_resched();

		if (wbc->nr_to_write == 0) {
			step = 2;
			break;
		}
	}

	if (step < 2) {
		if (!is_sbi_flag_set(sbi, SBI_CP_DISABLED) &&
				wbc->sync_mode == WB_SYNC_NONE && step == 1)
			goto out;
		step++;
		goto next_step;
	}
out:
	if (nwritten)
		f2fs_submit_merged_write(sbi, NODE);

	if (unlikely(f2fs_cp_error(sbi)))
		return -EIO;
	return ret;
}

int f2fs_wait_on_node_pages_writeback(struct f2fs_sb_info *sbi,
						unsigned int seq_id)
{
	struct fsync_node_entry *fn;
	struct list_head *head = &sbi->fsync_node_list;
	unsigned long flags;
	unsigned int cur_seq_id = 0;

	while (seq_id && cur_seq_id < seq_id) {
		struct folio *folio;

		spin_lock_irqsave(&sbi->fsync_node_lock, flags);
		if (list_empty(head)) {
			spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
			break;
		}
		fn = list_first_entry(head, struct fsync_node_entry, list);
		if (fn->seq_id > seq_id) {
			spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);
			break;
		}
		cur_seq_id = fn->seq_id;
		folio = fn->folio;
		folio_get(folio);
		spin_unlock_irqrestore(&sbi->fsync_node_lock, flags);

		f2fs_folio_wait_writeback(folio, NODE, true, false);

		folio_put(folio);
	}

	return filemap_check_errors(NODE_MAPPING(sbi));
}

static int f2fs_write_node_pages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct f2fs_sb_info *sbi = F2FS_M_SB(mapping);
	struct blk_plug plug;
	long diff;

	if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
		goto skip_write;

	/* balancing f2fs's metadata in background */
	f2fs_balance_fs_bg(sbi, true);

	/* collect a number of dirty node pages and write together */
	if (wbc->sync_mode != WB_SYNC_ALL &&
			get_pages(sbi, F2FS_DIRTY_NODES) <
					nr_pages_to_skip(sbi, NODE))
		goto skip_write;

	if (wbc->sync_mode == WB_SYNC_ALL)
		atomic_inc(&sbi->wb_sync_req[NODE]);
	else if (atomic_read(&sbi->wb_sync_req[NODE])) {
		/* to avoid potential deadlock */
		if (current->plug)
			blk_finish_plug(current->plug);
		goto skip_write;
	}

	trace_f2fs_writepages(mapping->host, wbc, NODE);

	diff = nr_pages_to_write(sbi, NODE, wbc);
	blk_start_plug(&plug);
	f2fs_sync_node_pages(sbi, wbc, true, FS_NODE_IO);
	blk_finish_plug(&plug);
	wbc->nr_to_write = max((long)0, wbc->nr_to_write - diff);

	if (wbc->sync_mode == WB_SYNC_ALL)
		atomic_dec(&sbi->wb_sync_req[NODE]);
	return 0;

skip_write:
	wbc->pages_skipped += get_pages(sbi, F2FS_DIRTY_NODES);
	trace_f2fs_writepages(mapping->host, wbc, NODE);
	return 0;
}

static bool f2fs_dirty_node_folio(struct address_space *mapping,
		struct folio *folio)
{
	trace_f2fs_set_page_dirty(folio, NODE);

	if (!folio_test_uptodate(folio))
		folio_mark_uptodate(folio);
#ifdef CONFIG_F2FS_CHECK_FS
	if (IS_INODE(&folio->page))
		f2fs_inode_chksum_set(F2FS_M_SB(mapping), &folio->page);
#endif
	if (filemap_dirty_folio(mapping, folio)) {
		inc_page_count(F2FS_M_SB(mapping), F2FS_DIRTY_NODES);
		set_page_private_reference(&folio->page);
		return true;
	}
	return false;
}

/*
 * Structure of the f2fs node operations
 */
const struct address_space_operations f2fs_node_aops = {
	.writepages	= f2fs_write_node_pages,
	.dirty_folio	= f2fs_dirty_node_folio,
	.invalidate_folio = f2fs_invalidate_folio,
	.release_folio	= f2fs_release_folio,
	.migrate_folio	= filemap_migrate_folio,
};

static struct free_nid *__lookup_free_nid_list(struct f2fs_nm_info *nm_i,
						nid_t n)
{
	return radix_tree_lookup(&nm_i->free_nid_root, n);
}

static int __insert_free_nid(struct f2fs_sb_info *sbi,
				struct free_nid *i)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int err = radix_tree_insert(&nm_i->free_nid_root, i->nid, i);

	if (err)
		return err;

	nm_i->nid_cnt[FREE_NID]++;
	list_add_tail(&i->list, &nm_i->free_nid_list);
	return 0;
}

static void __remove_free_nid(struct f2fs_sb_info *sbi,
			struct free_nid *i, enum nid_state state)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	f2fs_bug_on(sbi, state != i->state);
	nm_i->nid_cnt[state]--;
	if (state == FREE_NID)
		list_del(&i->list);
	radix_tree_delete(&nm_i->free_nid_root, i->nid);
}

static void __move_free_nid(struct f2fs_sb_info *sbi, struct free_nid *i,
			enum nid_state org_state, enum nid_state dst_state)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	f2fs_bug_on(sbi, org_state != i->state);
	i->state = dst_state;
	nm_i->nid_cnt[org_state]--;
	nm_i->nid_cnt[dst_state]++;

	switch (dst_state) {
	case PREALLOC_NID:
		list_del(&i->list);
		break;
	case FREE_NID:
		list_add_tail(&i->list, &nm_i->free_nid_list);
		break;
	default:
		BUG_ON(1);
	}
}

static void update_free_nid_bitmap(struct f2fs_sb_info *sbi, nid_t nid,
							bool set, bool build)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int nat_ofs = NAT_BLOCK_OFFSET(nid);
	unsigned int nid_ofs = nid - START_NID(nid);

	if (!test_bit_le(nat_ofs, nm_i->nat_block_bitmap))
		return;

	if (set) {
		if (test_bit_le(nid_ofs, nm_i->free_nid_bitmap[nat_ofs]))
			return;
		__set_bit_le(nid_ofs, nm_i->free_nid_bitmap[nat_ofs]);
		nm_i->free_nid_count[nat_ofs]++;
	} else {
		if (!test_bit_le(nid_ofs, nm_i->free_nid_bitmap[nat_ofs]))
			return;
		__clear_bit_le(nid_ofs, nm_i->free_nid_bitmap[nat_ofs]);
		if (!build)
			nm_i->free_nid_count[nat_ofs]--;
	}
}

/* return if the nid is recognized as free */
static bool add_free_nid(struct f2fs_sb_info *sbi,
				nid_t nid, bool build, bool update)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i, *e;
	struct nat_entry *ne;
	int err;
	bool ret = false;

	/* 0 nid should not be used */
	if (unlikely(nid == 0))
		return false;

	if (unlikely(f2fs_check_nid_range(sbi, nid)))
		return false;

	i = f2fs_kmem_cache_alloc(free_nid_slab, GFP_NOFS, true, NULL);
	i->nid = nid;
	i->state = FREE_NID;

	err = radix_tree_preload(GFP_NOFS | __GFP_NOFAIL);
	f2fs_bug_on(sbi, err);

	err = -EINVAL;

	spin_lock(&nm_i->nid_list_lock);

	if (build) {
		/*
		 *   Thread A             Thread B
		 *  - f2fs_create
		 *   - f2fs_new_inode
		 *    - f2fs_alloc_nid
		 *     - __insert_nid_to_list(PREALLOC_NID)
		 *                     - f2fs_balance_fs_bg
		 *                      - f2fs_build_free_nids
		 *                       - __f2fs_build_free_nids
		 *                        - scan_nat_page
		 *                         - add_free_nid
		 *                          - __lookup_nat_cache
		 *  - f2fs_add_link
		 *   - f2fs_init_inode_metadata
		 *    - f2fs_new_inode_folio
		 *     - f2fs_new_node_folio
		 *      - set_node_addr
		 *  - f2fs_alloc_nid_done
		 *   - __remove_nid_from_list(PREALLOC_NID)
		 *                         - __insert_nid_to_list(FREE_NID)
		 */
		ne = __lookup_nat_cache(nm_i, nid);
		if (ne && (!get_nat_flag(ne, IS_CHECKPOINTED) ||
				nat_get_blkaddr(ne) != NULL_ADDR))
			goto err_out;

		e = __lookup_free_nid_list(nm_i, nid);
		if (e) {
			if (e->state == FREE_NID)
				ret = true;
			goto err_out;
		}
	}
	ret = true;
	err = __insert_free_nid(sbi, i);
err_out:
	if (update) {
		update_free_nid_bitmap(sbi, nid, ret, build);
		if (!build)
			nm_i->available_nids++;
	}
	spin_unlock(&nm_i->nid_list_lock);
	radix_tree_preload_end();

	if (err)
		kmem_cache_free(free_nid_slab, i);
	return ret;
}

static void remove_free_nid(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;
	bool need_free = false;

	spin_lock(&nm_i->nid_list_lock);
	i = __lookup_free_nid_list(nm_i, nid);
	if (i && i->state == FREE_NID) {
		__remove_free_nid(sbi, i, FREE_NID);
		need_free = true;
	}
	spin_unlock(&nm_i->nid_list_lock);

	if (need_free)
		kmem_cache_free(free_nid_slab, i);
}

static int scan_nat_page(struct f2fs_sb_info *sbi,
			struct f2fs_nat_block *nat_blk, nid_t start_nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	block_t blk_addr;
	unsigned int nat_ofs = NAT_BLOCK_OFFSET(start_nid);
	int i;

	__set_bit_le(nat_ofs, nm_i->nat_block_bitmap);

	i = start_nid % NAT_ENTRY_PER_BLOCK;

	for (; i < NAT_ENTRY_PER_BLOCK; i++, start_nid++) {
		if (unlikely(start_nid >= nm_i->max_nid))
			break;

		blk_addr = le32_to_cpu(nat_blk->entries[i].block_addr);

		if (blk_addr == NEW_ADDR)
			return -EFSCORRUPTED;

		if (blk_addr == NULL_ADDR) {
			add_free_nid(sbi, start_nid, true, true);
		} else {
			spin_lock(&NM_I(sbi)->nid_list_lock);
			update_free_nid_bitmap(sbi, start_nid, false, true);
			spin_unlock(&NM_I(sbi)->nid_list_lock);
		}
	}

	return 0;
}

static void scan_curseg_cache(struct f2fs_sb_info *sbi)
{
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_journal *journal = curseg->journal;
	int i;

	down_read(&curseg->journal_rwsem);
	for (i = 0; i < nats_in_cursum(journal); i++) {
		block_t addr;
		nid_t nid;

		addr = le32_to_cpu(nat_in_journal(journal, i).block_addr);
		nid = le32_to_cpu(nid_in_journal(journal, i));
		if (addr == NULL_ADDR)
			add_free_nid(sbi, nid, true, false);
		else
			remove_free_nid(sbi, nid);
	}
	up_read(&curseg->journal_rwsem);
}

static void scan_free_nid_bits(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int i, idx;
	nid_t nid;

	f2fs_down_read(&nm_i->nat_tree_lock);

	for (i = 0; i < nm_i->nat_blocks; i++) {
		if (!test_bit_le(i, nm_i->nat_block_bitmap))
			continue;
		if (!nm_i->free_nid_count[i])
			continue;
		for (idx = 0; idx < NAT_ENTRY_PER_BLOCK; idx++) {
			idx = find_next_bit_le(nm_i->free_nid_bitmap[i],
						NAT_ENTRY_PER_BLOCK, idx);
			if (idx >= NAT_ENTRY_PER_BLOCK)
				break;

			nid = i * NAT_ENTRY_PER_BLOCK + idx;
			add_free_nid(sbi, nid, true, false);

			if (nm_i->nid_cnt[FREE_NID] >= MAX_FREE_NIDS)
				goto out;
		}
	}
out:
	scan_curseg_cache(sbi);

	f2fs_up_read(&nm_i->nat_tree_lock);
}

static int __f2fs_build_free_nids(struct f2fs_sb_info *sbi,
						bool sync, bool mount)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int i = 0, ret;
	nid_t nid = nm_i->next_scan_nid;

	if (unlikely(nid >= nm_i->max_nid))
		nid = 0;

	if (unlikely(nid % NAT_ENTRY_PER_BLOCK))
		nid = NAT_BLOCK_OFFSET(nid) * NAT_ENTRY_PER_BLOCK;

	/* Enough entries */
	if (nm_i->nid_cnt[FREE_NID] >= NAT_ENTRY_PER_BLOCK)
		return 0;

	if (!sync && !f2fs_available_free_memory(sbi, FREE_NIDS))
		return 0;

	if (!mount) {
		/* try to find free nids in free_nid_bitmap */
		scan_free_nid_bits(sbi);

		if (nm_i->nid_cnt[FREE_NID] >= NAT_ENTRY_PER_BLOCK)
			return 0;
	}

	/* readahead nat pages to be scanned */
	f2fs_ra_meta_pages(sbi, NAT_BLOCK_OFFSET(nid), FREE_NID_PAGES,
							META_NAT, true);

	f2fs_down_read(&nm_i->nat_tree_lock);

	while (1) {
		if (!test_bit_le(NAT_BLOCK_OFFSET(nid),
						nm_i->nat_block_bitmap)) {
			struct folio *folio = get_current_nat_folio(sbi, nid);

			if (IS_ERR(folio)) {
				ret = PTR_ERR(folio);
			} else {
				ret = scan_nat_page(sbi, folio_address(folio),
						nid);
				f2fs_folio_put(folio, true);
			}

			if (ret) {
				f2fs_up_read(&nm_i->nat_tree_lock);

				if (ret == -EFSCORRUPTED) {
					f2fs_err(sbi, "NAT is corrupt, run fsck to fix it");
					set_sbi_flag(sbi, SBI_NEED_FSCK);
					f2fs_handle_error(sbi,
						ERROR_INCONSISTENT_NAT);
				}

				return ret;
			}
		}

		nid += (NAT_ENTRY_PER_BLOCK - (nid % NAT_ENTRY_PER_BLOCK));
		if (unlikely(nid >= nm_i->max_nid))
			nid = 0;

		if (++i >= FREE_NID_PAGES)
			break;
	}

	/* go to the next free nat pages to find free nids abundantly */
	nm_i->next_scan_nid = nid;

	/* find free nids from current sum_pages */
	scan_curseg_cache(sbi);

	f2fs_up_read(&nm_i->nat_tree_lock);

	f2fs_ra_meta_pages(sbi, NAT_BLOCK_OFFSET(nm_i->next_scan_nid),
					nm_i->ra_nid_pages, META_NAT, false);

	return 0;
}

int f2fs_build_free_nids(struct f2fs_sb_info *sbi, bool sync, bool mount)
{
	int ret;

	mutex_lock(&NM_I(sbi)->build_lock);
	ret = __f2fs_build_free_nids(sbi, sync, mount);
	mutex_unlock(&NM_I(sbi)->build_lock);

	return ret;
}

/*
 * If this function returns success, caller can obtain a new nid
 * from second parameter of this function.
 * The returned nid could be used ino as well as nid when inode is created.
 */
bool f2fs_alloc_nid(struct f2fs_sb_info *sbi, nid_t *nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i = NULL;
retry:
	if (time_to_inject(sbi, FAULT_ALLOC_NID))
		return false;

	spin_lock(&nm_i->nid_list_lock);

	if (unlikely(nm_i->available_nids == 0)) {
		spin_unlock(&nm_i->nid_list_lock);
		return false;
	}

	/* We should not use stale free nids created by f2fs_build_free_nids */
	if (nm_i->nid_cnt[FREE_NID] && !on_f2fs_build_free_nids(nm_i)) {
		f2fs_bug_on(sbi, list_empty(&nm_i->free_nid_list));
		i = list_first_entry(&nm_i->free_nid_list,
					struct free_nid, list);
		*nid = i->nid;

		__move_free_nid(sbi, i, FREE_NID, PREALLOC_NID);
		nm_i->available_nids--;

		update_free_nid_bitmap(sbi, *nid, false, false);

		spin_unlock(&nm_i->nid_list_lock);
		return true;
	}
	spin_unlock(&nm_i->nid_list_lock);

	/* Let's scan nat pages and its caches to get free nids */
	if (!f2fs_build_free_nids(sbi, true, false))
		goto retry;
	return false;
}

/*
 * f2fs_alloc_nid() should be called prior to this function.
 */
void f2fs_alloc_nid_done(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;

	spin_lock(&nm_i->nid_list_lock);
	i = __lookup_free_nid_list(nm_i, nid);
	f2fs_bug_on(sbi, !i);
	__remove_free_nid(sbi, i, PREALLOC_NID);
	spin_unlock(&nm_i->nid_list_lock);

	kmem_cache_free(free_nid_slab, i);
}

/*
 * f2fs_alloc_nid() should be called prior to this function.
 */
void f2fs_alloc_nid_failed(struct f2fs_sb_info *sbi, nid_t nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i;
	bool need_free = false;

	if (!nid)
		return;

	spin_lock(&nm_i->nid_list_lock);
	i = __lookup_free_nid_list(nm_i, nid);
	f2fs_bug_on(sbi, !i);

	if (!f2fs_available_free_memory(sbi, FREE_NIDS)) {
		__remove_free_nid(sbi, i, PREALLOC_NID);
		need_free = true;
	} else {
		__move_free_nid(sbi, i, PREALLOC_NID, FREE_NID);
	}

	nm_i->available_nids++;

	update_free_nid_bitmap(sbi, nid, true, false);

	spin_unlock(&nm_i->nid_list_lock);

	if (need_free)
		kmem_cache_free(free_nid_slab, i);
}

int f2fs_try_to_free_nids(struct f2fs_sb_info *sbi, int nr_shrink)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int nr = nr_shrink;

	if (nm_i->nid_cnt[FREE_NID] <= MAX_FREE_NIDS)
		return 0;

	if (!mutex_trylock(&nm_i->build_lock))
		return 0;

	while (nr_shrink && nm_i->nid_cnt[FREE_NID] > MAX_FREE_NIDS) {
		struct free_nid *i, *next;
		unsigned int batch = SHRINK_NID_BATCH_SIZE;

		spin_lock(&nm_i->nid_list_lock);
		list_for_each_entry_safe(i, next, &nm_i->free_nid_list, list) {
			if (!nr_shrink || !batch ||
				nm_i->nid_cnt[FREE_NID] <= MAX_FREE_NIDS)
				break;
			__remove_free_nid(sbi, i, FREE_NID);
			kmem_cache_free(free_nid_slab, i);
			nr_shrink--;
			batch--;
		}
		spin_unlock(&nm_i->nid_list_lock);
	}

	mutex_unlock(&nm_i->build_lock);

	return nr - nr_shrink;
}

int f2fs_recover_inline_xattr(struct inode *inode, struct folio *folio)
{
	void *src_addr, *dst_addr;
	size_t inline_size;
	struct folio *ifolio;
	struct f2fs_inode *ri;

	ifolio = f2fs_get_inode_folio(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ifolio))
		return PTR_ERR(ifolio);

	ri = F2FS_INODE(&folio->page);
	if (ri->i_inline & F2FS_INLINE_XATTR) {
		if (!f2fs_has_inline_xattr(inode)) {
			set_inode_flag(inode, FI_INLINE_XATTR);
			stat_inc_inline_xattr(inode);
		}
	} else {
		if (f2fs_has_inline_xattr(inode)) {
			stat_dec_inline_xattr(inode);
			clear_inode_flag(inode, FI_INLINE_XATTR);
		}
		goto update_inode;
	}

	dst_addr = inline_xattr_addr(inode, ifolio);
	src_addr = inline_xattr_addr(inode, folio);
	inline_size = inline_xattr_size(inode);

	f2fs_folio_wait_writeback(ifolio, NODE, true, true);
	memcpy(dst_addr, src_addr, inline_size);
update_inode:
	f2fs_update_inode(inode, ifolio);
	f2fs_folio_put(ifolio, true);
	return 0;
}

int f2fs_recover_xattr_data(struct inode *inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t prev_xnid = F2FS_I(inode)->i_xattr_nid;
	nid_t new_xnid;
	struct dnode_of_data dn;
	struct node_info ni;
	struct folio *xfolio;
	int err;

	if (!prev_xnid)
		goto recover_xnid;

	/* 1: invalidate the previous xattr nid */
	err = f2fs_get_node_info(sbi, prev_xnid, &ni, false);
	if (err)
		return err;

	f2fs_invalidate_blocks(sbi, ni.blk_addr, 1);
	dec_valid_node_count(sbi, inode, false);
	set_node_addr(sbi, &ni, NULL_ADDR, false);

recover_xnid:
	/* 2: update xattr nid in inode */
	if (!f2fs_alloc_nid(sbi, &new_xnid))
		return -ENOSPC;

	set_new_dnode(&dn, inode, NULL, NULL, new_xnid);
	xfolio = f2fs_new_node_folio(&dn, XATTR_NODE_OFFSET);
	if (IS_ERR(xfolio)) {
		f2fs_alloc_nid_failed(sbi, new_xnid);
		return PTR_ERR(xfolio);
	}

	f2fs_alloc_nid_done(sbi, new_xnid);
	f2fs_update_inode_page(inode);

	/* 3: update and set xattr node page dirty */
	if (page) {
		memcpy(F2FS_NODE(&xfolio->page), F2FS_NODE(page),
				VALID_XATTR_BLOCK_SIZE);
		folio_mark_dirty(xfolio);
	}
	f2fs_folio_put(xfolio, true);

	return 0;
}

int f2fs_recover_inode_page(struct f2fs_sb_info *sbi, struct page *page)
{
	struct f2fs_inode *src, *dst;
	nid_t ino = ino_of_node(page);
	struct node_info old_ni, new_ni;
	struct folio *ifolio;
	int err;

	err = f2fs_get_node_info(sbi, ino, &old_ni, false);
	if (err)
		return err;

	if (unlikely(old_ni.blk_addr != NULL_ADDR))
		return -EINVAL;
retry:
	ifolio = f2fs_grab_cache_folio(NODE_MAPPING(sbi), ino, false);
	if (IS_ERR(ifolio)) {
		memalloc_retry_wait(GFP_NOFS);
		goto retry;
	}

	/* Should not use this inode from free nid list */
	remove_free_nid(sbi, ino);

	if (!folio_test_uptodate(ifolio))
		folio_mark_uptodate(ifolio);
	fill_node_footer(&ifolio->page, ino, ino, 0, true);
	set_cold_node(&ifolio->page, false);

	src = F2FS_INODE(page);
	dst = F2FS_INODE(&ifolio->page);

	memcpy(dst, src, offsetof(struct f2fs_inode, i_ext));
	dst->i_size = 0;
	dst->i_blocks = cpu_to_le64(1);
	dst->i_links = cpu_to_le32(1);
	dst->i_xattr_nid = 0;
	dst->i_inline = src->i_inline & (F2FS_INLINE_XATTR | F2FS_EXTRA_ATTR);
	if (dst->i_inline & F2FS_EXTRA_ATTR) {
		dst->i_extra_isize = src->i_extra_isize;

		if (f2fs_sb_has_flexible_inline_xattr(sbi) &&
			F2FS_FITS_IN_INODE(src, le16_to_cpu(src->i_extra_isize),
							i_inline_xattr_size))
			dst->i_inline_xattr_size = src->i_inline_xattr_size;

		if (f2fs_sb_has_project_quota(sbi) &&
			F2FS_FITS_IN_INODE(src, le16_to_cpu(src->i_extra_isize),
								i_projid))
			dst->i_projid = src->i_projid;

		if (f2fs_sb_has_inode_crtime(sbi) &&
			F2FS_FITS_IN_INODE(src, le16_to_cpu(src->i_extra_isize),
							i_crtime_nsec)) {
			dst->i_crtime = src->i_crtime;
			dst->i_crtime_nsec = src->i_crtime_nsec;
		}
	}

	new_ni = old_ni;
	new_ni.ino = ino;

	if (unlikely(inc_valid_node_count(sbi, NULL, true)))
		WARN_ON(1);
	set_node_addr(sbi, &new_ni, NEW_ADDR, false);
	inc_valid_inode_count(sbi);
	folio_mark_dirty(ifolio);
	f2fs_folio_put(ifolio, true);
	return 0;
}

int f2fs_restore_node_summary(struct f2fs_sb_info *sbi,
			unsigned int segno, struct f2fs_summary_block *sum)
{
	struct f2fs_node *rn;
	struct f2fs_summary *sum_entry;
	block_t addr;
	int i, idx, last_offset, nrpages;

	/* scan the node segment */
	last_offset = BLKS_PER_SEG(sbi);
	addr = START_BLOCK(sbi, segno);
	sum_entry = &sum->entries[0];

	for (i = 0; i < last_offset; i += nrpages, addr += nrpages) {
		nrpages = bio_max_segs(last_offset - i);

		/* readahead node pages */
		f2fs_ra_meta_pages(sbi, addr, nrpages, META_POR, true);

		for (idx = addr; idx < addr + nrpages; idx++) {
			struct folio *folio = f2fs_get_tmp_folio(sbi, idx);

			if (IS_ERR(folio))
				return PTR_ERR(folio);

			rn = F2FS_NODE(&folio->page);
			sum_entry->nid = rn->footer.nid;
			sum_entry->version = 0;
			sum_entry->ofs_in_node = 0;
			sum_entry++;
			f2fs_folio_put(folio, true);
		}

		invalidate_mapping_pages(META_MAPPING(sbi), addr,
							addr + nrpages);
	}
	return 0;
}

static void remove_nats_in_journal(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_journal *journal = curseg->journal;
	int i;

	down_write(&curseg->journal_rwsem);
	for (i = 0; i < nats_in_cursum(journal); i++) {
		struct nat_entry *ne;
		struct f2fs_nat_entry raw_ne;
		nid_t nid = le32_to_cpu(nid_in_journal(journal, i));

		if (f2fs_check_nid_range(sbi, nid))
			continue;

		raw_ne = nat_in_journal(journal, i);

		ne = __lookup_nat_cache(nm_i, nid);
		if (!ne) {
			ne = __alloc_nat_entry(sbi, nid, true);
			__init_nat_entry(nm_i, ne, &raw_ne, true);
		}

		/*
		 * if a free nat in journal has not been used after last
		 * checkpoint, we should remove it from available nids,
		 * since later we will add it again.
		 */
		if (!get_nat_flag(ne, IS_DIRTY) &&
				le32_to_cpu(raw_ne.block_addr) == NULL_ADDR) {
			spin_lock(&nm_i->nid_list_lock);
			nm_i->available_nids--;
			spin_unlock(&nm_i->nid_list_lock);
		}

		__set_nat_cache_dirty(nm_i, ne);
	}
	update_nats_in_cursum(journal, -i);
	up_write(&curseg->journal_rwsem);
}

static void __adjust_nat_entry_set(struct nat_entry_set *nes,
						struct list_head *head, int max)
{
	struct nat_entry_set *cur;

	if (nes->entry_cnt >= max)
		goto add_out;

	list_for_each_entry(cur, head, set_list) {
		if (cur->entry_cnt >= nes->entry_cnt) {
			list_add(&nes->set_list, cur->set_list.prev);
			return;
		}
	}
add_out:
	list_add_tail(&nes->set_list, head);
}

static void __update_nat_bits(struct f2fs_sb_info *sbi, nid_t start_nid,
						struct page *page)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int nat_index = start_nid / NAT_ENTRY_PER_BLOCK;
	struct f2fs_nat_block *nat_blk = page_address(page);
	int valid = 0;
	int i = 0;

	if (!enabled_nat_bits(sbi, NULL))
		return;

	if (nat_index == 0) {
		valid = 1;
		i = 1;
	}
	for (; i < NAT_ENTRY_PER_BLOCK; i++) {
		if (le32_to_cpu(nat_blk->entries[i].block_addr) != NULL_ADDR)
			valid++;
	}
	if (valid == 0) {
		__set_bit_le(nat_index, nm_i->empty_nat_bits);
		__clear_bit_le(nat_index, nm_i->full_nat_bits);
		return;
	}

	__clear_bit_le(nat_index, nm_i->empty_nat_bits);
	if (valid == NAT_ENTRY_PER_BLOCK)
		__set_bit_le(nat_index, nm_i->full_nat_bits);
	else
		__clear_bit_le(nat_index, nm_i->full_nat_bits);
}

static int __flush_nat_entry_set(struct f2fs_sb_info *sbi,
		struct nat_entry_set *set, struct cp_control *cpc)
{
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_journal *journal = curseg->journal;
	nid_t start_nid = set->set * NAT_ENTRY_PER_BLOCK;
	bool to_journal = true;
	struct f2fs_nat_block *nat_blk;
	struct nat_entry *ne, *cur;
	struct page *page = NULL;

	/*
	 * there are two steps to flush nat entries:
	 * #1, flush nat entries to journal in current hot data summary block.
	 * #2, flush nat entries to nat page.
	 */
	if (enabled_nat_bits(sbi, cpc) ||
		!__has_cursum_space(journal, set->entry_cnt, NAT_JOURNAL))
		to_journal = false;

	if (to_journal) {
		down_write(&curseg->journal_rwsem);
	} else {
		page = get_next_nat_page(sbi, start_nid);
		if (IS_ERR(page))
			return PTR_ERR(page);

		nat_blk = page_address(page);
		f2fs_bug_on(sbi, !nat_blk);
	}

	/* flush dirty nats in nat entry set */
	list_for_each_entry_safe(ne, cur, &set->entry_list, list) {
		struct f2fs_nat_entry *raw_ne;
		nid_t nid = nat_get_nid(ne);
		int offset;

		f2fs_bug_on(sbi, nat_get_blkaddr(ne) == NEW_ADDR);

		if (to_journal) {
			offset = f2fs_lookup_journal_in_cursum(journal,
							NAT_JOURNAL, nid, 1);
			f2fs_bug_on(sbi, offset < 0);
			raw_ne = &nat_in_journal(journal, offset);
			nid_in_journal(journal, offset) = cpu_to_le32(nid);
		} else {
			raw_ne = &nat_blk->entries[nid - start_nid];
		}
		raw_nat_from_node_info(raw_ne, &ne->ni);
		nat_reset_flag(ne);
		__clear_nat_cache_dirty(NM_I(sbi), set, ne);
		if (nat_get_blkaddr(ne) == NULL_ADDR) {
			add_free_nid(sbi, nid, false, true);
		} else {
			spin_lock(&NM_I(sbi)->nid_list_lock);
			update_free_nid_bitmap(sbi, nid, false, false);
			spin_unlock(&NM_I(sbi)->nid_list_lock);
		}
	}

	if (to_journal) {
		up_write(&curseg->journal_rwsem);
	} else {
		__update_nat_bits(sbi, start_nid, page);
		f2fs_put_page(page, 1);
	}

	/* Allow dirty nats by node block allocation in write_begin */
	if (!set->entry_cnt) {
		radix_tree_delete(&NM_I(sbi)->nat_set_root, set->set);
		kmem_cache_free(nat_entry_set_slab, set);
	}
	return 0;
}

/*
 * This function is called during the checkpointing process.
 */
int f2fs_flush_nat_entries(struct f2fs_sb_info *sbi, struct cp_control *cpc)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct curseg_info *curseg = CURSEG_I(sbi, CURSEG_HOT_DATA);
	struct f2fs_journal *journal = curseg->journal;
	struct nat_entry_set *setvec[NAT_VEC_SIZE];
	struct nat_entry_set *set, *tmp;
	unsigned int found;
	nid_t set_idx = 0;
	LIST_HEAD(sets);
	int err = 0;

	/*
	 * during unmount, let's flush nat_bits before checking
	 * nat_cnt[DIRTY_NAT].
	 */
	if (enabled_nat_bits(sbi, cpc)) {
		f2fs_down_write(&nm_i->nat_tree_lock);
		remove_nats_in_journal(sbi);
		f2fs_up_write(&nm_i->nat_tree_lock);
	}

	if (!nm_i->nat_cnt[DIRTY_NAT])
		return 0;

	f2fs_down_write(&nm_i->nat_tree_lock);

	/*
	 * if there are no enough space in journal to store dirty nat
	 * entries, remove all entries from journal and merge them
	 * into nat entry set.
	 */
	if (enabled_nat_bits(sbi, cpc) ||
		!__has_cursum_space(journal,
			nm_i->nat_cnt[DIRTY_NAT], NAT_JOURNAL))
		remove_nats_in_journal(sbi);

	while ((found = __gang_lookup_nat_set(nm_i,
					set_idx, NAT_VEC_SIZE, setvec))) {
		unsigned idx;

		set_idx = setvec[found - 1]->set + 1;
		for (idx = 0; idx < found; idx++)
			__adjust_nat_entry_set(setvec[idx], &sets,
						MAX_NAT_JENTRIES(journal));
	}

	/* flush dirty nats in nat entry set */
	list_for_each_entry_safe(set, tmp, &sets, set_list) {
		err = __flush_nat_entry_set(sbi, set, cpc);
		if (err)
			break;
	}

	f2fs_up_write(&nm_i->nat_tree_lock);
	/* Allow dirty nats by node block allocation in write_begin */

	return err;
}

static int __get_nat_bitmaps(struct f2fs_sb_info *sbi)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int nat_bits_bytes = nm_i->nat_blocks / BITS_PER_BYTE;
	unsigned int i;
	__u64 cp_ver = cur_cp_version(ckpt);
	block_t nat_bits_addr;

	if (!enabled_nat_bits(sbi, NULL))
		return 0;

	nm_i->nat_bits_blocks = F2FS_BLK_ALIGN((nat_bits_bytes << 1) + 8);
	nm_i->nat_bits = f2fs_kvzalloc(sbi,
			F2FS_BLK_TO_BYTES(nm_i->nat_bits_blocks), GFP_KERNEL);
	if (!nm_i->nat_bits)
		return -ENOMEM;

	nat_bits_addr = __start_cp_addr(sbi) + BLKS_PER_SEG(sbi) -
						nm_i->nat_bits_blocks;
	for (i = 0; i < nm_i->nat_bits_blocks; i++) {
		struct folio *folio;

		folio = f2fs_get_meta_folio(sbi, nat_bits_addr++);
		if (IS_ERR(folio))
			return PTR_ERR(folio);

		memcpy(nm_i->nat_bits + F2FS_BLK_TO_BYTES(i),
					folio_address(folio), F2FS_BLKSIZE);
		f2fs_folio_put(folio, true);
	}

	cp_ver |= (cur_cp_crc(ckpt) << 32);
	if (cpu_to_le64(cp_ver) != *(__le64 *)nm_i->nat_bits) {
		disable_nat_bits(sbi, true);
		return 0;
	}

	nm_i->full_nat_bits = nm_i->nat_bits + 8;
	nm_i->empty_nat_bits = nm_i->full_nat_bits + nat_bits_bytes;

	f2fs_notice(sbi, "Found nat_bits in checkpoint");
	return 0;
}

static inline void load_free_nid_bitmap(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned int i = 0;
	nid_t nid, last_nid;

	if (!enabled_nat_bits(sbi, NULL))
		return;

	for (i = 0; i < nm_i->nat_blocks; i++) {
		i = find_next_bit_le(nm_i->empty_nat_bits, nm_i->nat_blocks, i);
		if (i >= nm_i->nat_blocks)
			break;

		__set_bit_le(i, nm_i->nat_block_bitmap);

		nid = i * NAT_ENTRY_PER_BLOCK;
		last_nid = nid + NAT_ENTRY_PER_BLOCK;

		spin_lock(&NM_I(sbi)->nid_list_lock);
		for (; nid < last_nid; nid++)
			update_free_nid_bitmap(sbi, nid, true, true);
		spin_unlock(&NM_I(sbi)->nid_list_lock);
	}

	for (i = 0; i < nm_i->nat_blocks; i++) {
		i = find_next_bit_le(nm_i->full_nat_bits, nm_i->nat_blocks, i);
		if (i >= nm_i->nat_blocks)
			break;

		__set_bit_le(i, nm_i->nat_block_bitmap);
	}
}

static int init_node_manager(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *sb_raw = F2FS_RAW_SUPER(sbi);
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	unsigned char *version_bitmap;
	unsigned int nat_segs;
	int err;

	nm_i->nat_blkaddr = le32_to_cpu(sb_raw->nat_blkaddr);

	/* segment_count_nat includes pair segment so divide to 2. */
	nat_segs = le32_to_cpu(sb_raw->segment_count_nat) >> 1;
	nm_i->nat_blocks = nat_segs << le32_to_cpu(sb_raw->log_blocks_per_seg);
	nm_i->max_nid = NAT_ENTRY_PER_BLOCK * nm_i->nat_blocks;

	/* not used nids: 0, node, meta, (and root counted as valid node) */
	nm_i->available_nids = nm_i->max_nid - sbi->total_valid_node_count -
						F2FS_RESERVED_NODE_NUM;
	nm_i->nid_cnt[FREE_NID] = 0;
	nm_i->nid_cnt[PREALLOC_NID] = 0;
	nm_i->ram_thresh = DEF_RAM_THRESHOLD;
	nm_i->ra_nid_pages = DEF_RA_NID_PAGES;
	nm_i->dirty_nats_ratio = DEF_DIRTY_NAT_RATIO_THRESHOLD;
	nm_i->max_rf_node_blocks = DEF_RF_NODE_BLOCKS;

	INIT_RADIX_TREE(&nm_i->free_nid_root, GFP_ATOMIC);
	INIT_LIST_HEAD(&nm_i->free_nid_list);
	INIT_RADIX_TREE(&nm_i->nat_root, GFP_NOIO);
	INIT_RADIX_TREE(&nm_i->nat_set_root, GFP_NOIO);
	INIT_LIST_HEAD(&nm_i->nat_entries);
	spin_lock_init(&nm_i->nat_list_lock);

	mutex_init(&nm_i->build_lock);
	spin_lock_init(&nm_i->nid_list_lock);
	init_f2fs_rwsem(&nm_i->nat_tree_lock);

	nm_i->next_scan_nid = le32_to_cpu(sbi->ckpt->next_free_nid);
	nm_i->bitmap_size = __bitmap_size(sbi, NAT_BITMAP);
	version_bitmap = __bitmap_ptr(sbi, NAT_BITMAP);
	nm_i->nat_bitmap = kmemdup(version_bitmap, nm_i->bitmap_size,
					GFP_KERNEL);
	if (!nm_i->nat_bitmap)
		return -ENOMEM;

	if (!test_opt(sbi, NAT_BITS))
		disable_nat_bits(sbi, true);

	err = __get_nat_bitmaps(sbi);
	if (err)
		return err;

#ifdef CONFIG_F2FS_CHECK_FS
	nm_i->nat_bitmap_mir = kmemdup(version_bitmap, nm_i->bitmap_size,
					GFP_KERNEL);
	if (!nm_i->nat_bitmap_mir)
		return -ENOMEM;
#endif

	return 0;
}

static int init_free_nid_cache(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	int i;

	nm_i->free_nid_bitmap =
		f2fs_kvzalloc(sbi, array_size(sizeof(unsigned char *),
					      nm_i->nat_blocks),
			      GFP_KERNEL);
	if (!nm_i->free_nid_bitmap)
		return -ENOMEM;

	for (i = 0; i < nm_i->nat_blocks; i++) {
		nm_i->free_nid_bitmap[i] = f2fs_kvzalloc(sbi,
			f2fs_bitmap_size(NAT_ENTRY_PER_BLOCK), GFP_KERNEL);
		if (!nm_i->free_nid_bitmap[i])
			return -ENOMEM;
	}

	nm_i->nat_block_bitmap = f2fs_kvzalloc(sbi, nm_i->nat_blocks / 8,
								GFP_KERNEL);
	if (!nm_i->nat_block_bitmap)
		return -ENOMEM;

	nm_i->free_nid_count =
		f2fs_kvzalloc(sbi, array_size(sizeof(unsigned short),
					      nm_i->nat_blocks),
			      GFP_KERNEL);
	if (!nm_i->free_nid_count)
		return -ENOMEM;
	return 0;
}

int f2fs_build_node_manager(struct f2fs_sb_info *sbi)
{
	int err;

	sbi->nm_info = f2fs_kzalloc(sbi, sizeof(struct f2fs_nm_info),
							GFP_KERNEL);
	if (!sbi->nm_info)
		return -ENOMEM;

	err = init_node_manager(sbi);
	if (err)
		return err;

	err = init_free_nid_cache(sbi);
	if (err)
		return err;

	/* load free nid status from nat_bits table */
	load_free_nid_bitmap(sbi);

	return f2fs_build_free_nids(sbi, true, true);
}

void f2fs_destroy_node_manager(struct f2fs_sb_info *sbi)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *i, *next_i;
	void *vec[NAT_VEC_SIZE];
	struct nat_entry **natvec = (struct nat_entry **)vec;
	struct nat_entry_set **setvec = (struct nat_entry_set **)vec;
	nid_t nid = 0;
	unsigned int found;

	if (!nm_i)
		return;

	/* destroy free nid list */
	spin_lock(&nm_i->nid_list_lock);
	list_for_each_entry_safe(i, next_i, &nm_i->free_nid_list, list) {
		__remove_free_nid(sbi, i, FREE_NID);
		spin_unlock(&nm_i->nid_list_lock);
		kmem_cache_free(free_nid_slab, i);
		spin_lock(&nm_i->nid_list_lock);
	}
	f2fs_bug_on(sbi, nm_i->nid_cnt[FREE_NID]);
	f2fs_bug_on(sbi, nm_i->nid_cnt[PREALLOC_NID]);
	f2fs_bug_on(sbi, !list_empty(&nm_i->free_nid_list));
	spin_unlock(&nm_i->nid_list_lock);

	/* destroy nat cache */
	f2fs_down_write(&nm_i->nat_tree_lock);
	while ((found = __gang_lookup_nat_cache(nm_i,
					nid, NAT_VEC_SIZE, natvec))) {
		unsigned idx;

		nid = nat_get_nid(natvec[found - 1]) + 1;
		for (idx = 0; idx < found; idx++) {
			spin_lock(&nm_i->nat_list_lock);
			list_del(&natvec[idx]->list);
			spin_unlock(&nm_i->nat_list_lock);

			__del_from_nat_cache(nm_i, natvec[idx]);
		}
	}
	f2fs_bug_on(sbi, nm_i->nat_cnt[TOTAL_NAT]);

	/* destroy nat set cache */
	nid = 0;
	memset(vec, 0, sizeof(void *) * NAT_VEC_SIZE);
	while ((found = __gang_lookup_nat_set(nm_i,
					nid, NAT_VEC_SIZE, setvec))) {
		unsigned idx;

		nid = setvec[found - 1]->set + 1;
		for (idx = 0; idx < found; idx++) {
			/* entry_cnt is not zero, when cp_error was occurred */
			f2fs_bug_on(sbi, !list_empty(&setvec[idx]->entry_list));
			radix_tree_delete(&nm_i->nat_set_root, setvec[idx]->set);
			kmem_cache_free(nat_entry_set_slab, setvec[idx]);
		}
	}
	f2fs_up_write(&nm_i->nat_tree_lock);

	kvfree(nm_i->nat_block_bitmap);
	if (nm_i->free_nid_bitmap) {
		int i;

		for (i = 0; i < nm_i->nat_blocks; i++)
			kvfree(nm_i->free_nid_bitmap[i]);
		kvfree(nm_i->free_nid_bitmap);
	}
	kvfree(nm_i->free_nid_count);

	kvfree(nm_i->nat_bitmap);
	kvfree(nm_i->nat_bits);
#ifdef CONFIG_F2FS_CHECK_FS
	kvfree(nm_i->nat_bitmap_mir);
#endif
	sbi->nm_info = NULL;
	kfree(nm_i);
}

int __init f2fs_create_node_manager_caches(void)
{
	nat_entry_slab = f2fs_kmem_cache_create("f2fs_nat_entry",
			sizeof(struct nat_entry));
	if (!nat_entry_slab)
		goto fail;

	free_nid_slab = f2fs_kmem_cache_create("f2fs_free_nid",
			sizeof(struct free_nid));
	if (!free_nid_slab)
		goto destroy_nat_entry;

	nat_entry_set_slab = f2fs_kmem_cache_create("f2fs_nat_entry_set",
			sizeof(struct nat_entry_set));
	if (!nat_entry_set_slab)
		goto destroy_free_nid;

	fsync_node_entry_slab = f2fs_kmem_cache_create("f2fs_fsync_node_entry",
			sizeof(struct fsync_node_entry));
	if (!fsync_node_entry_slab)
		goto destroy_nat_entry_set;
	return 0;

destroy_nat_entry_set:
	kmem_cache_destroy(nat_entry_set_slab);
destroy_free_nid:
	kmem_cache_destroy(free_nid_slab);
destroy_nat_entry:
	kmem_cache_destroy(nat_entry_slab);
fail:
	return -ENOMEM;
}

void f2fs_destroy_node_manager_caches(void)
{
	kmem_cache_destroy(fsync_node_entry_slab);
	kmem_cache_destroy(nat_entry_set_slab);
	kmem_cache_destroy(free_nid_slab);
	kmem_cache_destroy(nat_entry_slab);
}
