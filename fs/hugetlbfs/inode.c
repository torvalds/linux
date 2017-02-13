/*
 * hugetlbpage-backed filesystem.  Based on ramfs.
 *
 * Nadia Yvette Chambers, 2002
 *
 * Copyright (C) 2002 Linus Torvalds.
 * License: GPL
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/thread_info.h>
#include <asm/current.h>
#include <linux/sched.h>		/* remove ASAP */
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/backing-dev.h>
#include <linux/hugetlb.h>
#include <linux/pagevec.h>
#include <linux/parser.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/dnotify.h>
#include <linux/statfs.h>
#include <linux/security.h>
#include <linux/magic.h>
#include <linux/migrate.h>
#include <linux/uio.h>

#include <linux/uaccess.h>

static const struct super_operations hugetlbfs_ops;
static const struct address_space_operations hugetlbfs_aops;
const struct file_operations hugetlbfs_file_operations;
static const struct inode_operations hugetlbfs_dir_inode_operations;
static const struct inode_operations hugetlbfs_inode_operations;

struct hugetlbfs_config {
	kuid_t   uid;
	kgid_t   gid;
	umode_t mode;
	long	max_hpages;
	long	nr_inodes;
	struct hstate *hstate;
	long    min_hpages;
};

struct hugetlbfs_inode_info {
	struct shared_policy policy;
	struct inode vfs_inode;
};

static inline struct hugetlbfs_inode_info *HUGETLBFS_I(struct inode *inode)
{
	return container_of(inode, struct hugetlbfs_inode_info, vfs_inode);
}

int sysctl_hugetlb_shm_group;

enum {
	Opt_size, Opt_nr_inodes,
	Opt_mode, Opt_uid, Opt_gid,
	Opt_pagesize, Opt_min_size,
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_size,	"size=%s"},
	{Opt_nr_inodes,	"nr_inodes=%s"},
	{Opt_mode,	"mode=%o"},
	{Opt_uid,	"uid=%u"},
	{Opt_gid,	"gid=%u"},
	{Opt_pagesize,	"pagesize=%s"},
	{Opt_min_size,	"min_size=%s"},
	{Opt_err,	NULL},
};

#ifdef CONFIG_NUMA
static inline void hugetlb_set_vma_policy(struct vm_area_struct *vma,
					struct inode *inode, pgoff_t index)
{
	vma->vm_policy = mpol_shared_policy_lookup(&HUGETLBFS_I(inode)->policy,
							index);
}

static inline void hugetlb_drop_vma_policy(struct vm_area_struct *vma)
{
	mpol_cond_put(vma->vm_policy);
}
#else
static inline void hugetlb_set_vma_policy(struct vm_area_struct *vma,
					struct inode *inode, pgoff_t index)
{
}

static inline void hugetlb_drop_vma_policy(struct vm_area_struct *vma)
{
}
#endif

static void huge_pagevec_release(struct pagevec *pvec)
{
	int i;

	for (i = 0; i < pagevec_count(pvec); ++i)
		put_page(pvec->pages[i]);

	pagevec_reinit(pvec);
}

static int hugetlbfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	loff_t len, vma_len;
	int ret;
	struct hstate *h = hstate_file(file);

	/*
	 * vma address alignment (but not the pgoff alignment) has
	 * already been checked by prepare_hugepage_range.  If you add
	 * any error returns here, do so after setting VM_HUGETLB, so
	 * is_vm_hugetlb_page tests below unmap_region go the right
	 * way when do_mmap_pgoff unwinds (may be important on powerpc
	 * and ia64).
	 */
	vma->vm_flags |= VM_HUGETLB | VM_DONTEXPAND;
	vma->vm_ops = &hugetlb_vm_ops;

	if (vma->vm_pgoff & (~huge_page_mask(h) >> PAGE_SHIFT))
		return -EINVAL;

	vma_len = (loff_t)(vma->vm_end - vma->vm_start);

	inode_lock(inode);
	file_accessed(file);

	ret = -ENOMEM;
	len = vma_len + ((loff_t)vma->vm_pgoff << PAGE_SHIFT);

	if (hugetlb_reserve_pages(inode,
				vma->vm_pgoff >> huge_page_order(h),
				len >> huge_page_shift(h), vma,
				vma->vm_flags))
		goto out;

	ret = 0;
	if (vma->vm_flags & VM_WRITE && inode->i_size < len)
		inode->i_size = len;
out:
	inode_unlock(inode);

	return ret;
}

/*
 * Called under down_write(mmap_sem).
 */

#ifndef HAVE_ARCH_HUGETLB_UNMAPPED_AREA
static unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct hstate *h = hstate_file(file);
	struct vm_unmapped_area_info info;

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (len > TASK_SIZE)
		return -ENOMEM;

	if (flags & MAP_FIXED) {
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
		return addr;
	}

	if (addr) {
		addr = ALIGN(addr, huge_page_size(h));
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = TASK_UNMAPPED_BASE;
	info.high_limit = TASK_SIZE;
	info.align_mask = PAGE_MASK & ~huge_page_mask(h);
	info.align_offset = 0;
	return vm_unmapped_area(&info);
}
#endif

static size_t
hugetlbfs_read_actor(struct page *page, unsigned long offset,
			struct iov_iter *to, unsigned long size)
{
	size_t copied = 0;
	int i, chunksize;

	/* Find which 4k chunk and offset with in that chunk */
	i = offset >> PAGE_SHIFT;
	offset = offset & ~PAGE_MASK;

	while (size) {
		size_t n;
		chunksize = PAGE_SIZE;
		if (offset)
			chunksize -= offset;
		if (chunksize > size)
			chunksize = size;
		n = copy_page_to_iter(&page[i], offset, chunksize, to);
		copied += n;
		if (n != chunksize)
			return copied;
		offset = 0;
		size -= chunksize;
		i++;
	}
	return copied;
}

/*
 * Support for read() - Find the page attached to f_mapping and copy out the
 * data. Its *very* similar to do_generic_mapping_read(), we can't use that
 * since it has PAGE_SIZE assumptions.
 */
static ssize_t hugetlbfs_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct hstate *h = hstate_file(file);
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long index = iocb->ki_pos >> huge_page_shift(h);
	unsigned long offset = iocb->ki_pos & ~huge_page_mask(h);
	unsigned long end_index;
	loff_t isize;
	ssize_t retval = 0;

	while (iov_iter_count(to)) {
		struct page *page;
		size_t nr, copied;

		/* nr is the maximum number of bytes to copy from this page */
		nr = huge_page_size(h);
		isize = i_size_read(inode);
		if (!isize)
			break;
		end_index = (isize - 1) >> huge_page_shift(h);
		if (index > end_index)
			break;
		if (index == end_index) {
			nr = ((isize - 1) & ~huge_page_mask(h)) + 1;
			if (nr <= offset)
				break;
		}
		nr = nr - offset;

		/* Find the page */
		page = find_lock_page(mapping, index);
		if (unlikely(page == NULL)) {
			/*
			 * We have a HOLE, zero out the user-buffer for the
			 * length of the hole or request.
			 */
			copied = iov_iter_zero(nr, to);
		} else {
			unlock_page(page);

			/*
			 * We have the page, copy it to user space buffer.
			 */
			copied = hugetlbfs_read_actor(page, offset, to, nr);
			put_page(page);
		}
		offset += copied;
		retval += copied;
		if (copied != nr && iov_iter_count(to)) {
			if (!retval)
				retval = -EFAULT;
			break;
		}
		index += offset >> huge_page_shift(h);
		offset &= ~huge_page_mask(h);
	}
	iocb->ki_pos = ((loff_t)index << huge_page_shift(h)) + offset;
	return retval;
}

static int hugetlbfs_write_begin(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	return -EINVAL;
}

static int hugetlbfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	BUG();
	return -EINVAL;
}

static void remove_huge_page(struct page *page)
{
	ClearPageDirty(page);
	ClearPageUptodate(page);
	delete_from_page_cache(page);
}

static void
hugetlb_vmdelete_list(struct rb_root *root, pgoff_t start, pgoff_t end)
{
	struct vm_area_struct *vma;

	/*
	 * end == 0 indicates that the entire range after
	 * start should be unmapped.
	 */
	vma_interval_tree_foreach(vma, root, start, end ? end : ULONG_MAX) {
		unsigned long v_offset;
		unsigned long v_end;

		/*
		 * Can the expression below overflow on 32-bit arches?
		 * No, because the interval tree returns us only those vmas
		 * which overlap the truncated area starting at pgoff,
		 * and no vma on a 32-bit arch can span beyond the 4GB.
		 */
		if (vma->vm_pgoff < start)
			v_offset = (start - vma->vm_pgoff) << PAGE_SHIFT;
		else
			v_offset = 0;

		if (!end)
			v_end = vma->vm_end;
		else {
			v_end = ((end - vma->vm_pgoff) << PAGE_SHIFT)
							+ vma->vm_start;
			if (v_end > vma->vm_end)
				v_end = vma->vm_end;
		}

		unmap_hugepage_range(vma, vma->vm_start + v_offset, v_end,
									NULL);
	}
}

/*
 * remove_inode_hugepages handles two distinct cases: truncation and hole
 * punch.  There are subtle differences in operation for each case.
 *
 * truncation is indicated by end of range being LLONG_MAX
 *	In this case, we first scan the range and release found pages.
 *	After releasing pages, hugetlb_unreserve_pages cleans up region/reserv
 *	maps and global counts.  Page faults can not race with truncation
 *	in this routine.  hugetlb_no_page() prevents page faults in the
 *	truncated range.  It checks i_size before allocation, and again after
 *	with the page table lock for the page held.  The same lock must be
 *	acquired to unmap a page.
 * hole punch is indicated if end is not LLONG_MAX
 *	In the hole punch case we scan the range and release found pages.
 *	Only when releasing a page is the associated region/reserv map
 *	deleted.  The region/reserv map for ranges without associated
 *	pages are not modified.  Page faults can race with hole punch.
 *	This is indicated if we find a mapped page.
 * Note: If the passed end of range value is beyond the end of file, but
 * not LLONG_MAX this routine still performs a hole punch operation.
 */
static void remove_inode_hugepages(struct inode *inode, loff_t lstart,
				   loff_t lend)
{
	struct hstate *h = hstate_inode(inode);
	struct address_space *mapping = &inode->i_data;
	const pgoff_t start = lstart >> huge_page_shift(h);
	const pgoff_t end = lend >> huge_page_shift(h);
	struct vm_area_struct pseudo_vma;
	struct pagevec pvec;
	pgoff_t next;
	int i, freed = 0;
	long lookup_nr = PAGEVEC_SIZE;
	bool truncate_op = (lend == LLONG_MAX);

	memset(&pseudo_vma, 0, sizeof(struct vm_area_struct));
	pseudo_vma.vm_flags = (VM_HUGETLB | VM_MAYSHARE | VM_SHARED);
	pagevec_init(&pvec, 0);
	next = start;
	while (next < end) {
		/*
		 * Don't grab more pages than the number left in the range.
		 */
		if (end - next < lookup_nr)
			lookup_nr = end - next;

		/*
		 * When no more pages are found, we are done.
		 */
		if (!pagevec_lookup(&pvec, mapping, next, lookup_nr))
			break;

		for (i = 0; i < pagevec_count(&pvec); ++i) {
			struct page *page = pvec.pages[i];
			u32 hash;

			/*
			 * The page (index) could be beyond end.  This is
			 * only possible in the punch hole case as end is
			 * max page offset in the truncate case.
			 */
			next = page->index;
			if (next >= end)
				break;

			hash = hugetlb_fault_mutex_hash(h, current->mm,
							&pseudo_vma,
							mapping, next, 0);
			mutex_lock(&hugetlb_fault_mutex_table[hash]);

			/*
			 * If page is mapped, it was faulted in after being
			 * unmapped in caller.  Unmap (again) now after taking
			 * the fault mutex.  The mutex will prevent faults
			 * until we finish removing the page.
			 *
			 * This race can only happen in the hole punch case.
			 * Getting here in a truncate operation is a bug.
			 */
			if (unlikely(page_mapped(page))) {
				BUG_ON(truncate_op);

				i_mmap_lock_write(mapping);
				hugetlb_vmdelete_list(&mapping->i_mmap,
					next * pages_per_huge_page(h),
					(next + 1) * pages_per_huge_page(h));
				i_mmap_unlock_write(mapping);
			}

			lock_page(page);
			/*
			 * We must free the huge page and remove from page
			 * cache (remove_huge_page) BEFORE removing the
			 * region/reserve map (hugetlb_unreserve_pages).  In
			 * rare out of memory conditions, removal of the
			 * region/reserve map could fail. Correspondingly,
			 * the subpool and global reserve usage count can need
			 * to be adjusted.
			 */
			VM_BUG_ON(PagePrivate(page));
			remove_huge_page(page);
			freed++;
			if (!truncate_op) {
				if (unlikely(hugetlb_unreserve_pages(inode,
							next, next + 1, 1)))
					hugetlb_fix_reserve_counts(inode);
			}

			unlock_page(page);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
		}
		++next;
		huge_pagevec_release(&pvec);
		cond_resched();
	}

	if (truncate_op)
		(void)hugetlb_unreserve_pages(inode, start, LONG_MAX, freed);
}

static void hugetlbfs_evict_inode(struct inode *inode)
{
	struct resv_map *resv_map;

	remove_inode_hugepages(inode, 0, LLONG_MAX);
	resv_map = (struct resv_map *)inode->i_mapping->private_data;
	/* root inode doesn't have the resv_map, so we should check it */
	if (resv_map)
		resv_map_release(&resv_map->refs);
	clear_inode(inode);
}

static int hugetlb_vmtruncate(struct inode *inode, loff_t offset)
{
	pgoff_t pgoff;
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);

	BUG_ON(offset & ~huge_page_mask(h));
	pgoff = offset >> PAGE_SHIFT;

	i_size_write(inode, offset);
	i_mmap_lock_write(mapping);
	if (!RB_EMPTY_ROOT(&mapping->i_mmap))
		hugetlb_vmdelete_list(&mapping->i_mmap, pgoff, 0);
	i_mmap_unlock_write(mapping);
	remove_inode_hugepages(inode, offset, LLONG_MAX);
	return 0;
}

static long hugetlbfs_punch_hole(struct inode *inode, loff_t offset, loff_t len)
{
	struct hstate *h = hstate_inode(inode);
	loff_t hpage_size = huge_page_size(h);
	loff_t hole_start, hole_end;

	/*
	 * For hole punch round up the beginning offset of the hole and
	 * round down the end.
	 */
	hole_start = round_up(offset, hpage_size);
	hole_end = round_down(offset + len, hpage_size);

	if (hole_end > hole_start) {
		struct address_space *mapping = inode->i_mapping;

		inode_lock(inode);
		i_mmap_lock_write(mapping);
		if (!RB_EMPTY_ROOT(&mapping->i_mmap))
			hugetlb_vmdelete_list(&mapping->i_mmap,
						hole_start >> PAGE_SHIFT,
						hole_end  >> PAGE_SHIFT);
		i_mmap_unlock_write(mapping);
		remove_inode_hugepages(inode, hole_start, hole_end);
		inode_unlock(inode);
	}

	return 0;
}

static long hugetlbfs_fallocate(struct file *file, int mode, loff_t offset,
				loff_t len)
{
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);
	struct vm_area_struct pseudo_vma;
	struct mm_struct *mm = current->mm;
	loff_t hpage_size = huge_page_size(h);
	unsigned long hpage_shift = huge_page_shift(h);
	pgoff_t start, index, end;
	int error;
	u32 hash;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		return hugetlbfs_punch_hole(inode, offset, len);

	/*
	 * Default preallocate case.
	 * For this range, start is rounded down and end is rounded up
	 * as well as being converted to page offsets.
	 */
	start = offset >> hpage_shift;
	end = (offset + len + hpage_size - 1) >> hpage_shift;

	inode_lock(inode);

	/* We need to check rlimit even when FALLOC_FL_KEEP_SIZE */
	error = inode_newsize_ok(inode, offset + len);
	if (error)
		goto out;

	/*
	 * Initialize a pseudo vma as this is required by the huge page
	 * allocation routines.  If NUMA is configured, use page index
	 * as input to create an allocation policy.
	 */
	memset(&pseudo_vma, 0, sizeof(struct vm_area_struct));
	pseudo_vma.vm_flags = (VM_HUGETLB | VM_MAYSHARE | VM_SHARED);
	pseudo_vma.vm_file = file;

	for (index = start; index < end; index++) {
		/*
		 * This is supposed to be the vaddr where the page is being
		 * faulted in, but we have no vaddr here.
		 */
		struct page *page;
		unsigned long addr;
		int avoid_reserve = 0;

		cond_resched();

		/*
		 * fallocate(2) manpage permits EINTR; we may have been
		 * interrupted because we are using up too much memory.
		 */
		if (signal_pending(current)) {
			error = -EINTR;
			break;
		}

		/* Set numa allocation policy based on index */
		hugetlb_set_vma_policy(&pseudo_vma, inode, index);

		/* addr is the offset within the file (zero based) */
		addr = index * hpage_size;

		/* mutex taken here, fault path and hole punch */
		hash = hugetlb_fault_mutex_hash(h, mm, &pseudo_vma, mapping,
						index, addr);
		mutex_lock(&hugetlb_fault_mutex_table[hash]);

		/* See if already present in mapping to avoid alloc/free */
		page = find_get_page(mapping, index);
		if (page) {
			put_page(page);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			hugetlb_drop_vma_policy(&pseudo_vma);
			continue;
		}

		/* Allocate page and add to page cache */
		page = alloc_huge_page(&pseudo_vma, addr, avoid_reserve);
		hugetlb_drop_vma_policy(&pseudo_vma);
		if (IS_ERR(page)) {
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			error = PTR_ERR(page);
			goto out;
		}
		clear_huge_page(page, addr, pages_per_huge_page(h));
		__SetPageUptodate(page);
		error = huge_add_to_page_cache(page, mapping, index);
		if (unlikely(error)) {
			put_page(page);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			goto out;
		}

		mutex_unlock(&hugetlb_fault_mutex_table[hash]);

		/*
		 * page_put due to reference from alloc_huge_page()
		 * unlock_page because locked by add_to_page_cache()
		 */
		put_page(page);
		unlock_page(page);
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
	inode->i_ctime = current_time(inode);
out:
	inode_unlock(inode);
	return error;
}

static int hugetlbfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct hstate *h = hstate_inode(inode);
	int error;
	unsigned int ia_valid = attr->ia_valid;

	BUG_ON(!inode);

	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	if (ia_valid & ATTR_SIZE) {
		error = -EINVAL;
		if (attr->ia_size & ~huge_page_mask(h))
			return -EINVAL;
		error = hugetlb_vmtruncate(inode, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static struct inode *hugetlbfs_get_root(struct super_block *sb,
					struct hugetlbfs_config *config)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		struct hugetlbfs_inode_info *info;
		inode->i_ino = get_next_ino();
		inode->i_mode = S_IFDIR | config->mode;
		inode->i_uid = config->uid;
		inode->i_gid = config->gid;
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		info = HUGETLBFS_I(inode);
		mpol_shared_policy_init(&info->policy, NULL);
		inode->i_op = &hugetlbfs_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inc_nlink(inode);
		lockdep_annotate_inode_mutex_key(inode);
	}
	return inode;
}

/*
 * Hugetlbfs is not reclaimable; therefore its i_mmap_rwsem will never
 * be taken from reclaim -- unlike regular filesystems. This needs an
 * annotation because huge_pmd_share() does an allocation under hugetlb's
 * i_mmap_rwsem.
 */
static struct lock_class_key hugetlbfs_i_mmap_rwsem_key;

static struct inode *hugetlbfs_get_inode(struct super_block *sb,
					struct inode *dir,
					umode_t mode, dev_t dev)
{
	struct inode *inode;
	struct resv_map *resv_map;

	resv_map = resv_map_alloc();
	if (!resv_map)
		return NULL;

	inode = new_inode(sb);
	if (inode) {
		struct hugetlbfs_inode_info *info;
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		lockdep_set_class(&inode->i_mapping->i_mmap_rwsem,
				&hugetlbfs_i_mmap_rwsem_key);
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_mapping->private_data = resv_map;
		info = HUGETLBFS_I(inode);
		/*
		 * The policy is initialized here even if we are creating a
		 * private inode because initialization simply creates an
		 * an empty rb tree and calls rwlock_init(), later when we
		 * call mpol_free_shared_policy() it will just return because
		 * the rb tree will still be empty.
		 */
		mpol_shared_policy_init(&info->policy, NULL);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &hugetlbfs_inode_operations;
			inode->i_fop = &hugetlbfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &hugetlbfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
		lockdep_annotate_inode_mutex_key(inode);
	} else
		kref_put(&resv_map->refs, resv_map_release);

	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int hugetlbfs_mknod(struct inode *dir,
			struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = hugetlbfs_get_inode(dir->i_sb, dir, mode, dev);
	if (inode) {
		dir->i_ctime = dir->i_mtime = current_time(dir);
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
	}
	return error;
}

static int hugetlbfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int retval = hugetlbfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int hugetlbfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return hugetlbfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int hugetlbfs_symlink(struct inode *dir,
			struct dentry *dentry, const char *symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = hugetlbfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	dir->i_ctime = dir->i_mtime = current_time(dir);

	return error;
}

/*
 * mark the head page dirty
 */
static int hugetlbfs_set_page_dirty(struct page *page)
{
	struct page *head = compound_head(page);

	SetPageDirty(head);
	return 0;
}

static int hugetlbfs_migrate_page(struct address_space *mapping,
				struct page *newpage, struct page *page,
				enum migrate_mode mode)
{
	int rc;

	rc = migrate_huge_page_move_mapping(mapping, newpage, page);
	if (rc != MIGRATEPAGE_SUCCESS)
		return rc;
	migrate_page_copy(newpage, page);

	return MIGRATEPAGE_SUCCESS;
}

static int hugetlbfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(dentry->d_sb);
	struct hstate *h = hstate_inode(d_inode(dentry));

	buf->f_type = HUGETLBFS_MAGIC;
	buf->f_bsize = huge_page_size(h);
	if (sbinfo) {
		spin_lock(&sbinfo->stat_lock);
		/* If no limits set, just report 0 for max/free/used
		 * blocks, like simple_statfs() */
		if (sbinfo->spool) {
			long free_pages;

			spin_lock(&sbinfo->spool->lock);
			buf->f_blocks = sbinfo->spool->max_hpages;
			free_pages = sbinfo->spool->max_hpages
				- sbinfo->spool->used_hpages;
			buf->f_bavail = buf->f_bfree = free_pages;
			spin_unlock(&sbinfo->spool->lock);
			buf->f_files = sbinfo->max_inodes;
			buf->f_ffree = sbinfo->free_inodes;
		}
		spin_unlock(&sbinfo->stat_lock);
	}
	buf->f_namelen = NAME_MAX;
	return 0;
}

static void hugetlbfs_put_super(struct super_block *sb)
{
	struct hugetlbfs_sb_info *sbi = HUGETLBFS_SB(sb);

	if (sbi) {
		sb->s_fs_info = NULL;

		if (sbi->spool)
			hugepage_put_subpool(sbi->spool);

		kfree(sbi);
	}
}

static inline int hugetlbfs_dec_free_inodes(struct hugetlbfs_sb_info *sbinfo)
{
	if (sbinfo->free_inodes >= 0) {
		spin_lock(&sbinfo->stat_lock);
		if (unlikely(!sbinfo->free_inodes)) {
			spin_unlock(&sbinfo->stat_lock);
			return 0;
		}
		sbinfo->free_inodes--;
		spin_unlock(&sbinfo->stat_lock);
	}

	return 1;
}

static void hugetlbfs_inc_free_inodes(struct hugetlbfs_sb_info *sbinfo)
{
	if (sbinfo->free_inodes >= 0) {
		spin_lock(&sbinfo->stat_lock);
		sbinfo->free_inodes++;
		spin_unlock(&sbinfo->stat_lock);
	}
}


static struct kmem_cache *hugetlbfs_inode_cachep;

static struct inode *hugetlbfs_alloc_inode(struct super_block *sb)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(sb);
	struct hugetlbfs_inode_info *p;

	if (unlikely(!hugetlbfs_dec_free_inodes(sbinfo)))
		return NULL;
	p = kmem_cache_alloc(hugetlbfs_inode_cachep, GFP_KERNEL);
	if (unlikely(!p)) {
		hugetlbfs_inc_free_inodes(sbinfo);
		return NULL;
	}
	return &p->vfs_inode;
}

static void hugetlbfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(hugetlbfs_inode_cachep, HUGETLBFS_I(inode));
}

static void hugetlbfs_destroy_inode(struct inode *inode)
{
	hugetlbfs_inc_free_inodes(HUGETLBFS_SB(inode->i_sb));
	mpol_free_shared_policy(&HUGETLBFS_I(inode)->policy);
	call_rcu(&inode->i_rcu, hugetlbfs_i_callback);
}

static const struct address_space_operations hugetlbfs_aops = {
	.write_begin	= hugetlbfs_write_begin,
	.write_end	= hugetlbfs_write_end,
	.set_page_dirty	= hugetlbfs_set_page_dirty,
	.migratepage    = hugetlbfs_migrate_page,
};


static void init_once(void *foo)
{
	struct hugetlbfs_inode_info *ei = (struct hugetlbfs_inode_info *)foo;

	inode_init_once(&ei->vfs_inode);
}

const struct file_operations hugetlbfs_file_operations = {
	.read_iter		= hugetlbfs_read_iter,
	.mmap			= hugetlbfs_file_mmap,
	.fsync			= noop_fsync,
	.get_unmapped_area	= hugetlb_get_unmapped_area,
	.llseek			= default_llseek,
	.fallocate		= hugetlbfs_fallocate,
};

static const struct inode_operations hugetlbfs_dir_inode_operations = {
	.create		= hugetlbfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= hugetlbfs_symlink,
	.mkdir		= hugetlbfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= hugetlbfs_mknod,
	.rename		= simple_rename,
	.setattr	= hugetlbfs_setattr,
};

static const struct inode_operations hugetlbfs_inode_operations = {
	.setattr	= hugetlbfs_setattr,
};

static const struct super_operations hugetlbfs_ops = {
	.alloc_inode    = hugetlbfs_alloc_inode,
	.destroy_inode  = hugetlbfs_destroy_inode,
	.evict_inode	= hugetlbfs_evict_inode,
	.statfs		= hugetlbfs_statfs,
	.put_super	= hugetlbfs_put_super,
	.show_options	= generic_show_options,
};

enum { NO_SIZE, SIZE_STD, SIZE_PERCENT };

/*
 * Convert size option passed from command line to number of huge pages
 * in the pool specified by hstate.  Size option could be in bytes
 * (val_type == SIZE_STD) or percentage of the pool (val_type == SIZE_PERCENT).
 */
static long long
hugetlbfs_size_to_hpages(struct hstate *h, unsigned long long size_opt,
								int val_type)
{
	if (val_type == NO_SIZE)
		return -1;

	if (val_type == SIZE_PERCENT) {
		size_opt <<= huge_page_shift(h);
		size_opt *= h->max_huge_pages;
		do_div(size_opt, 100);
	}

	size_opt >>= huge_page_shift(h);
	return size_opt;
}

static int
hugetlbfs_parse_options(char *options, struct hugetlbfs_config *pconfig)
{
	char *p, *rest;
	substring_t args[MAX_OPT_ARGS];
	int option;
	unsigned long long max_size_opt = 0, min_size_opt = 0;
	int max_val_type = NO_SIZE, min_val_type = NO_SIZE;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
 				goto bad_val;
			pconfig->uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(pconfig->uid))
				goto bad_val;
			break;

		case Opt_gid:
			if (match_int(&args[0], &option))
 				goto bad_val;
			pconfig->gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(pconfig->gid))
				goto bad_val;
			break;

		case Opt_mode:
			if (match_octal(&args[0], &option))
 				goto bad_val;
			pconfig->mode = option & 01777U;
			break;

		case Opt_size: {
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			max_size_opt = memparse(args[0].from, &rest);
			max_val_type = SIZE_STD;
			if (*rest == '%')
				max_val_type = SIZE_PERCENT;
			break;
		}

		case Opt_nr_inodes:
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			pconfig->nr_inodes = memparse(args[0].from, &rest);
			break;

		case Opt_pagesize: {
			unsigned long ps;
			ps = memparse(args[0].from, &rest);
			pconfig->hstate = size_to_hstate(ps);
			if (!pconfig->hstate) {
				pr_err("Unsupported page size %lu MB\n",
					ps >> 20);
				return -EINVAL;
			}
			break;
		}

		case Opt_min_size: {
			/* memparse() will accept a K/M/G without a digit */
			if (!isdigit(*args[0].from))
				goto bad_val;
			min_size_opt = memparse(args[0].from, &rest);
			min_val_type = SIZE_STD;
			if (*rest == '%')
				min_val_type = SIZE_PERCENT;
			break;
		}

		default:
			pr_err("Bad mount option: \"%s\"\n", p);
			return -EINVAL;
			break;
		}
	}

	/*
	 * Use huge page pool size (in hstate) to convert the size
	 * options to number of huge pages.  If NO_SIZE, -1 is returned.
	 */
	pconfig->max_hpages = hugetlbfs_size_to_hpages(pconfig->hstate,
						max_size_opt, max_val_type);
	pconfig->min_hpages = hugetlbfs_size_to_hpages(pconfig->hstate,
						min_size_opt, min_val_type);

	/*
	 * If max_size was specified, then min_size must be smaller
	 */
	if (max_val_type > NO_SIZE &&
	    pconfig->min_hpages > pconfig->max_hpages) {
		pr_err("minimum size can not be greater than maximum size\n");
		return -EINVAL;
	}

	return 0;

bad_val:
	pr_err("Bad value '%s' for mount option '%s'\n", args[0].from, p);
 	return -EINVAL;
}

static int
hugetlbfs_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret;
	struct hugetlbfs_config config;
	struct hugetlbfs_sb_info *sbinfo;

	save_mount_options(sb, data);

	config.max_hpages = -1; /* No limit on size by default */
	config.nr_inodes = -1; /* No limit on number of inodes by default */
	config.uid = current_fsuid();
	config.gid = current_fsgid();
	config.mode = 0755;
	config.hstate = &default_hstate;
	config.min_hpages = -1; /* No default minimum size */
	ret = hugetlbfs_parse_options(data, &config);
	if (ret)
		return ret;

	sbinfo = kmalloc(sizeof(struct hugetlbfs_sb_info), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;
	sb->s_fs_info = sbinfo;
	sbinfo->hstate = config.hstate;
	spin_lock_init(&sbinfo->stat_lock);
	sbinfo->max_inodes = config.nr_inodes;
	sbinfo->free_inodes = config.nr_inodes;
	sbinfo->spool = NULL;
	/*
	 * Allocate and initialize subpool if maximum or minimum size is
	 * specified.  Any needed reservations (for minimim size) are taken
	 * taken when the subpool is created.
	 */
	if (config.max_hpages != -1 || config.min_hpages != -1) {
		sbinfo->spool = hugepage_new_subpool(config.hstate,
							config.max_hpages,
							config.min_hpages);
		if (!sbinfo->spool)
			goto out_free;
	}
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = huge_page_size(config.hstate);
	sb->s_blocksize_bits = huge_page_shift(config.hstate);
	sb->s_magic = HUGETLBFS_MAGIC;
	sb->s_op = &hugetlbfs_ops;
	sb->s_time_gran = 1;
	sb->s_root = d_make_root(hugetlbfs_get_root(sb, &config));
	if (!sb->s_root)
		goto out_free;
	return 0;
out_free:
	kfree(sbinfo->spool);
	kfree(sbinfo);
	return -ENOMEM;
}

static struct dentry *hugetlbfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, hugetlbfs_fill_super);
}

static struct file_system_type hugetlbfs_fs_type = {
	.name		= "hugetlbfs",
	.mount		= hugetlbfs_mount,
	.kill_sb	= kill_litter_super,
};

static struct vfsmount *hugetlbfs_vfsmount[HUGE_MAX_HSTATE];

static int can_do_hugetlb_shm(void)
{
	kgid_t shm_group;
	shm_group = make_kgid(&init_user_ns, sysctl_hugetlb_shm_group);
	return capable(CAP_IPC_LOCK) || in_group_p(shm_group);
}

static int get_hstate_idx(int page_size_log)
{
	struct hstate *h = hstate_sizelog(page_size_log);

	if (!h)
		return -1;
	return h - hstates;
}

static const struct dentry_operations anon_ops = {
	.d_dname = simple_dname
};

/*
 * Note that size should be aligned to proper hugepage size in caller side,
 * otherwise hugetlb_reserve_pages reserves one less hugepages than intended.
 */
struct file *hugetlb_file_setup(const char *name, size_t size,
				vm_flags_t acctflag, struct user_struct **user,
				int creat_flags, int page_size_log)
{
	struct file *file = ERR_PTR(-ENOMEM);
	struct inode *inode;
	struct path path;
	struct super_block *sb;
	struct qstr quick_string;
	int hstate_idx;

	hstate_idx = get_hstate_idx(page_size_log);
	if (hstate_idx < 0)
		return ERR_PTR(-ENODEV);

	*user = NULL;
	if (!hugetlbfs_vfsmount[hstate_idx])
		return ERR_PTR(-ENOENT);

	if (creat_flags == HUGETLB_SHMFS_INODE && !can_do_hugetlb_shm()) {
		*user = current_user();
		if (user_shm_lock(size, *user)) {
			task_lock(current);
			pr_warn_once("%s (%d): Using mlock ulimits for SHM_HUGETLB is deprecated\n",
				current->comm, current->pid);
			task_unlock(current);
		} else {
			*user = NULL;
			return ERR_PTR(-EPERM);
		}
	}

	sb = hugetlbfs_vfsmount[hstate_idx]->mnt_sb;
	quick_string.name = name;
	quick_string.len = strlen(quick_string.name);
	quick_string.hash = 0;
	path.dentry = d_alloc_pseudo(sb, &quick_string);
	if (!path.dentry)
		goto out_shm_unlock;

	d_set_d_op(path.dentry, &anon_ops);
	path.mnt = mntget(hugetlbfs_vfsmount[hstate_idx]);
	file = ERR_PTR(-ENOSPC);
	inode = hugetlbfs_get_inode(sb, NULL, S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out_dentry;
	if (creat_flags == HUGETLB_SHMFS_INODE)
		inode->i_flags |= S_PRIVATE;

	file = ERR_PTR(-ENOMEM);
	if (hugetlb_reserve_pages(inode, 0,
			size >> huge_page_shift(hstate_inode(inode)), NULL,
			acctflag))
		goto out_inode;

	d_instantiate(path.dentry, inode);
	inode->i_size = size;
	clear_nlink(inode);

	file = alloc_file(&path, FMODE_WRITE | FMODE_READ,
			&hugetlbfs_file_operations);
	if (IS_ERR(file))
		goto out_dentry; /* inode is already attached */

	return file;

out_inode:
	iput(inode);
out_dentry:
	path_put(&path);
out_shm_unlock:
	if (*user) {
		user_shm_unlock(size, *user);
		*user = NULL;
	}
	return file;
}

static int __init init_hugetlbfs_fs(void)
{
	struct hstate *h;
	int error;
	int i;

	if (!hugepages_supported()) {
		pr_info("disabling because there are no supported hugepage sizes\n");
		return -ENOTSUPP;
	}

	error = -ENOMEM;
	hugetlbfs_inode_cachep = kmem_cache_create("hugetlbfs_inode_cache",
					sizeof(struct hugetlbfs_inode_info),
					0, SLAB_ACCOUNT, init_once);
	if (hugetlbfs_inode_cachep == NULL)
		goto out2;

	error = register_filesystem(&hugetlbfs_fs_type);
	if (error)
		goto out;

	i = 0;
	for_each_hstate(h) {
		char buf[50];
		unsigned ps_kb = 1U << (h->order + PAGE_SHIFT - 10);

		snprintf(buf, sizeof(buf), "pagesize=%uK", ps_kb);
		hugetlbfs_vfsmount[i] = kern_mount_data(&hugetlbfs_fs_type,
							buf);

		if (IS_ERR(hugetlbfs_vfsmount[i])) {
			pr_err("Cannot mount internal hugetlbfs for "
				"page size %uK", ps_kb);
			error = PTR_ERR(hugetlbfs_vfsmount[i]);
			hugetlbfs_vfsmount[i] = NULL;
		}
		i++;
	}
	/* Non default hstates are optional */
	if (!IS_ERR_OR_NULL(hugetlbfs_vfsmount[default_hstate_idx]))
		return 0;

 out:
	kmem_cache_destroy(hugetlbfs_inode_cachep);
 out2:
	return error;
}
fs_initcall(init_hugetlbfs_fs)
