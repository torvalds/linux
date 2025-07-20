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
#include <linux/fs_parser.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/dnotify.h>
#include <linux/statfs.h>
#include <linux/security.h>
#include <linux/magic.h>
#include <linux/migrate.h>
#include <linux/uio.h>

#include <linux/uaccess.h>
#include <linux/sched/mm.h>

#define CREATE_TRACE_POINTS
#include <trace/events/hugetlbfs.h>

static const struct address_space_operations hugetlbfs_aops;
static const struct file_operations hugetlbfs_file_operations;
static const struct inode_operations hugetlbfs_dir_inode_operations;
static const struct inode_operations hugetlbfs_inode_operations;

enum hugetlbfs_size_type { NO_SIZE, SIZE_STD, SIZE_PERCENT };

struct hugetlbfs_fs_context {
	struct hstate		*hstate;
	unsigned long long	max_size_opt;
	unsigned long long	min_size_opt;
	long			max_hpages;
	long			nr_inodes;
	long			min_hpages;
	enum hugetlbfs_size_type max_val_type;
	enum hugetlbfs_size_type min_val_type;
	kuid_t			uid;
	kgid_t			gid;
	umode_t			mode;
};

int sysctl_hugetlb_shm_group;

enum hugetlb_param {
	Opt_gid,
	Opt_min_size,
	Opt_mode,
	Opt_nr_inodes,
	Opt_pagesize,
	Opt_size,
	Opt_uid,
};

static const struct fs_parameter_spec hugetlb_fs_parameters[] = {
	fsparam_gid   ("gid",		Opt_gid),
	fsparam_string("min_size",	Opt_min_size),
	fsparam_u32oct("mode",		Opt_mode),
	fsparam_string("nr_inodes",	Opt_nr_inodes),
	fsparam_string("pagesize",	Opt_pagesize),
	fsparam_string("size",		Opt_size),
	fsparam_uid   ("uid",		Opt_uid),
	{}
};

/*
 * Mask used when checking the page offset value passed in via system
 * calls.  This value will be converted to a loff_t which is signed.
 * Therefore, we want to check the upper PAGE_SHIFT + 1 bits of the
 * value.  The extra bit (- 1 in the shift value) is to take the sign
 * bit into account.
 */
#define PGOFF_LOFFT_MAX \
	(((1UL << (PAGE_SHIFT + 1)) - 1) <<  (BITS_PER_LONG - (PAGE_SHIFT + 1)))

static int hugetlbfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	loff_t len, vma_len;
	int ret;
	struct hstate *h = hstate_file(file);
	vm_flags_t vm_flags;

	/*
	 * vma address alignment (but not the pgoff alignment) has
	 * already been checked by prepare_hugepage_range.  If you add
	 * any error returns here, do so after setting VM_HUGETLB, so
	 * is_vm_hugetlb_page tests below unmap_region go the right
	 * way when do_mmap unwinds (may be important on powerpc
	 * and ia64).
	 */
	vm_flags_set(vma, VM_HUGETLB | VM_DONTEXPAND);
	vma->vm_ops = &hugetlb_vm_ops;

	/*
	 * page based offset in vm_pgoff could be sufficiently large to
	 * overflow a loff_t when converted to byte offset.  This can
	 * only happen on architectures where sizeof(loff_t) ==
	 * sizeof(unsigned long).  So, only check in those instances.
	 */
	if (sizeof(unsigned long) == sizeof(loff_t)) {
		if (vma->vm_pgoff & PGOFF_LOFFT_MAX)
			return -EINVAL;
	}

	/* must be huge page aligned */
	if (vma->vm_pgoff & (~huge_page_mask(h) >> PAGE_SHIFT))
		return -EINVAL;

	vma_len = (loff_t)(vma->vm_end - vma->vm_start);
	len = vma_len + ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	/* check for overflow */
	if (len < vma_len)
		return -EINVAL;

	inode_lock(inode);
	file_accessed(file);

	ret = -ENOMEM;

	vm_flags = vma->vm_flags;
	/*
	 * for SHM_HUGETLB, the pages are reserved in the shmget() call so skip
	 * reserving here. Note: only for SHM hugetlbfs file, the inode
	 * flag S_PRIVATE is set.
	 */
	if (inode->i_flags & S_PRIVATE)
		vm_flags |= VM_NORESERVE;

	if (!hugetlb_reserve_pages(inode,
				vma->vm_pgoff >> huge_page_order(h),
				len >> huge_page_shift(h), vma,
				vm_flags))
		goto out;

	ret = 0;
	if (vma->vm_flags & VM_WRITE && inode->i_size < len)
		i_size_write(inode, len);
out:
	inode_unlock(inode);

	return ret;
}

/*
 * Called under mmap_write_lock(mm).
 */

unsigned long
hugetlb_get_unmapped_area(struct file *file, unsigned long addr,
			    unsigned long len, unsigned long pgoff,
			    unsigned long flags)
{
	unsigned long addr0 = 0;
	struct hstate *h = hstate_file(file);

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (flags & MAP_FIXED) {
		if (addr & ~huge_page_mask(h))
			return -EINVAL;
		if (prepare_hugepage_range(file, addr, len))
			return -EINVAL;
	}
	if (addr)
		addr0 = ALIGN(addr, huge_page_size(h));

	return mm_get_unmapped_area_vmflags(current->mm, file, addr0, len, pgoff,
					    flags, 0);
}

/*
 * Someone wants to read @bytes from a HWPOISON hugetlb @folio from @offset.
 * Returns the maximum number of bytes one can read without touching the 1st raw
 * HWPOISON page.
 *
 * The implementation borrows the iteration logic from copy_page_to_iter*.
 */
static size_t adjust_range_hwpoison(struct folio *folio, size_t offset,
		size_t bytes)
{
	struct page *page;
	size_t n = 0;
	size_t res = 0;

	/* First page to start the loop. */
	page = folio_page(folio, offset / PAGE_SIZE);
	offset %= PAGE_SIZE;
	while (1) {
		if (is_raw_hwpoison_page_in_hugepage(page))
			break;

		/* Safe to read n bytes without touching HWPOISON subpage. */
		n = min(bytes, (size_t)PAGE_SIZE - offset);
		res += n;
		bytes -= n;
		if (!bytes || !n)
			break;
		offset += n;
		if (offset == PAGE_SIZE) {
			page = nth_page(page, 1);
			offset = 0;
		}
	}

	return res;
}

/*
 * Support for read() - Find the page attached to f_mapping and copy out the
 * data. This provides functionality similar to filemap_read().
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
		struct folio *folio;
		size_t nr, copied, want;

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

		/* Find the folio */
		folio = filemap_lock_hugetlb_folio(h, mapping, index);
		if (IS_ERR(folio)) {
			/*
			 * We have a HOLE, zero out the user-buffer for the
			 * length of the hole or request.
			 */
			copied = iov_iter_zero(nr, to);
		} else {
			folio_unlock(folio);

			if (!folio_test_hwpoison(folio))
				want = nr;
			else {
				/*
				 * Adjust how many bytes safe to read without
				 * touching the 1st raw HWPOISON page after
				 * offset.
				 */
				want = adjust_range_hwpoison(folio, offset, nr);
				if (want == 0) {
					folio_put(folio);
					retval = -EIO;
					break;
				}
			}

			/*
			 * We have the folio, copy it to user space buffer.
			 */
			copied = copy_folio_to_iter(folio, offset, want, to);
			folio_put(folio);
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
			loff_t pos, unsigned len,
			struct folio **foliop, void **fsdata)
{
	return -EINVAL;
}

static int hugetlbfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct folio *folio, void *fsdata)
{
	BUG();
	return -EINVAL;
}

static void hugetlb_delete_from_page_cache(struct folio *folio)
{
	folio_clear_dirty(folio);
	folio_clear_uptodate(folio);
	filemap_remove_folio(folio);
}

/*
 * Called with i_mmap_rwsem held for inode based vma maps.  This makes
 * sure vma (and vm_mm) will not go away.  We also hold the hugetlb fault
 * mutex for the page in the mapping.  So, we can not race with page being
 * faulted into the vma.
 */
static bool hugetlb_vma_maps_pfn(struct vm_area_struct *vma,
				unsigned long addr, unsigned long pfn)
{
	pte_t *ptep, pte;

	ptep = hugetlb_walk(vma, addr, huge_page_size(hstate_vma(vma)));
	if (!ptep)
		return false;

	pte = huge_ptep_get(vma->vm_mm, addr, ptep);
	if (huge_pte_none(pte) || !pte_present(pte))
		return false;

	if (pte_pfn(pte) == pfn)
		return true;

	return false;
}

/*
 * Can vma_offset_start/vma_offset_end overflow on 32-bit arches?
 * No, because the interval tree returns us only those vmas
 * which overlap the truncated area starting at pgoff,
 * and no vma on a 32-bit arch can span beyond the 4GB.
 */
static unsigned long vma_offset_start(struct vm_area_struct *vma, pgoff_t start)
{
	unsigned long offset = 0;

	if (vma->vm_pgoff < start)
		offset = (start - vma->vm_pgoff) << PAGE_SHIFT;

	return vma->vm_start + offset;
}

static unsigned long vma_offset_end(struct vm_area_struct *vma, pgoff_t end)
{
	unsigned long t_end;

	if (!end)
		return vma->vm_end;

	t_end = ((end - vma->vm_pgoff) << PAGE_SHIFT) + vma->vm_start;
	if (t_end > vma->vm_end)
		t_end = vma->vm_end;
	return t_end;
}

/*
 * Called with hugetlb fault mutex held.  Therefore, no more mappings to
 * this folio can be created while executing the routine.
 */
static void hugetlb_unmap_file_folio(struct hstate *h,
					struct address_space *mapping,
					struct folio *folio, pgoff_t index)
{
	struct rb_root_cached *root = &mapping->i_mmap;
	struct hugetlb_vma_lock *vma_lock;
	unsigned long pfn = folio_pfn(folio);
	struct vm_area_struct *vma;
	unsigned long v_start;
	unsigned long v_end;
	pgoff_t start, end;

	start = index * pages_per_huge_page(h);
	end = (index + 1) * pages_per_huge_page(h);

	i_mmap_lock_write(mapping);
retry:
	vma_lock = NULL;
	vma_interval_tree_foreach(vma, root, start, end - 1) {
		v_start = vma_offset_start(vma, start);
		v_end = vma_offset_end(vma, end);

		if (!hugetlb_vma_maps_pfn(vma, v_start, pfn))
			continue;

		if (!hugetlb_vma_trylock_write(vma)) {
			vma_lock = vma->vm_private_data;
			/*
			 * If we can not get vma lock, we need to drop
			 * immap_sema and take locks in order.  First,
			 * take a ref on the vma_lock structure so that
			 * we can be guaranteed it will not go away when
			 * dropping immap_sema.
			 */
			kref_get(&vma_lock->refs);
			break;
		}

		unmap_hugepage_range(vma, v_start, v_end, NULL,
				     ZAP_FLAG_DROP_MARKER);
		hugetlb_vma_unlock_write(vma);
	}

	i_mmap_unlock_write(mapping);

	if (vma_lock) {
		/*
		 * Wait on vma_lock.  We know it is still valid as we have
		 * a reference.  We must 'open code' vma locking as we do
		 * not know if vma_lock is still attached to vma.
		 */
		down_write(&vma_lock->rw_sema);
		i_mmap_lock_write(mapping);

		vma = vma_lock->vma;
		if (!vma) {
			/*
			 * If lock is no longer attached to vma, then just
			 * unlock, drop our reference and retry looking for
			 * other vmas.
			 */
			up_write(&vma_lock->rw_sema);
			kref_put(&vma_lock->refs, hugetlb_vma_lock_release);
			goto retry;
		}

		/*
		 * vma_lock is still attached to vma.  Check to see if vma
		 * still maps page and if so, unmap.
		 */
		v_start = vma_offset_start(vma, start);
		v_end = vma_offset_end(vma, end);
		if (hugetlb_vma_maps_pfn(vma, v_start, pfn))
			unmap_hugepage_range(vma, v_start, v_end, NULL,
					     ZAP_FLAG_DROP_MARKER);

		kref_put(&vma_lock->refs, hugetlb_vma_lock_release);
		hugetlb_vma_unlock_write(vma);

		goto retry;
	}
}

static void
hugetlb_vmdelete_list(struct rb_root_cached *root, pgoff_t start, pgoff_t end,
		      zap_flags_t zap_flags)
{
	struct vm_area_struct *vma;

	/*
	 * end == 0 indicates that the entire range after start should be
	 * unmapped.  Note, end is exclusive, whereas the interval tree takes
	 * an inclusive "last".
	 */
	vma_interval_tree_foreach(vma, root, start, end ? end - 1 : ULONG_MAX) {
		unsigned long v_start;
		unsigned long v_end;

		if (!hugetlb_vma_trylock_write(vma))
			continue;

		v_start = vma_offset_start(vma, start);
		v_end = vma_offset_end(vma, end);

		unmap_hugepage_range(vma, v_start, v_end, NULL, zap_flags);

		/*
		 * Note that vma lock only exists for shared/non-private
		 * vmas.  Therefore, lock is not held when calling
		 * unmap_hugepage_range for private vmas.
		 */
		hugetlb_vma_unlock_write(vma);
	}
}

/*
 * Called with hugetlb fault mutex held.
 * Returns true if page was actually removed, false otherwise.
 */
static bool remove_inode_single_folio(struct hstate *h, struct inode *inode,
					struct address_space *mapping,
					struct folio *folio, pgoff_t index,
					bool truncate_op)
{
	bool ret = false;

	/*
	 * If folio is mapped, it was faulted in after being
	 * unmapped in caller.  Unmap (again) while holding
	 * the fault mutex.  The mutex will prevent faults
	 * until we finish removing the folio.
	 */
	if (unlikely(folio_mapped(folio)))
		hugetlb_unmap_file_folio(h, mapping, folio, index);

	folio_lock(folio);
	/*
	 * We must remove the folio from page cache before removing
	 * the region/ reserve map (hugetlb_unreserve_pages).  In
	 * rare out of memory conditions, removal of the region/reserve
	 * map could fail.  Correspondingly, the subpool and global
	 * reserve usage count can need to be adjusted.
	 */
	VM_BUG_ON_FOLIO(folio_test_hugetlb_restore_reserve(folio), folio);
	hugetlb_delete_from_page_cache(folio);
	ret = true;
	if (!truncate_op) {
		if (unlikely(hugetlb_unreserve_pages(inode, index,
							index + 1, 1)))
			hugetlb_fix_reserve_counts(inode);
	}

	folio_unlock(folio);
	return ret;
}

/*
 * remove_inode_hugepages handles two distinct cases: truncation and hole
 * punch.  There are subtle differences in operation for each case.
 *
 * truncation is indicated by end of range being LLONG_MAX
 *	In this case, we first scan the range and release found pages.
 *	After releasing pages, hugetlb_unreserve_pages cleans up region/reserve
 *	maps and global counts.  Page faults can race with truncation.
 *	During faults, hugetlb_no_page() checks i_size before page allocation,
 *	and again after obtaining page table lock.  It will 'back out'
 *	allocations in the truncated range.
 * hole punch is indicated if end is not LLONG_MAX
 *	In the hole punch case we scan the range and release found pages.
 *	Only when releasing a page is the associated region/reserve map
 *	deleted.  The region/reserve map for ranges without associated
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
	const pgoff_t end = lend >> PAGE_SHIFT;
	struct folio_batch fbatch;
	pgoff_t next, index;
	int i, freed = 0;
	bool truncate_op = (lend == LLONG_MAX);

	folio_batch_init(&fbatch);
	next = lstart >> PAGE_SHIFT;
	while (filemap_get_folios(mapping, &next, end - 1, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); ++i) {
			struct folio *folio = fbatch.folios[i];
			u32 hash = 0;

			index = folio->index >> huge_page_order(h);
			hash = hugetlb_fault_mutex_hash(mapping, index);
			mutex_lock(&hugetlb_fault_mutex_table[hash]);

			/*
			 * Remove folio that was part of folio_batch.
			 */
			if (remove_inode_single_folio(h, inode, mapping, folio,
							index, truncate_op))
				freed++;

			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
		}
		folio_batch_release(&fbatch);
		cond_resched();
	}

	if (truncate_op)
		(void)hugetlb_unreserve_pages(inode,
				lstart >> huge_page_shift(h),
				LONG_MAX, freed);
}

static void hugetlbfs_evict_inode(struct inode *inode)
{
	struct resv_map *resv_map;

	trace_hugetlbfs_evict_inode(inode);
	remove_inode_hugepages(inode, 0, LLONG_MAX);

	/*
	 * Get the resv_map from the address space embedded in the inode.
	 * This is the address space which points to any resv_map allocated
	 * at inode creation time.  If this is a device special inode,
	 * i_mapping may not point to the original address space.
	 */
	resv_map = (struct resv_map *)(&inode->i_data)->i_private_data;
	/* Only regular and link inodes have associated reserve maps */
	if (resv_map)
		resv_map_release(&resv_map->refs);
	clear_inode(inode);
}

static void hugetlb_vmtruncate(struct inode *inode, loff_t offset)
{
	pgoff_t pgoff;
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);

	BUG_ON(offset & ~huge_page_mask(h));
	pgoff = offset >> PAGE_SHIFT;

	i_size_write(inode, offset);
	i_mmap_lock_write(mapping);
	if (!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root))
		hugetlb_vmdelete_list(&mapping->i_mmap, pgoff, 0,
				      ZAP_FLAG_DROP_MARKER);
	i_mmap_unlock_write(mapping);
	remove_inode_hugepages(inode, offset, LLONG_MAX);
}

static void hugetlbfs_zero_partial_page(struct hstate *h,
					struct address_space *mapping,
					loff_t start,
					loff_t end)
{
	pgoff_t idx = start >> huge_page_shift(h);
	struct folio *folio;

	folio = filemap_lock_hugetlb_folio(h, mapping, idx);
	if (IS_ERR(folio))
		return;

	start = start & ~huge_page_mask(h);
	end = end & ~huge_page_mask(h);
	if (!end)
		end = huge_page_size(h);

	folio_zero_segment(folio, (size_t)start, (size_t)end);

	folio_unlock(folio);
	folio_put(folio);
}

static long hugetlbfs_punch_hole(struct inode *inode, loff_t offset, loff_t len)
{
	struct hugetlbfs_inode_info *info = HUGETLBFS_I(inode);
	struct address_space *mapping = inode->i_mapping;
	struct hstate *h = hstate_inode(inode);
	loff_t hpage_size = huge_page_size(h);
	loff_t hole_start, hole_end;

	/*
	 * hole_start and hole_end indicate the full pages within the hole.
	 */
	hole_start = round_up(offset, hpage_size);
	hole_end = round_down(offset + len, hpage_size);

	inode_lock(inode);

	/* protected by i_rwsem */
	if (info->seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)) {
		inode_unlock(inode);
		return -EPERM;
	}

	i_mmap_lock_write(mapping);

	/* If range starts before first full page, zero partial page. */
	if (offset < hole_start)
		hugetlbfs_zero_partial_page(h, mapping,
				offset, min(offset + len, hole_start));

	/* Unmap users of full pages in the hole. */
	if (hole_end > hole_start) {
		if (!RB_EMPTY_ROOT(&mapping->i_mmap.rb_root))
			hugetlb_vmdelete_list(&mapping->i_mmap,
					      hole_start >> PAGE_SHIFT,
					      hole_end >> PAGE_SHIFT, 0);
	}

	/* If range extends beyond last full page, zero partial page. */
	if ((offset + len) > hole_end && (offset + len) > hole_start)
		hugetlbfs_zero_partial_page(h, mapping,
				hole_end, offset + len);

	i_mmap_unlock_write(mapping);

	/* Remove full pages from the file. */
	if (hole_end > hole_start)
		remove_inode_hugepages(inode, hole_start, hole_end);

	inode_unlock(inode);

	return 0;
}

static long hugetlbfs_fallocate(struct file *file, int mode, loff_t offset,
				loff_t len)
{
	struct inode *inode = file_inode(file);
	struct hugetlbfs_inode_info *info = HUGETLBFS_I(inode);
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

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		error = hugetlbfs_punch_hole(inode, offset, len);
		goto out_nolock;
	}

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

	if ((info->seals & F_SEAL_GROW) && offset + len > inode->i_size) {
		error = -EPERM;
		goto out;
	}

	/*
	 * Initialize a pseudo vma as this is required by the huge page
	 * allocation routines.
	 */
	vma_init(&pseudo_vma, mm);
	vm_flags_init(&pseudo_vma, VM_HUGETLB | VM_MAYSHARE | VM_SHARED);
	pseudo_vma.vm_file = file;

	for (index = start; index < end; index++) {
		/*
		 * This is supposed to be the vaddr where the page is being
		 * faulted in, but we have no vaddr here.
		 */
		struct folio *folio;
		unsigned long addr;

		cond_resched();

		/*
		 * fallocate(2) manpage permits EINTR; we may have been
		 * interrupted because we are using up too much memory.
		 */
		if (signal_pending(current)) {
			error = -EINTR;
			break;
		}

		/* addr is the offset within the file (zero based) */
		addr = index * hpage_size;

		/* mutex taken here, fault path and hole punch */
		hash = hugetlb_fault_mutex_hash(mapping, index);
		mutex_lock(&hugetlb_fault_mutex_table[hash]);

		/* See if already present in mapping to avoid alloc/free */
		folio = filemap_get_folio(mapping, index << huge_page_order(h));
		if (!IS_ERR(folio)) {
			folio_put(folio);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			continue;
		}

		/*
		 * Allocate folio without setting the avoid_reserve argument.
		 * There certainly are no reserves associated with the
		 * pseudo_vma.  However, there could be shared mappings with
		 * reserves for the file at the inode level.  If we fallocate
		 * folios in these areas, we need to consume the reserves
		 * to keep reservation accounting consistent.
		 */
		folio = alloc_hugetlb_folio(&pseudo_vma, addr, false);
		if (IS_ERR(folio)) {
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			error = PTR_ERR(folio);
			goto out;
		}
		folio_zero_user(folio, addr);
		__folio_mark_uptodate(folio);
		error = hugetlb_add_to_page_cache(folio, mapping, index);
		if (unlikely(error)) {
			restore_reserve_on_error(h, &pseudo_vma, addr, folio);
			folio_put(folio);
			mutex_unlock(&hugetlb_fault_mutex_table[hash]);
			goto out;
		}

		mutex_unlock(&hugetlb_fault_mutex_table[hash]);

		folio_set_hugetlb_migratable(folio);
		/*
		 * folio_unlock because locked by hugetlb_add_to_page_cache()
		 * folio_put() due to reference from alloc_hugetlb_folio()
		 */
		folio_unlock(folio);
		folio_put(folio);
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + len > inode->i_size)
		i_size_write(inode, offset + len);
	inode_set_ctime_current(inode);
out:
	inode_unlock(inode);

out_nolock:
	trace_hugetlbfs_fallocate(inode, mode, offset, len, error);
	return error;
}

static int hugetlbfs_setattr(struct mnt_idmap *idmap,
			     struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct hstate *h = hstate_inode(inode);
	int error;
	unsigned int ia_valid = attr->ia_valid;
	struct hugetlbfs_inode_info *info = HUGETLBFS_I(inode);

	error = setattr_prepare(idmap, dentry, attr);
	if (error)
		return error;

	trace_hugetlbfs_setattr(inode, dentry, attr);

	if (ia_valid & ATTR_SIZE) {
		loff_t oldsize = inode->i_size;
		loff_t newsize = attr->ia_size;

		if (newsize & ~huge_page_mask(h))
			return -EINVAL;
		/* protected by i_rwsem */
		if ((newsize < oldsize && (info->seals & F_SEAL_SHRINK)) ||
		    (newsize > oldsize && (info->seals & F_SEAL_GROW)))
			return -EPERM;
		hugetlb_vmtruncate(inode, newsize);
	}

	setattr_copy(idmap, inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static struct inode *hugetlbfs_get_root(struct super_block *sb,
					struct hugetlbfs_fs_context *ctx)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_mode = S_IFDIR | ctx->mode;
		inode->i_uid = ctx->uid;
		inode->i_gid = ctx->gid;
		simple_inode_init_ts(inode);
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
					struct mnt_idmap *idmap,
					struct inode *dir,
					umode_t mode, dev_t dev)
{
	struct inode *inode;
	struct resv_map *resv_map = NULL;

	/*
	 * Reserve maps are only needed for inodes that can have associated
	 * page allocations.
	 */
	if (S_ISREG(mode) || S_ISLNK(mode)) {
		resv_map = resv_map_alloc();
		if (!resv_map)
			return NULL;
	}

	inode = new_inode(sb);
	if (inode) {
		struct hugetlbfs_inode_info *info = HUGETLBFS_I(inode);

		inode->i_ino = get_next_ino();
		inode_init_owner(idmap, inode, dir, mode);
		lockdep_set_class(&inode->i_mapping->i_mmap_rwsem,
				&hugetlbfs_i_mmap_rwsem_key);
		inode->i_mapping->a_ops = &hugetlbfs_aops;
		simple_inode_init_ts(inode);
		inode->i_mapping->i_private_data = resv_map;
		info->seals = F_SEAL_SEAL;
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
		trace_hugetlbfs_alloc_inode(inode, dir, mode);
	} else {
		if (resv_map)
			kref_put(&resv_map->refs, resv_map_release);
	}

	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
static int hugetlbfs_mknod(struct mnt_idmap *idmap, struct inode *dir,
			   struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;

	inode = hugetlbfs_get_inode(dir->i_sb, idmap, dir, mode, dev);
	if (!inode)
		return -ENOSPC;
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	d_instantiate(dentry, inode);
	dget(dentry);/* Extra count - pin the dentry in core */
	return 0;
}

static struct dentry *hugetlbfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
				      struct dentry *dentry, umode_t mode)
{
	int retval = hugetlbfs_mknod(idmap, dir, dentry,
				     mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return ERR_PTR(retval);
}

static int hugetlbfs_create(struct mnt_idmap *idmap,
			    struct inode *dir, struct dentry *dentry,
			    umode_t mode, bool excl)
{
	return hugetlbfs_mknod(idmap, dir, dentry, mode | S_IFREG, 0);
}

static int hugetlbfs_tmpfile(struct mnt_idmap *idmap,
			     struct inode *dir, struct file *file,
			     umode_t mode)
{
	struct inode *inode;

	inode = hugetlbfs_get_inode(dir->i_sb, idmap, dir, mode | S_IFREG, 0);
	if (!inode)
		return -ENOSPC;
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	d_tmpfile(file, inode);
	return finish_open_simple(file, 0);
}

static int hugetlbfs_symlink(struct mnt_idmap *idmap,
			     struct inode *dir, struct dentry *dentry,
			     const char *symname)
{
	const umode_t mode = S_IFLNK|S_IRWXUGO;
	struct inode *inode;
	int error = -ENOSPC;

	inode = hugetlbfs_get_inode(dir->i_sb, idmap, dir, mode, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
		} else
			iput(inode);
	}
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));

	return error;
}

#ifdef CONFIG_MIGRATION
static int hugetlbfs_migrate_folio(struct address_space *mapping,
				struct folio *dst, struct folio *src,
				enum migrate_mode mode)
{
	int rc;

	rc = migrate_huge_page_move_mapping(mapping, dst, src);
	if (rc != MIGRATEPAGE_SUCCESS)
		return rc;

	if (hugetlb_folio_subpool(src)) {
		hugetlb_set_folio_subpool(dst,
					hugetlb_folio_subpool(src));
		hugetlb_set_folio_subpool(src, NULL);
	}

	folio_migrate_flags(dst, src);

	return MIGRATEPAGE_SUCCESS;
}
#else
#define hugetlbfs_migrate_folio NULL
#endif

static int hugetlbfs_error_remove_folio(struct address_space *mapping,
				struct folio *folio)
{
	return 0;
}

/*
 * Display the mount options in /proc/mounts.
 */
static int hugetlbfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(root->d_sb);
	struct hugepage_subpool *spool = sbinfo->spool;
	unsigned long hpage_size = huge_page_size(sbinfo->hstate);
	unsigned hpage_shift = huge_page_shift(sbinfo->hstate);
	char mod;

	if (!uid_eq(sbinfo->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, sbinfo->uid));
	if (!gid_eq(sbinfo->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, sbinfo->gid));
	if (sbinfo->mode != 0755)
		seq_printf(m, ",mode=%o", sbinfo->mode);
	if (sbinfo->max_inodes != -1)
		seq_printf(m, ",nr_inodes=%lu", sbinfo->max_inodes);

	hpage_size /= 1024;
	mod = 'K';
	if (hpage_size >= 1024) {
		hpage_size /= 1024;
		mod = 'M';
	}
	seq_printf(m, ",pagesize=%lu%c", hpage_size, mod);
	if (spool) {
		if (spool->max_hpages != -1)
			seq_printf(m, ",size=%llu",
				   (unsigned long long)spool->max_hpages << hpage_shift);
		if (spool->min_hpages != -1)
			seq_printf(m, ",min_size=%llu",
				   (unsigned long long)spool->min_hpages << hpage_shift);
	}
	return 0;
}

static int hugetlbfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct hugetlbfs_sb_info *sbinfo = HUGETLBFS_SB(dentry->d_sb);
	struct hstate *h = hstate_inode(d_inode(dentry));
	u64 id = huge_encode_dev(dentry->d_sb->s_dev);

	buf->f_fsid = u64_to_fsid(id);
	buf->f_type = HUGETLBFS_MAGIC;
	buf->f_bsize = huge_page_size(h);
	if (sbinfo) {
		spin_lock(&sbinfo->stat_lock);
		/* If no limits set, just report 0 or -1 for max/free/used
		 * blocks, like simple_statfs() */
		if (sbinfo->spool) {
			long free_pages;

			spin_lock_irq(&sbinfo->spool->lock);
			buf->f_blocks = sbinfo->spool->max_hpages;
			free_pages = sbinfo->spool->max_hpages
				- sbinfo->spool->used_hpages;
			buf->f_bavail = buf->f_bfree = free_pages;
			spin_unlock_irq(&sbinfo->spool->lock);
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
	p = alloc_inode_sb(sb, hugetlbfs_inode_cachep, GFP_KERNEL);
	if (unlikely(!p)) {
		hugetlbfs_inc_free_inodes(sbinfo);
		return NULL;
	}
	return &p->vfs_inode;
}

static void hugetlbfs_free_inode(struct inode *inode)
{
	trace_hugetlbfs_free_inode(inode);
	kmem_cache_free(hugetlbfs_inode_cachep, HUGETLBFS_I(inode));
}

static void hugetlbfs_destroy_inode(struct inode *inode)
{
	hugetlbfs_inc_free_inodes(HUGETLBFS_SB(inode->i_sb));
}

static const struct address_space_operations hugetlbfs_aops = {
	.write_begin	= hugetlbfs_write_begin,
	.write_end	= hugetlbfs_write_end,
	.dirty_folio	= noop_dirty_folio,
	.migrate_folio  = hugetlbfs_migrate_folio,
	.error_remove_folio	= hugetlbfs_error_remove_folio,
};


static void init_once(void *foo)
{
	struct hugetlbfs_inode_info *ei = foo;

	inode_init_once(&ei->vfs_inode);
}

static const struct file_operations hugetlbfs_file_operations = {
	.read_iter		= hugetlbfs_read_iter,
	.mmap			= hugetlbfs_file_mmap,
	.fsync			= noop_fsync,
	.get_unmapped_area	= hugetlb_get_unmapped_area,
	.llseek			= default_llseek,
	.fallocate		= hugetlbfs_fallocate,
	.fop_flags		= FOP_HUGE_PAGES,
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
	.tmpfile	= hugetlbfs_tmpfile,
};

static const struct inode_operations hugetlbfs_inode_operations = {
	.setattr	= hugetlbfs_setattr,
};

static const struct super_operations hugetlbfs_ops = {
	.alloc_inode    = hugetlbfs_alloc_inode,
	.free_inode     = hugetlbfs_free_inode,
	.destroy_inode  = hugetlbfs_destroy_inode,
	.evict_inode	= hugetlbfs_evict_inode,
	.statfs		= hugetlbfs_statfs,
	.put_super	= hugetlbfs_put_super,
	.show_options	= hugetlbfs_show_options,
};

/*
 * Convert size option passed from command line to number of huge pages
 * in the pool specified by hstate.  Size option could be in bytes
 * (val_type == SIZE_STD) or percentage of the pool (val_type == SIZE_PERCENT).
 */
static long
hugetlbfs_size_to_hpages(struct hstate *h, unsigned long long size_opt,
			 enum hugetlbfs_size_type val_type)
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

/*
 * Parse one mount parameter.
 */
static int hugetlbfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct hugetlbfs_fs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	struct hstate *h;
	char *rest;
	unsigned long ps;
	int opt;

	opt = fs_parse(fc, hugetlb_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		ctx->uid = result.uid;
		return 0;

	case Opt_gid:
		ctx->gid = result.gid;
		return 0;

	case Opt_mode:
		ctx->mode = result.uint_32 & 01777U;
		return 0;

	case Opt_size:
		/* memparse() will accept a K/M/G without a digit */
		if (!param->string || !isdigit(param->string[0]))
			goto bad_val;
		ctx->max_size_opt = memparse(param->string, &rest);
		ctx->max_val_type = SIZE_STD;
		if (*rest == '%')
			ctx->max_val_type = SIZE_PERCENT;
		return 0;

	case Opt_nr_inodes:
		/* memparse() will accept a K/M/G without a digit */
		if (!param->string || !isdigit(param->string[0]))
			goto bad_val;
		ctx->nr_inodes = memparse(param->string, &rest);
		return 0;

	case Opt_pagesize:
		ps = memparse(param->string, &rest);
		h = size_to_hstate(ps);
		if (!h) {
			pr_err("Unsupported page size %lu MB\n", ps / SZ_1M);
			return -EINVAL;
		}
		ctx->hstate = h;
		return 0;

	case Opt_min_size:
		/* memparse() will accept a K/M/G without a digit */
		if (!param->string || !isdigit(param->string[0]))
			goto bad_val;
		ctx->min_size_opt = memparse(param->string, &rest);
		ctx->min_val_type = SIZE_STD;
		if (*rest == '%')
			ctx->min_val_type = SIZE_PERCENT;
		return 0;

	default:
		return -EINVAL;
	}

bad_val:
	return invalfc(fc, "Bad value '%s' for mount option '%s'\n",
		      param->string, param->key);
}

/*
 * Validate the parsed options.
 */
static int hugetlbfs_validate(struct fs_context *fc)
{
	struct hugetlbfs_fs_context *ctx = fc->fs_private;

	/*
	 * Use huge page pool size (in hstate) to convert the size
	 * options to number of huge pages.  If NO_SIZE, -1 is returned.
	 */
	ctx->max_hpages = hugetlbfs_size_to_hpages(ctx->hstate,
						   ctx->max_size_opt,
						   ctx->max_val_type);
	ctx->min_hpages = hugetlbfs_size_to_hpages(ctx->hstate,
						   ctx->min_size_opt,
						   ctx->min_val_type);

	/*
	 * If max_size was specified, then min_size must be smaller
	 */
	if (ctx->max_val_type > NO_SIZE &&
	    ctx->min_hpages > ctx->max_hpages) {
		pr_err("Minimum size can not be greater than maximum size\n");
		return -EINVAL;
	}

	return 0;
}

static int
hugetlbfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct hugetlbfs_fs_context *ctx = fc->fs_private;
	struct hugetlbfs_sb_info *sbinfo;

	sbinfo = kmalloc(sizeof(struct hugetlbfs_sb_info), GFP_KERNEL);
	if (!sbinfo)
		return -ENOMEM;
	sb->s_fs_info = sbinfo;
	spin_lock_init(&sbinfo->stat_lock);
	sbinfo->hstate		= ctx->hstate;
	sbinfo->max_inodes	= ctx->nr_inodes;
	sbinfo->free_inodes	= ctx->nr_inodes;
	sbinfo->spool		= NULL;
	sbinfo->uid		= ctx->uid;
	sbinfo->gid		= ctx->gid;
	sbinfo->mode		= ctx->mode;

	/*
	 * Allocate and initialize subpool if maximum or minimum size is
	 * specified.  Any needed reservations (for minimum size) are taken
	 * when the subpool is created.
	 */
	if (ctx->max_hpages != -1 || ctx->min_hpages != -1) {
		sbinfo->spool = hugepage_new_subpool(ctx->hstate,
						     ctx->max_hpages,
						     ctx->min_hpages);
		if (!sbinfo->spool)
			goto out_free;
	}
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = huge_page_size(ctx->hstate);
	sb->s_blocksize_bits = huge_page_shift(ctx->hstate);
	sb->s_magic = HUGETLBFS_MAGIC;
	sb->s_op = &hugetlbfs_ops;
	sb->s_time_gran = 1;

	/*
	 * Due to the special and limited functionality of hugetlbfs, it does
	 * not work well as a stacking filesystem.
	 */
	sb->s_stack_depth = FILESYSTEM_MAX_STACK_DEPTH;
	sb->s_root = d_make_root(hugetlbfs_get_root(sb, ctx));
	if (!sb->s_root)
		goto out_free;
	return 0;
out_free:
	kfree(sbinfo->spool);
	kfree(sbinfo);
	return -ENOMEM;
}

static int hugetlbfs_get_tree(struct fs_context *fc)
{
	int err = hugetlbfs_validate(fc);
	if (err)
		return err;
	return get_tree_nodev(fc, hugetlbfs_fill_super);
}

static void hugetlbfs_fs_context_free(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations hugetlbfs_fs_context_ops = {
	.free		= hugetlbfs_fs_context_free,
	.parse_param	= hugetlbfs_parse_param,
	.get_tree	= hugetlbfs_get_tree,
};

static int hugetlbfs_init_fs_context(struct fs_context *fc)
{
	struct hugetlbfs_fs_context *ctx;

	ctx = kzalloc(sizeof(struct hugetlbfs_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->max_hpages	= -1; /* No limit on size by default */
	ctx->nr_inodes	= -1; /* No limit on number of inodes by default */
	ctx->uid	= current_fsuid();
	ctx->gid	= current_fsgid();
	ctx->mode	= 0755;
	ctx->hstate	= &default_hstate;
	ctx->min_hpages	= -1; /* No default minimum size */
	ctx->max_val_type = NO_SIZE;
	ctx->min_val_type = NO_SIZE;
	fc->fs_private = ctx;
	fc->ops	= &hugetlbfs_fs_context_ops;
	return 0;
}

static struct file_system_type hugetlbfs_fs_type = {
	.name			= "hugetlbfs",
	.init_fs_context	= hugetlbfs_init_fs_context,
	.parameters		= hugetlb_fs_parameters,
	.kill_sb		= kill_litter_super,
	.fs_flags               = FS_ALLOW_IDMAP,
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
	return hstate_index(h);
}

/*
 * Note that size should be aligned to proper hugepage size in caller side,
 * otherwise hugetlb_reserve_pages reserves one less hugepages than intended.
 */
struct file *hugetlb_file_setup(const char *name, size_t size,
				vm_flags_t acctflag, int creat_flags,
				int page_size_log)
{
	struct inode *inode;
	struct vfsmount *mnt;
	int hstate_idx;
	struct file *file;

	hstate_idx = get_hstate_idx(page_size_log);
	if (hstate_idx < 0)
		return ERR_PTR(-ENODEV);

	mnt = hugetlbfs_vfsmount[hstate_idx];
	if (!mnt)
		return ERR_PTR(-ENOENT);

	if (creat_flags == HUGETLB_SHMFS_INODE && !can_do_hugetlb_shm()) {
		struct ucounts *ucounts = current_ucounts();

		if (user_shm_lock(size, ucounts)) {
			pr_warn_once("%s (%d): Using mlock ulimits for SHM_HUGETLB is obsolete\n",
				current->comm, current->pid);
			user_shm_unlock(size, ucounts);
		}
		return ERR_PTR(-EPERM);
	}

	file = ERR_PTR(-ENOSPC);
	/* hugetlbfs_vfsmount[] mounts do not use idmapped mounts.  */
	inode = hugetlbfs_get_inode(mnt->mnt_sb, &nop_mnt_idmap, NULL,
				    S_IFREG | S_IRWXUGO, 0);
	if (!inode)
		goto out;
	if (creat_flags == HUGETLB_SHMFS_INODE)
		inode->i_flags |= S_PRIVATE;

	inode->i_size = size;
	clear_nlink(inode);

	if (!hugetlb_reserve_pages(inode, 0,
			size >> huge_page_shift(hstate_inode(inode)), NULL,
			acctflag))
		file = ERR_PTR(-ENOMEM);
	else
		file = alloc_file_pseudo(inode, mnt, name, O_RDWR,
					&hugetlbfs_file_operations);
	if (!IS_ERR(file))
		return file;

	iput(inode);
out:
	return file;
}

static struct vfsmount *__init mount_one_hugetlbfs(struct hstate *h)
{
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&hugetlbfs_fs_type, SB_KERNMOUNT);
	if (IS_ERR(fc)) {
		mnt = ERR_CAST(fc);
	} else {
		struct hugetlbfs_fs_context *ctx = fc->fs_private;
		ctx->hstate = h;
		mnt = fc_mount(fc);
		put_fs_context(fc);
	}
	if (IS_ERR(mnt))
		pr_err("Cannot mount internal hugetlbfs for page size %luK",
		       huge_page_size(h) / SZ_1K);
	return mnt;
}

static int __init init_hugetlbfs_fs(void)
{
	struct vfsmount *mnt;
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
		goto out;

	error = register_filesystem(&hugetlbfs_fs_type);
	if (error)
		goto out_free;

	/* default hstate mount is required */
	mnt = mount_one_hugetlbfs(&default_hstate);
	if (IS_ERR(mnt)) {
		error = PTR_ERR(mnt);
		goto out_unreg;
	}
	hugetlbfs_vfsmount[default_hstate_idx] = mnt;

	/* other hstates are optional */
	i = 0;
	for_each_hstate(h) {
		if (i == default_hstate_idx) {
			i++;
			continue;
		}

		mnt = mount_one_hugetlbfs(h);
		if (IS_ERR(mnt))
			hugetlbfs_vfsmount[i] = NULL;
		else
			hugetlbfs_vfsmount[i] = mnt;
		i++;
	}

	return 0;

 out_unreg:
	(void)unregister_filesystem(&hugetlbfs_fs_type);
 out_free:
	kmem_cache_destroy(hugetlbfs_inode_cachep);
 out:
	return error;
}
fs_initcall(init_hugetlbfs_fs)
