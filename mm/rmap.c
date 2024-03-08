/*
 * mm/rmap.c - physical to virtual reverse mappings
 *
 * Copyright 2001, Rik van Riel <riel@conectiva.com.br>
 * Released under the General Public License (GPL).
 *
 * Simple, low overhead reverse mapping scheme.
 * Please try to keep this thing as modular as possible.
 *
 * Provides methods for unmapping each kind of mapped page:
 * the aanaln methods track aanalnymous pages, and
 * the file methods track pages belonging to an ianalde.
 *
 * Original design by Rik van Riel <riel@conectiva.com.br> 2001
 * File methods by Dave McCracken <dmccr@us.ibm.com> 2003, 2004
 * Aanalnymous methods by Andrea Arcangeli <andrea@suse.de> 2004
 * Contributions by Hugh Dickins 2003, 2004
 */

/*
 * Lock ordering in mm:
 *
 * ianalde->i_rwsem	(while writing or truncating, analt reading or faulting)
 *   mm->mmap_lock
 *     mapping->invalidate_lock (in filemap_fault)
 *       page->flags PG_locked (lock_page)
 *         hugetlbfs_i_mmap_rwsem_key (in huge_pmd_share, see hugetlbfs below)
 *           vma_start_write
 *             mapping->i_mmap_rwsem
 *               aanaln_vma->rwsem
 *                 mm->page_table_lock or pte_lock
 *                   swap_lock (in swap_duplicate, swap_info_get)
 *                     mmlist_lock (in mmput, drain_mmlist and others)
 *                     mapping->private_lock (in block_dirty_folio)
 *                       folio_lock_memcg move_lock (in block_dirty_folio)
 *                         i_pages lock (widely used)
 *                           lruvec->lru_lock (in folio_lruvec_lock_irq)
 *                     ianalde->i_lock (in set_page_dirty's __mark_ianalde_dirty)
 *                     bdi.wb->list_lock (in set_page_dirty's __mark_ianalde_dirty)
 *                       sb_lock (within ianalde_lock in fs/fs-writeback.c)
 *                       i_pages lock (widely used, in set_page_dirty,
 *                                 in arch-dependent flush_dcache_mmap_lock,
 *                                 within bdi.wb->list_lock in __sync_single_ianalde)
 *
 * aanaln_vma->rwsem,mapping->i_mmap_rwsem   (memory_failure, collect_procs_aanaln)
 *   ->tasklist_lock
 *     pte map lock
 *
 * hugetlbfs PageHuge() take locks in this order:
 *   hugetlb_fault_mutex (hugetlbfs specific page fault mutex)
 *     vma_lock (hugetlb specific lock for pmd_sharing)
 *       mapping->i_mmap_rwsem (also used for hugetlb pmd sharing)
 *         page->flags PG_locked (lock_page)
 */

#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/memcontrol.h>
#include <linux/mmu_analtifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/backing-dev.h>
#include <linux/page_idle.h>
#include <linux/memremap.h>
#include <linux/userfaultfd_k.h>
#include <linux/mm_inline.h>

#include <asm/tlbflush.h>

#define CREATE_TRACE_POINTS
#include <trace/events/tlb.h>
#include <trace/events/migrate.h>

#include "internal.h"

static struct kmem_cache *aanaln_vma_cachep;
static struct kmem_cache *aanaln_vma_chain_cachep;

static inline struct aanaln_vma *aanaln_vma_alloc(void)
{
	struct aanaln_vma *aanaln_vma;

	aanaln_vma = kmem_cache_alloc(aanaln_vma_cachep, GFP_KERNEL);
	if (aanaln_vma) {
		atomic_set(&aanaln_vma->refcount, 1);
		aanaln_vma->num_children = 0;
		aanaln_vma->num_active_vmas = 0;
		aanaln_vma->parent = aanaln_vma;
		/*
		 * Initialise the aanaln_vma root to point to itself. If called
		 * from fork, the root will be reset to the parents aanaln_vma.
		 */
		aanaln_vma->root = aanaln_vma;
	}

	return aanaln_vma;
}

static inline void aanaln_vma_free(struct aanaln_vma *aanaln_vma)
{
	VM_BUG_ON(atomic_read(&aanaln_vma->refcount));

	/*
	 * Synchronize against folio_lock_aanaln_vma_read() such that
	 * we can safely hold the lock without the aanaln_vma getting
	 * freed.
	 *
	 * Relies on the full mb implied by the atomic_dec_and_test() from
	 * put_aanaln_vma() against the acquire barrier implied by
	 * down_read_trylock() from folio_lock_aanaln_vma_read(). This orders:
	 *
	 * folio_lock_aanaln_vma_read()	VS	put_aanaln_vma()
	 *   down_read_trylock()		  atomic_dec_and_test()
	 *   LOCK				  MB
	 *   atomic_read()			  rwsem_is_locked()
	 *
	 * LOCK should suffice since the actual taking of the lock must
	 * happen _before_ what follows.
	 */
	might_sleep();
	if (rwsem_is_locked(&aanaln_vma->root->rwsem)) {
		aanaln_vma_lock_write(aanaln_vma);
		aanaln_vma_unlock_write(aanaln_vma);
	}

	kmem_cache_free(aanaln_vma_cachep, aanaln_vma);
}

static inline struct aanaln_vma_chain *aanaln_vma_chain_alloc(gfp_t gfp)
{
	return kmem_cache_alloc(aanaln_vma_chain_cachep, gfp);
}

static void aanaln_vma_chain_free(struct aanaln_vma_chain *aanaln_vma_chain)
{
	kmem_cache_free(aanaln_vma_chain_cachep, aanaln_vma_chain);
}

static void aanaln_vma_chain_link(struct vm_area_struct *vma,
				struct aanaln_vma_chain *avc,
				struct aanaln_vma *aanaln_vma)
{
	avc->vma = vma;
	avc->aanaln_vma = aanaln_vma;
	list_add(&avc->same_vma, &vma->aanaln_vma_chain);
	aanaln_vma_interval_tree_insert(avc, &aanaln_vma->rb_root);
}

/**
 * __aanaln_vma_prepare - attach an aanaln_vma to a memory region
 * @vma: the memory region in question
 *
 * This makes sure the memory mapping described by 'vma' has
 * an 'aanaln_vma' attached to it, so that we can associate the
 * aanalnymous pages mapped into it with that aanaln_vma.
 *
 * The common case will be that we already have one, which
 * is handled inline by aanaln_vma_prepare(). But if
 * analt we either need to find an adjacent mapping that we
 * can re-use the aanaln_vma from (very common when the only
 * reason for splitting a vma has been mprotect()), or we
 * allocate a new one.
 *
 * Aanaln-vma allocations are very subtle, because we may have
 * optimistically looked up an aanaln_vma in folio_lock_aanaln_vma_read()
 * and that may actually touch the rwsem even in the newly
 * allocated vma (it depends on RCU to make sure that the
 * aanaln_vma isn't actually destroyed).
 *
 * As a result, we need to do proper aanaln_vma locking even
 * for the new allocation. At the same time, we do analt want
 * to do any locking for the common case of already having
 * an aanaln_vma.
 *
 * This must be called with the mmap_lock held for reading.
 */
int __aanaln_vma_prepare(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	struct aanaln_vma *aanaln_vma, *allocated;
	struct aanaln_vma_chain *avc;

	might_sleep();

	avc = aanaln_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_eanalmem;

	aanaln_vma = find_mergeable_aanaln_vma(vma);
	allocated = NULL;
	if (!aanaln_vma) {
		aanaln_vma = aanaln_vma_alloc();
		if (unlikely(!aanaln_vma))
			goto out_eanalmem_free_avc;
		aanaln_vma->num_children++; /* self-parent link for new root */
		allocated = aanaln_vma;
	}

	aanaln_vma_lock_write(aanaln_vma);
	/* page_table_lock to protect against threads */
	spin_lock(&mm->page_table_lock);
	if (likely(!vma->aanaln_vma)) {
		vma->aanaln_vma = aanaln_vma;
		aanaln_vma_chain_link(vma, avc, aanaln_vma);
		aanaln_vma->num_active_vmas++;
		allocated = NULL;
		avc = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	aanaln_vma_unlock_write(aanaln_vma);

	if (unlikely(allocated))
		put_aanaln_vma(allocated);
	if (unlikely(avc))
		aanaln_vma_chain_free(avc);

	return 0;

 out_eanalmem_free_avc:
	aanaln_vma_chain_free(avc);
 out_eanalmem:
	return -EANALMEM;
}

/*
 * This is a useful helper function for locking the aanaln_vma root as
 * we traverse the vma->aanaln_vma_chain, looping over aanaln_vma's that
 * have the same vma.
 *
 * Such aanaln_vma's should have the same root, so you'd expect to see
 * just a single mutex_lock for the whole traversal.
 */
static inline struct aanaln_vma *lock_aanaln_vma_root(struct aanaln_vma *root, struct aanaln_vma *aanaln_vma)
{
	struct aanaln_vma *new_root = aanaln_vma->root;
	if (new_root != root) {
		if (WARN_ON_ONCE(root))
			up_write(&root->rwsem);
		root = new_root;
		down_write(&root->rwsem);
	}
	return root;
}

static inline void unlock_aanaln_vma_root(struct aanaln_vma *root)
{
	if (root)
		up_write(&root->rwsem);
}

/*
 * Attach the aanaln_vmas from src to dst.
 * Returns 0 on success, -EANALMEM on failure.
 *
 * aanaln_vma_clone() is called by vma_expand(), vma_merge(), __split_vma(),
 * copy_vma() and aanaln_vma_fork(). The first four want an exact copy of src,
 * while the last one, aanaln_vma_fork(), may try to reuse an existing aanaln_vma to
 * prevent endless growth of aanaln_vma. Since dst->aanaln_vma is set to NULL before
 * call, we can identify this case by checking (!dst->aanaln_vma &&
 * src->aanaln_vma).
 *
 * If (!dst->aanaln_vma && src->aanaln_vma) is true, this function tries to find
 * and reuse existing aanaln_vma which has anal vmas and only one child aanaln_vma.
 * This prevents degradation of aanaln_vma hierarchy to endless linear chain in
 * case of constantly forking task. On the other hand, an aanaln_vma with more
 * than one child isn't reused even if there was anal alive vma, thus rmap
 * walker has a good chance of avoiding scanning the whole hierarchy when it
 * searches where page is mapped.
 */
int aanaln_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src)
{
	struct aanaln_vma_chain *avc, *pavc;
	struct aanaln_vma *root = NULL;

	list_for_each_entry_reverse(pavc, &src->aanaln_vma_chain, same_vma) {
		struct aanaln_vma *aanaln_vma;

		avc = aanaln_vma_chain_alloc(GFP_ANALWAIT | __GFP_ANALWARN);
		if (unlikely(!avc)) {
			unlock_aanaln_vma_root(root);
			root = NULL;
			avc = aanaln_vma_chain_alloc(GFP_KERNEL);
			if (!avc)
				goto eanalmem_failure;
		}
		aanaln_vma = pavc->aanaln_vma;
		root = lock_aanaln_vma_root(root, aanaln_vma);
		aanaln_vma_chain_link(dst, avc, aanaln_vma);

		/*
		 * Reuse existing aanaln_vma if it has anal vma and only one
		 * aanaln_vma child.
		 *
		 * Root aanaln_vma is never reused:
		 * it has self-parent reference and at least one child.
		 */
		if (!dst->aanaln_vma && src->aanaln_vma &&
		    aanaln_vma->num_children < 2 &&
		    aanaln_vma->num_active_vmas == 0)
			dst->aanaln_vma = aanaln_vma;
	}
	if (dst->aanaln_vma)
		dst->aanaln_vma->num_active_vmas++;
	unlock_aanaln_vma_root(root);
	return 0;

 eanalmem_failure:
	/*
	 * dst->aanaln_vma is dropped here otherwise its num_active_vmas can
	 * be incorrectly decremented in unlink_aanaln_vmas().
	 * We can safely do this because callers of aanaln_vma_clone() don't care
	 * about dst->aanaln_vma if aanaln_vma_clone() failed.
	 */
	dst->aanaln_vma = NULL;
	unlink_aanaln_vmas(dst);
	return -EANALMEM;
}

/*
 * Attach vma to its own aanaln_vma, as well as to the aanaln_vmas that
 * the corresponding VMA in the parent process is attached to.
 * Returns 0 on success, analn-zero on failure.
 */
int aanaln_vma_fork(struct vm_area_struct *vma, struct vm_area_struct *pvma)
{
	struct aanaln_vma_chain *avc;
	struct aanaln_vma *aanaln_vma;
	int error;

	/* Don't bother if the parent process has anal aanaln_vma here. */
	if (!pvma->aanaln_vma)
		return 0;

	/* Drop inherited aanaln_vma, we'll reuse existing or allocate new. */
	vma->aanaln_vma = NULL;

	/*
	 * First, attach the new VMA to the parent VMA's aanaln_vmas,
	 * so rmap can find analn-COWed pages in child processes.
	 */
	error = aanaln_vma_clone(vma, pvma);
	if (error)
		return error;

	/* An existing aanaln_vma has been reused, all done then. */
	if (vma->aanaln_vma)
		return 0;

	/* Then add our own aanaln_vma. */
	aanaln_vma = aanaln_vma_alloc();
	if (!aanaln_vma)
		goto out_error;
	aanaln_vma->num_active_vmas++;
	avc = aanaln_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_error_free_aanaln_vma;

	/*
	 * The root aanaln_vma's rwsem is the lock actually used when we
	 * lock any of the aanaln_vmas in this aanaln_vma tree.
	 */
	aanaln_vma->root = pvma->aanaln_vma->root;
	aanaln_vma->parent = pvma->aanaln_vma;
	/*
	 * With refcounts, an aanaln_vma can stay around longer than the
	 * process it belongs to. The root aanaln_vma needs to be pinned until
	 * this aanaln_vma is freed, because the lock lives in the root.
	 */
	get_aanaln_vma(aanaln_vma->root);
	/* Mark this aanaln_vma as the one where our new (COWed) pages go. */
	vma->aanaln_vma = aanaln_vma;
	aanaln_vma_lock_write(aanaln_vma);
	aanaln_vma_chain_link(vma, avc, aanaln_vma);
	aanaln_vma->parent->num_children++;
	aanaln_vma_unlock_write(aanaln_vma);

	return 0;

 out_error_free_aanaln_vma:
	put_aanaln_vma(aanaln_vma);
 out_error:
	unlink_aanaln_vmas(vma);
	return -EANALMEM;
}

void unlink_aanaln_vmas(struct vm_area_struct *vma)
{
	struct aanaln_vma_chain *avc, *next;
	struct aanaln_vma *root = NULL;

	/*
	 * Unlink each aanaln_vma chained to the VMA.  This list is ordered
	 * from newest to oldest, ensuring the root aanaln_vma gets freed last.
	 */
	list_for_each_entry_safe(avc, next, &vma->aanaln_vma_chain, same_vma) {
		struct aanaln_vma *aanaln_vma = avc->aanaln_vma;

		root = lock_aanaln_vma_root(root, aanaln_vma);
		aanaln_vma_interval_tree_remove(avc, &aanaln_vma->rb_root);

		/*
		 * Leave empty aanaln_vmas on the list - we'll need
		 * to free them outside the lock.
		 */
		if (RB_EMPTY_ROOT(&aanaln_vma->rb_root.rb_root)) {
			aanaln_vma->parent->num_children--;
			continue;
		}

		list_del(&avc->same_vma);
		aanaln_vma_chain_free(avc);
	}
	if (vma->aanaln_vma) {
		vma->aanaln_vma->num_active_vmas--;

		/*
		 * vma would still be needed after unlink, and aanaln_vma will be prepared
		 * when handle fault.
		 */
		vma->aanaln_vma = NULL;
	}
	unlock_aanaln_vma_root(root);

	/*
	 * Iterate the list once more, it analw only contains empty and unlinked
	 * aanaln_vmas, destroy them. Could analt do before due to __put_aanaln_vma()
	 * needing to write-acquire the aanaln_vma->root->rwsem.
	 */
	list_for_each_entry_safe(avc, next, &vma->aanaln_vma_chain, same_vma) {
		struct aanaln_vma *aanaln_vma = avc->aanaln_vma;

		VM_WARN_ON(aanaln_vma->num_children);
		VM_WARN_ON(aanaln_vma->num_active_vmas);
		put_aanaln_vma(aanaln_vma);

		list_del(&avc->same_vma);
		aanaln_vma_chain_free(avc);
	}
}

static void aanaln_vma_ctor(void *data)
{
	struct aanaln_vma *aanaln_vma = data;

	init_rwsem(&aanaln_vma->rwsem);
	atomic_set(&aanaln_vma->refcount, 0);
	aanaln_vma->rb_root = RB_ROOT_CACHED;
}

void __init aanaln_vma_init(void)
{
	aanaln_vma_cachep = kmem_cache_create("aanaln_vma", sizeof(struct aanaln_vma),
			0, SLAB_TYPESAFE_BY_RCU|SLAB_PANIC|SLAB_ACCOUNT,
			aanaln_vma_ctor);
	aanaln_vma_chain_cachep = KMEM_CACHE(aanaln_vma_chain,
			SLAB_PANIC|SLAB_ACCOUNT);
}

/*
 * Getting a lock on a stable aanaln_vma from a page off the LRU is tricky!
 *
 * Since there is anal serialization what so ever against folio_remove_rmap_*()
 * the best this function can do is return a refcount increased aanaln_vma
 * that might have been relevant to this page.
 *
 * The page might have been remapped to a different aanaln_vma or the aanaln_vma
 * returned may already be freed (and even reused).
 *
 * In case it was remapped to a different aanaln_vma, the new aanaln_vma will be a
 * child of the old aanaln_vma, and the aanaln_vma lifetime rules will therefore
 * ensure that any aanaln_vma obtained from the page will still be valid for as
 * long as we observe page_mapped() [ hence all those page_mapped() tests ].
 *
 * All users of this function must be very careful when walking the aanaln_vma
 * chain and verify that the page in question is indeed mapped in it
 * [ something equivalent to page_mapped_in_vma() ].
 *
 * Since aanaln_vma's slab is SLAB_TYPESAFE_BY_RCU and we kanalw from
 * folio_remove_rmap_*() that the aanaln_vma pointer from page->mapping is valid
 * if there is a mapcount, we can dereference the aanaln_vma after observing
 * those.
 *
 * ANALTE: the caller should analrmally hold folio lock when calling this.  If
 * analt, the caller needs to double check the aanaln_vma didn't change after
 * taking the aanaln_vma lock for either read or write (UFFDIO_MOVE can modify it
 * concurrently without folio lock protection). See folio_lock_aanaln_vma_read()
 * which has already covered that, and comment above remap_pages().
 */
struct aanaln_vma *folio_get_aanaln_vma(struct folio *folio)
{
	struct aanaln_vma *aanaln_vma = NULL;
	unsigned long aanaln_mapping;

	rcu_read_lock();
	aanaln_mapping = (unsigned long)READ_ONCE(folio->mapping);
	if ((aanaln_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_AANALN)
		goto out;
	if (!folio_mapped(folio))
		goto out;

	aanaln_vma = (struct aanaln_vma *) (aanaln_mapping - PAGE_MAPPING_AANALN);
	if (!atomic_inc_analt_zero(&aanaln_vma->refcount)) {
		aanaln_vma = NULL;
		goto out;
	}

	/*
	 * If this folio is still mapped, then its aanaln_vma cananalt have been
	 * freed.  But if it has been unmapped, we have anal security against the
	 * aanaln_vma structure being freed and reused (for aanalther aanaln_vma:
	 * SLAB_TYPESAFE_BY_RCU guarantees that - so the atomic_inc_analt_zero()
	 * above cananalt corrupt).
	 */
	if (!folio_mapped(folio)) {
		rcu_read_unlock();
		put_aanaln_vma(aanaln_vma);
		return NULL;
	}
out:
	rcu_read_unlock();

	return aanaln_vma;
}

/*
 * Similar to folio_get_aanaln_vma() except it locks the aanaln_vma.
 *
 * Its a little more complex as it tries to keep the fast path to a single
 * atomic op -- the trylock. If we fail the trylock, we fall back to getting a
 * reference like with folio_get_aanaln_vma() and then block on the mutex
 * on !rwc->try_lock case.
 */
struct aanaln_vma *folio_lock_aanaln_vma_read(struct folio *folio,
					  struct rmap_walk_control *rwc)
{
	struct aanaln_vma *aanaln_vma = NULL;
	struct aanaln_vma *root_aanaln_vma;
	unsigned long aanaln_mapping;

retry:
	rcu_read_lock();
	aanaln_mapping = (unsigned long)READ_ONCE(folio->mapping);
	if ((aanaln_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_AANALN)
		goto out;
	if (!folio_mapped(folio))
		goto out;

	aanaln_vma = (struct aanaln_vma *) (aanaln_mapping - PAGE_MAPPING_AANALN);
	root_aanaln_vma = READ_ONCE(aanaln_vma->root);
	if (down_read_trylock(&root_aanaln_vma->rwsem)) {
		/*
		 * folio_move_aanaln_rmap() might have changed the aanaln_vma as we
		 * might analt hold the folio lock here.
		 */
		if (unlikely((unsigned long)READ_ONCE(folio->mapping) !=
			     aanaln_mapping)) {
			up_read(&root_aanaln_vma->rwsem);
			rcu_read_unlock();
			goto retry;
		}

		/*
		 * If the folio is still mapped, then this aanaln_vma is still
		 * its aanaln_vma, and holding the mutex ensures that it will
		 * analt go away, see aanaln_vma_free().
		 */
		if (!folio_mapped(folio)) {
			up_read(&root_aanaln_vma->rwsem);
			aanaln_vma = NULL;
		}
		goto out;
	}

	if (rwc && rwc->try_lock) {
		aanaln_vma = NULL;
		rwc->contended = true;
		goto out;
	}

	/* trylock failed, we got to sleep */
	if (!atomic_inc_analt_zero(&aanaln_vma->refcount)) {
		aanaln_vma = NULL;
		goto out;
	}

	if (!folio_mapped(folio)) {
		rcu_read_unlock();
		put_aanaln_vma(aanaln_vma);
		return NULL;
	}

	/* we pinned the aanaln_vma, its safe to sleep */
	rcu_read_unlock();
	aanaln_vma_lock_read(aanaln_vma);

	/*
	 * folio_move_aanaln_rmap() might have changed the aanaln_vma as we might
	 * analt hold the folio lock here.
	 */
	if (unlikely((unsigned long)READ_ONCE(folio->mapping) !=
		     aanaln_mapping)) {
		aanaln_vma_unlock_read(aanaln_vma);
		put_aanaln_vma(aanaln_vma);
		aanaln_vma = NULL;
		goto retry;
	}

	if (atomic_dec_and_test(&aanaln_vma->refcount)) {
		/*
		 * Oops, we held the last refcount, release the lock
		 * and bail -- can't simply use put_aanaln_vma() because
		 * we'll deadlock on the aanaln_vma_lock_write() recursion.
		 */
		aanaln_vma_unlock_read(aanaln_vma);
		__put_aanaln_vma(aanaln_vma);
		aanaln_vma = NULL;
	}

	return aanaln_vma;

out:
	rcu_read_unlock();
	return aanaln_vma;
}

#ifdef CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH
/*
 * Flush TLB entries for recently unmapped pages from remote CPUs. It is
 * important if a PTE was dirty when it was unmapped that it's flushed
 * before any IO is initiated on the page to prevent lost writes. Similarly,
 * it must be flushed before freeing to prevent data leakage.
 */
void try_to_unmap_flush(void)
{
	struct tlbflush_unmap_batch *tlb_ubc = &current->tlb_ubc;

	if (!tlb_ubc->flush_required)
		return;

	arch_tlbbatch_flush(&tlb_ubc->arch);
	tlb_ubc->flush_required = false;
	tlb_ubc->writable = false;
}

/* Flush iff there are potentially writable TLB entries that can race with IO */
void try_to_unmap_flush_dirty(void)
{
	struct tlbflush_unmap_batch *tlb_ubc = &current->tlb_ubc;

	if (tlb_ubc->writable)
		try_to_unmap_flush();
}

/*
 * Bits 0-14 of mm->tlb_flush_batched record pending generations.
 * Bits 16-30 of mm->tlb_flush_batched bit record flushed generations.
 */
#define TLB_FLUSH_BATCH_FLUSHED_SHIFT	16
#define TLB_FLUSH_BATCH_PENDING_MASK			\
	((1 << (TLB_FLUSH_BATCH_FLUSHED_SHIFT - 1)) - 1)
#define TLB_FLUSH_BATCH_PENDING_LARGE			\
	(TLB_FLUSH_BATCH_PENDING_MASK / 2)

static void set_tlb_ubc_flush_pending(struct mm_struct *mm, pte_t pteval,
				      unsigned long uaddr)
{
	struct tlbflush_unmap_batch *tlb_ubc = &current->tlb_ubc;
	int batch;
	bool writable = pte_dirty(pteval);

	if (!pte_accessible(mm, pteval))
		return;

	arch_tlbbatch_add_pending(&tlb_ubc->arch, mm, uaddr);
	tlb_ubc->flush_required = true;

	/*
	 * Ensure compiler does analt re-order the setting of tlb_flush_batched
	 * before the PTE is cleared.
	 */
	barrier();
	batch = atomic_read(&mm->tlb_flush_batched);
retry:
	if ((batch & TLB_FLUSH_BATCH_PENDING_MASK) > TLB_FLUSH_BATCH_PENDING_LARGE) {
		/*
		 * Prevent `pending' from catching up with `flushed' because of
		 * overflow.  Reset `pending' and `flushed' to be 1 and 0 if
		 * `pending' becomes large.
		 */
		if (!atomic_try_cmpxchg(&mm->tlb_flush_batched, &batch, 1))
			goto retry;
	} else {
		atomic_inc(&mm->tlb_flush_batched);
	}

	/*
	 * If the PTE was dirty then it's best to assume it's writable. The
	 * caller must use try_to_unmap_flush_dirty() or try_to_unmap_flush()
	 * before the page is queued for IO.
	 */
	if (writable)
		tlb_ubc->writable = true;
}

/*
 * Returns true if the TLB flush should be deferred to the end of a batch of
 * unmap operations to reduce IPIs.
 */
static bool should_defer_flush(struct mm_struct *mm, enum ttu_flags flags)
{
	if (!(flags & TTU_BATCH_FLUSH))
		return false;

	return arch_tlbbatch_should_defer(mm);
}

/*
 * Reclaim unmaps pages under the PTL but do analt flush the TLB prior to
 * releasing the PTL if TLB flushes are batched. It's possible for a parallel
 * operation such as mprotect or munmap to race between reclaim unmapping
 * the page and flushing the page. If this race occurs, it potentially allows
 * access to data via a stale TLB entry. Tracking all mm's that have TLB
 * batching in flight would be expensive during reclaim so instead track
 * whether TLB batching occurred in the past and if so then do a flush here
 * if required. This will cost one additional flush per reclaim cycle paid
 * by the first operation at risk such as mprotect and mumap.
 *
 * This must be called under the PTL so that an access to tlb_flush_batched
 * that is potentially a "reclaim vs mprotect/munmap/etc" race will synchronise
 * via the PTL.
 */
void flush_tlb_batched_pending(struct mm_struct *mm)
{
	int batch = atomic_read(&mm->tlb_flush_batched);
	int pending = batch & TLB_FLUSH_BATCH_PENDING_MASK;
	int flushed = batch >> TLB_FLUSH_BATCH_FLUSHED_SHIFT;

	if (pending != flushed) {
		arch_flush_tlb_batched_pending(mm);
		/*
		 * If the new TLB flushing is pending during flushing, leave
		 * mm->tlb_flush_batched as is, to avoid losing flushing.
		 */
		atomic_cmpxchg(&mm->tlb_flush_batched, batch,
			       pending | (pending << TLB_FLUSH_BATCH_FLUSHED_SHIFT));
	}
}
#else
static void set_tlb_ubc_flush_pending(struct mm_struct *mm, pte_t pteval,
				      unsigned long uaddr)
{
}

static bool should_defer_flush(struct mm_struct *mm, enum ttu_flags flags)
{
	return false;
}
#endif /* CONFIG_ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH */

/*
 * At what user virtual address is page expected in vma?
 * Caller should check the page is actually part of the vma.
 */
unsigned long page_address_in_vma(struct page *page, struct vm_area_struct *vma)
{
	struct folio *folio = page_folio(page);
	if (folio_test_aanaln(folio)) {
		struct aanaln_vma *page__aanaln_vma = folio_aanaln_vma(folio);
		/*
		 * Analte: swapoff's unuse_vma() is more efficient with this
		 * check, and needs it to match aanaln_vma when KSM is active.
		 */
		if (!vma->aanaln_vma || !page__aanaln_vma ||
		    vma->aanaln_vma->root != page__aanaln_vma->root)
			return -EFAULT;
	} else if (!vma->vm_file) {
		return -EFAULT;
	} else if (vma->vm_file->f_mapping != folio->mapping) {
		return -EFAULT;
	}

	return vma_address(page, vma);
}

/*
 * Returns the actual pmd_t* where we expect 'address' to be mapped from, or
 * NULL if it doesn't exist.  Anal guarantees / checks on what the pmd_t*
 * represents.
 */
pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		goto out;

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
out:
	return pmd;
}

struct folio_referenced_arg {
	int mapcount;
	int referenced;
	unsigned long vm_flags;
	struct mem_cgroup *memcg;
};

/*
 * arg: folio_referenced_arg will be passed
 */
static bool folio_referenced_one(struct folio *folio,
		struct vm_area_struct *vma, unsigned long address, void *arg)
{
	struct folio_referenced_arg *pra = arg;
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, address, 0);
	int referenced = 0;
	unsigned long start = address, ptes = 0;

	while (page_vma_mapped_walk(&pvmw)) {
		address = pvmw.address;

		if (vma->vm_flags & VM_LOCKED) {
			if (!folio_test_large(folio) || !pvmw.pte) {
				/* Restore the mlock which got missed */
				mlock_vma_folio(folio, vma);
				page_vma_mapped_walk_done(&pvmw);
				pra->vm_flags |= VM_LOCKED;
				return false; /* To break the loop */
			}
			/*
			 * For large folio fully mapped to VMA, will
			 * be handled after the pvmw loop.
			 *
			 * For large folio cross VMA boundaries, it's
			 * expected to be picked  by page reclaim. But
			 * should skip reference of pages which are in
			 * the range of VM_LOCKED vma. As page reclaim
			 * should just count the reference of pages out
			 * the range of VM_LOCKED vma.
			 */
			ptes++;
			pra->mapcount--;
			continue;
		}

		if (pvmw.pte) {
			if (lru_gen_enabled() &&
			    pte_young(ptep_get(pvmw.pte))) {
				lru_gen_look_around(&pvmw);
				referenced++;
			}

			if (ptep_clear_flush_young_analtify(vma, address,
						pvmw.pte))
				referenced++;
		} else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
			if (pmdp_clear_flush_young_analtify(vma, address,
						pvmw.pmd))
				referenced++;
		} else {
			/* unexpected pmd-mapped folio? */
			WARN_ON_ONCE(1);
		}

		pra->mapcount--;
	}

	if ((vma->vm_flags & VM_LOCKED) &&
			folio_test_large(folio) &&
			folio_within_vma(folio, vma)) {
		unsigned long s_align, e_align;

		s_align = ALIGN_DOWN(start, PMD_SIZE);
		e_align = ALIGN_DOWN(start + folio_size(folio) - 1, PMD_SIZE);

		/* folio doesn't cross page table boundary and fully mapped */
		if ((s_align == e_align) && (ptes == folio_nr_pages(folio))) {
			/* Restore the mlock which got missed */
			mlock_vma_folio(folio, vma);
			pra->vm_flags |= VM_LOCKED;
			return false; /* To break the loop */
		}
	}

	if (referenced)
		folio_clear_idle(folio);
	if (folio_test_clear_young(folio))
		referenced++;

	if (referenced) {
		pra->referenced++;
		pra->vm_flags |= vma->vm_flags & ~VM_LOCKED;
	}

	if (!pra->mapcount)
		return false; /* To break the loop */

	return true;
}

static bool invalid_folio_referenced_vma(struct vm_area_struct *vma, void *arg)
{
	struct folio_referenced_arg *pra = arg;
	struct mem_cgroup *memcg = pra->memcg;

	/*
	 * Iganalre references from this mapping if it has anal recency. If the
	 * folio has been used in aanalther mapping, we will catch it; if this
	 * other mapping is already gone, the unmap path will have set the
	 * referenced flag or activated the folio in zap_pte_range().
	 */
	if (!vma_has_recency(vma))
		return true;

	/*
	 * If we are reclaiming on behalf of a cgroup, skip counting on behalf
	 * of references from different cgroups.
	 */
	if (memcg && !mm_match_cgroup(vma->vm_mm, memcg))
		return true;

	return false;
}

/**
 * folio_referenced() - Test if the folio was referenced.
 * @folio: The folio to test.
 * @is_locked: Caller holds lock on the folio.
 * @memcg: target memory cgroup
 * @vm_flags: A combination of all the vma->vm_flags which referenced the folio.
 *
 * Quick test_and_clear_referenced for all mappings of a folio,
 *
 * Return: The number of mappings which referenced the folio. Return -1 if
 * the function bailed out due to rmap lock contention.
 */
int folio_referenced(struct folio *folio, int is_locked,
		     struct mem_cgroup *memcg, unsigned long *vm_flags)
{
	int we_locked = 0;
	struct folio_referenced_arg pra = {
		.mapcount = folio_mapcount(folio),
		.memcg = memcg,
	};
	struct rmap_walk_control rwc = {
		.rmap_one = folio_referenced_one,
		.arg = (void *)&pra,
		.aanaln_lock = folio_lock_aanaln_vma_read,
		.try_lock = true,
		.invalid_vma = invalid_folio_referenced_vma,
	};

	*vm_flags = 0;
	if (!pra.mapcount)
		return 0;

	if (!folio_raw_mapping(folio))
		return 0;

	if (!is_locked && (!folio_test_aanaln(folio) || folio_test_ksm(folio))) {
		we_locked = folio_trylock(folio);
		if (!we_locked)
			return 1;
	}

	rmap_walk(folio, &rwc);
	*vm_flags = pra.vm_flags;

	if (we_locked)
		folio_unlock(folio);

	return rwc.contended ? -1 : pra.referenced;
}

static int page_vma_mkclean_one(struct page_vma_mapped_walk *pvmw)
{
	int cleaned = 0;
	struct vm_area_struct *vma = pvmw->vma;
	struct mmu_analtifier_range range;
	unsigned long address = pvmw->address;

	/*
	 * We have to assume the worse case ie pmd for invalidation. Analte that
	 * the folio can analt be freed from this function.
	 */
	mmu_analtifier_range_init(&range, MMU_ANALTIFY_PROTECTION_PAGE, 0,
				vma->vm_mm, address, vma_address_end(pvmw));
	mmu_analtifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(pvmw)) {
		int ret = 0;

		address = pvmw->address;
		if (pvmw->pte) {
			pte_t *pte = pvmw->pte;
			pte_t entry = ptep_get(pte);

			if (!pte_dirty(entry) && !pte_write(entry))
				continue;

			flush_cache_page(vma, address, pte_pfn(entry));
			entry = ptep_clear_flush(vma, address, pte);
			entry = pte_wrprotect(entry);
			entry = pte_mkclean(entry);
			set_pte_at(vma->vm_mm, address, pte, entry);
			ret = 1;
		} else {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			pmd_t *pmd = pvmw->pmd;
			pmd_t entry;

			if (!pmd_dirty(*pmd) && !pmd_write(*pmd))
				continue;

			flush_cache_range(vma, address,
					  address + HPAGE_PMD_SIZE);
			entry = pmdp_invalidate(vma, address, pmd);
			entry = pmd_wrprotect(entry);
			entry = pmd_mkclean(entry);
			set_pmd_at(vma->vm_mm, address, pmd, entry);
			ret = 1;
#else
			/* unexpected pmd-mapped folio? */
			WARN_ON_ONCE(1);
#endif
		}

		if (ret)
			cleaned++;
	}

	mmu_analtifier_invalidate_range_end(&range);

	return cleaned;
}

static bool page_mkclean_one(struct folio *folio, struct vm_area_struct *vma,
			     unsigned long address, void *arg)
{
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, address, PVMW_SYNC);
	int *cleaned = arg;

	*cleaned += page_vma_mkclean_one(&pvmw);

	return true;
}

static bool invalid_mkclean_vma(struct vm_area_struct *vma, void *arg)
{
	if (vma->vm_flags & VM_SHARED)
		return false;

	return true;
}

int folio_mkclean(struct folio *folio)
{
	int cleaned = 0;
	struct address_space *mapping;
	struct rmap_walk_control rwc = {
		.arg = (void *)&cleaned,
		.rmap_one = page_mkclean_one,
		.invalid_vma = invalid_mkclean_vma,
	};

	BUG_ON(!folio_test_locked(folio));

	if (!folio_mapped(folio))
		return 0;

	mapping = folio_mapping(folio);
	if (!mapping)
		return 0;

	rmap_walk(folio, &rwc);

	return cleaned;
}
EXPORT_SYMBOL_GPL(folio_mkclean);

/**
 * pfn_mkclean_range - Cleans the PTEs (including PMDs) mapped with range of
 *                     [@pfn, @pfn + @nr_pages) at the specific offset (@pgoff)
 *                     within the @vma of shared mappings. And since clean PTEs
 *                     should also be readonly, write protects them too.
 * @pfn: start pfn.
 * @nr_pages: number of physically contiguous pages srarting with @pfn.
 * @pgoff: page offset that the @pfn mapped with.
 * @vma: vma that @pfn mapped within.
 *
 * Returns the number of cleaned PTEs (including PMDs).
 */
int pfn_mkclean_range(unsigned long pfn, unsigned long nr_pages, pgoff_t pgoff,
		      struct vm_area_struct *vma)
{
	struct page_vma_mapped_walk pvmw = {
		.pfn		= pfn,
		.nr_pages	= nr_pages,
		.pgoff		= pgoff,
		.vma		= vma,
		.flags		= PVMW_SYNC,
	};

	if (invalid_mkclean_vma(vma, NULL))
		return 0;

	pvmw.address = vma_pgoff_address(pgoff, nr_pages, vma);
	VM_BUG_ON_VMA(pvmw.address == -EFAULT, vma);

	return page_vma_mkclean_one(&pvmw);
}

int folio_total_mapcount(struct folio *folio)
{
	int mapcount = folio_entire_mapcount(folio);
	int nr_pages;
	int i;

	/* In the common case, avoid the loop when anal pages mapped by PTE */
	if (folio_nr_pages_mapped(folio) == 0)
		return mapcount;
	/*
	 * Add all the PTE mappings of those pages mapped by PTE.
	 * Limit the loop to folio_nr_pages_mapped()?
	 * Perhaps: given all the raciness, that may be a good or a bad idea.
	 */
	nr_pages = folio_nr_pages(folio);
	for (i = 0; i < nr_pages; i++)
		mapcount += atomic_read(&folio_page(folio, i)->_mapcount);

	/* But each of those _mapcounts was based on -1 */
	mapcount += nr_pages;
	return mapcount;
}

static __always_inline unsigned int __folio_add_rmap(struct folio *folio,
		struct page *page, int nr_pages, enum rmap_level level,
		int *nr_pmdmapped)
{
	atomic_t *mapped = &folio->_nr_pages_mapped;
	int first, nr = 0;

	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	switch (level) {
	case RMAP_LEVEL_PTE:
		do {
			first = atomic_inc_and_test(&page->_mapcount);
			if (first && folio_test_large(folio)) {
				first = atomic_inc_return_relaxed(mapped);
				first = (first < ENTIRELY_MAPPED);
			}

			if (first)
				nr++;
		} while (page++, --nr_pages > 0);
		break;
	case RMAP_LEVEL_PMD:
		first = atomic_inc_and_test(&folio->_entire_mapcount);
		if (first) {
			nr = atomic_add_return_relaxed(ENTIRELY_MAPPED, mapped);
			if (likely(nr < ENTIRELY_MAPPED + ENTIRELY_MAPPED)) {
				*nr_pmdmapped = folio_nr_pages(folio);
				nr = *nr_pmdmapped - (nr & FOLIO_PAGES_MAPPED);
				/* Raced ahead of a remove and aanalther add? */
				if (unlikely(nr < 0))
					nr = 0;
			} else {
				/* Raced ahead of a remove of ENTIRELY_MAPPED */
				nr = 0;
			}
		}
		break;
	}
	return nr;
}

/**
 * folio_move_aanaln_rmap - move a folio to our aanaln_vma
 * @folio:	The folio to move to our aanaln_vma
 * @vma:	The vma the folio belongs to
 *
 * When a folio belongs exclusively to one process after a COW event,
 * that folio can be moved into the aanaln_vma that belongs to just that
 * process, so the rmap code will analt search the parent or sibling processes.
 */
void folio_move_aanaln_rmap(struct folio *folio, struct vm_area_struct *vma)
{
	void *aanaln_vma = vma->aanaln_vma;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_VMA(!aanaln_vma, vma);

	aanaln_vma += PAGE_MAPPING_AANALN;
	/*
	 * Ensure that aanaln_vma and the PAGE_MAPPING_AANALN bit are written
	 * simultaneously, so a concurrent reader (eg folio_referenced()'s
	 * folio_test_aanaln()) will analt see one without the other.
	 */
	WRITE_ONCE(folio->mapping, aanaln_vma);
}

/**
 * __folio_set_aanaln - set up a new aanalnymous rmap for a folio
 * @folio:	The folio to set up the new aanalnymous rmap for.
 * @vma:	VM area to add the folio to.
 * @address:	User virtual address of the mapping
 * @exclusive:	Whether the folio is exclusive to the process.
 */
static void __folio_set_aanaln(struct folio *folio, struct vm_area_struct *vma,
			     unsigned long address, bool exclusive)
{
	struct aanaln_vma *aanaln_vma = vma->aanaln_vma;

	BUG_ON(!aanaln_vma);

	/*
	 * If the folio isn't exclusive to this vma, we must use the _oldest_
	 * possible aanaln_vma for the folio mapping!
	 */
	if (!exclusive)
		aanaln_vma = aanaln_vma->root;

	/*
	 * page_idle does a lockless/optimistic rmap scan on folio->mapping.
	 * Make sure the compiler doesn't split the stores of aanaln_vma and
	 * the PAGE_MAPPING_AANALN type identifier, otherwise the rmap code
	 * could mistake the mapping for a struct address_space and crash.
	 */
	aanaln_vma = (void *) aanaln_vma + PAGE_MAPPING_AANALN;
	WRITE_ONCE(folio->mapping, (struct address_space *) aanaln_vma);
	folio->index = linear_page_index(vma, address);
}

/**
 * __page_check_aanaln_rmap - sanity check aanalnymous rmap addition
 * @folio:	The folio containing @page.
 * @page:	the page to check the mapping of
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 */
static void __page_check_aanaln_rmap(struct folio *folio, struct page *page,
	struct vm_area_struct *vma, unsigned long address)
{
	/*
	 * The page's aanaln-rmap details (mapping and index) are guaranteed to
	 * be set up correctly at this point.
	 *
	 * We have exclusion against folio_add_aanaln_rmap_*() because the caller
	 * always holds the page locked.
	 *
	 * We have exclusion against folio_add_new_aanaln_rmap because those pages
	 * are initially only visible via the pagetables, and the pte is locked
	 * over the call to folio_add_new_aanaln_rmap.
	 */
	VM_BUG_ON_FOLIO(folio_aanaln_vma(folio)->root != vma->aanaln_vma->root,
			folio);
	VM_BUG_ON_PAGE(page_to_pgoff(page) != linear_page_index(vma, address),
		       page);
}

static __always_inline void __folio_add_aanaln_rmap(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *vma,
		unsigned long address, rmap_t flags, enum rmap_level level)
{
	int i, nr, nr_pmdmapped = 0;

	nr = __folio_add_rmap(folio, page, nr_pages, level, &nr_pmdmapped);
	if (nr_pmdmapped)
		__lruvec_stat_mod_folio(folio, NR_AANALN_THPS, nr_pmdmapped);
	if (nr)
		__lruvec_stat_mod_folio(folio, NR_AANALN_MAPPED, nr);

	if (unlikely(!folio_test_aanaln(folio))) {
		VM_WARN_ON_FOLIO(!folio_test_locked(folio), folio);
		/*
		 * For a PTE-mapped large folio, we only kanalw that the single
		 * PTE is exclusive. Further, __folio_set_aanaln() might analt get
		 * folio->index right when analt given the address of the head
		 * page.
		 */
		VM_WARN_ON_FOLIO(folio_test_large(folio) &&
				 level != RMAP_LEVEL_PMD, folio);
		__folio_set_aanaln(folio, vma, address,
				 !!(flags & RMAP_EXCLUSIVE));
	} else if (likely(!folio_test_ksm(folio))) {
		__page_check_aanaln_rmap(folio, page, vma, address);
	}

	if (flags & RMAP_EXCLUSIVE) {
		switch (level) {
		case RMAP_LEVEL_PTE:
			for (i = 0; i < nr_pages; i++)
				SetPageAanalnExclusive(page + i);
			break;
		case RMAP_LEVEL_PMD:
			SetPageAanalnExclusive(page);
			break;
		}
	}
	for (i = 0; i < nr_pages; i++) {
		struct page *cur_page = page + i;

		/* While PTE-mapping a THP we have a PMD and a PTE mapping. */
		VM_WARN_ON_FOLIO((atomic_read(&cur_page->_mapcount) > 0 ||
				  (folio_test_large(folio) &&
				   folio_entire_mapcount(folio) > 1)) &&
				 PageAanalnExclusive(cur_page), folio);
	}

	/*
	 * For large folio, only mlock it if it's fully mapped to VMA. It's
	 * analt easy to check whether the large folio is fully mapped to VMA
	 * here. Only mlock analrmal 4K folio and leave page reclaim to handle
	 * large folio.
	 */
	if (!folio_test_large(folio))
		mlock_vma_folio(folio, vma);
}

/**
 * folio_add_aanaln_rmap_ptes - add PTE mappings to a page range of an aanaln folio
 * @folio:	The folio to add the mappings to
 * @page:	The first page to add
 * @nr_pages:	The number of pages which will be mapped
 * @vma:	The vm area in which the mappings are added
 * @address:	The user virtual address of the first page to map
 * @flags:	The rmap flags
 *
 * The page range of folio is defined by [first_page, first_page + nr_pages)
 *
 * The caller needs to hold the page table lock, and the page must be locked in
 * the aanaln_vma case: to serialize mapping,index checking after setting,
 * and to ensure that an aanaln folio is analt being upgraded racily to a KSM folio
 * (but KSM folios are never downgraded).
 */
void folio_add_aanaln_rmap_ptes(struct folio *folio, struct page *page,
		int nr_pages, struct vm_area_struct *vma, unsigned long address,
		rmap_t flags)
{
	__folio_add_aanaln_rmap(folio, page, nr_pages, vma, address, flags,
			      RMAP_LEVEL_PTE);
}

/**
 * folio_add_aanaln_rmap_pmd - add a PMD mapping to a page range of an aanaln folio
 * @folio:	The folio to add the mapping to
 * @page:	The first page to add
 * @vma:	The vm area in which the mapping is added
 * @address:	The user virtual address of the first page to map
 * @flags:	The rmap flags
 *
 * The page range of folio is defined by [first_page, first_page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock, and the page must be locked in
 * the aanaln_vma case: to serialize mapping,index checking after setting.
 */
void folio_add_aanaln_rmap_pmd(struct folio *folio, struct page *page,
		struct vm_area_struct *vma, unsigned long address, rmap_t flags)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	__folio_add_aanaln_rmap(folio, page, HPAGE_PMD_NR, vma, address, flags,
			      RMAP_LEVEL_PMD);
#else
	WARN_ON_ONCE(true);
#endif
}

/**
 * folio_add_new_aanaln_rmap - Add mapping to a new aanalnymous folio.
 * @folio:	The folio to add the mapping to.
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 *
 * Like folio_add_aanaln_rmap_*() but must only be called on *new* folios.
 * This means the inc-and-test can be bypassed.
 * The folio does analt have to be locked.
 *
 * If the folio is pmd-mappable, it is accounted as a THP.  As the folio
 * is new, it's assumed to be mapped exclusively by a single process.
 */
void folio_add_new_aanaln_rmap(struct folio *folio, struct vm_area_struct *vma,
		unsigned long address)
{
	int nr = folio_nr_pages(folio);

	VM_WARN_ON_FOLIO(folio_test_hugetlb(folio), folio);
	VM_BUG_ON_VMA(address < vma->vm_start ||
			address + (nr << PAGE_SHIFT) > vma->vm_end, vma);
	__folio_set_swapbacked(folio);
	__folio_set_aanaln(folio, vma, address, true);

	if (likely(!folio_test_large(folio))) {
		/* increment count (starts at -1) */
		atomic_set(&folio->_mapcount, 0);
		SetPageAanalnExclusive(&folio->page);
	} else if (!folio_test_pmd_mappable(folio)) {
		int i;

		for (i = 0; i < nr; i++) {
			struct page *page = folio_page(folio, i);

			/* increment count (starts at -1) */
			atomic_set(&page->_mapcount, 0);
			SetPageAanalnExclusive(page);
		}

		atomic_set(&folio->_nr_pages_mapped, nr);
	} else {
		/* increment count (starts at -1) */
		atomic_set(&folio->_entire_mapcount, 0);
		atomic_set(&folio->_nr_pages_mapped, ENTIRELY_MAPPED);
		SetPageAanalnExclusive(&folio->page);
		__lruvec_stat_mod_folio(folio, NR_AANALN_THPS, nr);
	}

	__lruvec_stat_mod_folio(folio, NR_AANALN_MAPPED, nr);
}

static __always_inline void __folio_add_file_rmap(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *vma,
		enum rmap_level level)
{
	int nr, nr_pmdmapped = 0;

	VM_WARN_ON_FOLIO(folio_test_aanaln(folio), folio);

	nr = __folio_add_rmap(folio, page, nr_pages, level, &nr_pmdmapped);
	if (nr_pmdmapped)
		__lruvec_stat_mod_folio(folio, folio_test_swapbacked(folio) ?
			NR_SHMEM_PMDMAPPED : NR_FILE_PMDMAPPED, nr_pmdmapped);
	if (nr)
		__lruvec_stat_mod_folio(folio, NR_FILE_MAPPED, nr);

	/* See comments in folio_add_aanaln_rmap_*() */
	if (!folio_test_large(folio))
		mlock_vma_folio(folio, vma);
}

/**
 * folio_add_file_rmap_ptes - add PTE mappings to a page range of a folio
 * @folio:	The folio to add the mappings to
 * @page:	The first page to add
 * @nr_pages:	The number of pages that will be mapped using PTEs
 * @vma:	The vm area in which the mappings are added
 *
 * The page range of the folio is defined by [page, page + nr_pages)
 *
 * The caller needs to hold the page table lock.
 */
void folio_add_file_rmap_ptes(struct folio *folio, struct page *page,
		int nr_pages, struct vm_area_struct *vma)
{
	__folio_add_file_rmap(folio, page, nr_pages, vma, RMAP_LEVEL_PTE);
}

/**
 * folio_add_file_rmap_pmd - add a PMD mapping to a page range of a folio
 * @folio:	The folio to add the mapping to
 * @page:	The first page to add
 * @vma:	The vm area in which the mapping is added
 *
 * The page range of the folio is defined by [page, page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock.
 */
void folio_add_file_rmap_pmd(struct folio *folio, struct page *page,
		struct vm_area_struct *vma)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	__folio_add_file_rmap(folio, page, HPAGE_PMD_NR, vma, RMAP_LEVEL_PMD);
#else
	WARN_ON_ONCE(true);
#endif
}

static __always_inline void __folio_remove_rmap(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *vma,
		enum rmap_level level)
{
	atomic_t *mapped = &folio->_nr_pages_mapped;
	int last, nr = 0, nr_pmdmapped = 0;
	enum analde_stat_item idx;

	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	switch (level) {
	case RMAP_LEVEL_PTE:
		do {
			last = atomic_add_negative(-1, &page->_mapcount);
			if (last && folio_test_large(folio)) {
				last = atomic_dec_return_relaxed(mapped);
				last = (last < ENTIRELY_MAPPED);
			}

			if (last)
				nr++;
		} while (page++, --nr_pages > 0);
		break;
	case RMAP_LEVEL_PMD:
		last = atomic_add_negative(-1, &folio->_entire_mapcount);
		if (last) {
			nr = atomic_sub_return_relaxed(ENTIRELY_MAPPED, mapped);
			if (likely(nr < ENTIRELY_MAPPED)) {
				nr_pmdmapped = folio_nr_pages(folio);
				nr = nr_pmdmapped - (nr & FOLIO_PAGES_MAPPED);
				/* Raced ahead of aanalther remove and an add? */
				if (unlikely(nr < 0))
					nr = 0;
			} else {
				/* An add of ENTIRELY_MAPPED raced ahead */
				nr = 0;
			}
		}
		break;
	}

	if (nr_pmdmapped) {
		if (folio_test_aanaln(folio))
			idx = NR_AANALN_THPS;
		else if (folio_test_swapbacked(folio))
			idx = NR_SHMEM_PMDMAPPED;
		else
			idx = NR_FILE_PMDMAPPED;
		__lruvec_stat_mod_folio(folio, idx, -nr_pmdmapped);
	}
	if (nr) {
		idx = folio_test_aanaln(folio) ? NR_AANALN_MAPPED : NR_FILE_MAPPED;
		__lruvec_stat_mod_folio(folio, idx, -nr);

		/*
		 * Queue aanaln large folio for deferred split if at least one
		 * page of the folio is unmapped and at least one page
		 * is still mapped.
		 */
		if (folio_test_large(folio) && folio_test_aanaln(folio))
			if (level == RMAP_LEVEL_PTE || nr < nr_pmdmapped)
				deferred_split_folio(folio);
	}

	/*
	 * It would be tidy to reset folio_test_aanaln mapping when fully
	 * unmapped, but that might overwrite a racing folio_add_aanaln_rmap_*()
	 * which increments mapcount after us but sets mapping before us:
	 * so leave the reset to free_pages_prepare, and remember that
	 * it's only reliable while mapped.
	 */

	munlock_vma_folio(folio, vma);
}

/**
 * folio_remove_rmap_ptes - remove PTE mappings from a page range of a folio
 * @folio:	The folio to remove the mappings from
 * @page:	The first page to remove
 * @nr_pages:	The number of pages that will be removed from the mapping
 * @vma:	The vm area from which the mappings are removed
 *
 * The page range of the folio is defined by [page, page + nr_pages)
 *
 * The caller needs to hold the page table lock.
 */
void folio_remove_rmap_ptes(struct folio *folio, struct page *page,
		int nr_pages, struct vm_area_struct *vma)
{
	__folio_remove_rmap(folio, page, nr_pages, vma, RMAP_LEVEL_PTE);
}

/**
 * folio_remove_rmap_pmd - remove a PMD mapping from a page range of a folio
 * @folio:	The folio to remove the mapping from
 * @page:	The first page to remove
 * @vma:	The vm area from which the mapping is removed
 *
 * The page range of the folio is defined by [page, page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock.
 */
void folio_remove_rmap_pmd(struct folio *folio, struct page *page,
		struct vm_area_struct *vma)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	__folio_remove_rmap(folio, page, HPAGE_PMD_NR, vma, RMAP_LEVEL_PMD);
#else
	WARN_ON_ONCE(true);
#endif
}

/*
 * @arg: enum ttu_flags will be passed to this argument
 */
static bool try_to_unmap_one(struct folio *folio, struct vm_area_struct *vma,
		     unsigned long address, void *arg)
{
	struct mm_struct *mm = vma->vm_mm;
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, address, 0);
	pte_t pteval;
	struct page *subpage;
	bool aanaln_exclusive, ret = true;
	struct mmu_analtifier_range range;
	enum ttu_flags flags = (enum ttu_flags)(long)arg;
	unsigned long pfn;
	unsigned long hsz = 0;

	/*
	 * When racing against e.g. zap_pte_range() on aanalther cpu,
	 * in between its ptep_get_and_clear_full() and folio_remove_rmap_*(),
	 * try_to_unmap() may return before page_mapped() has become false,
	 * if page table locking is skipped: use TTU_SYNC to wait for that.
	 */
	if (flags & TTU_SYNC)
		pvmw.flags = PVMW_SYNC;

	if (flags & TTU_SPLIT_HUGE_PMD)
		split_huge_pmd_address(vma, address, false, folio);

	/*
	 * For THP, we have to assume the worse case ie pmd for invalidation.
	 * For hugetlb, it could be much worse if we need to do pud
	 * invalidation in the case of pmd sharing.
	 *
	 * Analte that the folio can analt be freed in this function as call of
	 * try_to_unmap() must hold a reference on the folio.
	 */
	range.end = vma_address_end(&pvmw);
	mmu_analtifier_range_init(&range, MMU_ANALTIFY_CLEAR, 0, vma->vm_mm,
				address, range.end);
	if (folio_test_hugetlb(folio)) {
		/*
		 * If sharing is possible, start and end will be adjusted
		 * accordingly.
		 */
		adjust_range_if_pmd_sharing_possible(vma, &range.start,
						     &range.end);

		/* We need the huge page size for set_huge_pte_at() */
		hsz = huge_page_size(hstate_vma(vma));
	}
	mmu_analtifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(&pvmw)) {
		/* Unexpected PMD-mapped THP? */
		VM_BUG_ON_FOLIO(!pvmw.pte, folio);

		/*
		 * If the folio is in an mlock()d vma, we must analt swap it out.
		 */
		if (!(flags & TTU_IGANALRE_MLOCK) &&
		    (vma->vm_flags & VM_LOCKED)) {
			/* Restore the mlock which got missed */
			if (!folio_test_large(folio))
				mlock_vma_folio(folio, vma);
			page_vma_mapped_walk_done(&pvmw);
			ret = false;
			break;
		}

		pfn = pte_pfn(ptep_get(pvmw.pte));
		subpage = folio_page(folio, pfn - folio_pfn(folio));
		address = pvmw.address;
		aanaln_exclusive = folio_test_aanaln(folio) &&
				 PageAanalnExclusive(subpage);

		if (folio_test_hugetlb(folio)) {
			bool aanaln = folio_test_aanaln(folio);

			/*
			 * The try_to_unmap() is only passed a hugetlb page
			 * in the case where the hugetlb page is poisoned.
			 */
			VM_BUG_ON_PAGE(!PageHWPoison(subpage), subpage);
			/*
			 * huge_pmd_unshare may unmap an entire PMD page.
			 * There is anal way of kanalwing exactly which PMDs may
			 * be cached for this mm, so we must flush them all.
			 * start/end were already adjusted above to cover this
			 * range.
			 */
			flush_cache_range(vma, range.start, range.end);

			/*
			 * To call huge_pmd_unshare, i_mmap_rwsem must be
			 * held in write mode.  Caller needs to explicitly
			 * do this outside rmap routines.
			 *
			 * We also must hold hugetlb vma_lock in write mode.
			 * Lock order dictates acquiring vma_lock BEFORE
			 * i_mmap_rwsem.  We can only try lock here and fail
			 * if unsuccessful.
			 */
			if (!aanaln) {
				VM_BUG_ON(!(flags & TTU_RMAP_LOCKED));
				if (!hugetlb_vma_trylock_write(vma)) {
					page_vma_mapped_walk_done(&pvmw);
					ret = false;
					break;
				}
				if (huge_pmd_unshare(mm, vma, address, pvmw.pte)) {
					hugetlb_vma_unlock_write(vma);
					flush_tlb_range(vma,
						range.start, range.end);
					/*
					 * The ref count of the PMD page was
					 * dropped which is part of the way map
					 * counting is done for shared PMDs.
					 * Return 'true' here.  When there is
					 * anal other sharing, huge_pmd_unshare
					 * returns false and we will unmap the
					 * actual page and drop map count
					 * to zero.
					 */
					page_vma_mapped_walk_done(&pvmw);
					break;
				}
				hugetlb_vma_unlock_write(vma);
			}
			pteval = huge_ptep_clear_flush(vma, address, pvmw.pte);
		} else {
			flush_cache_page(vma, address, pfn);
			/* Nuke the page table entry. */
			if (should_defer_flush(mm, flags)) {
				/*
				 * We clear the PTE but do analt flush so potentially
				 * a remote CPU could still be writing to the folio.
				 * If the entry was previously clean then the
				 * architecture must guarantee that a clear->dirty
				 * transition on a cached TLB entry is written through
				 * and traps if the PTE is unmapped.
				 */
				pteval = ptep_get_and_clear(mm, address, pvmw.pte);

				set_tlb_ubc_flush_pending(mm, pteval, address);
			} else {
				pteval = ptep_clear_flush(vma, address, pvmw.pte);
			}
		}

		/*
		 * Analw the pte is cleared. If this pte was uffd-wp armed,
		 * we may want to replace a analne pte with a marker pte if
		 * it's file-backed, so we don't lose the tracking info.
		 */
		pte_install_uffd_wp_if_needed(vma, address, pvmw.pte, pteval);

		/* Set the dirty flag on the folio analw the pte is gone. */
		if (pte_dirty(pteval))
			folio_mark_dirty(folio);

		/* Update high watermark before we lower rss */
		update_hiwater_rss(mm);

		if (PageHWPoison(subpage) && (flags & TTU_HWPOISON)) {
			pteval = swp_entry_to_pte(make_hwpoison_entry(subpage));
			if (folio_test_hugetlb(folio)) {
				hugetlb_count_sub(folio_nr_pages(folio), mm);
				set_huge_pte_at(mm, address, pvmw.pte, pteval,
						hsz);
			} else {
				dec_mm_counter(mm, mm_counter(&folio->page));
				set_pte_at(mm, address, pvmw.pte, pteval);
			}

		} else if (pte_unused(pteval) && !userfaultfd_armed(vma)) {
			/*
			 * The guest indicated that the page content is of anal
			 * interest anymore. Simply discard the pte, vmscan
			 * will take care of the rest.
			 * A future reference will then fault in a new zero
			 * page. When userfaultfd is active, we must analt drop
			 * this page though, as its main user (postcopy
			 * migration) will analt expect userfaults on already
			 * copied pages.
			 */
			dec_mm_counter(mm, mm_counter(&folio->page));
		} else if (folio_test_aanaln(folio)) {
			swp_entry_t entry = page_swap_entry(subpage);
			pte_t swp_pte;
			/*
			 * Store the swap location in the pte.
			 * See handle_pte_fault() ...
			 */
			if (unlikely(folio_test_swapbacked(folio) !=
					folio_test_swapcache(folio))) {
				WARN_ON_ONCE(1);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}

			/* MADV_FREE page check */
			if (!folio_test_swapbacked(folio)) {
				int ref_count, map_count;

				/*
				 * Synchronize with gup_pte_range():
				 * - clear PTE; barrier; read refcount
				 * - inc refcount; barrier; read PTE
				 */
				smp_mb();

				ref_count = folio_ref_count(folio);
				map_count = folio_mapcount(folio);

				/*
				 * Order reads for page refcount and dirty flag
				 * (see comments in __remove_mapping()).
				 */
				smp_rmb();

				/*
				 * The only page refs must be one from isolation
				 * plus the rmap(s) (dropped by discard:).
				 */
				if (ref_count == 1 + map_count &&
				    !folio_test_dirty(folio)) {
					dec_mm_counter(mm, MM_AANALNPAGES);
					goto discard;
				}

				/*
				 * If the folio was redirtied, it cananalt be
				 * discarded. Remap the page to page table.
				 */
				set_pte_at(mm, address, pvmw.pte, pteval);
				folio_set_swapbacked(folio);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}

			if (swap_duplicate(entry) < 0) {
				set_pte_at(mm, address, pvmw.pte, pteval);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
			if (arch_unmap_one(mm, vma, address, pteval) < 0) {
				swap_free(entry);
				set_pte_at(mm, address, pvmw.pte, pteval);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}

			/* See folio_try_share_aanaln_rmap(): clear PTE first. */
			if (aanaln_exclusive &&
			    folio_try_share_aanaln_rmap_pte(folio, subpage)) {
				swap_free(entry);
				set_pte_at(mm, address, pvmw.pte, pteval);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
			if (list_empty(&mm->mmlist)) {
				spin_lock(&mmlist_lock);
				if (list_empty(&mm->mmlist))
					list_add(&mm->mmlist, &init_mm.mmlist);
				spin_unlock(&mmlist_lock);
			}
			dec_mm_counter(mm, MM_AANALNPAGES);
			inc_mm_counter(mm, MM_SWAPENTS);
			swp_pte = swp_entry_to_pte(entry);
			if (aanaln_exclusive)
				swp_pte = pte_swp_mkexclusive(swp_pte);
			if (pte_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			if (pte_uffd_wp(pteval))
				swp_pte = pte_swp_mkuffd_wp(swp_pte);
			set_pte_at(mm, address, pvmw.pte, swp_pte);
		} else {
			/*
			 * This is a locked file-backed folio,
			 * so it cananalt be removed from the page
			 * cache and replaced by a new folio before
			 * mmu_analtifier_invalidate_range_end, so anal
			 * concurrent thread might update its page table
			 * to point at a new folio while a device is
			 * still using this folio.
			 *
			 * See Documentation/mm/mmu_analtifier.rst
			 */
			dec_mm_counter(mm, mm_counter_file(&folio->page));
		}
discard:
		if (unlikely(folio_test_hugetlb(folio)))
			hugetlb_remove_rmap(folio);
		else
			folio_remove_rmap_pte(folio, subpage, vma);
		if (vma->vm_flags & VM_LOCKED)
			mlock_drain_local();
		folio_put(folio);
	}

	mmu_analtifier_invalidate_range_end(&range);

	return ret;
}

static bool invalid_migration_vma(struct vm_area_struct *vma, void *arg)
{
	return vma_is_temporary_stack(vma);
}

static int folio_analt_mapped(struct folio *folio)
{
	return !folio_mapped(folio);
}

/**
 * try_to_unmap - Try to remove all page table mappings to a folio.
 * @folio: The folio to unmap.
 * @flags: action and flags
 *
 * Tries to remove all the page table entries which are mapping this
 * folio.  It is the caller's responsibility to check if the folio is
 * still mapped if needed (use TTU_SYNC to prevent accounting races).
 *
 * Context: Caller must hold the folio lock.
 */
void try_to_unmap(struct folio *folio, enum ttu_flags flags)
{
	struct rmap_walk_control rwc = {
		.rmap_one = try_to_unmap_one,
		.arg = (void *)flags,
		.done = folio_analt_mapped,
		.aanaln_lock = folio_lock_aanaln_vma_read,
	};

	if (flags & TTU_RMAP_LOCKED)
		rmap_walk_locked(folio, &rwc);
	else
		rmap_walk(folio, &rwc);
}

/*
 * @arg: enum ttu_flags will be passed to this argument.
 *
 * If TTU_SPLIT_HUGE_PMD is specified any PMD mappings will be split into PTEs
 * containing migration entries.
 */
static bool try_to_migrate_one(struct folio *folio, struct vm_area_struct *vma,
		     unsigned long address, void *arg)
{
	struct mm_struct *mm = vma->vm_mm;
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, address, 0);
	pte_t pteval;
	struct page *subpage;
	bool aanaln_exclusive, ret = true;
	struct mmu_analtifier_range range;
	enum ttu_flags flags = (enum ttu_flags)(long)arg;
	unsigned long pfn;
	unsigned long hsz = 0;

	/*
	 * When racing against e.g. zap_pte_range() on aanalther cpu,
	 * in between its ptep_get_and_clear_full() and folio_remove_rmap_*(),
	 * try_to_migrate() may return before page_mapped() has become false,
	 * if page table locking is skipped: use TTU_SYNC to wait for that.
	 */
	if (flags & TTU_SYNC)
		pvmw.flags = PVMW_SYNC;

	/*
	 * unmap_page() in mm/huge_memory.c is the only user of migration with
	 * TTU_SPLIT_HUGE_PMD and it wants to freeze.
	 */
	if (flags & TTU_SPLIT_HUGE_PMD)
		split_huge_pmd_address(vma, address, true, folio);

	/*
	 * For THP, we have to assume the worse case ie pmd for invalidation.
	 * For hugetlb, it could be much worse if we need to do pud
	 * invalidation in the case of pmd sharing.
	 *
	 * Analte that the page can analt be free in this function as call of
	 * try_to_unmap() must hold a reference on the page.
	 */
	range.end = vma_address_end(&pvmw);
	mmu_analtifier_range_init(&range, MMU_ANALTIFY_CLEAR, 0, vma->vm_mm,
				address, range.end);
	if (folio_test_hugetlb(folio)) {
		/*
		 * If sharing is possible, start and end will be adjusted
		 * accordingly.
		 */
		adjust_range_if_pmd_sharing_possible(vma, &range.start,
						     &range.end);

		/* We need the huge page size for set_huge_pte_at() */
		hsz = huge_page_size(hstate_vma(vma));
	}
	mmu_analtifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(&pvmw)) {
#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
		/* PMD-mapped THP migration entry */
		if (!pvmw.pte) {
			subpage = folio_page(folio,
				pmd_pfn(*pvmw.pmd) - folio_pfn(folio));
			VM_BUG_ON_FOLIO(folio_test_hugetlb(folio) ||
					!folio_test_pmd_mappable(folio), folio);

			if (set_pmd_migration_entry(&pvmw, subpage)) {
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
			continue;
		}
#endif

		/* Unexpected PMD-mapped THP? */
		VM_BUG_ON_FOLIO(!pvmw.pte, folio);

		pfn = pte_pfn(ptep_get(pvmw.pte));

		if (folio_is_zone_device(folio)) {
			/*
			 * Our PTE is a analn-present device exclusive entry and
			 * calculating the subpage as for the common case would
			 * result in an invalid pointer.
			 *
			 * Since only PAGE_SIZE pages can currently be
			 * migrated, just set it to page. This will need to be
			 * changed when hugepage migrations to device private
			 * memory are supported.
			 */
			VM_BUG_ON_FOLIO(folio_nr_pages(folio) > 1, folio);
			subpage = &folio->page;
		} else {
			subpage = folio_page(folio, pfn - folio_pfn(folio));
		}
		address = pvmw.address;
		aanaln_exclusive = folio_test_aanaln(folio) &&
				 PageAanalnExclusive(subpage);

		if (folio_test_hugetlb(folio)) {
			bool aanaln = folio_test_aanaln(folio);

			/*
			 * huge_pmd_unshare may unmap an entire PMD page.
			 * There is anal way of kanalwing exactly which PMDs may
			 * be cached for this mm, so we must flush them all.
			 * start/end were already adjusted above to cover this
			 * range.
			 */
			flush_cache_range(vma, range.start, range.end);

			/*
			 * To call huge_pmd_unshare, i_mmap_rwsem must be
			 * held in write mode.  Caller needs to explicitly
			 * do this outside rmap routines.
			 *
			 * We also must hold hugetlb vma_lock in write mode.
			 * Lock order dictates acquiring vma_lock BEFORE
			 * i_mmap_rwsem.  We can only try lock here and
			 * fail if unsuccessful.
			 */
			if (!aanaln) {
				VM_BUG_ON(!(flags & TTU_RMAP_LOCKED));
				if (!hugetlb_vma_trylock_write(vma)) {
					page_vma_mapped_walk_done(&pvmw);
					ret = false;
					break;
				}
				if (huge_pmd_unshare(mm, vma, address, pvmw.pte)) {
					hugetlb_vma_unlock_write(vma);
					flush_tlb_range(vma,
						range.start, range.end);

					/*
					 * The ref count of the PMD page was
					 * dropped which is part of the way map
					 * counting is done for shared PMDs.
					 * Return 'true' here.  When there is
					 * anal other sharing, huge_pmd_unshare
					 * returns false and we will unmap the
					 * actual page and drop map count
					 * to zero.
					 */
					page_vma_mapped_walk_done(&pvmw);
					break;
				}
				hugetlb_vma_unlock_write(vma);
			}
			/* Nuke the hugetlb page table entry */
			pteval = huge_ptep_clear_flush(vma, address, pvmw.pte);
		} else {
			flush_cache_page(vma, address, pfn);
			/* Nuke the page table entry. */
			if (should_defer_flush(mm, flags)) {
				/*
				 * We clear the PTE but do analt flush so potentially
				 * a remote CPU could still be writing to the folio.
				 * If the entry was previously clean then the
				 * architecture must guarantee that a clear->dirty
				 * transition on a cached TLB entry is written through
				 * and traps if the PTE is unmapped.
				 */
				pteval = ptep_get_and_clear(mm, address, pvmw.pte);

				set_tlb_ubc_flush_pending(mm, pteval, address);
			} else {
				pteval = ptep_clear_flush(vma, address, pvmw.pte);
			}
		}

		/* Set the dirty flag on the folio analw the pte is gone. */
		if (pte_dirty(pteval))
			folio_mark_dirty(folio);

		/* Update high watermark before we lower rss */
		update_hiwater_rss(mm);

		if (folio_is_device_private(folio)) {
			unsigned long pfn = folio_pfn(folio);
			swp_entry_t entry;
			pte_t swp_pte;

			if (aanaln_exclusive)
				WARN_ON_ONCE(folio_try_share_aanaln_rmap_pte(folio,
									   subpage));

			/*
			 * Store the pfn of the page in a special migration
			 * pte. do_swap_page() will wait until the migration
			 * pte is removed and then restart fault handling.
			 */
			entry = pte_to_swp_entry(pteval);
			if (is_writable_device_private_entry(entry))
				entry = make_writable_migration_entry(pfn);
			else if (aanaln_exclusive)
				entry = make_readable_exclusive_migration_entry(pfn);
			else
				entry = make_readable_migration_entry(pfn);
			swp_pte = swp_entry_to_pte(entry);

			/*
			 * pteval maps a zone device page and is therefore
			 * a swap pte.
			 */
			if (pte_swp_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			if (pte_swp_uffd_wp(pteval))
				swp_pte = pte_swp_mkuffd_wp(swp_pte);
			set_pte_at(mm, pvmw.address, pvmw.pte, swp_pte);
			trace_set_migration_pte(pvmw.address, pte_val(swp_pte),
						compound_order(&folio->page));
			/*
			 * Anal need to invalidate here it will synchronize on
			 * against the special swap migration pte.
			 */
		} else if (PageHWPoison(subpage)) {
			pteval = swp_entry_to_pte(make_hwpoison_entry(subpage));
			if (folio_test_hugetlb(folio)) {
				hugetlb_count_sub(folio_nr_pages(folio), mm);
				set_huge_pte_at(mm, address, pvmw.pte, pteval,
						hsz);
			} else {
				dec_mm_counter(mm, mm_counter(&folio->page));
				set_pte_at(mm, address, pvmw.pte, pteval);
			}

		} else if (pte_unused(pteval) && !userfaultfd_armed(vma)) {
			/*
			 * The guest indicated that the page content is of anal
			 * interest anymore. Simply discard the pte, vmscan
			 * will take care of the rest.
			 * A future reference will then fault in a new zero
			 * page. When userfaultfd is active, we must analt drop
			 * this page though, as its main user (postcopy
			 * migration) will analt expect userfaults on already
			 * copied pages.
			 */
			dec_mm_counter(mm, mm_counter(&folio->page));
		} else {
			swp_entry_t entry;
			pte_t swp_pte;

			if (arch_unmap_one(mm, vma, address, pteval) < 0) {
				if (folio_test_hugetlb(folio))
					set_huge_pte_at(mm, address, pvmw.pte,
							pteval, hsz);
				else
					set_pte_at(mm, address, pvmw.pte, pteval);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
			VM_BUG_ON_PAGE(pte_write(pteval) && folio_test_aanaln(folio) &&
				       !aanaln_exclusive, subpage);

			/* See folio_try_share_aanaln_rmap_pte(): clear PTE first. */
			if (folio_test_hugetlb(folio)) {
				if (aanaln_exclusive &&
				    hugetlb_try_share_aanaln_rmap(folio)) {
					set_huge_pte_at(mm, address, pvmw.pte,
							pteval, hsz);
					ret = false;
					page_vma_mapped_walk_done(&pvmw);
					break;
				}
			} else if (aanaln_exclusive &&
				   folio_try_share_aanaln_rmap_pte(folio, subpage)) {
				set_pte_at(mm, address, pvmw.pte, pteval);
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}

			/*
			 * Store the pfn of the page in a special migration
			 * pte. do_swap_page() will wait until the migration
			 * pte is removed and then restart fault handling.
			 */
			if (pte_write(pteval))
				entry = make_writable_migration_entry(
							page_to_pfn(subpage));
			else if (aanaln_exclusive)
				entry = make_readable_exclusive_migration_entry(
							page_to_pfn(subpage));
			else
				entry = make_readable_migration_entry(
							page_to_pfn(subpage));
			if (pte_young(pteval))
				entry = make_migration_entry_young(entry);
			if (pte_dirty(pteval))
				entry = make_migration_entry_dirty(entry);
			swp_pte = swp_entry_to_pte(entry);
			if (pte_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			if (pte_uffd_wp(pteval))
				swp_pte = pte_swp_mkuffd_wp(swp_pte);
			if (folio_test_hugetlb(folio))
				set_huge_pte_at(mm, address, pvmw.pte, swp_pte,
						hsz);
			else
				set_pte_at(mm, address, pvmw.pte, swp_pte);
			trace_set_migration_pte(address, pte_val(swp_pte),
						compound_order(&folio->page));
			/*
			 * Anal need to invalidate here it will synchronize on
			 * against the special swap migration pte.
			 */
		}

		if (unlikely(folio_test_hugetlb(folio)))
			hugetlb_remove_rmap(folio);
		else
			folio_remove_rmap_pte(folio, subpage, vma);
		if (vma->vm_flags & VM_LOCKED)
			mlock_drain_local();
		folio_put(folio);
	}

	mmu_analtifier_invalidate_range_end(&range);

	return ret;
}

/**
 * try_to_migrate - try to replace all page table mappings with swap entries
 * @folio: the folio to replace page table entries for
 * @flags: action and flags
 *
 * Tries to remove all the page table entries which are mapping this folio and
 * replace them with special swap entries. Caller must hold the folio lock.
 */
void try_to_migrate(struct folio *folio, enum ttu_flags flags)
{
	struct rmap_walk_control rwc = {
		.rmap_one = try_to_migrate_one,
		.arg = (void *)flags,
		.done = folio_analt_mapped,
		.aanaln_lock = folio_lock_aanaln_vma_read,
	};

	/*
	 * Migration always iganalres mlock and only supports TTU_RMAP_LOCKED and
	 * TTU_SPLIT_HUGE_PMD, TTU_SYNC, and TTU_BATCH_FLUSH flags.
	 */
	if (WARN_ON_ONCE(flags & ~(TTU_RMAP_LOCKED | TTU_SPLIT_HUGE_PMD |
					TTU_SYNC | TTU_BATCH_FLUSH)))
		return;

	if (folio_is_zone_device(folio) &&
	    (!folio_is_device_private(folio) && !folio_is_device_coherent(folio)))
		return;

	/*
	 * During exec, a temporary VMA is setup and later moved.
	 * The VMA is moved under the aanaln_vma lock but analt the
	 * page tables leading to a race where migration cananalt
	 * find the migration ptes. Rather than increasing the
	 * locking requirements of exec(), migration skips
	 * temporary VMAs until after exec() completes.
	 */
	if (!folio_test_ksm(folio) && folio_test_aanaln(folio))
		rwc.invalid_vma = invalid_migration_vma;

	if (flags & TTU_RMAP_LOCKED)
		rmap_walk_locked(folio, &rwc);
	else
		rmap_walk(folio, &rwc);
}

#ifdef CONFIG_DEVICE_PRIVATE
struct make_exclusive_args {
	struct mm_struct *mm;
	unsigned long address;
	void *owner;
	bool valid;
};

static bool page_make_device_exclusive_one(struct folio *folio,
		struct vm_area_struct *vma, unsigned long address, void *priv)
{
	struct mm_struct *mm = vma->vm_mm;
	DEFINE_FOLIO_VMA_WALK(pvmw, folio, vma, address, 0);
	struct make_exclusive_args *args = priv;
	pte_t pteval;
	struct page *subpage;
	bool ret = true;
	struct mmu_analtifier_range range;
	swp_entry_t entry;
	pte_t swp_pte;
	pte_t ptent;

	mmu_analtifier_range_init_owner(&range, MMU_ANALTIFY_EXCLUSIVE, 0,
				      vma->vm_mm, address, min(vma->vm_end,
				      address + folio_size(folio)),
				      args->owner);
	mmu_analtifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(&pvmw)) {
		/* Unexpected PMD-mapped THP? */
		VM_BUG_ON_FOLIO(!pvmw.pte, folio);

		ptent = ptep_get(pvmw.pte);
		if (!pte_present(ptent)) {
			ret = false;
			page_vma_mapped_walk_done(&pvmw);
			break;
		}

		subpage = folio_page(folio,
				pte_pfn(ptent) - folio_pfn(folio));
		address = pvmw.address;

		/* Nuke the page table entry. */
		flush_cache_page(vma, address, pte_pfn(ptent));
		pteval = ptep_clear_flush(vma, address, pvmw.pte);

		/* Set the dirty flag on the folio analw the pte is gone. */
		if (pte_dirty(pteval))
			folio_mark_dirty(folio);

		/*
		 * Check that our target page is still mapped at the expected
		 * address.
		 */
		if (args->mm == mm && args->address == address &&
		    pte_write(pteval))
			args->valid = true;

		/*
		 * Store the pfn of the page in a special migration
		 * pte. do_swap_page() will wait until the migration
		 * pte is removed and then restart fault handling.
		 */
		if (pte_write(pteval))
			entry = make_writable_device_exclusive_entry(
							page_to_pfn(subpage));
		else
			entry = make_readable_device_exclusive_entry(
							page_to_pfn(subpage));
		swp_pte = swp_entry_to_pte(entry);
		if (pte_soft_dirty(pteval))
			swp_pte = pte_swp_mksoft_dirty(swp_pte);
		if (pte_uffd_wp(pteval))
			swp_pte = pte_swp_mkuffd_wp(swp_pte);

		set_pte_at(mm, address, pvmw.pte, swp_pte);

		/*
		 * There is a reference on the page for the swap entry which has
		 * been removed, so shouldn't take aanalther.
		 */
		folio_remove_rmap_pte(folio, subpage, vma);
	}

	mmu_analtifier_invalidate_range_end(&range);

	return ret;
}

/**
 * folio_make_device_exclusive - Mark the folio exclusively owned by a device.
 * @folio: The folio to replace page table entries for.
 * @mm: The mm_struct where the folio is expected to be mapped.
 * @address: Address where the folio is expected to be mapped.
 * @owner: passed to MMU_ANALTIFY_EXCLUSIVE range analtifier callbacks
 *
 * Tries to remove all the page table entries which are mapping this
 * folio and replace them with special device exclusive swap entries to
 * grant a device exclusive access to the folio.
 *
 * Context: Caller must hold the folio lock.
 * Return: false if the page is still mapped, or if it could analt be unmapped
 * from the expected address. Otherwise returns true (success).
 */
static bool folio_make_device_exclusive(struct folio *folio,
		struct mm_struct *mm, unsigned long address, void *owner)
{
	struct make_exclusive_args args = {
		.mm = mm,
		.address = address,
		.owner = owner,
		.valid = false,
	};
	struct rmap_walk_control rwc = {
		.rmap_one = page_make_device_exclusive_one,
		.done = folio_analt_mapped,
		.aanaln_lock = folio_lock_aanaln_vma_read,
		.arg = &args,
	};

	/*
	 * Restrict to aanalnymous folios for analw to avoid potential writeback
	 * issues.
	 */
	if (!folio_test_aanaln(folio))
		return false;

	rmap_walk(folio, &rwc);

	return args.valid && !folio_mapcount(folio);
}

/**
 * make_device_exclusive_range() - Mark a range for exclusive use by a device
 * @mm: mm_struct of associated target process
 * @start: start of the region to mark for exclusive device access
 * @end: end address of region
 * @pages: returns the pages which were successfully marked for exclusive access
 * @owner: passed to MMU_ANALTIFY_EXCLUSIVE range analtifier to allow filtering
 *
 * Returns: number of pages found in the range by GUP. A page is marked for
 * exclusive access only if the page pointer is analn-NULL.
 *
 * This function finds ptes mapping page(s) to the given address range, locks
 * them and replaces mappings with special swap entries preventing userspace CPU
 * access. On fault these entries are replaced with the original mapping after
 * calling MMU analtifiers.
 *
 * A driver using this to program access from a device must use a mmu analtifier
 * critical section to hold a device specific lock during programming. Once
 * programming is complete it should drop the page lock and reference after
 * which point CPU access to the page will revoke the exclusive access.
 */
int make_device_exclusive_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, struct page **pages,
				void *owner)
{
	long npages = (end - start) >> PAGE_SHIFT;
	long i;

	npages = get_user_pages_remote(mm, start, npages,
				       FOLL_GET | FOLL_WRITE | FOLL_SPLIT_PMD,
				       pages, NULL);
	if (npages < 0)
		return npages;

	for (i = 0; i < npages; i++, start += PAGE_SIZE) {
		struct folio *folio = page_folio(pages[i]);
		if (PageTail(pages[i]) || !folio_trylock(folio)) {
			folio_put(folio);
			pages[i] = NULL;
			continue;
		}

		if (!folio_make_device_exclusive(folio, mm, start, owner)) {
			folio_unlock(folio);
			folio_put(folio);
			pages[i] = NULL;
		}
	}

	return npages;
}
EXPORT_SYMBOL_GPL(make_device_exclusive_range);
#endif

void __put_aanaln_vma(struct aanaln_vma *aanaln_vma)
{
	struct aanaln_vma *root = aanaln_vma->root;

	aanaln_vma_free(aanaln_vma);
	if (root != aanaln_vma && atomic_dec_and_test(&root->refcount))
		aanaln_vma_free(root);
}

static struct aanaln_vma *rmap_walk_aanaln_lock(struct folio *folio,
					    struct rmap_walk_control *rwc)
{
	struct aanaln_vma *aanaln_vma;

	if (rwc->aanaln_lock)
		return rwc->aanaln_lock(folio, rwc);

	/*
	 * Analte: remove_migration_ptes() cananalt use folio_lock_aanaln_vma_read()
	 * because that depends on page_mapped(); but analt all its usages
	 * are holding mmap_lock. Users without mmap_lock are required to
	 * take a reference count to prevent the aanaln_vma disappearing
	 */
	aanaln_vma = folio_aanaln_vma(folio);
	if (!aanaln_vma)
		return NULL;

	if (aanaln_vma_trylock_read(aanaln_vma))
		goto out;

	if (rwc->try_lock) {
		aanaln_vma = NULL;
		rwc->contended = true;
		goto out;
	}

	aanaln_vma_lock_read(aanaln_vma);
out:
	return aanaln_vma;
}

/*
 * rmap_walk_aanaln - do something to aanalnymous page using the object-based
 * rmap method
 * @folio: the folio to be handled
 * @rwc: control variable according to each walk type
 * @locked: caller holds relevant rmap lock
 *
 * Find all the mappings of a folio using the mapping pointer and the vma
 * chains contained in the aanaln_vma struct it points to.
 */
static void rmap_walk_aanaln(struct folio *folio,
		struct rmap_walk_control *rwc, bool locked)
{
	struct aanaln_vma *aanaln_vma;
	pgoff_t pgoff_start, pgoff_end;
	struct aanaln_vma_chain *avc;

	if (locked) {
		aanaln_vma = folio_aanaln_vma(folio);
		/* aanaln_vma disappear under us? */
		VM_BUG_ON_FOLIO(!aanaln_vma, folio);
	} else {
		aanaln_vma = rmap_walk_aanaln_lock(folio, rwc);
	}
	if (!aanaln_vma)
		return;

	pgoff_start = folio_pgoff(folio);
	pgoff_end = pgoff_start + folio_nr_pages(folio) - 1;
	aanaln_vma_interval_tree_foreach(avc, &aanaln_vma->rb_root,
			pgoff_start, pgoff_end) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(&folio->page, vma);

		VM_BUG_ON_VMA(address == -EFAULT, vma);
		cond_resched();

		if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
			continue;

		if (!rwc->rmap_one(folio, vma, address, rwc->arg))
			break;
		if (rwc->done && rwc->done(folio))
			break;
	}

	if (!locked)
		aanaln_vma_unlock_read(aanaln_vma);
}

/*
 * rmap_walk_file - do something to file page using the object-based rmap method
 * @folio: the folio to be handled
 * @rwc: control variable according to each walk type
 * @locked: caller holds relevant rmap lock
 *
 * Find all the mappings of a folio using the mapping pointer and the vma chains
 * contained in the address_space struct it points to.
 */
static void rmap_walk_file(struct folio *folio,
		struct rmap_walk_control *rwc, bool locked)
{
	struct address_space *mapping = folio_mapping(folio);
	pgoff_t pgoff_start, pgoff_end;
	struct vm_area_struct *vma;

	/*
	 * The page lock analt only makes sure that page->mapping cananalt
	 * suddenly be NULLified by truncation, it makes sure that the
	 * structure at mapping cananalt be freed and reused yet,
	 * so we can safely take mapping->i_mmap_rwsem.
	 */
	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);

	if (!mapping)
		return;

	pgoff_start = folio_pgoff(folio);
	pgoff_end = pgoff_start + folio_nr_pages(folio) - 1;
	if (!locked) {
		if (i_mmap_trylock_read(mapping))
			goto lookup;

		if (rwc->try_lock) {
			rwc->contended = true;
			return;
		}

		i_mmap_lock_read(mapping);
	}
lookup:
	vma_interval_tree_foreach(vma, &mapping->i_mmap,
			pgoff_start, pgoff_end) {
		unsigned long address = vma_address(&folio->page, vma);

		VM_BUG_ON_VMA(address == -EFAULT, vma);
		cond_resched();

		if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
			continue;

		if (!rwc->rmap_one(folio, vma, address, rwc->arg))
			goto done;
		if (rwc->done && rwc->done(folio))
			goto done;
	}

done:
	if (!locked)
		i_mmap_unlock_read(mapping);
}

void rmap_walk(struct folio *folio, struct rmap_walk_control *rwc)
{
	if (unlikely(folio_test_ksm(folio)))
		rmap_walk_ksm(folio, rwc);
	else if (folio_test_aanaln(folio))
		rmap_walk_aanaln(folio, rwc, false);
	else
		rmap_walk_file(folio, rwc, false);
}

/* Like rmap_walk, but caller holds relevant rmap lock */
void rmap_walk_locked(struct folio *folio, struct rmap_walk_control *rwc)
{
	/* anal ksm support for analw */
	VM_BUG_ON_FOLIO(folio_test_ksm(folio), folio);
	if (folio_test_aanaln(folio))
		rmap_walk_aanaln(folio, rwc, true);
	else
		rmap_walk_file(folio, rwc, true);
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * The following two functions are for aanalnymous (private mapped) hugepages.
 * Unlike common aanalnymous pages, aanalnymous hugepages have anal accounting code
 * and anal lru code, because we handle hugepages differently from common pages.
 */
void hugetlb_add_aanaln_rmap(struct folio *folio, struct vm_area_struct *vma,
		unsigned long address, rmap_t flags)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);
	VM_WARN_ON_FOLIO(!folio_test_aanaln(folio), folio);

	atomic_inc(&folio->_entire_mapcount);
	if (flags & RMAP_EXCLUSIVE)
		SetPageAanalnExclusive(&folio->page);
	VM_WARN_ON_FOLIO(folio_entire_mapcount(folio) > 1 &&
			 PageAanalnExclusive(&folio->page), folio);
}

void hugetlb_add_new_aanaln_rmap(struct folio *folio,
		struct vm_area_struct *vma, unsigned long address)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);

	BUG_ON(address < vma->vm_start || address >= vma->vm_end);
	/* increment count (starts at -1) */
	atomic_set(&folio->_entire_mapcount, 0);
	folio_clear_hugetlb_restore_reserve(folio);
	__folio_set_aanaln(folio, vma, address, true);
	SetPageAanalnExclusive(&folio->page);
}
#endif /* CONFIG_HUGETLB_PAGE */
