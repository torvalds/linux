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
 * the ayesn methods track ayesnymous pages, and
 * the file methods track pages belonging to an iyesde.
 *
 * Original design by Rik van Riel <riel@conectiva.com.br> 2001
 * File methods by Dave McCracken <dmccr@us.ibm.com> 2003, 2004
 * Ayesnymous methods by Andrea Arcangeli <andrea@suse.de> 2004
 * Contributions by Hugh Dickins 2003, 2004
 */

/*
 * Lock ordering in mm:
 *
 * iyesde->i_mutex	(while writing or truncating, yest reading or faulting)
 *   mm->mmap_sem
 *     page->flags PG_locked (lock_page)
 *       hugetlbfs_i_mmap_rwsem_key (in huge_pmd_share)
 *         mapping->i_mmap_rwsem
 *           ayesn_vma->rwsem
 *             mm->page_table_lock or pte_lock
 *               pgdat->lru_lock (in mark_page_accessed, isolate_lru_page)
 *               swap_lock (in swap_duplicate, swap_info_get)
 *                 mmlist_lock (in mmput, drain_mmlist and others)
 *                 mapping->private_lock (in __set_page_dirty_buffers)
 *                   mem_cgroup_{begin,end}_page_stat (memcg->move_lock)
 *                     i_pages lock (widely used)
 *                 iyesde->i_lock (in set_page_dirty's __mark_iyesde_dirty)
 *                 bdi.wb->list_lock (in set_page_dirty's __mark_iyesde_dirty)
 *                   sb_lock (within iyesde_lock in fs/fs-writeback.c)
 *                   i_pages lock (widely used, in set_page_dirty,
 *                             in arch-dependent flush_dcache_mmap_lock,
 *                             within bdi.wb->list_lock in __sync_single_iyesde)
 *
 * ayesn_vma->rwsem,mapping->i_mutex      (memory_failure, collect_procs_ayesn)
 *   ->tasklist_lock
 *     pte map lock
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
#include <linux/mmu_yestifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/huge_mm.h>
#include <linux/backing-dev.h>
#include <linux/page_idle.h>
#include <linux/memremap.h>
#include <linux/userfaultfd_k.h>

#include <asm/tlbflush.h>

#include <trace/events/tlb.h>

#include "internal.h"

static struct kmem_cache *ayesn_vma_cachep;
static struct kmem_cache *ayesn_vma_chain_cachep;

static inline struct ayesn_vma *ayesn_vma_alloc(void)
{
	struct ayesn_vma *ayesn_vma;

	ayesn_vma = kmem_cache_alloc(ayesn_vma_cachep, GFP_KERNEL);
	if (ayesn_vma) {
		atomic_set(&ayesn_vma->refcount, 1);
		ayesn_vma->degree = 1;	/* Reference for first vma */
		ayesn_vma->parent = ayesn_vma;
		/*
		 * Initialise the ayesn_vma root to point to itself. If called
		 * from fork, the root will be reset to the parents ayesn_vma.
		 */
		ayesn_vma->root = ayesn_vma;
	}

	return ayesn_vma;
}

static inline void ayesn_vma_free(struct ayesn_vma *ayesn_vma)
{
	VM_BUG_ON(atomic_read(&ayesn_vma->refcount));

	/*
	 * Synchronize against page_lock_ayesn_vma_read() such that
	 * we can safely hold the lock without the ayesn_vma getting
	 * freed.
	 *
	 * Relies on the full mb implied by the atomic_dec_and_test() from
	 * put_ayesn_vma() against the acquire barrier implied by
	 * down_read_trylock() from page_lock_ayesn_vma_read(). This orders:
	 *
	 * page_lock_ayesn_vma_read()	VS	put_ayesn_vma()
	 *   down_read_trylock()		  atomic_dec_and_test()
	 *   LOCK				  MB
	 *   atomic_read()			  rwsem_is_locked()
	 *
	 * LOCK should suffice since the actual taking of the lock must
	 * happen _before_ what follows.
	 */
	might_sleep();
	if (rwsem_is_locked(&ayesn_vma->root->rwsem)) {
		ayesn_vma_lock_write(ayesn_vma);
		ayesn_vma_unlock_write(ayesn_vma);
	}

	kmem_cache_free(ayesn_vma_cachep, ayesn_vma);
}

static inline struct ayesn_vma_chain *ayesn_vma_chain_alloc(gfp_t gfp)
{
	return kmem_cache_alloc(ayesn_vma_chain_cachep, gfp);
}

static void ayesn_vma_chain_free(struct ayesn_vma_chain *ayesn_vma_chain)
{
	kmem_cache_free(ayesn_vma_chain_cachep, ayesn_vma_chain);
}

static void ayesn_vma_chain_link(struct vm_area_struct *vma,
				struct ayesn_vma_chain *avc,
				struct ayesn_vma *ayesn_vma)
{
	avc->vma = vma;
	avc->ayesn_vma = ayesn_vma;
	list_add(&avc->same_vma, &vma->ayesn_vma_chain);
	ayesn_vma_interval_tree_insert(avc, &ayesn_vma->rb_root);
}

/**
 * __ayesn_vma_prepare - attach an ayesn_vma to a memory region
 * @vma: the memory region in question
 *
 * This makes sure the memory mapping described by 'vma' has
 * an 'ayesn_vma' attached to it, so that we can associate the
 * ayesnymous pages mapped into it with that ayesn_vma.
 *
 * The common case will be that we already have one, which
 * is handled inline by ayesn_vma_prepare(). But if
 * yest we either need to find an adjacent mapping that we
 * can re-use the ayesn_vma from (very common when the only
 * reason for splitting a vma has been mprotect()), or we
 * allocate a new one.
 *
 * Ayesn-vma allocations are very subtle, because we may have
 * optimistically looked up an ayesn_vma in page_lock_ayesn_vma_read()
 * and that may actually touch the spinlock even in the newly
 * allocated vma (it depends on RCU to make sure that the
 * ayesn_vma isn't actually destroyed).
 *
 * As a result, we need to do proper ayesn_vma locking even
 * for the new allocation. At the same time, we do yest want
 * to do any locking for the common case of already having
 * an ayesn_vma.
 *
 * This must be called with the mmap_sem held for reading.
 */
int __ayesn_vma_prepare(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	struct ayesn_vma *ayesn_vma, *allocated;
	struct ayesn_vma_chain *avc;

	might_sleep();

	avc = ayesn_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_eyesmem;

	ayesn_vma = find_mergeable_ayesn_vma(vma);
	allocated = NULL;
	if (!ayesn_vma) {
		ayesn_vma = ayesn_vma_alloc();
		if (unlikely(!ayesn_vma))
			goto out_eyesmem_free_avc;
		allocated = ayesn_vma;
	}

	ayesn_vma_lock_write(ayesn_vma);
	/* page_table_lock to protect against threads */
	spin_lock(&mm->page_table_lock);
	if (likely(!vma->ayesn_vma)) {
		vma->ayesn_vma = ayesn_vma;
		ayesn_vma_chain_link(vma, avc, ayesn_vma);
		/* vma reference or self-parent link for new root */
		ayesn_vma->degree++;
		allocated = NULL;
		avc = NULL;
	}
	spin_unlock(&mm->page_table_lock);
	ayesn_vma_unlock_write(ayesn_vma);

	if (unlikely(allocated))
		put_ayesn_vma(allocated);
	if (unlikely(avc))
		ayesn_vma_chain_free(avc);

	return 0;

 out_eyesmem_free_avc:
	ayesn_vma_chain_free(avc);
 out_eyesmem:
	return -ENOMEM;
}

/*
 * This is a useful helper function for locking the ayesn_vma root as
 * we traverse the vma->ayesn_vma_chain, looping over ayesn_vma's that
 * have the same vma.
 *
 * Such ayesn_vma's should have the same root, so you'd expect to see
 * just a single mutex_lock for the whole traversal.
 */
static inline struct ayesn_vma *lock_ayesn_vma_root(struct ayesn_vma *root, struct ayesn_vma *ayesn_vma)
{
	struct ayesn_vma *new_root = ayesn_vma->root;
	if (new_root != root) {
		if (WARN_ON_ONCE(root))
			up_write(&root->rwsem);
		root = new_root;
		down_write(&root->rwsem);
	}
	return root;
}

static inline void unlock_ayesn_vma_root(struct ayesn_vma *root)
{
	if (root)
		up_write(&root->rwsem);
}

/*
 * Attach the ayesn_vmas from src to dst.
 * Returns 0 on success, -ENOMEM on failure.
 *
 * ayesn_vma_clone() is called by __vma_split(), __split_vma(), copy_vma() and
 * ayesn_vma_fork(). The first three want an exact copy of src, while the last
 * one, ayesn_vma_fork(), may try to reuse an existing ayesn_vma to prevent
 * endless growth of ayesn_vma. Since dst->ayesn_vma is set to NULL before call,
 * we can identify this case by checking (!dst->ayesn_vma && src->ayesn_vma).
 *
 * If (!dst->ayesn_vma && src->ayesn_vma) is true, this function tries to find
 * and reuse existing ayesn_vma which has yes vmas and only one child ayesn_vma.
 * This prevents degradation of ayesn_vma hierarchy to endless linear chain in
 * case of constantly forking task. On the other hand, an ayesn_vma with more
 * than one child isn't reused even if there was yes alive vma, thus rmap
 * walker has a good chance of avoiding scanning the whole hierarchy when it
 * searches where page is mapped.
 */
int ayesn_vma_clone(struct vm_area_struct *dst, struct vm_area_struct *src)
{
	struct ayesn_vma_chain *avc, *pavc;
	struct ayesn_vma *root = NULL;
	struct vm_area_struct *prev = dst->vm_prev, *pprev = src->vm_prev;

	/*
	 * If parent share ayesn_vma with its vm_prev, keep this sharing in in
	 * child.
	 *
	 * 1. Parent has vm_prev, which implies we have vm_prev.
	 * 2. Parent and its vm_prev have the same ayesn_vma.
	 */
	if (!dst->ayesn_vma && src->ayesn_vma &&
	    pprev && pprev->ayesn_vma == src->ayesn_vma)
		dst->ayesn_vma = prev->ayesn_vma;


	list_for_each_entry_reverse(pavc, &src->ayesn_vma_chain, same_vma) {
		struct ayesn_vma *ayesn_vma;

		avc = ayesn_vma_chain_alloc(GFP_NOWAIT | __GFP_NOWARN);
		if (unlikely(!avc)) {
			unlock_ayesn_vma_root(root);
			root = NULL;
			avc = ayesn_vma_chain_alloc(GFP_KERNEL);
			if (!avc)
				goto eyesmem_failure;
		}
		ayesn_vma = pavc->ayesn_vma;
		root = lock_ayesn_vma_root(root, ayesn_vma);
		ayesn_vma_chain_link(dst, avc, ayesn_vma);

		/*
		 * Reuse existing ayesn_vma if its degree lower than two,
		 * that means it has yes vma and only one ayesn_vma child.
		 *
		 * Do yest chose parent ayesn_vma, otherwise first child
		 * will always reuse it. Root ayesn_vma is never reused:
		 * it has self-parent reference and at least one child.
		 */
		if (!dst->ayesn_vma && src->ayesn_vma &&
		    ayesn_vma != src->ayesn_vma && ayesn_vma->degree < 2)
			dst->ayesn_vma = ayesn_vma;
	}
	if (dst->ayesn_vma)
		dst->ayesn_vma->degree++;
	unlock_ayesn_vma_root(root);
	return 0;

 eyesmem_failure:
	/*
	 * dst->ayesn_vma is dropped here otherwise its degree can be incorrectly
	 * decremented in unlink_ayesn_vmas().
	 * We can safely do this because callers of ayesn_vma_clone() don't care
	 * about dst->ayesn_vma if ayesn_vma_clone() failed.
	 */
	dst->ayesn_vma = NULL;
	unlink_ayesn_vmas(dst);
	return -ENOMEM;
}

/*
 * Attach vma to its own ayesn_vma, as well as to the ayesn_vmas that
 * the corresponding VMA in the parent process is attached to.
 * Returns 0 on success, yesn-zero on failure.
 */
int ayesn_vma_fork(struct vm_area_struct *vma, struct vm_area_struct *pvma)
{
	struct ayesn_vma_chain *avc;
	struct ayesn_vma *ayesn_vma;
	int error;

	/* Don't bother if the parent process has yes ayesn_vma here. */
	if (!pvma->ayesn_vma)
		return 0;

	/* Drop inherited ayesn_vma, we'll reuse existing or allocate new. */
	vma->ayesn_vma = NULL;

	/*
	 * First, attach the new VMA to the parent VMA's ayesn_vmas,
	 * so rmap can find yesn-COWed pages in child processes.
	 */
	error = ayesn_vma_clone(vma, pvma);
	if (error)
		return error;

	/* An existing ayesn_vma has been reused, all done then. */
	if (vma->ayesn_vma)
		return 0;

	/* Then add our own ayesn_vma. */
	ayesn_vma = ayesn_vma_alloc();
	if (!ayesn_vma)
		goto out_error;
	avc = ayesn_vma_chain_alloc(GFP_KERNEL);
	if (!avc)
		goto out_error_free_ayesn_vma;

	/*
	 * The root ayesn_vma's spinlock is the lock actually used when we
	 * lock any of the ayesn_vmas in this ayesn_vma tree.
	 */
	ayesn_vma->root = pvma->ayesn_vma->root;
	ayesn_vma->parent = pvma->ayesn_vma;
	/*
	 * With refcounts, an ayesn_vma can stay around longer than the
	 * process it belongs to. The root ayesn_vma needs to be pinned until
	 * this ayesn_vma is freed, because the lock lives in the root.
	 */
	get_ayesn_vma(ayesn_vma->root);
	/* Mark this ayesn_vma as the one where our new (COWed) pages go. */
	vma->ayesn_vma = ayesn_vma;
	ayesn_vma_lock_write(ayesn_vma);
	ayesn_vma_chain_link(vma, avc, ayesn_vma);
	ayesn_vma->parent->degree++;
	ayesn_vma_unlock_write(ayesn_vma);

	return 0;

 out_error_free_ayesn_vma:
	put_ayesn_vma(ayesn_vma);
 out_error:
	unlink_ayesn_vmas(vma);
	return -ENOMEM;
}

void unlink_ayesn_vmas(struct vm_area_struct *vma)
{
	struct ayesn_vma_chain *avc, *next;
	struct ayesn_vma *root = NULL;

	/*
	 * Unlink each ayesn_vma chained to the VMA.  This list is ordered
	 * from newest to oldest, ensuring the root ayesn_vma gets freed last.
	 */
	list_for_each_entry_safe(avc, next, &vma->ayesn_vma_chain, same_vma) {
		struct ayesn_vma *ayesn_vma = avc->ayesn_vma;

		root = lock_ayesn_vma_root(root, ayesn_vma);
		ayesn_vma_interval_tree_remove(avc, &ayesn_vma->rb_root);

		/*
		 * Leave empty ayesn_vmas on the list - we'll need
		 * to free them outside the lock.
		 */
		if (RB_EMPTY_ROOT(&ayesn_vma->rb_root.rb_root)) {
			ayesn_vma->parent->degree--;
			continue;
		}

		list_del(&avc->same_vma);
		ayesn_vma_chain_free(avc);
	}
	if (vma->ayesn_vma)
		vma->ayesn_vma->degree--;
	unlock_ayesn_vma_root(root);

	/*
	 * Iterate the list once more, it yesw only contains empty and unlinked
	 * ayesn_vmas, destroy them. Could yest do before due to __put_ayesn_vma()
	 * needing to write-acquire the ayesn_vma->root->rwsem.
	 */
	list_for_each_entry_safe(avc, next, &vma->ayesn_vma_chain, same_vma) {
		struct ayesn_vma *ayesn_vma = avc->ayesn_vma;

		VM_WARN_ON(ayesn_vma->degree);
		put_ayesn_vma(ayesn_vma);

		list_del(&avc->same_vma);
		ayesn_vma_chain_free(avc);
	}
}

static void ayesn_vma_ctor(void *data)
{
	struct ayesn_vma *ayesn_vma = data;

	init_rwsem(&ayesn_vma->rwsem);
	atomic_set(&ayesn_vma->refcount, 0);
	ayesn_vma->rb_root = RB_ROOT_CACHED;
}

void __init ayesn_vma_init(void)
{
	ayesn_vma_cachep = kmem_cache_create("ayesn_vma", sizeof(struct ayesn_vma),
			0, SLAB_TYPESAFE_BY_RCU|SLAB_PANIC|SLAB_ACCOUNT,
			ayesn_vma_ctor);
	ayesn_vma_chain_cachep = KMEM_CACHE(ayesn_vma_chain,
			SLAB_PANIC|SLAB_ACCOUNT);
}

/*
 * Getting a lock on a stable ayesn_vma from a page off the LRU is tricky!
 *
 * Since there is yes serialization what so ever against page_remove_rmap()
 * the best this function can do is return a locked ayesn_vma that might
 * have been relevant to this page.
 *
 * The page might have been remapped to a different ayesn_vma or the ayesn_vma
 * returned may already be freed (and even reused).
 *
 * In case it was remapped to a different ayesn_vma, the new ayesn_vma will be a
 * child of the old ayesn_vma, and the ayesn_vma lifetime rules will therefore
 * ensure that any ayesn_vma obtained from the page will still be valid for as
 * long as we observe page_mapped() [ hence all those page_mapped() tests ].
 *
 * All users of this function must be very careful when walking the ayesn_vma
 * chain and verify that the page in question is indeed mapped in it
 * [ something equivalent to page_mapped_in_vma() ].
 *
 * Since ayesn_vma's slab is SLAB_TYPESAFE_BY_RCU and we kyesw from
 * page_remove_rmap() that the ayesn_vma pointer from page->mapping is valid
 * if there is a mapcount, we can dereference the ayesn_vma after observing
 * those.
 */
struct ayesn_vma *page_get_ayesn_vma(struct page *page)
{
	struct ayesn_vma *ayesn_vma = NULL;
	unsigned long ayesn_mapping;

	rcu_read_lock();
	ayesn_mapping = (unsigned long)READ_ONCE(page->mapping);
	if ((ayesn_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		goto out;
	if (!page_mapped(page))
		goto out;

	ayesn_vma = (struct ayesn_vma *) (ayesn_mapping - PAGE_MAPPING_ANON);
	if (!atomic_inc_yest_zero(&ayesn_vma->refcount)) {
		ayesn_vma = NULL;
		goto out;
	}

	/*
	 * If this page is still mapped, then its ayesn_vma canyest have been
	 * freed.  But if it has been unmapped, we have yes security against the
	 * ayesn_vma structure being freed and reused (for ayesther ayesn_vma:
	 * SLAB_TYPESAFE_BY_RCU guarantees that - so the atomic_inc_yest_zero()
	 * above canyest corrupt).
	 */
	if (!page_mapped(page)) {
		rcu_read_unlock();
		put_ayesn_vma(ayesn_vma);
		return NULL;
	}
out:
	rcu_read_unlock();

	return ayesn_vma;
}

/*
 * Similar to page_get_ayesn_vma() except it locks the ayesn_vma.
 *
 * Its a little more complex as it tries to keep the fast path to a single
 * atomic op -- the trylock. If we fail the trylock, we fall back to getting a
 * reference like with page_get_ayesn_vma() and then block on the mutex.
 */
struct ayesn_vma *page_lock_ayesn_vma_read(struct page *page)
{
	struct ayesn_vma *ayesn_vma = NULL;
	struct ayesn_vma *root_ayesn_vma;
	unsigned long ayesn_mapping;

	rcu_read_lock();
	ayesn_mapping = (unsigned long)READ_ONCE(page->mapping);
	if ((ayesn_mapping & PAGE_MAPPING_FLAGS) != PAGE_MAPPING_ANON)
		goto out;
	if (!page_mapped(page))
		goto out;

	ayesn_vma = (struct ayesn_vma *) (ayesn_mapping - PAGE_MAPPING_ANON);
	root_ayesn_vma = READ_ONCE(ayesn_vma->root);
	if (down_read_trylock(&root_ayesn_vma->rwsem)) {
		/*
		 * If the page is still mapped, then this ayesn_vma is still
		 * its ayesn_vma, and holding the mutex ensures that it will
		 * yest go away, see ayesn_vma_free().
		 */
		if (!page_mapped(page)) {
			up_read(&root_ayesn_vma->rwsem);
			ayesn_vma = NULL;
		}
		goto out;
	}

	/* trylock failed, we got to sleep */
	if (!atomic_inc_yest_zero(&ayesn_vma->refcount)) {
		ayesn_vma = NULL;
		goto out;
	}

	if (!page_mapped(page)) {
		rcu_read_unlock();
		put_ayesn_vma(ayesn_vma);
		return NULL;
	}

	/* we pinned the ayesn_vma, its safe to sleep */
	rcu_read_unlock();
	ayesn_vma_lock_read(ayesn_vma);

	if (atomic_dec_and_test(&ayesn_vma->refcount)) {
		/*
		 * Oops, we held the last refcount, release the lock
		 * and bail -- can't simply use put_ayesn_vma() because
		 * we'll deadlock on the ayesn_vma_lock_write() recursion.
		 */
		ayesn_vma_unlock_read(ayesn_vma);
		__put_ayesn_vma(ayesn_vma);
		ayesn_vma = NULL;
	}

	return ayesn_vma;

out:
	rcu_read_unlock();
	return ayesn_vma;
}

void page_unlock_ayesn_vma_read(struct ayesn_vma *ayesn_vma)
{
	ayesn_vma_unlock_read(ayesn_vma);
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

static void set_tlb_ubc_flush_pending(struct mm_struct *mm, bool writable)
{
	struct tlbflush_unmap_batch *tlb_ubc = &current->tlb_ubc;

	arch_tlbbatch_add_mm(&tlb_ubc->arch, mm);
	tlb_ubc->flush_required = true;

	/*
	 * Ensure compiler does yest re-order the setting of tlb_flush_batched
	 * before the PTE is cleared.
	 */
	barrier();
	mm->tlb_flush_batched = true;

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
	bool should_defer = false;

	if (!(flags & TTU_BATCH_FLUSH))
		return false;

	/* If remote CPUs need to be flushed then defer batch the flush */
	if (cpumask_any_but(mm_cpumask(mm), get_cpu()) < nr_cpu_ids)
		should_defer = true;
	put_cpu();

	return should_defer;
}

/*
 * Reclaim unmaps pages under the PTL but do yest flush the TLB prior to
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
	if (mm->tlb_flush_batched) {
		flush_tlb_mm(mm);

		/*
		 * Do yest allow the compiler to re-order the clearing of
		 * tlb_flush_batched before the tlb is flushed.
		 */
		barrier();
		mm->tlb_flush_batched = false;
	}
}
#else
static void set_tlb_ubc_flush_pending(struct mm_struct *mm, bool writable)
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
	unsigned long address;
	if (PageAyesn(page)) {
		struct ayesn_vma *page__ayesn_vma = page_ayesn_vma(page);
		/*
		 * Note: swapoff's unuse_vma() is more efficient with this
		 * check, and needs it to match ayesn_vma when KSM is active.
		 */
		if (!vma->ayesn_vma || !page__ayesn_vma ||
		    vma->ayesn_vma->root != page__ayesn_vma->root)
			return -EFAULT;
	} else if (page->mapping) {
		if (!vma->vm_file || vma->vm_file->f_mapping != page->mapping)
			return -EFAULT;
	} else
		return -EFAULT;
	address = __vma_address(page, vma);
	if (unlikely(address < vma->vm_start || address >= vma->vm_end))
		return -EFAULT;
	return address;
}

pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd = NULL;
	pmd_t pmde;

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
	/*
	 * Some THP functions use the sequence pmdp_huge_clear_flush(), set_pmd_at()
	 * without holding ayesn_vma lock for write.  So when looking for a
	 * genuine pmde (in which to find pte), test present and !THP together.
	 */
	pmde = *pmd;
	barrier();
	if (!pmd_present(pmde) || pmd_trans_huge(pmde))
		pmd = NULL;
out:
	return pmd;
}

struct page_referenced_arg {
	int mapcount;
	int referenced;
	unsigned long vm_flags;
	struct mem_cgroup *memcg;
};
/*
 * arg: page_referenced_arg will be passed
 */
static bool page_referenced_one(struct page *page, struct vm_area_struct *vma,
			unsigned long address, void *arg)
{
	struct page_referenced_arg *pra = arg;
	struct page_vma_mapped_walk pvmw = {
		.page = page,
		.vma = vma,
		.address = address,
	};
	int referenced = 0;

	while (page_vma_mapped_walk(&pvmw)) {
		address = pvmw.address;

		if (vma->vm_flags & VM_LOCKED) {
			page_vma_mapped_walk_done(&pvmw);
			pra->vm_flags |= VM_LOCKED;
			return false; /* To break the loop */
		}

		if (pvmw.pte) {
			if (ptep_clear_flush_young_yestify(vma, address,
						pvmw.pte)) {
				/*
				 * Don't treat a reference through
				 * a sequentially read mapping as such.
				 * If the page has been used in ayesther mapping,
				 * we will catch it; if this other mapping is
				 * already gone, the unmap path will have set
				 * PG_referenced or activated the page.
				 */
				if (likely(!(vma->vm_flags & VM_SEQ_READ)))
					referenced++;
			}
		} else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
			if (pmdp_clear_flush_young_yestify(vma, address,
						pvmw.pmd))
				referenced++;
		} else {
			/* unexpected pmd-mapped page? */
			WARN_ON_ONCE(1);
		}

		pra->mapcount--;
	}

	if (referenced)
		clear_page_idle(page);
	if (test_and_clear_page_young(page))
		referenced++;

	if (referenced) {
		pra->referenced++;
		pra->vm_flags |= vma->vm_flags;
	}

	if (!pra->mapcount)
		return false; /* To break the loop */

	return true;
}

static bool invalid_page_referenced_vma(struct vm_area_struct *vma, void *arg)
{
	struct page_referenced_arg *pra = arg;
	struct mem_cgroup *memcg = pra->memcg;

	if (!mm_match_cgroup(vma->vm_mm, memcg))
		return true;

	return false;
}

/**
 * page_referenced - test if the page was referenced
 * @page: the page to test
 * @is_locked: caller holds lock on the page
 * @memcg: target memory cgroup
 * @vm_flags: collect encountered vma->vm_flags who actually referenced the page
 *
 * Quick test_and_clear_referenced for all mappings to a page,
 * returns the number of ptes which referenced the page.
 */
int page_referenced(struct page *page,
		    int is_locked,
		    struct mem_cgroup *memcg,
		    unsigned long *vm_flags)
{
	int we_locked = 0;
	struct page_referenced_arg pra = {
		.mapcount = total_mapcount(page),
		.memcg = memcg,
	};
	struct rmap_walk_control rwc = {
		.rmap_one = page_referenced_one,
		.arg = (void *)&pra,
		.ayesn_lock = page_lock_ayesn_vma_read,
	};

	*vm_flags = 0;
	if (!pra.mapcount)
		return 0;

	if (!page_rmapping(page))
		return 0;

	if (!is_locked && (!PageAyesn(page) || PageKsm(page))) {
		we_locked = trylock_page(page);
		if (!we_locked)
			return 1;
	}

	/*
	 * If we are reclaiming on behalf of a cgroup, skip
	 * counting on behalf of references from different
	 * cgroups
	 */
	if (memcg) {
		rwc.invalid_vma = invalid_page_referenced_vma;
	}

	rmap_walk(page, &rwc);
	*vm_flags = pra.vm_flags;

	if (we_locked)
		unlock_page(page);

	return pra.referenced;
}

static bool page_mkclean_one(struct page *page, struct vm_area_struct *vma,
			    unsigned long address, void *arg)
{
	struct page_vma_mapped_walk pvmw = {
		.page = page,
		.vma = vma,
		.address = address,
		.flags = PVMW_SYNC,
	};
	struct mmu_yestifier_range range;
	int *cleaned = arg;

	/*
	 * We have to assume the worse case ie pmd for invalidation. Note that
	 * the page can yest be free from this function.
	 */
	mmu_yestifier_range_init(&range, MMU_NOTIFY_PROTECTION_PAGE,
				0, vma, vma->vm_mm, address,
				min(vma->vm_end, address + page_size(page)));
	mmu_yestifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(&pvmw)) {
		int ret = 0;

		address = pvmw.address;
		if (pvmw.pte) {
			pte_t entry;
			pte_t *pte = pvmw.pte;

			if (!pte_dirty(*pte) && !pte_write(*pte))
				continue;

			flush_cache_page(vma, address, pte_pfn(*pte));
			entry = ptep_clear_flush(vma, address, pte);
			entry = pte_wrprotect(entry);
			entry = pte_mkclean(entry);
			set_pte_at(vma->vm_mm, address, pte, entry);
			ret = 1;
		} else {
#ifdef CONFIG_TRANSPARENT_HUGE_PAGECACHE
			pmd_t *pmd = pvmw.pmd;
			pmd_t entry;

			if (!pmd_dirty(*pmd) && !pmd_write(*pmd))
				continue;

			flush_cache_page(vma, address, page_to_pfn(page));
			entry = pmdp_invalidate(vma, address, pmd);
			entry = pmd_wrprotect(entry);
			entry = pmd_mkclean(entry);
			set_pmd_at(vma->vm_mm, address, pmd, entry);
			ret = 1;
#else
			/* unexpected pmd-mapped page? */
			WARN_ON_ONCE(1);
#endif
		}

		/*
		 * No need to call mmu_yestifier_invalidate_range() as we are
		 * downgrading page table protection yest changing it to point
		 * to a new page.
		 *
		 * See Documentation/vm/mmu_yestifier.rst
		 */
		if (ret)
			(*cleaned)++;
	}

	mmu_yestifier_invalidate_range_end(&range);

	return true;
}

static bool invalid_mkclean_vma(struct vm_area_struct *vma, void *arg)
{
	if (vma->vm_flags & VM_SHARED)
		return false;

	return true;
}

int page_mkclean(struct page *page)
{
	int cleaned = 0;
	struct address_space *mapping;
	struct rmap_walk_control rwc = {
		.arg = (void *)&cleaned,
		.rmap_one = page_mkclean_one,
		.invalid_vma = invalid_mkclean_vma,
	};

	BUG_ON(!PageLocked(page));

	if (!page_mapped(page))
		return 0;

	mapping = page_mapping(page);
	if (!mapping)
		return 0;

	rmap_walk(page, &rwc);

	return cleaned;
}
EXPORT_SYMBOL_GPL(page_mkclean);

/**
 * page_move_ayesn_rmap - move a page to our ayesn_vma
 * @page:	the page to move to our ayesn_vma
 * @vma:	the vma the page belongs to
 *
 * When a page belongs exclusively to one process after a COW event,
 * that page can be moved into the ayesn_vma that belongs to just that
 * process, so the rmap code will yest search the parent or sibling
 * processes.
 */
void page_move_ayesn_rmap(struct page *page, struct vm_area_struct *vma)
{
	struct ayesn_vma *ayesn_vma = vma->ayesn_vma;

	page = compound_head(page);

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_VMA(!ayesn_vma, vma);

	ayesn_vma = (void *) ayesn_vma + PAGE_MAPPING_ANON;
	/*
	 * Ensure that ayesn_vma and the PAGE_MAPPING_ANON bit are written
	 * simultaneously, so a concurrent reader (eg page_referenced()'s
	 * PageAyesn()) will yest see one without the other.
	 */
	WRITE_ONCE(page->mapping, (struct address_space *) ayesn_vma);
}

/**
 * __page_set_ayesn_rmap - set up new ayesnymous rmap
 * @page:	Page or Hugepage to add to rmap
 * @vma:	VM area to add page to.
 * @address:	User virtual address of the mapping	
 * @exclusive:	the page is exclusively owned by the current process
 */
static void __page_set_ayesn_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address, int exclusive)
{
	struct ayesn_vma *ayesn_vma = vma->ayesn_vma;

	BUG_ON(!ayesn_vma);

	if (PageAyesn(page))
		return;

	/*
	 * If the page isn't exclusively mapped into this vma,
	 * we must use the _oldest_ possible ayesn_vma for the
	 * page mapping!
	 */
	if (!exclusive)
		ayesn_vma = ayesn_vma->root;

	ayesn_vma = (void *) ayesn_vma + PAGE_MAPPING_ANON;
	page->mapping = (struct address_space *) ayesn_vma;
	page->index = linear_page_index(vma, address);
}

/**
 * __page_check_ayesn_rmap - sanity check ayesnymous rmap addition
 * @page:	the page to add the mapping to
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 */
static void __page_check_ayesn_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address)
{
	/*
	 * The page's ayesn-rmap details (mapping and index) are guaranteed to
	 * be set up correctly at this point.
	 *
	 * We have exclusion against page_add_ayesn_rmap because the caller
	 * always holds the page locked, except if called from page_dup_rmap,
	 * in which case the page is already kyeswn to be setup.
	 *
	 * We have exclusion against page_add_new_ayesn_rmap because those pages
	 * are initially only visible via the pagetables, and the pte is locked
	 * over the call to page_add_new_ayesn_rmap.
	 */
	VM_BUG_ON_PAGE(page_ayesn_vma(page)->root != vma->ayesn_vma->root, page);
	VM_BUG_ON_PAGE(page_to_pgoff(page) != linear_page_index(vma, address),
		       page);
}

/**
 * page_add_ayesn_rmap - add pte mapping to an ayesnymous page
 * @page:	the page to add the mapping to
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 * @compound:	charge the page as compound or small page
 *
 * The caller needs to hold the pte lock, and the page must be locked in
 * the ayesn_vma case: to serialize mapping,index checking after setting,
 * and to ensure that PageAyesn is yest being upgraded racily to PageKsm
 * (but PageKsm is never downgraded to PageAyesn).
 */
void page_add_ayesn_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address, bool compound)
{
	do_page_add_ayesn_rmap(page, vma, address, compound ? RMAP_COMPOUND : 0);
}

/*
 * Special version of the above for do_swap_page, which often runs
 * into pages that are exclusively owned by the current process.
 * Everybody else should continue to use page_add_ayesn_rmap above.
 */
void do_page_add_ayesn_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address, int flags)
{
	bool compound = flags & RMAP_COMPOUND;
	bool first;

	if (compound) {
		atomic_t *mapcount;
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
		mapcount = compound_mapcount_ptr(page);
		first = atomic_inc_and_test(mapcount);
	} else {
		first = atomic_inc_and_test(&page->_mapcount);
	}

	if (first) {
		int nr = compound ? hpage_nr_pages(page) : 1;
		/*
		 * We use the irq-unsafe __{inc|mod}_zone_page_stat because
		 * these counters are yest modified in interrupt context, and
		 * pte lock(a spinlock) is held, which implies preemption
		 * disabled.
		 */
		if (compound)
			__inc_yesde_page_state(page, NR_ANON_THPS);
		__mod_yesde_page_state(page_pgdat(page), NR_ANON_MAPPED, nr);
	}
	if (unlikely(PageKsm(page)))
		return;

	VM_BUG_ON_PAGE(!PageLocked(page), page);

	/* address might be in next vma when migration races vma_adjust */
	if (first)
		__page_set_ayesn_rmap(page, vma, address,
				flags & RMAP_EXCLUSIVE);
	else
		__page_check_ayesn_rmap(page, vma, address);
}

/**
 * page_add_new_ayesn_rmap - add pte mapping to a new ayesnymous page
 * @page:	the page to add the mapping to
 * @vma:	the vm area in which the mapping is added
 * @address:	the user virtual address mapped
 * @compound:	charge the page as compound or small page
 *
 * Same as page_add_ayesn_rmap but must only be called on *new* pages.
 * This means the inc-and-test can be bypassed.
 * Page does yest have to be locked.
 */
void page_add_new_ayesn_rmap(struct page *page,
	struct vm_area_struct *vma, unsigned long address, bool compound)
{
	int nr = compound ? hpage_nr_pages(page) : 1;

	VM_BUG_ON_VMA(address < vma->vm_start || address >= vma->vm_end, vma);
	__SetPageSwapBacked(page);
	if (compound) {
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
		/* increment count (starts at -1) */
		atomic_set(compound_mapcount_ptr(page), 0);
		__inc_yesde_page_state(page, NR_ANON_THPS);
	} else {
		/* Ayesn THP always mapped first with PMD */
		VM_BUG_ON_PAGE(PageTransCompound(page), page);
		/* increment count (starts at -1) */
		atomic_set(&page->_mapcount, 0);
	}
	__mod_yesde_page_state(page_pgdat(page), NR_ANON_MAPPED, nr);
	__page_set_ayesn_rmap(page, vma, address, 1);
}

/**
 * page_add_file_rmap - add pte mapping to a file page
 * @page: the page to add the mapping to
 * @compound: charge the page as compound or small page
 *
 * The caller needs to hold the pte lock.
 */
void page_add_file_rmap(struct page *page, bool compound)
{
	int i, nr = 1;

	VM_BUG_ON_PAGE(compound && !PageTransHuge(page), page);
	lock_page_memcg(page);
	if (compound && PageTransHuge(page)) {
		for (i = 0, nr = 0; i < HPAGE_PMD_NR; i++) {
			if (atomic_inc_and_test(&page[i]._mapcount))
				nr++;
		}
		if (!atomic_inc_and_test(compound_mapcount_ptr(page)))
			goto out;
		if (PageSwapBacked(page))
			__inc_yesde_page_state(page, NR_SHMEM_PMDMAPPED);
		else
			__inc_yesde_page_state(page, NR_FILE_PMDMAPPED);
	} else {
		if (PageTransCompound(page) && page_mapping(page)) {
			VM_WARN_ON_ONCE(!PageLocked(page));

			SetPageDoubleMap(compound_head(page));
			if (PageMlocked(page))
				clear_page_mlock(compound_head(page));
		}
		if (!atomic_inc_and_test(&page->_mapcount))
			goto out;
	}
	__mod_lruvec_page_state(page, NR_FILE_MAPPED, nr);
out:
	unlock_page_memcg(page);
}

static void page_remove_file_rmap(struct page *page, bool compound)
{
	int i, nr = 1;

	VM_BUG_ON_PAGE(compound && !PageHead(page), page);
	lock_page_memcg(page);

	/* Hugepages are yest counted in NR_FILE_MAPPED for yesw. */
	if (unlikely(PageHuge(page))) {
		/* hugetlb pages are always mapped with pmds */
		atomic_dec(compound_mapcount_ptr(page));
		goto out;
	}

	/* page still mapped by someone else? */
	if (compound && PageTransHuge(page)) {
		for (i = 0, nr = 0; i < HPAGE_PMD_NR; i++) {
			if (atomic_add_negative(-1, &page[i]._mapcount))
				nr++;
		}
		if (!atomic_add_negative(-1, compound_mapcount_ptr(page)))
			goto out;
		if (PageSwapBacked(page))
			__dec_yesde_page_state(page, NR_SHMEM_PMDMAPPED);
		else
			__dec_yesde_page_state(page, NR_FILE_PMDMAPPED);
	} else {
		if (!atomic_add_negative(-1, &page->_mapcount))
			goto out;
	}

	/*
	 * We use the irq-unsafe __{inc|mod}_lruvec_page_state because
	 * these counters are yest modified in interrupt context, and
	 * pte lock(a spinlock) is held, which implies preemption disabled.
	 */
	__mod_lruvec_page_state(page, NR_FILE_MAPPED, -nr);

	if (unlikely(PageMlocked(page)))
		clear_page_mlock(page);
out:
	unlock_page_memcg(page);
}

static void page_remove_ayesn_compound_rmap(struct page *page)
{
	int i, nr;

	if (!atomic_add_negative(-1, compound_mapcount_ptr(page)))
		return;

	/* Hugepages are yest counted in NR_ANON_PAGES for yesw. */
	if (unlikely(PageHuge(page)))
		return;

	if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		return;

	__dec_yesde_page_state(page, NR_ANON_THPS);

	if (TestClearPageDoubleMap(page)) {
		/*
		 * Subpages can be mapped with PTEs too. Check how many of
		 * them are still mapped.
		 */
		for (i = 0, nr = 0; i < HPAGE_PMD_NR; i++) {
			if (atomic_add_negative(-1, &page[i]._mapcount))
				nr++;
		}

		/*
		 * Queue the page for deferred split if at least one small
		 * page of the compound page is unmapped, but at least one
		 * small page is still mapped.
		 */
		if (nr && nr < HPAGE_PMD_NR)
			deferred_split_huge_page(page);
	} else {
		nr = HPAGE_PMD_NR;
	}

	if (unlikely(PageMlocked(page)))
		clear_page_mlock(page);

	if (nr)
		__mod_yesde_page_state(page_pgdat(page), NR_ANON_MAPPED, -nr);
}

/**
 * page_remove_rmap - take down pte mapping from a page
 * @page:	page to remove mapping from
 * @compound:	uncharge the page as compound or small page
 *
 * The caller needs to hold the pte lock.
 */
void page_remove_rmap(struct page *page, bool compound)
{
	if (!PageAyesn(page))
		return page_remove_file_rmap(page, compound);

	if (compound)
		return page_remove_ayesn_compound_rmap(page);

	/* page still mapped by someone else? */
	if (!atomic_add_negative(-1, &page->_mapcount))
		return;

	/*
	 * We use the irq-unsafe __{inc|mod}_zone_page_stat because
	 * these counters are yest modified in interrupt context, and
	 * pte lock(a spinlock) is held, which implies preemption disabled.
	 */
	__dec_yesde_page_state(page, NR_ANON_MAPPED);

	if (unlikely(PageMlocked(page)))
		clear_page_mlock(page);

	if (PageTransCompound(page))
		deferred_split_huge_page(compound_head(page));

	/*
	 * It would be tidy to reset the PageAyesn mapping here,
	 * but that might overwrite a racing page_add_ayesn_rmap
	 * which increments mapcount after us but sets mapping
	 * before us: so leave the reset to free_unref_page,
	 * and remember that it's only reliable while mapped.
	 * Leaving it set also helps swapoff to reinstate ptes
	 * faster for those pages still in swapcache.
	 */
}

/*
 * @arg: enum ttu_flags will be passed to this argument
 */
static bool try_to_unmap_one(struct page *page, struct vm_area_struct *vma,
		     unsigned long address, void *arg)
{
	struct mm_struct *mm = vma->vm_mm;
	struct page_vma_mapped_walk pvmw = {
		.page = page,
		.vma = vma,
		.address = address,
	};
	pte_t pteval;
	struct page *subpage;
	bool ret = true;
	struct mmu_yestifier_range range;
	enum ttu_flags flags = (enum ttu_flags)arg;

	/* munlock has yesthing to gain from examining un-locked vmas */
	if ((flags & TTU_MUNLOCK) && !(vma->vm_flags & VM_LOCKED))
		return true;

	if (IS_ENABLED(CONFIG_MIGRATION) && (flags & TTU_MIGRATION) &&
	    is_zone_device_page(page) && !is_device_private_page(page))
		return true;

	if (flags & TTU_SPLIT_HUGE_PMD) {
		split_huge_pmd_address(vma, address,
				flags & TTU_SPLIT_FREEZE, page);
	}

	/*
	 * For THP, we have to assume the worse case ie pmd for invalidation.
	 * For hugetlb, it could be much worse if we need to do pud
	 * invalidation in the case of pmd sharing.
	 *
	 * Note that the page can yest be free in this function as call of
	 * try_to_unmap() must hold a reference on the page.
	 */
	mmu_yestifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, vma, vma->vm_mm,
				address,
				min(vma->vm_end, address + page_size(page)));
	if (PageHuge(page)) {
		/*
		 * If sharing is possible, start and end will be adjusted
		 * accordingly.
		 */
		adjust_range_if_pmd_sharing_possible(vma, &range.start,
						     &range.end);
	}
	mmu_yestifier_invalidate_range_start(&range);

	while (page_vma_mapped_walk(&pvmw)) {
#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
		/* PMD-mapped THP migration entry */
		if (!pvmw.pte && (flags & TTU_MIGRATION)) {
			VM_BUG_ON_PAGE(PageHuge(page) || !PageTransCompound(page), page);

			set_pmd_migration_entry(&pvmw, page);
			continue;
		}
#endif

		/*
		 * If the page is mlock()d, we canyest swap it out.
		 * If it's recently referenced (perhaps page_referenced
		 * skipped over this mm) then we should reactivate it.
		 */
		if (!(flags & TTU_IGNORE_MLOCK)) {
			if (vma->vm_flags & VM_LOCKED) {
				/* PTE-mapped THP are never mlocked */
				if (!PageTransCompound(page)) {
					/*
					 * Holding pte lock, we do *yest* need
					 * mmap_sem here
					 */
					mlock_vma_page(page);
				}
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
			if (flags & TTU_MUNLOCK)
				continue;
		}

		/* Unexpected PMD-mapped THP? */
		VM_BUG_ON_PAGE(!pvmw.pte, page);

		subpage = page - page_to_pfn(page) + pte_pfn(*pvmw.pte);
		address = pvmw.address;

		if (PageHuge(page)) {
			if (huge_pmd_unshare(mm, &address, pvmw.pte)) {
				/*
				 * huge_pmd_unshare unmapped an entire PMD
				 * page.  There is yes way of kyeswing exactly
				 * which PMDs may be cached for this mm, so
				 * we must flush them all.  start/end were
				 * already adjusted above to cover this range.
				 */
				flush_cache_range(vma, range.start, range.end);
				flush_tlb_range(vma, range.start, range.end);
				mmu_yestifier_invalidate_range(mm, range.start,
							      range.end);

				/*
				 * The ref count of the PMD page was dropped
				 * which is part of the way map counting
				 * is done for shared PMDs.  Return 'true'
				 * here.  When there is yes other sharing,
				 * huge_pmd_unshare returns false and we will
				 * unmap the actual page and drop map count
				 * to zero.
				 */
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
		}

		if (IS_ENABLED(CONFIG_MIGRATION) &&
		    (flags & TTU_MIGRATION) &&
		    is_zone_device_page(page)) {
			swp_entry_t entry;
			pte_t swp_pte;

			pteval = ptep_get_and_clear(mm, pvmw.address, pvmw.pte);

			/*
			 * Store the pfn of the page in a special migration
			 * pte. do_swap_page() will wait until the migration
			 * pte is removed and then restart fault handling.
			 */
			entry = make_migration_entry(page, 0);
			swp_pte = swp_entry_to_pte(entry);
			if (pte_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			set_pte_at(mm, pvmw.address, pvmw.pte, swp_pte);
			/*
			 * No need to invalidate here it will synchronize on
			 * against the special swap migration pte.
			 *
			 * The assignment to subpage above was computed from a
			 * swap PTE which results in an invalid pointer.
			 * Since only PAGE_SIZE pages can currently be
			 * migrated, just set it to page. This will need to be
			 * changed when hugepage migrations to device private
			 * memory are supported.
			 */
			subpage = page;
			goto discard;
		}

		if (!(flags & TTU_IGNORE_ACCESS)) {
			if (ptep_clear_flush_young_yestify(vma, address,
						pvmw.pte)) {
				ret = false;
				page_vma_mapped_walk_done(&pvmw);
				break;
			}
		}

		/* Nuke the page table entry. */
		flush_cache_page(vma, address, pte_pfn(*pvmw.pte));
		if (should_defer_flush(mm, flags)) {
			/*
			 * We clear the PTE but do yest flush so potentially
			 * a remote CPU could still be writing to the page.
			 * If the entry was previously clean then the
			 * architecture must guarantee that a clear->dirty
			 * transition on a cached TLB entry is written through
			 * and traps if the PTE is unmapped.
			 */
			pteval = ptep_get_and_clear(mm, address, pvmw.pte);

			set_tlb_ubc_flush_pending(mm, pte_dirty(pteval));
		} else {
			pteval = ptep_clear_flush(vma, address, pvmw.pte);
		}

		/* Move the dirty bit to the page. Now the pte is gone. */
		if (pte_dirty(pteval))
			set_page_dirty(page);

		/* Update high watermark before we lower rss */
		update_hiwater_rss(mm);

		if (PageHWPoison(page) && !(flags & TTU_IGNORE_HWPOISON)) {
			pteval = swp_entry_to_pte(make_hwpoison_entry(subpage));
			if (PageHuge(page)) {
				hugetlb_count_sub(compound_nr(page), mm);
				set_huge_swap_pte_at(mm, address,
						     pvmw.pte, pteval,
						     vma_mmu_pagesize(vma));
			} else {
				dec_mm_counter(mm, mm_counter(page));
				set_pte_at(mm, address, pvmw.pte, pteval);
			}

		} else if (pte_unused(pteval) && !userfaultfd_armed(vma)) {
			/*
			 * The guest indicated that the page content is of yes
			 * interest anymore. Simply discard the pte, vmscan
			 * will take care of the rest.
			 * A future reference will then fault in a new zero
			 * page. When userfaultfd is active, we must yest drop
			 * this page though, as its main user (postcopy
			 * migration) will yest expect userfaults on already
			 * copied pages.
			 */
			dec_mm_counter(mm, mm_counter(page));
			/* We have to invalidate as we cleared the pte */
			mmu_yestifier_invalidate_range(mm, address,
						      address + PAGE_SIZE);
		} else if (IS_ENABLED(CONFIG_MIGRATION) &&
				(flags & (TTU_MIGRATION|TTU_SPLIT_FREEZE))) {
			swp_entry_t entry;
			pte_t swp_pte;

			if (arch_unmap_one(mm, vma, address, pteval) < 0) {
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
			entry = make_migration_entry(subpage,
					pte_write(pteval));
			swp_pte = swp_entry_to_pte(entry);
			if (pte_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			set_pte_at(mm, address, pvmw.pte, swp_pte);
			/*
			 * No need to invalidate here it will synchronize on
			 * against the special swap migration pte.
			 */
		} else if (PageAyesn(page)) {
			swp_entry_t entry = { .val = page_private(subpage) };
			pte_t swp_pte;
			/*
			 * Store the swap location in the pte.
			 * See handle_pte_fault() ...
			 */
			if (unlikely(PageSwapBacked(page) != PageSwapCache(page))) {
				WARN_ON_ONCE(1);
				ret = false;
				/* We have to invalidate as we cleared the pte */
				mmu_yestifier_invalidate_range(mm, address,
							address + PAGE_SIZE);
				page_vma_mapped_walk_done(&pvmw);
				break;
			}

			/* MADV_FREE page check */
			if (!PageSwapBacked(page)) {
				if (!PageDirty(page)) {
					/* Invalidate as we cleared the pte */
					mmu_yestifier_invalidate_range(mm,
						address, address + PAGE_SIZE);
					dec_mm_counter(mm, MM_ANONPAGES);
					goto discard;
				}

				/*
				 * If the page was redirtied, it canyest be
				 * discarded. Remap the page to page table.
				 */
				set_pte_at(mm, address, pvmw.pte, pteval);
				SetPageSwapBacked(page);
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
			dec_mm_counter(mm, MM_ANONPAGES);
			inc_mm_counter(mm, MM_SWAPENTS);
			swp_pte = swp_entry_to_pte(entry);
			if (pte_soft_dirty(pteval))
				swp_pte = pte_swp_mksoft_dirty(swp_pte);
			set_pte_at(mm, address, pvmw.pte, swp_pte);
			/* Invalidate as we cleared the pte */
			mmu_yestifier_invalidate_range(mm, address,
						      address + PAGE_SIZE);
		} else {
			/*
			 * This is a locked file-backed page, thus it canyest
			 * be removed from the page cache and replaced by a new
			 * page before mmu_yestifier_invalidate_range_end, so yes
			 * concurrent thread might update its page table to
			 * point at new page while a device still is using this
			 * page.
			 *
			 * See Documentation/vm/mmu_yestifier.rst
			 */
			dec_mm_counter(mm, mm_counter_file(page));
		}
discard:
		/*
		 * No need to call mmu_yestifier_invalidate_range() it has be
		 * done above for all cases requiring it to happen under page
		 * table lock before mmu_yestifier_invalidate_range_end()
		 *
		 * See Documentation/vm/mmu_yestifier.rst
		 */
		page_remove_rmap(subpage, PageHuge(page));
		put_page(page);
	}

	mmu_yestifier_invalidate_range_end(&range);

	return ret;
}

bool is_vma_temporary_stack(struct vm_area_struct *vma)
{
	int maybe_stack = vma->vm_flags & (VM_GROWSDOWN | VM_GROWSUP);

	if (!maybe_stack)
		return false;

	if ((vma->vm_flags & VM_STACK_INCOMPLETE_SETUP) ==
						VM_STACK_INCOMPLETE_SETUP)
		return true;

	return false;
}

static bool invalid_migration_vma(struct vm_area_struct *vma, void *arg)
{
	return is_vma_temporary_stack(vma);
}

static int page_mapcount_is_zero(struct page *page)
{
	return !total_mapcount(page);
}

/**
 * try_to_unmap - try to remove all page table mappings to a page
 * @page: the page to get unmapped
 * @flags: action and flags
 *
 * Tries to remove all the page table entries which are mapping this
 * page, used in the pageout path.  Caller must hold the page lock.
 *
 * If unmap is successful, return true. Otherwise, false.
 */
bool try_to_unmap(struct page *page, enum ttu_flags flags)
{
	struct rmap_walk_control rwc = {
		.rmap_one = try_to_unmap_one,
		.arg = (void *)flags,
		.done = page_mapcount_is_zero,
		.ayesn_lock = page_lock_ayesn_vma_read,
	};

	/*
	 * During exec, a temporary VMA is setup and later moved.
	 * The VMA is moved under the ayesn_vma lock but yest the
	 * page tables leading to a race where migration canyest
	 * find the migration ptes. Rather than increasing the
	 * locking requirements of exec(), migration skips
	 * temporary VMAs until after exec() completes.
	 */
	if ((flags & (TTU_MIGRATION|TTU_SPLIT_FREEZE))
	    && !PageKsm(page) && PageAyesn(page))
		rwc.invalid_vma = invalid_migration_vma;

	if (flags & TTU_RMAP_LOCKED)
		rmap_walk_locked(page, &rwc);
	else
		rmap_walk(page, &rwc);

	return !page_mapcount(page) ? true : false;
}

static int page_yest_mapped(struct page *page)
{
	return !page_mapped(page);
};

/**
 * try_to_munlock - try to munlock a page
 * @page: the page to be munlocked
 *
 * Called from munlock code.  Checks all of the VMAs mapping the page
 * to make sure yesbody else has this page mlocked. The page will be
 * returned with PG_mlocked cleared if yes other vmas have it mlocked.
 */

void try_to_munlock(struct page *page)
{
	struct rmap_walk_control rwc = {
		.rmap_one = try_to_unmap_one,
		.arg = (void *)TTU_MUNLOCK,
		.done = page_yest_mapped,
		.ayesn_lock = page_lock_ayesn_vma_read,

	};

	VM_BUG_ON_PAGE(!PageLocked(page) || PageLRU(page), page);
	VM_BUG_ON_PAGE(PageCompound(page) && PageDoubleMap(page), page);

	rmap_walk(page, &rwc);
}

void __put_ayesn_vma(struct ayesn_vma *ayesn_vma)
{
	struct ayesn_vma *root = ayesn_vma->root;

	ayesn_vma_free(ayesn_vma);
	if (root != ayesn_vma && atomic_dec_and_test(&root->refcount))
		ayesn_vma_free(root);
}

static struct ayesn_vma *rmap_walk_ayesn_lock(struct page *page,
					struct rmap_walk_control *rwc)
{
	struct ayesn_vma *ayesn_vma;

	if (rwc->ayesn_lock)
		return rwc->ayesn_lock(page);

	/*
	 * Note: remove_migration_ptes() canyest use page_lock_ayesn_vma_read()
	 * because that depends on page_mapped(); but yest all its usages
	 * are holding mmap_sem. Users without mmap_sem are required to
	 * take a reference count to prevent the ayesn_vma disappearing
	 */
	ayesn_vma = page_ayesn_vma(page);
	if (!ayesn_vma)
		return NULL;

	ayesn_vma_lock_read(ayesn_vma);
	return ayesn_vma;
}

/*
 * rmap_walk_ayesn - do something to ayesnymous page using the object-based
 * rmap method
 * @page: the page to be handled
 * @rwc: control variable according to each walk type
 *
 * Find all the mappings of a page using the mapping pointer and the vma chains
 * contained in the ayesn_vma struct it points to.
 *
 * When called from try_to_munlock(), the mmap_sem of the mm containing the vma
 * where the page was found will be held for write.  So, we won't recheck
 * vm_flags for that VMA.  That should be OK, because that vma shouldn't be
 * LOCKED.
 */
static void rmap_walk_ayesn(struct page *page, struct rmap_walk_control *rwc,
		bool locked)
{
	struct ayesn_vma *ayesn_vma;
	pgoff_t pgoff_start, pgoff_end;
	struct ayesn_vma_chain *avc;

	if (locked) {
		ayesn_vma = page_ayesn_vma(page);
		/* ayesn_vma disappear under us? */
		VM_BUG_ON_PAGE(!ayesn_vma, page);
	} else {
		ayesn_vma = rmap_walk_ayesn_lock(page, rwc);
	}
	if (!ayesn_vma)
		return;

	pgoff_start = page_to_pgoff(page);
	pgoff_end = pgoff_start + hpage_nr_pages(page) - 1;
	ayesn_vma_interval_tree_foreach(avc, &ayesn_vma->rb_root,
			pgoff_start, pgoff_end) {
		struct vm_area_struct *vma = avc->vma;
		unsigned long address = vma_address(page, vma);

		cond_resched();

		if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
			continue;

		if (!rwc->rmap_one(page, vma, address, rwc->arg))
			break;
		if (rwc->done && rwc->done(page))
			break;
	}

	if (!locked)
		ayesn_vma_unlock_read(ayesn_vma);
}

/*
 * rmap_walk_file - do something to file page using the object-based rmap method
 * @page: the page to be handled
 * @rwc: control variable according to each walk type
 *
 * Find all the mappings of a page using the mapping pointer and the vma chains
 * contained in the address_space struct it points to.
 *
 * When called from try_to_munlock(), the mmap_sem of the mm containing the vma
 * where the page was found will be held for write.  So, we won't recheck
 * vm_flags for that VMA.  That should be OK, because that vma shouldn't be
 * LOCKED.
 */
static void rmap_walk_file(struct page *page, struct rmap_walk_control *rwc,
		bool locked)
{
	struct address_space *mapping = page_mapping(page);
	pgoff_t pgoff_start, pgoff_end;
	struct vm_area_struct *vma;

	/*
	 * The page lock yest only makes sure that page->mapping canyest
	 * suddenly be NULLified by truncation, it makes sure that the
	 * structure at mapping canyest be freed and reused yet,
	 * so we can safely take mapping->i_mmap_rwsem.
	 */
	VM_BUG_ON_PAGE(!PageLocked(page), page);

	if (!mapping)
		return;

	pgoff_start = page_to_pgoff(page);
	pgoff_end = pgoff_start + hpage_nr_pages(page) - 1;
	if (!locked)
		i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap,
			pgoff_start, pgoff_end) {
		unsigned long address = vma_address(page, vma);

		cond_resched();

		if (rwc->invalid_vma && rwc->invalid_vma(vma, rwc->arg))
			continue;

		if (!rwc->rmap_one(page, vma, address, rwc->arg))
			goto done;
		if (rwc->done && rwc->done(page))
			goto done;
	}

done:
	if (!locked)
		i_mmap_unlock_read(mapping);
}

void rmap_walk(struct page *page, struct rmap_walk_control *rwc)
{
	if (unlikely(PageKsm(page)))
		rmap_walk_ksm(page, rwc);
	else if (PageAyesn(page))
		rmap_walk_ayesn(page, rwc, false);
	else
		rmap_walk_file(page, rwc, false);
}

/* Like rmap_walk, but caller holds relevant rmap lock */
void rmap_walk_locked(struct page *page, struct rmap_walk_control *rwc)
{
	/* yes ksm support for yesw */
	VM_BUG_ON_PAGE(PageKsm(page), page);
	if (PageAyesn(page))
		rmap_walk_ayesn(page, rwc, true);
	else
		rmap_walk_file(page, rwc, true);
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * The following two functions are for ayesnymous (private mapped) hugepages.
 * Unlike common ayesnymous pages, ayesnymous hugepages have yes accounting code
 * and yes lru code, because we handle hugepages differently from common pages.
 */
void hugepage_add_ayesn_rmap(struct page *page,
			    struct vm_area_struct *vma, unsigned long address)
{
	struct ayesn_vma *ayesn_vma = vma->ayesn_vma;
	int first;

	BUG_ON(!PageLocked(page));
	BUG_ON(!ayesn_vma);
	/* address might be in next vma when migration races vma_adjust */
	first = atomic_inc_and_test(compound_mapcount_ptr(page));
	if (first)
		__page_set_ayesn_rmap(page, vma, address, 0);
}

void hugepage_add_new_ayesn_rmap(struct page *page,
			struct vm_area_struct *vma, unsigned long address)
{
	BUG_ON(address < vma->vm_start || address >= vma->vm_end);
	atomic_set(compound_mapcount_ptr(page), 0);
	__page_set_ayesn_rmap(page, vma, address, 1);
}
#endif /* CONFIG_HUGETLB_PAGE */
