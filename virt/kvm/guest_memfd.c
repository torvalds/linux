// SPDX-License-Identifier: GPL-2.0
#include <linux/anon_inodes.h>
#include <linux/backing-dev.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/kvm_host.h>
#include <linux/maple_tree.h>
#include <linux/mempolicy.h>
#include <linux/pseudo_fs.h>
#include <linux/pagemap.h>
#include <linux/swap.h>

#include "kvm_mm.h"
#include "guest_memfd.h"

static struct vfsmount *kvm_gmem_mnt;

/*
 * A guest_memfd instance can be associated multiple VMs, each with its own
 * "view" of the underlying physical memory.
 *
 * The gmem's inode is effectively the raw underlying physical storage, and is
 * used to track properties of the physical memory, while each gmem file is
 * effectively a single VM's view of that storage, and is used to track assets
 * specific to its associated VM, e.g. memslots=>gmem bindings.
 */
struct gmem_file {
	struct kvm *kvm;
	struct xarray bindings;
	struct list_head entry;
};

struct gmem_inode {
	struct shared_policy policy;
	struct inode vfs_inode;
	struct list_head gmem_file_list;

	u64 flags;
	/*
	 * Every index in this inode, whether memory is populated or
	 * not, is tracked in attributes. The entire range of indices,
	 * corresponding to the size of this inode, is represented in
	 * this maple tree.
	 */
	struct maple_tree attributes;
};

static __always_inline struct gmem_inode *GMEM_I(struct inode *inode)
{
	return container_of(inode, struct gmem_inode, vfs_inode);
}

#define kvm_gmem_for_each_file(f, inode) \
	list_for_each_entry(f, &GMEM_I(inode)->gmem_file_list, entry)

/**
 * folio_file_pfn - like folio_file_page, but return a pfn.
 * @folio: The folio which contains this index.
 * @index: The index we want to look up.
 *
 * Return: The pfn for this index.
 */
static inline kvm_pfn_t folio_file_pfn(struct folio *folio, pgoff_t index)
{
	return folio_pfn(folio) + (index & (folio_nr_pages(folio) - 1));
}

static pgoff_t kvm_gmem_get_index(struct kvm_memory_slot *slot, gfn_t gfn)
{
	return gfn - slot->base_gfn + slot->gmem.pgoff;
}

static u64 kvm_gmem_get_default_attributes(struct inode *inode)
{
	bool init_shared = GMEM_I(inode)->flags & GUEST_MEMFD_FLAG_INIT_SHARED;

	return init_shared ? 0 : KVM_MEMORY_ATTRIBUTE_PRIVATE;
}

static u64 kvm_gmem_get_attributes(struct inode *inode, void *entry)
{
	if (WARN_ON_ONCE(!entry))
		return kvm_gmem_get_default_attributes(inode);

	return xa_to_value(entry);
}

static bool kvm_gmem_is_private_mem(struct inode *inode, pgoff_t index)
{
	struct maple_tree *mt = &GMEM_I(inode)->attributes;
	void *entry = mtree_load(mt, index);

	return kvm_gmem_get_attributes(inode, entry) &
	       KVM_MEMORY_ATTRIBUTE_PRIVATE;
}

static bool kvm_gmem_is_shared_mem(struct inode *inode, pgoff_t index)
{
	return !kvm_gmem_is_private_mem(inode, index);
}

static bool kvm_gmem_range_has_attributes(struct inode *inode,
					  pgoff_t index, size_t nr_pages,
					  u64 attributes)
{
	struct maple_tree *mt = &GMEM_I(inode)->attributes;
	pgoff_t end = index + nr_pages - 1;
	void *entry;

	lockdep_assert(mt_lock_is_held(mt));

	mt_for_each(mt, entry, index, end) {
		if (kvm_gmem_get_attributes(inode, entry) != attributes)
			return false;
	}

	return true;
}

/*
 * Returns a locked folio on success.  The caller is responsible for
 * setting the up-to-date flag before the memory is mapped into the guest.
 * There is no backing storage for the memory, so the folio will remain
 * up-to-date until it's removed.
 *
 * Ignore accessed, referenced, and dirty flags.  The memory is
 * unevictable and there is no storage to write back to.
 */
static struct folio *kvm_gmem_get_folio(struct inode *inode, pgoff_t index)
{
	/* TODO: Support huge pages. */
	struct mempolicy *policy;
	struct folio *folio;

	/*
	 * Fast-path: See if folio is already present in mapping to avoid
	 * policy_lookup.
	 */
	folio = filemap_lock_folio(inode->i_mapping, index);
	if (!IS_ERR(folio))
		return folio;

	policy = mpol_shared_policy_lookup(&GMEM_I(inode)->policy, index);
	folio = __filemap_get_folio_mpol(inode->i_mapping, index,
					 FGP_LOCK | FGP_CREAT,
					 mapping_gfp_mask(inode->i_mapping), policy);
	mpol_cond_put(policy);

	/*
	 * External interfaces like kvm_gmem_get_pfn() support dealing
	 * with hugepages to a degree, but internally, guest_memfd currently
	 * assumes that all folios are order-0 and handling would need
	 * to be updated for anything otherwise (e.g. page-clearing
	 * operations).
	 */
	WARN_ON_ONCE(!IS_ERR(folio) && folio_order(folio));

	return folio;
}

static enum kvm_gfn_range_filter kvm_gmem_get_all_gfns_filter(struct inode *inode)
{
	if (gmem_in_place_conversion)
		return KVM_FILTER_SHARED | KVM_FILTER_PRIVATE;

	if (GMEM_I(inode)->flags & GUEST_MEMFD_FLAG_INIT_SHARED)
		return KVM_FILTER_SHARED;

	return KVM_FILTER_PRIVATE;
}

static void __kvm_gmem_invalidate_start(struct gmem_file *f, pgoff_t start,
					pgoff_t end,
					enum kvm_gfn_range_filter attr_filter)
{
	bool flush = false, found_memslot = false;
	struct kvm_memory_slot *slot;
	struct kvm *kvm = f->kvm;
	unsigned long index;

	xa_for_each_range(&f->bindings, index, slot, start, end - 1) {
		pgoff_t pgoff = slot->gmem.pgoff;

		struct kvm_gfn_range gfn_range = {
			.start = slot->base_gfn + max(pgoff, start) - pgoff,
			.end = slot->base_gfn + min(pgoff + slot->npages, end) - pgoff,
			.slot = slot,
			.may_block = true,
			.attr_filter = attr_filter,
		};

		if (!found_memslot) {
			found_memslot = true;

			KVM_MMU_LOCK(kvm);
			kvm_mmu_invalidate_start(kvm);
		}

		flush |= kvm_mmu_unmap_gfn_range(kvm, &gfn_range);

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_INVALIDATE
		kvm_arch_gmem_invalidate_range(kvm, &gfn_range);
#endif
	}

	if (flush)
		kvm_flush_remote_tlbs(kvm);

	if (found_memslot)
		KVM_MMU_UNLOCK(kvm);
}

static void kvm_gmem_invalidate_start(struct inode *inode, pgoff_t start,
				      pgoff_t end,
				      enum kvm_gfn_range_filter filter)
{
	struct gmem_file *f;

	kvm_gmem_for_each_file(f, inode)
		__kvm_gmem_invalidate_start(f, start, end, filter);
}

static void __kvm_gmem_invalidate_end(struct gmem_file *f, pgoff_t start,
				      pgoff_t end)
{
	struct kvm *kvm = f->kvm;

	if (xa_find(&f->bindings, &start, end - 1, XA_PRESENT)) {
		KVM_MMU_LOCK(kvm);
		kvm_mmu_invalidate_end(kvm);
		KVM_MMU_UNLOCK(kvm);
	}
}

static void kvm_gmem_invalidate_end(struct inode *inode, pgoff_t start,
				    pgoff_t end)
{
	struct gmem_file *f;

	kvm_gmem_for_each_file(f, inode)
		__kvm_gmem_invalidate_end(f, start, end);
}

static long kvm_gmem_punch_hole(struct inode *inode, loff_t offset, loff_t len)
{
	enum kvm_gfn_range_filter filter = kvm_gmem_get_all_gfns_filter(inode);
	pgoff_t start = offset >> PAGE_SHIFT;
	pgoff_t end = (offset + len) >> PAGE_SHIFT;

	/*
	 * Bindings must be stable across invalidation to ensure the start+end
	 * are balanced.
	 */
	filemap_invalidate_lock(inode->i_mapping);

	kvm_gmem_invalidate_start(inode, start, end, filter);

	truncate_inode_pages_range(inode->i_mapping, offset, offset + len - 1);

	kvm_gmem_invalidate_end(inode, start, end);

	filemap_invalidate_unlock(inode->i_mapping);

	return 0;
}

static long kvm_gmem_allocate(struct inode *inode, loff_t offset, loff_t len)
{
	struct address_space *mapping = inode->i_mapping;
	pgoff_t start, index, end;
	int r;

	/* Dedicated guest is immutable by default. */
	if (offset + len > i_size_read(inode))
		return -EINVAL;

	filemap_invalidate_lock_shared(mapping);

	start = offset >> PAGE_SHIFT;
	end = (offset + len) >> PAGE_SHIFT;

	r = 0;
	for (index = start; index < end; ) {
		struct folio *folio;

		if (signal_pending(current)) {
			r = -EINTR;
			break;
		}

		folio = kvm_gmem_get_folio(inode, index);
		if (IS_ERR(folio)) {
			r = PTR_ERR(folio);
			break;
		}

		index = folio_next_index(folio);

		folio_unlock(folio);
		folio_put(folio);

		/* 64-bit only, wrapping the index should be impossible. */
		if (WARN_ON_ONCE(!index))
			break;

		cond_resched();
	}

	filemap_invalidate_unlock_shared(mapping);

	return r;
}

static long kvm_gmem_fallocate(struct file *file, int mode, loff_t offset,
			       loff_t len)
{
	int ret;

	if (!(mode & FALLOC_FL_KEEP_SIZE))
		return -EOPNOTSUPP;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (!PAGE_ALIGNED(offset) || !PAGE_ALIGNED(len))
		return -EINVAL;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		ret = kvm_gmem_punch_hole(file_inode(file), offset, len);
	else
		ret = kvm_gmem_allocate(file_inode(file), offset, len);

	if (!ret)
		file_modified(file);
	return ret;
}

static int kvm_gmem_release(struct inode *inode, struct file *file)
{
	struct gmem_file *f = file->private_data;
	struct kvm_memory_slot *slot;
	struct kvm *kvm = f->kvm;
	unsigned long index;

	/*
	 * Prevent concurrent attempts to *unbind* a memslot.  This is the last
	 * reference to the file and thus no new bindings can be created, but
	 * dereferencing the slot for existing bindings needs to be protected
	 * against memslot updates, specifically so that unbind doesn't race
	 * and free the memslot (kvm_gmem_get_file() will return NULL).
	 */
	mutex_lock(&kvm->slots_lock);

	filemap_invalidate_lock(inode->i_mapping);

	/*
	 * Note!  synchronize_srcu() is _not_ needed after nullifying memslot
	 * bindings as slot->gmem.file cannot be set back to a non-null value
	 * without the memslot first being deleted.  I.e. this relies on the
	 * synchronize_srcu_expedited() in kvm_swap_active_memslots() to ensure
	 * kvm_gmem_get_pfn() (which runs with kvm->srcu held for read) can't
	 * grab a reference to slot->gmem.file even if the struct file object
	 * is reallocated (for use as a different file).
	 *
	 * file_ref_put() provides a full barrier, and __get_file_rcu() the
	 * matching acquire barrier, to ensure that kvm_gmem_get_file() (via
	 * __get_file_rcu()) sees refcount==0 or fails the "file reloaded"
	 * check (file != NULL due to nullifying the file pointer here).
	 *
	 * Unlike most other users of get_file_rcu(), where callers don't care
	 * if they race with a write, only that they have a reference to _a_
	 * live file, kvm_gmem_get_pfn() needs to get the exact file that is
	 * associated with the memslot.  Without the aforementioned SRCU
	 * synchronization, the following could happen:
	 *
	 *  CPU0				CPU1
	 *  kvm_gmem_get_pfn()
	 *    slot->gmem.file == A
	 *					kvm_gmem_release())
	 *					  slot->gmem.file = NULL
	 *
	 *					kvm_set_memory_region()
	 *					  slot deleted
	 *
	 *					kvm_set_memory_region()
	 *					  slot created
	 *					  slot->gmem.file = B (alloc the same object)
	 *
	 *  get_file_active()
	 *    file = B
	 *    file_reloaded = B
	 *
	 * <KVM does weird things with an old memslot + new file>
	 *
	 * Obviously KVM would be broken in many places if the synchronization
	 * were omitted, but it's important to note that get_file_active() does
	 * NOT guarantee a reference to the "original" file was obtained, only
	 * that it grabbed a reference for the returned file, i.e. didn't grab
	 * a reference for file A, but then returned a pointer to file B.
	 */
	xa_for_each(&f->bindings, index, slot)
		WRITE_ONCE(slot->gmem.file, NULL);

	/*
	 * All in-flight operations are gone and new bindings can be created.
	 * Zap all SPTEs pointed at by this file.  Do not free the backing
	 * memory, as its lifetime is associated with the inode, not the file.
	 */
	__kvm_gmem_invalidate_start(f, 0, -1ul,
				    kvm_gmem_get_all_gfns_filter(inode));
	__kvm_gmem_invalidate_end(f, 0, -1ul);

	list_del(&f->entry);

	filemap_invalidate_unlock(inode->i_mapping);

	mutex_unlock(&kvm->slots_lock);

	xa_destroy(&f->bindings);
	kfree(f);

	kvm_put_kvm(kvm);

	return 0;
}

static inline struct file *kvm_gmem_get_file(struct kvm_memory_slot *slot)
{
	/*
	 * Do not return slot->gmem.file if it has already been closed;
	 * there might be some time between the last fput() and when
	 * kvm_gmem_release() clears slot->gmem.file.
	 */
	return get_file_active(&slot->gmem.file);
}

DEFINE_CLASS(gmem_get_file, struct file *, if (_T) fput(_T),
	     kvm_gmem_get_file(slot), struct kvm_memory_slot *slot);

static bool kvm_gmem_supports_mmap(struct inode *inode)
{
	return GMEM_I(inode)->flags & GUEST_MEMFD_FLAG_MMAP;
}

static vm_fault_t kvm_gmem_fault_user_mapping(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct folio *folio;
	vm_fault_t ret = VM_FAULT_LOCKED;

	if (((loff_t)vmf->pgoff << PAGE_SHIFT) >= i_size_read(inode))
		return VM_FAULT_SIGBUS;

	filemap_invalidate_lock_shared(inode->i_mapping);
	if (kvm_gmem_is_shared_mem(inode, vmf->pgoff))
		folio = kvm_gmem_get_folio(inode, vmf->pgoff);
	else
		folio = ERR_PTR(-EACCES);
	filemap_invalidate_unlock_shared(inode->i_mapping);

	if (IS_ERR(folio)) {
		if (PTR_ERR(folio) == -EAGAIN)
			return VM_FAULT_RETRY;

		return vmf_error(PTR_ERR(folio));
	}

	if (WARN_ON_ONCE(folio_test_large(folio))) {
		ret = VM_FAULT_SIGBUS;
		goto out_folio;
	}

	if (!folio_test_uptodate(folio)) {
		clear_highpage(folio_page(folio, 0));
		folio_mark_uptodate(folio);
	}

	vmf->page = folio_file_page(folio, vmf->pgoff);

out_folio:
	if (ret != VM_FAULT_LOCKED) {
		folio_unlock(folio);
		folio_put(folio);
	}

	return ret;
}

#ifdef CONFIG_NUMA
static int kvm_gmem_set_policy(struct vm_area_struct *vma, struct mempolicy *mpol)
{
	struct inode *inode = file_inode(vma->vm_file);

	return mpol_set_shared_policy(&GMEM_I(inode)->policy, vma, mpol);
}

static struct mempolicy *kvm_gmem_get_policy(struct vm_area_struct *vma,
					     unsigned long addr, pgoff_t *ilx)
{
	pgoff_t pgoff = linear_page_index(vma, addr);
	struct inode *inode = file_inode(vma->vm_file);

	*ilx = inode->i_ino;

	/*
	 * Return the memory policy for this index, or NULL if none is set.
	 *
	 * Returning NULL, e.g. instead of the current task's memory policy, is
	 * important for the .get_policy kernel ABI: it indicates that no
	 * explicit policy has been set via mbind() for this memory. The caller
	 * can then replace NULL with the default memory policy instead of the
	 * current task's memory policy.
	 */
	return mpol_shared_policy_lookup(&GMEM_I(inode)->policy, pgoff);
}
#endif /* CONFIG_NUMA */

static const struct vm_operations_struct kvm_gmem_vm_ops = {
	.fault		= kvm_gmem_fault_user_mapping,
#ifdef CONFIG_NUMA
	.get_policy	= kvm_gmem_get_policy,
	.set_policy	= kvm_gmem_set_policy,
#endif
};

static int kvm_gmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!kvm_gmem_supports_mmap(file_inode(file)))
		return -ENODEV;

	if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) !=
	    (VM_SHARED | VM_MAYSHARE)) {
		return -EINVAL;
	}

	vma->vm_ops = &kvm_gmem_vm_ops;

	return 0;
}

bool kvm_gmem_is_private_gfn(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot = gfn_to_memslot(kvm, gfn);
	struct inode *inode;

	if (!slot)
		return 0;

	CLASS(gmem_get_file, file)(slot);
	if (!file)
		return 0;

	inode = file_inode(file);

	/*
	 * Rely on the maple tree's internal RCU lock to ensure a stable result.
	 * This result can become stale as soon as the lock is dropped, so the
	 * caller _must_ protect consumption of private vs. shared either by
	 * holding guest_memfd's invalidate lock for the entire duration, or by
	 * checking mmu_invalidate_retry_gfn() under mmu_lock to serialize
	 * against concurrent attribute updates.
	 */
	return kvm_gmem_is_private_mem(inode, kvm_gmem_get_index(slot, gfn));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_gmem_is_private_gfn);

/*
 * Preallocate memory for attributes to be stored on a maple tree, pointed to
 * by mas.  Adjacent ranges with attributes identical to the new attributes
 * will be merged.  Also sets mas's bounds up for storing attributes.
 *
 * This maintains the invariant that ranges with the same attributes will
 * always be merged.
 */
static int kvm_gmem_mas_preallocate(struct ma_state *mas, u64 attributes,
				    pgoff_t start, size_t nr_pages)
{
	pgoff_t end = start + nr_pages;
	pgoff_t last = end - 1;
	void *entry;

	/* Try extending range. entry is NULL on overflow/wrap-around. */
	mas_set(mas, end);
	entry = mas_find(mas, end);
	if (entry && xa_to_value(entry) == attributes)
		last = mas->last;

	if (start > 0) {
		mas_set(mas, start - 1);
		entry = mas_find(mas, start - 1);
		if (entry && xa_to_value(entry) == attributes)
			start = mas->index;
	}

	mas_set_range(mas, start, last);
	return mas_preallocate(mas, xa_mk_value(attributes), GFP_KERNEL);
}

static bool __folio_has_outstanding_references(struct folio *folio,
					       enum lru_cache_drained *drained)
{
	if (folio_maybe_dma_pinned(folio) || folio_mapped(folio))
		return true;

	/* 1 reference held by filemap_get_folios() in the folio batch. */
	lru_cache_drain_for_folio(folio, 1, drained);

	/*
	 * Outstanding references are anything other than those from the page
	 * cache, plus 1 temporary reference held by filemap_get_folios() in the
	 * folio batch.
	 */
	return folio_ref_count(folio) != folio_nr_pages(folio) + 1;
}

static bool kvm_gmem_has_outstanding_references(struct inode *inode,
						pgoff_t start, size_t nr_pages,
						pgoff_t *err_index)
{
	enum lru_cache_drained drained = LRU_CACHE_NOT_DRAINED;
	struct address_space *mapping = inode->i_mapping;
	pgoff_t last = start + nr_pages - 1;
	struct folio_batch fbatch;
	pgoff_t next;
	int i;

	folio_batch_init(&fbatch);

	next = start;
	while (filemap_get_folios(mapping, &next, last, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); ++i) {
			struct folio *folio = fbatch.folios[i];

			if (__folio_has_outstanding_references(folio, &drained)) {
				*err_index = max(start, folio->index);
				folio_batch_release(&fbatch);
				return true;
			}
		}

		folio_batch_release(&fbatch);
		cond_resched();
	}

	return false;
}

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_CONVERT
static void kvm_gmem_make_shared(struct inode *inode, pgoff_t start, pgoff_t end)
{
	struct folio_batch fbatch;
	pgoff_t next = start;
	int i;

	folio_batch_init(&fbatch);
	while (filemap_get_folios(inode->i_mapping, &next, end - 1, &fbatch)) {
		for (i = 0; i < folio_batch_count(&fbatch); ++i) {
			struct folio *folio = fbatch.folios[i];
			pgoff_t start_index, end_index;
			kvm_pfn_t start_pfn;
			kvm_pfn_t nr_pages;

			start_index = max(start, folio->index);
			end_index = min(end, folio_next_index(folio));
			/*
			 * end_index is either in folio or points to
			 * the first page of the next folio. Hence,
			 * all pages in range [start_index, end_index)
			 * are contiguous.
			 */
			start_pfn = folio_file_pfn(folio, start_index);
			nr_pages = end_index - start_index;

			kvm_arch_gmem_make_shared(start_pfn, nr_pages);
		}

		folio_batch_release(&fbatch);
		cond_resched();
	}
}
#else
static void kvm_gmem_make_shared(struct inode *inode, pgoff_t start, pgoff_t end) {}
#endif

static int __kvm_gmem_set_attributes(struct inode *inode, pgoff_t start,
				     size_t nr_pages, uint64_t attrs,
				     pgoff_t *err_index)
{
	bool to_private = attrs & KVM_MEMORY_ATTRIBUTE_PRIVATE;
	struct address_space *mapping = inode->i_mapping;
	struct gmem_inode *gi = GMEM_I(inode);
	enum kvm_gfn_range_filter filter;
	pgoff_t end = start + nr_pages;
	struct maple_tree *mt;
	struct ma_state mas;
	int r = 0;

	mt = &gi->attributes;

	filemap_invalidate_lock(mapping);

	if (kvm_gmem_range_has_attributes(inode, start, nr_pages, attrs))
		goto out;

	mas_init(&mas, mt, start);
	r = kvm_gmem_mas_preallocate(&mas, attrs, start, nr_pages);
	if (r) {
		*err_index = start;
		goto out;
	}

	if (to_private) {
		/*
		 * Forcefully unmap the pages from all userspace page tables,
		 * and then verify there are no outstanding references, e.g.
		 * acquired via GUP or similar.  Tell userspace to try again if
		 * there are outstanding references and hope that whatever has
		 * pinned the page will put its reference "soon".
		 */
		unmap_mapping_pages(mapping, start, nr_pages, false);

		if (kvm_gmem_has_outstanding_references(inode, start, nr_pages,
							err_index)) {
			mas_destroy(&mas);
			r = -EAGAIN;
			goto out;
		}
	}

	/*
	 * From this point on guest_memfd has performed necessary
	 * checks and can proceed to do guest-breaking changes.
	 */

	filter = to_private ? KVM_FILTER_SHARED : KVM_FILTER_PRIVATE;
	kvm_gmem_invalidate_start(inode, start, end, filter);

	if (!to_private && kvm_arch_has_gmem_convert())
		kvm_gmem_make_shared(inode, start, end);

	mas_store_prealloc(&mas, xa_mk_value(attrs));

	kvm_gmem_invalidate_end(inode, start, end);
out:
	filemap_invalidate_unlock(mapping);
	return r;
}

static long kvm_gmem_set_attributes(struct file *file, void __user *argp)
{
	struct gmem_file *f = file->private_data;
	struct inode *inode = file_inode(file);
	struct kvm_memory_attributes2 attrs;
	pgoff_t err_index;
	size_t nr_pages;
	pgoff_t index;
	int i, r;

	if (copy_from_user(&attrs, argp, sizeof(attrs)))
		return -EFAULT;

	if (attrs.flags)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(attrs.reserved); i++) {
		if (attrs.reserved[i])
			return -EINVAL;
	}
	if (!kvm_arch_has_private_mem(f->kvm))
		return -EINVAL;
	if (attrs.attributes & ~KVM_MEMORY_ATTRIBUTE_PRIVATE)
		return -EINVAL;
	if (attrs.size == 0 || attrs.offset + attrs.size < attrs.offset)
		return -EINVAL;
	if (!PAGE_ALIGNED(attrs.offset) || !PAGE_ALIGNED(attrs.size))
		return -EINVAL;

	if (attrs.offset >= i_size_read(inode) ||
	    attrs.offset + attrs.size > i_size_read(inode))
		return -EINVAL;

	nr_pages = attrs.size >> PAGE_SHIFT;
	index = attrs.offset >> PAGE_SHIFT;
	r = __kvm_gmem_set_attributes(inode, index, nr_pages, attrs.attributes,
				      &err_index);
	if (r) {
		attrs.error_offset = ((uint64_t)err_index) << PAGE_SHIFT;

		if (copy_to_user(argp, &attrs, sizeof(attrs)))
			return -EFAULT;
	}

	return r;
}

static long kvm_gmem_ioctl(struct file *file, unsigned int ioctl,
			   unsigned long arg)
{
	switch (ioctl) {
	case KVM_SET_MEMORY_ATTRIBUTES2:
		if (!gmem_in_place_conversion)
			return -ENOTTY;

		return kvm_gmem_set_attributes(file, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}

static struct file_operations kvm_gmem_fops = {
	.mmap		= kvm_gmem_mmap,
	.open		= generic_file_open,
	.release	= kvm_gmem_release,
	.fallocate	= kvm_gmem_fallocate,
	.unlocked_ioctl	= kvm_gmem_ioctl,
};

static int kvm_gmem_migrate_folio(struct address_space *mapping,
				  struct folio *dst, struct folio *src,
				  enum migrate_mode mode)
{
	WARN_ON_ONCE(1);
	return -EINVAL;
}

static int kvm_gmem_error_folio(struct address_space *mapping, struct folio *folio)
{
	struct inode *inode = mapping->host;
	enum kvm_gfn_range_filter filter;
	pgoff_t start, end;

	/*
	 * memory_failure() invokes ->error_remove_folio() while holding the
	 * poisoned folio lock.  Do not block on mapping->invalidate_lock, as
	 * a writer that already holds the invalidate lock can be waiting for
	 * the same folio, e.g. truncation from MADV_REMOVE/FALLOC_FL_PUNCH_HOLE.
	 *
	 * If the invalidate lock is contended, fail the recovery instead of
	 * risking a deadlock in the memory-failure path.
	 */
	if (!filemap_invalidate_trylock_shared(mapping))
		return -EBUSY;

	start = folio->index;
	end = start + folio_nr_pages(folio);

	filter = kvm_gmem_get_all_gfns_filter(inode);
	kvm_gmem_invalidate_start(inode, start, end, filter);

	/*
	 * Do not truncate the range, what action is taken in response to the
	 * error is userspace's decision (assuming the architecture supports
	 * gracefully handling memory errors).  If/when the guest attempts to
	 * access a poisoned page, kvm_gmem_get_pfn() will return -EHWPOISON,
	 * at which point KVM can either terminate the VM or propagate the
	 * error to userspace.
	 */

	kvm_gmem_invalidate_end(mapping->host, start, end);

	filemap_invalidate_unlock_shared(mapping);

	return MF_DELAYED;
}

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_RECLAIM
static void kvm_gmem_free_folio(struct folio *folio)
{
	kvm_arch_gmem_reclaim(folio_file_pfn(folio, 0), folio_nr_pages(folio));
}
#endif

static const struct address_space_operations kvm_gmem_aops = {
	.dirty_folio = noop_dirty_folio,
	.migrate_folio	= kvm_gmem_migrate_folio,
	.error_remove_folio = kvm_gmem_error_folio,
#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_RECLAIM
	.free_folio = kvm_gmem_free_folio,
#endif
};

static int kvm_gmem_setattr(const struct mnt_idmap *idmap, struct dentry *dentry,
			    struct iattr *attr)
{
	return -EINVAL;
}
static const struct inode_operations kvm_gmem_iops = {
	.setattr	= kvm_gmem_setattr,
};

bool __weak kvm_arch_supports_gmem_init_shared(struct kvm *kvm)
{
	return true;
}

static int kvm_gmem_init_inode(struct inode *inode, loff_t size, u64 flags)
{
	struct gmem_inode *gi = GMEM_I(inode);
	MA_STATE(mas, &gi->attributes, 0, (size >> PAGE_SHIFT) - 1);
	u64 attrs;
	int r;

	inode->i_op = &kvm_gmem_iops;
	inode->i_mapping->a_ops = &kvm_gmem_aops;
	inode->i_mode |= S_IFREG;
	inode->i_size = size;
	mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);

	/*
	 * guest_memfd memory is neither migratable nor swappable: set
	 * inaccessible to gate off both.
	 */
	mapping_set_inaccessible(inode->i_mapping);
	WARN_ON_ONCE(!mapping_unevictable(inode->i_mapping));

	gi->flags = flags;

	mt_set_external_lock(&gi->attributes,
			     &inode->i_mapping->invalidate_lock);

	/*
	 * Store default attributes for the entire gmem instance. Ensuring every
	 * index is represented in the maple tree at all times simplifies the
	 * conversion and merging logic.
	 */
	attrs = kvm_gmem_get_default_attributes(inode);

	/*
	 * Acquire the invalidation lock purely to make lockdep happy.  The
	 * maple tree library expects all stores to be protected via the lock,
	 * and the library can't know when the tree is reachable only by the
	 * caller, as is the case here.
	 */
	filemap_invalidate_lock(inode->i_mapping);
	r = mas_store_gfp(&mas, xa_mk_value(attrs), GFP_KERNEL);
	filemap_invalidate_unlock(inode->i_mapping);

	return r;
}

static int __kvm_gmem_create(struct kvm *kvm, loff_t size, u64 flags)
{
	static const char *name = "[kvm-gmem]";
	struct gmem_file *f;
	struct inode *inode;
	struct file *file;
	int fd, err;

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;

	f = kzalloc_obj(*f);
	if (!f) {
		err = -ENOMEM;
		goto err_fd;
	}

	/* __fput() will take care of fops_put(). */
	if (!fops_get(&kvm_gmem_fops)) {
		err = -ENOENT;
		goto err_gmem;
	}

	inode = anon_inode_make_secure_inode(kvm_gmem_mnt->mnt_sb, name, NULL);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto err_fops;
	}

	err = kvm_gmem_init_inode(inode, size, flags);
	if (err)
		goto err_inode;

	file = alloc_file_pseudo(inode, kvm_gmem_mnt, name, O_RDWR, &kvm_gmem_fops);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		goto err_inode;
	}

	file->f_flags |= O_LARGEFILE;
	file->private_data = f;

	kvm_get_kvm(kvm);
	f->kvm = kvm;
	xa_init(&f->bindings);
	list_add(&f->entry, &GMEM_I(inode)->gmem_file_list);

	fd_install(fd, file);
	return fd;

err_inode:
	iput(inode);
err_fops:
	fops_put(&kvm_gmem_fops);
err_gmem:
	kfree(f);
err_fd:
	put_unused_fd(fd);
	return err;
}

int kvm_gmem_create(struct kvm *kvm, struct kvm_create_guest_memfd *args)
{
	loff_t size = args->size;
	u64 flags = args->flags;

	if (flags & ~kvm_gmem_get_supported_flags(kvm))
		return -EINVAL;

	if (size <= 0 || !PAGE_ALIGNED(size))
		return -EINVAL;

	return __kvm_gmem_create(kvm, size, flags);
}

int kvm_gmem_prepare_memory_region(struct kvm *kvm, struct kvm_memory_slot *slot,
				   unsigned int fd, uoff_t offset)
{
	uoff_t size = slot->npages << PAGE_SHIFT;
	struct gmem_file *f;
	struct inode *inode;
	struct file *file;


	BUILD_BUG_ON(sizeof(gpa_t) != sizeof(offset));
	BUILD_BUG_ON(sizeof(gfn_t) != sizeof(slot->gmem.pgoff));

	if (WARN_ON_ONCE(slot->flags & KVM_MEMSLOT_GMEM_ONLY))
		return -EINVAL;

	file = fget(fd);
	if (!file)
		return -EBADF;

	if (file->f_op != &kvm_gmem_fops)
		goto err;

	f = file->private_data;
	if (f->kvm != kvm)
		goto err;

	inode = file_inode(file);

	if (!PAGE_ALIGNED(offset) || offset + size > i_size_read(inode))
		goto err;

	/*
	 * memslots of flag KVM_MEM_GUEST_MEMFD are immutable to change, so
	 * kvm_gmem_bind() must occur on a new memslot.  Because the memslot
	 * is not visible yet, kvm_gmem_get_pfn() is guaranteed to see the file.
	 */
	slot->gmem.file = file;
	slot->gmem.pgoff = offset >> PAGE_SHIFT;
	if (gmem_in_place_conversion || kvm_gmem_supports_mmap(inode))
		slot->flags |= KVM_MEMSLOT_GMEM_ONLY;

	/*
	 * Gift the caller a reference to the file.  The reference will be
	 * dropped after bindings are established, or if installing the new
	 * memslot ultimately fails.
	 */
	return 0;

err:
	fput(file);
	return -EINVAL;
}

int kvm_gmem_commit_memory_region(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	struct gmem_file *f = slot->gmem.file->private_data;
	struct inode *inode = file_inode(slot->gmem.file);
	unsigned long start, end;
	int r;

	if (WARN_ON_ONCE(slot->gmem.file->f_op != &kvm_gmem_fops))
		return -EIO;

	filemap_invalidate_lock(inode->i_mapping);

	start = slot->gmem.pgoff;
	end = start + slot->npages;

	if (!xa_empty(&f->bindings) &&
	    xa_find(&f->bindings, &start, end - 1, XA_PRESENT)) {
		filemap_invalidate_unlock(inode->i_mapping);
		return -EEXIST;
	}

	r = xa_err(xa_store_range(&f->bindings, start, end - 1, slot, GFP_KERNEL));
	if (r)
		xa_store_range(&f->bindings, start, end - 1, NULL, GFP_KERNEL);

	filemap_invalidate_unlock(inode->i_mapping);

	return r;
}

void kvm_gmem_unbind(struct kvm_memory_slot *slot)
{
	struct file *file = slot->gmem.file;
	unsigned long start = slot->gmem.pgoff;
	unsigned long end = start + slot->npages;
	struct gmem_file *f;

	/*
	 * Nothing to do if the underlying file was _already_ closed, as
	 * kvm_gmem_release() invalidates and nullifies all bindings.
	 */
	if (!file)
		return;

	/*
	 * However, if the file is _being_ closed, then the bindings need to be
	 * removed as kvm_gmem_release() might not run until after the memslot
	 * is freed.  Modifying the bindings is safe even if the file is dying
	 * as kvm_gmem_release() nullifies slot->gmem.file under slots_lock,
	 * and only puts its reference to KVM after destroying all bindings.
	 * I.e. reaching this point means kvm_gmem_release() hasn't destroyed
	 * the bindings or freed the gmem_file and can't do so until the caller
	 * drops slots_lock, so there's no need to verify the file is live.
	 */
	f = file->private_data;

	filemap_invalidate_lock(file->f_mapping);
	xa_store_range(&f->bindings, start, end - 1, NULL, GFP_KERNEL);

	/*
	 * Note, the caller is responsible for ensuring the slot is unreachable
	 * before unbinding, e.g. by synchronizing SRCU after deleting the slot,
	 * to guarantee kvm_gmem_get_pfn() can't see the slot+file.
	 */
	WRITE_ONCE(slot->gmem.file, NULL);
	filemap_invalidate_unlock(file->f_mapping);
}

/* Returns a locked folio on success.  */
static struct folio *__kvm_gmem_get_pfn(struct file *file,
					struct kvm_memory_slot *slot,
					pgoff_t index, kvm_pfn_t *pfn,
					int *max_order)
{
	struct file *slot_file = READ_ONCE(slot->gmem.file);
	struct gmem_file *f = file->private_data;
	struct folio *folio;

	if (file != slot_file) {
		WARN_ON_ONCE(slot_file);
		return ERR_PTR(-EFAULT);
	}

	if (xa_load(&f->bindings, index) != slot) {
		WARN_ON_ONCE(xa_load(&f->bindings, index));
		return ERR_PTR(-EIO);
	}

	folio = kvm_gmem_get_folio(file_inode(file), index);
	if (IS_ERR(folio))
		return folio;

	if (folio_test_hwpoison(folio)) {
		folio_unlock(folio);
		folio_put(folio);
		return ERR_PTR(-EHWPOISON);
	}

	if (!folio_test_uptodate(folio)) {
		clear_highpage(folio_page(folio, 0));
		folio_mark_uptodate(folio);
	}

	*pfn = folio_file_pfn(folio, index);
	if (max_order)
		*max_order = 0;

	return folio;
}

int kvm_gmem_get_pfn(struct kvm *kvm, struct kvm_memory_slot *slot,
		     gfn_t gfn, kvm_pfn_t *pfn, int *max_order)
{
	pgoff_t index = kvm_gmem_get_index(slot, gfn);
	struct folio *folio;
	int r = 0, __order;

	max_order = max_order ?: &__order;

	CLASS(gmem_get_file, file)(slot);
	if (!file)
		return -EFAULT;

	filemap_invalidate_lock_shared(file_inode(file)->i_mapping);

	folio = __kvm_gmem_get_pfn(file, slot, index, pfn, max_order);
	if (IS_ERR(folio)) {
		r = PTR_ERR(folio);
		goto out;
	}

	if (kvm_arch_has_gmem_convert() &&
	    kvm_gmem_is_private_mem(file_inode(file), index))
		r = kvm_arch_gmem_make_private(kvm, gfn, *pfn,
					       (kvm_pfn_t)1 << *max_order);

	folio_unlock(folio);
	folio_put(folio);

out:
	filemap_invalidate_unlock_shared(file_inode(file)->i_mapping);
	return r;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_gmem_get_pfn);

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_POPULATE

static long __kvm_gmem_populate(struct kvm *kvm, struct kvm_memory_slot *slot,
				struct file *file, gfn_t gfn, struct page *src_page,
				kvm_gmem_populate_cb post_populate, void *opaque)
{
	pgoff_t index = kvm_gmem_get_index(slot, gfn);
	struct folio *folio;
	kvm_pfn_t pfn;
	int ret;

	filemap_invalidate_lock(file->f_mapping);

	folio = __kvm_gmem_get_pfn(file, slot, index, &pfn, NULL);
	if (IS_ERR(folio)) {
		ret = PTR_ERR(folio);
		goto out_unlock;
	}

	folio_unlock(folio);

	if (!kvm_is_private_gfn(kvm, gfn)) {
		ret = -EINVAL;
		goto out_put_folio;
	}

	ret = post_populate(kvm, gfn, pfn, src_page, opaque);

out_put_folio:
	folio_put(folio);
out_unlock:
	filemap_invalidate_unlock(file->f_mapping);
	return ret;
}

long kvm_gmem_populate(struct kvm *kvm, gfn_t start_gfn, void __user *src,
		       long npages, bool may_writeback_src,
		       kvm_gmem_populate_cb post_populate, void *opaque)
{
	struct kvm_memory_slot *slot;
	int ret = 0;
	long i;

	lockdep_assert_held(&kvm->slots_lock);

	if (WARN_ON_ONCE(npages <= 0))
		return -EINVAL;

	if (WARN_ON_ONCE(!PAGE_ALIGNED(src)))
		return -EINVAL;

	slot = gfn_to_memslot(kvm, start_gfn);
	if (!kvm_slot_has_gmem(slot))
		return -EINVAL;

	CLASS(gmem_get_file, file)(slot);
	if (!file)
		return -EFAULT;

	npages = min_t(ulong, slot->npages - (start_gfn - slot->base_gfn), npages);
	for (i = 0; i < npages; i++) {
		struct page *src_page = NULL;

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (src) {
			unsigned long uaddr = (unsigned long)src + i * PAGE_SIZE;
			unsigned int flags = may_writeback_src ? FOLL_WRITE : 0;

			ret = get_user_pages_fast(uaddr, 1, flags, &src_page);
			if (ret < 0)
				break;
			if (ret != 1) {
				ret = -ENOMEM;
				break;
			}
		}

		ret = __kvm_gmem_populate(kvm, slot, file, start_gfn + i, src_page,
					  post_populate, opaque);

		if (src_page)
			put_page(src_page);

		if (ret)
			break;
	}

	return ret && !i ? ret : i;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_gmem_populate);
#endif

static struct kmem_cache *kvm_gmem_inode_cachep;

static void kvm_gmem_init_inode_once(void *__gi)
{
	struct gmem_inode *gi = __gi;

	/*
	 * Note!  Don't initialize the inode with anything specific to the
	 * guest_memfd instance, or that might be specific to how the inode is
	 * used (from the VFS-layer's perspective).  This hook is called only
	 * during the initial slab allocation, i.e. only fields/state that are
	 * idempotent across _all_ use of the inode _object_ can be initialized
	 * at this time!
	 */
	inode_init_once(&gi->vfs_inode);
}

static struct inode *kvm_gmem_alloc_inode(struct super_block *sb)
{
	struct gmem_inode *gi;

	gi = alloc_inode_sb(sb, kvm_gmem_inode_cachep, GFP_KERNEL);
	if (!gi)
		return NULL;

	mpol_shared_policy_init(&gi->policy, NULL);

	/*
	 * Memory attributes are protected by the filemap invalidation lock, but
	 * the lock structure isn't available at this time.  Immediately mark
	 * maple tree as using external locking so that accessing the tree
	 * before it's fully initialized results in NULL pointer dereferences
	 * and not more subtle bugs.
	 */
	mt_init_flags(&gi->attributes, MT_FLAGS_LOCK_EXTERN | MT_FLAGS_USE_RCU);

	gi->flags = 0;
	INIT_LIST_HEAD(&gi->gmem_file_list);
	return &gi->vfs_inode;
}

static void kvm_gmem_destroy_inode(struct inode *inode)
{
	struct gmem_inode *gi = GMEM_I(inode);

	mpol_free_shared_policy(&gi->policy);

	/*
	 * Note!  Checking for an empty tree is functionally necessary
	 * to avoid explosions if the tree hasn't been fully
	 * initialized, i.e. if the inode is being destroyed before
	 * guest_memfd can set the external lock, lockdep would find
	 * that the tree's internal ma_lock was not held.
	 */
	if (!mtree_empty(&gi->attributes)) {
		/*
		 * Acquire the invalidation lock purely to make lockdep happy,
		 * the inode is unreachable at this point.
		 */
		filemap_invalidate_lock(inode->i_mapping);
		__mt_destroy(&gi->attributes);
		filemap_invalidate_unlock(inode->i_mapping);
	}
}

static void kvm_gmem_free_inode(struct inode *inode)
{
	kmem_cache_free(kvm_gmem_inode_cachep, GMEM_I(inode));
}

static const struct super_operations kvm_gmem_super_operations = {
	.statfs		= simple_statfs,
	.alloc_inode	= kvm_gmem_alloc_inode,
	.destroy_inode	= kvm_gmem_destroy_inode,
	.free_inode	= kvm_gmem_free_inode,
};

static int kvm_gmem_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx;

	if (!init_pseudo(fc, GUEST_MEMFD_MAGIC))
		return -ENOMEM;

	ctx = fc->fs_private;
	ctx->ops = &kvm_gmem_super_operations;

	return 0;
}

static struct file_system_type kvm_gmem_fs = {
	.name		 = "guest_memfd",
	.init_fs_context = kvm_gmem_init_fs_context,
	.kill_sb	 = kill_anon_super,
};

static int kvm_gmem_init_mount(void)
{
	kvm_gmem_mnt = kern_mount(&kvm_gmem_fs);

	if (IS_ERR(kvm_gmem_mnt))
		return PTR_ERR(kvm_gmem_mnt);

	kvm_gmem_mnt->mnt_flags |= MNT_NOEXEC;
	return 0;
}

int kvm_gmem_init(struct module *module)
{
	struct kmem_cache_args args = {
		.align = 0,
		.ctor = kvm_gmem_init_inode_once,
	};
	int ret;

	kvm_gmem_fops.owner = module;
	kvm_gmem_inode_cachep = kmem_cache_create("kvm_gmem_inode_cache",
						  sizeof(struct gmem_inode),
						  &args, SLAB_ACCOUNT);
	if (!kvm_gmem_inode_cachep)
		return -ENOMEM;

	ret = kvm_gmem_init_mount();
	if (ret) {
		kmem_cache_destroy(kvm_gmem_inode_cachep);
		return ret;
	}
	return 0;
}

void kvm_gmem_exit(void)
{
	kern_unmount(kvm_gmem_mnt);
	kvm_gmem_mnt = NULL;
	rcu_barrier();
	kmem_cache_destroy(kvm_gmem_inode_cachep);
}
