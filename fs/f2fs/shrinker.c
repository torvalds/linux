// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs shrinker support
 *   the basic infra was copied from fs/ubifs/shrinker.c
 *
 * Copyright (c) 2015 Motorola Mobility
 * Copyright (c) 2015 Jaegeuk Kim <jaegeuk@kernel.org>
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "node.h"

static LIST_HEAD(f2fs_list);
static DEFINE_SPINLOCK(f2fs_list_lock);
static unsigned int shrinker_run_no;

static unsigned long __count_nat_entries(struct f2fs_sb_info *sbi)
{
	return NM_I(sbi)->nat_cnt[RECLAIMABLE_NAT];
}

static unsigned long __count_free_nids(struct f2fs_sb_info *sbi)
{
	long count = NM_I(sbi)->nid_cnt[FREE_NID] - MAX_FREE_NIDS;

	return count > 0 ? count : 0;
}

static unsigned long __count_extent_cache(struct f2fs_sb_info *sbi,
					enum extent_type type)
{
	struct extent_tree_info *eti = &sbi->extent_tree[type];

	return atomic_read(&eti->total_zombie_tree) +
				atomic_read(&eti->total_ext_node);
}

unsigned long f2fs_shrink_count(struct shrinker *shrink,
				struct shrink_control *sc)
{
	struct f2fs_sb_info *sbi;
	struct list_head *p;
	unsigned long count = 0;

	spin_lock(&f2fs_list_lock);
	p = f2fs_list.next;
	while (p != &f2fs_list) {
		sbi = list_entry(p, struct f2fs_sb_info, s_list);

		/* stop f2fs_put_super */
		if (!mutex_trylock(&sbi->umount_mutex)) {
			p = p->next;
			continue;
		}
		spin_unlock(&f2fs_list_lock);

		/* count read extent cache entries */
		count += __count_extent_cache(sbi, EX_READ);

		/* count block age extent cache entries */
		count += __count_extent_cache(sbi, EX_BLOCK_AGE);

		/* count clean nat cache entries */
		count += __count_nat_entries(sbi);

		/* count free nids cache entries */
		count += __count_free_nids(sbi);

		spin_lock(&f2fs_list_lock);
		p = p->next;
		mutex_unlock(&sbi->umount_mutex);
	}
	spin_unlock(&f2fs_list_lock);
	return count ?: SHRINK_EMPTY;
}

unsigned long f2fs_shrink_scan(struct shrinker *shrink,
				struct shrink_control *sc)
{
	unsigned long nr = sc->nr_to_scan;
	struct f2fs_sb_info *sbi;
	struct list_head *p;
	unsigned int run_no;
	unsigned long freed = 0;

	spin_lock(&f2fs_list_lock);
	do {
		run_no = ++shrinker_run_no;
	} while (run_no == 0);
	p = f2fs_list.next;
	while (p != &f2fs_list) {
		sbi = list_entry(p, struct f2fs_sb_info, s_list);

		if (sbi->shrinker_run_no == run_no)
			break;

		/* stop f2fs_put_super */
		if (!mutex_trylock(&sbi->umount_mutex)) {
			p = p->next;
			continue;
		}
		spin_unlock(&f2fs_list_lock);

		sbi->shrinker_run_no = run_no;

		/* shrink extent cache entries */
		freed += f2fs_shrink_age_extent_tree(sbi, nr >> 2);

		/* shrink read extent cache entries */
		freed += f2fs_shrink_read_extent_tree(sbi, nr >> 2);

		/* shrink clean nat cache entries */
		if (freed < nr)
			freed += f2fs_try_to_free_nats(sbi, nr - freed);

		/* shrink free nids cache entries */
		if (freed < nr)
			freed += f2fs_try_to_free_nids(sbi, nr - freed);

		spin_lock(&f2fs_list_lock);
		p = p->next;
		list_move_tail(&sbi->s_list, &f2fs_list);
		mutex_unlock(&sbi->umount_mutex);
		if (freed >= nr)
			break;
	}
	spin_unlock(&f2fs_list_lock);
	return freed;
}

unsigned int f2fs_donate_files(void)
{
	struct f2fs_sb_info *sbi;
	struct list_head *p;
	unsigned int donate_files = 0;

	spin_lock(&f2fs_list_lock);
	p = f2fs_list.next;
	while (p != &f2fs_list) {
		sbi = list_entry(p, struct f2fs_sb_info, s_list);

		/* stop f2fs_put_super */
		if (!mutex_trylock(&sbi->umount_mutex)) {
			p = p->next;
			continue;
		}
		spin_unlock(&f2fs_list_lock);

		donate_files += sbi->donate_files;

		spin_lock(&f2fs_list_lock);
		p = p->next;
		mutex_unlock(&sbi->umount_mutex);
	}
	spin_unlock(&f2fs_list_lock);

	return donate_files;
}

static unsigned int do_reclaim_caches(struct f2fs_sb_info *sbi,
				unsigned int reclaim_caches_kb)
{
	struct inode *inode;
	struct f2fs_inode_info *fi;
	unsigned int nfiles = sbi->donate_files;
	pgoff_t npages = reclaim_caches_kb >> (PAGE_SHIFT - 10);

	while (npages && nfiles--) {
		pgoff_t len;

		spin_lock(&sbi->inode_lock[DONATE_INODE]);
		if (list_empty(&sbi->inode_list[DONATE_INODE])) {
			spin_unlock(&sbi->inode_lock[DONATE_INODE]);
			break;
		}
		fi = list_first_entry(&sbi->inode_list[DONATE_INODE],
					struct f2fs_inode_info, gdonate_list);
		list_move_tail(&fi->gdonate_list, &sbi->inode_list[DONATE_INODE]);
		inode = igrab(&fi->vfs_inode);
		spin_unlock(&sbi->inode_lock[DONATE_INODE]);

		if (!inode)
			continue;

		len = fi->donate_end - fi->donate_start + 1;
		npages = npages < len ? 0 : npages - len;
		invalidate_inode_pages2_range(inode->i_mapping,
					fi->donate_start, fi->donate_end);
		iput(inode);
		cond_resched();
	}
	return npages << (PAGE_SHIFT - 10);
}

void f2fs_reclaim_caches(unsigned int reclaim_caches_kb)
{
	struct f2fs_sb_info *sbi;
	struct list_head *p;

	spin_lock(&f2fs_list_lock);
	p = f2fs_list.next;
	while (p != &f2fs_list && reclaim_caches_kb) {
		sbi = list_entry(p, struct f2fs_sb_info, s_list);

		/* stop f2fs_put_super */
		if (!mutex_trylock(&sbi->umount_mutex)) {
			p = p->next;
			continue;
		}
		spin_unlock(&f2fs_list_lock);

		reclaim_caches_kb = do_reclaim_caches(sbi, reclaim_caches_kb);

		spin_lock(&f2fs_list_lock);
		p = p->next;
		mutex_unlock(&sbi->umount_mutex);
	}
	spin_unlock(&f2fs_list_lock);
}

void f2fs_join_shrinker(struct f2fs_sb_info *sbi)
{
	spin_lock(&f2fs_list_lock);
	list_add_tail(&sbi->s_list, &f2fs_list);
	spin_unlock(&f2fs_list_lock);
}

void f2fs_leave_shrinker(struct f2fs_sb_info *sbi)
{
	f2fs_shrink_read_extent_tree(sbi, __count_extent_cache(sbi, EX_READ));
	f2fs_shrink_age_extent_tree(sbi,
				__count_extent_cache(sbi, EX_BLOCK_AGE));

	spin_lock(&f2fs_list_lock);
	list_del_init(&sbi->s_list);
	spin_unlock(&f2fs_list_lock);
}
