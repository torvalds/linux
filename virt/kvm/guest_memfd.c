// SPDX-License-Identifier: GPL-2.0
#include <linux/anon_inodes.h>
#include <linux/backing-dev.h>
#include <linux/falloc.h>
#include <linux/fs.h>
#include <linux/kvm_host.h>
#include <linux/mempolicy.h>
#include <linux/pseudo_fs.h>
#include <linux/pagemap.h>

#include "kvm_mm.h"

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

	u64 flags;
};

static __always_inline struct gmem_inode *GMEM_I(struct inode *inode)
{
	return container_of(inode, struct gmem_inode, vfs_inode);
}

#define kvm_gmem_for_each_file(f, mapping) \
	list_for_each_entry(f, &(mapping)->i_private_list, entry)

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

static int __kvm_gmem_prepare_folio(struct kvm *kvm, struct kvm_memory_slot *slot,
				    pgoff_t index, struct folio *folio)
{
#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_PREPARE
	kvm_pfn_t pfn = folio_file_pfn(folio, index);
	gfn_t gfn = slot->base_gfn + index - slot->gmem.pgoff;
	int rc = kvm_arch_gmem_prepare(kvm, gfn, pfn, folio_order(folio));
	if (rc) {
		pr_warn_ratelimited("gmem: Failed to prepare folio for index %lx GFN %llx PFN %llx error %d.\n",
				    index, gfn, pfn, rc);
		return rc;
	}
#endif

	return 0;
}

static inline void kvm_gmem_mark_prepared(struct folio *folio)
{
	folio_mark_uptodate(folio);
}

/*
 * Process @folio, which contains @gfn, so that the guest can use it.
 * The folio must be locked and the gfn must be contained in @slot.
 * On successful return the guest sees a zero page so as to avoid
 * leaking host data and the up-to-date flag is set.
 */
static int kvm_gmem_prepare_folio(struct kvm *kvm, struct kvm_memory_slot *slot,
				  gfn_t gfn, struct folio *folio)
{
	unsigned long nr_pages, i;
	pgoff_t index;
	int r;

	nr_pages = folio_nr_pages(folio);
	for (i = 0; i < nr_pages; i++)
		clear_highpage(folio_page(folio, i));

	/*
	 * Preparing huge folios should always be safe, since it should
	 * be possible to split them later if needed.
	 *
	 * Right now the folio order is always going to be zero, but the
	 * code is ready for huge folios.  The only assumption is that
	 * the base pgoff of memslots is naturally aligned with the
	 * requested page order, ensuring that huge folios can also use
	 * huge page table entries for GPA->HPA mapping.
	 *
	 * The order will be passed when creating the guest_memfd, and
	 * checked when creating memslots.
	 */
	WARN_ON(!IS_ALIGNED(slot->gmem.pgoff, folio_nr_pages(folio)));
	index = kvm_gmem_get_index(slot, gfn);
	index = ALIGN_DOWN(index, folio_nr_pages(folio));
	r = __kvm_gmem_prepare_folio(kvm, slot, index, folio);
	if (!r)
		kvm_gmem_mark_prepared(folio);

	return r;
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
	folio = __filemap_get_folio(inode->i_mapping, index,
				    FGP_LOCK | FGP_ACCESSED, 0);
	if (!IS_ERR(folio))
		return folio;

	policy = mpol_shared_policy_lookup(&GMEM_I(inode)->policy, index);
	folio = __filemap_get_folio_mpol(inode->i_mapping, index,
					 FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
					 mapping_gfp_mask(inode->i_mapping), policy);
	mpol_cond_put(policy);

	return folio;
}

static enum kvm_gfn_range_filter kvm_gmem_get_invalidate_filter(struct inode *inode)
{
	if (GMEM_I(inode)->flags & GUEST_MEMFD_FLAG_INIT_SHARED)
		return KVM_FILTER_SHARED;

	return KVM_FILTER_PRIVATE;
}

static void __kvm_gmem_invalidate_begin(struct gmem_file *f, pgoff_t start,
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
			kvm_mmu_invalidate_begin(kvm);
		}

		flush |= kvm_mmu_unmap_gfn_range(kvm, &gfn_range);
	}

	if (flush)
		kvm_flush_remote_tlbs(kvm);

	if (found_memslot)
		KVM_MMU_UNLOCK(kvm);
}

static void kvm_gmem_invalidate_begin(struct inode *inode, pgoff_t start,
				      pgoff_t end)
{
	enum kvm_gfn_range_filter attr_filter;
	struct gmem_file *f;

	attr_filter = kvm_gmem_get_invalidate_filter(inode);

	kvm_gmem_for_each_file(f, inode->i_mapping)
		__kvm_gmem_invalidate_begin(f, start, end, attr_filter);
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

	kvm_gmem_for_each_file(f, inode->i_mapping)
		__kvm_gmem_invalidate_end(f, start, end);
}

static long kvm_gmem_punch_hole(struct inode *inode, loff_t offset, loff_t len)
{
	pgoff_t start = offset >> PAGE_SHIFT;
	pgoff_t end = (offset + len) >> PAGE_SHIFT;

	/*
	 * Bindings must be stable across invalidation to ensure the start+end
	 * are balanced.
	 */
	filemap_invalidate_lock(inode->i_mapping);

	kvm_gmem_invalidate_begin(inode, start, end);

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
	 *
	 * Since .release is called only when the reference count is zero,
	 * after which file_ref_get() and get_file_active() fail,
	 * kvm_gmem_get_pfn() cannot be using the file concurrently.
	 * file_ref_put() provides a full barrier, and get_file_active() the
	 * matching acquire barrier.
	 */
	mutex_lock(&kvm->slots_lock);

	filemap_invalidate_lock(inode->i_mapping);

	xa_for_each(&f->bindings, index, slot)
		WRITE_ONCE(slot->gmem.file, NULL);

	/*
	 * All in-flight operations are gone and new bindings can be created.
	 * Zap all SPTEs pointed at by this file.  Do not free the backing
	 * memory, as its lifetime is associated with the inode, not the file.
	 */
	__kvm_gmem_invalidate_begin(f, 0, -1ul,
				    kvm_gmem_get_invalidate_filter(inode));
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

	if (!(GMEM_I(inode)->flags & GUEST_MEMFD_FLAG_INIT_SHARED))
		return VM_FAULT_SIGBUS;

	folio = kvm_gmem_get_folio(inode, vmf->pgoff);
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
		kvm_gmem_mark_prepared(folio);
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
					     unsigned long addr, pgoff_t *pgoff)
{
	struct inode *inode = file_inode(vma->vm_file);

	*pgoff = vma->vm_pgoff + ((addr - vma->vm_start) >> PAGE_SHIFT);

	/*
	 * Return the memory policy for this index, or NULL if none is set.
	 *
	 * Returning NULL, e.g. instead of the current task's memory policy, is
	 * important for the .get_policy kernel ABI: it indicates that no
	 * explicit policy has been set via mbind() for this memory. The caller
	 * can then replace NULL with the default memory policy instead of the
	 * current task's memory policy.
	 */
	return mpol_shared_policy_lookup(&GMEM_I(inode)->policy, *pgoff);
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

static struct file_operations kvm_gmem_fops = {
	.mmap		= kvm_gmem_mmap,
	.open		= generic_file_open,
	.release	= kvm_gmem_release,
	.fallocate	= kvm_gmem_fallocate,
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
	pgoff_t start, end;

	filemap_invalidate_lock_shared(mapping);

	start = folio->index;
	end = start + folio_nr_pages(folio);

	kvm_gmem_invalidate_begin(mapping->host, start, end);

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

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_INVALIDATE
static void kvm_gmem_free_folio(struct folio *folio)
{
	struct page *page = folio_page(folio, 0);
	kvm_pfn_t pfn = page_to_pfn(page);
	int order = folio_order(folio);

	kvm_arch_gmem_invalidate(pfn, pfn + (1ul << order));
}
#endif

static const struct address_space_operations kvm_gmem_aops = {
	.dirty_folio = noop_dirty_folio,
	.migrate_folio	= kvm_gmem_migrate_folio,
	.error_remove_folio = kvm_gmem_error_folio,
#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_INVALIDATE
	.free_folio = kvm_gmem_free_folio,
#endif
};

static int kvm_gmem_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
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

	f = kzalloc(sizeof(*f), GFP_KERNEL);
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

	inode->i_op = &kvm_gmem_iops;
	inode->i_mapping->a_ops = &kvm_gmem_aops;
	inode->i_mode |= S_IFREG;
	inode->i_size = size;
	mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
	mapping_set_inaccessible(inode->i_mapping);
	/* Unmovable mappings are supposed to be marked unevictable as well. */
	WARN_ON_ONCE(!mapping_unevictable(inode->i_mapping));

	GMEM_I(inode)->flags = flags;

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
	list_add(&f->entry, &inode->i_mapping->i_private_list);

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

int kvm_gmem_bind(struct kvm *kvm, struct kvm_memory_slot *slot,
		  unsigned int fd, loff_t offset)
{
	loff_t size = slot->npages << PAGE_SHIFT;
	unsigned long start, end;
	struct gmem_file *f;
	struct inode *inode;
	struct file *file;
	int r = -EINVAL;

	BUILD_BUG_ON(sizeof(gfn_t) != sizeof(slot->gmem.pgoff));

	file = fget(fd);
	if (!file)
		return -EBADF;

	if (file->f_op != &kvm_gmem_fops)
		goto err;

	f = file->private_data;
	if (f->kvm != kvm)
		goto err;

	inode = file_inode(file);

	if (offset < 0 || !PAGE_ALIGNED(offset) ||
	    offset + size > i_size_read(inode))
		goto err;

	filemap_invalidate_lock(inode->i_mapping);

	start = offset >> PAGE_SHIFT;
	end = start + slot->npages;

	if (!xa_empty(&f->bindings) &&
	    xa_find(&f->bindings, &start, end - 1, XA_PRESENT)) {
		filemap_invalidate_unlock(inode->i_mapping);
		goto err;
	}

	/*
	 * memslots of flag KVM_MEM_GUEST_MEMFD are immutable to change, so
	 * kvm_gmem_bind() must occur on a new memslot.  Because the memslot
	 * is not visible yet, kvm_gmem_get_pfn() is guaranteed to see the file.
	 */
	WRITE_ONCE(slot->gmem.file, file);
	slot->gmem.pgoff = start;
	if (kvm_gmem_supports_mmap(inode))
		slot->flags |= KVM_MEMSLOT_GMEM_ONLY;

	xa_store_range(&f->bindings, start, end - 1, slot, GFP_KERNEL);
	filemap_invalidate_unlock(inode->i_mapping);

	/*
	 * Drop the reference to the file, even on success.  The file pins KVM,
	 * not the other way 'round.  Active bindings are invalidated if the
	 * file is closed before memslots are destroyed.
	 */
	r = 0;
err:
	fput(file);
	return r;
}

static void __kvm_gmem_unbind(struct kvm_memory_slot *slot, struct gmem_file *f)
{
	unsigned long start = slot->gmem.pgoff;
	unsigned long end = start + slot->npages;

	xa_store_range(&f->bindings, start, end - 1, NULL, GFP_KERNEL);

	/*
	 * synchronize_srcu(&kvm->srcu) ensured that kvm_gmem_get_pfn()
	 * cannot see this memslot.
	 */
	WRITE_ONCE(slot->gmem.file, NULL);
}

void kvm_gmem_unbind(struct kvm_memory_slot *slot)
{
	/*
	 * Nothing to do if the underlying file was _already_ closed, as
	 * kvm_gmem_release() invalidates and nullifies all bindings.
	 */
	if (!slot->gmem.file)
		return;

	CLASS(gmem_get_file, file)(slot);

	/*
	 * However, if the file is _being_ closed, then the bindings need to be
	 * removed as kvm_gmem_release() might not run until after the memslot
	 * is freed.  Note, modifying the bindings is safe even though the file
	 * is dying as kvm_gmem_release() nullifies slot->gmem.file under
	 * slots_lock, and only puts its reference to KVM after destroying all
	 * bindings.  I.e. reaching this point means kvm_gmem_release() hasn't
	 * yet destroyed the bindings or freed the gmem_file, and can't do so
	 * until the caller drops slots_lock.
	 */
	if (!file) {
		__kvm_gmem_unbind(slot, slot->gmem.file->private_data);
		return;
	}

	filemap_invalidate_lock(file->f_mapping);
	__kvm_gmem_unbind(slot, file->private_data);
	filemap_invalidate_unlock(file->f_mapping);
}

/* Returns a locked folio on success.  */
static struct folio *__kvm_gmem_get_pfn(struct file *file,
					struct kvm_memory_slot *slot,
					pgoff_t index, kvm_pfn_t *pfn,
					bool *is_prepared, int *max_order)
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

	*pfn = folio_file_pfn(folio, index);
	if (max_order)
		*max_order = 0;

	*is_prepared = folio_test_uptodate(folio);
	return folio;
}

int kvm_gmem_get_pfn(struct kvm *kvm, struct kvm_memory_slot *slot,
		     gfn_t gfn, kvm_pfn_t *pfn, struct page **page,
		     int *max_order)
{
	pgoff_t index = kvm_gmem_get_index(slot, gfn);
	struct folio *folio;
	bool is_prepared = false;
	int r = 0;

	CLASS(gmem_get_file, file)(slot);
	if (!file)
		return -EFAULT;

	folio = __kvm_gmem_get_pfn(file, slot, index, pfn, &is_prepared, max_order);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	if (!is_prepared)
		r = kvm_gmem_prepare_folio(kvm, slot, gfn, folio);

	folio_unlock(folio);

	if (!r)
		*page = folio_file_page(folio, index);
	else
		folio_put(folio);

	return r;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_gmem_get_pfn);

#ifdef CONFIG_HAVE_KVM_ARCH_GMEM_POPULATE
long kvm_gmem_populate(struct kvm *kvm, gfn_t start_gfn, void __user *src, long npages,
		       kvm_gmem_populate_cb post_populate, void *opaque)
{
	struct kvm_memory_slot *slot;
	void __user *p;

	int ret = 0, max_order;
	long i;

	lockdep_assert_held(&kvm->slots_lock);

	if (WARN_ON_ONCE(npages <= 0))
		return -EINVAL;

	slot = gfn_to_memslot(kvm, start_gfn);
	if (!kvm_slot_has_gmem(slot))
		return -EINVAL;

	CLASS(gmem_get_file, file)(slot);
	if (!file)
		return -EFAULT;

	filemap_invalidate_lock(file->f_mapping);

	npages = min_t(ulong, slot->npages - (start_gfn - slot->base_gfn), npages);
	for (i = 0; i < npages; i += (1 << max_order)) {
		struct folio *folio;
		gfn_t gfn = start_gfn + i;
		pgoff_t index = kvm_gmem_get_index(slot, gfn);
		bool is_prepared = false;
		kvm_pfn_t pfn;

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		folio = __kvm_gmem_get_pfn(file, slot, index, &pfn, &is_prepared, &max_order);
		if (IS_ERR(folio)) {
			ret = PTR_ERR(folio);
			break;
		}

		if (is_prepared) {
			folio_unlock(folio);
			folio_put(folio);
			ret = -EEXIST;
			break;
		}

		folio_unlock(folio);
		WARN_ON(!IS_ALIGNED(gfn, 1 << max_order) ||
			(npages - i) < (1 << max_order));

		ret = -EINVAL;
		while (!kvm_range_has_memory_attributes(kvm, gfn, gfn + (1 << max_order),
							KVM_MEMORY_ATTRIBUTE_PRIVATE,
							KVM_MEMORY_ATTRIBUTE_PRIVATE)) {
			if (!max_order)
				goto put_folio_and_exit;
			max_order--;
		}

		p = src ? src + i * PAGE_SIZE : NULL;
		ret = post_populate(kvm, gfn, pfn, p, max_order, opaque);
		if (!ret)
			kvm_gmem_mark_prepared(folio);

put_folio_and_exit:
		folio_put(folio);
		if (ret)
			break;
	}

	filemap_invalidate_unlock(file->f_mapping);

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

	gi->flags = 0;
	return &gi->vfs_inode;
}

static void kvm_gmem_destroy_inode(struct inode *inode)
{
	mpol_free_shared_policy(&GMEM_I(inode)->policy);
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

	fc->s_iflags |= SB_I_NOEXEC;
	fc->s_iflags |= SB_I_NODEV;
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
