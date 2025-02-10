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
 * the anon methods track anonymous pages, and
 * the file methods track pages belonging to an inode.
 *
 * Original design by Rik van Riel <riel@conectiva.com.br> 2001
 * File methods by Dave McCracken <dmccr@us.ibm.com> 2003, 2004
 * Anonymous methods by Andrea Arcangeli <andrea@suse.de> 2004
 * Contributions by Hugh Dickins 2003, 2004
 */

/*
 * Lock ordering in mm:
 *
 * inode->i_rwsem	(while writing or truncating, not reading or faulting)
 *   mm->mmap_lock
 *     mapping->invalidate_lock (in filemap_fault)
 *       folio_lock
 *         hugetlbfs_i_mmap_rwsem_key (in huge_pmd_share, see hugetlbfs below)
 *           vma_start_write
 *             mapping->i_mmap_rwsem
 *               anon_vma->rwsem
 *                 mm->page_table_lock or pte_lock
 *                   swap_lock (in swap_duplicate, swap_info_get)
 *                     mmlist_lock (in mmput, drain_mmlist and others)
 *                     mapping->private_lock (in block_dirty_folio)
 *                         i_pages lock (widely used)
 *                           lruvec->lru_lock (in folio_lruvec_lock_irq)
 *                     inode->i_lock (in set_page_dirty's __mark_inode_dirty)
 *                     bdi.wb->list_lock (in set_page_dirty's __mark_inode_dirty)
 *                       sb_lock (within inode_lock in fs/fs-writeback.c)
 *                       i_pages lock (widely used, in set_page_dirty,
 *                                 in arch-dependent flush_dcache_mmap_lock,
 *                                 within bdi.wb->list_lock in __sync_single_inode)
 *
 * anon_vma->rwsem,mapping->i_mmap_rwsem   (memory_failure, collect_procs_anon)
 *   ->tasklist_lock
 *     pte map lock
 *
 * hugetlbfs PageHuge() take locks in this order:
 *   hugetlb_fault_mutex (hugetlbfs specific page fault mutex)
 *     vma_lock (hugetlb specific lock for pmd_sharing)
 *       mapping->i_mmap_rwsem (also used for hugetlb pmd sharing)
 *         folio_lock
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
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/backing-dev.h>
#include <linux/page_idle.h>
#include <linux/memremap.h>
#include <linux/userfaultfd_k.h>
#include <linux/mm_inline.h>
#include <linux/oom.h>

#include <asm/tlbflush.h>

#define CREATE_TRACE_POINTS
#include <trace/events/tlb.h>
#include <trace/events/migrate.h>

#include "internal.h"

static struct kmem_cache *anon_vma_cachep;
static struct kmem_cache *anon_vma_chain_cachep;

static inline struct anon_vma *anon_vma_alloc(void)
{
	struct anon_vma *anon_vma;

	anon_vma = kmem_cache_alloc(anon_vma_cachep, GFP_KERNEL);
	if (anon_vma) {
		atomic_set(&anon_vma->refcount, 1);
		anon_vma->num_children = 0;
		anon_vma->num_active_vmas = 0;
		anon_vma->parent = anon_vma;
		/*
		 * Initialise the anon_vma root to point to itself. If called
		 * from fork, the root will be reset to the parents anon_vma.
		 */
		anon_vma->root = anon_vma;
	}

	return anon_vma;
}

static inline void anon_vma_free(struct anon_vma *anon_vma)
{
	VM_BUG_ON(atomic_read(&anon_vma->refcount));

	/*
	 * Synchronize against folio_lock_anon_vma_read() such that
	 * we can safely hold the lock without the anon_vma getting
	 * freed.
	 *
	 * Relies on the full mb implied by the atomic_dec_and_test() from
	 * put_anon_vma() against the acquire barrier implied by
	 * down_read_trylock() from folio_lock_anon_vma_read(). This orders:
	 *
	 * folio_lock_anon_vma_read()	VS	put_anon_vma()
	 *   down_read_trylock()		  atomic_dec_and_test()
	 *   LOCK				  MB
	 *   atomic_read()			  rwsem_is_locked()
	 *
	 * LOCK should suffice since the actual taking of the lock must
	 * happen _before_ what follows.
	 */
	might_sleep();
	if (rwsem_is_locked(&anon_vma->root->rwsem)) {
		anon_vma_lock_write(anon_vma);
		anon_vma_unlock_write(anon_vma);
	}

	kmem_cache_free(anon_vma_cachep, anon_vma);
}

static inline struct anon_vma_chain *anon_vma_chain_alloc(gfp_t gfp)
{
	return kmem_cache_alloc(anon_vma_chain_cachep, gfp);
}

static void anon_vma_chain_free(struct anon_vma_chain *anon_vma_chain)
{
	kmem_cache_free(anon_vma_chain_cachep, anon_vma_chain);
}

static void anon_vma_chain_link(struct vm_area_struct *vma,
				struct anon_vma_chain *avc,
				struct anon_vma *anon_vma)
{
	avc->vma = vma;
	avc->anon_vma = anon_vma;
	list_add(&avc->same_vma, &vma->anon_vma_chain);
	anon_vma_interval_tree_insert(avc, &anon_vma->rb_root);
}

/**
 * __anon_vma_prepare - attach an anon_vma to a memory region
 * @vma: the memory region in question
 *
 * This makes sure the memory mapping described by 'vma' has
 * an 'anon_vma' attached to it, so that we can associate the
 * anonymous pages mapped into it with that anon_vma.
 *
 * The common case will be that we already have one, which
 * is handled inline by anon_vma_prepare(). But if
 * not we either need to find an adjacent mapping that we
 * can re-use the anon_vma from (very common when the only
 * reason for splitting a vma has been mprotect()), or we
 * allocate a new one.
 *
 * Anon-vma allocations are very subtle, because we may have
 * optimistically looked up an anon_vma in folio_lock_anon_vma_read()
 * and that may actually touch the rwsem even in the newly
 * allocated vma (it depends on RCU to make sure that the
 * anon_vma isn't actually destroyed).
 *
 * As a result, we need to do proper anon_vma locking even
 * for the new allocation. At the same time, we do not want
 * to do any locking for the common case of already having
 * an anon_vma.
 */
int __anon_vma_prepare(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	struct anon_vma *anon_vma, *allocated;
	struct anon_vma_chain *avc;

	mmap_assert_locked(mm);
	might_sleep();

	avc = anon_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_enomem;

	anon_vma = find_mergeable_anon_vma(vma);
	allocated = NULL;
	if (!anon_vma) {
		anon_vma = anon_vma_alloc();
		if (unlikely(!anon_vma))
			goto out_enomem_free_avc;
		anon_vma->num_children++; /* self-parent link for new root */
		allocated = anon_vma;
	}

	anon_vma_lock_write(anon_vma);
	/* page_table_lock to protect against threads */
	spin_lock(&mm->page_table_lock);
	if (likely(!vma->anon_vma)) {
		vma->anon_vma = anon_vma;
		anon_vma_chain_link(vma, avc, anon_vma);
		anon_vma->num_active_vmas++;
		allocated = NULL;
		avc = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	anon_vma_unlock_write(anon_vma);

	if (unlikely(allocated))
		put_anon_vma(allocated);
	if (unlikely(avc))
		anon_vma_chain_free(avc);

	return 0;

 out_enomem_free_avc:
	anon_vma_chain_free(avc);
 out_enomem:
	return -ENOMEM;
}

/*
 * This is a useful helper function for locking the anon_vma root as
 * we traverse the vma->anon_vma_chain, looping over anon_vma's that
 * have the same vma.
 *
 * Such anon_vma's should have the same root, so you'd expect to see
 * just a single mutex_lock for the whole traversal.
 */
static inline struct anon_vma *lock_anon_vma_root(struct anon_vma *root, struct anon_vma *anon_vma)
{
	struct anon_vma *new_root = anon_vma->root;
	if (new_root != root) {
		if (WARN_ON_ONCE(root))
			up_write(&root->rwsem);
		root = new_root;
		down_write(&root->rwsem);
	}
	return root;
}

static inline void unlock_anon_vma_root(struct anon_vma *root)
{
	if (root)
		up_write(&root->rwsem);
}

/*
 * Attach the anon_vmas from src to dst.
 * Returns 0 on success, -ENOMEM on failure.
 *
 * anon_vma_clone() is called by vma_expand(), vma_merge(), __split_vma(),
 * copy_vma() and anon_vma_fork(). The first four want an exact copy of src,
 * while the last one, anon_vma_fork(), may try to reuse an existing anon_vma to
 * prevent endless growth of anon_vma. Since dst->anon_vma is set to NULL before
 * call, we can identify this case by checking (!dst->anon_vma &&
 * src->anon_vma).
 *
 * If (!dst->anon_vma && src->anon_vma) is true, this function tries to find
 * and reuse existing anon_vma which has no vmas and only one child anon_vma.
 * This prevents degradation of anon_vma hierarchy to endless linear chain in
 * case of constantly forking task. On the other hand, an anon_vma with more
 * than one child isn't reused even if there was no alive vma, thus rmap
 * walker has a good chance of avoiding scanning the whole hierarchy when it
 * searches where page is mapped.
 */
int anon_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src)
{
	struct anon_vma_chain *avc, *pavc;
	struct anon_vma *root = NULL;

	list_for_each_entry_reverse(pavc, &src->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma;

		avc = anon_vma_chain_alloc(GFP_NOWAIT | __GFP_NOWARN);
		if (unlikely(!avc)) {
			unlock_anon_vma_root(root);
			root = NULL;
			avc = anon_vma_chain_alloc(GFP_KERNEL);
			if (!avc)
				goto enomem_failure;
		}
		anon_vma = pavc->anon_vma;
		root = lock_anon_vma_root(root, anon_vma);
		anon_vma_chain_link(dst, avc, anon_vma);

		/*
		 * Reuse existing anon_vma if it has no vma and only one
		 * anon_vma child.
		 *
		 * Root anon_vma is never reused:
		 * it has self-parent reference and at least one child.
		 */
		if (!dst->anon_vma && src->anon_vma &&
		    anon_vma->num_children < 2 &&
		    anon_vma->num_active_vmas == 0)
			dst->anon_vma = anon_vma;
	}
	if (dst->anon_vma)
		dst->anon_vma->num_active_vmas++;
	unlock_anon_vma_root(root);
	return 0;

 enomem_failure:
	/*
	 * dst->anon_vma is dropped here otherwise its num_active_vmas can
	 * be incorrectly decremented in unlink_anon_vmas().
	 * We can safely do this because callers of anon_vma_clone() don't care
	 * about dst->anon_vma if anon_vma_clone() failed.
	 */
	dst->anon_vma = NULL;
	unlink_anon_vmas(dst);
	return -ENOMEM;
}

/*
 * Attach vma to its own anon_vma, as well as to the anon_vmas that
 * the corresponding VMA in the parent process is attached to.
 * Returns 0 on success, non-zero on failure.
 */
int anon_vma_fork(struct vm_area_struct *vma, struct vm_area_struct *pvma)
{
	struct anon_vma_chain *avc;
	struct anon_vma *anon_vma;
	int error;

	/* Don't bother if the parent process has no anon_vma here. */
	if (!pvma->anon_vma)
		return 0;

	/* Drop inherited anon_vma, we'll reuse existing or allocate new. */
	vma->anon_vma = NULL;

	/*
	 * First, attach the new VMA to the parent VMA's anon_vmas,
	 * so rmap can find non-COWed pages in child processes.
	 */
	error = anon_vma_clone(vma, pvma);
	if (error)
		return error;

	/* An existing anon_vma has been reused, all done then. */
	if (vma->anon_vma)
		return 0;

	/* Then add our own anon_vma. */
	anon_vma = anon_vma_alloc();
	if (!anon_vma)
		goto out_error;
	anon_vma->num_active_vmas++;
	avc = anon_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_error_free_anon_vma;

	/*
	 * The root anon_vma's rwsem is the lock actually used when we
	 * lock any of the anon_vmas in this anon_vma tree.
	 */
	anon_vma->root = pvma->anon_vma->root;
	anon_vma->parent = pvma->anon_vma;
	/*
	 * With refcounts, an anon_vma can stay around longer than the
	 * process it belongs to. The root anon_vma needs to be pinned until
	 * this anon_vma is freed, because the lock lives in the root.
	 */
	get_anon_vma(anon_vma->root);
	/* Mark this anon_vma as the one where our new (COWed) pages go. */
	vma->anon_vma = anon_vma;
	anon_vma_lock_write(anon_vma);
	anon_vma_chain_link(vma, avc, anon_vma);
	anon_vma->parent->num_children++;
	anon_vma_unlock_write(anon_vma);

	return 0;

 out_error_free_anon_vma:
	put_anon_vma(anon_vma);
 out_error:
	unlink_anon_vmas(vma);
	return -ENOMEM;
}

void unlink_anon_vmas(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc, *next;
	struct anon_vma *root = NULL;

	/*
	 * Unlink each anon_vma chained to the VMA.  This list is ordered
	 * from newest to oldest, ensuring the root anon_vma gets freed last.
	 */
	list_for_each_entry_safe(avc, next, &vma->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma = avc->anon_vma;

		root = lock_anon_vma_root(root, anon_vma);
		anon_vma_interval_tree_remove(avc, &anon_vma->rb_root);

		/*
		 * Leave empty anon_vmas on the list - we'll need
		 * to free them outside the lock.
		 */
		if (RB_EMPTY_ROOT(&anon_vma->rb_root.rb_root)) {
			anon_vma->parent->num_children--;
			continue;
		}

		list_del(&avc->same_vma);
		anon_vma_chain_free(avc);
	}
	if (vma->anon_vma) {
		vma->anon_vma->num_active_vmas--;

		/*
		 * vma would still be needed after unlink, and anon_vma will be prepared
		 * when handle fault.
		 */
		vma->anon_vma = NULL;
	}
	unlock_anon_vma_root(root);

	/*
	 * Iterate the list once more, it now only contains empty and unlinked
	 * anon_vmas, destroy them. Could not do before due to __put_anon_vma()
	 * needing to write-acquire the anon_vma->root->rwsem.
	 */
	list_for_each_entry_safe(avc, next, &vma->anon_vma_chain, same_vma) {
		struct anon_vma *anon_vma = avc->anon_vma;

		VM_WARN_ON(anon_vma->num_children);
		VM_WARN_ON(anon_vma->num_active_vmas);
		put_anon_vma(anon_vma);

		list_del(&avc->same_vma);
		anon_vma_chain_free(avc);
	}
}

static void anon_vma_ctor(void *data)
{
	struct anon_vma *anon_vma = data;

	init_rwsem(&anon_vma->rwsem);
	atomic_set(&anon_vma->refcount, 0);
	anon_vma->rb_root = RB_ROOT_CACHED;
}

void __init anon_vma_init(void)
{
	anon_vma_cachep = kmem_cache_create("anon_vma", sizeof(struct anon_vma),
			0, SLAB_TYPESAFE_BY_RCU|SLAB_PANIC|SLAB_ACCOUNT,
			anon_vma_ctor);
	anon_vma_chain_cachep = KMEM_CACHE(anon_vma_chain,
			SLAB_PANIC|SLAB_ACCOUNT);
}

/*
 * Getting a lock on a stable anon_vma from a page off the LRU is tricky!
 *
 * Since there is no serialization what so ever against folio_remove_rmap_*()
 * the best this function can do is return a refcount increased anon_vma
 * that might have been relevant to this page.
 *
 * The page might have been remapped to a different anon_vma or the anon_vma
 * returned may already be freed (and even reused).
 *
 * In case it was remapped to a different anon_vma, the new anon_vma will be a
 * child of the old anon_vma, and the anon_vma lifetime rules will therefore
 * ensure that any anon_vma obtained from the page will still be valid for as
 * long as we observe page_mapped() [ hence all those page_mapped() tests ].
 *
 * All users of this function must be very careful when walking the anon_vma
 * chain and verify that the page in question is indeed mapped in it
 * [ something equivalent to page_mapped_in_vma() ].
 *
 * Since anon_vma's slab is SLAB_TYPESAFE_BY_RCU and we know from
 * folio_remove_rmap_*() that the anon_vma pointer from page->mapping is valid
 * if there is a mapcount, we can dereference the anon_vma after observing
 * those.
 *
 * NOTE: the caller should normally hold folio lock when calling this.  If
 * not, the caller needs to double check the anon_vma didn't change after
 * taking the anon_vma lock for either read or write (UFFDIO_MOVE can modify it
 * concurrently without folio lock protection). See folio_lock_anon_vma_read()
 * which has already covered that, and comment above remap_pages().
 */
struct anon_vma *folio_get_anon_vma(const struct folio *folio)
{
	struct anon_vma *anon_vma = NULL;
	unsigned long anon_mapping;

	rcu_read_lock();
	anon_mapping = (unsigned long)READ_ONCE(folio->mapping);
	if ((anon_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		goto out;
	if (!folio_mapped(folio))
		goto out;

	anon_vma = (struct anon_vma *) (anon_mapping - PAGE_MAPPING_ANON);
	if (!atomic_inc_not_zero(&anon_vma->refcount)) {
		anon_vma = NULL;
		goto out;
	}

	/*
	 * If this folio is still mapped, then its anon_vma cannot have been
	 * freed.  But if it has been unmapped, we have no security against the
	 * anon_vma structure being freed and reused (for another anon_vma:
	 * SLAB_TYPESAFE_BY_RCU guarantees that - so the atomic_inc_not_zero()
	 * above cannot corrupt).
	 */
	if (!folio_mapped(folio)) {
		rcu_read_unlock();
		put_anon_vma(anon_vma);
		return NULL;
	}
out:
	rcu_read_unlock();

	return anon_vma;
}

/*
 * Similar to folio_get_anon_vma() except it locks the anon_vma.
 *
 * Its a little more complex as it tries to keep the fast path to a single
 * atomic op -- the trylock. If we fail the trylock, we fall back to getting a
 * reference like with folio_get_anon_vma() and then block on the mutex
 * on !rwc->try_lock case.
 */
struct anon_vma *folio_lock_anon_vma_read(const struct folio *folio,
					  struct rmap_walk_control *rwc)
{
	struct anon_vma *anon_vma = NULL;
	struct anon_vma *root_anon_vma;
	unsigned long anon_mapping;

retry:
	rcu_read_lock();
	anon_mapping = (unsigned long)READ_ONCE(folio->mapping);
	if ((anon_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		goto out;
	if (!folio_mapped(folio))
		goto out;

	anon_vma = (struct anon_vma *) (anon_mapping - PAGE_MAPPING_ANON);
	root_anon_vma = READ_ONCE(anon_vma->root);
	if (down_read_trylock(&root_anon_vma->rwsem)) {
		/*
		 * folio_move_anon_rmap() might have changed the anon_vma as we
		 * might not hold the folio lock here.
		 */
		if (unlikely((unsigned long)READ_ONCE(folio->mapping) !=
			     anon_mapping)) {
			up_read(&root_anon_vma->rwsem);
			rcu_read_unlock();
			goto retry;
		}

		/*
		 * If the folio is still mapped, then this anon_vma is still
		 * its anon_vma, and holding the mutex ensures that it will
		 * not go away, see anon_vma_free().
		 */
		if (!folio_mapped(folio)) {
			up_read(&root_anon_vma->rwsem);
			anon_vma = NULL;
		}
		goto out;
	}

	if (rwc && rwc->try_lock) {
		anon_vma = NULL;
		rwc->contended = true;
		goto out;
	}

	/* trylock failed, we got to sleep */
	if (!atomic_inc_not_zero(&anon_vma->refcount)) {
		anon_vma = NULL;
		goto out;
	}

	if (!folio_mapped(folio)) {
		rcu_read_unlock();
		put_anon_vma(anon_vma);
		return NULL;
	}

	/* we pinned the anon_vma, its safe to sleep */
	rcu_read_unlock();
	anon_vma_lock_read(anon_vma);

	/*
	 * folio_move_anon_rmap() might have changed the anon_vma as we might
	 * not hold the folio lock here.
	 */
	if (unlikely((unsigned long)READ_ONCE(folio->mapping) !=
		     anon_mapping)) {
		anon_vma_unlock_read(anon_vma);
		put_anon_vma(anon_vma);
		anon_vma = NULL;
		goto retry;
	}

	if (atomic_dec_and_test(&anon_vma->refcount)) {
		/*
		 * Oops, we held the last refcount, release the lock
		 * and bail -- can't simply use put_anon_vma() because
		 * we'll deadlock on the anon_vma_lock_write() recursion.
		 */
		anon_vma_unlock_read(anon_vma);
		__put_anon_vma(anon_vma);
		anon_vma = NULL;
	}

	return anon_vma;

out:
	rcu_read_unlock();
	return anon_vma;
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
	 * Ensure compiler does not re-order the setting of tlb_flush_batched
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
 * Reclaim unmaps pages under the PTL but do not flush the TLB prior to
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

/**
 * page_address_in_vma - The virtual address of a page in this VMA.
 * @folio: The folio containing the page.
 * @page: The page within the folio.
 * @vma: The VMA we need to know the address in.
 *
 * Calculates the user virtual address of this page in the specified VMA.
 * It is the caller's responsibililty to check the page is actually
 * within the VMA.  There may not currently be a PTE pointing at this
 * page, but if a page fault occurs at this address, this is the page
 * which will be accessed.
 *
 * Context: Caller should hold a reference to the folio.  Caller should
 * hold a lock (eg the i_mmap_lock or the mmap_lock) which keeps the
 * VMA from being altered.
 *
 * Return: The virtual address corresponding to this page in the VMA.
 */
unsigned long page_address_in_vma(const struct folio *folio,
		const struct page *page, const struct vm_area_struct *vma)
{
	if (folio_test_anon(folio)) {
		struct anon_vma *page__anon_vma = folio_anon_vma(folio);
		/*
		 * Note: swapoff's unuse_vma() is more efficient with this
		 * check, and needs it to match anon_vma when KSM is active.
		 */
		if (!vma->anon_vma || !page__anon_vma ||
		    vma->anon_vma->root != page__anon_vma->root)
			return -EFAULT;
	} else if (!vma->vm_file) {
		return -EFAULT;
	} else if (vma->vm_file->f_mapping != folio->mapping) {
		return -EFAULT;
	}

	/* KSM folios don't reach here because of the !page__anon_vma check */
	return vma_address(vma, page_pgoff(folio, page), 1);
}

/*
 * Returns the actual pmd_t* where we expect 'address' to be mapped from, or
 * NULL if it doesn't exist.  No guarantees / checks on what the pmd_t*
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

		/*
		 * Skip the non-shared swapbacked folio mapped solely by
		 * the exiting or OOM-reaped process. This avoids redundant
		 * swap-out followed by an immediate unmap.
		 */
		if ((!atomic_read(&vma->vm_mm->mm_users) ||
		    check_stable_address_space(vma->vm_mm)) &&
		    folio_test_anon(folio) && folio_test_swapbacked(folio) &&
		    !folio_likely_mapped_shared(folio)) {
			pra->referenced = -1;
			page_vma_mapped_walk_done(&pvmw);
			return false;
		}

		if (lru_gen_enabled() && pvmw.pte) {
			if (lru_gen_look_around(&pvmw))
				referenced++;
		} else if (pvmw.pte) {
			if (ptep_clear_flush_young_notify(vma, address,
						pvmw.pte))
				referenced++;
		} else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
			if (pmdp_clear_flush_young_notify(vma, address,
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
	 * Ignore references from this mapping if it has no recency. If the
	 * folio has been used in another mapping, we will catch it; if this
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
	bool we_locked = false;
	struct folio_referenced_arg pra = {
		.mapcount = folio_mapcount(folio),
		.memcg = memcg,
	};
	struct rmap_walk_control rwc = {
		.rmap_one = folio_referenced_one,
		.arg = (void *)&pra,
		.anon_lock = folio_lock_anon_vma_read,
		.try_lock = true,
		.invalid_vma = invalid_folio_referenced_vma,
	};

	*vm_flags = 0;
	if (!pra.mapcount)
		return 0;

	if (!folio_raw_mapping(folio))
		return 0;

	if (!is_locked && (!folio_test_anon(folio) || folio_test_ksm(folio))) {
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
	struct mmu_notifier_range range;
	unsigned long address = pvmw->address;

	/*
	 * We have to assume the worse case ie pmd for invalidation. Note that
	 * the folio can not be freed from this function.
	 */
	mmu_notifier_range_init(&range, MMU_NOTIFY_PROTECTION_PAGE, 0,
				vma->vm_mm, address, vma_address_end(pvmw));
	mmu_notifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(pvmw)) {
		int ret = 0;

		address = pvmw->address;
		if (pvmw->pte) {
			pte_t *pte = pvmw->pte;
			pte_t entry = ptep_get(pte);

			/*
			 * PFN swap PTEs, such as device-exclusive ones, that
			 * actually map pages are clean and not writable from a
			 * CPU perspective. The MMU notifier takes care of any
			 * device aspects.
			 */
			if (!pte_present(entry))
				continue;
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

	mmu_notifier_invalidate_range_end(&range);

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

	pvmw.address = vma_address(vma, pgoff, nr_pages);
	VM_BUG_ON_VMA(pvmw.address == -EFAULT, vma);

	return page_vma_mkclean_one(&pvmw);
}

static __always_inline unsigned int __folio_add_rmap(struct folio *folio,
		struct page *page, int nr_pages, enum rmap_level level,
		int *nr_pmdmapped)
{
	atomic_t *mapped = &folio->_nr_pages_mapped;
	const int orig_nr_pages = nr_pages;
	int first = 0, nr = 0;

	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	switch (level) {
	case RMAP_LEVEL_PTE:
		if (!folio_test_large(folio)) {
			nr = atomic_inc_and_test(&folio->_mapcount);
			break;
		}

		do {
			first += atomic_inc_and_test(&page->_mapcount);
		} while (page++, --nr_pages > 0);

		if (first &&
		    atomic_add_return_relaxed(first, mapped) < ENTIRELY_MAPPED)
			nr = first;

		atomic_add(orig_nr_pages, &folio->_large_mapcount);
		break;
	case RMAP_LEVEL_PMD:
		first = atomic_inc_and_test(&folio->_entire_mapcount);
		if (first) {
			nr = atomic_add_return_relaxed(ENTIRELY_MAPPED, mapped);
			if (likely(nr < ENTIRELY_MAPPED + ENTIRELY_MAPPED)) {
				*nr_pmdmapped = folio_nr_pages(folio);
				nr = *nr_pmdmapped - (nr & FOLIO_PAGES_MAPPED);
				/* Raced ahead of a remove and another add? */
				if (unlikely(nr < 0))
					nr = 0;
			} else {
				/* Raced ahead of a remove of ENTIRELY_MAPPED */
				nr = 0;
			}
		}
		atomic_inc(&folio->_large_mapcount);
		break;
	}
	return nr;
}

/**
 * folio_move_anon_rmap - move a folio to our anon_vma
 * @folio:	The folio to move to our anon_vma
 * @vma:	The vma the folio belongs to
 *
 * When a folio belongs exclusively to one process after a COW event,
 * that folio can be moved into the anon_vma that belongs to just that
 * process, so the rmap code will not search the parent or sibling processes.
 */
void folio_move_anon_rmap(struct folio *folio, struct vm_area_struct *vma)
{
	void *anon_vma = vma->anon_vma;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);
	VM_BUG_ON_VMA(!anon_vma, vma);

	anon_vma += PAGE_MAPPING_ANON;
	/*
	 * Ensure that anon_vma and the PAGE_MAPPING_ANON bit are written
	 * simultaneously, so a concurrent reader (eg folio_referenced()'s
	 * folio_test_anon()) will not see one without the other.
	 */
	WRITE_ONCE(folio->mapping, anon_vma);
}

/**
 * __folio_set_anon - set up a new anonymous rmap for a folio
 * @folio:	The folio to set up the new anonymous rmap for.
 * @vma:	VM area to add the folio to.
 * @address:	User virtual address of the mapping
 * @exclusive:	Whether the folio is exclusive to the process.
 */
static void __folio_set_anon(struct folio *folio, struct vm_area_struct *vma,
			     unsigned long address, bool exclusive)
{
	struct anon_vma *anon_vma = vma->anon_vma;

	BUG_ON(!anon_vma);

	/*
	 * If the folio isn't exclusive to this vma, we must use the _oldest_
	 * possible anon_vma for the folio mapping!
	 */
	if (!exclusive)
		anon_vma = anon_vma->root;

	/*
	 * page_idle does a lockless/optimistic rmap scan on folio->mapping.
	 * Make sure the compiler doesn't split the stores of anon_vma and
	 * the PAGE_MAPPING_ANON type identifier, otherwise the rmap code
	 * could mistake the mapping for a struct address_space and crash.
	 */
	anon_vma = (void *) anon_vma + PAGE_MAPPING_ANON;
	WRITE_ONCE(folio->mapping, (struct address_space *) anon_vma);
	folio->index = linear_page_index(vma, address);
}

/**
 * __page_check_anon_rmap - sanity check anonymous rmap addition
 * @folio:	The folio containing @page.
 * @page:	the page to check the mapping of
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 */
static void __page_check_anon_rmap(const struct folio *folio,
		const struct page *page, struct vm_area_struct *vma,
		unsigned long address)
{
	/*
	 * The page's anon-rmap details (mapping and index) are guaranteed to
	 * be set up correctly at this point.
	 *
	 * We have exclusion against folio_add_anon_rmap_*() because the caller
	 * always holds the page locked.
	 *
	 * We have exclusion against folio_add_new_anon_rmap because those pages
	 * are initially only visible via the pagetables, and the pte is locked
	 * over the call to folio_add_new_anon_rmap.
	 */
	VM_BUG_ON_FOLIO(folio_anon_vma(folio)->root != vma->anon_vma->root,
			folio);
	VM_BUG_ON_PAGE(page_pgoff(folio, page) != linear_page_index(vma, address),
		       page);
}

static void __folio_mod_stat(struct folio *folio, int nr, int nr_pmdmapped)
{
	int idx;

	if (nr) {
		idx = folio_test_anon(folio) ? NR_ANON_MAPPED : NR_FILE_MAPPED;
		__lruvec_stat_mod_folio(folio, idx, nr);
	}
	if (nr_pmdmapped) {
		if (folio_test_anon(folio)) {
			idx = NR_ANON_THPS;
			__lruvec_stat_mod_folio(folio, idx, nr_pmdmapped);
		} else {
			/* NR_*_PMDMAPPED are not maintained per-memcg */
			idx = folio_test_swapbacked(folio) ?
				NR_SHMEM_PMDMAPPED : NR_FILE_PMDMAPPED;
			__mod_node_page_state(folio_pgdat(folio), idx,
					      nr_pmdmapped);
		}
	}
}

static __always_inline void __folio_add_anon_rmap(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *vma,
		unsigned long address, rmap_t flags, enum rmap_level level)
{
	int i, nr, nr_pmdmapped = 0;

	VM_WARN_ON_FOLIO(!folio_test_anon(folio), folio);

	nr = __folio_add_rmap(folio, page, nr_pages, level, &nr_pmdmapped);

	if (likely(!folio_test_ksm(folio)))
		__page_check_anon_rmap(folio, page, vma, address);

	__folio_mod_stat(folio, nr, nr_pmdmapped);

	if (flags & RMAP_EXCLUSIVE) {
		switch (level) {
		case RMAP_LEVEL_PTE:
			for (i = 0; i < nr_pages; i++)
				SetPageAnonExclusive(page + i);
			break;
		case RMAP_LEVEL_PMD:
			SetPageAnonExclusive(page);
			break;
		}
	}
	for (i = 0; i < nr_pages; i++) {
		struct page *cur_page = page + i;

		/* While PTE-mapping a THP we have a PMD and a PTE mapping. */
		VM_WARN_ON_FOLIO((atomic_read(&cur_page->_mapcount) > 0 ||
				  (folio_test_large(folio) &&
				   folio_entire_mapcount(folio) > 1)) &&
				 PageAnonExclusive(cur_page), folio);
	}

	/*
	 * For large folio, only mlock it if it's fully mapped to VMA. It's
	 * not easy to check whether the large folio is fully mapped to VMA
	 * here. Only mlock normal 4K folio and leave page reclaim to handle
	 * large folio.
	 */
	if (!folio_test_large(folio))
		mlock_vma_folio(folio, vma);
}

/**
 * folio_add_anon_rmap_ptes - add PTE mappings to a page range of an anon folio
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
 * the anon_vma case: to serialize mapping,index checking after setting,
 * and to ensure that an anon folio is not being upgraded racily to a KSM folio
 * (but KSM folios are never downgraded).
 */
void folio_add_anon_rmap_ptes(struct folio *folio, struct page *page,
		int nr_pages, struct vm_area_struct *vma, unsigned long address,
		rmap_t flags)
{
	__folio_add_anon_rmap(folio, page, nr_pages, vma, address, flags,
			      RMAP_LEVEL_PTE);
}

/**
 * folio_add_anon_rmap_pmd - add a PMD mapping to a page range of an anon folio
 * @folio:	The folio to add the mapping to
 * @page:	The first page to add
 * @vma:	The vm area in which the mapping is added
 * @address:	The user virtual address of the first page to map
 * @flags:	The rmap flags
 *
 * The page range of folio is defined by [first_page, first_page + HPAGE_PMD_NR)
 *
 * The caller needs to hold the page table lock, and the page must be locked in
 * the anon_vma case: to serialize mapping,index checking after setting.
 */
void folio_add_anon_rmap_pmd(struct folio *folio, struct page *page,
		struct vm_area_struct *vma, unsigned long address, rmap_t flags)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	__folio_add_anon_rmap(folio, page, HPAGE_PMD_NR, vma, address, flags,
			      RMAP_LEVEL_PMD);
#else
	WARN_ON_ONCE(true);
#endif
}

/**
 * folio_add_new_anon_rmap - Add mapping to a new anonymous folio.
 * @folio:	The folio to add the mapping to.
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 * @flags:	The rmap flags
 *
 * Like folio_add_anon_rmap_*() but must only be called on *new* folios.
 * This means the inc-and-test can be bypassed.
 * The folio doesn't necessarily need to be locked while it's exclusive
 * unless two threads map it concurrently. However, the folio must be
 * locked if it's shared.
 *
 * If the folio is pmd-mappable, it is accounted as a THP.
 */
void folio_add_new_anon_rmap(struct folio *folio, struct vm_area_struct *vma,
		unsigned long address, rmap_t flags)
{
	const int nr = folio_nr_pages(folio);
	const bool exclusive = flags & RMAP_EXCLUSIVE;
	int nr_pmdmapped = 0;

	VM_WARN_ON_FOLIO(folio_test_hugetlb(folio), folio);
	VM_WARN_ON_FOLIO(!exclusive && !folio_test_locked(folio), folio);
	VM_BUG_ON_VMA(address < vma->vm_start ||
			address + (nr << PAGE_SHIFT) > vma->vm_end, vma);

	/*
	 * VM_DROPPABLE mappings don't swap; instead they're just dropped when
	 * under memory pressure.
	 */
	if (!folio_test_swapbacked(folio) && !(vma->vm_flags & VM_DROPPABLE))
		__folio_set_swapbacked(folio);
	__folio_set_anon(folio, vma, address, exclusive);

	if (likely(!folio_test_large(folio))) {
		/* increment count (starts at -1) */
		atomic_set(&folio->_mapcount, 0);
		if (exclusive)
			SetPageAnonExclusive(&folio->page);
	} else if (!folio_test_pmd_mappable(folio)) {
		int i;

		for (i = 0; i < nr; i++) {
			struct page *page = folio_page(folio, i);

			/* increment count (starts at -1) */
			atomic_set(&page->_mapcount, 0);
			if (exclusive)
				SetPageAnonExclusive(page);
		}

		/* increment count (starts at -1) */
		atomic_set(&folio->_large_mapcount, nr - 1);
		atomic_set(&folio->_nr_pages_mapped, nr);
	} else {
		/* increment count (starts at -1) */
		atomic_set(&folio->_entire_mapcount, 0);
		/* increment count (starts at -1) */
		atomic_set(&folio->_large_mapcount, 0);
		atomic_set(&folio->_nr_pages_mapped, ENTIRELY_MAPPED);
		if (exclusive)
			SetPageAnonExclusive(&folio->page);
		nr_pmdmapped = nr;
	}

	__folio_mod_stat(folio, nr, nr_pmdmapped);
	mod_mthp_stat(folio_order(folio), MTHP_STAT_NR_ANON, 1);
}

static __always_inline void __folio_add_file_rmap(struct folio *folio,
		struct page *page, int nr_pages, struct vm_area_struct *vma,
		enum rmap_level level)
{
	int nr, nr_pmdmapped = 0;

	VM_WARN_ON_FOLIO(folio_test_anon(folio), folio);

	nr = __folio_add_rmap(folio, page, nr_pages, level, &nr_pmdmapped);
	__folio_mod_stat(folio, nr, nr_pmdmapped);

	/* See comments in folio_add_anon_rmap_*() */
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
	int last = 0, nr = 0, nr_pmdmapped = 0;
	bool partially_mapped = false;

	__folio_rmap_sanity_checks(folio, page, nr_pages, level);

	switch (level) {
	case RMAP_LEVEL_PTE:
		if (!folio_test_large(folio)) {
			nr = atomic_add_negative(-1, &folio->_mapcount);
			break;
		}

		atomic_sub(nr_pages, &folio->_large_mapcount);
		do {
			last += atomic_add_negative(-1, &page->_mapcount);
		} while (page++, --nr_pages > 0);

		if (last &&
		    atomic_sub_return_relaxed(last, mapped) < ENTIRELY_MAPPED)
			nr = last;

		partially_mapped = nr && atomic_read(mapped);
		break;
	case RMAP_LEVEL_PMD:
		atomic_dec(&folio->_large_mapcount);
		last = atomic_add_negative(-1, &folio->_entire_mapcount);
		if (last) {
			nr = atomic_sub_return_relaxed(ENTIRELY_MAPPED, mapped);
			if (likely(nr < ENTIRELY_MAPPED)) {
				nr_pmdmapped = folio_nr_pages(folio);
				nr = nr_pmdmapped - (nr & FOLIO_PAGES_MAPPED);
				/* Raced ahead of another remove and an add? */
				if (unlikely(nr < 0))
					nr = 0;
			} else {
				/* An add of ENTIRELY_MAPPED raced ahead */
				nr = 0;
			}
		}

		partially_mapped = nr && nr < nr_pmdmapped;
		break;
	}

	/*
	 * Queue anon large folio for deferred split if at least one page of
	 * the folio is unmapped and at least one page is still mapped.
	 *
	 * Check partially_mapped first to ensure it is a large folio.
	 */
	if (partially_mapped && folio_test_anon(folio) &&
	    !folio_test_partially_mapped(folio))
		deferred_split_folio(folio, true);

	__folio_mod_stat(folio, -nr, -nr_pmdmapped);

	/*
	 * It would be tidy to reset folio_test_anon mapping when fully
	 * unmapped, but that might overwrite a racing folio_add_anon_rmap_*()
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
	bool anon_exclusive, ret = true;
	pte_t pteval;
	struct page *subpage;
	struct mmu_notifier_range range;
	enum ttu_flags flags = (enum ttu_flags)(long)arg;
	unsigned long pfn;
	unsigned long hsz = 0;

	/*
	 * When racing against e.g. zap_pte_range() on another cpu,
	 * in between its ptep_get_and_clear_full() and folio_remove_rmap_*(),
	 * try_to_unmap() may return before page_mapped() has become false,
	 * if page table locking is skipped: use TTU_SYNC to wait for that.
	 */
	if (flags & TTU_SYNC)
		pvmw.flags = PVMW_SYNC;

	/*
	 * For THP, we have to assume the worse case ie pmd for invalidation.
	 * For hugetlb, it could be much worse if we need to do pud
	 * invalidation in the case of pmd sharing.
	 *
	 * Note that the folio can not be freed in this function as call of
	 * try_to_unmap() must hold a reference on the folio.
	 */
	range.end = vma_address_end(&pvmw);
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma->vm_mm,
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
	mmu_notifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(&pvmw)) {
		/*
		 * If the folio is in an mlock()d vma, we must not swap it out.
		 */
		if (!(flags & TTU_IGNORE_MLOCK) &&
		    (vma->vm_flags & VM_LOCKED)) {
			/* Restore the mlock which got missed */
			if (!folio_test_large(folio))
				mlock_vma_folio(folio, vma);
			goto walk_abort;
		}

		if (!pvmw.pte) {
			if (unmap_huge_pmd_locked(vma, pvmw.address, pvmw.pmd,
						  folio))
				goto walk_done;

			if (flags & TTU_SPLIT_HUGE_PMD) {
				/*
				 * We temporarily have to drop the PTL and
				 * restart so we can process the PTE-mapped THP.
				 */
				split_huge_pmd_locked(vma, pvmw.address,
						      pvmw.pmd, false, folio);
				flags &= ~TTU_SPLIT_HUGE_PMD;
				page_vma_mapped_walk_restart(&pvmw);
				continue;
			}
		}

		/* Unexpected PMD-mapped THP? */
		VM_BUG_ON_FOLIO(!pvmw.pte, folio);

		/*
		 * Handle PFN swap PTEs, such as device-exclusive ones, that
		 * actually map pages.
		 */
		pteval = ptep_get(pvmw.pte);
		if (likely(pte_present(pteval))) {
			pfn = pte_pfn(pteval);
		} else {
			pfn = swp_offset_pfn(pte_to_swp_entry(pteval));
			VM_WARN_ON_FOLIO(folio_test_hugetlb(folio), folio);
		}

		subpage = folio_page(folio, pfn - folio_pfn(folio));
		address = pvmw.address;
		anon_exclusive = folio_test_anon(folio) &&
				 PageAnonExclusive(subpage);

		if (folio_test_hugetlb(folio)) {
			bool anon = folio_test_anon(folio);

			/*
			 * The try_to_unmap() is only passed a hugetlb page
			 * in the case where the hugetlb page is poisoned.
			 */
			VM_BUG_ON_PAGE(!PageHWPoison(subpage), subpage);
			/*
			 * huge_pmd_unshare may unmap an entire PMD page.
			 * There is no way of knowing exactly which PMDs may
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
			if (!anon) {
				VM_BUG_ON(!(flags & TTU_RMAP_LOCKED));
				if (!hugetlb_vma_trylock_write(vma))
					goto walk_abort;
				if (huge_pmd_unshare(mm, vma, address, pvmw.pte)) {
					hugetlb_vma_unlock_write(vma);
					flush_tlb_range(vma,
						range.start, range.end);
					/*
					 * The ref count of the PMD page was
					 * dropped which is part of the way map
					 * counting is done for shared PMDs.
					 * Return 'true' here.  When there is
					 * no other sharing, huge_pmd_unshare
					 * returns false and we will unmap the
					 * actual page and drop map count
					 * to zero.
					 */
					goto walk_done;
				}
				hugetlb_vma_unlock_write(vma);
			}
			pteval = huge_ptep_clear_flush(vma, address, pvmw.pte);
			if (pte_dirty(pteval))
				folio_mark_dirty(folio);
		} else if (likely(pte_present(pteval))) {
			flush_cache_page(vma, address, pfn);
			/* Nuke the page table entry. */
			if (should_defer_flush(mm, flags)) {
				/*
				 * We clear the PTE but do not flush so potentially
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
			if (pte_dirty(pteval))
				folio_mark_dirty(folio);
		} else {
			pte_clear(mm, address, pvmw.pte);
		}

		/*
		 * Now the pte is cleared. If this pte was uffd-wp armed,
		 * we may want to replace a none pte with a marker pte if
		 * it's file-backed, so we don't lose the tracking info.
		 */
		pte_install_uffd_wp_if_needed(vma, address, pvmw.pte, pteval);

		/* Update high watermark before we lower rss */
		update_hiwater_rss(mm);

		if (PageHWPoison(subpage) && (flags & TTU_HWPOISON)) {
			pteval = swp_entry_to_pte(make_hwpoison_entry(subpage));
			if (folio_test_hugetlb(folio)) {
				hugetlb_count_sub(folio_nr_pages(folio), mm);
				set_huge_pte_at(mm, address, pvmw.pte, pteval,
						hsz);
			} else {
				dec_mm_counter(mm, mm_counter(folio));
				set_pte_at(mm, address, pvmw.pte, pteval);
			}
		} else if (likely(pte_present(pteval)) && pte_unused(pteval) &&
			   !userfaultfd_armed(vma)) {
			/*
			 * The guest indicated that the page content is of no
			 * interest anymore. Simply discard the pte, vmscan
			 * will take care of the rest.
			 * A future reference will then fault in a new zero
			 * page. When userfaultfd is active, we must not drop
			 * this page though, as its main user (postcopy
			 * migration) will not expect userfaults on already
			 * copied pages.
			 */
			dec_mm_counter(mm, mm_counter(folio));
		} else if (folio_test_anon(folio)) {
			swp_entry_t entry = page_swap_entry(subpage);
			pte_t swp_pte;
			/*
			 * Store the swap location in the pte.
			 * See handle_pte_fault() ...
			 */
			if (unlikely(folio_test_swapbacked(folio) !=
					folio_test_swapcache(folio))) {
				WARN_ON_ONCE(1);
				goto walk_abort;
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
				    (!folio_test_dirty(folio) ||
				     /*
				      * Unlike MADV_FREE mappings, VM_DROPPABLE
				      * ones can be dropped even if they've
				      * been dirtied.
				      */
				     (vma->vm_flags & VM_DROPPABLE))) {
					dec_mm_counter(mm, MM_ANONPAGES);
					goto discard;
				}

				/*
				 * If the folio was redirtied, it cannot be
				 * discarded. Remap the page to page table.
				 */
				set_pte_at(mm, address, pvmw.pte, pteval);
				/*
				 * Unlike MADV_FREE mappings, VM_DROPPABLE ones
				 * never get swap backed on failure to drop.
				 */
				if (!(vma->vm_flags & VM_DROPPABLE))
					folio_set_swapbacked(folio);
				goto walk_abort;
			}

			if (swap_duplicate(entry) < 0) {
				set_pte_at(mm, address, pvmw.pte, pteval);
				goto walk_abort;
			}

			/*
			 * arch_unmap_one() is expected to be a NOP on
			 * architectures where we could have PFN swap PTEs,
			 * so we'll not check/care.
			 */
			if (arch_unmap_one(mm, vma, address, pteval) < 0) {
				swap_free(entry);
				set_pte_at(mm, address, pvmw.pte, pteval);
				goto walk_abort;
			}

			/* See folio_try_share_anon_rmap(): clear PTE first. */
			if (anon_exclusive &&
			    folio_try_share_anon_rmap_pte(folio, subpage)) {
				swap_free(entry);
				set_pte_at(mm, address, pvmw.pte, pteval);
				goto walk_abort;
			}
			if (list_empty(&mm->mmlist)) {
				spin_lock(&mmlist_lock);
				if (list_empty(&mm->mmlist))
					list_add(&mm->mmlist, &init_mm.mmlist);
				spin_unlock(&mmlist_lock);
			}
			dec_mm_counter(mm, MM_ANONPAGES);
			inc_mm_counter(mm, MM_SWAPENTS);
			swp_pte = swp_entry_to_pte(entry);
			if (anon_exclusive)
				swp_pte = pte_swp_mkexclusive(swp_pte);
			if (likely(pte_present(pteval))) {
				if (pte_soft_dirty(pteval))
					swp_pte = pte_swp_mksoft_dirty(swp_pte);
				if (pte_uffd_wp(pteval))
					swp_pte = pte_swp_mkuffd_wp(swp_pte);
			} else {
				if (pte_swp_soft_dirty(pteval))
					swp_pte = pte_swp_mksoft_dirty(swp_pte);
				if (pte_swp_uffd_wp(pteval))
					swp_pte = pte_swp_mkuffd_wp(swp_pte);
			}
			set_pte_at(mm, address, pvmw.pte, swp_pte);
		} else {
			/*
			 * This is a locked file-backed folio,
			 * so it cannot be removed from the page
			 * cache and replaced by a new folio before
			 * mmu_notifier_invalidate_range_end, so no
			 * concurrent thread might update its page table
			 * to point at a new folio while a device is
			 * still using this folio.
			 *
			 * See Documentation/mm/mmu_notifier.rst
			 */
			dec_mm_counter(mm, mm_counter_file(folio));
		}
discard:
		if (unlikely(folio_test_hugetlb(folio)))
			hugetlb_remove_rmap(folio);
		else
			folio_remove_rmap_pte(folio, subpage, vma);
		if (vma->vm_flags & VM_LOCKED)
			mlock_drain_local();
		folio_put(folio);
		continue;
walk_abort:
		ret = false;
walk_done:
		page_vma_mapped_walk_done(&pvmw);
		break;
	}

	mmu_notifier_invalidate_range_end(&range);

	return ret;
}

static bool invalid_migration_vma(struct vm_area_struct *vma, void *arg)
{
	return vma_is_temporary_stack(vma);
}

static int folio_not_mapped(struct folio *folio)
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
		.done = folio_not_mapped,
		.anon_lock = folio_lock_anon_vma_read,
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
	bool anon_exclusive, writable, ret = true;
	pte_t pteval;
	struct page *subpage;
	struct mmu_notifier_range range;
	enum ttu_flags flags = (enum ttu_flags)(long)arg;
	unsigned long pfn;
	unsigned long hsz = 0;

	/*
	 * When racing against e.g. zap_pte_range() on another cpu,
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
	 * Note that the page can not be free in this function as call of
	 * try_to_unmap() must hold a reference on the page.
	 */
	range.end = vma_address_end(&pvmw);
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma->vm_mm,
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
	mmu_notifier_invalidate_range_start(&range);

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

		/*
		 * Handle PFN swap PTEs, such as device-exclusive ones, that
		 * actually map pages.
		 */
		pteval = ptep_get(pvmw.pte);
		if (likely(pte_present(pteval))) {
			pfn = pte_pfn(pteval);
		} else {
			pfn = swp_offset_pfn(pte_to_swp_entry(pteval));
			VM_WARN_ON_FOLIO(folio_test_hugetlb(folio), folio);
		}

		subpage = folio_page(folio, pfn - folio_pfn(folio));
		address = pvmw.address;
		anon_exclusive = folio_test_anon(folio) &&
				 PageAnonExclusive(subpage);

		if (folio_test_hugetlb(folio)) {
			bool anon = folio_test_anon(folio);

			/*
			 * huge_pmd_unshare may unmap an entire PMD page.
			 * There is no way of knowing exactly which PMDs may
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
			if (!anon) {
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
					 * no other sharing, huge_pmd_unshare
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
			if (pte_dirty(pteval))
				folio_mark_dirty(folio);
			writable = pte_write(pteval);
		} else if (likely(pte_present(pteval))) {
			flush_cache_page(vma, address, pfn);
			/* Nuke the page table entry. */
			if (should_defer_flush(mm, flags)) {
				/*
				 * We clear the PTE but do not flush so potentially
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
			if (pte_dirty(pteval))
				folio_mark_dirty(folio);
			writable = pte_write(pteval);
		} else {
			pte_clear(mm, address, pvmw.pte);
			writable = is_writable_device_private_entry(pte_to_swp_entry(pteval));
		}

		VM_WARN_ON_FOLIO(writable && folio_test_anon(folio) &&
				!anon_exclusive, folio);

		/* Update high watermark before we lower rss */
		update_hiwater_rss(mm);

		if (PageHWPoison(subpage)) {
			VM_WARN_ON_FOLIO(folio_is_device_private(folio), folio);

			pteval = swp_entry_to_pte(make_hwpoison_entry(subpage));
			if (folio_test_hugetlb(folio)) {
				hugetlb_count_sub(folio_nr_pages(folio), mm);
				set_huge_pte_at(mm, address, pvmw.pte, pteval,
						hsz);
			} else {
				dec_mm_counter(mm, mm_counter(folio));
				set_pte_at(mm, address, pvmw.pte, pteval);
			}
		} else if (likely(pte_present(pteval)) && pte_unused(pteval) &&
			   !userfaultfd_armed(vma)) {
			/*
			 * The guest indicated that the page content is of no
			 * interest anymore. Simply discard the pte, vmscan
			 * will take care of the rest.
			 * A future reference will then fault in a new zero
			 * page. When userfaultfd is active, we must not drop
			 * this page though, as its main user (postcopy
			 * migration) will not expect userfaults on already
			 * copied pages.
			 */
			dec_mm_counter(mm, mm_counter(folio));
		} else {
			swp_entry_t entry;
			pte_t swp_pte;

			/*
			 * arch_unmap_one() is expected to be a NOP on
			 * architectures where we could have PFN swap PTEs,
			 * so we'll not check/care.
			 */
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

			/* See folio_try_share_anon_rmap_pte(): clear PTE first. */
			if (folio_test_hugetlb(folio)) {
				if (anon_exclusive &&
				    hugetlb_try_share_anon_rmap(folio)) {
					set_huge_pte_at(mm, address, pvmw.pte,
							pteval, hsz);
					ret = false;
					page_vma_mapped_walk_done(&pvmw);
					break;
				}
			} else if (anon_exclusive &&
				   folio_try_share_anon_rmap_pte(folio, subpage)) {
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
			if (writable)
				entry = make_writable_migration_entry(
							page_to_pfn(subpage));
			else if (anon_exclusive)
				entry = make_readable_exclusive_migration_entry(
							page_to_pfn(subpage));
			else
				entry = make_readable_migration_entry(
							page_to_pfn(subpage));
			if (likely(pte_present(pteval))) {
				if (pte_young(pteval))
					entry = make_migration_entry_young(entry);
				if (pte_dirty(pteval))
					entry = make_migration_entry_dirty(entry);
				swp_pte = swp_entry_to_pte(entry);
				if (pte_soft_dirty(pteval))
					swp_pte = pte_swp_mksoft_dirty(swp_pte);
				if (pte_uffd_wp(pteval))
					swp_pte = pte_swp_mkuffd_wp(swp_pte);
			} else {
				swp_pte = swp_entry_to_pte(entry);
				if (pte_swp_soft_dirty(pteval))
					swp_pte = pte_swp_mksoft_dirty(swp_pte);
				if (pte_swp_uffd_wp(pteval))
					swp_pte = pte_swp_mkuffd_wp(swp_pte);
			}
			if (folio_test_hugetlb(folio))
				set_huge_pte_at(mm, address, pvmw.pte, swp_pte,
						hsz);
			else
				set_pte_at(mm, address, pvmw.pte, swp_pte);
			trace_set_migration_pte(address, pte_val(swp_pte),
						folio_order(folio));
			/*
			 * No need to invalidate here it will synchronize on
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

	mmu_notifier_invalidate_range_end(&range);

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
		.done = folio_not_mapped,
		.anon_lock = folio_lock_anon_vma_read,
	};

	/*
	 * Migration always ignores mlock and only supports TTU_RMAP_LOCKED and
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
	 * The VMA is moved under the anon_vma lock but not the
	 * page tables leading to a race where migration cannot
	 * find the migration ptes. Rather than increasing the
	 * locking requirements of exec(), migration skips
	 * temporary VMAs until after exec() completes.
	 */
	if (!folio_test_ksm(folio) && folio_test_anon(folio))
		rwc.invalid_vma = invalid_migration_vma;

	if (flags & TTU_RMAP_LOCKED)
		rmap_walk_locked(folio, &rwc);
	else
		rmap_walk(folio, &rwc);
}

#ifdef CONFIG_DEVICE_PRIVATE
/**
 * make_device_exclusive() - Mark a page for exclusive use by a device
 * @mm: mm_struct of associated target process
 * @addr: the virtual address to mark for exclusive device access
 * @owner: passed to MMU_NOTIFY_EXCLUSIVE range notifier to allow filtering
 * @foliop: folio pointer will be stored here on success.
 *
 * This function looks up the page mapped at the given address, grabs a
 * folio reference, locks the folio and replaces the PTE with special
 * device-exclusive PFN swap entry, preventing access through the process
 * page tables. The function will return with the folio locked and referenced.
 *
 * On fault, the device-exclusive entries are replaced with the original PTE
 * under folio lock, after calling MMU notifiers.
 *
 * Only anonymous non-hugetlb folios are supported and the VMA must have
 * write permissions such that we can fault in the anonymous page writable
 * in order to mark it exclusive. The caller must hold the mmap_lock in read
 * mode.
 *
 * A driver using this to program access from a device must use a mmu notifier
 * critical section to hold a device specific lock during programming. Once
 * programming is complete it should drop the folio lock and reference after
 * which point CPU access to the page will revoke the exclusive access.
 *
 * Notes:
 *   #. This function always operates on individual PTEs mapping individual
 *      pages. PMD-sized THPs are first remapped to be mapped by PTEs before
 *      the conversion happens on a single PTE corresponding to @addr.
 *   #. While concurrent access through the process page tables is prevented,
 *      concurrent access through other page references (e.g., earlier GUP
 *      invocation) is not handled and not supported.
 *   #. device-exclusive entries are considered "clean" and "old" by core-mm.
 *      Device drivers must update the folio state when informed by MMU
 *      notifiers.
 *
 * Returns: pointer to mapped page on success, otherwise a negative error.
 */
struct page *make_device_exclusive(struct mm_struct *mm, unsigned long addr,
		void *owner, struct folio **foliop)
{
	struct mmu_notifier_range range;
	struct folio *folio, *fw_folio;
	struct vm_area_struct *vma;
	struct folio_walk fw;
	struct page *page;
	swp_entry_t entry;
	pte_t swp_pte;
	int ret;

	mmap_assert_locked(mm);
	addr = PAGE_ALIGN_DOWN(addr);

	/*
	 * Fault in the page writable and try to lock it; note that if the
	 * address would already be marked for exclusive use by a device,
	 * the GUP call would undo that first by triggering a fault.
	 *
	 * If any other device would already map this page exclusively, the
	 * fault will trigger a conversion to an ordinary
	 * (non-device-exclusive) PTE and issue a MMU_NOTIFY_EXCLUSIVE.
	 */
retry:
	page = get_user_page_vma_remote(mm, addr,
					FOLL_GET | FOLL_WRITE | FOLL_SPLIT_PMD,
					&vma);
	if (IS_ERR(page))
		return page;
	folio = page_folio(page);

	if (!folio_test_anon(folio) || folio_test_hugetlb(folio)) {
		folio_put(folio);
		return ERR_PTR(-EOPNOTSUPP);
	}

	ret = folio_lock_killable(folio);
	if (ret) {
		folio_put(folio);
		return ERR_PTR(ret);
	}

	/*
	 * Inform secondary MMUs that we are going to convert this PTE to
	 * device-exclusive, such that they unmap it now. Note that the
	 * caller must filter this event out to prevent livelocks.
	 */
	mmu_notifier_range_init_owner(&range, MMU_NOTIFY_EXCLUSIVE, 0,
				      mm, addr, addr + PAGE_SIZE, owner);
	mmu_notifier_invalidate_range_start(&range);

	/*
	 * Let's do a second walk and make sure we still find the same page
	 * mapped writable. Note that any page of an anonymous folio can
	 * only be mapped writable using exactly one PTE ("exclusive"), so
	 * there cannot be other mappings.
	 */
	fw_folio = folio_walk_start(&fw, vma, addr, 0);
	if (fw_folio != folio || fw.page != page ||
	    fw.level != FW_LEVEL_PTE || !pte_write(fw.pte)) {
		if (fw_folio)
			folio_walk_end(&fw, vma);
		mmu_notifier_invalidate_range_end(&range);
		folio_unlock(folio);
		folio_put(folio);
		goto retry;
	}

	/* Nuke the page table entry so we get the uptodate dirty bit. */
	flush_cache_page(vma, addr, page_to_pfn(page));
	fw.pte = ptep_clear_flush(vma, addr, fw.ptep);

	/* Set the dirty flag on the folio now the PTE is gone. */
	if (pte_dirty(fw.pte))
		folio_mark_dirty(folio);

	/*
	 * Store the pfn of the page in a special device-exclusive PFN swap PTE.
	 * do_swap_page() will trigger the conversion back while holding the
	 * folio lock.
	 */
	entry = make_device_exclusive_entry(page_to_pfn(page));
	swp_pte = swp_entry_to_pte(entry);
	if (pte_soft_dirty(fw.pte))
		swp_pte = pte_swp_mksoft_dirty(swp_pte);
	/* The pte is writable, uffd-wp does not apply. */
	set_pte_at(mm, addr, fw.ptep, swp_pte);

	folio_walk_end(&fw, vma);
	mmu_notifier_invalidate_range_end(&range);
	*foliop = folio;
	return page;
}
EXPORT_SYMBOL_GPL(make_device_exclusive);
#endif

void __put_anon_vma(struct anon_vma *anon_vma)
{
	struct anon_vma *root = anon_vma->root;

	anon_vma_free(anon_vma);
	if (root != anon_vma && atomic_dec_and_test(&root->refcount))
		anon_vma_free(root);
}

static struct anon_vma *rmap_walk_anon_lock(const struct folio *folio,
					    struct rmap_walk_control *rwc)
{
	struct anon_vma *anon_vma;

	if (rwc->anon_lock)
		return rwc->anon_lock(folio, rwc);

	/*
	 * Note: remove_migration_ptes() cannot use folio_lock_anon_vma_read()
	 * because that depends on page_mapped(); but not all its usages
	 * are holding mmap_lock. Users without mmap_lock are required to
	 * take a reference count to prevent the anon_vma disappearing
	 */
	anon_vma = folio_anon_vma(folio);
	if (!anon_vma)
		return NULL;

	if (anon_vma_trylock_read(anon_vma))
		goto out;

	if (rwc->try_lock) {
		anon_vma = NULL;
		rwc->contended = true;
		goto out;
	}

	anon_vma_lock_read(anon_vma);
out:
	return anon_vma;
}

/*
 * rmap_walk_anon - do something to anonymous page using the object-based
 * rmap method
 * @folio: the folio to be handled
 * @rwc: control variable according to each walk type
 * @locked: caller holds relevant rmap lock
 *
 * Find all the mappings of a folio using the mapping pointer and the vma
 * chains contained in the anon_vma struct it points to.
 */
static void rmap_walk_anon(struct folio *folio,
		struct rmap_walk_control *rwc, bool locked)
{
	struct anon_vma *anon_vma;
	pgoff_t pgoff_start, pgoff_end;
	struct anon_vma_chain *avc;

	if (locked) {
		anon_vma = folio_anon_vma(folio);
		/* anon_vma disappear under us? */
		VM_BUG_ON_FOLIO(!anon_vma, folio);
	} else {
		anon_vma = rmap_walk_anon_lock(folio, rwc);
	}
	if (!anon_vma)
		return;

	pgoff_start = folio_pgoff(folio);
	pgoff_end = pgoff_start + folio_nr_pages(folio) - 1;
	anon_vma_interval_tree_foreach(avc, &anon_vma->rb_root,
			pgoff_start, pgoff_end) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(vma, pgoff_start,
				folio_nr_pages(folio));

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
		anon_vma_unlock_read(anon_vma);
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
	 * The page lock not only makes sure that page->mapping cannot
	 * suddenly be NULLified by truncation, it makes sure that the
	 * structure at mapping cannot be freed and reused yet,
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
		unsigned long address = vma_address(vma, pgoff_start,
			       folio_nr_pages(folio));

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
	else if (folio_test_anon(folio))
		rmap_walk_anon(folio, rwc, false);
	else
		rmap_walk_file(folio, rwc, false);
}

/* Like rmap_walk, but caller holds relevant rmap lock */
void rmap_walk_locked(struct folio *folio, struct rmap_walk_control *rwc)
{
	/* no ksm support for now */
	VM_BUG_ON_FOLIO(folio_test_ksm(folio), folio);
	if (folio_test_anon(folio))
		rmap_walk_anon(folio, rwc, true);
	else
		rmap_walk_file(folio, rwc, true);
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * The following two functions are for anonymous (private mapped) hugepages.
 * Unlike common anonymous pages, anonymous hugepages have no accounting code
 * and no lru code, because we handle hugepages differently from common pages.
 */
void hugetlb_add_anon_rmap(struct folio *folio, struct vm_area_struct *vma,
		unsigned long address, rmap_t flags)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);
	VM_WARN_ON_FOLIO(!folio_test_anon(folio), folio);

	atomic_inc(&folio->_entire_mapcount);
	atomic_inc(&folio->_large_mapcount);
	if (flags & RMAP_EXCLUSIVE)
		SetPageAnonExclusive(&folio->page);
	VM_WARN_ON_FOLIO(folio_entire_mapcount(folio) > 1 &&
			 PageAnonExclusive(&folio->page), folio);
}

void hugetlb_add_new_anon_rmap(struct folio *folio,
		struct vm_area_struct *vma, unsigned long address)
{
	VM_WARN_ON_FOLIO(!folio_test_hugetlb(folio), folio);

	BUG_ON(address < vma->vm_start || address >= vma->vm_end);
	/* increment count (starts at -1) */
	atomic_set(&folio->_entire_mapcount, 0);
	atomic_set(&folio->_large_mapcount, 0);
	folio_clear_hugetlb_restore_reserve(folio);
	__folio_set_anon(folio, vma, address, true);
	SetPageAnonExclusive(&folio->page);
}
#endif /* CONFIG_HUGETLB_PAGE */
