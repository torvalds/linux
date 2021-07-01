/*
 * Resizable virtual memory filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *		 2000 Transmeta Corp.
 *		 2000-2001 Christoph Rohland
 *		 2000-2001 SAP AG
 *		 2002 Red Hat Inc.
 * Copyright (C) 2002-2011 Hugh Dickins.
 * Copyright (C) 2011 Google Inc.
 * Copyright (C) 2002-2005 VERITAS Software Corporation.
 * Copyright (C) 2004 Andi Kleen, SuSE Labs
 *
 * Extended attribute support for tmpfs:
 * Copyright (c) 2004, Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 * Copyright (c) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * tiny-shmem:
 * Copyright (c) 2004, 2008 Matt Mackall <mpm@selenic.com>
 *
 * This file is released under the GPL.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched/signal.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/khugepaged.h>
#include <linux/hugetlb.h>
#include <linux/frontswap.h>
#include <linux/fs_parser.h>

#include <asm/tlbflush.h> /* for arch/microblaze update_mmu_cache() */

static struct vfsmount *shm_mnt;

#ifdef CONFIG_SHMEM
/*
 * This virtual memory filesystem is heavily based on the ramfs. It
 * extends ramfs by the ability to use swap and honor resource limits
 * which makes it a completely usable filesystem.
 */

#include <linux/xattr.h>
#include <linux/exportfs.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/shmem_fs.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/pagevec.h>
#include <linux/percpu_counter.h>
#include <linux/falloc.h>
#include <linux/splice.h>
#include <linux/security.h>
#include <linux/swapops.h>
#include <linux/mempolicy.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/migrate.h>
#include <linux/highmem.h>
#include <linux/seq_file.h>
#include <linux/magic.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <uapi/linux/memfd.h>
#include <linux/userfaultfd_k.h>
#include <linux/rmap.h>
#include <linux/uuid.h>

#include <linux/uaccess.h>

#include "internal.h"

#define BLOCKS_PER_PAGE  (PAGE_SIZE/512)
#define VM_ACCT(size)    (PAGE_ALIGN(size) >> PAGE_SHIFT)

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

/* Symlink up to this size is kmalloc'ed instead of using a swappable page */
#define SHORT_SYMLINK_LEN 128

/*
 * shmem_fallocate communicates with shmem_fault or shmem_writepage via
 * inode->i_private (with i_mutex making sure that it has only one user at
 * a time): we would prefer not to enlarge the shmem inode just for that.
 */
struct shmem_falloc {
	wait_queue_head_t *waitq; /* faults into hole wait for punch to end */
	pgoff_t start;		/* start of range currently being fallocated */
	pgoff_t next;		/* the next page offset to be fallocated */
	pgoff_t nr_falloced;	/* how many new pages have been fallocated */
	pgoff_t nr_unswapped;	/* how often writepage refused to swap out */
};

struct shmem_options {
	unsigned long long blocks;
	unsigned long long inodes;
	struct mempolicy *mpol;
	kuid_t uid;
	kgid_t gid;
	umode_t mode;
	bool full_inums;
	int huge;
	int seen;
#define SHMEM_SEEN_BLOCKS 1
#define SHMEM_SEEN_INODES 2
#define SHMEM_SEEN_HUGE 4
#define SHMEM_SEEN_INUMS 8
};

#ifdef CONFIG_TMPFS
static unsigned long shmem_default_max_blocks(void)
{
	return totalram_pages() / 2;
}

static unsigned long shmem_default_max_inodes(void)
{
	unsigned long nr_pages = totalram_pages();

	return min(nr_pages - totalhigh_pages(), nr_pages / 2);
}
#endif

static bool shmem_should_replace_page(struct page *page, gfp_t gfp);
static int shmem_replace_page(struct page **pagep, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index);
static int shmem_swapin_page(struct inode *inode, pgoff_t index,
			     struct page **pagep, enum sgp_type sgp,
			     gfp_t gfp, struct vm_area_struct *vma,
			     vm_fault_t *fault_type);
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
		struct page **pagep, enum sgp_type sgp,
		gfp_t gfp, struct vm_area_struct *vma,
		struct vm_fault *vmf, vm_fault_t *fault_type);

int shmem_getpage(struct inode *inode, pgoff_t index,
		struct page **pagep, enum sgp_type sgp)
{
	return shmem_getpage_gfp(inode, index, pagep, sgp,
		mapping_gfp_mask(inode->i_mapping), NULL, NULL, NULL);
}

static inline struct shmem_sb_info *SHMEM_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 * shmem_file_setup pre-accounts the whole fixed size of a VM object,
 * for shared memory and for shared anonymous (/dev/zero) mappings
 * (unless MAP_NORESERVE and sysctl_overcommit_memory <= 1),
 * consistent with the pre-accounting of private mappings ...
 */
static inline int shmem_acct_size(unsigned long flags, loff_t size)
{
	return (flags & VM_NORESERVE) ?
		0 : security_vm_enough_memory_mm(current->mm, VM_ACCT(size));
}

static inline void shmem_unacct_size(unsigned long flags, loff_t size)
{
	if (!(flags & VM_NORESERVE))
		vm_unacct_memory(VM_ACCT(size));
}

static inline int shmem_reacct_size(unsigned long flags,
		loff_t oldsize, loff_t newsize)
{
	if (!(flags & VM_NORESERVE)) {
		if (VM_ACCT(newsize) > VM_ACCT(oldsize))
			return security_vm_enough_memory_mm(current->mm,
					VM_ACCT(newsize) - VM_ACCT(oldsize));
		else if (VM_ACCT(newsize) < VM_ACCT(oldsize))
			vm_unacct_memory(VM_ACCT(oldsize) - VM_ACCT(newsize));
	}
	return 0;
}

/*
 * ... whereas tmpfs objects are accounted incrementally as
 * pages are allocated, in order to allow large sparse files.
 * shmem_getpage reports shmem_acct_block failure as -ENOSPC not -ENOMEM,
 * so that a failure on a sparse tmpfs mapping will give SIGBUS not OOM.
 */
static inline int shmem_acct_block(unsigned long flags, long pages)
{
	if (!(flags & VM_NORESERVE))
		return 0;

	return security_vm_enough_memory_mm(current->mm,
			pages * VM_ACCT(PAGE_SIZE));
}

static inline void shmem_unacct_blocks(unsigned long flags, long pages)
{
	if (flags & VM_NORESERVE)
		vm_unacct_memory(pages * VM_ACCT(PAGE_SIZE));
}

static inline bool shmem_inode_acct_block(struct inode *inode, long pages)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);

	if (shmem_acct_block(info->flags, pages))
		return false;

	if (sbinfo->max_blocks) {
		if (percpu_counter_compare(&sbinfo->used_blocks,
					   sbinfo->max_blocks - pages) > 0)
			goto unacct;
		percpu_counter_add(&sbinfo->used_blocks, pages);
	}

	return true;

unacct:
	shmem_unacct_blocks(info->flags, pages);
	return false;
}

static inline void shmem_inode_unacct_blocks(struct inode *inode, long pages)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);

	if (sbinfo->max_blocks)
		percpu_counter_sub(&sbinfo->used_blocks, pages);
	shmem_unacct_blocks(info->flags, pages);
}

static const struct super_operations shmem_ops;
static const struct address_space_operations shmem_aops;
static const struct file_operations shmem_file_operations;
static const struct inode_operations shmem_inode_operations;
static const struct inode_operations shmem_dir_inode_operations;
static const struct inode_operations shmem_special_inode_operations;
static const struct vm_operations_struct shmem_vm_ops;
static struct file_system_type shmem_fs_type;

bool vma_is_shmem(struct vm_area_struct *vma)
{
	return vma->vm_ops == &shmem_vm_ops;
}

static LIST_HEAD(shmem_swaplist);
static DEFINE_MUTEX(shmem_swaplist_mutex);

/*
 * shmem_reserve_inode() performs bookkeeping to reserve a shmem inode, and
 * produces a novel ino for the newly allocated inode.
 *
 * It may also be called when making a hard link to permit the space needed by
 * each dentry. However, in that case, no new inode number is needed since that
 * internally draws from another pool of inode numbers (currently global
 * get_next_ino()). This case is indicated by passing NULL as inop.
 */
#define SHMEM_INO_BATCH 1024
static int shmem_reserve_inode(struct super_block *sb, ino_t *inop)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	ino_t ino;

	if (!(sb->s_flags & SB_KERNMOUNT)) {
		spin_lock(&sbinfo->stat_lock);
		if (sbinfo->max_inodes) {
			if (!sbinfo->free_inodes) {
				spin_unlock(&sbinfo->stat_lock);
				return -ENOSPC;
			}
			sbinfo->free_inodes--;
		}
		if (inop) {
			ino = sbinfo->next_ino++;
			if (unlikely(is_zero_ino(ino)))
				ino = sbinfo->next_ino++;
			if (unlikely(!sbinfo->full_inums &&
				     ino > UINT_MAX)) {
				/*
				 * Emulate get_next_ino uint wraparound for
				 * compatibility
				 */
				if (IS_ENABLED(CONFIG_64BIT))
					pr_warn("%s: inode number overflow on device %d, consider using inode64 mount option\n",
						__func__, MINOR(sb->s_dev));
				sbinfo->next_ino = 1;
				ino = sbinfo->next_ino++;
			}
			*inop = ino;
		}
		spin_unlock(&sbinfo->stat_lock);
	} else if (inop) {
		/*
		 * __shmem_file_setup, one of our callers, is lock-free: it
		 * doesn't hold stat_lock in shmem_reserve_inode since
		 * max_inodes is always 0, and is called from potentially
		 * unknown contexts. As such, use a per-cpu batched allocator
		 * which doesn't require the per-sb stat_lock unless we are at
		 * the batch boundary.
		 *
		 * We don't need to worry about inode{32,64} since SB_KERNMOUNT
		 * shmem mounts are not exposed to userspace, so we don't need
		 * to worry about things like glibc compatibility.
		 */
		ino_t *next_ino;
		next_ino = per_cpu_ptr(sbinfo->ino_batch, get_cpu());
		ino = *next_ino;
		if (unlikely(ino % SHMEM_INO_BATCH == 0)) {
			spin_lock(&sbinfo->stat_lock);
			ino = sbinfo->next_ino;
			sbinfo->next_ino += SHMEM_INO_BATCH;
			spin_unlock(&sbinfo->stat_lock);
			if (unlikely(is_zero_ino(ino)))
				ino++;
		}
		*inop = ino;
		*next_ino = ++ino;
		put_cpu();
	}

	return 0;
}

static void shmem_free_inode(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	if (sbinfo->max_inodes) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_inodes++;
		spin_unlock(&sbinfo->stat_lock);
	}
}

/**
 * shmem_recalc_inode - recalculate the block usage of an inode
 * @inode: inode to recalc
 *
 * We have to calculate the free blocks since the mm can drop
 * undirtied hole pages behind our back.
 *
 * But normally   info->alloced == inode->i_mapping->nrpages + info->swapped
 * So mm freed is info->alloced - (inode->i_mapping->nrpages + info->swapped)
 *
 * It has to be called with the spinlock held.
 */
static void shmem_recalc_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	long freed;

	freed = info->alloced - info->swapped - inode->i_mapping->nrpages;
	if (freed > 0) {
		info->alloced -= freed;
		inode->i_blocks -= freed * BLOCKS_PER_PAGE;
		shmem_inode_unacct_blocks(inode, freed);
	}
}

bool shmem_charge(struct inode *inode, long pages)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	unsigned long flags;

	if (!shmem_inode_acct_block(inode, pages))
		return false;

	/* nrpages adjustment first, then shmem_recalc_inode() when balanced */
	inode->i_mapping->nrpages += pages;

	spin_lock_irqsave(&info->lock, flags);
	info->alloced += pages;
	inode->i_blocks += pages * BLOCKS_PER_PAGE;
	shmem_recalc_inode(inode);
	spin_unlock_irqrestore(&info->lock, flags);

	return true;
}

void shmem_uncharge(struct inode *inode, long pages)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	unsigned long flags;

	/* nrpages adjustment done by __delete_from_page_cache() or caller */

	spin_lock_irqsave(&info->lock, flags);
	info->alloced -= pages;
	inode->i_blocks -= pages * BLOCKS_PER_PAGE;
	shmem_recalc_inode(inode);
	spin_unlock_irqrestore(&info->lock, flags);

	shmem_inode_unacct_blocks(inode, pages);
}

/*
 * Replace item expected in xarray by a new item, while holding xa_lock.
 */
static int shmem_replace_entry(struct address_space *mapping,
			pgoff_t index, void *expected, void *replacement)
{
	XA_STATE(xas, &mapping->i_pages, index);
	void *item;

	VM_BUG_ON(!expected);
	VM_BUG_ON(!replacement);
	item = xas_load(&xas);
	if (item != expected)
		return -ENOENT;
	xas_store(&xas, replacement);
	return 0;
}

/*
 * Sometimes, before we decide whether to proceed or to fail, we must check
 * that an entry was not already brought back from swap by a racing thread.
 *
 * Checking page is not enough: by the time a SwapCache page is locked, it
 * might be reused, and again be SwapCache, using the same swap as before.
 */
static bool shmem_confirm_swap(struct address_space *mapping,
			       pgoff_t index, swp_entry_t swap)
{
	return xa_load(&mapping->i_pages, index) == swp_to_radix_entry(swap);
}

/*
 * Definitions for "huge tmpfs": tmpfs mounted with the huge= option
 *
 * SHMEM_HUGE_NEVER:
 *	disables huge pages for the mount;
 * SHMEM_HUGE_ALWAYS:
 *	enables huge pages for the mount;
 * SHMEM_HUGE_WITHIN_SIZE:
 *	only allocate huge pages if the page will be fully within i_size,
 *	also respect fadvise()/madvise() hints;
 * SHMEM_HUGE_ADVISE:
 *	only allocate huge pages if requested with fadvise()/madvise();
 */

#define SHMEM_HUGE_NEVER	0
#define SHMEM_HUGE_ALWAYS	1
#define SHMEM_HUGE_WITHIN_SIZE	2
#define SHMEM_HUGE_ADVISE	3

/*
 * Special values.
 * Only can be set via /sys/kernel/mm/transparent_hugepage/shmem_enabled:
 *
 * SHMEM_HUGE_DENY:
 *	disables huge on shm_mnt and all mounts, for emergency use;
 * SHMEM_HUGE_FORCE:
 *	enables huge on shm_mnt and all mounts, w/o needing option, for testing;
 *
 */
#define SHMEM_HUGE_DENY		(-1)
#define SHMEM_HUGE_FORCE	(-2)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/* ifdef here to avoid bloating shmem.o when not necessary */

static int shmem_huge __read_mostly;

#if defined(CONFIG_SYSFS)
static int shmem_parse_huge(const char *str)
{
	if (!strcmp(str, "never"))
		return SHMEM_HUGE_NEVER;
	if (!strcmp(str, "always"))
		return SHMEM_HUGE_ALWAYS;
	if (!strcmp(str, "within_size"))
		return SHMEM_HUGE_WITHIN_SIZE;
	if (!strcmp(str, "advise"))
		return SHMEM_HUGE_ADVISE;
	if (!strcmp(str, "deny"))
		return SHMEM_HUGE_DENY;
	if (!strcmp(str, "force"))
		return SHMEM_HUGE_FORCE;
	return -EINVAL;
}
#endif

#if defined(CONFIG_SYSFS) || defined(CONFIG_TMPFS)
static const char *shmem_format_huge(int huge)
{
	switch (huge) {
	case SHMEM_HUGE_NEVER:
		return "never";
	case SHMEM_HUGE_ALWAYS:
		return "always";
	case SHMEM_HUGE_WITHIN_SIZE:
		return "within_size";
	case SHMEM_HUGE_ADVISE:
		return "advise";
	case SHMEM_HUGE_DENY:
		return "deny";
	case SHMEM_HUGE_FORCE:
		return "force";
	default:
		VM_BUG_ON(1);
		return "bad_val";
	}
}
#endif

static unsigned long shmem_unused_huge_shrink(struct shmem_sb_info *sbinfo,
		struct shrink_control *sc, unsigned long nr_to_split)
{
	LIST_HEAD(list), *pos, *next;
	LIST_HEAD(to_remove);
	struct inode *inode;
	struct shmem_inode_info *info;
	struct page *page;
	unsigned long batch = sc ? sc->nr_to_scan : 128;
	int removed = 0, split = 0;

	if (list_empty(&sbinfo->shrinklist))
		return SHRINK_STOP;

	spin_lock(&sbinfo->shrinklist_lock);
	list_for_each_safe(pos, next, &sbinfo->shrinklist) {
		info = list_entry(pos, struct shmem_inode_info, shrinklist);

		/* pin the inode */
		inode = igrab(&info->vfs_inode);

		/* inode is about to be evicted */
		if (!inode) {
			list_del_init(&info->shrinklist);
			removed++;
			goto next;
		}

		/* Check if there's anything to gain */
		if (round_up(inode->i_size, PAGE_SIZE) ==
				round_up(inode->i_size, HPAGE_PMD_SIZE)) {
			list_move(&info->shrinklist, &to_remove);
			removed++;
			goto next;
		}

		list_move(&info->shrinklist, &list);
next:
		if (!--batch)
			break;
	}
	spin_unlock(&sbinfo->shrinklist_lock);

	list_for_each_safe(pos, next, &to_remove) {
		info = list_entry(pos, struct shmem_inode_info, shrinklist);
		inode = &info->vfs_inode;
		list_del_init(&info->shrinklist);
		iput(inode);
	}

	list_for_each_safe(pos, next, &list) {
		int ret;

		info = list_entry(pos, struct shmem_inode_info, shrinklist);
		inode = &info->vfs_inode;

		if (nr_to_split && split >= nr_to_split)
			goto leave;

		page = find_get_page(inode->i_mapping,
				(inode->i_size & HPAGE_PMD_MASK) >> PAGE_SHIFT);
		if (!page)
			goto drop;

		/* No huge page at the end of the file: nothing to split */
		if (!PageTransHuge(page)) {
			put_page(page);
			goto drop;
		}

		/*
		 * Leave the inode on the list if we failed to lock
		 * the page at this time.
		 *
		 * Waiting for the lock may lead to deadlock in the
		 * reclaim path.
		 */
		if (!trylock_page(page)) {
			put_page(page);
			goto leave;
		}

		ret = split_huge_page(page);
		unlock_page(page);
		put_page(page);

		/* If split failed leave the inode on the list */
		if (ret)
			goto leave;

		split++;
drop:
		list_del_init(&info->shrinklist);
		removed++;
leave:
		iput(inode);
	}

	spin_lock(&sbinfo->shrinklist_lock);
	list_splice_tail(&list, &sbinfo->shrinklist);
	sbinfo->shrinklist_len -= removed;
	spin_unlock(&sbinfo->shrinklist_lock);

	return split;
}

static long shmem_unused_huge_scan(struct super_block *sb,
		struct shrink_control *sc)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	if (!READ_ONCE(sbinfo->shrinklist_len))
		return SHRINK_STOP;

	return shmem_unused_huge_shrink(sbinfo, sc, 0);
}

static long shmem_unused_huge_count(struct super_block *sb,
		struct shrink_control *sc)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	return READ_ONCE(sbinfo->shrinklist_len);
}
#else /* !CONFIG_TRANSPARENT_HUGEPAGE */

#define shmem_huge SHMEM_HUGE_DENY

static unsigned long shmem_unused_huge_shrink(struct shmem_sb_info *sbinfo,
		struct shrink_control *sc, unsigned long nr_to_split)
{
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static inline bool is_huge_enabled(struct shmem_sb_info *sbinfo)
{
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
	    (shmem_huge == SHMEM_HUGE_FORCE || sbinfo->huge) &&
	    shmem_huge != SHMEM_HUGE_DENY)
		return true;
	return false;
}

/*
 * Like add_to_page_cache_locked, but error if expected item has gone.
 */
static int shmem_add_to_page_cache(struct page *page,
				   struct address_space *mapping,
				   pgoff_t index, void *expected, gfp_t gfp,
				   struct mm_struct *charge_mm)
{
	XA_STATE_ORDER(xas, &mapping->i_pages, index, compound_order(page));
	unsigned long i = 0;
	unsigned long nr = compound_nr(page);
	int error;

	VM_BUG_ON_PAGE(PageTail(page), page);
	VM_BUG_ON_PAGE(index != round_down(index, nr), page);
	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageSwapBacked(page), page);
	VM_BUG_ON(expected && PageTransHuge(page));

	page_ref_add(page, nr);
	page->mapping = mapping;
	page->index = index;

	if (!PageSwapCache(page)) {
		error = mem_cgroup_charge(page, charge_mm, gfp);
		if (error) {
			if (PageTransHuge(page)) {
				count_vm_event(THP_FILE_FALLBACK);
				count_vm_event(THP_FILE_FALLBACK_CHARGE);
			}
			goto error;
		}
	}
	cgroup_throttle_swaprate(page, gfp);

	do {
		void *entry;
		xas_lock_irq(&xas);
		entry = xas_find_conflict(&xas);
		if (entry != expected)
			xas_set_err(&xas, -EEXIST);
		xas_create_range(&xas);
		if (xas_error(&xas))
			goto unlock;
next:
		xas_store(&xas, page);
		if (++i < nr) {
			xas_next(&xas);
			goto next;
		}
		if (PageTransHuge(page)) {
			count_vm_event(THP_FILE_ALLOC);
			__inc_node_page_state(page, NR_SHMEM_THPS);
		}
		mapping->nrpages += nr;
		__mod_lruvec_page_state(page, NR_FILE_PAGES, nr);
		__mod_lruvec_page_state(page, NR_SHMEM, nr);
unlock:
		xas_unlock_irq(&xas);
	} while (xas_nomem(&xas, gfp));

	if (xas_error(&xas)) {
		error = xas_error(&xas);
		goto error;
	}

	return 0;
error:
	page->mapping = NULL;
	page_ref_sub(page, nr);
	return error;
}

/*
 * Like delete_from_page_cache, but substitutes swap for page.
 */
static void shmem_delete_from_page_cache(struct page *page, void *radswap)
{
	struct address_space *mapping = page->mapping;
	int error;

	VM_BUG_ON_PAGE(PageCompound(page), page);

	xa_lock_irq(&mapping->i_pages);
	error = shmem_replace_entry(mapping, page->index, page, radswap);
	page->mapping = NULL;
	mapping->nrpages--;
	__dec_lruvec_page_state(page, NR_FILE_PAGES);
	__dec_lruvec_page_state(page, NR_SHMEM);
	xa_unlock_irq(&mapping->i_pages);
	put_page(page);
	BUG_ON(error);
}

/*
 * Remove swap entry from page cache, free the swap and its page cache.
 */
static int shmem_free_swap(struct address_space *mapping,
			   pgoff_t index, void *radswap)
{
	void *old;

	old = xa_cmpxchg_irq(&mapping->i_pages, index, radswap, NULL, 0);
	if (old != radswap)
		return -ENOENT;
	free_swap_and_cache(radix_to_swp_entry(radswap));
	return 0;
}

/*
 * Determine (in bytes) how many of the shmem object's pages mapped by the
 * given offsets are swapped out.
 *
 * This is safe to call without i_mutex or the i_pages lock thanks to RCU,
 * as long as the inode doesn't go away and racy results are not a problem.
 */
unsigned long shmem_partial_swap_usage(struct address_space *mapping,
						pgoff_t start, pgoff_t end)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct page *page;
	unsigned long swapped = 0;

	rcu_read_lock();
	xas_for_each(&xas, page, end - 1) {
		if (xas_retry(&xas, page))
			continue;
		if (xa_is_value(page))
			swapped++;

		if (need_resched()) {
			xas_pause(&xas);
			cond_resched_rcu();
		}
	}

	rcu_read_unlock();

	return swapped << PAGE_SHIFT;
}

/*
 * Determine (in bytes) how many of the shmem object's pages mapped by the
 * given vma is swapped out.
 *
 * This is safe to call without i_mutex or the i_pages lock thanks to RCU,
 * as long as the inode doesn't go away and racy results are not a problem.
 */
unsigned long shmem_swap_usage(struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(vma->vm_file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct address_space *mapping = inode->i_mapping;
	unsigned long swapped;

	/* Be careful as we don't hold info->lock */
	swapped = READ_ONCE(info->swapped);

	/*
	 * The easier cases are when the shmem object has nothing in swap, or
	 * the vma maps it whole. Then we can simply use the stats that we
	 * already track.
	 */
	if (!swapped)
		return 0;

	if (!vma->vm_pgoff && vma->vm_end - vma->vm_start >= inode->i_size)
		return swapped << PAGE_SHIFT;

	/* Here comes the more involved part */
	return shmem_partial_swap_usage(mapping,
			linear_page_index(vma, vma->vm_start),
			linear_page_index(vma, vma->vm_end));
}

/*
 * SysV IPC SHM_UNLOCK restore Unevictable pages to their evictable lists.
 */
void shmem_unlock_mapping(struct address_space *mapping)
{
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	pgoff_t index = 0;

	pagevec_init(&pvec);
	/*
	 * Minor point, but we might as well stop if someone else SHM_LOCKs it.
	 */
	while (!mapping_unevictable(mapping)) {
		/*
		 * Avoid pagevec_lookup(): find_get_pages() returns 0 as if it
		 * has finished, if it hits a row of PAGEVEC_SIZE swap entries.
		 */
		pvec.nr = find_get_entries(mapping, index,
					   PAGEVEC_SIZE, pvec.pages, indices);
		if (!pvec.nr)
			break;
		index = indices[pvec.nr - 1] + 1;
		pagevec_remove_exceptionals(&pvec);
		check_move_unevictable_pages(&pvec);
		pagevec_release(&pvec);
		cond_resched();
	}
}

/*
 * Check whether a hole-punch or truncation needs to split a huge page,
 * returning true if no split was required, or the split has been successful.
 *
 * Eviction (or truncation to 0 size) should never need to split a huge page;
 * but in rare cases might do so, if shmem_undo_range() failed to trylock on
 * head, and then succeeded to trylock on tail.
 *
 * A split can only succeed when there are no additional references on the
 * huge page: so the split below relies upon find_get_entries() having stopped
 * when it found a subpage of the huge page, without getting further references.
 */
static bool shmem_punch_compound(struct page *page, pgoff_t start, pgoff_t end)
{
	if (!PageTransCompound(page))
		return true;

	/* Just proceed to delete a huge page wholly within the range punched */
	if (PageHead(page) &&
	    page->index >= start && page->index + HPAGE_PMD_NR <= end)
		return true;

	/* Try to split huge page, so we can truly punch the hole or truncate */
	return split_huge_page(page) >= 0;
}

/*
 * Remove range of pages and swap entries from page cache, and free them.
 * If !unfalloc, truncate or punch hole; if unfalloc, undo failed fallocate.
 */
static void shmem_undo_range(struct inode *inode, loff_t lstart, loff_t lend,
								 bool unfalloc)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t start = (lstart + PAGE_SIZE - 1) >> PAGE_SHIFT;
	pgoff_t end = (lend + 1) >> PAGE_SHIFT;
	unsigned int partial_start = lstart & (PAGE_SIZE - 1);
	unsigned int partial_end = (lend + 1) & (PAGE_SIZE - 1);
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	long nr_swaps_freed = 0;
	pgoff_t index;
	int i;

	if (lend == -1)
		end = -1;	/* unsigned, so actually very big */

	pagevec_init(&pvec);
	index = start;
	while (index < end) {
		pvec.nr = find_get_entries(mapping, index,
			min(end - index, (pgoff_t)PAGEVEC_SIZE),
			pvec.pages, indices);
		if (!pvec.nr)
			break;
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			index = indices[i];
			if (index >= end)
				break;

			if (xa_is_value(page)) {
				if (unfalloc)
					continue;
				nr_swaps_freed += !shmem_free_swap(mapping,
								index, page);
				continue;
			}

			VM_BUG_ON_PAGE(page_to_pgoff(page) != index, page);

			if (!trylock_page(page))
				continue;

			if ((!unfalloc || !PageUptodate(page)) &&
			    page_mapping(page) == mapping) {
				VM_BUG_ON_PAGE(PageWriteback(page), page);
				if (shmem_punch_compound(page, start, end))
					truncate_inode_page(mapping, page);
			}
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		cond_resched();
		index++;
	}

	if (partial_start) {
		struct page *page = NULL;
		shmem_getpage(inode, start - 1, &page, SGP_READ);
		if (page) {
			unsigned int top = PAGE_SIZE;
			if (start > end) {
				top = partial_end;
				partial_end = 0;
			}
			zero_user_segment(page, partial_start, top);
			set_page_dirty(page);
			unlock_page(page);
			put_page(page);
		}
	}
	if (partial_end) {
		struct page *page = NULL;
		shmem_getpage(inode, end, &page, SGP_READ);
		if (page) {
			zero_user_segment(page, 0, partial_end);
			set_page_dirty(page);
			unlock_page(page);
			put_page(page);
		}
	}
	if (start >= end)
		return;

	index = start;
	while (index < end) {
		cond_resched();

		pvec.nr = find_get_entries(mapping, index,
				min(end - index, (pgoff_t)PAGEVEC_SIZE),
				pvec.pages, indices);
		if (!pvec.nr) {
			/* If all gone or hole-punch or unfalloc, we're done */
			if (index == start || end != -1)
				break;
			/* But if truncating, restart to make sure all gone */
			index = start;
			continue;
		}
		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];

			index = indices[i];
			if (index >= end)
				break;

			if (xa_is_value(page)) {
				if (unfalloc)
					continue;
				if (shmem_free_swap(mapping, index, page)) {
					/* Swap was replaced by page: retry */
					index--;
					break;
				}
				nr_swaps_freed++;
				continue;
			}

			lock_page(page);

			if (!unfalloc || !PageUptodate(page)) {
				if (page_mapping(page) != mapping) {
					/* Page was replaced by swap: retry */
					unlock_page(page);
					index--;
					break;
				}
				VM_BUG_ON_PAGE(PageWriteback(page), page);
				if (shmem_punch_compound(page, start, end))
					truncate_inode_page(mapping, page);
				else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
					/* Wipe the page and don't get stuck */
					clear_highpage(page);
					flush_dcache_page(page);
					set_page_dirty(page);
					if (index <
					    round_up(start, HPAGE_PMD_NR))
						start = index + 1;
				}
			}
			unlock_page(page);
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		index++;
	}

	spin_lock_irq(&info->lock);
	info->swapped -= nr_swaps_freed;
	shmem_recalc_inode(inode);
	spin_unlock_irq(&info->lock);
}

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	shmem_undo_range(inode, lstart, lend, false);
	inode->i_ctime = inode->i_mtime = current_time(inode);
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

static int shmem_getattr(const struct path *path, struct kstat *stat,
			 u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = path->dentry->d_inode;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sb_info = SHMEM_SB(inode->i_sb);

	if (info->alloced - info->swapped != inode->i_mapping->nrpages) {
		spin_lock_irq(&info->lock);
		shmem_recalc_inode(inode);
		spin_unlock_irq(&info->lock);
	}
	generic_fillattr(inode, stat);

	if (is_huge_enabled(sb_info))
		stat->blksize = HPAGE_PMD_SIZE;

	return 0;
}

static int shmem_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	int error;

	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		loff_t oldsize = inode->i_size;
		loff_t newsize = attr->ia_size;

		/* protected by i_mutex */
		if ((newsize < oldsize && (info->seals & F_SEAL_SHRINK)) ||
		    (newsize > oldsize && (info->seals & F_SEAL_GROW)))
			return -EPERM;

		if (newsize != oldsize) {
			error = shmem_reacct_size(SHMEM_I(inode)->flags,
					oldsize, newsize);
			if (error)
				return error;
			i_size_write(inode, newsize);
			inode->i_ctime = inode->i_mtime = current_time(inode);
		}
		if (newsize <= oldsize) {
			loff_t holebegin = round_up(newsize, PAGE_SIZE);
			if (oldsize > holebegin)
				unmap_mapping_range(inode->i_mapping,
							holebegin, 0, 1);
			if (info->alloced)
				shmem_truncate_range(inode,
							newsize, (loff_t)-1);
			/* unmap again to remove racily COWed private pages */
			if (oldsize > holebegin)
				unmap_mapping_range(inode->i_mapping,
							holebegin, 0, 1);

			/*
			 * Part of the huge page can be beyond i_size: subject
			 * to shrink under memory pressure.
			 */
			if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
				spin_lock(&sbinfo->shrinklist_lock);
				/*
				 * _careful to defend against unlocked access to
				 * ->shrink_list in shmem_unused_huge_shrink()
				 */
				if (list_empty_careful(&info->shrinklist)) {
					list_add_tail(&info->shrinklist,
							&sbinfo->shrinklist);
					sbinfo->shrinklist_len++;
				}
				spin_unlock(&sbinfo->shrinklist_lock);
			}
		}
	}

	setattr_copy(inode, attr);
	if (attr->ia_valid & ATTR_MODE)
		error = posix_acl_chmod(inode, inode->i_mode);
	return error;
}

static void shmem_evict_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);

	if (inode->i_mapping->a_ops == &shmem_aops) {
		shmem_unacct_size(info->flags, inode->i_size);
		inode->i_size = 0;
		shmem_truncate_range(inode, 0, (loff_t)-1);
		if (!list_empty(&info->shrinklist)) {
			spin_lock(&sbinfo->shrinklist_lock);
			if (!list_empty(&info->shrinklist)) {
				list_del_init(&info->shrinklist);
				sbinfo->shrinklist_len--;
			}
			spin_unlock(&sbinfo->shrinklist_lock);
		}
		while (!list_empty(&info->swaplist)) {
			/* Wait while shmem_unuse() is scanning this inode... */
			wait_var_event(&info->stop_eviction,
				       !atomic_read(&info->stop_eviction));
			mutex_lock(&shmem_swaplist_mutex);
			/* ...but beware of the race if we peeked too early */
			if (!atomic_read(&info->stop_eviction))
				list_del_init(&info->swaplist);
			mutex_unlock(&shmem_swaplist_mutex);
		}
	}

	simple_xattrs_free(&info->xattrs);
	WARN_ON(inode->i_blocks);
	shmem_free_inode(inode->i_sb);
	clear_inode(inode);
}

extern struct swap_info_struct *swap_info[];

static int shmem_find_swap_entries(struct address_space *mapping,
				   pgoff_t start, unsigned int nr_entries,
				   struct page **entries, pgoff_t *indices,
				   unsigned int type, bool frontswap)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct page *page;
	swp_entry_t entry;
	unsigned int ret = 0;

	if (!nr_entries)
		return 0;

	rcu_read_lock();
	xas_for_each(&xas, page, ULONG_MAX) {
		if (xas_retry(&xas, page))
			continue;

		if (!xa_is_value(page))
			continue;

		entry = radix_to_swp_entry(page);
		if (swp_type(entry) != type)
			continue;
		if (frontswap &&
		    !frontswap_test(swap_info[type], swp_offset(entry)))
			continue;

		indices[ret] = xas.xa_index;
		entries[ret] = page;

		if (need_resched()) {
			xas_pause(&xas);
			cond_resched_rcu();
		}
		if (++ret == nr_entries)
			break;
	}
	rcu_read_unlock();

	return ret;
}

/*
 * Move the swapped pages for an inode to page cache. Returns the count
 * of pages swapped in, or the error in case of failure.
 */
static int shmem_unuse_swap_entries(struct inode *inode, struct pagevec pvec,
				    pgoff_t *indices)
{
	int i = 0;
	int ret = 0;
	int error = 0;
	struct address_space *mapping = inode->i_mapping;

	for (i = 0; i < pvec.nr; i++) {
		struct page *page = pvec.pages[i];

		if (!xa_is_value(page))
			continue;
		error = shmem_swapin_page(inode, indices[i],
					  &page, SGP_CACHE,
					  mapping_gfp_mask(mapping),
					  NULL, NULL);
		if (error == 0) {
			unlock_page(page);
			put_page(page);
			ret++;
		}
		if (error == -ENOMEM)
			break;
		error = 0;
	}
	return error ? error : ret;
}

/*
 * If swap found in inode, free it and move page from swapcache to filecache.
 */
static int shmem_unuse_inode(struct inode *inode, unsigned int type,
			     bool frontswap, unsigned long *fs_pages_to_unuse)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t start = 0;
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	bool frontswap_partial = (frontswap && *fs_pages_to_unuse > 0);
	int ret = 0;

	pagevec_init(&pvec);
	do {
		unsigned int nr_entries = PAGEVEC_SIZE;

		if (frontswap_partial && *fs_pages_to_unuse < PAGEVEC_SIZE)
			nr_entries = *fs_pages_to_unuse;

		pvec.nr = shmem_find_swap_entries(mapping, start, nr_entries,
						  pvec.pages, indices,
						  type, frontswap);
		if (pvec.nr == 0) {
			ret = 0;
			break;
		}

		ret = shmem_unuse_swap_entries(inode, pvec, indices);
		if (ret < 0)
			break;

		if (frontswap_partial) {
			*fs_pages_to_unuse -= ret;
			if (*fs_pages_to_unuse == 0) {
				ret = FRONTSWAP_PAGES_UNUSED;
				break;
			}
		}

		start = indices[pvec.nr - 1];
	} while (true);

	return ret;
}

/*
 * Read all the shared memory data that resides in the swap
 * device 'type' back into memory, so the swap device can be
 * unused.
 */
int shmem_unuse(unsigned int type, bool frontswap,
		unsigned long *fs_pages_to_unuse)
{
	struct shmem_inode_info *info, *next;
	int error = 0;

	if (list_empty(&shmem_swaplist))
		return 0;

	mutex_lock(&shmem_swaplist_mutex);
	list_for_each_entry_safe(info, next, &shmem_swaplist, swaplist) {
		if (!info->swapped) {
			list_del_init(&info->swaplist);
			continue;
		}
		/*
		 * Drop the swaplist mutex while searching the inode for swap;
		 * but before doing so, make sure shmem_evict_inode() will not
		 * remove placeholder inode from swaplist, nor let it be freed
		 * (igrab() would protect from unlink, but not from unmount).
		 */
		atomic_inc(&info->stop_eviction);
		mutex_unlock(&shmem_swaplist_mutex);

		error = shmem_unuse_inode(&info->vfs_inode, type, frontswap,
					  fs_pages_to_unuse);
		cond_resched();

		mutex_lock(&shmem_swaplist_mutex);
		next = list_next_entry(info, swaplist);
		if (!info->swapped)
			list_del_init(&info->swaplist);
		if (atomic_dec_and_test(&info->stop_eviction))
			wake_up_var(&info->stop_eviction);
		if (error)
			break;
	}
	mutex_unlock(&shmem_swaplist_mutex);

	return error;
}

/*
 * Move the page from the page cache to the swap cache.
 */
static int shmem_writepage(struct page *page, struct writeback_control *wbc)
{
	struct shmem_inode_info *info;
	struct address_space *mapping;
	struct inode *inode;
	swp_entry_t swap;
	pgoff_t index;

	VM_BUG_ON_PAGE(PageCompound(page), page);
	BUG_ON(!PageLocked(page));
	mapping = page->mapping;
	index = page->index;
	inode = mapping->host;
	info = SHMEM_I(inode);
	if (info->flags & VM_LOCKED)
		goto redirty;
	if (!total_swap_pages)
		goto redirty;

	/*
	 * Our capabilities prevent regular writeback or sync from ever calling
	 * shmem_writepage; but a stacking filesystem might use ->writepage of
	 * its underlying filesystem, in which case tmpfs should write out to
	 * swap only in response to memory pressure, and not for the writeback
	 * threads or sync.
	 */
	if (!wbc->for_reclaim) {
		WARN_ON_ONCE(1);	/* Still happens? Tell us about it! */
		goto redirty;
	}

	/*
	 * This is somewhat ridiculous, but without plumbing a SWAP_MAP_FALLOC
	 * value into swapfile.c, the only way we can correctly account for a
	 * fallocated page arriving here is now to initialize it and write it.
	 *
	 * That's okay for a page already fallocated earlier, but if we have
	 * not yet completed the fallocation, then (a) we want to keep track
	 * of this page in case we have to undo it, and (b) it may not be a
	 * good idea to continue anyway, once we're pushing into swap.  So
	 * reactivate the page, and let shmem_fallocate() quit when too many.
	 */
	if (!PageUptodate(page)) {
		if (inode->i_private) {
			struct shmem_falloc *shmem_falloc;
			spin_lock(&inode->i_lock);
			shmem_falloc = inode->i_private;
			if (shmem_falloc &&
			    !shmem_falloc->waitq &&
			    index >= shmem_falloc->start &&
			    index < shmem_falloc->next)
				shmem_falloc->nr_unswapped++;
			else
				shmem_falloc = NULL;
			spin_unlock(&inode->i_lock);
			if (shmem_falloc)
				goto redirty;
		}
		clear_highpage(page);
		flush_dcache_page(page);
		SetPageUptodate(page);
	}

	swap = get_swap_page(page);
	if (!swap.val)
		goto redirty;

	/*
	 * Add inode to shmem_unuse()'s list of swapped-out inodes,
	 * if it's not already there.  Do it now before the page is
	 * moved to swap cache, when its pagelock no longer protects
	 * the inode from eviction.  But don't unlock the mutex until
	 * we've incremented swapped, because shmem_unuse_inode() will
	 * prune a !swapped inode from the swaplist under this mutex.
	 */
	mutex_lock(&shmem_swaplist_mutex);
	if (list_empty(&info->swaplist))
		list_add(&info->swaplist, &shmem_swaplist);

	if (add_to_swap_cache(page, swap,
			__GFP_HIGH | __GFP_NOMEMALLOC | __GFP_NOWARN,
			NULL) == 0) {
		spin_lock_irq(&info->lock);
		shmem_recalc_inode(inode);
		info->swapped++;
		spin_unlock_irq(&info->lock);

		swap_shmem_alloc(swap);
		shmem_delete_from_page_cache(page, swp_to_radix_entry(swap));

		mutex_unlock(&shmem_swaplist_mutex);
		BUG_ON(page_mapped(page));
		swap_writepage(page, wbc);
		return 0;
	}

	mutex_unlock(&shmem_swaplist_mutex);
	put_swap_page(page, swap);
redirty:
	set_page_dirty(page);
	if (wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;	/* Return with page locked */
	unlock_page(page);
	return 0;
}

#if defined(CONFIG_NUMA) && defined(CONFIG_TMPFS)
static void shmem_show_mpol(struct seq_file *seq, struct mempolicy *mpol)
{
	char buffer[64];

	if (!mpol || mpol->mode == MPOL_DEFAULT)
		return;		/* show nothing */

	mpol_to_str(buffer, sizeof(buffer), mpol);

	seq_printf(seq, ",mpol=%s", buffer);
}

static struct mempolicy *shmem_get_sbmpol(struct shmem_sb_info *sbinfo)
{
	struct mempolicy *mpol = NULL;
	if (sbinfo->mpol) {
		spin_lock(&sbinfo->stat_lock);	/* prevent replace/use races */
		mpol = sbinfo->mpol;
		mpol_get(mpol);
		spin_unlock(&sbinfo->stat_lock);
	}
	return mpol;
}
#else /* !CONFIG_NUMA || !CONFIG_TMPFS */
static inline void shmem_show_mpol(struct seq_file *seq, struct mempolicy *mpol)
{
}
static inline struct mempolicy *shmem_get_sbmpol(struct shmem_sb_info *sbinfo)
{
	return NULL;
}
#endif /* CONFIG_NUMA && CONFIG_TMPFS */
#ifndef CONFIG_NUMA
#define vm_policy vm_private_data
#endif

static void shmem_pseudo_vma_init(struct vm_area_struct *vma,
		struct shmem_inode_info *info, pgoff_t index)
{
	/* Create a pseudo vma that just contains the policy */
	vma_init(vma, NULL);
	/* Bias interleave by inode number to distribute better across nodes */
	vma->vm_pgoff = index + info->vfs_inode.i_ino;
	vma->vm_policy = mpol_shared_policy_lookup(&info->policy, index);
}

static void shmem_pseudo_vma_destroy(struct vm_area_struct *vma)
{
	/* Drop reference taken by mpol_shared_policy_lookup() */
	mpol_cond_put(vma->vm_policy);
}

static struct page *shmem_swapin(swp_entry_t swap, gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct page *page;
	struct vm_fault vmf;

	shmem_pseudo_vma_init(&pvma, info, index);
	vmf.vma = &pvma;
	vmf.address = 0;
	page = swap_cluster_readahead(swap, gfp, &vmf);
	shmem_pseudo_vma_destroy(&pvma);

	return page;
}

static struct page *shmem_alloc_hugepage(gfp_t gfp,
		struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct address_space *mapping = info->vfs_inode.i_mapping;
	pgoff_t hindex;
	struct page *page;

	hindex = round_down(index, HPAGE_PMD_NR);
	if (xa_find(&mapping->i_pages, &hindex, hindex + HPAGE_PMD_NR - 1,
								XA_PRESENT))
		return NULL;

	shmem_pseudo_vma_init(&pvma, info, hindex);
	page = alloc_pages_vma(gfp | __GFP_COMP | __GFP_NORETRY | __GFP_NOWARN,
			HPAGE_PMD_ORDER, &pvma, 0, numa_node_id(), true);
	shmem_pseudo_vma_destroy(&pvma);
	if (page)
		prep_transhuge_page(page);
	else
		count_vm_event(THP_FILE_FALLBACK);
	return page;
}

static struct page *shmem_alloc_page(gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	struct vm_area_struct pvma;
	struct page *page;

	shmem_pseudo_vma_init(&pvma, info, index);
	page = alloc_page_vma(gfp, &pvma, 0);
	shmem_pseudo_vma_destroy(&pvma);

	return page;
}

static struct page *shmem_alloc_and_acct_page(gfp_t gfp,
		struct inode *inode,
		pgoff_t index, bool huge)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct page *page;
	int nr;
	int err = -ENOSPC;

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		huge = false;
	nr = huge ? HPAGE_PMD_NR : 1;

	if (!shmem_inode_acct_block(inode, nr))
		goto failed;

	if (huge)
		page = shmem_alloc_hugepage(gfp, info, index);
	else
		page = shmem_alloc_page(gfp, info, index);
	if (page) {
		__SetPageLocked(page);
		__SetPageSwapBacked(page);
		return page;
	}

	err = -ENOMEM;
	shmem_inode_unacct_blocks(inode, nr);
failed:
	return ERR_PTR(err);
}

/*
 * When a page is moved from swapcache to shmem filecache (either by the
 * usual swapin of shmem_getpage_gfp(), or by the less common swapoff of
 * shmem_unuse_inode()), it may have been read in earlier from swap, in
 * ignorance of the mapping it belongs to.  If that mapping has special
 * constraints (like the gma500 GEM driver, which requires RAM below 4GB),
 * we may need to copy to a suitable page before moving to filecache.
 *
 * In a future release, this may well be extended to respect cpuset and
 * NUMA mempolicy, and applied also to anonymous pages in do_swap_page();
 * but for now it is a simple matter of zone.
 */
static bool shmem_should_replace_page(struct page *page, gfp_t gfp)
{
	return page_zonenum(page) > gfp_zone(gfp);
}

static int shmem_replace_page(struct page **pagep, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index)
{
	struct page *oldpage, *newpage;
	struct address_space *swap_mapping;
	swp_entry_t entry;
	pgoff_t swap_index;
	int error;

	oldpage = *pagep;
	entry.val = page_private(oldpage);
	swap_index = swp_offset(entry);
	swap_mapping = page_mapping(oldpage);

	/*
	 * We have arrived here because our zones are constrained, so don't
	 * limit chance of success by further cpuset and node constraints.
	 */
	gfp &= ~GFP_CONSTRAINT_MASK;
	newpage = shmem_alloc_page(gfp, info, index);
	if (!newpage)
		return -ENOMEM;

	get_page(newpage);
	copy_highpage(newpage, oldpage);
	flush_dcache_page(newpage);

	__SetPageLocked(newpage);
	__SetPageSwapBacked(newpage);
	SetPageUptodate(newpage);
	set_page_private(newpage, entry.val);
	SetPageSwapCache(newpage);

	/*
	 * Our caller will very soon move newpage out of swapcache, but it's
	 * a nice clean interface for us to replace oldpage by newpage there.
	 */
	xa_lock_irq(&swap_mapping->i_pages);
	error = shmem_replace_entry(swap_mapping, swap_index, oldpage, newpage);
	if (!error) {
		mem_cgroup_migrate(oldpage, newpage);
		__inc_lruvec_page_state(newpage, NR_FILE_PAGES);
		__dec_lruvec_page_state(oldpage, NR_FILE_PAGES);
	}
	xa_unlock_irq(&swap_mapping->i_pages);

	if (unlikely(error)) {
		/*
		 * Is this possible?  I think not, now that our callers check
		 * both PageSwapCache and page_private after getting page lock;
		 * but be defensive.  Reverse old to newpage for clear and free.
		 */
		oldpage = newpage;
	} else {
		lru_cache_add(newpage);
		*pagep = newpage;
	}

	ClearPageSwapCache(oldpage);
	set_page_private(oldpage, 0);

	unlock_page(oldpage);
	put_page(oldpage);
	put_page(oldpage);
	return error;
}

/*
 * Swap in the page pointed to by *pagep.
 * Caller has to make sure that *pagep contains a valid swapped page.
 * Returns 0 and the page in pagep if success. On failure, returns the
 * error code and NULL in *pagep.
 */
static int shmem_swapin_page(struct inode *inode, pgoff_t index,
			     struct page **pagep, enum sgp_type sgp,
			     gfp_t gfp, struct vm_area_struct *vma,
			     vm_fault_t *fault_type)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct mm_struct *charge_mm = vma ? vma->vm_mm : current->mm;
	struct swap_info_struct *si;
	struct page *page = NULL;
	swp_entry_t swap;
	int error;

	VM_BUG_ON(!*pagep || !xa_is_value(*pagep));
	swap = radix_to_swp_entry(*pagep);
	*pagep = NULL;

	/* Prevent swapoff from happening to us. */
	si = get_swap_device(swap);
	if (!si) {
		error = EINVAL;
		goto failed;
	}
	/* Look it up and read it in.. */
	page = lookup_swap_cache(swap, NULL, 0);
	if (!page) {
		/* Or update major stats only when swapin succeeds?? */
		if (fault_type) {
			*fault_type |= VM_FAULT_MAJOR;
			count_vm_event(PGMAJFAULT);
			count_memcg_event_mm(charge_mm, PGMAJFAULT);
		}
		/* Here we actually start the io */
		page = shmem_swapin(swap, gfp, info, index);
		if (!page) {
			error = -ENOMEM;
			goto failed;
		}
	}

	/* We have to do this with page locked to prevent races */
	lock_page(page);
	if (!PageSwapCache(page) || page_private(page) != swap.val ||
	    !shmem_confirm_swap(mapping, index, swap)) {
		error = -EEXIST;
		goto unlock;
	}
	if (!PageUptodate(page)) {
		error = -EIO;
		goto failed;
	}
	wait_on_page_writeback(page);

	/*
	 * Some architectures may have to restore extra metadata to the
	 * physical page after reading from swap.
	 */
	arch_swap_restore(swap, page);

	if (shmem_should_replace_page(page, gfp)) {
		error = shmem_replace_page(&page, gfp, info, index);
		if (error)
			goto failed;
	}

	error = shmem_add_to_page_cache(page, mapping, index,
					swp_to_radix_entry(swap), gfp,
					charge_mm);
	if (error)
		goto failed;

	spin_lock_irq(&info->lock);
	info->swapped--;
	shmem_recalc_inode(inode);
	spin_unlock_irq(&info->lock);

	if (sgp == SGP_WRITE)
		mark_page_accessed(page);

	delete_from_swap_cache(page);
	set_page_dirty(page);
	swap_free(swap);

	*pagep = page;
	if (si)
		put_swap_device(si);
	return 0;
failed:
	if (!shmem_confirm_swap(mapping, index, swap))
		error = -EEXIST;
unlock:
	if (page) {
		unlock_page(page);
		put_page(page);
	}

	if (si)
		put_swap_device(si);

	return error;
}

/*
 * shmem_getpage_gfp - find page in cache, or get from swap, or allocate
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache.
 *
 * vmf and fault_type are only supplied by shmem_fault:
 * otherwise they are NULL.
 */
static int shmem_getpage_gfp(struct inode *inode, pgoff_t index,
	struct page **pagep, enum sgp_type sgp, gfp_t gfp,
	struct vm_area_struct *vma, struct vm_fault *vmf,
			vm_fault_t *fault_type)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo;
	struct mm_struct *charge_mm;
	struct page *page;
	enum sgp_type sgp_huge = sgp;
	pgoff_t hindex = index;
	int error;
	int once = 0;
	int alloced = 0;

	if (index > (MAX_LFS_FILESIZE >> PAGE_SHIFT))
		return -EFBIG;
	if (sgp == SGP_NOHUGE || sgp == SGP_HUGE)
		sgp = SGP_CACHE;
repeat:
	if (sgp <= SGP_CACHE &&
	    ((loff_t)index << PAGE_SHIFT) >= i_size_read(inode)) {
		return -EINVAL;
	}

	sbinfo = SHMEM_SB(inode->i_sb);
	charge_mm = vma ? vma->vm_mm : current->mm;

	page = find_lock_entry(mapping, index);
	if (xa_is_value(page)) {
		error = shmem_swapin_page(inode, index, &page,
					  sgp, gfp, vma, fault_type);
		if (error == -EEXIST)
			goto repeat;

		*pagep = page;
		return error;
	}

	if (page)
		hindex = page->index;
	if (page && sgp == SGP_WRITE)
		mark_page_accessed(page);

	/* fallocated page? */
	if (page && !PageUptodate(page)) {
		if (sgp != SGP_READ)
			goto clear;
		unlock_page(page);
		put_page(page);
		page = NULL;
		hindex = index;
	}
	if (page || sgp == SGP_READ)
		goto out;

	/*
	 * Fast cache lookup did not find it:
	 * bring it back from swap or allocate.
	 */

	if (vma && userfaultfd_missing(vma)) {
		*fault_type = handle_userfault(vmf, VM_UFFD_MISSING);
		return 0;
	}

	/* shmem_symlink() */
	if (mapping->a_ops != &shmem_aops)
		goto alloc_nohuge;
	if (shmem_huge == SHMEM_HUGE_DENY || sgp_huge == SGP_NOHUGE)
		goto alloc_nohuge;
	if (shmem_huge == SHMEM_HUGE_FORCE)
		goto alloc_huge;
	switch (sbinfo->huge) {
	case SHMEM_HUGE_NEVER:
		goto alloc_nohuge;
	case SHMEM_HUGE_WITHIN_SIZE: {
		loff_t i_size;
		pgoff_t off;

		off = round_up(index, HPAGE_PMD_NR);
		i_size = round_up(i_size_read(inode), PAGE_SIZE);
		if (i_size >= HPAGE_PMD_SIZE &&
		    i_size >> PAGE_SHIFT >= off)
			goto alloc_huge;

		fallthrough;
	}
	case SHMEM_HUGE_ADVISE:
		if (sgp_huge == SGP_HUGE)
			goto alloc_huge;
		/* TODO: implement fadvise() hints */
		goto alloc_nohuge;
	}

alloc_huge:
	page = shmem_alloc_and_acct_page(gfp, inode, index, true);
	if (IS_ERR(page)) {
alloc_nohuge:
		page = shmem_alloc_and_acct_page(gfp, inode,
						 index, false);
	}
	if (IS_ERR(page)) {
		int retry = 5;

		error = PTR_ERR(page);
		page = NULL;
		if (error != -ENOSPC)
			goto unlock;
		/*
		 * Try to reclaim some space by splitting a huge page
		 * beyond i_size on the filesystem.
		 */
		while (retry--) {
			int ret;

			ret = shmem_unused_huge_shrink(sbinfo, NULL, 1);
			if (ret == SHRINK_STOP)
				break;
			if (ret)
				goto alloc_nohuge;
		}
		goto unlock;
	}

	if (PageTransHuge(page))
		hindex = round_down(index, HPAGE_PMD_NR);
	else
		hindex = index;

	if (sgp == SGP_WRITE)
		__SetPageReferenced(page);

	error = shmem_add_to_page_cache(page, mapping, hindex,
					NULL, gfp & GFP_RECLAIM_MASK,
					charge_mm);
	if (error)
		goto unacct;
	lru_cache_add(page);

	spin_lock_irq(&info->lock);
	info->alloced += compound_nr(page);
	inode->i_blocks += BLOCKS_PER_PAGE << compound_order(page);
	shmem_recalc_inode(inode);
	spin_unlock_irq(&info->lock);
	alloced = true;

	if (PageTransHuge(page) &&
	    DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE) <
			hindex + HPAGE_PMD_NR - 1) {
		/*
		 * Part of the huge page is beyond i_size: subject
		 * to shrink under memory pressure.
		 */
		spin_lock(&sbinfo->shrinklist_lock);
		/*
		 * _careful to defend against unlocked access to
		 * ->shrink_list in shmem_unused_huge_shrink()
		 */
		if (list_empty_careful(&info->shrinklist)) {
			list_add_tail(&info->shrinklist,
				      &sbinfo->shrinklist);
			sbinfo->shrinklist_len++;
		}
		spin_unlock(&sbinfo->shrinklist_lock);
	}

	/*
	 * Let SGP_FALLOC use the SGP_WRITE optimization on a new page.
	 */
	if (sgp == SGP_FALLOC)
		sgp = SGP_WRITE;
clear:
	/*
	 * Let SGP_WRITE caller clear ends if write does not fill page;
	 * but SGP_FALLOC on a page fallocated earlier must initialize
	 * it now, lest undo on failure cancel our earlier guarantee.
	 */
	if (sgp != SGP_WRITE && !PageUptodate(page)) {
		int i;

		for (i = 0; i < compound_nr(page); i++) {
			clear_highpage(page + i);
			flush_dcache_page(page + i);
		}
		SetPageUptodate(page);
	}

	/* Perhaps the file has been truncated since we checked */
	if (sgp <= SGP_CACHE &&
	    ((loff_t)index << PAGE_SHIFT) >= i_size_read(inode)) {
		if (alloced) {
			ClearPageDirty(page);
			delete_from_page_cache(page);
			spin_lock_irq(&info->lock);
			shmem_recalc_inode(inode);
			spin_unlock_irq(&info->lock);
		}
		error = -EINVAL;
		goto unlock;
	}
out:
	*pagep = page + index - hindex;
	return 0;

	/*
	 * Error recovery.
	 */
unacct:
	shmem_inode_unacct_blocks(inode, compound_nr(page));

	if (PageTransHuge(page)) {
		unlock_page(page);
		put_page(page);
		goto alloc_nohuge;
	}
unlock:
	if (page) {
		unlock_page(page);
		put_page(page);
	}
	if (error == -ENOSPC && !once++) {
		spin_lock_irq(&info->lock);
		shmem_recalc_inode(inode);
		spin_unlock_irq(&info->lock);
		goto repeat;
	}
	if (error == -EEXIST)
		goto repeat;
	return error;
}

/*
 * This is like autoremove_wake_function, but it removes the wait queue
 * entry unconditionally - even if something else had already woken the
 * target.
 */
static int synchronous_wake_function(wait_queue_entry_t *wait, unsigned mode, int sync, void *key)
{
	int ret = default_wake_function(wait, mode, sync, key);
	list_del_init(&wait->entry);
	return ret;
}

static vm_fault_t shmem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct inode *inode = file_inode(vma->vm_file);
	gfp_t gfp = mapping_gfp_mask(inode->i_mapping);
	enum sgp_type sgp;
	int err;
	vm_fault_t ret = VM_FAULT_LOCKED;

	/*
	 * Trinity finds that probing a hole which tmpfs is punching can
	 * prevent the hole-punch from ever completing: which in turn
	 * locks writers out with its hold on i_mutex.  So refrain from
	 * faulting pages into the hole while it's being punched.  Although
	 * shmem_undo_range() does remove the additions, it may be unable to
	 * keep up, as each new page needs its own unmap_mapping_range() call,
	 * and the i_mmap tree grows ever slower to scan if new vmas are added.
	 *
	 * It does not matter if we sometimes reach this check just before the
	 * hole-punch begins, so that one fault then races with the punch:
	 * we just need to make racing faults a rare case.
	 *
	 * The implementation below would be much simpler if we just used a
	 * standard mutex or completion: but we cannot take i_mutex in fault,
	 * and bloating every shmem inode for this unlikely case would be sad.
	 */
	if (unlikely(inode->i_private)) {
		struct shmem_falloc *shmem_falloc;

		spin_lock(&inode->i_lock);
		shmem_falloc = inode->i_private;
		if (shmem_falloc &&
		    shmem_falloc->waitq &&
		    vmf->pgoff >= shmem_falloc->start &&
		    vmf->pgoff < shmem_falloc->next) {
			struct file *fpin;
			wait_queue_head_t *shmem_falloc_waitq;
			DEFINE_WAIT_FUNC(shmem_fault_wait, synchronous_wake_function);

			ret = VM_FAULT_NOPAGE;
			fpin = maybe_unlock_mmap_for_io(vmf, NULL);
			if (fpin)
				ret = VM_FAULT_RETRY;

			shmem_falloc_waitq = shmem_falloc->waitq;
			prepare_to_wait(shmem_falloc_waitq, &shmem_fault_wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock(&inode->i_lock);
			schedule();

			/*
			 * shmem_falloc_waitq points into the shmem_fallocate()
			 * stack of the hole-punching task: shmem_falloc_waitq
			 * is usually invalid by the time we reach here, but
			 * finish_wait() does not dereference it in that case;
			 * though i_lock needed lest racing with wake_up_all().
			 */
			spin_lock(&inode->i_lock);
			finish_wait(shmem_falloc_waitq, &shmem_fault_wait);
			spin_unlock(&inode->i_lock);

			if (fpin)
				fput(fpin);
			return ret;
		}
		spin_unlock(&inode->i_lock);
	}

	sgp = SGP_CACHE;

	if ((vma->vm_flags & VM_NOHUGEPAGE) ||
	    test_bit(MMF_DISABLE_THP, &vma->vm_mm->flags))
		sgp = SGP_NOHUGE;
	else if (vma->vm_flags & VM_HUGEPAGE)
		sgp = SGP_HUGE;

	err = shmem_getpage_gfp(inode, vmf->pgoff, &vmf->page, sgp,
				  gfp, vma, vmf, &ret);
	if (err)
		return vmf_error(err);
	return ret;
}

unsigned long shmem_get_unmapped_area(struct file *file,
				      unsigned long uaddr, unsigned long len,
				      unsigned long pgoff, unsigned long flags)
{
	unsigned long (*get_area)(struct file *,
		unsigned long, unsigned long, unsigned long, unsigned long);
	unsigned long addr;
	unsigned long offset;
	unsigned long inflated_len;
	unsigned long inflated_addr;
	unsigned long inflated_offset;

	if (len > TASK_SIZE)
		return -ENOMEM;

	get_area = current->mm->get_unmapped_area;
	addr = get_area(file, uaddr, len, pgoff, flags);

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		return addr;
	if (IS_ERR_VALUE(addr))
		return addr;
	if (addr & ~PAGE_MASK)
		return addr;
	if (addr > TASK_SIZE - len)
		return addr;

	if (shmem_huge == SHMEM_HUGE_DENY)
		return addr;
	if (len < HPAGE_PMD_SIZE)
		return addr;
	if (flags & MAP_FIXED)
		return addr;
	/*
	 * Our priority is to support MAP_SHARED mapped hugely;
	 * and support MAP_PRIVATE mapped hugely too, until it is COWed.
	 * But if caller specified an address hint and we allocated area there
	 * successfully, respect that as before.
	 */
	if (uaddr == addr)
		return addr;

	if (shmem_huge != SHMEM_HUGE_FORCE) {
		struct super_block *sb;

		if (file) {
			VM_BUG_ON(file->f_op != &shmem_file_operations);
			sb = file_inode(file)->i_sb;
		} else {
			/*
			 * Called directly from mm/mmap.c, or drivers/char/mem.c
			 * for "/dev/zero", to create a shared anonymous object.
			 */
			if (IS_ERR(shm_mnt))
				return addr;
			sb = shm_mnt->mnt_sb;
		}
		if (SHMEM_SB(sb)->huge == SHMEM_HUGE_NEVER)
			return addr;
	}

	offset = (pgoff << PAGE_SHIFT) & (HPAGE_PMD_SIZE-1);
	if (offset && offset + len < 2 * HPAGE_PMD_SIZE)
		return addr;
	if ((addr & (HPAGE_PMD_SIZE-1)) == offset)
		return addr;

	inflated_len = len + HPAGE_PMD_SIZE - PAGE_SIZE;
	if (inflated_len > TASK_SIZE)
		return addr;
	if (inflated_len < len)
		return addr;

	inflated_addr = get_area(NULL, uaddr, inflated_len, 0, flags);
	if (IS_ERR_VALUE(inflated_addr))
		return addr;
	if (inflated_addr & ~PAGE_MASK)
		return addr;

	inflated_offset = inflated_addr & (HPAGE_PMD_SIZE-1);
	inflated_addr += offset - inflated_offset;
	if (inflated_offset > offset)
		inflated_addr += HPAGE_PMD_SIZE;

	if (inflated_addr > TASK_SIZE - len)
		return addr;
	return inflated_addr;
}

#ifdef CONFIG_NUMA
static int shmem_set_policy(struct vm_area_struct *vma, struct mempolicy *mpol)
{
	struct inode *inode = file_inode(vma->vm_file);
	return mpol_set_shared_policy(&SHMEM_I(inode)->policy, vma, mpol);
}

static struct mempolicy *shmem_get_policy(struct vm_area_struct *vma,
					  unsigned long addr)
{
	struct inode *inode = file_inode(vma->vm_file);
	pgoff_t index;

	index = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	return mpol_shared_policy_lookup(&SHMEM_I(inode)->policy, index);
}
#endif

int shmem_lock(struct file *file, int lock, struct user_struct *user)
{
	struct inode *inode = file_inode(file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int retval = -ENOMEM;

	/*
	 * What serializes the accesses to info->flags?
	 * ipc_lock_object() when called from shmctl_do_lock(),
	 * no serialization needed when called from shm_destroy().
	 */
	if (lock && !(info->flags & VM_LOCKED)) {
		if (!user_shm_lock(inode->i_size, user))
			goto out_nomem;
		info->flags |= VM_LOCKED;
		mapping_set_unevictable(file->f_mapping);
	}
	if (!lock && (info->flags & VM_LOCKED) && user) {
		user_shm_unlock(inode->i_size, user);
		info->flags &= ~VM_LOCKED;
		mapping_clear_unevictable(file->f_mapping);
	}
	retval = 0;

out_nomem:
	return retval;
}

static int shmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct shmem_inode_info *info = SHMEM_I(file_inode(file));
	int ret;

	ret = seal_check_future_write(info->seals, vma);
	if (ret)
		return ret;

	/* arm64 - allow memory tagging on RAM-based files */
	vma->vm_flags |= VM_MTE_ALLOWED;

	file_accessed(file);
	vma->vm_ops = &shmem_vm_ops;
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
			((vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK) <
			(vma->vm_end & HPAGE_PMD_MASK)) {
		khugepaged_enter(vma, vma->vm_flags);
	}
	return 0;
}

static struct inode *shmem_get_inode(struct super_block *sb, const struct inode *dir,
				     umode_t mode, dev_t dev, unsigned long flags)
{
	struct inode *inode;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	ino_t ino;

	if (shmem_reserve_inode(sb, &ino))
		return NULL;

	inode = new_inode(sb);
	if (inode) {
		inode->i_ino = ino;
		inode_init_owner(inode, dir, mode);
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_generation = prandom_u32();
		info = SHMEM_I(inode);
		memset(info, 0, (char *)inode - (char *)info);
		spin_lock_init(&info->lock);
		atomic_set(&info->stop_eviction, 0);
		info->seals = F_SEAL_SEAL;
		info->flags = flags & VM_NORESERVE;
		INIT_LIST_HEAD(&info->shrinklist);
		INIT_LIST_HEAD(&info->swaplist);
		simple_xattrs_init(&info->xattrs);
		cache_no_acl(inode);

		switch (mode & S_IFMT) {
		default:
			inode->i_op = &shmem_special_inode_operations;
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_mapping->a_ops = &shmem_aops;
			inode->i_op = &shmem_inode_operations;
			inode->i_fop = &shmem_file_operations;
			mpol_shared_policy_init(&info->policy,
						 shmem_get_sbmpol(sbinfo));
			break;
		case S_IFDIR:
			inc_nlink(inode);
			/* Some things misbehave if size == 0 on a directory */
			inode->i_size = 2 * BOGO_DIRENT_SIZE;
			inode->i_op = &shmem_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			break;
		case S_IFLNK:
			/*
			 * Must not load anything in the rbtree,
			 * mpol_free_shared_policy will not be called.
			 */
			mpol_shared_policy_init(&info->policy, NULL);
			break;
		}

		lockdep_annotate_inode_mutex_key(inode);
	} else
		shmem_free_inode(sb);
	return inode;
}

bool shmem_mapping(struct address_space *mapping)
{
	return mapping->a_ops == &shmem_aops;
}

static int shmem_mfill_atomic_pte(struct mm_struct *dst_mm,
				  pmd_t *dst_pmd,
				  struct vm_area_struct *dst_vma,
				  unsigned long dst_addr,
				  unsigned long src_addr,
				  bool zeropage,
				  struct page **pagep)
{
	struct inode *inode = file_inode(dst_vma->vm_file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct address_space *mapping = inode->i_mapping;
	gfp_t gfp = mapping_gfp_mask(mapping);
	pgoff_t pgoff = linear_page_index(dst_vma, dst_addr);
	spinlock_t *ptl;
	void *page_kaddr;
	struct page *page;
	pte_t _dst_pte, *dst_pte;
	int ret;
	pgoff_t offset, max_off;

	ret = -ENOMEM;
	if (!shmem_inode_acct_block(inode, 1)) {
		/*
		 * We may have got a page, returned -ENOENT triggering a retry,
		 * and now we find ourselves with -ENOMEM. Release the page, to
		 * avoid a BUG_ON in our caller.
		 */
		if (unlikely(*pagep)) {
			put_page(*pagep);
			*pagep = NULL;
		}
		goto out;
	}

	if (!*pagep) {
		page = shmem_alloc_page(gfp, info, pgoff);
		if (!page)
			goto out_unacct_blocks;

		if (!zeropage) {	/* mcopy_atomic */
			page_kaddr = kmap_atomic(page);
			ret = copy_from_user(page_kaddr,
					     (const void __user *)src_addr,
					     PAGE_SIZE);
			kunmap_atomic(page_kaddr);

			/* fallback to copy_from_user outside mmap_lock */
			if (unlikely(ret)) {
				*pagep = page;
				shmem_inode_unacct_blocks(inode, 1);
				/* don't free the page */
				return -ENOENT;
			}
		} else {		/* mfill_zeropage_atomic */
			clear_highpage(page);
		}
	} else {
		page = *pagep;
		*pagep = NULL;
	}

	VM_BUG_ON(PageLocked(page) || PageSwapBacked(page));
	__SetPageLocked(page);
	__SetPageSwapBacked(page);
	__SetPageUptodate(page);

	ret = -EFAULT;
	offset = linear_page_index(dst_vma, dst_addr);
	max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(offset >= max_off))
		goto out_release;

	ret = shmem_add_to_page_cache(page, mapping, pgoff, NULL,
				      gfp & GFP_RECLAIM_MASK, dst_mm);
	if (ret)
		goto out_release;

	_dst_pte = mk_pte(page, dst_vma->vm_page_prot);
	if (dst_vma->vm_flags & VM_WRITE)
		_dst_pte = pte_mkwrite(pte_mkdirty(_dst_pte));
	else {
		/*
		 * We don't set the pte dirty if the vma has no
		 * VM_WRITE permission, so mark the page dirty or it
		 * could be freed from under us. We could do it
		 * unconditionally before unlock_page(), but doing it
		 * only if VM_WRITE is not set is faster.
		 */
		set_page_dirty(page);
	}

	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);

	ret = -EFAULT;
	max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(offset >= max_off))
		goto out_release_unlock;

	ret = -EEXIST;
	if (!pte_none(*dst_pte))
		goto out_release_unlock;

	lru_cache_add(page);

	spin_lock_irq(&info->lock);
	info->alloced++;
	inode->i_blocks += BLOCKS_PER_PAGE;
	shmem_recalc_inode(inode);
	spin_unlock_irq(&info->lock);

	inc_mm_counter(dst_mm, mm_counter_file(page));
	page_add_file_rmap(page, false);
	set_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);
	pte_unmap_unlock(dst_pte, ptl);
	unlock_page(page);
	ret = 0;
out:
	return ret;
out_release_unlock:
	pte_unmap_unlock(dst_pte, ptl);
	ClearPageDirty(page);
	delete_from_page_cache(page);
out_release:
	unlock_page(page);
	put_page(page);
out_unacct_blocks:
	shmem_inode_unacct_blocks(inode, 1);
	goto out;
}

int shmem_mcopy_atomic_pte(struct mm_struct *dst_mm,
			   pmd_t *dst_pmd,
			   struct vm_area_struct *dst_vma,
			   unsigned long dst_addr,
			   unsigned long src_addr,
			   struct page **pagep)
{
	return shmem_mfill_atomic_pte(dst_mm, dst_pmd, dst_vma,
				      dst_addr, src_addr, false, pagep);
}

int shmem_mfill_zeropage_pte(struct mm_struct *dst_mm,
			     pmd_t *dst_pmd,
			     struct vm_area_struct *dst_vma,
			     unsigned long dst_addr)
{
	struct page *page = NULL;

	return shmem_mfill_atomic_pte(dst_mm, dst_pmd, dst_vma,
				      dst_addr, 0, true, &page);
}

#ifdef CONFIG_TMPFS
static const struct inode_operations shmem_symlink_inode_operations;
static const struct inode_operations shmem_short_symlink_operations;

#ifdef CONFIG_TMPFS_XATTR
static int shmem_initxattrs(struct inode *, const struct xattr *, void *);
#else
#define shmem_initxattrs NULL
#endif

static int
shmem_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t index = pos >> PAGE_SHIFT;

	/* i_mutex is held by caller */
	if (unlikely(info->seals & (F_SEAL_GROW |
				   F_SEAL_WRITE | F_SEAL_FUTURE_WRITE))) {
		if (info->seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE))
			return -EPERM;
		if ((info->seals & F_SEAL_GROW) && pos + len > inode->i_size)
			return -EPERM;
	}

	return shmem_getpage(inode, index, pagep, SGP_WRITE);
}

static int
shmem_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;

	if (pos + copied > inode->i_size)
		i_size_write(inode, pos + copied);

	if (!PageUptodate(page)) {
		struct page *head = compound_head(page);
		if (PageTransCompound(page)) {
			int i;

			for (i = 0; i < HPAGE_PMD_NR; i++) {
				if (head + i == page)
					continue;
				clear_highpage(head + i);
				flush_dcache_page(head + i);
			}
		}
		if (copied < PAGE_SIZE) {
			unsigned from = pos & (PAGE_SIZE - 1);
			zero_user_segments(page, 0, from,
					from + copied, PAGE_SIZE);
		}
		SetPageUptodate(head);
	}
	set_page_dirty(page);
	unlock_page(page);
	put_page(page);

	return copied;
}

static ssize_t shmem_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
	unsigned long offset;
	enum sgp_type sgp = SGP_READ;
	int error = 0;
	ssize_t retval = 0;
	loff_t *ppos = &iocb->ki_pos;

	/*
	 * Might this read be for a stacking filesystem?  Then when reading
	 * holes of a sparse file, we actually need to allocate those pages,
	 * and even mark them dirty, so it cannot exceed the max_blocks limit.
	 */
	if (!iter_is_iovec(to))
		sgp = SGP_CACHE;

	index = *ppos >> PAGE_SHIFT;
	offset = *ppos & ~PAGE_MASK;

	for (;;) {
		struct page *page = NULL;
		pgoff_t end_index;
		unsigned long nr, ret;
		loff_t i_size = i_size_read(inode);

		end_index = i_size >> PAGE_SHIFT;
		if (index > end_index)
			break;
		if (index == end_index) {
			nr = i_size & ~PAGE_MASK;
			if (nr <= offset)
				break;
		}

		error = shmem_getpage(inode, index, &page, sgp);
		if (error) {
			if (error == -EINVAL)
				error = 0;
			break;
		}
		if (page) {
			if (sgp == SGP_CACHE)
				set_page_dirty(page);
			unlock_page(page);
		}

		/*
		 * We must evaluate after, since reads (unlike writes)
		 * are called without i_mutex protection against truncate
		 */
		nr = PAGE_SIZE;
		i_size = i_size_read(inode);
		end_index = i_size >> PAGE_SHIFT;
		if (index == end_index) {
			nr = i_size & ~PAGE_MASK;
			if (nr <= offset) {
				if (page)
					put_page(page);
				break;
			}
		}
		nr -= offset;

		if (page) {
			/*
			 * If users can be writing to this page using arbitrary
			 * virtual addresses, take care about potential aliasing
			 * before reading the page on the kernel side.
			 */
			if (mapping_writably_mapped(mapping))
				flush_dcache_page(page);
			/*
			 * Mark the page accessed if we read the beginning.
			 */
			if (!offset)
				mark_page_accessed(page);
		} else {
			page = ZERO_PAGE(0);
			get_page(page);
		}

		/*
		 * Ok, we have the page, and it's up-to-date, so
		 * now we can copy it to user space...
		 */
		ret = copy_page_to_iter(page, offset, nr, to);
		retval += ret;
		offset += ret;
		index += offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;

		put_page(page);
		if (!iov_iter_count(to))
			break;
		if (ret < nr) {
			error = -EFAULT;
			break;
		}
		cond_resched();
	}

	*ppos = ((loff_t) index << PAGE_SHIFT) + offset;
	file_accessed(file);
	return retval ? retval : error;
}

/*
 * llseek SEEK_DATA or SEEK_HOLE through the page cache.
 */
static pgoff_t shmem_seek_hole_data(struct address_space *mapping,
				    pgoff_t index, pgoff_t end, int whence)
{
	struct page *page;
	struct pagevec pvec;
	pgoff_t indices[PAGEVEC_SIZE];
	bool done = false;
	int i;

	pagevec_init(&pvec);
	pvec.nr = 1;		/* start small: we may be there already */
	while (!done) {
		pvec.nr = find_get_entries(mapping, index,
					pvec.nr, pvec.pages, indices);
		if (!pvec.nr) {
			if (whence == SEEK_DATA)
				index = end;
			break;
		}
		for (i = 0; i < pvec.nr; i++, index++) {
			if (index < indices[i]) {
				if (whence == SEEK_HOLE) {
					done = true;
					break;
				}
				index = indices[i];
			}
			page = pvec.pages[i];
			if (page && !xa_is_value(page)) {
				if (!PageUptodate(page))
					page = NULL;
			}
			if (index >= end ||
			    (page && whence == SEEK_DATA) ||
			    (!page && whence == SEEK_HOLE)) {
				done = true;
				break;
			}
		}
		pagevec_remove_exceptionals(&pvec);
		pagevec_release(&pvec);
		pvec.nr = PAGEVEC_SIZE;
		cond_resched();
	}
	return index;
}

static loff_t shmem_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	pgoff_t start, end;
	loff_t new_offset;

	if (whence != SEEK_DATA && whence != SEEK_HOLE)
		return generic_file_llseek_size(file, offset, whence,
					MAX_LFS_FILESIZE, i_size_read(inode));
	inode_lock(inode);
	/* We're holding i_mutex so we can access i_size directly */

	if (offset < 0 || offset >= inode->i_size)
		offset = -ENXIO;
	else {
		start = offset >> PAGE_SHIFT;
		end = (inode->i_size + PAGE_SIZE - 1) >> PAGE_SHIFT;
		new_offset = shmem_seek_hole_data(mapping, start, end, whence);
		new_offset <<= PAGE_SHIFT;
		if (new_offset > offset) {
			if (new_offset < inode->i_size)
				offset = new_offset;
			else if (whence == SEEK_DATA)
				offset = -ENXIO;
			else
				offset = inode->i_size;
		}
	}

	if (offset >= 0)
		offset = vfs_setpos(file, offset, MAX_LFS_FILESIZE);
	inode_unlock(inode);
	return offset;
}

static long shmem_fallocate(struct file *file, int mode, loff_t offset,
							 loff_t len)
{
	struct inode *inode = file_inode(file);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_falloc shmem_falloc;
	pgoff_t start, index, end;
	int error;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	inode_lock(inode);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		struct address_space *mapping = file->f_mapping;
		loff_t unmap_start = round_up(offset, PAGE_SIZE);
		loff_t unmap_end = round_down(offset + len, PAGE_SIZE) - 1;
		DECLARE_WAIT_QUEUE_HEAD_ONSTACK(shmem_falloc_waitq);

		/* protected by i_mutex */
		if (info->seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)) {
			error = -EPERM;
			goto out;
		}

		shmem_falloc.waitq = &shmem_falloc_waitq;
		shmem_falloc.start = (u64)unmap_start >> PAGE_SHIFT;
		shmem_falloc.next = (unmap_end + 1) >> PAGE_SHIFT;
		spin_lock(&inode->i_lock);
		inode->i_private = &shmem_falloc;
		spin_unlock(&inode->i_lock);

		if ((u64)unmap_end > (u64)unmap_start)
			unmap_mapping_range(mapping, unmap_start,
					    1 + unmap_end - unmap_start, 0);
		shmem_truncate_range(inode, offset, offset + len - 1);
		/* No need to unmap again: hole-punching leaves COWed pages */

		spin_lock(&inode->i_lock);
		inode->i_private = NULL;
		wake_up_all(&shmem_falloc_waitq);
		WARN_ON_ONCE(!list_empty(&shmem_falloc_waitq.head));
		spin_unlock(&inode->i_lock);
		error = 0;
		goto out;
	}

	/* We need to check rlimit even when FALLOC_FL_KEEP_SIZE */
	error = inode_newsize_ok(inode, offset + len);
	if (error)
		goto out;

	if ((info->seals & F_SEAL_GROW) && offset + len > inode->i_size) {
		error = -EPERM;
		goto out;
	}

	start = offset >> PAGE_SHIFT;
	end = (offset + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	/* Try to avoid a swapstorm if len is impossible to satisfy */
	if (sbinfo->max_blocks && end - start > sbinfo->max_blocks) {
		error = -ENOSPC;
		goto out;
	}

	shmem_falloc.waitq = NULL;
	shmem_falloc.start = start;
	shmem_falloc.next  = start;
	shmem_falloc.nr_falloced = 0;
	shmem_falloc.nr_unswapped = 0;
	spin_lock(&inode->i_lock);
	inode->i_private = &shmem_falloc;
	spin_unlock(&inode->i_lock);

	for (index = start; index < end; index++) {
		struct page *page;

		/*
		 * Good, the fallocate(2) manpage permits EINTR: we may have
		 * been interrupted because we are using up too much memory.
		 */
		if (signal_pending(current))
			error = -EINTR;
		else if (shmem_falloc.nr_unswapped > shmem_falloc.nr_falloced)
			error = -ENOMEM;
		else
			error = shmem_getpage(inode, index, &page, SGP_FALLOC);
		if (error) {
			/* Remove the !PageUptodate pages we added */
			if (index > start) {
				shmem_undo_range(inode,
				    (loff_t)start << PAGE_SHIFT,
				    ((loff_t)index << PAGE_SHIFT) - 1, true);
			}
			goto undone;
		}

		/*
		 * Inform shmem_writepage() how far we have reached.
		 * No need for lock or barrier: we have the page lock.
		 */
		shmem_falloc.next++;
		if (!PageUptodate(page))
			shmem_falloc.nr_falloced++;

		/*
		 * If !PageUptodate, leave it that way so that freeable pages
		 * can be recognized if we need to rollback on error later.
		 * But set_page_dirty so that memory pressure will swap rather
		 * than free the pages we are allocating (and SGP_CACHE pages
		 * might still be clean: we now need to mark those dirty too).
		 */
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);
		cond_resched();
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
	inode->i_ctime = current_time(inode);
undone:
	spin_lock(&inode->i_lock);
	inode->i_private = NULL;
	spin_unlock(&inode->i_lock);
out:
	inode_unlock(inode);
	return error;
}

static int shmem_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(dentry->d_sb);

	buf->f_type = TMPFS_MAGIC;
	buf->f_bsize = PAGE_SIZE;
	buf->f_namelen = NAME_MAX;
	if (sbinfo->max_blocks) {
		buf->f_blocks = sbinfo->max_blocks;
		buf->f_bavail =
		buf->f_bfree  = sbinfo->max_blocks -
				percpu_counter_sum(&sbinfo->used_blocks);
	}
	if (sbinfo->max_inodes) {
		buf->f_files = sbinfo->max_inodes;
		buf->f_ffree = sbinfo->free_inodes;
	}
	/* else leave those fields 0 like simple_statfs */
	return 0;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int
shmem_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = shmem_get_inode(dir->i_sb, dir, mode, dev, VM_NORESERVE);
	if (inode) {
		error = simple_acl_create(dir, inode);
		if (error)
			goto out_iput;
		error = security_inode_init_security(inode, dir,
						     &dentry->d_name,
						     shmem_initxattrs, NULL);
		if (error && error != -EOPNOTSUPP)
			goto out_iput;

		error = 0;
		dir->i_size += BOGO_DIRENT_SIZE;
		dir->i_ctime = dir->i_mtime = current_time(dir);
		d_instantiate(dentry, inode);
		dget(dentry); /* Extra count - pin the dentry in core */
	}
	return error;
out_iput:
	iput(inode);
	return error;
}

static int
shmem_tmpfile(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = shmem_get_inode(dir->i_sb, dir, mode, 0, VM_NORESERVE);
	if (inode) {
		error = security_inode_init_security(inode, dir,
						     NULL,
						     shmem_initxattrs, NULL);
		if (error && error != -EOPNOTSUPP)
			goto out_iput;
		error = simple_acl_create(dir, inode);
		if (error)
			goto out_iput;
		d_tmpfile(dentry, inode);
	}
	return error;
out_iput:
	iput(inode);
	return error;
}

static int shmem_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int error;

	if ((error = shmem_mknod(dir, dentry, mode | S_IFDIR, 0)))
		return error;
	inc_nlink(dir);
	return 0;
}

static int shmem_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return shmem_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	int ret = 0;

	/*
	 * No ordinary (disk based) filesystem counts links as inodes;
	 * but each new link needs a new dentry, pinning lowmem, and
	 * tmpfs dentries cannot be pruned until they are unlinked.
	 * But if an O_TMPFILE file is linked into the tmpfs, the
	 * first link must skip that, to get the accounting right.
	 */
	if (inode->i_nlink) {
		ret = shmem_reserve_inode(inode->i_sb, NULL);
		if (ret)
			goto out;
	}

	dir->i_size += BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
	inc_nlink(inode);
	ihold(inode);	/* New dentry reference */
	dget(dentry);		/* Extra pinning count for the created dentry */
	d_instantiate(dentry, inode);
out:
	return ret;
}

static int shmem_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_nlink > 1 && !S_ISDIR(inode->i_mode))
		shmem_free_inode(inode->i_sb);

	dir->i_size -= BOGO_DIRENT_SIZE;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
	drop_nlink(inode);
	dput(dentry);	/* Undo the count from "create" - this does all the work */
	return 0;
}

static int shmem_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	drop_nlink(d_inode(dentry));
	drop_nlink(dir);
	return shmem_unlink(dir, dentry);
}

static int shmem_exchange(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	bool old_is_dir = d_is_dir(old_dentry);
	bool new_is_dir = d_is_dir(new_dentry);

	if (old_dir != new_dir && old_is_dir != new_is_dir) {
		if (old_is_dir) {
			drop_nlink(old_dir);
			inc_nlink(new_dir);
		} else {
			drop_nlink(new_dir);
			inc_nlink(old_dir);
		}
	}
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	d_inode(old_dentry)->i_ctime =
	d_inode(new_dentry)->i_ctime = current_time(old_dir);

	return 0;
}

static int shmem_whiteout(struct inode *old_dir, struct dentry *old_dentry)
{
	struct dentry *whiteout;
	int error;

	whiteout = d_alloc(old_dentry->d_parent, &old_dentry->d_name);
	if (!whiteout)
		return -ENOMEM;

	error = shmem_mknod(old_dir, whiteout,
			    S_IFCHR | WHITEOUT_MODE, WHITEOUT_DEV);
	dput(whiteout);
	if (error)
		return error;

	/*
	 * Cheat and hash the whiteout while the old dentry is still in
	 * place, instead of playing games with FS_RENAME_DOES_D_MOVE.
	 *
	 * d_lookup() will consistently find one of them at this point,
	 * not sure which one, but that isn't even important.
	 */
	d_rehash(whiteout);
	return 0;
}

/*
 * The VFS layer already does all the dentry stuff for rename,
 * we just have to decrement the usage count for the target if
 * it exists so that the VFS layer correctly free's it when it
 * gets overwritten.
 */
static int shmem_rename2(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	int they_are_dirs = S_ISDIR(inode->i_mode);

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return shmem_exchange(old_dir, old_dentry, new_dir, new_dentry);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (flags & RENAME_WHITEOUT) {
		int error;

		error = shmem_whiteout(old_dir, old_dentry);
		if (error)
			return error;
	}

	if (d_really_is_positive(new_dentry)) {
		(void) shmem_unlink(new_dir, new_dentry);
		if (they_are_dirs) {
			drop_nlink(d_inode(new_dentry));
			drop_nlink(old_dir);
		}
	} else if (they_are_dirs) {
		drop_nlink(old_dir);
		inc_nlink(new_dir);
	}

	old_dir->i_size -= BOGO_DIRENT_SIZE;
	new_dir->i_size += BOGO_DIRENT_SIZE;
	old_dir->i_ctime = old_dir->i_mtime =
	new_dir->i_ctime = new_dir->i_mtime =
	inode->i_ctime = current_time(old_dir);
	return 0;
}

static int shmem_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int error;
	int len;
	struct inode *inode;
	struct page *page;

	len = strlen(symname) + 1;
	if (len > PAGE_SIZE)
		return -ENAMETOOLONG;

	inode = shmem_get_inode(dir->i_sb, dir, S_IFLNK | 0777, 0,
				VM_NORESERVE);
	if (!inode)
		return -ENOSPC;

	error = security_inode_init_security(inode, dir, &dentry->d_name,
					     shmem_initxattrs, NULL);
	if (error && error != -EOPNOTSUPP) {
		iput(inode);
		return error;
	}

	inode->i_size = len-1;
	if (len <= SHORT_SYMLINK_LEN) {
		inode->i_link = kmemdup(symname, len, GFP_KERNEL);
		if (!inode->i_link) {
			iput(inode);
			return -ENOMEM;
		}
		inode->i_op = &shmem_short_symlink_operations;
	} else {
		inode_nohighmem(inode);
		error = shmem_getpage(inode, 0, &page, SGP_WRITE);
		if (error) {
			iput(inode);
			return error;
		}
		inode->i_mapping->a_ops = &shmem_aops;
		inode->i_op = &shmem_symlink_inode_operations;
		memcpy(page_address(page), symname, len);
		SetPageUptodate(page);
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);
	}
	dir->i_size += BOGO_DIRENT_SIZE;
	dir->i_ctime = dir->i_mtime = current_time(dir);
	d_instantiate(dentry, inode);
	dget(dentry);
	return 0;
}

static void shmem_put_link(void *arg)
{
	mark_page_accessed(arg);
	put_page(arg);
}

static const char *shmem_get_link(struct dentry *dentry,
				  struct inode *inode,
				  struct delayed_call *done)
{
	struct page *page = NULL;
	int error;
	if (!dentry) {
		page = find_get_page(inode->i_mapping, 0);
		if (!page)
			return ERR_PTR(-ECHILD);
		if (!PageUptodate(page)) {
			put_page(page);
			return ERR_PTR(-ECHILD);
		}
	} else {
		error = shmem_getpage(inode, 0, &page, SGP_READ);
		if (error)
			return ERR_PTR(error);
		unlock_page(page);
	}
	set_delayed_call(done, shmem_put_link, page);
	return page_address(page);
}

#ifdef CONFIG_TMPFS_XATTR
/*
 * Superblocks without xattr inode operations may get some security.* xattr
 * support from the LSM "for free". As soon as we have any other xattrs
 * like ACLs, we also need to implement the security.* handlers at
 * filesystem level, though.
 */

/*
 * Callback for security_inode_init_security() for acquiring xattrs.
 */
static int shmem_initxattrs(struct inode *inode,
			    const struct xattr *xattr_array,
			    void *fs_info)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	const struct xattr *xattr;
	struct simple_xattr *new_xattr;
	size_t len;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		new_xattr = simple_xattr_alloc(xattr->value, xattr->value_len);
		if (!new_xattr)
			return -ENOMEM;

		len = strlen(xattr->name) + 1;
		new_xattr->name = kmalloc(XATTR_SECURITY_PREFIX_LEN + len,
					  GFP_KERNEL);
		if (!new_xattr->name) {
			kvfree(new_xattr);
			return -ENOMEM;
		}

		memcpy(new_xattr->name, XATTR_SECURITY_PREFIX,
		       XATTR_SECURITY_PREFIX_LEN);
		memcpy(new_xattr->name + XATTR_SECURITY_PREFIX_LEN,
		       xattr->name, len);

		simple_xattr_list_add(&info->xattrs, new_xattr);
	}

	return 0;
}

static int shmem_xattr_handler_get(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *name, void *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(inode);

	name = xattr_full_name(handler, name);
	return simple_xattr_get(&info->xattrs, name, buffer, size);
}

static int shmem_xattr_handler_set(const struct xattr_handler *handler,
				   struct dentry *unused, struct inode *inode,
				   const char *name, const void *value,
				   size_t size, int flags)
{
	struct shmem_inode_info *info = SHMEM_I(inode);

	name = xattr_full_name(handler, name);
	return simple_xattr_set(&info->xattrs, name, value, size, flags, NULL);
}

static const struct xattr_handler shmem_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = shmem_xattr_handler_get,
	.set = shmem_xattr_handler_set,
};

static const struct xattr_handler shmem_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = shmem_xattr_handler_get,
	.set = shmem_xattr_handler_set,
};

static const struct xattr_handler *shmem_xattr_handlers[] = {
#ifdef CONFIG_TMPFS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	&shmem_security_xattr_handler,
	&shmem_trusted_xattr_handler,
	NULL
};

static ssize_t shmem_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	return simple_xattr_list(d_inode(dentry), &info->xattrs, buffer, size);
}
#endif /* CONFIG_TMPFS_XATTR */

static const struct inode_operations shmem_short_symlink_operations = {
	.get_link	= simple_get_link,
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
#endif
};

static const struct inode_operations shmem_symlink_inode_operations = {
	.get_link	= shmem_get_link,
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
#endif
};

static struct dentry *shmem_get_parent(struct dentry *child)
{
	return ERR_PTR(-ESTALE);
}

static int shmem_match(struct inode *ino, void *vfh)
{
	__u32 *fh = vfh;
	__u64 inum = fh[2];
	inum = (inum << 32) | fh[1];
	return ino->i_ino == inum && fh[0] == ino->i_generation;
}

/* Find any alias of inode, but prefer a hashed alias */
static struct dentry *shmem_find_alias(struct inode *inode)
{
	struct dentry *alias = d_find_alias(inode);

	return alias ?: d_find_any_alias(inode);
}


static struct dentry *shmem_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct inode *inode;
	struct dentry *dentry = NULL;
	u64 inum;

	if (fh_len < 3)
		return NULL;

	inum = fid->raw[2];
	inum = (inum << 32) | fid->raw[1];

	inode = ilookup5(sb, (unsigned long)(inum + fid->raw[0]),
			shmem_match, fid->raw);
	if (inode) {
		dentry = shmem_find_alias(inode);
		iput(inode);
	}

	return dentry;
}

static int shmem_encode_fh(struct inode *inode, __u32 *fh, int *len,
				struct inode *parent)
{
	if (*len < 3) {
		*len = 3;
		return FILEID_INVALID;
	}

	if (inode_unhashed(inode)) {
		/* Unfortunately insert_inode_hash is not idempotent,
		 * so as we hash inodes here rather than at creation
		 * time, we need a lock to ensure we only try
		 * to do it once
		 */
		static DEFINE_SPINLOCK(lock);
		spin_lock(&lock);
		if (inode_unhashed(inode))
			__insert_inode_hash(inode,
					    inode->i_ino + inode->i_generation);
		spin_unlock(&lock);
	}

	fh[0] = inode->i_generation;
	fh[1] = inode->i_ino;
	fh[2] = ((__u64)inode->i_ino) >> 32;

	*len = 3;
	return 1;
}

static const struct export_operations shmem_export_ops = {
	.get_parent     = shmem_get_parent,
	.encode_fh      = shmem_encode_fh,
	.fh_to_dentry	= shmem_fh_to_dentry,
};

enum shmem_param {
	Opt_gid,
	Opt_huge,
	Opt_mode,
	Opt_mpol,
	Opt_nr_blocks,
	Opt_nr_inodes,
	Opt_size,
	Opt_uid,
	Opt_inode32,
	Opt_inode64,
};

static const struct constant_table shmem_param_enums_huge[] = {
	{"never",	SHMEM_HUGE_NEVER },
	{"always",	SHMEM_HUGE_ALWAYS },
	{"within_size",	SHMEM_HUGE_WITHIN_SIZE },
	{"advise",	SHMEM_HUGE_ADVISE },
	{}
};

const struct fs_parameter_spec shmem_fs_parameters[] = {
	fsparam_u32   ("gid",		Opt_gid),
	fsparam_enum  ("huge",		Opt_huge,  shmem_param_enums_huge),
	fsparam_u32oct("mode",		Opt_mode),
	fsparam_string("mpol",		Opt_mpol),
	fsparam_string("nr_blocks",	Opt_nr_blocks),
	fsparam_string("nr_inodes",	Opt_nr_inodes),
	fsparam_string("size",		Opt_size),
	fsparam_u32   ("uid",		Opt_uid),
	fsparam_flag  ("inode32",	Opt_inode32),
	fsparam_flag  ("inode64",	Opt_inode64),
	{}
};

static int shmem_parse_one(struct fs_context *fc, struct fs_parameter *param)
{
	struct shmem_options *ctx = fc->fs_private;
	struct fs_parse_result result;
	unsigned long long size;
	char *rest;
	int opt;

	opt = fs_parse(fc, shmem_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_size:
		size = memparse(param->string, &rest);
		if (*rest == '%') {
			size <<= PAGE_SHIFT;
			size *= totalram_pages();
			do_div(size, 100);
			rest++;
		}
		if (*rest)
			goto bad_value;
		ctx->blocks = DIV_ROUND_UP(size, PAGE_SIZE);
		ctx->seen |= SHMEM_SEEN_BLOCKS;
		break;
	case Opt_nr_blocks:
		ctx->blocks = memparse(param->string, &rest);
		if (*rest)
			goto bad_value;
		ctx->seen |= SHMEM_SEEN_BLOCKS;
		break;
	case Opt_nr_inodes:
		ctx->inodes = memparse(param->string, &rest);
		if (*rest)
			goto bad_value;
		ctx->seen |= SHMEM_SEEN_INODES;
		break;
	case Opt_mode:
		ctx->mode = result.uint_32 & 07777;
		break;
	case Opt_uid:
		ctx->uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(ctx->uid))
			goto bad_value;
		break;
	case Opt_gid:
		ctx->gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(ctx->gid))
			goto bad_value;
		break;
	case Opt_huge:
		ctx->huge = result.uint_32;
		if (ctx->huge != SHMEM_HUGE_NEVER &&
		    !(IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
		      has_transparent_hugepage()))
			goto unsupported_parameter;
		ctx->seen |= SHMEM_SEEN_HUGE;
		break;
	case Opt_mpol:
		if (IS_ENABLED(CONFIG_NUMA)) {
			mpol_put(ctx->mpol);
			ctx->mpol = NULL;
			if (mpol_parse_str(param->string, &ctx->mpol))
				goto bad_value;
			break;
		}
		goto unsupported_parameter;
	case Opt_inode32:
		ctx->full_inums = false;
		ctx->seen |= SHMEM_SEEN_INUMS;
		break;
	case Opt_inode64:
		if (sizeof(ino_t) < 8) {
			return invalfc(fc,
				       "Cannot use inode64 with <64bit inums in kernel\n");
		}
		ctx->full_inums = true;
		ctx->seen |= SHMEM_SEEN_INUMS;
		break;
	}
	return 0;

unsupported_parameter:
	return invalfc(fc, "Unsupported parameter '%s'", param->key);
bad_value:
	return invalfc(fc, "Bad value for '%s'", param->key);
}

static int shmem_parse_options(struct fs_context *fc, void *data)
{
	char *options = data;

	if (options) {
		int err = security_sb_eat_lsm_opts(options, &fc->security);
		if (err)
			return err;
	}

	while (options != NULL) {
		char *this_char = options;
		for (;;) {
			/*
			 * NUL-terminate this option: unfortunately,
			 * mount options form a comma-separated list,
			 * but mpol's nodelist may also contain commas.
			 */
			options = strchr(options, ',');
			if (options == NULL)
				break;
			options++;
			if (!isdigit(*options)) {
				options[-1] = '\0';
				break;
			}
		}
		if (*this_char) {
			char *value = strchr(this_char,'=');
			size_t len = 0;
			int err;

			if (value) {
				*value++ = '\0';
				len = strlen(value);
			}
			err = vfs_parse_fs_string(fc, this_char, value, len);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/*
 * Reconfigure a shmem filesystem.
 *
 * Note that we disallow change from limited->unlimited blocks/inodes while any
 * are in use; but we must separately disallow unlimited->limited, because in
 * that case we have no record of how much is already in use.
 */
static int shmem_reconfigure(struct fs_context *fc)
{
	struct shmem_options *ctx = fc->fs_private;
	struct shmem_sb_info *sbinfo = SHMEM_SB(fc->root->d_sb);
	unsigned long inodes;
	const char *err;

	spin_lock(&sbinfo->stat_lock);
	inodes = sbinfo->max_inodes - sbinfo->free_inodes;
	if ((ctx->seen & SHMEM_SEEN_BLOCKS) && ctx->blocks) {
		if (!sbinfo->max_blocks) {
			err = "Cannot retroactively limit size";
			goto out;
		}
		if (percpu_counter_compare(&sbinfo->used_blocks,
					   ctx->blocks) > 0) {
			err = "Too small a size for current use";
			goto out;
		}
	}
	if ((ctx->seen & SHMEM_SEEN_INODES) && ctx->inodes) {
		if (!sbinfo->max_inodes) {
			err = "Cannot retroactively limit inodes";
			goto out;
		}
		if (ctx->inodes < inodes) {
			err = "Too few inodes for current use";
			goto out;
		}
	}

	if ((ctx->seen & SHMEM_SEEN_INUMS) && !ctx->full_inums &&
	    sbinfo->next_ino > UINT_MAX) {
		err = "Current inum too high to switch to 32-bit inums";
		goto out;
	}

	if (ctx->seen & SHMEM_SEEN_HUGE)
		sbinfo->huge = ctx->huge;
	if (ctx->seen & SHMEM_SEEN_INUMS)
		sbinfo->full_inums = ctx->full_inums;
	if (ctx->seen & SHMEM_SEEN_BLOCKS)
		sbinfo->max_blocks  = ctx->blocks;
	if (ctx->seen & SHMEM_SEEN_INODES) {
		sbinfo->max_inodes  = ctx->inodes;
		sbinfo->free_inodes = ctx->inodes - inodes;
	}

	/*
	 * Preserve previous mempolicy unless mpol remount option was specified.
	 */
	if (ctx->mpol) {
		mpol_put(sbinfo->mpol);
		sbinfo->mpol = ctx->mpol;	/* transfers initial ref */
		ctx->mpol = NULL;
	}
	spin_unlock(&sbinfo->stat_lock);
	return 0;
out:
	spin_unlock(&sbinfo->stat_lock);
	return invalfc(fc, "%s", err);
}

static int shmem_show_options(struct seq_file *seq, struct dentry *root)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(root->d_sb);

	if (sbinfo->max_blocks != shmem_default_max_blocks())
		seq_printf(seq, ",size=%luk",
			sbinfo->max_blocks << (PAGE_SHIFT - 10));
	if (sbinfo->max_inodes != shmem_default_max_inodes())
		seq_printf(seq, ",nr_inodes=%lu", sbinfo->max_inodes);
	if (sbinfo->mode != (0777 | S_ISVTX))
		seq_printf(seq, ",mode=%03ho", sbinfo->mode);
	if (!uid_eq(sbinfo->uid, GLOBAL_ROOT_UID))
		seq_printf(seq, ",uid=%u",
				from_kuid_munged(&init_user_ns, sbinfo->uid));
	if (!gid_eq(sbinfo->gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u",
				from_kgid_munged(&init_user_ns, sbinfo->gid));

	/*
	 * Showing inode{64,32} might be useful even if it's the system default,
	 * since then people don't have to resort to checking both here and
	 * /proc/config.gz to confirm 64-bit inums were successfully applied
	 * (which may not even exist if IKCONFIG_PROC isn't enabled).
	 *
	 * We hide it when inode64 isn't the default and we are using 32-bit
	 * inodes, since that probably just means the feature isn't even under
	 * consideration.
	 *
	 * As such:
	 *
	 *                     +-----------------+-----------------+
	 *                     | TMPFS_INODE64=y | TMPFS_INODE64=n |
	 *  +------------------+-----------------+-----------------+
	 *  | full_inums=true  | show            | show            |
	 *  | full_inums=false | show            | hide            |
	 *  +------------------+-----------------+-----------------+
	 *
	 */
	if (IS_ENABLED(CONFIG_TMPFS_INODE64) || sbinfo->full_inums)
		seq_printf(seq, ",inode%d", (sbinfo->full_inums ? 64 : 32));
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/* Rightly or wrongly, show huge mount option unmasked by shmem_huge */
	if (sbinfo->huge)
		seq_printf(seq, ",huge=%s", shmem_format_huge(sbinfo->huge));
#endif
	shmem_show_mpol(seq, sbinfo->mpol);
	return 0;
}

#endif /* CONFIG_TMPFS */

static void shmem_put_super(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

	free_percpu(sbinfo->ino_batch);
	percpu_counter_destroy(&sbinfo->used_blocks);
	mpol_put(sbinfo->mpol);
	kfree(sbinfo);
	sb->s_fs_info = NULL;
}

static int shmem_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct shmem_options *ctx = fc->fs_private;
	struct inode *inode;
	struct shmem_sb_info *sbinfo;
	int err = -ENOMEM;

	/* Round up to L1_CACHE_BYTES to resist false sharing */
	sbinfo = kzalloc(max((int)sizeof(struct shmem_sb_info),
				L1_CACHE_BYTES), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;

	sb->s_fs_info = sbinfo;

#ifdef CONFIG_TMPFS
	/*
	 * Per default we only allow half of the physical ram per
	 * tmpfs instance, limiting inodes to one per page of lowmem;
	 * but the internal instance is left unlimited.
	 */
	if (!(sb->s_flags & SB_KERNMOUNT)) {
		if (!(ctx->seen & SHMEM_SEEN_BLOCKS))
			ctx->blocks = shmem_default_max_blocks();
		if (!(ctx->seen & SHMEM_SEEN_INODES))
			ctx->inodes = shmem_default_max_inodes();
		if (!(ctx->seen & SHMEM_SEEN_INUMS))
			ctx->full_inums = IS_ENABLED(CONFIG_TMPFS_INODE64);
	} else {
		sb->s_flags |= SB_NOUSER;
	}
	sb->s_export_op = &shmem_export_ops;
	sb->s_flags |= SB_NOSEC;
#else
	sb->s_flags |= SB_NOUSER;
#endif
	sbinfo->max_blocks = ctx->blocks;
	sbinfo->free_inodes = sbinfo->max_inodes = ctx->inodes;
	if (sb->s_flags & SB_KERNMOUNT) {
		sbinfo->ino_batch = alloc_percpu(ino_t);
		if (!sbinfo->ino_batch)
			goto failed;
	}
	sbinfo->uid = ctx->uid;
	sbinfo->gid = ctx->gid;
	sbinfo->full_inums = ctx->full_inums;
	sbinfo->mode = ctx->mode;
	sbinfo->huge = ctx->huge;
	sbinfo->mpol = ctx->mpol;
	ctx->mpol = NULL;

	spin_lock_init(&sbinfo->stat_lock);
	if (percpu_counter_init(&sbinfo->used_blocks, 0, GFP_KERNEL))
		goto failed;
	spin_lock_init(&sbinfo->shrinklist_lock);
	INIT_LIST_HEAD(&sbinfo->shrinklist);

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = TMPFS_MAGIC;
	sb->s_op = &shmem_ops;
	sb->s_time_gran = 1;
#ifdef CONFIG_TMPFS_XATTR
	sb->s_xattr = shmem_xattr_handlers;
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	sb->s_flags |= SB_POSIXACL;
#endif
	uuid_gen(&sb->s_uuid);

	inode = shmem_get_inode(sb, NULL, S_IFDIR | sbinfo->mode, 0, VM_NORESERVE);
	if (!inode)
		goto failed;
	inode->i_uid = sbinfo->uid;
	inode->i_gid = sbinfo->gid;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto failed;
	return 0;

failed:
	shmem_put_super(sb);
	return err;
}

static int shmem_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, shmem_fill_super);
}

static void shmem_free_fc(struct fs_context *fc)
{
	struct shmem_options *ctx = fc->fs_private;

	if (ctx) {
		mpol_put(ctx->mpol);
		kfree(ctx);
	}
}

static const struct fs_context_operations shmem_fs_context_ops = {
	.free			= shmem_free_fc,
	.get_tree		= shmem_get_tree,
#ifdef CONFIG_TMPFS
	.parse_monolithic	= shmem_parse_options,
	.parse_param		= shmem_parse_one,
	.reconfigure		= shmem_reconfigure,
#endif
};

static struct kmem_cache *shmem_inode_cachep;

static struct inode *shmem_alloc_inode(struct super_block *sb)
{
	struct shmem_inode_info *info;
	info = kmem_cache_alloc(shmem_inode_cachep, GFP_KERNEL);
	if (!info)
		return NULL;
	return &info->vfs_inode;
}

static void shmem_free_in_core_inode(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	kmem_cache_free(shmem_inode_cachep, SHMEM_I(inode));
}

static void shmem_destroy_inode(struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		mpol_free_shared_policy(&SHMEM_I(inode)->policy);
}

static void shmem_init_inode(void *foo)
{
	struct shmem_inode_info *info = foo;
	inode_init_once(&info->vfs_inode);
}

static void shmem_init_inodecache(void)
{
	shmem_inode_cachep = kmem_cache_create("shmem_inode_cache",
				sizeof(struct shmem_inode_info),
				0, SLAB_PANIC|SLAB_ACCOUNT, shmem_init_inode);
}

static void shmem_destroy_inodecache(void)
{
	kmem_cache_destroy(shmem_inode_cachep);
}

static const struct address_space_operations shmem_aops = {
	.writepage	= shmem_writepage,
	.set_page_dirty	= __set_page_dirty_no_writeback,
#ifdef CONFIG_TMPFS
	.write_begin	= shmem_write_begin,
	.write_end	= shmem_write_end,
#endif
#ifdef CONFIG_MIGRATION
	.migratepage	= migrate_page,
#endif
	.error_remove_page = generic_error_remove_page,
};

static const struct file_operations shmem_file_operations = {
	.mmap		= shmem_mmap,
	.get_unmapped_area = shmem_get_unmapped_area,
#ifdef CONFIG_TMPFS
	.llseek		= shmem_file_llseek,
	.read_iter	= shmem_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= shmem_fallocate,
#endif
};

static const struct inode_operations shmem_inode_operations = {
	.getattr	= shmem_getattr,
	.setattr	= shmem_setattr,
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_dir_inode_operations = {
#ifdef CONFIG_TMPFS
	.create		= shmem_create,
	.lookup		= simple_lookup,
	.link		= shmem_link,
	.unlink		= shmem_unlink,
	.symlink	= shmem_symlink,
	.mkdir		= shmem_mkdir,
	.rmdir		= shmem_rmdir,
	.mknod		= shmem_mknod,
	.rename		= shmem_rename2,
	.tmpfile	= shmem_tmpfile,
#endif
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_special_inode_operations = {
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct super_operations shmem_ops = {
	.alloc_inode	= shmem_alloc_inode,
	.free_inode	= shmem_free_in_core_inode,
	.destroy_inode	= shmem_destroy_inode,
#ifdef CONFIG_TMPFS
	.statfs		= shmem_statfs,
	.show_options	= shmem_show_options,
#endif
	.evict_inode	= shmem_evict_inode,
	.drop_inode	= generic_delete_inode,
	.put_super	= shmem_put_super,
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	.nr_cached_objects	= shmem_unused_huge_count,
	.free_cached_objects	= shmem_unused_huge_scan,
#endif
};

static const struct vm_operations_struct shmem_vm_ops = {
	.fault		= shmem_fault,
	.map_pages	= filemap_map_pages,
#ifdef CONFIG_NUMA
	.set_policy     = shmem_set_policy,
	.get_policy     = shmem_get_policy,
#endif
};

int shmem_init_fs_context(struct fs_context *fc)
{
	struct shmem_options *ctx;

	ctx = kzalloc(sizeof(struct shmem_options), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->mode = 0777 | S_ISVTX;
	ctx->uid = current_fsuid();
	ctx->gid = current_fsgid();

	fc->fs_private = ctx;
	fc->ops = &shmem_fs_context_ops;
	return 0;
}

static struct file_system_type shmem_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tmpfs",
	.init_fs_context = shmem_init_fs_context,
#ifdef CONFIG_TMPFS
	.parameters	= shmem_fs_parameters,
#endif
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT | FS_THP_SUPPORT,
};

int __init shmem_init(void)
{
	int error;

	shmem_init_inodecache();

	error = register_filesystem(&shmem_fs_type);
	if (error) {
		pr_err("Could not register tmpfs\n");
		goto out2;
	}

	shm_mnt = kern_mount(&shmem_fs_type);
	if (IS_ERR(shm_mnt)) {
		error = PTR_ERR(shm_mnt);
		pr_err("Could not kern_mount tmpfs\n");
		goto out1;
	}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (has_transparent_hugepage() && shmem_huge > SHMEM_HUGE_DENY)
		SHMEM_SB(shm_mnt->mnt_sb)->huge = shmem_huge;
	else
		shmem_huge = 0; /* just in case it was patched */
#endif
	return 0;

out1:
	unregister_filesystem(&shmem_fs_type);
out2:
	shmem_destroy_inodecache();
	shm_mnt = ERR_PTR(error);
	return error;
}

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && defined(CONFIG_SYSFS)
static ssize_t shmem_enabled_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	static const int values[] = {
		SHMEM_HUGE_ALWAYS,
		SHMEM_HUGE_WITHIN_SIZE,
		SHMEM_HUGE_ADVISE,
		SHMEM_HUGE_NEVER,
		SHMEM_HUGE_DENY,
		SHMEM_HUGE_FORCE,
	};
	int i, count;

	for (i = 0, count = 0; i < ARRAY_SIZE(values); i++) {
		const char *fmt = shmem_huge == values[i] ? "[%s] " : "%s ";

		count += sprintf(buf + count, fmt,
				shmem_format_huge(values[i]));
	}
	buf[count - 1] = '\n';
	return count;
}

static ssize_t shmem_enabled_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	char tmp[16];
	int huge;

	if (count + 1 > sizeof(tmp))
		return -EINVAL;
	memcpy(tmp, buf, count);
	tmp[count] = '\0';
	if (count && tmp[count - 1] == '\n')
		tmp[count - 1] = '\0';

	huge = shmem_parse_huge(tmp);
	if (huge == -EINVAL)
		return -EINVAL;
	if (!has_transparent_hugepage() &&
			huge != SHMEM_HUGE_NEVER && huge != SHMEM_HUGE_DENY)
		return -EINVAL;

	shmem_huge = huge;
	if (shmem_huge > SHMEM_HUGE_DENY)
		SHMEM_SB(shm_mnt->mnt_sb)->huge = shmem_huge;
	return count;
}

struct kobj_attribute shmem_enabled_attr =
	__ATTR(shmem_enabled, 0644, shmem_enabled_show, shmem_enabled_store);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE && CONFIG_SYSFS */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
bool shmem_huge_enabled(struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(vma->vm_file);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	loff_t i_size;
	pgoff_t off;

	if (!transhuge_vma_enabled(vma, vma->vm_flags))
		return false;
	if (shmem_huge == SHMEM_HUGE_FORCE)
		return true;
	if (shmem_huge == SHMEM_HUGE_DENY)
		return false;
	switch (sbinfo->huge) {
		case SHMEM_HUGE_NEVER:
			return false;
		case SHMEM_HUGE_ALWAYS:
			return true;
		case SHMEM_HUGE_WITHIN_SIZE:
			off = round_up(vma->vm_pgoff, HPAGE_PMD_NR);
			i_size = round_up(i_size_read(inode), PAGE_SIZE);
			if (i_size >= HPAGE_PMD_SIZE &&
					i_size >> PAGE_SHIFT >= off)
				return true;
			fallthrough;
		case SHMEM_HUGE_ADVISE:
			/* TODO: implement fadvise() hints */
			return (vma->vm_flags & VM_HUGEPAGE);
		default:
			VM_BUG_ON(1);
			return false;
	}
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#else /* !CONFIG_SHMEM */

/*
 * tiny-shmem: simple shmemfs and tmpfs using ramfs code
 *
 * This is intended for small system where the benefits of the full
 * shmem code (swap-backed and resource-limited) are outweighed by
 * their complexity. On systems without swap this code should be
 * effectively equivalent, but much lighter weight.
 */

static struct file_system_type shmem_fs_type = {
	.name		= "tmpfs",
	.init_fs_context = ramfs_init_fs_context,
	.parameters	= ramfs_fs_parameters,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

int __init shmem_init(void)
{
	BUG_ON(register_filesystem(&shmem_fs_type) != 0);

	shm_mnt = kern_mount(&shmem_fs_type);
	BUG_ON(IS_ERR(shm_mnt));

	return 0;
}

int shmem_unuse(unsigned int type, bool frontswap,
		unsigned long *fs_pages_to_unuse)
{
	return 0;
}

int shmem_lock(struct file *file, int lock, struct user_struct *user)
{
	return 0;
}

void shmem_unlock_mapping(struct address_space *mapping)
{
}

#ifdef CONFIG_MMU
unsigned long shmem_get_unmapped_area(struct file *file,
				      unsigned long addr, unsigned long len,
				      unsigned long pgoff, unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}
#endif

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	truncate_inode_pages_range(inode->i_mapping, lstart, lend);
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

#define shmem_vm_ops				generic_file_vm_ops
#define shmem_file_operations			ramfs_file_operations
#define shmem_get_inode(sb, dir, mode, dev, flags)	ramfs_get_inode(sb, dir, mode, dev)
#define shmem_acct_size(flags, size)		0
#define shmem_unacct_size(flags, size)		do {} while (0)

#endif /* CONFIG_SHMEM */

/* common code */

static struct file *__shmem_file_setup(struct vfsmount *mnt, const char *name, loff_t size,
				       unsigned long flags, unsigned int i_flags)
{
	struct inode *inode;
	struct file *res;

	if (IS_ERR(mnt))
		return ERR_CAST(mnt);

	if (size < 0 || size > MAX_LFS_FILESIZE)
		return ERR_PTR(-EINVAL);

	if (shmem_acct_size(flags, size))
		return ERR_PTR(-ENOMEM);

	inode = shmem_get_inode(mnt->mnt_sb, NULL, S_IFREG | S_IRWXUGO, 0,
				flags);
	if (unlikely(!inode)) {
		shmem_unacct_size(flags, size);
		return ERR_PTR(-ENOSPC);
	}
	inode->i_flags |= i_flags;
	inode->i_size = size;
	clear_nlink(inode);	/* It is unlinked */
	res = ERR_PTR(ramfs_nommu_expand_for_mapping(inode, size));
	if (!IS_ERR(res))
		res = alloc_file_pseudo(inode, mnt, name, O_RDWR,
				&shmem_file_operations);
	if (IS_ERR(res))
		iput(inode);
	return res;
}

/**
 * shmem_kernel_file_setup - get an unlinked file living in tmpfs which must be
 * 	kernel internal.  There will be NO LSM permission checks against the
 * 	underlying inode.  So users of this interface must do LSM checks at a
 *	higher layer.  The users are the big_key and shm implementations.  LSM
 *	checks are provided at the key or shm level rather than the inode.
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_kernel_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return __shmem_file_setup(shm_mnt, name, size, flags, S_PRIVATE);
}

/**
 * shmem_file_setup - get an unlinked file living in tmpfs
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_file_setup(const char *name, loff_t size, unsigned long flags)
{
	return __shmem_file_setup(shm_mnt, name, size, flags, 0);
}
EXPORT_SYMBOL_GPL(shmem_file_setup);

/**
 * shmem_file_setup_with_mnt - get an unlinked file living in tmpfs
 * @mnt: the tmpfs mount where the file will be created
 * @name: name for dentry (to be seen in /proc/<pid>/maps
 * @size: size to be set for the file
 * @flags: VM_NORESERVE suppresses pre-accounting of the entire object size
 */
struct file *shmem_file_setup_with_mnt(struct vfsmount *mnt, const char *name,
				       loff_t size, unsigned long flags)
{
	return __shmem_file_setup(mnt, name, size, flags, 0);
}
EXPORT_SYMBOL_GPL(shmem_file_setup_with_mnt);

/**
 * shmem_zero_setup - setup a shared anonymous mapping
 * @vma: the vma to be mmapped is prepared by do_mmap
 */
int shmem_zero_setup(struct vm_area_struct *vma)
{
	struct file *file;
	loff_t size = vma->vm_end - vma->vm_start;

	/*
	 * Cloning a new file under mmap_lock leads to a lock ordering conflict
	 * between XFS directory reading and selinux: since this file is only
	 * accessible to the user through its mapping, use S_PRIVATE flag to
	 * bypass file security, in the same way as shmem_kernel_file_setup().
	 */
	file = shmem_kernel_file_setup("dev/zero", size, vma->vm_flags);
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (vma->vm_file)
		fput(vma->vm_file);
	vma->vm_file = file;
	vma->vm_ops = &shmem_vm_ops;

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
			((vma->vm_start + ~HPAGE_PMD_MASK) & HPAGE_PMD_MASK) <
			(vma->vm_end & HPAGE_PMD_MASK)) {
		khugepaged_enter(vma, vma->vm_flags);
	}

	return 0;
}

/**
 * shmem_read_mapping_page_gfp - read into page cache, using specified page allocation flags.
 * @mapping:	the page's address_space
 * @index:	the page index
 * @gfp:	the page allocator flags to use if allocating
 *
 * This behaves as a tmpfs "read_cache_page_gfp(mapping, index, gfp)",
 * with any new page allocations done using the specified allocation flags.
 * But read_cache_page_gfp() uses the ->readpage() method: which does not
 * suit tmpfs, since it may have pages in swapcache, and needs to find those
 * for itself; although drivers/gpu/drm i915 and ttm rely upon this support.
 *
 * i915_gem_object_get_pages_gtt() mixes __GFP_NORETRY | __GFP_NOWARN in
 * with the mapping_gfp_mask(), to avoid OOMing the machine unnecessarily.
 */
struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					 pgoff_t index, gfp_t gfp)
{
#ifdef CONFIG_SHMEM
	struct inode *inode = mapping->host;
	struct page *page;
	int error;

	BUG_ON(mapping->a_ops != &shmem_aops);
	error = shmem_getpage_gfp(inode, index, &page, SGP_CACHE,
				  gfp, NULL, NULL, NULL);
	if (error)
		page = ERR_PTR(error);
	else
		unlock_page(page);
	return page;
#else
	/*
	 * The tiny !SHMEM case uses ramfs without swap
	 */
	return read_cache_page_gfp(mapping, index, gfp);
#endif
}
EXPORT_SYMBOL_GPL(shmem_read_mapping_page_gfp);
