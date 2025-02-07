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
#include <linux/fileattr.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched/signal.h>
#include <linux/export.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>
#include <linux/uio.h>
#include <linux/hugetlb.h>
#include <linux/fs_parser.h>
#include <linux/swapfile.h>
#include <linux/iversion.h>
#include <linux/unicode.h>
#include "swap.h"

static struct vfsmount *shm_mnt __ro_after_init;

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
#include <linux/writeback.h>
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
#include <linux/rmap.h>
#include <linux/uuid.h>
#include <linux/quotaops.h>
#include <linux/rcupdate_wait.h>

#include <linux/uaccess.h>

#include "internal.h"

#define VM_ACCT(size)    (PAGE_ALIGN(size) >> PAGE_SHIFT)

/* Pretend that each entry is of this size in directory's i_size */
#define BOGO_DIRENT_SIZE 20

/* Pretend that one inode + its dentry occupy this much memory */
#define BOGO_INODE_SIZE 1024

/* Symlink up to this size is kmalloc'ed instead of using a swappable page */
#define SHORT_SYMLINK_LEN 128

/*
 * shmem_fallocate communicates with shmem_fault or shmem_writepage via
 * inode->i_private (with i_rwsem making sure that it has only one user at
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
	bool noswap;
	unsigned short quota_types;
	struct shmem_quota_limits qlimits;
#if IS_ENABLED(CONFIG_UNICODE)
	struct unicode_map *encoding;
	bool strict_encoding;
#endif
#define SHMEM_SEEN_BLOCKS 1
#define SHMEM_SEEN_INODES 2
#define SHMEM_SEEN_HUGE 4
#define SHMEM_SEEN_INUMS 8
#define SHMEM_SEEN_NOSWAP 16
#define SHMEM_SEEN_QUOTA 32
};

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static unsigned long huge_shmem_orders_always __read_mostly;
static unsigned long huge_shmem_orders_madvise __read_mostly;
static unsigned long huge_shmem_orders_inherit __read_mostly;
static unsigned long huge_shmem_orders_within_size __read_mostly;
static bool shmem_orders_configured __initdata;
#endif

#ifdef CONFIG_TMPFS
static unsigned long shmem_default_max_blocks(void)
{
	return totalram_pages() / 2;
}

static unsigned long shmem_default_max_inodes(void)
{
	unsigned long nr_pages = totalram_pages();

	return min3(nr_pages - totalhigh_pages(), nr_pages / 2,
			ULONG_MAX / BOGO_INODE_SIZE);
}
#endif

static int shmem_swapin_folio(struct inode *inode, pgoff_t index,
			struct folio **foliop, enum sgp_type sgp, gfp_t gfp,
			struct vm_area_struct *vma, vm_fault_t *fault_type);

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
 * shmem_get_folio reports shmem_acct_blocks failure as -ENOSPC not -ENOMEM,
 * so that a failure on a sparse tmpfs mapping will give SIGBUS not OOM.
 */
static inline int shmem_acct_blocks(unsigned long flags, long pages)
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

static int shmem_inode_acct_blocks(struct inode *inode, long pages)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	int err = -ENOSPC;

	if (shmem_acct_blocks(info->flags, pages))
		return err;

	might_sleep();	/* when quotas */
	if (sbinfo->max_blocks) {
		if (!percpu_counter_limited_add(&sbinfo->used_blocks,
						sbinfo->max_blocks, pages))
			goto unacct;

		err = dquot_alloc_block_nodirty(inode, pages);
		if (err) {
			percpu_counter_sub(&sbinfo->used_blocks, pages);
			goto unacct;
		}
	} else {
		err = dquot_alloc_block_nodirty(inode, pages);
		if (err)
			goto unacct;
	}

	return 0;

unacct:
	shmem_unacct_blocks(info->flags, pages);
	return err;
}

static void shmem_inode_unacct_blocks(struct inode *inode, long pages)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);

	might_sleep();	/* when quotas */
	dquot_free_block_nodirty(inode, pages);

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
static const struct vm_operations_struct shmem_anon_vm_ops;
static struct file_system_type shmem_fs_type;

bool shmem_mapping(struct address_space *mapping)
{
	return mapping->a_ops == &shmem_aops;
}
EXPORT_SYMBOL_GPL(shmem_mapping);

bool vma_is_anon_shmem(struct vm_area_struct *vma)
{
	return vma->vm_ops == &shmem_anon_vm_ops;
}

bool vma_is_shmem(struct vm_area_struct *vma)
{
	return vma_is_anon_shmem(vma) || vma->vm_ops == &shmem_vm_ops;
}

static LIST_HEAD(shmem_swaplist);
static DEFINE_MUTEX(shmem_swaplist_mutex);

#ifdef CONFIG_TMPFS_QUOTA

static int shmem_enable_quotas(struct super_block *sb,
			       unsigned short quota_types)
{
	int type, err = 0;

	sb_dqopt(sb)->flags |= DQUOT_QUOTA_SYS_FILE | DQUOT_NOLIST_DIRTY;
	for (type = 0; type < SHMEM_MAXQUOTAS; type++) {
		if (!(quota_types & (1 << type)))
			continue;
		err = dquot_load_quota_sb(sb, type, QFMT_SHMEM,
					  DQUOT_USAGE_ENABLED |
					  DQUOT_LIMITS_ENABLED);
		if (err)
			goto out_err;
	}
	return 0;

out_err:
	pr_warn("tmpfs: failed to enable quota tracking (type=%d, err=%d)\n",
		type, err);
	for (type--; type >= 0; type--)
		dquot_quota_off(sb, type);
	return err;
}

static void shmem_disable_quotas(struct super_block *sb)
{
	int type;

	for (type = 0; type < SHMEM_MAXQUOTAS; type++)
		dquot_quota_off(sb, type);
}

static struct dquot __rcu **shmem_get_dquots(struct inode *inode)
{
	return SHMEM_I(inode)->i_dquot;
}
#endif /* CONFIG_TMPFS_QUOTA */

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
		raw_spin_lock(&sbinfo->stat_lock);
		if (sbinfo->max_inodes) {
			if (sbinfo->free_ispace < BOGO_INODE_SIZE) {
				raw_spin_unlock(&sbinfo->stat_lock);
				return -ENOSPC;
			}
			sbinfo->free_ispace -= BOGO_INODE_SIZE;
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
		raw_spin_unlock(&sbinfo->stat_lock);
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
			raw_spin_lock(&sbinfo->stat_lock);
			ino = sbinfo->next_ino;
			sbinfo->next_ino += SHMEM_INO_BATCH;
			raw_spin_unlock(&sbinfo->stat_lock);
			if (unlikely(is_zero_ino(ino)))
				ino++;
		}
		*inop = ino;
		*next_ino = ++ino;
		put_cpu();
	}

	return 0;
}

static void shmem_free_inode(struct super_block *sb, size_t freed_ispace)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	if (sbinfo->max_inodes) {
		raw_spin_lock(&sbinfo->stat_lock);
		sbinfo->free_ispace += BOGO_INODE_SIZE + freed_ispace;
		raw_spin_unlock(&sbinfo->stat_lock);
	}
}

/**
 * shmem_recalc_inode - recalculate the block usage of an inode
 * @inode: inode to recalc
 * @alloced: the change in number of pages allocated to inode
 * @swapped: the change in number of pages swapped from inode
 *
 * We have to calculate the free blocks since the mm can drop
 * undirtied hole pages behind our back.
 *
 * But normally   info->alloced == inode->i_mapping->nrpages + info->swapped
 * So mm freed is info->alloced - (inode->i_mapping->nrpages + info->swapped)
 */
static void shmem_recalc_inode(struct inode *inode, long alloced, long swapped)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	long freed;

	spin_lock(&info->lock);
	info->alloced += alloced;
	info->swapped += swapped;
	freed = info->alloced - info->swapped -
		READ_ONCE(inode->i_mapping->nrpages);
	/*
	 * Special case: whereas normally shmem_recalc_inode() is called
	 * after i_mapping->nrpages has already been adjusted (up or down),
	 * shmem_writepage() has to raise swapped before nrpages is lowered -
	 * to stop a racing shmem_recalc_inode() from thinking that a page has
	 * been freed.  Compensate here, to avoid the need for a followup call.
	 */
	if (swapped > 0)
		freed += swapped;
	if (freed > 0)
		info->alloced -= freed;
	spin_unlock(&info->lock);

	/* The quota case may block */
	if (freed > 0)
		shmem_inode_unacct_blocks(inode, freed);
}

bool shmem_charge(struct inode *inode, long pages)
{
	struct address_space *mapping = inode->i_mapping;

	if (shmem_inode_acct_blocks(inode, pages))
		return false;

	/* nrpages adjustment first, then shmem_recalc_inode() when balanced */
	xa_lock_irq(&mapping->i_pages);
	mapping->nrpages += pages;
	xa_unlock_irq(&mapping->i_pages);

	shmem_recalc_inode(inode, pages, 0);
	return true;
}

void shmem_uncharge(struct inode *inode, long pages)
{
	/* pages argument is currently unused: keep it to help debugging */
	/* nrpages adjustment done by __filemap_remove_folio() or caller */

	shmem_recalc_inode(inode, 0, 0);
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
 * Checking folio is not enough: by the time a swapcache folio is locked, it
 * might be reused, and again be swapcache, using the same swap as before.
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
 *	also respect madvise() hints;
 * SHMEM_HUGE_ADVISE:
 *	only allocate huge pages if requested with madvise();
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

static int shmem_huge __read_mostly = SHMEM_HUGE_NEVER;
static int tmpfs_huge __read_mostly = SHMEM_HUGE_NEVER;

/**
 * shmem_mapping_size_orders - Get allowable folio orders for the given file size.
 * @mapping: Target address_space.
 * @index: The page index.
 * @write_end: end of a write, could extend inode size.
 *
 * This returns huge orders for folios (when supported) based on the file size
 * which the mapping currently allows at the given index. The index is relevant
 * due to alignment considerations the mapping might have. The returned order
 * may be less than the size passed.
 *
 * Return: The orders.
 */
static inline unsigned int
shmem_mapping_size_orders(struct address_space *mapping, pgoff_t index, loff_t write_end)
{
	unsigned int order;
	size_t size;

	if (!mapping_large_folio_support(mapping) || !write_end)
		return 0;

	/* Calculate the write size based on the write_end */
	size = write_end - (index << PAGE_SHIFT);
	order = filemap_get_order(size);
	if (!order)
		return 0;

	/* If we're not aligned, allocate a smaller folio */
	if (index & ((1UL << order) - 1))
		order = __ffs(index);

	order = min_t(size_t, order, MAX_PAGECACHE_ORDER);
	return order > 0 ? BIT(order + 1) - 1 : 0;
}

static unsigned int shmem_huge_global_enabled(struct inode *inode, pgoff_t index,
					      loff_t write_end, bool shmem_huge_force,
					      struct vm_area_struct *vma,
					      unsigned long vm_flags)
{
	unsigned int maybe_pmd_order = HPAGE_PMD_ORDER > MAX_PAGECACHE_ORDER ?
		0 : BIT(HPAGE_PMD_ORDER);
	unsigned long within_size_orders;
	unsigned int order;
	pgoff_t aligned_index;
	loff_t i_size;

	if (!S_ISREG(inode->i_mode))
		return 0;
	if (shmem_huge == SHMEM_HUGE_DENY)
		return 0;
	if (shmem_huge_force || shmem_huge == SHMEM_HUGE_FORCE)
		return maybe_pmd_order;

	/*
	 * The huge order allocation for anon shmem is controlled through
	 * the mTHP interface, so we still use PMD-sized huge order to
	 * check whether global control is enabled.
	 *
	 * For tmpfs mmap()'s huge order, we still use PMD-sized order to
	 * allocate huge pages due to lack of a write size hint.
	 *
	 * Otherwise, tmpfs will allow getting a highest order hint based on
	 * the size of write and fallocate paths, then will try each allowable
	 * huge orders.
	 */
	switch (SHMEM_SB(inode->i_sb)->huge) {
	case SHMEM_HUGE_ALWAYS:
		if (vma)
			return maybe_pmd_order;

		return shmem_mapping_size_orders(inode->i_mapping, index, write_end);
	case SHMEM_HUGE_WITHIN_SIZE:
		if (vma)
			within_size_orders = maybe_pmd_order;
		else
			within_size_orders = shmem_mapping_size_orders(inode->i_mapping,
								       index, write_end);

		order = highest_order(within_size_orders);
		while (within_size_orders) {
			aligned_index = round_up(index + 1, 1 << order);
			i_size = max(write_end, i_size_read(inode));
			i_size = round_up(i_size, PAGE_SIZE);
			if (i_size >> PAGE_SHIFT >= aligned_index)
				return within_size_orders;

			order = next_order(&within_size_orders, order);
		}
		fallthrough;
	case SHMEM_HUGE_ADVISE:
		if (vm_flags & VM_HUGEPAGE)
			return maybe_pmd_order;
		fallthrough;
	default:
		return 0;
	}
}

static int shmem_parse_huge(const char *str)
{
	int huge;

	if (!str)
		return -EINVAL;

	if (!strcmp(str, "never"))
		huge = SHMEM_HUGE_NEVER;
	else if (!strcmp(str, "always"))
		huge = SHMEM_HUGE_ALWAYS;
	else if (!strcmp(str, "within_size"))
		huge = SHMEM_HUGE_WITHIN_SIZE;
	else if (!strcmp(str, "advise"))
		huge = SHMEM_HUGE_ADVISE;
	else if (!strcmp(str, "deny"))
		huge = SHMEM_HUGE_DENY;
	else if (!strcmp(str, "force"))
		huge = SHMEM_HUGE_FORCE;
	else
		return -EINVAL;

	if (!has_transparent_hugepage() &&
	    huge != SHMEM_HUGE_NEVER && huge != SHMEM_HUGE_DENY)
		return -EINVAL;

	/* Do not override huge allocation policy with non-PMD sized mTHP */
	if (huge == SHMEM_HUGE_FORCE &&
	    huge_shmem_orders_inherit != BIT(HPAGE_PMD_ORDER))
		return -EINVAL;

	return huge;
}

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
		struct shrink_control *sc, unsigned long nr_to_free)
{
	LIST_HEAD(list), *pos, *next;
	struct inode *inode;
	struct shmem_inode_info *info;
	struct folio *folio;
	unsigned long batch = sc ? sc->nr_to_scan : 128;
	unsigned long split = 0, freed = 0;

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
			goto next;
		}

		list_move(&info->shrinklist, &list);
next:
		sbinfo->shrinklist_len--;
		if (!--batch)
			break;
	}
	spin_unlock(&sbinfo->shrinklist_lock);

	list_for_each_safe(pos, next, &list) {
		pgoff_t next, end;
		loff_t i_size;
		int ret;

		info = list_entry(pos, struct shmem_inode_info, shrinklist);
		inode = &info->vfs_inode;

		if (nr_to_free && freed >= nr_to_free)
			goto move_back;

		i_size = i_size_read(inode);
		folio = filemap_get_entry(inode->i_mapping, i_size / PAGE_SIZE);
		if (!folio || xa_is_value(folio))
			goto drop;

		/* No large folio at the end of the file: nothing to split */
		if (!folio_test_large(folio)) {
			folio_put(folio);
			goto drop;
		}

		/* Check if there is anything to gain from splitting */
		next = folio_next_index(folio);
		end = shmem_fallocend(inode, DIV_ROUND_UP(i_size, PAGE_SIZE));
		if (end <= folio->index || end >= next) {
			folio_put(folio);
			goto drop;
		}

		/*
		 * Move the inode on the list back to shrinklist if we failed
		 * to lock the page at this time.
		 *
		 * Waiting for the lock may lead to deadlock in the
		 * reclaim path.
		 */
		if (!folio_trylock(folio)) {
			folio_put(folio);
			goto move_back;
		}

		ret = split_folio(folio);
		folio_unlock(folio);
		folio_put(folio);

		/* If split failed move the inode on the list back to shrinklist */
		if (ret)
			goto move_back;

		freed += next - end;
		split++;
drop:
		list_del_init(&info->shrinklist);
		goto put;
move_back:
		/*
		 * Make sure the inode is either on the global list or deleted
		 * from any local list before iput() since it could be deleted
		 * in another thread once we put the inode (then the local list
		 * is corrupted).
		 */
		spin_lock(&sbinfo->shrinklist_lock);
		list_move(&info->shrinklist, &sbinfo->shrinklist);
		sbinfo->shrinklist_len++;
		spin_unlock(&sbinfo->shrinklist_lock);
put:
		iput(inode);
	}

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
		struct shrink_control *sc, unsigned long nr_to_free)
{
	return 0;
}

static unsigned int shmem_huge_global_enabled(struct inode *inode, pgoff_t index,
					      loff_t write_end, bool shmem_huge_force,
					      struct vm_area_struct *vma,
					      unsigned long vm_flags)
{
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static void shmem_update_stats(struct folio *folio, int nr_pages)
{
	if (folio_test_pmd_mappable(folio))
		__lruvec_stat_mod_folio(folio, NR_SHMEM_THPS, nr_pages);
	__lruvec_stat_mod_folio(folio, NR_FILE_PAGES, nr_pages);
	__lruvec_stat_mod_folio(folio, NR_SHMEM, nr_pages);
}

/*
 * Somewhat like filemap_add_folio, but error if expected item has gone.
 */
static int shmem_add_to_page_cache(struct folio *folio,
				   struct address_space *mapping,
				   pgoff_t index, void *expected, gfp_t gfp)
{
	XA_STATE_ORDER(xas, &mapping->i_pages, index, folio_order(folio));
	long nr = folio_nr_pages(folio);

	VM_BUG_ON_FOLIO(index != round_down(index, nr), folio);
	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_FOLIO(!folio_test_swapbacked(folio), folio);

	folio_ref_add(folio, nr);
	folio->mapping = mapping;
	folio->index = index;

	gfp &= GFP_RECLAIM_MASK;
	folio_throttle_swaprate(folio, gfp);

	do {
		xas_lock_irq(&xas);
		if (expected != xas_find_conflict(&xas)) {
			xas_set_err(&xas, -EEXIST);
			goto unlock;
		}
		if (expected && xas_find_conflict(&xas)) {
			xas_set_err(&xas, -EEXIST);
			goto unlock;
		}
		xas_store(&xas, folio);
		if (xas_error(&xas))
			goto unlock;
		shmem_update_stats(folio, nr);
		mapping->nrpages += nr;
unlock:
		xas_unlock_irq(&xas);
	} while (xas_nomem(&xas, gfp));

	if (xas_error(&xas)) {
		folio->mapping = NULL;
		folio_ref_sub(folio, nr);
		return xas_error(&xas);
	}

	return 0;
}

/*
 * Somewhat like filemap_remove_folio, but substitutes swap for @folio.
 */
static void shmem_delete_from_page_cache(struct folio *folio, void *radswap)
{
	struct address_space *mapping = folio->mapping;
	long nr = folio_nr_pages(folio);
	int error;

	xa_lock_irq(&mapping->i_pages);
	error = shmem_replace_entry(mapping, folio->index, folio, radswap);
	folio->mapping = NULL;
	mapping->nrpages -= nr;
	shmem_update_stats(folio, -nr);
	xa_unlock_irq(&mapping->i_pages);
	folio_put_refs(folio, nr);
	BUG_ON(error);
}

/*
 * Remove swap entry from page cache, free the swap and its page cache. Returns
 * the number of pages being freed. 0 means entry not found in XArray (0 pages
 * being freed).
 */
static long shmem_free_swap(struct address_space *mapping,
			    pgoff_t index, void *radswap)
{
	int order = xa_get_order(&mapping->i_pages, index);
	void *old;

	old = xa_cmpxchg_irq(&mapping->i_pages, index, radswap, NULL, 0);
	if (old != radswap)
		return 0;
	free_swap_and_cache_nr(radix_to_swp_entry(radswap), 1 << order);

	return 1 << order;
}

/*
 * Determine (in bytes) how many of the shmem object's pages mapped by the
 * given offsets are swapped out.
 *
 * This is safe to call without i_rwsem or the i_pages lock thanks to RCU,
 * as long as the inode doesn't go away and racy results are not a problem.
 */
unsigned long shmem_partial_swap_usage(struct address_space *mapping,
						pgoff_t start, pgoff_t end)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct page *page;
	unsigned long swapped = 0;
	unsigned long max = end - 1;

	rcu_read_lock();
	xas_for_each(&xas, page, max) {
		if (xas_retry(&xas, page))
			continue;
		if (xa_is_value(page))
			swapped += 1 << xas_get_order(&xas);
		if (xas.xa_index == max)
			break;
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
 * This is safe to call without i_rwsem or the i_pages lock thanks to RCU,
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
	return shmem_partial_swap_usage(mapping, vma->vm_pgoff,
					vma->vm_pgoff + vma_pages(vma));
}

/*
 * SysV IPC SHM_UNLOCK restore Unevictable pages to their evictable lists.
 */
void shmem_unlock_mapping(struct address_space *mapping)
{
	struct folio_batch fbatch;
	pgoff_t index = 0;

	folio_batch_init(&fbatch);
	/*
	 * Minor point, but we might as well stop if someone else SHM_LOCKs it.
	 */
	while (!mapping_unevictable(mapping) &&
	       filemap_get_folios(mapping, &index, ~0UL, &fbatch)) {
		check_move_unevictable_folios(&fbatch);
		folio_batch_release(&fbatch);
		cond_resched();
	}
}

static struct folio *shmem_get_partial_folio(struct inode *inode, pgoff_t index)
{
	struct folio *folio;

	/*
	 * At first avoid shmem_get_folio(,,,SGP_READ): that fails
	 * beyond i_size, and reports fallocated folios as holes.
	 */
	folio = filemap_get_entry(inode->i_mapping, index);
	if (!folio)
		return folio;
	if (!xa_is_value(folio)) {
		folio_lock(folio);
		if (folio->mapping == inode->i_mapping)
			return folio;
		/* The folio has been swapped out */
		folio_unlock(folio);
		folio_put(folio);
	}
	/*
	 * But read a folio back from swap if any of it is within i_size
	 * (although in some cases this is just a waste of time).
	 */
	folio = NULL;
	shmem_get_folio(inode, index, 0, &folio, SGP_READ);
	return folio;
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
	struct folio_batch fbatch;
	pgoff_t indices[PAGEVEC_SIZE];
	struct folio *folio;
	bool same_folio;
	long nr_swaps_freed = 0;
	pgoff_t index;
	int i;

	if (lend == -1)
		end = -1;	/* unsigned, so actually very big */

	if (info->fallocend > start && info->fallocend <= end && !unfalloc)
		info->fallocend = start;

	folio_batch_init(&fbatch);
	index = start;
	while (index < end && find_lock_entries(mapping, &index, end - 1,
			&fbatch, indices)) {
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			folio = fbatch.folios[i];

			if (xa_is_value(folio)) {
				if (unfalloc)
					continue;
				nr_swaps_freed += shmem_free_swap(mapping,
							indices[i], folio);
				continue;
			}

			if (!unfalloc || !folio_test_uptodate(folio))
				truncate_inode_folio(mapping, folio);
			folio_unlock(folio);
		}
		folio_batch_remove_exceptionals(&fbatch);
		folio_batch_release(&fbatch);
		cond_resched();
	}

	/*
	 * When undoing a failed fallocate, we want none of the partial folio
	 * zeroing and splitting below, but shall want to truncate the whole
	 * folio when !uptodate indicates that it was added by this fallocate,
	 * even when [lstart, lend] covers only a part of the folio.
	 */
	if (unfalloc)
		goto whole_folios;

	same_folio = (lstart >> PAGE_SHIFT) == (lend >> PAGE_SHIFT);
	folio = shmem_get_partial_folio(inode, lstart >> PAGE_SHIFT);
	if (folio) {
		same_folio = lend < folio_pos(folio) + folio_size(folio);
		folio_mark_dirty(folio);
		if (!truncate_inode_partial_folio(folio, lstart, lend)) {
			start = folio_next_index(folio);
			if (same_folio)
				end = folio->index;
		}
		folio_unlock(folio);
		folio_put(folio);
		folio = NULL;
	}

	if (!same_folio)
		folio = shmem_get_partial_folio(inode, lend >> PAGE_SHIFT);
	if (folio) {
		folio_mark_dirty(folio);
		if (!truncate_inode_partial_folio(folio, lstart, lend))
			end = folio->index;
		folio_unlock(folio);
		folio_put(folio);
	}

whole_folios:

	index = start;
	while (index < end) {
		cond_resched();

		if (!find_get_entries(mapping, &index, end - 1, &fbatch,
				indices)) {
			/* If all gone or hole-punch or unfalloc, we're done */
			if (index == start || end != -1)
				break;
			/* But if truncating, restart to make sure all gone */
			index = start;
			continue;
		}
		for (i = 0; i < folio_batch_count(&fbatch); i++) {
			folio = fbatch.folios[i];

			if (xa_is_value(folio)) {
				long swaps_freed;

				if (unfalloc)
					continue;
				swaps_freed = shmem_free_swap(mapping, indices[i], folio);
				if (!swaps_freed) {
					/* Swap was replaced by page: retry */
					index = indices[i];
					break;
				}
				nr_swaps_freed += swaps_freed;
				continue;
			}

			folio_lock(folio);

			if (!unfalloc || !folio_test_uptodate(folio)) {
				if (folio_mapping(folio) != mapping) {
					/* Page was replaced by swap: retry */
					folio_unlock(folio);
					index = indices[i];
					break;
				}
				VM_BUG_ON_FOLIO(folio_test_writeback(folio),
						folio);

				if (!folio_test_large(folio)) {
					truncate_inode_folio(mapping, folio);
				} else if (truncate_inode_partial_folio(folio, lstart, lend)) {
					/*
					 * If we split a page, reset the loop so
					 * that we pick up the new sub pages.
					 * Otherwise the THP was entirely
					 * dropped or the target range was
					 * zeroed, so just continue the loop as
					 * is.
					 */
					if (!folio_test_large(folio)) {
						folio_unlock(folio);
						index = start;
						break;
					}
				}
			}
			folio_unlock(folio);
		}
		folio_batch_remove_exceptionals(&fbatch);
		folio_batch_release(&fbatch);
	}

	shmem_recalc_inode(inode, 0, -nr_swaps_freed);
}

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	shmem_undo_range(inode, lstart, lend, false);
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	inode_inc_iversion(inode);
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

static int shmem_getattr(struct mnt_idmap *idmap,
			 const struct path *path, struct kstat *stat,
			 u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = path->dentry->d_inode;
	struct shmem_inode_info *info = SHMEM_I(inode);

	if (info->alloced - info->swapped != inode->i_mapping->nrpages)
		shmem_recalc_inode(inode, 0, 0);

	if (info->fsflags & FS_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (info->fsflags & FS_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (info->fsflags & FS_NODUMP_FL)
		stat->attributes |= STATX_ATTR_NODUMP;
	stat->attributes_mask |= (STATX_ATTR_APPEND |
			STATX_ATTR_IMMUTABLE |
			STATX_ATTR_NODUMP);
	generic_fillattr(idmap, request_mask, inode, stat);

	if (shmem_huge_global_enabled(inode, 0, 0, false, NULL, 0))
		stat->blksize = HPAGE_PMD_SIZE;

	if (request_mask & STATX_BTIME) {
		stat->result_mask |= STATX_BTIME;
		stat->btime.tv_sec = info->i_crtime.tv_sec;
		stat->btime.tv_nsec = info->i_crtime.tv_nsec;
	}

	return 0;
}

static int shmem_setattr(struct mnt_idmap *idmap,
			 struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int error;
	bool update_mtime = false;
	bool update_ctime = true;

	error = setattr_prepare(idmap, dentry, attr);
	if (error)
		return error;

	if ((info->seals & F_SEAL_EXEC) && (attr->ia_valid & ATTR_MODE)) {
		if ((inode->i_mode ^ attr->ia_mode) & 0111) {
			return -EPERM;
		}
	}

	if (S_ISREG(inode->i_mode) && (attr->ia_valid & ATTR_SIZE)) {
		loff_t oldsize = inode->i_size;
		loff_t newsize = attr->ia_size;

		/* protected by i_rwsem */
		if ((newsize < oldsize && (info->seals & F_SEAL_SHRINK)) ||
		    (newsize > oldsize && (info->seals & F_SEAL_GROW)))
			return -EPERM;

		if (newsize != oldsize) {
			error = shmem_reacct_size(SHMEM_I(inode)->flags,
					oldsize, newsize);
			if (error)
				return error;
			i_size_write(inode, newsize);
			update_mtime = true;
		} else {
			update_ctime = false;
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
		}
	}

	if (is_quota_modification(idmap, inode, attr)) {
		error = dquot_initialize(inode);
		if (error)
			return error;
	}

	/* Transfer quota accounting */
	if (i_uid_needs_update(idmap, attr, inode) ||
	    i_gid_needs_update(idmap, attr, inode)) {
		error = dquot_transfer(idmap, inode, attr);
		if (error)
			return error;
	}

	setattr_copy(idmap, inode, attr);
	if (attr->ia_valid & ATTR_MODE)
		error = posix_acl_chmod(idmap, dentry, inode->i_mode);
	if (!error && update_ctime) {
		inode_set_ctime_current(inode);
		if (update_mtime)
			inode_set_mtime_to_ts(inode, inode_get_ctime(inode));
		inode_inc_iversion(inode);
	}
	return error;
}

static void shmem_evict_inode(struct inode *inode)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	size_t freed = 0;

	if (shmem_mapping(inode->i_mapping)) {
		shmem_unacct_size(info->flags, inode->i_size);
		inode->i_size = 0;
		mapping_set_exiting(inode->i_mapping);
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

	simple_xattrs_free(&info->xattrs, sbinfo->max_inodes ? &freed : NULL);
	shmem_free_inode(inode->i_sb, freed);
	WARN_ON(inode->i_blocks);
	clear_inode(inode);
#ifdef CONFIG_TMPFS_QUOTA
	dquot_free_inode(inode);
	dquot_drop(inode);
#endif
}

static unsigned int shmem_find_swap_entries(struct address_space *mapping,
				pgoff_t start, struct folio_batch *fbatch,
				pgoff_t *indices, unsigned int type)
{
	XA_STATE(xas, &mapping->i_pages, start);
	struct folio *folio;
	swp_entry_t entry;

	rcu_read_lock();
	xas_for_each(&xas, folio, ULONG_MAX) {
		if (xas_retry(&xas, folio))
			continue;

		if (!xa_is_value(folio))
			continue;

		entry = radix_to_swp_entry(folio);
		/*
		 * swapin error entries can be found in the mapping. But they're
		 * deliberately ignored here as we've done everything we can do.
		 */
		if (swp_type(entry) != type)
			continue;

		indices[folio_batch_count(fbatch)] = xas.xa_index;
		if (!folio_batch_add(fbatch, folio))
			break;

		if (need_resched()) {
			xas_pause(&xas);
			cond_resched_rcu();
		}
	}
	rcu_read_unlock();

	return folio_batch_count(fbatch);
}

/*
 * Move the swapped pages for an inode to page cache. Returns the count
 * of pages swapped in, or the error in case of failure.
 */
static int shmem_unuse_swap_entries(struct inode *inode,
		struct folio_batch *fbatch, pgoff_t *indices)
{
	int i = 0;
	int ret = 0;
	int error = 0;
	struct address_space *mapping = inode->i_mapping;

	for (i = 0; i < folio_batch_count(fbatch); i++) {
		struct folio *folio = fbatch->folios[i];

		if (!xa_is_value(folio))
			continue;
		error = shmem_swapin_folio(inode, indices[i], &folio, SGP_CACHE,
					mapping_gfp_mask(mapping), NULL, NULL);
		if (error == 0) {
			folio_unlock(folio);
			folio_put(folio);
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
static int shmem_unuse_inode(struct inode *inode, unsigned int type)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t start = 0;
	struct folio_batch fbatch;
	pgoff_t indices[PAGEVEC_SIZE];
	int ret = 0;

	do {
		folio_batch_init(&fbatch);
		if (!shmem_find_swap_entries(mapping, start, &fbatch,
					     indices, type)) {
			ret = 0;
			break;
		}

		ret = shmem_unuse_swap_entries(inode, &fbatch, indices);
		if (ret < 0)
			break;

		start = indices[folio_batch_count(&fbatch) - 1];
	} while (true);

	return ret;
}

/*
 * Read all the shared memory data that resides in the swap
 * device 'type' back into memory, so the swap device can be
 * unused.
 */
int shmem_unuse(unsigned int type)
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

		error = shmem_unuse_inode(&info->vfs_inode, type);
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
	struct folio *folio = page_folio(page);
	struct address_space *mapping = folio->mapping;
	struct inode *inode = mapping->host;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	pgoff_t index;
	int nr_pages;
	bool split = false;

	/*
	 * Our capabilities prevent regular writeback or sync from ever calling
	 * shmem_writepage; but a stacking filesystem might use ->writepage of
	 * its underlying filesystem, in which case tmpfs should write out to
	 * swap only in response to memory pressure, and not for the writeback
	 * threads or sync.
	 */
	if (WARN_ON_ONCE(!wbc->for_reclaim))
		goto redirty;

	if ((info->flags & VM_LOCKED) || sbinfo->noswap)
		goto redirty;

	if (!total_swap_pages)
		goto redirty;

	/*
	 * If CONFIG_THP_SWAP is not enabled, the large folio should be
	 * split when swapping.
	 *
	 * And shrinkage of pages beyond i_size does not split swap, so
	 * swapout of a large folio crossing i_size needs to split too
	 * (unless fallocate has been used to preallocate beyond EOF).
	 */
	if (folio_test_large(folio)) {
		index = shmem_fallocend(inode,
			DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE));
		if ((index > folio->index && index < folio_next_index(folio)) ||
		    !IS_ENABLED(CONFIG_THP_SWAP))
			split = true;
	}

	if (split) {
try_split:
		/* Ensure the subpages are still dirty */
		folio_test_set_dirty(folio);
		if (split_huge_page_to_list_to_order(page, wbc->list, 0))
			goto redirty;
		folio = page_folio(page);
		folio_clear_dirty(folio);
	}

	index = folio->index;
	nr_pages = folio_nr_pages(folio);

	/*
	 * This is somewhat ridiculous, but without plumbing a SWAP_MAP_FALLOC
	 * value into swapfile.c, the only way we can correctly account for a
	 * fallocated folio arriving here is now to initialize it and write it.
	 *
	 * That's okay for a folio already fallocated earlier, but if we have
	 * not yet completed the fallocation, then (a) we want to keep track
	 * of this folio in case we have to undo it, and (b) it may not be a
	 * good idea to continue anyway, once we're pushing into swap.  So
	 * reactivate the folio, and let shmem_fallocate() quit when too many.
	 */
	if (!folio_test_uptodate(folio)) {
		if (inode->i_private) {
			struct shmem_falloc *shmem_falloc;
			spin_lock(&inode->i_lock);
			shmem_falloc = inode->i_private;
			if (shmem_falloc &&
			    !shmem_falloc->waitq &&
			    index >= shmem_falloc->start &&
			    index < shmem_falloc->next)
				shmem_falloc->nr_unswapped += nr_pages;
			else
				shmem_falloc = NULL;
			spin_unlock(&inode->i_lock);
			if (shmem_falloc)
				goto redirty;
		}
		folio_zero_range(folio, 0, folio_size(folio));
		flush_dcache_folio(folio);
		folio_mark_uptodate(folio);
	}

	/*
	 * Add inode to shmem_unuse()'s list of swapped-out inodes,
	 * if it's not already there.  Do it now before the folio is
	 * moved to swap cache, when its pagelock no longer protects
	 * the inode from eviction.  But don't unlock the mutex until
	 * we've incremented swapped, because shmem_unuse_inode() will
	 * prune a !swapped inode from the swaplist under this mutex.
	 */
	mutex_lock(&shmem_swaplist_mutex);
	if (list_empty(&info->swaplist))
		list_add(&info->swaplist, &shmem_swaplist);

	if (!folio_alloc_swap(folio, __GFP_HIGH | __GFP_NOMEMALLOC | __GFP_NOWARN)) {
		shmem_recalc_inode(inode, 0, nr_pages);
		swap_shmem_alloc(folio->swap, nr_pages);
		shmem_delete_from_page_cache(folio, swp_to_radix_entry(folio->swap));

		mutex_unlock(&shmem_swaplist_mutex);
		BUG_ON(folio_mapped(folio));
		return swap_writepage(&folio->page, wbc);
	}

	list_del_init(&info->swaplist);
	mutex_unlock(&shmem_swaplist_mutex);
	if (nr_pages > 1)
		goto try_split;
redirty:
	folio_mark_dirty(folio);
	if (wbc->for_reclaim)
		return AOP_WRITEPAGE_ACTIVATE;	/* Return with folio locked */
	folio_unlock(folio);
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
		raw_spin_lock(&sbinfo->stat_lock);	/* prevent replace/use races */
		mpol = sbinfo->mpol;
		mpol_get(mpol);
		raw_spin_unlock(&sbinfo->stat_lock);
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

static struct mempolicy *shmem_get_pgoff_policy(struct shmem_inode_info *info,
			pgoff_t index, unsigned int order, pgoff_t *ilx);

static struct folio *shmem_swapin_cluster(swp_entry_t swap, gfp_t gfp,
			struct shmem_inode_info *info, pgoff_t index)
{
	struct mempolicy *mpol;
	pgoff_t ilx;
	struct folio *folio;

	mpol = shmem_get_pgoff_policy(info, index, 0, &ilx);
	folio = swap_cluster_readahead(swap, gfp, mpol, ilx);
	mpol_cond_put(mpol);

	return folio;
}

/*
 * Make sure huge_gfp is always more limited than limit_gfp.
 * Some of the flags set permissions, while others set limitations.
 */
static gfp_t limit_gfp_mask(gfp_t huge_gfp, gfp_t limit_gfp)
{
	gfp_t allowflags = __GFP_IO | __GFP_FS | __GFP_RECLAIM;
	gfp_t denyflags = __GFP_NOWARN | __GFP_NORETRY;
	gfp_t zoneflags = limit_gfp & GFP_ZONEMASK;
	gfp_t result = huge_gfp & ~(allowflags | GFP_ZONEMASK);

	/* Allow allocations only from the originally specified zones. */
	result |= zoneflags;

	/*
	 * Minimize the result gfp by taking the union with the deny flags,
	 * and the intersection of the allow flags.
	 */
	result |= (limit_gfp & denyflags);
	result |= (huge_gfp & limit_gfp) & allowflags;

	return result;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
bool shmem_hpage_pmd_enabled(void)
{
	if (shmem_huge == SHMEM_HUGE_DENY)
		return false;
	if (test_bit(HPAGE_PMD_ORDER, &huge_shmem_orders_always))
		return true;
	if (test_bit(HPAGE_PMD_ORDER, &huge_shmem_orders_madvise))
		return true;
	if (test_bit(HPAGE_PMD_ORDER, &huge_shmem_orders_within_size))
		return true;
	if (test_bit(HPAGE_PMD_ORDER, &huge_shmem_orders_inherit) &&
	    shmem_huge != SHMEM_HUGE_NEVER)
		return true;

	return false;
}

unsigned long shmem_allowable_huge_orders(struct inode *inode,
				struct vm_area_struct *vma, pgoff_t index,
				loff_t write_end, bool shmem_huge_force)
{
	unsigned long mask = READ_ONCE(huge_shmem_orders_always);
	unsigned long within_size_orders = READ_ONCE(huge_shmem_orders_within_size);
	unsigned long vm_flags = vma ? vma->vm_flags : 0;
	pgoff_t aligned_index;
	unsigned int global_orders;
	loff_t i_size;
	int order;

	if (thp_disabled_by_hw() || (vma && vma_thp_disabled(vma, vm_flags)))
		return 0;

	global_orders = shmem_huge_global_enabled(inode, index, write_end,
						  shmem_huge_force, vma, vm_flags);
	/* Tmpfs huge pages allocation */
	if (!vma || !vma_is_anon_shmem(vma))
		return global_orders;

	/*
	 * Following the 'deny' semantics of the top level, force the huge
	 * option off from all mounts.
	 */
	if (shmem_huge == SHMEM_HUGE_DENY)
		return 0;

	/*
	 * Only allow inherit orders if the top-level value is 'force', which
	 * means non-PMD sized THP can not override 'huge' mount option now.
	 */
	if (shmem_huge == SHMEM_HUGE_FORCE)
		return READ_ONCE(huge_shmem_orders_inherit);

	/* Allow mTHP that will be fully within i_size. */
	order = highest_order(within_size_orders);
	while (within_size_orders) {
		aligned_index = round_up(index + 1, 1 << order);
		i_size = round_up(i_size_read(inode), PAGE_SIZE);
		if (i_size >> PAGE_SHIFT >= aligned_index) {
			mask |= within_size_orders;
			break;
		}

		order = next_order(&within_size_orders, order);
	}

	if (vm_flags & VM_HUGEPAGE)
		mask |= READ_ONCE(huge_shmem_orders_madvise);

	if (global_orders > 0)
		mask |= READ_ONCE(huge_shmem_orders_inherit);

	return THP_ORDERS_ALL_FILE_DEFAULT & mask;
}

static unsigned long shmem_suitable_orders(struct inode *inode, struct vm_fault *vmf,
					   struct address_space *mapping, pgoff_t index,
					   unsigned long orders)
{
	struct vm_area_struct *vma = vmf ? vmf->vma : NULL;
	pgoff_t aligned_index;
	unsigned long pages;
	int order;

	if (vma) {
		orders = thp_vma_suitable_orders(vma, vmf->address, orders);
		if (!orders)
			return 0;
	}

	/* Find the highest order that can add into the page cache */
	order = highest_order(orders);
	while (orders) {
		pages = 1UL << order;
		aligned_index = round_down(index, pages);
		/*
		 * Check for conflict before waiting on a huge allocation.
		 * Conflict might be that a huge page has just been allocated
		 * and added to page cache by a racing thread, or that there
		 * is already at least one small page in the huge extent.
		 * Be careful to retry when appropriate, but not forever!
		 * Elsewhere -EEXIST would be the right code, but not here.
		 */
		if (!xa_find(&mapping->i_pages, &aligned_index,
			     aligned_index + pages - 1, XA_PRESENT))
			break;
		order = next_order(&orders, order);
	}

	return orders;
}
#else
static unsigned long shmem_suitable_orders(struct inode *inode, struct vm_fault *vmf,
					   struct address_space *mapping, pgoff_t index,
					   unsigned long orders)
{
	return 0;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

static struct folio *shmem_alloc_folio(gfp_t gfp, int order,
		struct shmem_inode_info *info, pgoff_t index)
{
	struct mempolicy *mpol;
	pgoff_t ilx;
	struct folio *folio;

	mpol = shmem_get_pgoff_policy(info, index, order, &ilx);
	folio = folio_alloc_mpol(gfp, order, mpol, ilx, numa_node_id());
	mpol_cond_put(mpol);

	return folio;
}

static struct folio *shmem_alloc_and_add_folio(struct vm_fault *vmf,
		gfp_t gfp, struct inode *inode, pgoff_t index,
		struct mm_struct *fault_mm, unsigned long orders)
{
	struct address_space *mapping = inode->i_mapping;
	struct shmem_inode_info *info = SHMEM_I(inode);
	unsigned long suitable_orders = 0;
	struct folio *folio = NULL;
	long pages;
	int error, order;

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		orders = 0;

	if (orders > 0) {
		suitable_orders = shmem_suitable_orders(inode, vmf,
							mapping, index, orders);

		order = highest_order(suitable_orders);
		while (suitable_orders) {
			pages = 1UL << order;
			index = round_down(index, pages);
			folio = shmem_alloc_folio(gfp, order, info, index);
			if (folio)
				goto allocated;

			if (pages == HPAGE_PMD_NR)
				count_vm_event(THP_FILE_FALLBACK);
			count_mthp_stat(order, MTHP_STAT_SHMEM_FALLBACK);
			order = next_order(&suitable_orders, order);
		}
	} else {
		pages = 1;
		folio = shmem_alloc_folio(gfp, 0, info, index);
	}
	if (!folio)
		return ERR_PTR(-ENOMEM);

allocated:
	__folio_set_locked(folio);
	__folio_set_swapbacked(folio);

	gfp &= GFP_RECLAIM_MASK;
	error = mem_cgroup_charge(folio, fault_mm, gfp);
	if (error) {
		if (xa_find(&mapping->i_pages, &index,
				index + pages - 1, XA_PRESENT)) {
			error = -EEXIST;
		} else if (pages > 1) {
			if (pages == HPAGE_PMD_NR) {
				count_vm_event(THP_FILE_FALLBACK);
				count_vm_event(THP_FILE_FALLBACK_CHARGE);
			}
			count_mthp_stat(folio_order(folio), MTHP_STAT_SHMEM_FALLBACK);
			count_mthp_stat(folio_order(folio), MTHP_STAT_SHMEM_FALLBACK_CHARGE);
		}
		goto unlock;
	}

	error = shmem_add_to_page_cache(folio, mapping, index, NULL, gfp);
	if (error)
		goto unlock;

	error = shmem_inode_acct_blocks(inode, pages);
	if (error) {
		struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
		long freed;
		/*
		 * Try to reclaim some space by splitting a few
		 * large folios beyond i_size on the filesystem.
		 */
		shmem_unused_huge_shrink(sbinfo, NULL, pages);
		/*
		 * And do a shmem_recalc_inode() to account for freed pages:
		 * except our folio is there in cache, so not quite balanced.
		 */
		spin_lock(&info->lock);
		freed = pages + info->alloced - info->swapped -
			READ_ONCE(mapping->nrpages);
		if (freed > 0)
			info->alloced -= freed;
		spin_unlock(&info->lock);
		if (freed > 0)
			shmem_inode_unacct_blocks(inode, freed);
		error = shmem_inode_acct_blocks(inode, pages);
		if (error) {
			filemap_remove_folio(folio);
			goto unlock;
		}
	}

	shmem_recalc_inode(inode, pages, 0);
	folio_add_lru(folio);
	return folio;

unlock:
	folio_unlock(folio);
	folio_put(folio);
	return ERR_PTR(error);
}

static struct folio *shmem_swap_alloc_folio(struct inode *inode,
		struct vm_area_struct *vma, pgoff_t index,
		swp_entry_t entry, int order, gfp_t gfp)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct folio *new;
	void *shadow;
	int nr_pages;

	/*
	 * We have arrived here because our zones are constrained, so don't
	 * limit chance of success with further cpuset and node constraints.
	 */
	gfp &= ~GFP_CONSTRAINT_MASK;
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) && order > 0) {
		gfp_t huge_gfp = vma_thp_gfp_mask(vma);

		gfp = limit_gfp_mask(huge_gfp, gfp);
	}

	new = shmem_alloc_folio(gfp, order, info, index);
	if (!new)
		return ERR_PTR(-ENOMEM);

	nr_pages = folio_nr_pages(new);
	if (mem_cgroup_swapin_charge_folio(new, vma ? vma->vm_mm : NULL,
					   gfp, entry)) {
		folio_put(new);
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Prevent parallel swapin from proceeding with the swap cache flag.
	 *
	 * Of course there is another possible concurrent scenario as well,
	 * that is to say, the swap cache flag of a large folio has already
	 * been set by swapcache_prepare(), while another thread may have
	 * already split the large swap entry stored in the shmem mapping.
	 * In this case, shmem_add_to_page_cache() will help identify the
	 * concurrent swapin and return -EEXIST.
	 */
	if (swapcache_prepare(entry, nr_pages)) {
		folio_put(new);
		return ERR_PTR(-EEXIST);
	}

	__folio_set_locked(new);
	__folio_set_swapbacked(new);
	new->swap = entry;

	memcg1_swapin(entry, nr_pages);
	shadow = get_shadow_from_swap_cache(entry);
	if (shadow)
		workingset_refault(new, shadow);
	folio_add_lru(new);
	swap_read_folio(new, NULL);
	return new;
}

/*
 * When a page is moved from swapcache to shmem filecache (either by the
 * usual swapin of shmem_get_folio_gfp(), or by the less common swapoff of
 * shmem_unuse_inode()), it may have been read in earlier from swap, in
 * ignorance of the mapping it belongs to.  If that mapping has special
 * constraints (like the gma500 GEM driver, which requires RAM below 4GB),
 * we may need to copy to a suitable page before moving to filecache.
 *
 * In a future release, this may well be extended to respect cpuset and
 * NUMA mempolicy, and applied also to anonymous pages in do_swap_page();
 * but for now it is a simple matter of zone.
 */
static bool shmem_should_replace_folio(struct folio *folio, gfp_t gfp)
{
	return folio_zonenum(folio) > gfp_zone(gfp);
}

static int shmem_replace_folio(struct folio **foliop, gfp_t gfp,
				struct shmem_inode_info *info, pgoff_t index,
				struct vm_area_struct *vma)
{
	struct folio *new, *old = *foliop;
	swp_entry_t entry = old->swap;
	struct address_space *swap_mapping = swap_address_space(entry);
	pgoff_t swap_index = swap_cache_index(entry);
	XA_STATE(xas, &swap_mapping->i_pages, swap_index);
	int nr_pages = folio_nr_pages(old);
	int error = 0, i;

	/*
	 * We have arrived here because our zones are constrained, so don't
	 * limit chance of success by further cpuset and node constraints.
	 */
	gfp &= ~GFP_CONSTRAINT_MASK;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (nr_pages > 1) {
		gfp_t huge_gfp = vma_thp_gfp_mask(vma);

		gfp = limit_gfp_mask(huge_gfp, gfp);
	}
#endif

	new = shmem_alloc_folio(gfp, folio_order(old), info, index);
	if (!new)
		return -ENOMEM;

	folio_ref_add(new, nr_pages);
	folio_copy(new, old);
	flush_dcache_folio(new);

	__folio_set_locked(new);
	__folio_set_swapbacked(new);
	folio_mark_uptodate(new);
	new->swap = entry;
	folio_set_swapcache(new);

	/* Swap cache still stores N entries instead of a high-order entry */
	xa_lock_irq(&swap_mapping->i_pages);
	for (i = 0; i < nr_pages; i++) {
		void *item = xas_load(&xas);

		if (item != old) {
			error = -ENOENT;
			break;
		}

		xas_store(&xas, new);
		xas_next(&xas);
	}
	if (!error) {
		mem_cgroup_replace_folio(old, new);
		shmem_update_stats(new, nr_pages);
		shmem_update_stats(old, -nr_pages);
	}
	xa_unlock_irq(&swap_mapping->i_pages);

	if (unlikely(error)) {
		/*
		 * Is this possible?  I think not, now that our callers
		 * check both the swapcache flag and folio->private
		 * after getting the folio lock; but be defensive.
		 * Reverse old to newpage for clear and free.
		 */
		old = new;
	} else {
		folio_add_lru(new);
		*foliop = new;
	}

	folio_clear_swapcache(old);
	old->private = NULL;

	folio_unlock(old);
	/*
	 * The old folio are removed from swap cache, drop the 'nr_pages'
	 * reference, as well as one temporary reference getting from swap
	 * cache.
	 */
	folio_put_refs(old, nr_pages + 1);
	return error;
}

static void shmem_set_folio_swapin_error(struct inode *inode, pgoff_t index,
					 struct folio *folio, swp_entry_t swap,
					 bool skip_swapcache)
{
	struct address_space *mapping = inode->i_mapping;
	swp_entry_t swapin_error;
	void *old;
	int nr_pages;

	swapin_error = make_poisoned_swp_entry();
	old = xa_cmpxchg_irq(&mapping->i_pages, index,
			     swp_to_radix_entry(swap),
			     swp_to_radix_entry(swapin_error), 0);
	if (old != swp_to_radix_entry(swap))
		return;

	nr_pages = folio_nr_pages(folio);
	folio_wait_writeback(folio);
	if (!skip_swapcache)
		delete_from_swap_cache(folio);
	/*
	 * Don't treat swapin error folio as alloced. Otherwise inode->i_blocks
	 * won't be 0 when inode is released and thus trigger WARN_ON(i_blocks)
	 * in shmem_evict_inode().
	 */
	shmem_recalc_inode(inode, -nr_pages, -nr_pages);
	swap_free_nr(swap, nr_pages);
}

static int shmem_split_large_entry(struct inode *inode, pgoff_t index,
				   swp_entry_t swap, gfp_t gfp)
{
	struct address_space *mapping = inode->i_mapping;
	XA_STATE_ORDER(xas, &mapping->i_pages, index, 0);
	void *alloced_shadow = NULL;
	int alloced_order = 0, i;

	/* Convert user data gfp flags to xarray node gfp flags */
	gfp &= GFP_RECLAIM_MASK;

	for (;;) {
		int order = -1, split_order = 0;
		void *old = NULL;

		xas_lock_irq(&xas);
		old = xas_load(&xas);
		if (!xa_is_value(old) || swp_to_radix_entry(swap) != old) {
			xas_set_err(&xas, -EEXIST);
			goto unlock;
		}

		order = xas_get_order(&xas);

		/* Swap entry may have changed before we re-acquire the lock */
		if (alloced_order &&
		    (old != alloced_shadow || order != alloced_order)) {
			xas_destroy(&xas);
			alloced_order = 0;
		}

		/* Try to split large swap entry in pagecache */
		if (order > 0) {
			if (!alloced_order) {
				split_order = order;
				goto unlock;
			}
			xas_split(&xas, old, order);

			/*
			 * Re-set the swap entry after splitting, and the swap
			 * offset of the original large entry must be continuous.
			 */
			for (i = 0; i < 1 << order; i++) {
				pgoff_t aligned_index = round_down(index, 1 << order);
				swp_entry_t tmp;

				tmp = swp_entry(swp_type(swap), swp_offset(swap) + i);
				__xa_store(&mapping->i_pages, aligned_index + i,
					   swp_to_radix_entry(tmp), 0);
			}
		}

unlock:
		xas_unlock_irq(&xas);

		/* split needed, alloc here and retry. */
		if (split_order) {
			xas_split_alloc(&xas, old, split_order, gfp);
			if (xas_error(&xas))
				goto error;
			alloced_shadow = old;
			alloced_order = split_order;
			xas_reset(&xas);
			continue;
		}

		if (!xas_nomem(&xas, gfp))
			break;
	}

error:
	if (xas_error(&xas))
		return xas_error(&xas);

	return alloced_order;
}

/*
 * Swap in the folio pointed to by *foliop.
 * Caller has to make sure that *foliop contains a valid swapped folio.
 * Returns 0 and the folio in foliop if success. On failure, returns the
 * error code and NULL in *foliop.
 */
static int shmem_swapin_folio(struct inode *inode, pgoff_t index,
			     struct folio **foliop, enum sgp_type sgp,
			     gfp_t gfp, struct vm_area_struct *vma,
			     vm_fault_t *fault_type)
{
	struct address_space *mapping = inode->i_mapping;
	struct mm_struct *fault_mm = vma ? vma->vm_mm : NULL;
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct swap_info_struct *si;
	struct folio *folio = NULL;
	bool skip_swapcache = false;
	swp_entry_t swap;
	int error, nr_pages, order, split_order;

	VM_BUG_ON(!*foliop || !xa_is_value(*foliop));
	swap = radix_to_swp_entry(*foliop);
	*foliop = NULL;

	if (is_poisoned_swp_entry(swap))
		return -EIO;

	si = get_swap_device(swap);
	if (!si) {
		if (!shmem_confirm_swap(mapping, index, swap))
			return -EEXIST;
		else
			return -EINVAL;
	}

	/* Look it up and read it in.. */
	folio = swap_cache_get_folio(swap, NULL, 0);
	order = xa_get_order(&mapping->i_pages, index);
	if (!folio) {
		bool fallback_order0 = false;

		/* Or update major stats only when swapin succeeds?? */
		if (fault_type) {
			*fault_type |= VM_FAULT_MAJOR;
			count_vm_event(PGMAJFAULT);
			count_memcg_event_mm(fault_mm, PGMAJFAULT);
		}

		/*
		 * If uffd is active for the vma, we need per-page fault
		 * fidelity to maintain the uffd semantics, then fallback
		 * to swapin order-0 folio, as well as for zswap case.
		 */
		if (order > 0 && ((vma && unlikely(userfaultfd_armed(vma))) ||
				  !zswap_never_enabled()))
			fallback_order0 = true;

		/* Skip swapcache for synchronous device. */
		if (!fallback_order0 && data_race(si->flags & SWP_SYNCHRONOUS_IO)) {
			folio = shmem_swap_alloc_folio(inode, vma, index, swap, order, gfp);
			if (!IS_ERR(folio)) {
				skip_swapcache = true;
				goto alloced;
			}

			/*
			 * Fallback to swapin order-0 folio unless the swap entry
			 * already exists.
			 */
			error = PTR_ERR(folio);
			folio = NULL;
			if (error == -EEXIST)
				goto failed;
		}

		/*
		 * Now swap device can only swap in order 0 folio, then we
		 * should split the large swap entry stored in the pagecache
		 * if necessary.
		 */
		split_order = shmem_split_large_entry(inode, index, swap, gfp);
		if (split_order < 0) {
			error = split_order;
			goto failed;
		}

		/*
		 * If the large swap entry has already been split, it is
		 * necessary to recalculate the new swap entry based on
		 * the old order alignment.
		 */
		if (split_order > 0) {
			pgoff_t offset = index - round_down(index, 1 << split_order);

			swap = swp_entry(swp_type(swap), swp_offset(swap) + offset);
		}

		/* Here we actually start the io */
		folio = shmem_swapin_cluster(swap, gfp, info, index);
		if (!folio) {
			error = -ENOMEM;
			goto failed;
		}
	} else if (order != folio_order(folio)) {
		/*
		 * Swap readahead may swap in order 0 folios into swapcache
		 * asynchronously, while the shmem mapping can still stores
		 * large swap entries. In such cases, we should split the
		 * large swap entry to prevent possible data corruption.
		 */
		split_order = shmem_split_large_entry(inode, index, swap, gfp);
		if (split_order < 0) {
			error = split_order;
			goto failed;
		}

		/*
		 * If the large swap entry has already been split, it is
		 * necessary to recalculate the new swap entry based on
		 * the old order alignment.
		 */
		if (split_order > 0) {
			pgoff_t offset = index - round_down(index, 1 << split_order);

			swap = swp_entry(swp_type(swap), swp_offset(swap) + offset);
		}
	}

alloced:
	/* We have to do this with folio locked to prevent races */
	folio_lock(folio);
	if ((!skip_swapcache && !folio_test_swapcache(folio)) ||
	    folio->swap.val != swap.val ||
	    !shmem_confirm_swap(mapping, index, swap) ||
	    xa_get_order(&mapping->i_pages, index) != folio_order(folio)) {
		error = -EEXIST;
		goto unlock;
	}
	if (!folio_test_uptodate(folio)) {
		error = -EIO;
		goto failed;
	}
	folio_wait_writeback(folio);
	nr_pages = folio_nr_pages(folio);

	/*
	 * Some architectures may have to restore extra metadata to the
	 * folio after reading from swap.
	 */
	arch_swap_restore(folio_swap(swap, folio), folio);

	if (shmem_should_replace_folio(folio, gfp)) {
		error = shmem_replace_folio(&folio, gfp, info, index, vma);
		if (error)
			goto failed;
	}

	error = shmem_add_to_page_cache(folio, mapping,
					round_down(index, nr_pages),
					swp_to_radix_entry(swap), gfp);
	if (error)
		goto failed;

	shmem_recalc_inode(inode, 0, -nr_pages);

	if (sgp == SGP_WRITE)
		folio_mark_accessed(folio);

	if (skip_swapcache) {
		folio->swap.val = 0;
		swapcache_clear(si, swap, nr_pages);
	} else {
		delete_from_swap_cache(folio);
	}
	folio_mark_dirty(folio);
	swap_free_nr(swap, nr_pages);
	put_swap_device(si);

	*foliop = folio;
	return 0;
failed:
	if (!shmem_confirm_swap(mapping, index, swap))
		error = -EEXIST;
	if (error == -EIO)
		shmem_set_folio_swapin_error(inode, index, folio, swap,
					     skip_swapcache);
unlock:
	if (skip_swapcache)
		swapcache_clear(si, swap, folio_nr_pages(folio));
	if (folio) {
		folio_unlock(folio);
		folio_put(folio);
	}
	put_swap_device(si);

	return error;
}

/*
 * shmem_get_folio_gfp - find page in cache, or get from swap, or allocate
 *
 * If we allocate a new one we do not mark it dirty. That's up to the
 * vm. If we swap it in we mark it dirty since we also free the swap
 * entry since a page cannot live in both the swap and page cache.
 *
 * vmf and fault_type are only supplied by shmem_fault: otherwise they are NULL.
 */
static int shmem_get_folio_gfp(struct inode *inode, pgoff_t index,
		loff_t write_end, struct folio **foliop, enum sgp_type sgp,
		gfp_t gfp, struct vm_fault *vmf, vm_fault_t *fault_type)
{
	struct vm_area_struct *vma = vmf ? vmf->vma : NULL;
	struct mm_struct *fault_mm;
	struct folio *folio;
	int error;
	bool alloced;
	unsigned long orders = 0;

	if (WARN_ON_ONCE(!shmem_mapping(inode->i_mapping)))
		return -EINVAL;

	if (index > (MAX_LFS_FILESIZE >> PAGE_SHIFT))
		return -EFBIG;
repeat:
	if (sgp <= SGP_CACHE &&
	    ((loff_t)index << PAGE_SHIFT) >= i_size_read(inode))
		return -EINVAL;

	alloced = false;
	fault_mm = vma ? vma->vm_mm : NULL;

	folio = filemap_get_entry(inode->i_mapping, index);
	if (folio && vma && userfaultfd_minor(vma)) {
		if (!xa_is_value(folio))
			folio_put(folio);
		*fault_type = handle_userfault(vmf, VM_UFFD_MINOR);
		return 0;
	}

	if (xa_is_value(folio)) {
		error = shmem_swapin_folio(inode, index, &folio,
					   sgp, gfp, vma, fault_type);
		if (error == -EEXIST)
			goto repeat;

		*foliop = folio;
		return error;
	}

	if (folio) {
		folio_lock(folio);

		/* Has the folio been truncated or swapped out? */
		if (unlikely(folio->mapping != inode->i_mapping)) {
			folio_unlock(folio);
			folio_put(folio);
			goto repeat;
		}
		if (sgp == SGP_WRITE)
			folio_mark_accessed(folio);
		if (folio_test_uptodate(folio))
			goto out;
		/* fallocated folio */
		if (sgp != SGP_READ)
			goto clear;
		folio_unlock(folio);
		folio_put(folio);
	}

	/*
	 * SGP_READ: succeed on hole, with NULL folio, letting caller zero.
	 * SGP_NOALLOC: fail on hole, with NULL folio, letting caller fail.
	 */
	*foliop = NULL;
	if (sgp == SGP_READ)
		return 0;
	if (sgp == SGP_NOALLOC)
		return -ENOENT;

	/*
	 * Fast cache lookup and swap lookup did not find it: allocate.
	 */

	if (vma && userfaultfd_missing(vma)) {
		*fault_type = handle_userfault(vmf, VM_UFFD_MISSING);
		return 0;
	}

	/* Find hugepage orders that are allowed for anonymous shmem and tmpfs. */
	orders = shmem_allowable_huge_orders(inode, vma, index, write_end, false);
	if (orders > 0) {
		gfp_t huge_gfp;

		huge_gfp = vma_thp_gfp_mask(vma);
		huge_gfp = limit_gfp_mask(huge_gfp, gfp);
		folio = shmem_alloc_and_add_folio(vmf, huge_gfp,
				inode, index, fault_mm, orders);
		if (!IS_ERR(folio)) {
			if (folio_test_pmd_mappable(folio))
				count_vm_event(THP_FILE_ALLOC);
			count_mthp_stat(folio_order(folio), MTHP_STAT_SHMEM_ALLOC);
			goto alloced;
		}
		if (PTR_ERR(folio) == -EEXIST)
			goto repeat;
	}

	folio = shmem_alloc_and_add_folio(vmf, gfp, inode, index, fault_mm, 0);
	if (IS_ERR(folio)) {
		error = PTR_ERR(folio);
		if (error == -EEXIST)
			goto repeat;
		folio = NULL;
		goto unlock;
	}

alloced:
	alloced = true;
	if (folio_test_large(folio) &&
	    DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE) <
					folio_next_index(folio)) {
		struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
		struct shmem_inode_info *info = SHMEM_I(inode);
		/*
		 * Part of the large folio is beyond i_size: subject
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

	if (sgp == SGP_WRITE)
		folio_set_referenced(folio);
	/*
	 * Let SGP_FALLOC use the SGP_WRITE optimization on a new folio.
	 */
	if (sgp == SGP_FALLOC)
		sgp = SGP_WRITE;
clear:
	/*
	 * Let SGP_WRITE caller clear ends if write does not fill folio;
	 * but SGP_FALLOC on a folio fallocated earlier must initialize
	 * it now, lest undo on failure cancel our earlier guarantee.
	 */
	if (sgp != SGP_WRITE && !folio_test_uptodate(folio)) {
		long i, n = folio_nr_pages(folio);

		for (i = 0; i < n; i++)
			clear_highpage(folio_page(folio, i));
		flush_dcache_folio(folio);
		folio_mark_uptodate(folio);
	}

	/* Perhaps the file has been truncated since we checked */
	if (sgp <= SGP_CACHE &&
	    ((loff_t)index << PAGE_SHIFT) >= i_size_read(inode)) {
		error = -EINVAL;
		goto unlock;
	}
out:
	*foliop = folio;
	return 0;

	/*
	 * Error recovery.
	 */
unlock:
	if (alloced)
		filemap_remove_folio(folio);
	shmem_recalc_inode(inode, 0, 0);
	if (folio) {
		folio_unlock(folio);
		folio_put(folio);
	}
	return error;
}

/**
 * shmem_get_folio - find, and lock a shmem folio.
 * @inode:	inode to search
 * @index:	the page index.
 * @write_end:	end of a write, could extend inode size
 * @foliop:	pointer to the folio if found
 * @sgp:	SGP_* flags to control behavior
 *
 * Looks up the page cache entry at @inode & @index.  If a folio is
 * present, it is returned locked with an increased refcount.
 *
 * If the caller modifies data in the folio, it must call folio_mark_dirty()
 * before unlocking the folio to ensure that the folio is not reclaimed.
 * There is no need to reserve space before calling folio_mark_dirty().
 *
 * When no folio is found, the behavior depends on @sgp:
 *  - for SGP_READ, *@foliop is %NULL and 0 is returned
 *  - for SGP_NOALLOC, *@foliop is %NULL and -ENOENT is returned
 *  - for all other flags a new folio is allocated, inserted into the
 *    page cache and returned locked in @foliop.
 *
 * Context: May sleep.
 * Return: 0 if successful, else a negative error code.
 */
int shmem_get_folio(struct inode *inode, pgoff_t index, loff_t write_end,
		    struct folio **foliop, enum sgp_type sgp)
{
	return shmem_get_folio_gfp(inode, index, write_end, foliop, sgp,
			mapping_gfp_mask(inode->i_mapping), NULL, NULL);
}
EXPORT_SYMBOL_GPL(shmem_get_folio);

/*
 * This is like autoremove_wake_function, but it removes the wait queue
 * entry unconditionally - even if something else had already woken the
 * target.
 */
static int synchronous_wake_function(wait_queue_entry_t *wait,
			unsigned int mode, int sync, void *key)
{
	int ret = default_wake_function(wait, mode, sync, key);
	list_del_init(&wait->entry);
	return ret;
}

/*
 * Trinity finds that probing a hole which tmpfs is punching can
 * prevent the hole-punch from ever completing: which in turn
 * locks writers out with its hold on i_rwsem.  So refrain from
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
 * standard mutex or completion: but we cannot take i_rwsem in fault,
 * and bloating every shmem inode for this unlikely case would be sad.
 */
static vm_fault_t shmem_falloc_wait(struct vm_fault *vmf, struct inode *inode)
{
	struct shmem_falloc *shmem_falloc;
	struct file *fpin = NULL;
	vm_fault_t ret = 0;

	spin_lock(&inode->i_lock);
	shmem_falloc = inode->i_private;
	if (shmem_falloc &&
	    shmem_falloc->waitq &&
	    vmf->pgoff >= shmem_falloc->start &&
	    vmf->pgoff < shmem_falloc->next) {
		wait_queue_head_t *shmem_falloc_waitq;
		DEFINE_WAIT_FUNC(shmem_fault_wait, synchronous_wake_function);

		ret = VM_FAULT_NOPAGE;
		fpin = maybe_unlock_mmap_for_io(vmf, NULL);
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
	}
	spin_unlock(&inode->i_lock);
	if (fpin) {
		fput(fpin);
		ret = VM_FAULT_RETRY;
	}
	return ret;
}

static vm_fault_t shmem_fault(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	gfp_t gfp = mapping_gfp_mask(inode->i_mapping);
	struct folio *folio = NULL;
	vm_fault_t ret = 0;
	int err;

	/*
	 * Trinity finds that probing a hole which tmpfs is punching can
	 * prevent the hole-punch from ever completing: noted in i_private.
	 */
	if (unlikely(inode->i_private)) {
		ret = shmem_falloc_wait(vmf, inode);
		if (ret)
			return ret;
	}

	WARN_ON_ONCE(vmf->page != NULL);
	err = shmem_get_folio_gfp(inode, vmf->pgoff, 0, &folio, SGP_CACHE,
				  gfp, vmf, &ret);
	if (err)
		return vmf_error(err);
	if (folio) {
		vmf->page = folio_file_page(folio, vmf->pgoff);
		ret |= VM_FAULT_LOCKED;
	}
	return ret;
}

unsigned long shmem_get_unmapped_area(struct file *file,
				      unsigned long uaddr, unsigned long len,
				      unsigned long pgoff, unsigned long flags)
{
	unsigned long addr;
	unsigned long offset;
	unsigned long inflated_len;
	unsigned long inflated_addr;
	unsigned long inflated_offset;
	unsigned long hpage_size;

	if (len > TASK_SIZE)
		return -ENOMEM;

	addr = mm_get_unmapped_area(current->mm, file, uaddr, len, pgoff,
				    flags);

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

	hpage_size = HPAGE_PMD_SIZE;
	if (shmem_huge != SHMEM_HUGE_FORCE) {
		struct super_block *sb;
		unsigned long __maybe_unused hpage_orders;
		int order = 0;

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

			/*
			 * Find the highest mTHP order used for anonymous shmem to
			 * provide a suitable alignment address.
			 */
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			hpage_orders = READ_ONCE(huge_shmem_orders_always);
			hpage_orders |= READ_ONCE(huge_shmem_orders_within_size);
			hpage_orders |= READ_ONCE(huge_shmem_orders_madvise);
			if (SHMEM_SB(sb)->huge != SHMEM_HUGE_NEVER)
				hpage_orders |= READ_ONCE(huge_shmem_orders_inherit);

			if (hpage_orders > 0) {
				order = highest_order(hpage_orders);
				hpage_size = PAGE_SIZE << order;
			}
#endif
		}
		if (SHMEM_SB(sb)->huge == SHMEM_HUGE_NEVER && !order)
			return addr;
	}

	if (len < hpage_size)
		return addr;

	offset = (pgoff << PAGE_SHIFT) & (hpage_size - 1);
	if (offset && offset + len < 2 * hpage_size)
		return addr;
	if ((addr & (hpage_size - 1)) == offset)
		return addr;

	inflated_len = len + hpage_size - PAGE_SIZE;
	if (inflated_len > TASK_SIZE)
		return addr;
	if (inflated_len < len)
		return addr;

	inflated_addr = mm_get_unmapped_area(current->mm, NULL, uaddr,
					     inflated_len, 0, flags);
	if (IS_ERR_VALUE(inflated_addr))
		return addr;
	if (inflated_addr & ~PAGE_MASK)
		return addr;

	inflated_offset = inflated_addr & (hpage_size - 1);
	inflated_addr += offset - inflated_offset;
	if (inflated_offset > offset)
		inflated_addr += hpage_size;

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
					  unsigned long addr, pgoff_t *ilx)
{
	struct inode *inode = file_inode(vma->vm_file);
	pgoff_t index;

	/*
	 * Bias interleave by inode number to distribute better across nodes;
	 * but this interface is independent of which page order is used, so
	 * supplies only that bias, letting caller apply the offset (adjusted
	 * by page order, as in shmem_get_pgoff_policy() and get_vma_policy()).
	 */
	*ilx = inode->i_ino;
	index = ((addr - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	return mpol_shared_policy_lookup(&SHMEM_I(inode)->policy, index);
}

static struct mempolicy *shmem_get_pgoff_policy(struct shmem_inode_info *info,
			pgoff_t index, unsigned int order, pgoff_t *ilx)
{
	struct mempolicy *mpol;

	/* Bias interleave by inode number to distribute better across nodes */
	*ilx = info->vfs_inode.i_ino + (index >> order);

	mpol = mpol_shared_policy_lookup(&info->policy, index);
	return mpol ? mpol : get_task_policy(current);
}
#else
static struct mempolicy *shmem_get_pgoff_policy(struct shmem_inode_info *info,
			pgoff_t index, unsigned int order, pgoff_t *ilx)
{
	*ilx = 0;
	return NULL;
}
#endif /* CONFIG_NUMA */

int shmem_lock(struct file *file, int lock, struct ucounts *ucounts)
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
		if (!user_shm_lock(inode->i_size, ucounts))
			goto out_nomem;
		info->flags |= VM_LOCKED;
		mapping_set_unevictable(file->f_mapping);
	}
	if (!lock && (info->flags & VM_LOCKED) && ucounts) {
		user_shm_unlock(inode->i_size, ucounts);
		info->flags &= ~VM_LOCKED;
		mapping_clear_unevictable(file->f_mapping);
	}
	retval = 0;

out_nomem:
	return retval;
}

static int shmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);

	file_accessed(file);
	/* This is anonymous shared memory if it is unlinked at the time of mmap */
	if (inode->i_nlink)
		vma->vm_ops = &shmem_vm_ops;
	else
		vma->vm_ops = &shmem_anon_vm_ops;
	return 0;
}

static int shmem_file_open(struct inode *inode, struct file *file)
{
	file->f_mode |= FMODE_CAN_ODIRECT;
	return generic_file_open(inode, file);
}

#ifdef CONFIG_TMPFS_XATTR
static int shmem_initxattrs(struct inode *, const struct xattr *, void *);

#if IS_ENABLED(CONFIG_UNICODE)
/*
 * shmem_inode_casefold_flags - Deal with casefold file attribute flag
 *
 * The casefold file attribute needs some special checks. I can just be added to
 * an empty dir, and can't be removed from a non-empty dir.
 */
static int shmem_inode_casefold_flags(struct inode *inode, unsigned int fsflags,
				      struct dentry *dentry, unsigned int *i_flags)
{
	unsigned int old = inode->i_flags;
	struct super_block *sb = inode->i_sb;

	if (fsflags & FS_CASEFOLD_FL) {
		if (!(old & S_CASEFOLD)) {
			if (!sb->s_encoding)
				return -EOPNOTSUPP;

			if (!S_ISDIR(inode->i_mode))
				return -ENOTDIR;

			if (dentry && !simple_empty(dentry))
				return -ENOTEMPTY;
		}

		*i_flags = *i_flags | S_CASEFOLD;
	} else if (old & S_CASEFOLD) {
		if (dentry && !simple_empty(dentry))
			return -ENOTEMPTY;
	}

	return 0;
}
#else
static int shmem_inode_casefold_flags(struct inode *inode, unsigned int fsflags,
				      struct dentry *dentry, unsigned int *i_flags)
{
	if (fsflags & FS_CASEFOLD_FL)
		return -EOPNOTSUPP;

	return 0;
}
#endif

/*
 * chattr's fsflags are unrelated to extended attributes,
 * but tmpfs has chosen to enable them under the same config option.
 */
static int shmem_set_inode_flags(struct inode *inode, unsigned int fsflags, struct dentry *dentry)
{
	unsigned int i_flags = 0;
	int ret;

	ret = shmem_inode_casefold_flags(inode, fsflags, dentry, &i_flags);
	if (ret)
		return ret;

	if (fsflags & FS_NOATIME_FL)
		i_flags |= S_NOATIME;
	if (fsflags & FS_APPEND_FL)
		i_flags |= S_APPEND;
	if (fsflags & FS_IMMUTABLE_FL)
		i_flags |= S_IMMUTABLE;
	/*
	 * But FS_NODUMP_FL does not require any action in i_flags.
	 */
	inode_set_flags(inode, i_flags, S_NOATIME | S_APPEND | S_IMMUTABLE | S_CASEFOLD);

	return 0;
}
#else
static void shmem_set_inode_flags(struct inode *inode, unsigned int fsflags, struct dentry *dentry)
{
}
#define shmem_initxattrs NULL
#endif

static struct offset_ctx *shmem_get_offset_ctx(struct inode *inode)
{
	return &SHMEM_I(inode)->dir_offsets;
}

static struct inode *__shmem_get_inode(struct mnt_idmap *idmap,
					     struct super_block *sb,
					     struct inode *dir, umode_t mode,
					     dev_t dev, unsigned long flags)
{
	struct inode *inode;
	struct shmem_inode_info *info;
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);
	ino_t ino;
	int err;

	err = shmem_reserve_inode(sb, &ino);
	if (err)
		return ERR_PTR(err);

	inode = new_inode(sb);
	if (!inode) {
		shmem_free_inode(sb, 0);
		return ERR_PTR(-ENOSPC);
	}

	inode->i_ino = ino;
	inode_init_owner(idmap, inode, dir, mode);
	inode->i_blocks = 0;
	simple_inode_init_ts(inode);
	inode->i_generation = get_random_u32();
	info = SHMEM_I(inode);
	memset(info, 0, (char *)inode - (char *)info);
	spin_lock_init(&info->lock);
	atomic_set(&info->stop_eviction, 0);
	info->seals = F_SEAL_SEAL;
	info->flags = flags & VM_NORESERVE;
	info->i_crtime = inode_get_mtime(inode);
	info->fsflags = (dir == NULL) ? 0 :
		SHMEM_I(dir)->fsflags & SHMEM_FL_INHERITED;
	if (info->fsflags)
		shmem_set_inode_flags(inode, info->fsflags, NULL);
	INIT_LIST_HEAD(&info->shrinklist);
	INIT_LIST_HEAD(&info->swaplist);
	simple_xattrs_init(&info->xattrs);
	cache_no_acl(inode);
	if (sbinfo->noswap)
		mapping_set_unevictable(inode->i_mapping);

	/* Don't consider 'deny' for emergencies and 'force' for testing */
	if (sbinfo->huge)
		mapping_set_large_folios(inode->i_mapping);

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
		inode->i_fop = &simple_offset_dir_operations;
		simple_offset_init(shmem_get_offset_ctx(inode));
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
	return inode;
}

#ifdef CONFIG_TMPFS_QUOTA
static struct inode *shmem_get_inode(struct mnt_idmap *idmap,
				     struct super_block *sb, struct inode *dir,
				     umode_t mode, dev_t dev, unsigned long flags)
{
	int err;
	struct inode *inode;

	inode = __shmem_get_inode(idmap, sb, dir, mode, dev, flags);
	if (IS_ERR(inode))
		return inode;

	err = dquot_initialize(inode);
	if (err)
		goto errout;

	err = dquot_alloc_inode(inode);
	if (err) {
		dquot_drop(inode);
		goto errout;
	}
	return inode;

errout:
	inode->i_flags |= S_NOQUOTA;
	iput(inode);
	return ERR_PTR(err);
}
#else
static inline struct inode *shmem_get_inode(struct mnt_idmap *idmap,
				     struct super_block *sb, struct inode *dir,
				     umode_t mode, dev_t dev, unsigned long flags)
{
	return __shmem_get_inode(idmap, sb, dir, mode, dev, flags);
}
#endif /* CONFIG_TMPFS_QUOTA */

#ifdef CONFIG_USERFAULTFD
int shmem_mfill_atomic_pte(pmd_t *dst_pmd,
			   struct vm_area_struct *dst_vma,
			   unsigned long dst_addr,
			   unsigned long src_addr,
			   uffd_flags_t flags,
			   struct folio **foliop)
{
	struct inode *inode = file_inode(dst_vma->vm_file);
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct address_space *mapping = inode->i_mapping;
	gfp_t gfp = mapping_gfp_mask(mapping);
	pgoff_t pgoff = linear_page_index(dst_vma, dst_addr);
	void *page_kaddr;
	struct folio *folio;
	int ret;
	pgoff_t max_off;

	if (shmem_inode_acct_blocks(inode, 1)) {
		/*
		 * We may have got a page, returned -ENOENT triggering a retry,
		 * and now we find ourselves with -ENOMEM. Release the page, to
		 * avoid a BUG_ON in our caller.
		 */
		if (unlikely(*foliop)) {
			folio_put(*foliop);
			*foliop = NULL;
		}
		return -ENOMEM;
	}

	if (!*foliop) {
		ret = -ENOMEM;
		folio = shmem_alloc_folio(gfp, 0, info, pgoff);
		if (!folio)
			goto out_unacct_blocks;

		if (uffd_flags_mode_is(flags, MFILL_ATOMIC_COPY)) {
			page_kaddr = kmap_local_folio(folio, 0);
			/*
			 * The read mmap_lock is held here.  Despite the
			 * mmap_lock being read recursive a deadlock is still
			 * possible if a writer has taken a lock.  For example:
			 *
			 * process A thread 1 takes read lock on own mmap_lock
			 * process A thread 2 calls mmap, blocks taking write lock
			 * process B thread 1 takes page fault, read lock on own mmap lock
			 * process B thread 2 calls mmap, blocks taking write lock
			 * process A thread 1 blocks taking read lock on process B
			 * process B thread 1 blocks taking read lock on process A
			 *
			 * Disable page faults to prevent potential deadlock
			 * and retry the copy outside the mmap_lock.
			 */
			pagefault_disable();
			ret = copy_from_user(page_kaddr,
					     (const void __user *)src_addr,
					     PAGE_SIZE);
			pagefault_enable();
			kunmap_local(page_kaddr);

			/* fallback to copy_from_user outside mmap_lock */
			if (unlikely(ret)) {
				*foliop = folio;
				ret = -ENOENT;
				/* don't free the page */
				goto out_unacct_blocks;
			}

			flush_dcache_folio(folio);
		} else {		/* ZEROPAGE */
			clear_user_highpage(&folio->page, dst_addr);
		}
	} else {
		folio = *foliop;
		VM_BUG_ON_FOLIO(folio_test_large(folio), folio);
		*foliop = NULL;
	}

	VM_BUG_ON(folio_test_locked(folio));
	VM_BUG_ON(folio_test_swapbacked(folio));
	__folio_set_locked(folio);
	__folio_set_swapbacked(folio);
	__folio_mark_uptodate(folio);

	ret = -EFAULT;
	max_off = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);
	if (unlikely(pgoff >= max_off))
		goto out_release;

	ret = mem_cgroup_charge(folio, dst_vma->vm_mm, gfp);
	if (ret)
		goto out_release;
	ret = shmem_add_to_page_cache(folio, mapping, pgoff, NULL, gfp);
	if (ret)
		goto out_release;

	ret = mfill_atomic_install_pte(dst_pmd, dst_vma, dst_addr,
				       &folio->page, true, flags);
	if (ret)
		goto out_delete_from_cache;

	shmem_recalc_inode(inode, 1, 0);
	folio_unlock(folio);
	return 0;
out_delete_from_cache:
	filemap_remove_folio(folio);
out_release:
	folio_unlock(folio);
	folio_put(folio);
out_unacct_blocks:
	shmem_inode_unacct_blocks(inode, 1);
	return ret;
}
#endif /* CONFIG_USERFAULTFD */

#ifdef CONFIG_TMPFS
static const struct inode_operations shmem_symlink_inode_operations;
static const struct inode_operations shmem_short_symlink_operations;

static int
shmem_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct folio **foliop, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct shmem_inode_info *info = SHMEM_I(inode);
	pgoff_t index = pos >> PAGE_SHIFT;
	struct folio *folio;
	int ret = 0;

	/* i_rwsem is held by caller */
	if (unlikely(info->seals & (F_SEAL_GROW |
				   F_SEAL_WRITE | F_SEAL_FUTURE_WRITE))) {
		if (info->seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE))
			return -EPERM;
		if ((info->seals & F_SEAL_GROW) && pos + len > inode->i_size)
			return -EPERM;
	}

	ret = shmem_get_folio(inode, index, pos + len, &folio, SGP_WRITE);
	if (ret)
		return ret;

	if (folio_test_hwpoison(folio) ||
	    (folio_test_large(folio) && folio_test_has_hwpoisoned(folio))) {
		folio_unlock(folio);
		folio_put(folio);
		return -EIO;
	}

	*foliop = folio;
	return 0;
}

static int
shmem_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct folio *folio, void *fsdata)
{
	struct inode *inode = mapping->host;

	if (pos + copied > inode->i_size)
		i_size_write(inode, pos + copied);

	if (!folio_test_uptodate(folio)) {
		if (copied < folio_size(folio)) {
			size_t from = offset_in_folio(folio, pos);
			folio_zero_segments(folio, 0, from,
					from + copied, folio_size(folio));
		}
		folio_mark_uptodate(folio);
	}
	folio_mark_dirty(folio);
	folio_unlock(folio);
	folio_put(folio);

	return copied;
}

static ssize_t shmem_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index;
	unsigned long offset;
	int error = 0;
	ssize_t retval = 0;

	for (;;) {
		struct folio *folio = NULL;
		struct page *page = NULL;
		unsigned long nr, ret;
		loff_t end_offset, i_size = i_size_read(inode);
		bool fallback_page_copy = false;
		size_t fsize;

		if (unlikely(iocb->ki_pos >= i_size))
			break;

		index = iocb->ki_pos >> PAGE_SHIFT;
		error = shmem_get_folio(inode, index, 0, &folio, SGP_READ);
		if (error) {
			if (error == -EINVAL)
				error = 0;
			break;
		}
		if (folio) {
			folio_unlock(folio);

			page = folio_file_page(folio, index);
			if (PageHWPoison(page)) {
				folio_put(folio);
				error = -EIO;
				break;
			}

			if (folio_test_large(folio) &&
			    folio_test_has_hwpoisoned(folio))
				fallback_page_copy = true;
		}

		/*
		 * We must evaluate after, since reads (unlike writes)
		 * are called without i_rwsem protection against truncate
		 */
		i_size = i_size_read(inode);
		if (unlikely(iocb->ki_pos >= i_size)) {
			if (folio)
				folio_put(folio);
			break;
		}
		end_offset = min_t(loff_t, i_size, iocb->ki_pos + to->count);
		if (folio && likely(!fallback_page_copy))
			fsize = folio_size(folio);
		else
			fsize = PAGE_SIZE;
		offset = iocb->ki_pos & (fsize - 1);
		nr = min_t(loff_t, end_offset - iocb->ki_pos, fsize - offset);

		if (folio) {
			/*
			 * If users can be writing to this page using arbitrary
			 * virtual addresses, take care about potential aliasing
			 * before reading the page on the kernel side.
			 */
			if (mapping_writably_mapped(mapping)) {
				if (likely(!fallback_page_copy))
					flush_dcache_folio(folio);
				else
					flush_dcache_page(page);
			}

			/*
			 * Mark the folio accessed if we read the beginning.
			 */
			if (!offset)
				folio_mark_accessed(folio);
			/*
			 * Ok, we have the page, and it's up-to-date, so
			 * now we can copy it to user space...
			 */
			if (likely(!fallback_page_copy))
				ret = copy_folio_to_iter(folio, offset, nr, to);
			else
				ret = copy_page_to_iter(page, offset, nr, to);
			folio_put(folio);
		} else if (user_backed_iter(to)) {
			/*
			 * Copy to user tends to be so well optimized, but
			 * clear_user() not so much, that it is noticeably
			 * faster to copy the zero page instead of clearing.
			 */
			ret = copy_page_to_iter(ZERO_PAGE(0), offset, nr, to);
		} else {
			/*
			 * But submitting the same page twice in a row to
			 * splice() - or others? - can result in confusion:
			 * so don't attempt that optimization on pipes etc.
			 */
			ret = iov_iter_zero(nr, to);
		}

		retval += ret;
		iocb->ki_pos += ret;

		if (!iov_iter_count(to))
			break;
		if (ret < nr) {
			error = -EFAULT;
			break;
		}
		cond_resched();
	}

	file_accessed(file);
	return retval ? retval : error;
}

static ssize_t shmem_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto unlock;
	ret = file_remove_privs(file);
	if (ret)
		goto unlock;
	ret = file_update_time(file);
	if (ret)
		goto unlock;
	ret = generic_perform_write(iocb, from);
unlock:
	inode_unlock(inode);
	return ret;
}

static bool zero_pipe_buf_get(struct pipe_inode_info *pipe,
			      struct pipe_buffer *buf)
{
	return true;
}

static void zero_pipe_buf_release(struct pipe_inode_info *pipe,
				  struct pipe_buffer *buf)
{
}

static bool zero_pipe_buf_try_steal(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	return false;
}

static const struct pipe_buf_operations zero_pipe_buf_ops = {
	.release	= zero_pipe_buf_release,
	.try_steal	= zero_pipe_buf_try_steal,
	.get		= zero_pipe_buf_get,
};

static size_t splice_zeropage_into_pipe(struct pipe_inode_info *pipe,
					loff_t fpos, size_t size)
{
	size_t offset = fpos & ~PAGE_MASK;

	size = min_t(size_t, size, PAGE_SIZE - offset);

	if (!pipe_is_full(pipe)) {
		struct pipe_buffer *buf = pipe_head_buf(pipe);

		*buf = (struct pipe_buffer) {
			.ops	= &zero_pipe_buf_ops,
			.page	= ZERO_PAGE(0),
			.offset	= offset,
			.len	= size,
		};
		pipe->head++;
	}

	return size;
}

static ssize_t shmem_file_splice_read(struct file *in, loff_t *ppos,
				      struct pipe_inode_info *pipe,
				      size_t len, unsigned int flags)
{
	struct inode *inode = file_inode(in);
	struct address_space *mapping = inode->i_mapping;
	struct folio *folio = NULL;
	size_t total_spliced = 0, used, npages, n, part;
	loff_t isize;
	int error = 0;

	/* Work out how much data we can actually add into the pipe */
	used = pipe_buf_usage(pipe);
	npages = max_t(ssize_t, pipe->max_usage - used, 0);
	len = min_t(size_t, len, npages * PAGE_SIZE);

	do {
		bool fallback_page_splice = false;
		struct page *page = NULL;
		pgoff_t index;
		size_t size;

		if (*ppos >= i_size_read(inode))
			break;

		index = *ppos >> PAGE_SHIFT;
		error = shmem_get_folio(inode, index, 0, &folio, SGP_READ);
		if (error) {
			if (error == -EINVAL)
				error = 0;
			break;
		}
		if (folio) {
			folio_unlock(folio);

			page = folio_file_page(folio, index);
			if (PageHWPoison(page)) {
				error = -EIO;
				break;
			}

			if (folio_test_large(folio) &&
			    folio_test_has_hwpoisoned(folio))
				fallback_page_splice = true;
		}

		/*
		 * i_size must be checked after we know the pages are Uptodate.
		 *
		 * Checking i_size after the check allows us to calculate
		 * the correct value for "nr", which means the zero-filled
		 * part of the page is not copied back to userspace (unless
		 * another truncate extends the file - this is desired though).
		 */
		isize = i_size_read(inode);
		if (unlikely(*ppos >= isize))
			break;
		/*
		 * Fallback to PAGE_SIZE splice if the large folio has hwpoisoned
		 * pages.
		 */
		size = len;
		if (unlikely(fallback_page_splice)) {
			size_t offset = *ppos & ~PAGE_MASK;

			size = umin(size, PAGE_SIZE - offset);
		}
		part = min_t(loff_t, isize - *ppos, size);

		if (folio) {
			/*
			 * If users can be writing to this page using arbitrary
			 * virtual addresses, take care about potential aliasing
			 * before reading the page on the kernel side.
			 */
			if (mapping_writably_mapped(mapping)) {
				if (likely(!fallback_page_splice))
					flush_dcache_folio(folio);
				else
					flush_dcache_page(page);
			}
			folio_mark_accessed(folio);
			/*
			 * Ok, we have the page, and it's up-to-date, so we can
			 * now splice it into the pipe.
			 */
			n = splice_folio_into_pipe(pipe, folio, *ppos, part);
			folio_put(folio);
			folio = NULL;
		} else {
			n = splice_zeropage_into_pipe(pipe, *ppos, part);
		}

		if (!n)
			break;
		len -= n;
		total_spliced += n;
		*ppos += n;
		in->f_ra.prev_pos = *ppos;
		if (pipe_is_full(pipe))
			break;

		cond_resched();
	} while (len);

	if (folio)
		folio_put(folio);

	file_accessed(in);
	return total_spliced ? total_spliced : error;
}

static loff_t shmem_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;

	if (whence != SEEK_DATA && whence != SEEK_HOLE)
		return generic_file_llseek_size(file, offset, whence,
					MAX_LFS_FILESIZE, i_size_read(inode));
	if (offset < 0)
		return -ENXIO;

	inode_lock(inode);
	/* We're holding i_rwsem so we can access i_size directly */
	offset = mapping_seek_hole_data(mapping, offset, inode->i_size, whence);
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
	pgoff_t start, index, end, undo_fallocend;
	int error;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	inode_lock(inode);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		struct address_space *mapping = file->f_mapping;
		loff_t unmap_start = round_up(offset, PAGE_SIZE);
		loff_t unmap_end = round_down(offset + len, PAGE_SIZE) - 1;
		DECLARE_WAIT_QUEUE_HEAD_ONSTACK(shmem_falloc_waitq);

		/* protected by i_rwsem */
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

	/*
	 * info->fallocend is only relevant when huge pages might be
	 * involved: to prevent split_huge_page() freeing fallocated
	 * pages when FALLOC_FL_KEEP_SIZE committed beyond i_size.
	 */
	undo_fallocend = info->fallocend;
	if (info->fallocend < end)
		info->fallocend = end;

	for (index = start; index < end; ) {
		struct folio *folio;

		/*
		 * Check for fatal signal so that we abort early in OOM
		 * situations. We don't want to abort in case of non-fatal
		 * signals as large fallocate can take noticeable time and
		 * e.g. periodic timers may result in fallocate constantly
		 * restarting.
		 */
		if (fatal_signal_pending(current))
			error = -EINTR;
		else if (shmem_falloc.nr_unswapped > shmem_falloc.nr_falloced)
			error = -ENOMEM;
		else
			error = shmem_get_folio(inode, index, offset + len,
						&folio, SGP_FALLOC);
		if (error) {
			info->fallocend = undo_fallocend;
			/* Remove the !uptodate folios we added */
			if (index > start) {
				shmem_undo_range(inode,
				    (loff_t)start << PAGE_SHIFT,
				    ((loff_t)index << PAGE_SHIFT) - 1, true);
			}
			goto undone;
		}

		/*
		 * Here is a more important optimization than it appears:
		 * a second SGP_FALLOC on the same large folio will clear it,
		 * making it uptodate and un-undoable if we fail later.
		 */
		index = folio_next_index(folio);
		/* Beware 32-bit wraparound */
		if (!index)
			index--;

		/*
		 * Inform shmem_writepage() how far we have reached.
		 * No need for lock or barrier: we have the page lock.
		 */
		if (!folio_test_uptodate(folio))
			shmem_falloc.nr_falloced += index - shmem_falloc.next;
		shmem_falloc.next = index;

		/*
		 * If !uptodate, leave it that way so that freeable folios
		 * can be recognized if we need to rollback on error later.
		 * But mark it dirty so that memory pressure will swap rather
		 * than free the folios we are allocating (and SGP_CACHE folios
		 * might still be clean: we now need to mark those dirty too).
		 */
		folio_mark_dirty(folio);
		folio_unlock(folio);
		folio_put(folio);
		cond_resched();
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
undone:
	spin_lock(&inode->i_lock);
	inode->i_private = NULL;
	spin_unlock(&inode->i_lock);
out:
	if (!error)
		file_modified(file);
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
		buf->f_ffree = sbinfo->free_ispace / BOGO_INODE_SIZE;
	}
	/* else leave those fields 0 like simple_statfs */

	buf->f_fsid = uuid_to_fsid(dentry->d_sb->s_uuid.b);

	return 0;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int
shmem_mknod(struct mnt_idmap *idmap, struct inode *dir,
	    struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error;

	if (!generic_ci_validate_strict_name(dir, &dentry->d_name))
		return -EINVAL;

	inode = shmem_get_inode(idmap, dir->i_sb, dir, mode, dev, VM_NORESERVE);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	error = simple_acl_create(dir, inode);
	if (error)
		goto out_iput;
	error = security_inode_init_security(inode, dir, &dentry->d_name,
					     shmem_initxattrs, NULL);
	if (error && error != -EOPNOTSUPP)
		goto out_iput;

	error = simple_offset_add(shmem_get_offset_ctx(dir), dentry);
	if (error)
		goto out_iput;

	dir->i_size += BOGO_DIRENT_SIZE;
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	inode_inc_iversion(dir);

	if (IS_ENABLED(CONFIG_UNICODE) && IS_CASEFOLDED(dir))
		d_add(dentry, inode);
	else
		d_instantiate(dentry, inode);

	dget(dentry); /* Extra count - pin the dentry in core */
	return error;

out_iput:
	iput(inode);
	return error;
}

static int
shmem_tmpfile(struct mnt_idmap *idmap, struct inode *dir,
	      struct file *file, umode_t mode)
{
	struct inode *inode;
	int error;

	inode = shmem_get_inode(idmap, dir->i_sb, dir, mode, 0, VM_NORESERVE);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		goto err_out;
	}
	error = security_inode_init_security(inode, dir, NULL,
					     shmem_initxattrs, NULL);
	if (error && error != -EOPNOTSUPP)
		goto out_iput;
	error = simple_acl_create(dir, inode);
	if (error)
		goto out_iput;
	d_tmpfile(file, inode);

err_out:
	return finish_open_simple(file, error);
out_iput:
	iput(inode);
	return error;
}

static int shmem_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	int error;

	error = shmem_mknod(idmap, dir, dentry, mode | S_IFDIR, 0);
	if (error)
		return error;
	inc_nlink(dir);
	return 0;
}

static int shmem_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return shmem_mknod(idmap, dir, dentry, mode | S_IFREG, 0);
}

/*
 * Link a file..
 */
static int shmem_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
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

	ret = simple_offset_add(shmem_get_offset_ctx(dir), dentry);
	if (ret) {
		if (inode->i_nlink)
			shmem_free_inode(inode->i_sb, 0);
		goto out;
	}

	dir->i_size += BOGO_DIRENT_SIZE;
	inode_set_mtime_to_ts(dir,
			      inode_set_ctime_to_ts(dir, inode_set_ctime_current(inode)));
	inode_inc_iversion(dir);
	inc_nlink(inode);
	ihold(inode);	/* New dentry reference */
	dget(dentry);	/* Extra pinning count for the created dentry */
	if (IS_ENABLED(CONFIG_UNICODE) && IS_CASEFOLDED(dir))
		d_add(dentry, inode);
	else
		d_instantiate(dentry, inode);
out:
	return ret;
}

static int shmem_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode->i_nlink > 1 && !S_ISDIR(inode->i_mode))
		shmem_free_inode(inode->i_sb, 0);

	simple_offset_remove(shmem_get_offset_ctx(dir), dentry);

	dir->i_size -= BOGO_DIRENT_SIZE;
	inode_set_mtime_to_ts(dir,
			      inode_set_ctime_to_ts(dir, inode_set_ctime_current(inode)));
	inode_inc_iversion(dir);
	drop_nlink(inode);
	dput(dentry);	/* Undo the count from "create" - does all the work */

	/*
	 * For now, VFS can't deal with case-insensitive negative dentries, so
	 * we invalidate them
	 */
	if (IS_ENABLED(CONFIG_UNICODE) && IS_CASEFOLDED(dir))
		d_invalidate(dentry);

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

static int shmem_whiteout(struct mnt_idmap *idmap,
			  struct inode *old_dir, struct dentry *old_dentry)
{
	struct dentry *whiteout;
	int error;

	whiteout = d_alloc(old_dentry->d_parent, &old_dentry->d_name);
	if (!whiteout)
		return -ENOMEM;

	error = shmem_mknod(idmap, old_dir, whiteout,
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
static int shmem_rename2(struct mnt_idmap *idmap,
			 struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry,
			 unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	int they_are_dirs = S_ISDIR(inode->i_mode);
	int error;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return simple_offset_rename_exchange(old_dir, old_dentry,
						     new_dir, new_dentry);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (flags & RENAME_WHITEOUT) {
		error = shmem_whiteout(idmap, old_dir, old_dentry);
		if (error)
			return error;
	}

	error = simple_offset_rename(old_dir, old_dentry, new_dir, new_dentry);
	if (error)
		return error;

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
	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);
	inode_inc_iversion(old_dir);
	inode_inc_iversion(new_dir);
	return 0;
}

static int shmem_symlink(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	int error;
	int len;
	struct inode *inode;
	struct folio *folio;
	char *link;

	len = strlen(symname) + 1;
	if (len > PAGE_SIZE)
		return -ENAMETOOLONG;

	inode = shmem_get_inode(idmap, dir->i_sb, dir, S_IFLNK | 0777, 0,
				VM_NORESERVE);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	error = security_inode_init_security(inode, dir, &dentry->d_name,
					     shmem_initxattrs, NULL);
	if (error && error != -EOPNOTSUPP)
		goto out_iput;

	error = simple_offset_add(shmem_get_offset_ctx(dir), dentry);
	if (error)
		goto out_iput;

	inode->i_size = len-1;
	if (len <= SHORT_SYMLINK_LEN) {
		link = kmemdup(symname, len, GFP_KERNEL);
		if (!link) {
			error = -ENOMEM;
			goto out_remove_offset;
		}
		inode->i_op = &shmem_short_symlink_operations;
		inode_set_cached_link(inode, link, len - 1);
	} else {
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &shmem_aops;
		error = shmem_get_folio(inode, 0, 0, &folio, SGP_WRITE);
		if (error)
			goto out_remove_offset;
		inode->i_op = &shmem_symlink_inode_operations;
		memcpy(folio_address(folio), symname, len);
		folio_mark_uptodate(folio);
		folio_mark_dirty(folio);
		folio_unlock(folio);
		folio_put(folio);
	}
	dir->i_size += BOGO_DIRENT_SIZE;
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	inode_inc_iversion(dir);
	if (IS_ENABLED(CONFIG_UNICODE) && IS_CASEFOLDED(dir))
		d_add(dentry, inode);
	else
		d_instantiate(dentry, inode);
	dget(dentry);
	return 0;

out_remove_offset:
	simple_offset_remove(shmem_get_offset_ctx(dir), dentry);
out_iput:
	iput(inode);
	return error;
}

static void shmem_put_link(void *arg)
{
	folio_mark_accessed(arg);
	folio_put(arg);
}

static const char *shmem_get_link(struct dentry *dentry, struct inode *inode,
				  struct delayed_call *done)
{
	struct folio *folio = NULL;
	int error;

	if (!dentry) {
		folio = filemap_get_folio(inode->i_mapping, 0);
		if (IS_ERR(folio))
			return ERR_PTR(-ECHILD);
		if (PageHWPoison(folio_page(folio, 0)) ||
		    !folio_test_uptodate(folio)) {
			folio_put(folio);
			return ERR_PTR(-ECHILD);
		}
	} else {
		error = shmem_get_folio(inode, 0, 0, &folio, SGP_READ);
		if (error)
			return ERR_PTR(error);
		if (!folio)
			return ERR_PTR(-ECHILD);
		if (PageHWPoison(folio_page(folio, 0))) {
			folio_unlock(folio);
			folio_put(folio);
			return ERR_PTR(-ECHILD);
		}
		folio_unlock(folio);
	}
	set_delayed_call(done, shmem_put_link, folio);
	return folio_address(folio);
}

#ifdef CONFIG_TMPFS_XATTR

static int shmem_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));

	fileattr_fill_flags(fa, info->fsflags & SHMEM_FL_USER_VISIBLE);

	return 0;
}

static int shmem_fileattr_set(struct mnt_idmap *idmap,
			      struct dentry *dentry, struct fileattr *fa)
{
	struct inode *inode = d_inode(dentry);
	struct shmem_inode_info *info = SHMEM_I(inode);
	int ret, flags;

	if (fileattr_has_fsx(fa))
		return -EOPNOTSUPP;
	if (fa->flags & ~SHMEM_FL_USER_MODIFIABLE)
		return -EOPNOTSUPP;

	flags = (info->fsflags & ~SHMEM_FL_USER_MODIFIABLE) |
		(fa->flags & SHMEM_FL_USER_MODIFIABLE);

	ret = shmem_set_inode_flags(inode, flags, dentry);

	if (ret)
		return ret;

	info->fsflags = flags;

	inode_set_ctime_current(inode);
	inode_inc_iversion(inode);
	return 0;
}

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
			    const struct xattr *xattr_array, void *fs_info)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	const struct xattr *xattr;
	struct simple_xattr *new_xattr;
	size_t ispace = 0;
	size_t len;

	if (sbinfo->max_inodes) {
		for (xattr = xattr_array; xattr->name != NULL; xattr++) {
			ispace += simple_xattr_space(xattr->name,
				xattr->value_len + XATTR_SECURITY_PREFIX_LEN);
		}
		if (ispace) {
			raw_spin_lock(&sbinfo->stat_lock);
			if (sbinfo->free_ispace < ispace)
				ispace = 0;
			else
				sbinfo->free_ispace -= ispace;
			raw_spin_unlock(&sbinfo->stat_lock);
			if (!ispace)
				return -ENOSPC;
		}
	}

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		new_xattr = simple_xattr_alloc(xattr->value, xattr->value_len);
		if (!new_xattr)
			break;

		len = strlen(xattr->name) + 1;
		new_xattr->name = kmalloc(XATTR_SECURITY_PREFIX_LEN + len,
					  GFP_KERNEL_ACCOUNT);
		if (!new_xattr->name) {
			kvfree(new_xattr);
			break;
		}

		memcpy(new_xattr->name, XATTR_SECURITY_PREFIX,
		       XATTR_SECURITY_PREFIX_LEN);
		memcpy(new_xattr->name + XATTR_SECURITY_PREFIX_LEN,
		       xattr->name, len);

		simple_xattr_add(&info->xattrs, new_xattr);
	}

	if (xattr->name != NULL) {
		if (ispace) {
			raw_spin_lock(&sbinfo->stat_lock);
			sbinfo->free_ispace += ispace;
			raw_spin_unlock(&sbinfo->stat_lock);
		}
		simple_xattrs_free(&info->xattrs, NULL);
		return -ENOMEM;
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
				   struct mnt_idmap *idmap,
				   struct dentry *unused, struct inode *inode,
				   const char *name, const void *value,
				   size_t size, int flags)
{
	struct shmem_inode_info *info = SHMEM_I(inode);
	struct shmem_sb_info *sbinfo = SHMEM_SB(inode->i_sb);
	struct simple_xattr *old_xattr;
	size_t ispace = 0;

	name = xattr_full_name(handler, name);
	if (value && sbinfo->max_inodes) {
		ispace = simple_xattr_space(name, size);
		raw_spin_lock(&sbinfo->stat_lock);
		if (sbinfo->free_ispace < ispace)
			ispace = 0;
		else
			sbinfo->free_ispace -= ispace;
		raw_spin_unlock(&sbinfo->stat_lock);
		if (!ispace)
			return -ENOSPC;
	}

	old_xattr = simple_xattr_set(&info->xattrs, name, value, size, flags);
	if (!IS_ERR(old_xattr)) {
		ispace = 0;
		if (old_xattr && sbinfo->max_inodes)
			ispace = simple_xattr_space(old_xattr->name,
						    old_xattr->size);
		simple_xattr_free(old_xattr);
		old_xattr = NULL;
		inode_set_ctime_current(inode);
		inode_inc_iversion(inode);
	}
	if (ispace) {
		raw_spin_lock(&sbinfo->stat_lock);
		sbinfo->free_ispace += ispace;
		raw_spin_unlock(&sbinfo->stat_lock);
	}
	return PTR_ERR(old_xattr);
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

static const struct xattr_handler shmem_user_xattr_handler = {
	.prefix = XATTR_USER_PREFIX,
	.get = shmem_xattr_handler_get,
	.set = shmem_xattr_handler_set,
};

static const struct xattr_handler * const shmem_xattr_handlers[] = {
	&shmem_security_xattr_handler,
	&shmem_trusted_xattr_handler,
	&shmem_user_xattr_handler,
	NULL
};

static ssize_t shmem_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct shmem_inode_info *info = SHMEM_I(d_inode(dentry));
	return simple_xattr_list(d_inode(dentry), &info->xattrs, buffer, size);
}
#endif /* CONFIG_TMPFS_XATTR */

static const struct inode_operations shmem_short_symlink_operations = {
	.getattr	= shmem_getattr,
	.setattr	= shmem_setattr,
	.get_link	= simple_get_link,
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
#endif
};

static const struct inode_operations shmem_symlink_inode_operations = {
	.getattr	= shmem_getattr,
	.setattr	= shmem_setattr,
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
	Opt_noswap,
	Opt_quota,
	Opt_usrquota,
	Opt_grpquota,
	Opt_usrquota_block_hardlimit,
	Opt_usrquota_inode_hardlimit,
	Opt_grpquota_block_hardlimit,
	Opt_grpquota_inode_hardlimit,
	Opt_casefold_version,
	Opt_casefold,
	Opt_strict_encoding,
};

static const struct constant_table shmem_param_enums_huge[] = {
	{"never",	SHMEM_HUGE_NEVER },
	{"always",	SHMEM_HUGE_ALWAYS },
	{"within_size",	SHMEM_HUGE_WITHIN_SIZE },
	{"advise",	SHMEM_HUGE_ADVISE },
	{}
};

const struct fs_parameter_spec shmem_fs_parameters[] = {
	fsparam_gid   ("gid",		Opt_gid),
	fsparam_enum  ("huge",		Opt_huge,  shmem_param_enums_huge),
	fsparam_u32oct("mode",		Opt_mode),
	fsparam_string("mpol",		Opt_mpol),
	fsparam_string("nr_blocks",	Opt_nr_blocks),
	fsparam_string("nr_inodes",	Opt_nr_inodes),
	fsparam_string("size",		Opt_size),
	fsparam_uid   ("uid",		Opt_uid),
	fsparam_flag  ("inode32",	Opt_inode32),
	fsparam_flag  ("inode64",	Opt_inode64),
	fsparam_flag  ("noswap",	Opt_noswap),
#ifdef CONFIG_TMPFS_QUOTA
	fsparam_flag  ("quota",		Opt_quota),
	fsparam_flag  ("usrquota",	Opt_usrquota),
	fsparam_flag  ("grpquota",	Opt_grpquota),
	fsparam_string("usrquota_block_hardlimit", Opt_usrquota_block_hardlimit),
	fsparam_string("usrquota_inode_hardlimit", Opt_usrquota_inode_hardlimit),
	fsparam_string("grpquota_block_hardlimit", Opt_grpquota_block_hardlimit),
	fsparam_string("grpquota_inode_hardlimit", Opt_grpquota_inode_hardlimit),
#endif
	fsparam_string("casefold",	Opt_casefold_version),
	fsparam_flag  ("casefold",	Opt_casefold),
	fsparam_flag  ("strict_encoding", Opt_strict_encoding),
	{}
};

#if IS_ENABLED(CONFIG_UNICODE)
static int shmem_parse_opt_casefold(struct fs_context *fc, struct fs_parameter *param,
				    bool latest_version)
{
	struct shmem_options *ctx = fc->fs_private;
	int version = UTF8_LATEST;
	struct unicode_map *encoding;
	char *version_str = param->string + 5;

	if (!latest_version) {
		if (strncmp(param->string, "utf8-", 5))
			return invalfc(fc, "Only UTF-8 encodings are supported "
				       "in the format: utf8-<version number>");

		version = utf8_parse_version(version_str);
		if (version < 0)
			return invalfc(fc, "Invalid UTF-8 version: %s", version_str);
	}

	encoding = utf8_load(version);

	if (IS_ERR(encoding)) {
		return invalfc(fc, "Failed loading UTF-8 version: utf8-%u.%u.%u\n",
			       unicode_major(version), unicode_minor(version),
			       unicode_rev(version));
	}

	pr_info("tmpfs: Using encoding : utf8-%u.%u.%u\n",
		unicode_major(version), unicode_minor(version), unicode_rev(version));

	ctx->encoding = encoding;

	return 0;
}
#else
static int shmem_parse_opt_casefold(struct fs_context *fc, struct fs_parameter *param,
				    bool latest_version)
{
	return invalfc(fc, "tmpfs: Kernel not built with CONFIG_UNICODE\n");
}
#endif

static int shmem_parse_one(struct fs_context *fc, struct fs_parameter *param)
{
	struct shmem_options *ctx = fc->fs_private;
	struct fs_parse_result result;
	unsigned long long size;
	char *rest;
	int opt;
	kuid_t kuid;
	kgid_t kgid;

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
		if (*rest || ctx->blocks > LONG_MAX)
			goto bad_value;
		ctx->seen |= SHMEM_SEEN_BLOCKS;
		break;
	case Opt_nr_inodes:
		ctx->inodes = memparse(param->string, &rest);
		if (*rest || ctx->inodes > ULONG_MAX / BOGO_INODE_SIZE)
			goto bad_value;
		ctx->seen |= SHMEM_SEEN_INODES;
		break;
	case Opt_mode:
		ctx->mode = result.uint_32 & 07777;
		break;
	case Opt_uid:
		kuid = result.uid;

		/*
		 * The requested uid must be representable in the
		 * filesystem's idmapping.
		 */
		if (!kuid_has_mapping(fc->user_ns, kuid))
			goto bad_value;

		ctx->uid = kuid;
		break;
	case Opt_gid:
		kgid = result.gid;

		/*
		 * The requested gid must be representable in the
		 * filesystem's idmapping.
		 */
		if (!kgid_has_mapping(fc->user_ns, kgid))
			goto bad_value;

		ctx->gid = kgid;
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
	case Opt_noswap:
		if ((fc->user_ns != &init_user_ns) || !capable(CAP_SYS_ADMIN)) {
			return invalfc(fc,
				       "Turning off swap in unprivileged tmpfs mounts unsupported");
		}
		ctx->noswap = true;
		ctx->seen |= SHMEM_SEEN_NOSWAP;
		break;
	case Opt_quota:
		if (fc->user_ns != &init_user_ns)
			return invalfc(fc, "Quotas in unprivileged tmpfs mounts are unsupported");
		ctx->seen |= SHMEM_SEEN_QUOTA;
		ctx->quota_types |= (QTYPE_MASK_USR | QTYPE_MASK_GRP);
		break;
	case Opt_usrquota:
		if (fc->user_ns != &init_user_ns)
			return invalfc(fc, "Quotas in unprivileged tmpfs mounts are unsupported");
		ctx->seen |= SHMEM_SEEN_QUOTA;
		ctx->quota_types |= QTYPE_MASK_USR;
		break;
	case Opt_grpquota:
		if (fc->user_ns != &init_user_ns)
			return invalfc(fc, "Quotas in unprivileged tmpfs mounts are unsupported");
		ctx->seen |= SHMEM_SEEN_QUOTA;
		ctx->quota_types |= QTYPE_MASK_GRP;
		break;
	case Opt_usrquota_block_hardlimit:
		size = memparse(param->string, &rest);
		if (*rest || !size)
			goto bad_value;
		if (size > SHMEM_QUOTA_MAX_SPC_LIMIT)
			return invalfc(fc,
				       "User quota block hardlimit too large.");
		ctx->qlimits.usrquota_bhardlimit = size;
		break;
	case Opt_grpquota_block_hardlimit:
		size = memparse(param->string, &rest);
		if (*rest || !size)
			goto bad_value;
		if (size > SHMEM_QUOTA_MAX_SPC_LIMIT)
			return invalfc(fc,
				       "Group quota block hardlimit too large.");
		ctx->qlimits.grpquota_bhardlimit = size;
		break;
	case Opt_usrquota_inode_hardlimit:
		size = memparse(param->string, &rest);
		if (*rest || !size)
			goto bad_value;
		if (size > SHMEM_QUOTA_MAX_INO_LIMIT)
			return invalfc(fc,
				       "User quota inode hardlimit too large.");
		ctx->qlimits.usrquota_ihardlimit = size;
		break;
	case Opt_grpquota_inode_hardlimit:
		size = memparse(param->string, &rest);
		if (*rest || !size)
			goto bad_value;
		if (size > SHMEM_QUOTA_MAX_INO_LIMIT)
			return invalfc(fc,
				       "Group quota inode hardlimit too large.");
		ctx->qlimits.grpquota_ihardlimit = size;
		break;
	case Opt_casefold_version:
		return shmem_parse_opt_casefold(fc, param, false);
	case Opt_casefold:
		return shmem_parse_opt_casefold(fc, param, true);
	case Opt_strict_encoding:
#if IS_ENABLED(CONFIG_UNICODE)
		ctx->strict_encoding = true;
		break;
#else
		return invalfc(fc, "tmpfs: Kernel not built with CONFIG_UNICODE\n");
#endif
	}
	return 0;

unsupported_parameter:
	return invalfc(fc, "Unsupported parameter '%s'", param->key);
bad_value:
	return invalfc(fc, "Bad value for '%s'", param->key);
}

static char *shmem_next_opt(char **s)
{
	char *sbegin = *s;
	char *p;

	if (sbegin == NULL)
		return NULL;

	/*
	 * NUL-terminate this option: unfortunately,
	 * mount options form a comma-separated list,
	 * but mpol's nodelist may also contain commas.
	 */
	for (;;) {
		p = strchr(*s, ',');
		if (p == NULL)
			break;
		*s = p + 1;
		if (!isdigit(*(p+1))) {
			*p = '\0';
			return sbegin;
		}
	}

	*s = NULL;
	return sbegin;
}

static int shmem_parse_monolithic(struct fs_context *fc, void *data)
{
	return vfs_parse_monolithic_sep(fc, data, shmem_next_opt);
}

/*
 * Reconfigure a shmem filesystem.
 */
static int shmem_reconfigure(struct fs_context *fc)
{
	struct shmem_options *ctx = fc->fs_private;
	struct shmem_sb_info *sbinfo = SHMEM_SB(fc->root->d_sb);
	unsigned long used_isp;
	struct mempolicy *mpol = NULL;
	const char *err;

	raw_spin_lock(&sbinfo->stat_lock);
	used_isp = sbinfo->max_inodes * BOGO_INODE_SIZE - sbinfo->free_ispace;

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
		if (ctx->inodes * BOGO_INODE_SIZE < used_isp) {
			err = "Too few inodes for current use";
			goto out;
		}
	}

	if ((ctx->seen & SHMEM_SEEN_INUMS) && !ctx->full_inums &&
	    sbinfo->next_ino > UINT_MAX) {
		err = "Current inum too high to switch to 32-bit inums";
		goto out;
	}
	if ((ctx->seen & SHMEM_SEEN_NOSWAP) && ctx->noswap && !sbinfo->noswap) {
		err = "Cannot disable swap on remount";
		goto out;
	}
	if (!(ctx->seen & SHMEM_SEEN_NOSWAP) && !ctx->noswap && sbinfo->noswap) {
		err = "Cannot enable swap on remount if it was disabled on first mount";
		goto out;
	}

	if (ctx->seen & SHMEM_SEEN_QUOTA &&
	    !sb_any_quota_loaded(fc->root->d_sb)) {
		err = "Cannot enable quota on remount";
		goto out;
	}

#ifdef CONFIG_TMPFS_QUOTA
#define CHANGED_LIMIT(name)						\
	(ctx->qlimits.name## hardlimit &&				\
	(ctx->qlimits.name## hardlimit != sbinfo->qlimits.name## hardlimit))

	if (CHANGED_LIMIT(usrquota_b) || CHANGED_LIMIT(usrquota_i) ||
	    CHANGED_LIMIT(grpquota_b) || CHANGED_LIMIT(grpquota_i)) {
		err = "Cannot change global quota limit on remount";
		goto out;
	}
#endif /* CONFIG_TMPFS_QUOTA */

	if (ctx->seen & SHMEM_SEEN_HUGE)
		sbinfo->huge = ctx->huge;
	if (ctx->seen & SHMEM_SEEN_INUMS)
		sbinfo->full_inums = ctx->full_inums;
	if (ctx->seen & SHMEM_SEEN_BLOCKS)
		sbinfo->max_blocks  = ctx->blocks;
	if (ctx->seen & SHMEM_SEEN_INODES) {
		sbinfo->max_inodes  = ctx->inodes;
		sbinfo->free_ispace = ctx->inodes * BOGO_INODE_SIZE - used_isp;
	}

	/*
	 * Preserve previous mempolicy unless mpol remount option was specified.
	 */
	if (ctx->mpol) {
		mpol = sbinfo->mpol;
		sbinfo->mpol = ctx->mpol;	/* transfers initial ref */
		ctx->mpol = NULL;
	}

	if (ctx->noswap)
		sbinfo->noswap = true;

	raw_spin_unlock(&sbinfo->stat_lock);
	mpol_put(mpol);
	return 0;
out:
	raw_spin_unlock(&sbinfo->stat_lock);
	return invalfc(fc, "%s", err);
}

static int shmem_show_options(struct seq_file *seq, struct dentry *root)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(root->d_sb);
	struct mempolicy *mpol;

	if (sbinfo->max_blocks != shmem_default_max_blocks())
		seq_printf(seq, ",size=%luk", K(sbinfo->max_blocks));
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
	mpol = shmem_get_sbmpol(sbinfo);
	shmem_show_mpol(seq, mpol);
	mpol_put(mpol);
	if (sbinfo->noswap)
		seq_printf(seq, ",noswap");
#ifdef CONFIG_TMPFS_QUOTA
	if (sb_has_quota_active(root->d_sb, USRQUOTA))
		seq_printf(seq, ",usrquota");
	if (sb_has_quota_active(root->d_sb, GRPQUOTA))
		seq_printf(seq, ",grpquota");
	if (sbinfo->qlimits.usrquota_bhardlimit)
		seq_printf(seq, ",usrquota_block_hardlimit=%lld",
			   sbinfo->qlimits.usrquota_bhardlimit);
	if (sbinfo->qlimits.grpquota_bhardlimit)
		seq_printf(seq, ",grpquota_block_hardlimit=%lld",
			   sbinfo->qlimits.grpquota_bhardlimit);
	if (sbinfo->qlimits.usrquota_ihardlimit)
		seq_printf(seq, ",usrquota_inode_hardlimit=%lld",
			   sbinfo->qlimits.usrquota_ihardlimit);
	if (sbinfo->qlimits.grpquota_ihardlimit)
		seq_printf(seq, ",grpquota_inode_hardlimit=%lld",
			   sbinfo->qlimits.grpquota_ihardlimit);
#endif
	return 0;
}

#endif /* CONFIG_TMPFS */

static void shmem_put_super(struct super_block *sb)
{
	struct shmem_sb_info *sbinfo = SHMEM_SB(sb);

#if IS_ENABLED(CONFIG_UNICODE)
	if (sb->s_encoding)
		utf8_unload(sb->s_encoding);
#endif

#ifdef CONFIG_TMPFS_QUOTA
	shmem_disable_quotas(sb);
#endif
	free_percpu(sbinfo->ino_batch);
	percpu_counter_destroy(&sbinfo->used_blocks);
	mpol_put(sbinfo->mpol);
	kfree(sbinfo);
	sb->s_fs_info = NULL;
}

#if IS_ENABLED(CONFIG_UNICODE) && defined(CONFIG_TMPFS)
static const struct dentry_operations shmem_ci_dentry_ops = {
	.d_hash = generic_ci_d_hash,
	.d_compare = generic_ci_d_compare,
	.d_delete = always_delete_dentry,
};
#endif

static int shmem_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct shmem_options *ctx = fc->fs_private;
	struct inode *inode;
	struct shmem_sb_info *sbinfo;
	int error = -ENOMEM;

	/* Round up to L1_CACHE_BYTES to resist false sharing */
	sbinfo = kzalloc(max((int)sizeof(struct shmem_sb_info),
				L1_CACHE_BYTES), GFP_KERNEL);
	if (!sbinfo)
		return error;

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
		sbinfo->noswap = ctx->noswap;
	} else {
		sb->s_flags |= SB_NOUSER;
	}
	sb->s_export_op = &shmem_export_ops;
	sb->s_flags |= SB_NOSEC | SB_I_VERSION;

#if IS_ENABLED(CONFIG_UNICODE)
	if (!ctx->encoding && ctx->strict_encoding) {
		pr_err("tmpfs: strict_encoding option without encoding is forbidden\n");
		error = -EINVAL;
		goto failed;
	}

	if (ctx->encoding) {
		sb->s_encoding = ctx->encoding;
		sb->s_d_op = &shmem_ci_dentry_ops;
		if (ctx->strict_encoding)
			sb->s_encoding_flags = SB_ENC_STRICT_MODE_FL;
	}
#endif

#else
	sb->s_flags |= SB_NOUSER;
#endif /* CONFIG_TMPFS */
	sbinfo->max_blocks = ctx->blocks;
	sbinfo->max_inodes = ctx->inodes;
	sbinfo->free_ispace = sbinfo->max_inodes * BOGO_INODE_SIZE;
	if (sb->s_flags & SB_KERNMOUNT) {
		sbinfo->ino_batch = alloc_percpu(ino_t);
		if (!sbinfo->ino_batch)
			goto failed;
	}
	sbinfo->uid = ctx->uid;
	sbinfo->gid = ctx->gid;
	sbinfo->full_inums = ctx->full_inums;
	sbinfo->mode = ctx->mode;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (ctx->seen & SHMEM_SEEN_HUGE)
		sbinfo->huge = ctx->huge;
	else
		sbinfo->huge = tmpfs_huge;
#endif
	sbinfo->mpol = ctx->mpol;
	ctx->mpol = NULL;

	raw_spin_lock_init(&sbinfo->stat_lock);
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
	uuid_t uuid;
	uuid_gen(&uuid);
	super_set_uuid(sb, uuid.b, sizeof(uuid));

#ifdef CONFIG_TMPFS_QUOTA
	if (ctx->seen & SHMEM_SEEN_QUOTA) {
		sb->dq_op = &shmem_quota_operations;
		sb->s_qcop = &dquot_quotactl_sysfile_ops;
		sb->s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP;

		/* Copy the default limits from ctx into sbinfo */
		memcpy(&sbinfo->qlimits, &ctx->qlimits,
		       sizeof(struct shmem_quota_limits));

		if (shmem_enable_quotas(sb, ctx->quota_types))
			goto failed;
	}
#endif /* CONFIG_TMPFS_QUOTA */

	inode = shmem_get_inode(&nop_mnt_idmap, sb, NULL,
				S_IFDIR | sbinfo->mode, 0, VM_NORESERVE);
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		goto failed;
	}
	inode->i_uid = sbinfo->uid;
	inode->i_gid = sbinfo->gid;
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto failed;
	return 0;

failed:
	shmem_put_super(sb);
	return error;
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
	.parse_monolithic	= shmem_parse_monolithic,
	.parse_param		= shmem_parse_one,
	.reconfigure		= shmem_reconfigure,
#endif
};

static struct kmem_cache *shmem_inode_cachep __ro_after_init;

static struct inode *shmem_alloc_inode(struct super_block *sb)
{
	struct shmem_inode_info *info;
	info = alloc_inode_sb(sb, shmem_inode_cachep, GFP_KERNEL);
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
	if (S_ISDIR(inode->i_mode))
		simple_offset_destroy(shmem_get_offset_ctx(inode));
}

static void shmem_init_inode(void *foo)
{
	struct shmem_inode_info *info = foo;
	inode_init_once(&info->vfs_inode);
}

static void __init shmem_init_inodecache(void)
{
	shmem_inode_cachep = kmem_cache_create("shmem_inode_cache",
				sizeof(struct shmem_inode_info),
				0, SLAB_PANIC|SLAB_ACCOUNT, shmem_init_inode);
}

static void __init shmem_destroy_inodecache(void)
{
	kmem_cache_destroy(shmem_inode_cachep);
}

/* Keep the page in page cache instead of truncating it */
static int shmem_error_remove_folio(struct address_space *mapping,
				   struct folio *folio)
{
	return 0;
}

static const struct address_space_operations shmem_aops = {
	.writepage	= shmem_writepage,
	.dirty_folio	= noop_dirty_folio,
#ifdef CONFIG_TMPFS
	.write_begin	= shmem_write_begin,
	.write_end	= shmem_write_end,
#endif
#ifdef CONFIG_MIGRATION
	.migrate_folio	= migrate_folio,
#endif
	.error_remove_folio = shmem_error_remove_folio,
};

static const struct file_operations shmem_file_operations = {
	.mmap		= shmem_mmap,
	.open		= shmem_file_open,
	.get_unmapped_area = shmem_get_unmapped_area,
#ifdef CONFIG_TMPFS
	.llseek		= shmem_file_llseek,
	.read_iter	= shmem_file_read_iter,
	.write_iter	= shmem_file_write_iter,
	.fsync		= noop_fsync,
	.splice_read	= shmem_file_splice_read,
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
	.fileattr_get	= shmem_fileattr_get,
	.fileattr_set	= shmem_fileattr_set,
#endif
};

static const struct inode_operations shmem_dir_inode_operations = {
#ifdef CONFIG_TMPFS
	.getattr	= shmem_getattr,
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
	.get_offset_ctx	= shmem_get_offset_ctx,
#endif
#ifdef CONFIG_TMPFS_XATTR
	.listxattr	= shmem_listxattr,
	.fileattr_get	= shmem_fileattr_get,
	.fileattr_set	= shmem_fileattr_set,
#endif
#ifdef CONFIG_TMPFS_POSIX_ACL
	.setattr	= shmem_setattr,
	.set_acl	= simple_set_acl,
#endif
};

static const struct inode_operations shmem_special_inode_operations = {
	.getattr	= shmem_getattr,
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
#ifdef CONFIG_TMPFS_QUOTA
	.get_dquots	= shmem_get_dquots,
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

static const struct vm_operations_struct shmem_anon_vm_ops = {
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

#if IS_ENABLED(CONFIG_UNICODE)
	ctx->encoding = NULL;
#endif

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
	.fs_flags	= FS_USERNS_MOUNT | FS_ALLOW_IDMAP | FS_MGTIME,
};

#if defined(CONFIG_SYSFS) && defined(CONFIG_TMPFS)

#define __INIT_KOBJ_ATTR(_name, _mode, _show, _store)			\
{									\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.show	= _show,						\
	.store	= _store,						\
}

#define TMPFS_ATTR_W(_name, _store)				\
	static struct kobj_attribute tmpfs_attr_##_name =	\
			__INIT_KOBJ_ATTR(_name, 0200, NULL, _store)

#define TMPFS_ATTR_RW(_name, _show, _store)			\
	static struct kobj_attribute tmpfs_attr_##_name =	\
			__INIT_KOBJ_ATTR(_name, 0644, _show, _store)

#define TMPFS_ATTR_RO(_name, _show)				\
	static struct kobj_attribute tmpfs_attr_##_name =	\
			__INIT_KOBJ_ATTR(_name, 0444, _show, NULL)

#if IS_ENABLED(CONFIG_UNICODE)
static ssize_t casefold_show(struct kobject *kobj, struct kobj_attribute *a,
			char *buf)
{
		return sysfs_emit(buf, "supported\n");
}
TMPFS_ATTR_RO(casefold, casefold_show);
#endif

static struct attribute *tmpfs_attributes[] = {
#if IS_ENABLED(CONFIG_UNICODE)
	&tmpfs_attr_casefold.attr,
#endif
	NULL
};

static const struct attribute_group tmpfs_attribute_group = {
	.attrs = tmpfs_attributes,
	.name = "features"
};

static struct kobject *tmpfs_kobj;

static int __init tmpfs_sysfs_init(void)
{
	int ret;

	tmpfs_kobj = kobject_create_and_add("tmpfs", fs_kobj);
	if (!tmpfs_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(tmpfs_kobj, &tmpfs_attribute_group);
	if (ret)
		kobject_put(tmpfs_kobj);

	return ret;
}
#endif /* CONFIG_SYSFS && CONFIG_TMPFS */

void __init shmem_init(void)
{
	int error;

	shmem_init_inodecache();

#ifdef CONFIG_TMPFS_QUOTA
	register_quota_format(&shmem_quota_format);
#endif

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

#if defined(CONFIG_SYSFS) && defined(CONFIG_TMPFS)
	error = tmpfs_sysfs_init();
	if (error) {
		pr_err("Could not init tmpfs sysfs\n");
		goto out1;
	}
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (has_transparent_hugepage() && shmem_huge > SHMEM_HUGE_DENY)
		SHMEM_SB(shm_mnt->mnt_sb)->huge = shmem_huge;
	else
		shmem_huge = SHMEM_HUGE_NEVER; /* just in case it was patched */

	/*
	 * Default to setting PMD-sized THP to inherit the global setting and
	 * disable all other multi-size THPs.
	 */
	if (!shmem_orders_configured)
		huge_shmem_orders_inherit = BIT(HPAGE_PMD_ORDER);
#endif
	return;

out1:
	unregister_filesystem(&shmem_fs_type);
out2:
#ifdef CONFIG_TMPFS_QUOTA
	unregister_quota_format(&shmem_quota_format);
#endif
	shmem_destroy_inodecache();
	shm_mnt = ERR_PTR(error);
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
	int len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(values); i++) {
		len += sysfs_emit_at(buf, len,
				shmem_huge == values[i] ? "%s[%s]" : "%s%s",
				i ? " " : "", shmem_format_huge(values[i]));
	}
	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

static ssize_t shmem_enabled_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	char tmp[16];
	int huge, err;

	if (count + 1 > sizeof(tmp))
		return -EINVAL;
	memcpy(tmp, buf, count);
	tmp[count] = '\0';
	if (count && tmp[count - 1] == '\n')
		tmp[count - 1] = '\0';

	huge = shmem_parse_huge(tmp);
	if (huge == -EINVAL)
		return huge;

	shmem_huge = huge;
	if (shmem_huge > SHMEM_HUGE_DENY)
		SHMEM_SB(shm_mnt->mnt_sb)->huge = shmem_huge;

	err = start_stop_khugepaged();
	return err ? err : count;
}

struct kobj_attribute shmem_enabled_attr = __ATTR_RW(shmem_enabled);
static DEFINE_SPINLOCK(huge_shmem_orders_lock);

static ssize_t thpsize_shmem_enabled_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	int order = to_thpsize(kobj)->order;
	const char *output;

	if (test_bit(order, &huge_shmem_orders_always))
		output = "[always] inherit within_size advise never";
	else if (test_bit(order, &huge_shmem_orders_inherit))
		output = "always [inherit] within_size advise never";
	else if (test_bit(order, &huge_shmem_orders_within_size))
		output = "always inherit [within_size] advise never";
	else if (test_bit(order, &huge_shmem_orders_madvise))
		output = "always inherit within_size [advise] never";
	else
		output = "always inherit within_size advise [never]";

	return sysfs_emit(buf, "%s\n", output);
}

static ssize_t thpsize_shmem_enabled_store(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   const char *buf, size_t count)
{
	int order = to_thpsize(kobj)->order;
	ssize_t ret = count;

	if (sysfs_streq(buf, "always")) {
		spin_lock(&huge_shmem_orders_lock);
		clear_bit(order, &huge_shmem_orders_inherit);
		clear_bit(order, &huge_shmem_orders_madvise);
		clear_bit(order, &huge_shmem_orders_within_size);
		set_bit(order, &huge_shmem_orders_always);
		spin_unlock(&huge_shmem_orders_lock);
	} else if (sysfs_streq(buf, "inherit")) {
		/* Do not override huge allocation policy with non-PMD sized mTHP */
		if (shmem_huge == SHMEM_HUGE_FORCE &&
		    order != HPAGE_PMD_ORDER)
			return -EINVAL;

		spin_lock(&huge_shmem_orders_lock);
		clear_bit(order, &huge_shmem_orders_always);
		clear_bit(order, &huge_shmem_orders_madvise);
		clear_bit(order, &huge_shmem_orders_within_size);
		set_bit(order, &huge_shmem_orders_inherit);
		spin_unlock(&huge_shmem_orders_lock);
	} else if (sysfs_streq(buf, "within_size")) {
		spin_lock(&huge_shmem_orders_lock);
		clear_bit(order, &huge_shmem_orders_always);
		clear_bit(order, &huge_shmem_orders_inherit);
		clear_bit(order, &huge_shmem_orders_madvise);
		set_bit(order, &huge_shmem_orders_within_size);
		spin_unlock(&huge_shmem_orders_lock);
	} else if (sysfs_streq(buf, "advise")) {
		spin_lock(&huge_shmem_orders_lock);
		clear_bit(order, &huge_shmem_orders_always);
		clear_bit(order, &huge_shmem_orders_inherit);
		clear_bit(order, &huge_shmem_orders_within_size);
		set_bit(order, &huge_shmem_orders_madvise);
		spin_unlock(&huge_shmem_orders_lock);
	} else if (sysfs_streq(buf, "never")) {
		spin_lock(&huge_shmem_orders_lock);
		clear_bit(order, &huge_shmem_orders_always);
		clear_bit(order, &huge_shmem_orders_inherit);
		clear_bit(order, &huge_shmem_orders_within_size);
		clear_bit(order, &huge_shmem_orders_madvise);
		spin_unlock(&huge_shmem_orders_lock);
	} else {
		ret = -EINVAL;
	}

	if (ret > 0) {
		int err = start_stop_khugepaged();

		if (err)
			ret = err;
	}
	return ret;
}

struct kobj_attribute thpsize_shmem_enabled_attr =
	__ATTR(shmem_enabled, 0644, thpsize_shmem_enabled_show, thpsize_shmem_enabled_store);
#endif /* CONFIG_TRANSPARENT_HUGEPAGE && CONFIG_SYSFS */

#if defined(CONFIG_TRANSPARENT_HUGEPAGE)

static int __init setup_transparent_hugepage_shmem(char *str)
{
	int huge;

	huge = shmem_parse_huge(str);
	if (huge == -EINVAL) {
		pr_warn("transparent_hugepage_shmem= cannot parse, ignored\n");
		return huge;
	}

	shmem_huge = huge;
	return 1;
}
__setup("transparent_hugepage_shmem=", setup_transparent_hugepage_shmem);

static int __init setup_transparent_hugepage_tmpfs(char *str)
{
	int huge;

	huge = shmem_parse_huge(str);
	if (huge < 0) {
		pr_warn("transparent_hugepage_tmpfs= cannot parse, ignored\n");
		return huge;
	}

	tmpfs_huge = huge;
	return 1;
}
__setup("transparent_hugepage_tmpfs=", setup_transparent_hugepage_tmpfs);

static char str_dup[PAGE_SIZE] __initdata;
static int __init setup_thp_shmem(char *str)
{
	char *token, *range, *policy, *subtoken;
	unsigned long always, inherit, madvise, within_size;
	char *start_size, *end_size;
	int start, end, nr;
	char *p;

	if (!str || strlen(str) + 1 > PAGE_SIZE)
		goto err;
	strscpy(str_dup, str);

	always = huge_shmem_orders_always;
	inherit = huge_shmem_orders_inherit;
	madvise = huge_shmem_orders_madvise;
	within_size = huge_shmem_orders_within_size;
	p = str_dup;
	while ((token = strsep(&p, ";")) != NULL) {
		range = strsep(&token, ":");
		policy = token;

		if (!policy)
			goto err;

		while ((subtoken = strsep(&range, ",")) != NULL) {
			if (strchr(subtoken, '-')) {
				start_size = strsep(&subtoken, "-");
				end_size = subtoken;

				start = get_order_from_str(start_size,
							   THP_ORDERS_ALL_FILE_DEFAULT);
				end = get_order_from_str(end_size,
							 THP_ORDERS_ALL_FILE_DEFAULT);
			} else {
				start_size = end_size = subtoken;
				start = end = get_order_from_str(subtoken,
								 THP_ORDERS_ALL_FILE_DEFAULT);
			}

			if (start < 0) {
				pr_err("invalid size %s in thp_shmem boot parameter\n",
				       start_size);
				goto err;
			}

			if (end < 0) {
				pr_err("invalid size %s in thp_shmem boot parameter\n",
				       end_size);
				goto err;
			}

			if (start > end)
				goto err;

			nr = end - start + 1;
			if (!strcmp(policy, "always")) {
				bitmap_set(&always, start, nr);
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
				bitmap_clear(&within_size, start, nr);
			} else if (!strcmp(policy, "advise")) {
				bitmap_set(&madvise, start, nr);
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&always, start, nr);
				bitmap_clear(&within_size, start, nr);
			} else if (!strcmp(policy, "inherit")) {
				bitmap_set(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
				bitmap_clear(&always, start, nr);
				bitmap_clear(&within_size, start, nr);
			} else if (!strcmp(policy, "within_size")) {
				bitmap_set(&within_size, start, nr);
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
				bitmap_clear(&always, start, nr);
			} else if (!strcmp(policy, "never")) {
				bitmap_clear(&inherit, start, nr);
				bitmap_clear(&madvise, start, nr);
				bitmap_clear(&always, start, nr);
				bitmap_clear(&within_size, start, nr);
			} else {
				pr_err("invalid policy %s in thp_shmem boot parameter\n", policy);
				goto err;
			}
		}
	}

	huge_shmem_orders_always = always;
	huge_shmem_orders_madvise = madvise;
	huge_shmem_orders_inherit = inherit;
	huge_shmem_orders_within_size = within_size;
	shmem_orders_configured = true;
	return 1;

err:
	pr_warn("thp_shmem=%s: error parsing string, ignoring setting\n", str);
	return 0;
}
__setup("thp_shmem=", setup_thp_shmem);

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
	.kill_sb	= ramfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

void __init shmem_init(void)
{
	BUG_ON(register_filesystem(&shmem_fs_type) != 0);

	shm_mnt = kern_mount(&shmem_fs_type);
	BUG_ON(IS_ERR(shm_mnt));
}

int shmem_unuse(unsigned int type)
{
	return 0;
}

int shmem_lock(struct file *file, int lock, struct ucounts *ucounts)
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
	return mm_get_unmapped_area(current->mm, file, addr, len, pgoff, flags);
}
#endif

void shmem_truncate_range(struct inode *inode, loff_t lstart, loff_t lend)
{
	truncate_inode_pages_range(inode->i_mapping, lstart, lend);
}
EXPORT_SYMBOL_GPL(shmem_truncate_range);

#define shmem_vm_ops				generic_file_vm_ops
#define shmem_anon_vm_ops			generic_file_vm_ops
#define shmem_file_operations			ramfs_file_operations
#define shmem_acct_size(flags, size)		0
#define shmem_unacct_size(flags, size)		do {} while (0)

static inline struct inode *shmem_get_inode(struct mnt_idmap *idmap,
				struct super_block *sb, struct inode *dir,
				umode_t mode, dev_t dev, unsigned long flags)
{
	struct inode *inode = ramfs_get_inode(sb, dir, mode, dev);
	return inode ? inode : ERR_PTR(-ENOSPC);
}

#endif /* CONFIG_SHMEM */

/* common code */

static struct file *__shmem_file_setup(struct vfsmount *mnt, const char *name,
			loff_t size, unsigned long flags, unsigned int i_flags)
{
	struct inode *inode;
	struct file *res;

	if (IS_ERR(mnt))
		return ERR_CAST(mnt);

	if (size < 0 || size > MAX_LFS_FILESIZE)
		return ERR_PTR(-EINVAL);

	if (shmem_acct_size(flags, size))
		return ERR_PTR(-ENOMEM);

	if (is_idmapped_mnt(mnt))
		return ERR_PTR(-EINVAL);

	inode = shmem_get_inode(&nop_mnt_idmap, mnt->mnt_sb, NULL,
				S_IFREG | S_IRWXUGO, 0, flags);
	if (IS_ERR(inode)) {
		shmem_unacct_size(flags, size);
		return ERR_CAST(inode);
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
EXPORT_SYMBOL_GPL(shmem_kernel_file_setup);

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
	vma->vm_ops = &shmem_anon_vm_ops;

	return 0;
}

/**
 * shmem_read_folio_gfp - read into page cache, using specified page allocation flags.
 * @mapping:	the folio's address_space
 * @index:	the folio index
 * @gfp:	the page allocator flags to use if allocating
 *
 * This behaves as a tmpfs "read_cache_page_gfp(mapping, index, gfp)",
 * with any new page allocations done using the specified allocation flags.
 * But read_cache_page_gfp() uses the ->read_folio() method: which does not
 * suit tmpfs, since it may have pages in swapcache, and needs to find those
 * for itself; although drivers/gpu/drm i915 and ttm rely upon this support.
 *
 * i915_gem_object_get_pages_gtt() mixes __GFP_NORETRY | __GFP_NOWARN in
 * with the mapping_gfp_mask(), to avoid OOMing the machine unnecessarily.
 */
struct folio *shmem_read_folio_gfp(struct address_space *mapping,
		pgoff_t index, gfp_t gfp)
{
#ifdef CONFIG_SHMEM
	struct inode *inode = mapping->host;
	struct folio *folio;
	int error;

	error = shmem_get_folio_gfp(inode, index, 0, &folio, SGP_CACHE,
				    gfp, NULL, NULL);
	if (error)
		return ERR_PTR(error);

	folio_unlock(folio);
	return folio;
#else
	/*
	 * The tiny !SHMEM case uses ramfs without swap
	 */
	return mapping_read_folio_gfp(mapping, index, gfp);
#endif
}
EXPORT_SYMBOL_GPL(shmem_read_folio_gfp);

struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
					 pgoff_t index, gfp_t gfp)
{
	struct folio *folio = shmem_read_folio_gfp(mapping, index, gfp);
	struct page *page;

	if (IS_ERR(folio))
		return &folio->page;

	page = folio_file_page(folio, index);
	if (PageHWPoison(page)) {
		folio_put(folio);
		return ERR_PTR(-EIO);
	}

	return page;
}
EXPORT_SYMBOL_GPL(shmem_read_mapping_page_gfp);
