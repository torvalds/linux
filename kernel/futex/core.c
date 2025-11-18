// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Fast Userspace Mutexes (which I call "Futexes!").
 *  (C) Rusty Russell, IBM 2002
 *
 *  Generalized futexes, futex requeueing, misc fixes by Ingo Molnar
 *  (C) Copyright 2003 Red Hat Inc, All Rights Reserved
 *
 *  Removed page pinning, fix privately mapped COW pages and other cleanups
 *  (C) Copyright 2003, 2004 Jamie Lokier
 *
 *  Robust futex support started by Ingo Molnar
 *  (C) Copyright 2006 Red Hat Inc, All Rights Reserved
 *  Thanks to Thomas Gleixner for suggestions, analysis and fixes.
 *
 *  PI-futex support started by Ingo Molnar and Thomas Gleixner
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 *  PRIVATE futexes by Eric Dumazet
 *  Copyright (C) 2007 Eric Dumazet <dada1@cosmosbay.com>
 *
 *  Requeue-PI support by Darren Hart <dvhltc@us.ibm.com>
 *  Copyright (C) IBM Corporation, 2009
 *  Thanks to Thomas Gleixner for conceptual design and careful reviews.
 *
 *  Thanks to Ben LaHaise for yelling "hashed waitqueues" loudly
 *  enough at me, Linus for the original (flawed) idea, Matthew
 *  Kirkwood for proof-of-concept implementation.
 *
 *  "The futexes are also cursed."
 *  "But they come in a choice of three flavours!"
 */
#include <linux/compat.h>
#include <linux/jhash.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/plist.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>
#include <linux/fault-inject.h>
#include <linux/slab.h>
#include <linux/prctl.h>
#include <linux/mempolicy.h>
#include <linux/mmap_lock.h>

#include "futex.h"
#include "../locking/rtmutex_common.h"

/*
 * The base of the bucket array and its size are always used together
 * (after initialization only in futex_hash()), so ensure that they
 * reside in the same cacheline.
 */
static struct {
	unsigned long            hashmask;
	unsigned int		 hashshift;
	struct futex_hash_bucket *queues[MAX_NUMNODES];
} __futex_data __read_mostly __aligned(2*sizeof(long));

#define futex_hashmask	(__futex_data.hashmask)
#define futex_hashshift	(__futex_data.hashshift)
#define futex_queues	(__futex_data.queues)

struct futex_private_hash {
	int		state;
	unsigned int	hash_mask;
	struct rcu_head	rcu;
	void		*mm;
	bool		custom;
	struct futex_hash_bucket queues[];
};

/*
 * Fault injections for futexes.
 */
#ifdef CONFIG_FAIL_FUTEX

static struct {
	struct fault_attr attr;

	bool ignore_private;
} fail_futex = {
	.attr = FAULT_ATTR_INITIALIZER,
	.ignore_private = false,
};

static int __init setup_fail_futex(char *str)
{
	return setup_fault_attr(&fail_futex.attr, str);
}
__setup("fail_futex=", setup_fail_futex);

bool should_fail_futex(bool fshared)
{
	if (fail_futex.ignore_private && !fshared)
		return false;

	return should_fail(&fail_futex.attr, 1);
}

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int __init fail_futex_debugfs(void)
{
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_futex", NULL,
					&fail_futex.attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	debugfs_create_bool("ignore-private", mode, dir,
			    &fail_futex.ignore_private);
	return 0;
}

late_initcall(fail_futex_debugfs);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#endif /* CONFIG_FAIL_FUTEX */

static struct futex_hash_bucket *
__futex_hash(union futex_key *key, struct futex_private_hash *fph);

#ifdef CONFIG_FUTEX_PRIVATE_HASH
static bool futex_ref_get(struct futex_private_hash *fph);
static bool futex_ref_put(struct futex_private_hash *fph);
static bool futex_ref_is_dead(struct futex_private_hash *fph);

enum { FR_PERCPU = 0, FR_ATOMIC };

static inline bool futex_key_is_private(union futex_key *key)
{
	/*
	 * Relies on get_futex_key() to set either bit for shared
	 * futexes -- see comment with union futex_key.
	 */
	return !(key->both.offset & (FUT_OFF_INODE | FUT_OFF_MMSHARED));
}

static bool futex_private_hash_get(struct futex_private_hash *fph)
{
	return futex_ref_get(fph);
}

void futex_private_hash_put(struct futex_private_hash *fph)
{
	if (futex_ref_put(fph))
		wake_up_var(fph->mm);
}

/**
 * futex_hash_get - Get an additional reference for the local hash.
 * @hb:                    ptr to the private local hash.
 *
 * Obtain an additional reference for the already obtained hash bucket. The
 * caller must already own an reference.
 */
void futex_hash_get(struct futex_hash_bucket *hb)
{
	struct futex_private_hash *fph = hb->priv;

	if (!fph)
		return;
	WARN_ON_ONCE(!futex_private_hash_get(fph));
}

void futex_hash_put(struct futex_hash_bucket *hb)
{
	struct futex_private_hash *fph = hb->priv;

	if (!fph)
		return;
	futex_private_hash_put(fph);
}

static struct futex_hash_bucket *
__futex_hash_private(union futex_key *key, struct futex_private_hash *fph)
{
	u32 hash;

	if (!futex_key_is_private(key))
		return NULL;

	if (!fph)
		fph = rcu_dereference(key->private.mm->futex_phash);
	if (!fph || !fph->hash_mask)
		return NULL;

	hash = jhash2((void *)&key->private.address,
		      sizeof(key->private.address) / 4,
		      key->both.offset);
	return &fph->queues[hash & fph->hash_mask];
}

static void futex_rehash_private(struct futex_private_hash *old,
				 struct futex_private_hash *new)
{
	struct futex_hash_bucket *hb_old, *hb_new;
	unsigned int slots = old->hash_mask + 1;
	unsigned int i;

	for (i = 0; i < slots; i++) {
		struct futex_q *this, *tmp;

		hb_old = &old->queues[i];

		spin_lock(&hb_old->lock);
		plist_for_each_entry_safe(this, tmp, &hb_old->chain, list) {

			plist_del(&this->list, &hb_old->chain);
			futex_hb_waiters_dec(hb_old);

			WARN_ON_ONCE(this->lock_ptr != &hb_old->lock);

			hb_new = __futex_hash(&this->key, new);
			futex_hb_waiters_inc(hb_new);
			/*
			 * The new pointer isn't published yet but an already
			 * moved user can be unqueued due to timeout or signal.
			 */
			spin_lock_nested(&hb_new->lock, SINGLE_DEPTH_NESTING);
			plist_add(&this->list, &hb_new->chain);
			this->lock_ptr = &hb_new->lock;
			spin_unlock(&hb_new->lock);
		}
		spin_unlock(&hb_old->lock);
	}
}

static bool __futex_pivot_hash(struct mm_struct *mm,
			       struct futex_private_hash *new)
{
	struct futex_private_hash *fph;

	WARN_ON_ONCE(mm->futex_phash_new);

	fph = rcu_dereference_protected(mm->futex_phash,
					lockdep_is_held(&mm->futex_hash_lock));
	if (fph) {
		if (!futex_ref_is_dead(fph)) {
			mm->futex_phash_new = new;
			return false;
		}

		futex_rehash_private(fph, new);
	}
	new->state = FR_PERCPU;
	scoped_guard(rcu) {
		mm->futex_batches = get_state_synchronize_rcu();
		rcu_assign_pointer(mm->futex_phash, new);
	}
	kvfree_rcu(fph, rcu);
	return true;
}

static void futex_pivot_hash(struct mm_struct *mm)
{
	scoped_guard(mutex, &mm->futex_hash_lock) {
		struct futex_private_hash *fph;

		fph = mm->futex_phash_new;
		if (fph) {
			mm->futex_phash_new = NULL;
			__futex_pivot_hash(mm, fph);
		}
	}
}

struct futex_private_hash *futex_private_hash(void)
{
	struct mm_struct *mm = current->mm;
	/*
	 * Ideally we don't loop. If there is a replacement in progress
	 * then a new private hash is already prepared and a reference can't be
	 * obtained once the last user dropped it's.
	 * In that case we block on mm_struct::futex_hash_lock and either have
	 * to perform the replacement or wait while someone else is doing the
	 * job. Eitherway, on the second iteration we acquire a reference on the
	 * new private hash or loop again because a new replacement has been
	 * requested.
	 */
again:
	scoped_guard(rcu) {
		struct futex_private_hash *fph;

		fph = rcu_dereference(mm->futex_phash);
		if (!fph)
			return NULL;

		if (futex_private_hash_get(fph))
			return fph;
	}
	futex_pivot_hash(mm);
	goto again;
}

struct futex_hash_bucket *futex_hash(union futex_key *key)
{
	struct futex_private_hash *fph;
	struct futex_hash_bucket *hb;

again:
	scoped_guard(rcu) {
		hb = __futex_hash(key, NULL);
		fph = hb->priv;

		if (!fph || futex_private_hash_get(fph))
			return hb;
	}
	futex_pivot_hash(key->private.mm);
	goto again;
}

#else /* !CONFIG_FUTEX_PRIVATE_HASH */

static struct futex_hash_bucket *
__futex_hash_private(union futex_key *key, struct futex_private_hash *fph)
{
	return NULL;
}

struct futex_hash_bucket *futex_hash(union futex_key *key)
{
	return __futex_hash(key, NULL);
}

#endif /* CONFIG_FUTEX_PRIVATE_HASH */

#ifdef CONFIG_FUTEX_MPOL

static int __futex_key_to_node(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma = vma_lookup(mm, addr);
	struct mempolicy *mpol;
	int node = FUTEX_NO_NODE;

	if (!vma)
		return FUTEX_NO_NODE;

	mpol = vma_policy(vma);
	if (!mpol)
		return FUTEX_NO_NODE;

	switch (mpol->mode) {
	case MPOL_PREFERRED:
		node = first_node(mpol->nodes);
		break;
	case MPOL_PREFERRED_MANY:
	case MPOL_BIND:
		if (mpol->home_node != NUMA_NO_NODE)
			node = mpol->home_node;
		break;
	default:
		break;
	}

	return node;
}

static int futex_key_to_node_opt(struct mm_struct *mm, unsigned long addr)
{
	int seq, node;

	guard(rcu)();

	if (!mmap_lock_speculate_try_begin(mm, &seq))
		return -EBUSY;

	node = __futex_key_to_node(mm, addr);

	if (mmap_lock_speculate_retry(mm, seq))
		return -EAGAIN;

	return node;
}

static int futex_mpol(struct mm_struct *mm, unsigned long addr)
{
	int node;

	node = futex_key_to_node_opt(mm, addr);
	if (node >= FUTEX_NO_NODE)
		return node;

	guard(mmap_read_lock)(mm);
	return __futex_key_to_node(mm, addr);
}

#else /* !CONFIG_FUTEX_MPOL */

static int futex_mpol(struct mm_struct *mm, unsigned long addr)
{
	return FUTEX_NO_NODE;
}

#endif /* CONFIG_FUTEX_MPOL */

/**
 * __futex_hash - Return the hash bucket
 * @key:	Pointer to the futex key for which the hash is calculated
 * @fph:	Pointer to private hash if known
 *
 * We hash on the keys returned from get_futex_key (see below) and return the
 * corresponding hash bucket.
 * If the FUTEX is PROCESS_PRIVATE then a per-process hash bucket (from the
 * private hash) is returned if existing. Otherwise a hash bucket from the
 * global hash is returned.
 */
static struct futex_hash_bucket *
__futex_hash(union futex_key *key, struct futex_private_hash *fph)
{
	int node = key->both.node;
	u32 hash;

	if (node == FUTEX_NO_NODE) {
		struct futex_hash_bucket *hb;

		hb = __futex_hash_private(key, fph);
		if (hb)
			return hb;
	}

	hash = jhash2((u32 *)key,
		      offsetof(typeof(*key), both.offset) / sizeof(u32),
		      key->both.offset);

	if (node == FUTEX_NO_NODE) {
		/*
		 * In case of !FLAGS_NUMA, use some unused hash bits to pick a
		 * node -- this ensures regular futexes are interleaved across
		 * the nodes and avoids having to allocate multiple
		 * hash-tables.
		 *
		 * NOTE: this isn't perfectly uniform, but it is fast and
		 * handles sparse node masks.
		 */
		node = (hash >> futex_hashshift) % nr_node_ids;
		if (!node_possible(node)) {
			node = find_next_bit_wrap(node_possible_map.bits,
						  nr_node_ids, node);
		}
	}

	return &futex_queues[node][hash & futex_hashmask];
}

/**
 * futex_setup_timer - set up the sleeping hrtimer.
 * @time:	ptr to the given timeout value
 * @timeout:	the hrtimer_sleeper structure to be set up
 * @flags:	futex flags
 * @range_ns:	optional range in ns
 *
 * Return: Initialized hrtimer_sleeper structure or NULL if no timeout
 *	   value given
 */
struct hrtimer_sleeper *
futex_setup_timer(ktime_t *time, struct hrtimer_sleeper *timeout,
		  int flags, u64 range_ns)
{
	if (!time)
		return NULL;

	hrtimer_setup_sleeper_on_stack(timeout,
				       (flags & FLAGS_CLOCKRT) ? CLOCK_REALTIME : CLOCK_MONOTONIC,
				       HRTIMER_MODE_ABS);
	/*
	 * If range_ns is 0, calling hrtimer_set_expires_range_ns() is
	 * effectively the same as calling hrtimer_set_expires().
	 */
	hrtimer_set_expires_range_ns(&timeout->timer, *time, range_ns);

	return timeout;
}

/*
 * Generate a machine wide unique identifier for this inode.
 *
 * This relies on u64 not wrapping in the life-time of the machine; which with
 * 1ns resolution means almost 585 years.
 *
 * This further relies on the fact that a well formed program will not unmap
 * the file while it has a (shared) futex waiting on it. This mapping will have
 * a file reference which pins the mount and inode.
 *
 * If for some reason an inode gets evicted and read back in again, it will get
 * a new sequence number and will _NOT_ match, even though it is the exact same
 * file.
 *
 * It is important that futex_match() will never have a false-positive, esp.
 * for PI futexes that can mess up the state. The above argues that false-negatives
 * are only possible for malformed programs.
 */
static u64 get_inode_sequence_number(struct inode *inode)
{
	static atomic64_t i_seq;
	u64 old;

	/* Does the inode already have a sequence number? */
	old = atomic64_read(&inode->i_sequence);
	if (likely(old))
		return old;

	for (;;) {
		u64 new = atomic64_inc_return(&i_seq);
		if (WARN_ON_ONCE(!new))
			continue;

		old = 0;
		if (!atomic64_try_cmpxchg_relaxed(&inode->i_sequence, &old, new))
			return old;
		return new;
	}
}

/**
 * get_futex_key() - Get parameters which are the keys for a futex
 * @uaddr:	virtual address of the futex
 * @flags:	FLAGS_*
 * @key:	address where result is stored.
 * @rw:		mapping needs to be read/write (values: FUTEX_READ,
 *              FUTEX_WRITE)
 *
 * Return: a negative error code or 0
 *
 * The key words are stored in @key on success.
 *
 * For shared mappings (when @fshared), the key is:
 *
 *   ( inode->i_sequence, page offset within mapping, offset_within_page )
 *
 * [ also see get_inode_sequence_number() ]
 *
 * For private mappings (or when !@fshared), the key is:
 *
 *   ( current->mm, address, 0 )
 *
 * This allows (cross process, where applicable) identification of the futex
 * without keeping the page pinned for the duration of the FUTEX_WAIT.
 *
 * lock_page() might sleep, the caller should not hold a spinlock.
 */
int get_futex_key(u32 __user *uaddr, unsigned int flags, union futex_key *key,
		  enum futex_access rw)
{
	unsigned long address = (unsigned long)uaddr;
	struct mm_struct *mm = current->mm;
	struct page *page;
	struct folio *folio;
	struct address_space *mapping;
	int node, err, size, ro = 0;
	bool node_updated = false;
	bool fshared;

	fshared = flags & FLAGS_SHARED;
	size = futex_size(flags);
	if (flags & FLAGS_NUMA)
		size *= 2;

	/*
	 * The futex address must be "naturally" aligned.
	 */
	key->both.offset = address % PAGE_SIZE;
	if (unlikely((address % size) != 0))
		return -EINVAL;
	address -= key->both.offset;

	if (unlikely(!access_ok(uaddr, size)))
		return -EFAULT;

	if (unlikely(should_fail_futex(fshared)))
		return -EFAULT;

	node = FUTEX_NO_NODE;

	if (flags & FLAGS_NUMA) {
		u32 __user *naddr = (void *)uaddr + size / 2;

		if (futex_get_value(&node, naddr))
			return -EFAULT;

		if ((node != FUTEX_NO_NODE) &&
		    ((unsigned int)node >= MAX_NUMNODES || !node_possible(node)))
			return -EINVAL;
	}

	if (node == FUTEX_NO_NODE && (flags & FLAGS_MPOL)) {
		node = futex_mpol(mm, address);
		node_updated = true;
	}

	if (flags & FLAGS_NUMA) {
		u32 __user *naddr = (void *)uaddr + size / 2;

		if (node == FUTEX_NO_NODE) {
			node = numa_node_id();
			node_updated = true;
		}
		if (node_updated && futex_put_value(node, naddr))
			return -EFAULT;
	}

	key->both.node = node;

	/*
	 * PROCESS_PRIVATE futexes are fast.
	 * As the mm cannot disappear under us and the 'key' only needs
	 * virtual address, we dont even have to find the underlying vma.
	 * Note : We do have to check 'uaddr' is a valid user address,
	 *        but access_ok() should be faster than find_vma()
	 */
	if (!fshared) {
		/*
		 * On no-MMU, shared futexes are treated as private, therefore
		 * we must not include the current process in the key. Since
		 * there is only one address space, the address is a unique key
		 * on its own.
		 */
		if (IS_ENABLED(CONFIG_MMU))
			key->private.mm = mm;
		else
			key->private.mm = NULL;

		key->private.address = address;
		return 0;
	}

again:
	/* Ignore any VERIFY_READ mapping (futex common case) */
	if (unlikely(should_fail_futex(true)))
		return -EFAULT;

	err = get_user_pages_fast(address, 1, FOLL_WRITE, &page);
	/*
	 * If write access is not required (eg. FUTEX_WAIT), try
	 * and get read-only access.
	 */
	if (err == -EFAULT && rw == FUTEX_READ) {
		err = get_user_pages_fast(address, 1, 0, &page);
		ro = 1;
	}
	if (err < 0)
		return err;
	else
		err = 0;

	/*
	 * The treatment of mapping from this point on is critical. The folio
	 * lock protects many things but in this context the folio lock
	 * stabilizes mapping, prevents inode freeing in the shared
	 * file-backed region case and guards against movement to swap cache.
	 *
	 * Strictly speaking the folio lock is not needed in all cases being
	 * considered here and folio lock forces unnecessarily serialization.
	 * From this point on, mapping will be re-verified if necessary and
	 * folio lock will be acquired only if it is unavoidable
	 *
	 * Mapping checks require the folio so it is looked up now. For
	 * anonymous pages, it does not matter if the folio is split
	 * in the future as the key is based on the address. For
	 * filesystem-backed pages, the precise page is required as the
	 * index of the page determines the key.
	 */
	folio = page_folio(page);
	mapping = READ_ONCE(folio->mapping);

	/*
	 * If folio->mapping is NULL, then it cannot be an anonymous
	 * page; but it might be the ZERO_PAGE or in the gate area or
	 * in a special mapping (all cases which we are happy to fail);
	 * or it may have been a good file page when get_user_pages_fast
	 * found it, but truncated or holepunched or subjected to
	 * invalidate_complete_page2 before we got the folio lock (also
	 * cases which we are happy to fail).  And we hold a reference,
	 * so refcount care in invalidate_inode_page's remove_mapping
	 * prevents drop_caches from setting mapping to NULL beneath us.
	 *
	 * The case we do have to guard against is when memory pressure made
	 * shmem_writepage move it from filecache to swapcache beneath us:
	 * an unlikely race, but we do need to retry for folio->mapping.
	 */
	if (unlikely(!mapping)) {
		int shmem_swizzled;

		/*
		 * Folio lock is required to identify which special case above
		 * applies. If this is really a shmem page then the folio lock
		 * will prevent unexpected transitions.
		 */
		folio_lock(folio);
		shmem_swizzled = folio_test_swapcache(folio) || folio->mapping;
		folio_unlock(folio);
		folio_put(folio);

		if (shmem_swizzled)
			goto again;

		return -EFAULT;
	}

	/*
	 * Private mappings are handled in a simple way.
	 *
	 * If the futex key is stored in anonymous memory, then the associated
	 * object is the mm which is implicitly pinned by the calling process.
	 *
	 * NOTE: When userspace waits on a MAP_SHARED mapping, even if
	 * it's a read-only handle, it's expected that futexes attach to
	 * the object not the particular process.
	 */
	if (folio_test_anon(folio)) {
		/*
		 * A RO anonymous page will never change and thus doesn't make
		 * sense for futex operations.
		 */
		if (unlikely(should_fail_futex(true)) || ro) {
			err = -EFAULT;
			goto out;
		}

		key->both.offset |= FUT_OFF_MMSHARED; /* ref taken on mm */
		key->private.mm = mm;
		key->private.address = address;

	} else {
		struct inode *inode;

		/*
		 * The associated futex object in this case is the inode and
		 * the folio->mapping must be traversed. Ordinarily this should
		 * be stabilised under folio lock but it's not strictly
		 * necessary in this case as we just want to pin the inode, not
		 * update i_pages or anything like that.
		 *
		 * The RCU read lock is taken as the inode is finally freed
		 * under RCU. If the mapping still matches expectations then the
		 * mapping->host can be safely accessed as being a valid inode.
		 */
		rcu_read_lock();

		if (READ_ONCE(folio->mapping) != mapping) {
			rcu_read_unlock();
			folio_put(folio);

			goto again;
		}

		inode = READ_ONCE(mapping->host);
		if (!inode) {
			rcu_read_unlock();
			folio_put(folio);

			goto again;
		}

		key->both.offset |= FUT_OFF_INODE; /* inode-based key */
		key->shared.i_seq = get_inode_sequence_number(inode);
		key->shared.pgoff = page_pgoff(folio, page);
		rcu_read_unlock();
	}

out:
	folio_put(folio);
	return err;
}

/**
 * fault_in_user_writeable() - Fault in user address and verify RW access
 * @uaddr:	pointer to faulting user space address
 *
 * Slow path to fixup the fault we just took in the atomic write
 * access to @uaddr.
 *
 * We have no generic implementation of a non-destructive write to the
 * user address. We know that we faulted in the atomic pagefault
 * disabled section so we can as well avoid the #PF overhead by
 * calling get_user_pages() right away.
 */
int fault_in_user_writeable(u32 __user *uaddr)
{
	struct mm_struct *mm = current->mm;
	int ret;

	mmap_read_lock(mm);
	ret = fixup_user_fault(mm, (unsigned long)uaddr,
			       FAULT_FLAG_WRITE, NULL);
	mmap_read_unlock(mm);

	return ret < 0 ? ret : 0;
}

/**
 * futex_top_waiter() - Return the highest priority waiter on a futex
 * @hb:		the hash bucket the futex_q's reside in
 * @key:	the futex key (to distinguish it from other futex futex_q's)
 *
 * Must be called with the hb lock held.
 */
struct futex_q *futex_top_waiter(struct futex_hash_bucket *hb, union futex_key *key)
{
	struct futex_q *this;

	plist_for_each_entry(this, &hb->chain, list) {
		if (futex_match(&this->key, key))
			return this;
	}
	return NULL;
}

/**
 * wait_for_owner_exiting - Block until the owner has exited
 * @ret: owner's current futex lock status
 * @exiting:	Pointer to the exiting task
 *
 * Caller must hold a refcount on @exiting.
 */
void wait_for_owner_exiting(int ret, struct task_struct *exiting)
{
	if (ret != -EBUSY) {
		WARN_ON_ONCE(exiting);
		return;
	}

	if (WARN_ON_ONCE(ret == -EBUSY && !exiting))
		return;

	mutex_lock(&exiting->futex_exit_mutex);
	/*
	 * No point in doing state checking here. If the waiter got here
	 * while the task was in exec()->exec_futex_release() then it can
	 * have any FUTEX_STATE_* value when the waiter has acquired the
	 * mutex. OK, if running, EXITING or DEAD if it reached exit()
	 * already. Highly unlikely and not a problem. Just one more round
	 * through the futex maze.
	 */
	mutex_unlock(&exiting->futex_exit_mutex);

	put_task_struct(exiting);
}

/**
 * __futex_unqueue() - Remove the futex_q from its futex_hash_bucket
 * @q:	The futex_q to unqueue
 *
 * The q->lock_ptr must not be NULL and must be held by the caller.
 */
void __futex_unqueue(struct futex_q *q)
{
	struct futex_hash_bucket *hb;

	if (WARN_ON_SMP(!q->lock_ptr) || WARN_ON(plist_node_empty(&q->list)))
		return;
	lockdep_assert_held(q->lock_ptr);

	hb = container_of(q->lock_ptr, struct futex_hash_bucket, lock);
	plist_del(&q->list, &hb->chain);
	futex_hb_waiters_dec(hb);
}

/* The key must be already stored in q->key. */
void futex_q_lock(struct futex_q *q, struct futex_hash_bucket *hb)
	__acquires(&hb->lock)
{
	/*
	 * Increment the counter before taking the lock so that
	 * a potential waker won't miss a to-be-slept task that is
	 * waiting for the spinlock. This is safe as all futex_q_lock()
	 * users end up calling futex_queue(). Similarly, for housekeeping,
	 * decrement the counter at futex_q_unlock() when some error has
	 * occurred and we don't end up adding the task to the list.
	 */
	futex_hb_waiters_inc(hb); /* implies smp_mb(); (A) */

	q->lock_ptr = &hb->lock;

	spin_lock(&hb->lock);
}

void futex_q_unlock(struct futex_hash_bucket *hb)
	__releases(&hb->lock)
{
	futex_hb_waiters_dec(hb);
	spin_unlock(&hb->lock);
}

void __futex_queue(struct futex_q *q, struct futex_hash_bucket *hb,
		   struct task_struct *task)
{
	int prio;

	/*
	 * The priority used to register this element is
	 * - either the real thread-priority for the real-time threads
	 * (i.e. threads with a priority lower than MAX_RT_PRIO)
	 * - or MAX_RT_PRIO for non-RT threads.
	 * Thus, all RT-threads are woken first in priority order, and
	 * the others are woken last, in FIFO order.
	 */
	prio = min(current->normal_prio, MAX_RT_PRIO);

	plist_node_init(&q->list, prio);
	plist_add(&q->list, &hb->chain);
	q->task = task;
}

/**
 * futex_unqueue() - Remove the futex_q from its futex_hash_bucket
 * @q:	The futex_q to unqueue
 *
 * The q->lock_ptr must not be held by the caller. A call to futex_unqueue() must
 * be paired with exactly one earlier call to futex_queue().
 *
 * Return:
 *  - 1 - if the futex_q was still queued (and we removed unqueued it);
 *  - 0 - if the futex_q was already removed by the waking thread
 */
int futex_unqueue(struct futex_q *q)
{
	spinlock_t *lock_ptr;
	int ret = 0;

	/* RCU so lock_ptr is not going away during locking. */
	guard(rcu)();
	/* In the common case we don't take the spinlock, which is nice. */
retry:
	/*
	 * q->lock_ptr can change between this read and the following spin_lock.
	 * Use READ_ONCE to forbid the compiler from reloading q->lock_ptr and
	 * optimizing lock_ptr out of the logic below.
	 */
	lock_ptr = READ_ONCE(q->lock_ptr);
	if (lock_ptr != NULL) {
		spin_lock(lock_ptr);
		/*
		 * q->lock_ptr can change between reading it and
		 * spin_lock(), causing us to take the wrong lock.  This
		 * corrects the race condition.
		 *
		 * Reasoning goes like this: if we have the wrong lock,
		 * q->lock_ptr must have changed (maybe several times)
		 * between reading it and the spin_lock().  It can
		 * change again after the spin_lock() but only if it was
		 * already changed before the spin_lock().  It cannot,
		 * however, change back to the original value.  Therefore
		 * we can detect whether we acquired the correct lock.
		 */
		if (unlikely(lock_ptr != q->lock_ptr)) {
			spin_unlock(lock_ptr);
			goto retry;
		}
		__futex_unqueue(q);

		BUG_ON(q->pi_state);

		spin_unlock(lock_ptr);
		ret = 1;
	}

	return ret;
}

void futex_q_lockptr_lock(struct futex_q *q)
{
	spinlock_t *lock_ptr;

	/*
	 * See futex_unqueue() why lock_ptr can change.
	 */
	guard(rcu)();
retry:
	lock_ptr = READ_ONCE(q->lock_ptr);
	spin_lock(lock_ptr);

	if (unlikely(lock_ptr != q->lock_ptr)) {
		spin_unlock(lock_ptr);
		goto retry;
	}
}

/*
 * PI futexes can not be requeued and must remove themselves from the hash
 * bucket. The hash bucket lock (i.e. lock_ptr) is held.
 */
void futex_unqueue_pi(struct futex_q *q)
{
	/*
	 * If the lock was not acquired (due to timeout or signal) then the
	 * rt_waiter is removed before futex_q is. If this is observed by
	 * an unlocker after dropping the rtmutex wait lock and before
	 * acquiring the hash bucket lock, then the unlocker dequeues the
	 * futex_q from the hash bucket list to guarantee consistent state
	 * vs. userspace. Therefore the dequeue here must be conditional.
	 */
	if (!plist_node_empty(&q->list))
		__futex_unqueue(q);

	BUG_ON(!q->pi_state);
	put_pi_state(q->pi_state);
	q->pi_state = NULL;
}

/* Constants for the pending_op argument of handle_futex_death */
#define HANDLE_DEATH_PENDING	true
#define HANDLE_DEATH_LIST	false

/*
 * Process a futex-list entry, check whether it's owned by the
 * dying task, and do notification if so:
 */
static int handle_futex_death(u32 __user *uaddr, struct task_struct *curr,
			      bool pi, bool pending_op)
{
	u32 uval, nval, mval;
	pid_t owner;
	int err;

	/* Futex address must be 32bit aligned */
	if ((((unsigned long)uaddr) % sizeof(*uaddr)) != 0)
		return -1;

retry:
	if (get_user(uval, uaddr))
		return -1;

	/*
	 * Special case for regular (non PI) futexes. The unlock path in
	 * user space has two race scenarios:
	 *
	 * 1. The unlock path releases the user space futex value and
	 *    before it can execute the futex() syscall to wake up
	 *    waiters it is killed.
	 *
	 * 2. A woken up waiter is killed before it can acquire the
	 *    futex in user space.
	 *
	 * In the second case, the wake up notification could be generated
	 * by the unlock path in user space after setting the futex value
	 * to zero or by the kernel after setting the OWNER_DIED bit below.
	 *
	 * In both cases the TID validation below prevents a wakeup of
	 * potential waiters which can cause these waiters to block
	 * forever.
	 *
	 * In both cases the following conditions are met:
	 *
	 *	1) task->robust_list->list_op_pending != NULL
	 *	   @pending_op == true
	 *	2) The owner part of user space futex value == 0
	 *	3) Regular futex: @pi == false
	 *
	 * If these conditions are met, it is safe to attempt waking up a
	 * potential waiter without touching the user space futex value and
	 * trying to set the OWNER_DIED bit. If the futex value is zero,
	 * the rest of the user space mutex state is consistent, so a woken
	 * waiter will just take over the uncontended futex. Setting the
	 * OWNER_DIED bit would create inconsistent state and malfunction
	 * of the user space owner died handling. Otherwise, the OWNER_DIED
	 * bit is already set, and the woken waiter is expected to deal with
	 * this.
	 */
	owner = uval & FUTEX_TID_MASK;

	if (pending_op && !pi && !owner) {
		futex_wake(uaddr, FLAGS_SIZE_32 | FLAGS_SHARED, 1,
			   FUTEX_BITSET_MATCH_ANY);
		return 0;
	}

	if (owner != task_pid_vnr(curr))
		return 0;

	/*
	 * Ok, this dying thread is truly holding a futex
	 * of interest. Set the OWNER_DIED bit atomically
	 * via cmpxchg, and if the value had FUTEX_WAITERS
	 * set, wake up a waiter (if any). (We have to do a
	 * futex_wake() even if OWNER_DIED is already set -
	 * to handle the rare but possible case of recursive
	 * thread-death.) The rest of the cleanup is done in
	 * userspace.
	 */
	mval = (uval & FUTEX_WAITERS) | FUTEX_OWNER_DIED;

	/*
	 * We are not holding a lock here, but we want to have
	 * the pagefault_disable/enable() protection because
	 * we want to handle the fault gracefully. If the
	 * access fails we try to fault in the futex with R/W
	 * verification via get_user_pages. get_user() above
	 * does not guarantee R/W access. If that fails we
	 * give up and leave the futex locked.
	 */
	if ((err = futex_cmpxchg_value_locked(&nval, uaddr, uval, mval))) {
		switch (err) {
		case -EFAULT:
			if (fault_in_user_writeable(uaddr))
				return -1;
			goto retry;

		case -EAGAIN:
			cond_resched();
			goto retry;

		default:
			WARN_ON_ONCE(1);
			return err;
		}
	}

	if (nval != uval)
		goto retry;

	/*
	 * Wake robust non-PI futexes here. The wakeup of
	 * PI futexes happens in exit_pi_state():
	 */
	if (!pi && (uval & FUTEX_WAITERS)) {
		futex_wake(uaddr, FLAGS_SIZE_32 | FLAGS_SHARED, 1,
			   FUTEX_BITSET_MATCH_ANY);
	}

	return 0;
}

/*
 * Fetch a robust-list pointer. Bit 0 signals PI futexes:
 */
static inline int fetch_robust_entry(struct robust_list __user **entry,
				     struct robust_list __user * __user *head,
				     unsigned int *pi)
{
	unsigned long uentry;

	if (get_user(uentry, (unsigned long __user *)head))
		return -EFAULT;

	*entry = (void __user *)(uentry & ~1UL);
	*pi = uentry & 1;

	return 0;
}

/*
 * Walk curr->robust_list (very carefully, it's a userspace list!)
 * and mark any locks found there dead, and notify any waiters.
 *
 * We silently return on any sign of list-walking problem.
 */
static void exit_robust_list(struct task_struct *curr)
{
	struct robust_list_head __user *head = curr->robust_list;
	struct robust_list __user *entry, *next_entry, *pending;
	unsigned int limit = ROBUST_LIST_LIMIT, pi, pip;
	unsigned int next_pi;
	unsigned long futex_offset;
	int rc;

	/*
	 * Fetch the list head (which was registered earlier, via
	 * sys_set_robust_list()):
	 */
	if (fetch_robust_entry(&entry, &head->list.next, &pi))
		return;
	/*
	 * Fetch the relative futex offset:
	 */
	if (get_user(futex_offset, &head->futex_offset))
		return;
	/*
	 * Fetch any possibly pending lock-add first, and handle it
	 * if it exists:
	 */
	if (fetch_robust_entry(&pending, &head->list_op_pending, &pip))
		return;

	next_entry = NULL;	/* avoid warning with gcc */
	while (entry != &head->list) {
		/*
		 * Fetch the next entry in the list before calling
		 * handle_futex_death:
		 */
		rc = fetch_robust_entry(&next_entry, &entry->next, &next_pi);
		/*
		 * A pending lock might already be on the list, so
		 * don't process it twice:
		 */
		if (entry != pending) {
			if (handle_futex_death((void __user *)entry + futex_offset,
						curr, pi, HANDLE_DEATH_LIST))
				return;
		}
		if (rc)
			return;
		entry = next_entry;
		pi = next_pi;
		/*
		 * Avoid excessively long or circular lists:
		 */
		if (!--limit)
			break;

		cond_resched();
	}

	if (pending) {
		handle_futex_death((void __user *)pending + futex_offset,
				   curr, pip, HANDLE_DEATH_PENDING);
	}
}

#ifdef CONFIG_COMPAT
static void __user *futex_uaddr(struct robust_list __user *entry,
				compat_long_t futex_offset)
{
	compat_uptr_t base = ptr_to_compat(entry);
	void __user *uaddr = compat_ptr(base + futex_offset);

	return uaddr;
}

/*
 * Fetch a robust-list pointer. Bit 0 signals PI futexes:
 */
static inline int
compat_fetch_robust_entry(compat_uptr_t *uentry, struct robust_list __user **entry,
		   compat_uptr_t __user *head, unsigned int *pi)
{
	if (get_user(*uentry, head))
		return -EFAULT;

	*entry = compat_ptr((*uentry) & ~1);
	*pi = (unsigned int)(*uentry) & 1;

	return 0;
}

/*
 * Walk curr->robust_list (very carefully, it's a userspace list!)
 * and mark any locks found there dead, and notify any waiters.
 *
 * We silently return on any sign of list-walking problem.
 */
static void compat_exit_robust_list(struct task_struct *curr)
{
	struct compat_robust_list_head __user *head = curr->compat_robust_list;
	struct robust_list __user *entry, *next_entry, *pending;
	unsigned int limit = ROBUST_LIST_LIMIT, pi, pip;
	unsigned int next_pi;
	compat_uptr_t uentry, next_uentry, upending;
	compat_long_t futex_offset;
	int rc;

	/*
	 * Fetch the list head (which was registered earlier, via
	 * sys_set_robust_list()):
	 */
	if (compat_fetch_robust_entry(&uentry, &entry, &head->list.next, &pi))
		return;
	/*
	 * Fetch the relative futex offset:
	 */
	if (get_user(futex_offset, &head->futex_offset))
		return;
	/*
	 * Fetch any possibly pending lock-add first, and handle it
	 * if it exists:
	 */
	if (compat_fetch_robust_entry(&upending, &pending,
			       &head->list_op_pending, &pip))
		return;

	next_entry = NULL;	/* avoid warning with gcc */
	while (entry != (struct robust_list __user *) &head->list) {
		/*
		 * Fetch the next entry in the list before calling
		 * handle_futex_death:
		 */
		rc = compat_fetch_robust_entry(&next_uentry, &next_entry,
			(compat_uptr_t __user *)&entry->next, &next_pi);
		/*
		 * A pending lock might already be on the list, so
		 * dont process it twice:
		 */
		if (entry != pending) {
			void __user *uaddr = futex_uaddr(entry, futex_offset);

			if (handle_futex_death(uaddr, curr, pi,
					       HANDLE_DEATH_LIST))
				return;
		}
		if (rc)
			return;
		uentry = next_uentry;
		entry = next_entry;
		pi = next_pi;
		/*
		 * Avoid excessively long or circular lists:
		 */
		if (!--limit)
			break;

		cond_resched();
	}
	if (pending) {
		void __user *uaddr = futex_uaddr(pending, futex_offset);

		handle_futex_death(uaddr, curr, pip, HANDLE_DEATH_PENDING);
	}
}
#endif

#ifdef CONFIG_FUTEX_PI

/*
 * This task is holding PI mutexes at exit time => bad.
 * Kernel cleans up PI-state, but userspace is likely hosed.
 * (Robust-futex cleanup is separate and might save the day for userspace.)
 */
static void exit_pi_state_list(struct task_struct *curr)
{
	struct list_head *next, *head = &curr->pi_state_list;
	struct futex_pi_state *pi_state;
	union futex_key key = FUTEX_KEY_INIT;

	/*
	 * The mutex mm_struct::futex_hash_lock might be acquired.
	 */
	might_sleep();
	/*
	 * Ensure the hash remains stable (no resize) during the while loop
	 * below. The hb pointer is acquired under the pi_lock so we can't block
	 * on the mutex.
	 */
	WARN_ON(curr != current);
	guard(private_hash)();
	/*
	 * We are a ZOMBIE and nobody can enqueue itself on
	 * pi_state_list anymore, but we have to be careful
	 * versus waiters unqueueing themselves:
	 */
	raw_spin_lock_irq(&curr->pi_lock);
	while (!list_empty(head)) {
		next = head->next;
		pi_state = list_entry(next, struct futex_pi_state, list);
		key = pi_state->key;
		if (1) {
			CLASS(hb, hb)(&key);

			/*
			 * We can race against put_pi_state() removing itself from the
			 * list (a waiter going away). put_pi_state() will first
			 * decrement the reference count and then modify the list, so
			 * its possible to see the list entry but fail this reference
			 * acquire.
			 *
			 * In that case; drop the locks to let put_pi_state() make
			 * progress and retry the loop.
			 */
			if (!refcount_inc_not_zero(&pi_state->refcount)) {
				raw_spin_unlock_irq(&curr->pi_lock);
				cpu_relax();
				raw_spin_lock_irq(&curr->pi_lock);
				continue;
			}
			raw_spin_unlock_irq(&curr->pi_lock);

			spin_lock(&hb->lock);
			raw_spin_lock_irq(&pi_state->pi_mutex.wait_lock);
			raw_spin_lock(&curr->pi_lock);
			/*
			 * We dropped the pi-lock, so re-check whether this
			 * task still owns the PI-state:
			 */
			if (head->next != next) {
				/* retain curr->pi_lock for the loop invariant */
				raw_spin_unlock(&pi_state->pi_mutex.wait_lock);
				spin_unlock(&hb->lock);
				put_pi_state(pi_state);
				continue;
			}

			WARN_ON(pi_state->owner != curr);
			WARN_ON(list_empty(&pi_state->list));
			list_del_init(&pi_state->list);
			pi_state->owner = NULL;

			raw_spin_unlock(&curr->pi_lock);
			raw_spin_unlock_irq(&pi_state->pi_mutex.wait_lock);
			spin_unlock(&hb->lock);
		}

		rt_mutex_futex_unlock(&pi_state->pi_mutex);
		put_pi_state(pi_state);

		raw_spin_lock_irq(&curr->pi_lock);
	}
	raw_spin_unlock_irq(&curr->pi_lock);
}
#else
static inline void exit_pi_state_list(struct task_struct *curr) { }
#endif

static void futex_cleanup(struct task_struct *tsk)
{
	if (unlikely(tsk->robust_list)) {
		exit_robust_list(tsk);
		tsk->robust_list = NULL;
	}

#ifdef CONFIG_COMPAT
	if (unlikely(tsk->compat_robust_list)) {
		compat_exit_robust_list(tsk);
		tsk->compat_robust_list = NULL;
	}
#endif

	if (unlikely(!list_empty(&tsk->pi_state_list)))
		exit_pi_state_list(tsk);
}

/**
 * futex_exit_recursive - Set the tasks futex state to FUTEX_STATE_DEAD
 * @tsk:	task to set the state on
 *
 * Set the futex exit state of the task lockless. The futex waiter code
 * observes that state when a task is exiting and loops until the task has
 * actually finished the futex cleanup. The worst case for this is that the
 * waiter runs through the wait loop until the state becomes visible.
 *
 * This is called from the recursive fault handling path in make_task_dead().
 *
 * This is best effort. Either the futex exit code has run already or
 * not. If the OWNER_DIED bit has been set on the futex then the waiter can
 * take it over. If not, the problem is pushed back to user space. If the
 * futex exit code did not run yet, then an already queued waiter might
 * block forever, but there is nothing which can be done about that.
 */
void futex_exit_recursive(struct task_struct *tsk)
{
	/* If the state is FUTEX_STATE_EXITING then futex_exit_mutex is held */
	if (tsk->futex_state == FUTEX_STATE_EXITING)
		mutex_unlock(&tsk->futex_exit_mutex);
	tsk->futex_state = FUTEX_STATE_DEAD;
}

static void futex_cleanup_begin(struct task_struct *tsk)
{
	/*
	 * Prevent various race issues against a concurrent incoming waiter
	 * including live locks by forcing the waiter to block on
	 * tsk->futex_exit_mutex when it observes FUTEX_STATE_EXITING in
	 * attach_to_pi_owner().
	 */
	mutex_lock(&tsk->futex_exit_mutex);

	/*
	 * Switch the state to FUTEX_STATE_EXITING under tsk->pi_lock.
	 *
	 * This ensures that all subsequent checks of tsk->futex_state in
	 * attach_to_pi_owner() must observe FUTEX_STATE_EXITING with
	 * tsk->pi_lock held.
	 *
	 * It guarantees also that a pi_state which was queued right before
	 * the state change under tsk->pi_lock by a concurrent waiter must
	 * be observed in exit_pi_state_list().
	 */
	raw_spin_lock_irq(&tsk->pi_lock);
	tsk->futex_state = FUTEX_STATE_EXITING;
	raw_spin_unlock_irq(&tsk->pi_lock);
}

static void futex_cleanup_end(struct task_struct *tsk, int state)
{
	/*
	 * Lockless store. The only side effect is that an observer might
	 * take another loop until it becomes visible.
	 */
	tsk->futex_state = state;
	/*
	 * Drop the exit protection. This unblocks waiters which observed
	 * FUTEX_STATE_EXITING to reevaluate the state.
	 */
	mutex_unlock(&tsk->futex_exit_mutex);
}

void futex_exec_release(struct task_struct *tsk)
{
	/*
	 * The state handling is done for consistency, but in the case of
	 * exec() there is no way to prevent further damage as the PID stays
	 * the same. But for the unlikely and arguably buggy case that a
	 * futex is held on exec(), this provides at least as much state
	 * consistency protection which is possible.
	 */
	futex_cleanup_begin(tsk);
	futex_cleanup(tsk);
	/*
	 * Reset the state to FUTEX_STATE_OK. The task is alive and about
	 * exec a new binary.
	 */
	futex_cleanup_end(tsk, FUTEX_STATE_OK);
}

void futex_exit_release(struct task_struct *tsk)
{
	futex_cleanup_begin(tsk);
	futex_cleanup(tsk);
	futex_cleanup_end(tsk, FUTEX_STATE_DEAD);
}

static void futex_hash_bucket_init(struct futex_hash_bucket *fhb,
				   struct futex_private_hash *fph)
{
#ifdef CONFIG_FUTEX_PRIVATE_HASH
	fhb->priv = fph;
#endif
	atomic_set(&fhb->waiters, 0);
	plist_head_init(&fhb->chain);
	spin_lock_init(&fhb->lock);
}

#define FH_CUSTOM	0x01

#ifdef CONFIG_FUTEX_PRIVATE_HASH

/*
 * futex-ref
 *
 * Heavily inspired by percpu-rwsem/percpu-refcount; not reusing any of that
 * code because it just doesn't fit right.
 *
 * Dual counter, per-cpu / atomic approach like percpu-refcount, except it
 * re-initializes the state automatically, such that the fph swizzle is also a
 * transition back to per-cpu.
 */

static void futex_ref_rcu(struct rcu_head *head);

static void __futex_ref_atomic_begin(struct futex_private_hash *fph)
{
	struct mm_struct *mm = fph->mm;

	/*
	 * The counter we're about to switch to must have fully switched;
	 * otherwise it would be impossible for it to have reported success
	 * from futex_ref_is_dead().
	 */
	WARN_ON_ONCE(atomic_long_read(&mm->futex_atomic) != 0);

	/*
	 * Set the atomic to the bias value such that futex_ref_{get,put}()
	 * will never observe 0. Will be fixed up in __futex_ref_atomic_end()
	 * when folding in the percpu count.
	 */
	atomic_long_set(&mm->futex_atomic, LONG_MAX);
	smp_store_release(&fph->state, FR_ATOMIC);

	call_rcu_hurry(&mm->futex_rcu, futex_ref_rcu);
}

static void __futex_ref_atomic_end(struct futex_private_hash *fph)
{
	struct mm_struct *mm = fph->mm;
	unsigned int count = 0;
	long ret;
	int cpu;

	/*
	 * Per __futex_ref_atomic_begin() the state of the fph must be ATOMIC
	 * and per this RCU callback, everybody must now observe this state and
	 * use the atomic variable.
	 */
	WARN_ON_ONCE(fph->state != FR_ATOMIC);

	/*
	 * Therefore the per-cpu counter is now stable, sum and reset.
	 */
	for_each_possible_cpu(cpu) {
		unsigned int *ptr = per_cpu_ptr(mm->futex_ref, cpu);
		count += *ptr;
		*ptr = 0;
	}

	/*
	 * Re-init for the next cycle.
	 */
	this_cpu_inc(*mm->futex_ref); /* 0 -> 1 */

	/*
	 * Add actual count, subtract bias and initial refcount.
	 *
	 * The moment this atomic operation happens, futex_ref_is_dead() can
	 * become true.
	 */
	ret = atomic_long_add_return(count - LONG_MAX - 1, &mm->futex_atomic);
	if (!ret)
		wake_up_var(mm);

	WARN_ON_ONCE(ret < 0);
	mmput_async(mm);
}

static void futex_ref_rcu(struct rcu_head *head)
{
	struct mm_struct *mm = container_of(head, struct mm_struct, futex_rcu);
	struct futex_private_hash *fph = rcu_dereference_raw(mm->futex_phash);

	if (fph->state == FR_PERCPU) {
		/*
		 * Per this extra grace-period, everybody must now observe
		 * fph as the current fph and no previously observed fph's
		 * are in-flight.
		 *
		 * Notably, nobody will now rely on the atomic
		 * futex_ref_is_dead() state anymore so we can begin the
		 * migration of the per-cpu counter into the atomic.
		 */
		__futex_ref_atomic_begin(fph);
		return;
	}

	__futex_ref_atomic_end(fph);
}

/*
 * Drop the initial refcount and transition to atomics.
 */
static void futex_ref_drop(struct futex_private_hash *fph)
{
	struct mm_struct *mm = fph->mm;

	/*
	 * Can only transition the current fph;
	 */
	WARN_ON_ONCE(rcu_dereference_raw(mm->futex_phash) != fph);
	/*
	 * We enqueue at least one RCU callback. Ensure mm stays if the task
	 * exits before the transition is completed.
	 */
	mmget(mm);

	/*
	 * In order to avoid the following scenario:
	 *
	 * futex_hash()			__futex_pivot_hash()
	 *   guard(rcu);		  guard(mm->futex_hash_lock);
	 *   fph = mm->futex_phash;
	 *				  rcu_assign_pointer(&mm->futex_phash, new);
	 *				futex_hash_allocate()
	 *				  futex_ref_drop()
	 *				    fph->state = FR_ATOMIC;
	 *				    atomic_set(, BIAS);
	 *
	 *   futex_private_hash_get(fph); // OOPS
	 *
	 * Where an old fph (which is FR_ATOMIC) and should fail on
	 * inc_not_zero, will succeed because a new transition is started and
	 * the atomic is bias'ed away from 0.
	 *
	 * There must be at least one full grace-period between publishing a
	 * new fph and trying to replace it.
	 */
	if (poll_state_synchronize_rcu(mm->futex_batches)) {
		/*
		 * There was a grace-period, we can begin now.
		 */
		__futex_ref_atomic_begin(fph);
		return;
	}

	call_rcu_hurry(&mm->futex_rcu, futex_ref_rcu);
}

static bool futex_ref_get(struct futex_private_hash *fph)
{
	struct mm_struct *mm = fph->mm;

	guard(preempt)();

	if (READ_ONCE(fph->state) == FR_PERCPU) {
		__this_cpu_inc(*mm->futex_ref);
		return true;
	}

	return atomic_long_inc_not_zero(&mm->futex_atomic);
}

static bool futex_ref_put(struct futex_private_hash *fph)
{
	struct mm_struct *mm = fph->mm;

	guard(preempt)();

	if (READ_ONCE(fph->state) == FR_PERCPU) {
		__this_cpu_dec(*mm->futex_ref);
		return false;
	}

	return atomic_long_dec_and_test(&mm->futex_atomic);
}

static bool futex_ref_is_dead(struct futex_private_hash *fph)
{
	struct mm_struct *mm = fph->mm;

	guard(rcu)();

	if (smp_load_acquire(&fph->state) == FR_PERCPU)
		return false;

	return atomic_long_read(&mm->futex_atomic) == 0;
}

int futex_mm_init(struct mm_struct *mm)
{
	mutex_init(&mm->futex_hash_lock);
	RCU_INIT_POINTER(mm->futex_phash, NULL);
	mm->futex_phash_new = NULL;
	/* futex-ref */
	mm->futex_ref = NULL;
	atomic_long_set(&mm->futex_atomic, 0);
	mm->futex_batches = get_state_synchronize_rcu();
	return 0;
}

void futex_hash_free(struct mm_struct *mm)
{
	struct futex_private_hash *fph;

	free_percpu(mm->futex_ref);
	kvfree(mm->futex_phash_new);
	fph = rcu_dereference_raw(mm->futex_phash);
	if (fph)
		kvfree(fph);
}

static bool futex_pivot_pending(struct mm_struct *mm)
{
	struct futex_private_hash *fph;

	guard(rcu)();

	if (!mm->futex_phash_new)
		return true;

	fph = rcu_dereference(mm->futex_phash);
	return futex_ref_is_dead(fph);
}

static bool futex_hash_less(struct futex_private_hash *a,
			    struct futex_private_hash *b)
{
	/* user provided always wins */
	if (!a->custom && b->custom)
		return true;
	if (a->custom && !b->custom)
		return false;

	/* zero-sized hash wins */
	if (!b->hash_mask)
		return true;
	if (!a->hash_mask)
		return false;

	/* keep the biggest */
	if (a->hash_mask < b->hash_mask)
		return true;
	if (a->hash_mask > b->hash_mask)
		return false;

	return false; /* equal */
}

static int futex_hash_allocate(unsigned int hash_slots, unsigned int flags)
{
	struct mm_struct *mm = current->mm;
	struct futex_private_hash *fph;
	bool custom = flags & FH_CUSTOM;
	int i;

	if (hash_slots && (hash_slots == 1 || !is_power_of_2(hash_slots)))
		return -EINVAL;

	/*
	 * Once we've disabled the global hash there is no way back.
	 */
	scoped_guard(rcu) {
		fph = rcu_dereference(mm->futex_phash);
		if (fph && !fph->hash_mask) {
			if (custom)
				return -EBUSY;
			return 0;
		}
	}

	if (!mm->futex_ref) {
		/*
		 * This will always be allocated by the first thread and
		 * therefore requires no locking.
		 */
		mm->futex_ref = alloc_percpu(unsigned int);
		if (!mm->futex_ref)
			return -ENOMEM;
		this_cpu_inc(*mm->futex_ref); /* 0 -> 1 */
	}

	fph = kvzalloc(struct_size(fph, queues, hash_slots),
		       GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (!fph)
		return -ENOMEM;

	fph->hash_mask = hash_slots ? hash_slots - 1 : 0;
	fph->custom = custom;
	fph->mm = mm;

	for (i = 0; i < hash_slots; i++)
		futex_hash_bucket_init(&fph->queues[i], fph);

	if (custom) {
		/*
		 * Only let prctl() wait / retry; don't unduly delay clone().
		 */
again:
		wait_var_event(mm, futex_pivot_pending(mm));
	}

	scoped_guard(mutex, &mm->futex_hash_lock) {
		struct futex_private_hash *free __free(kvfree) = NULL;
		struct futex_private_hash *cur, *new;

		cur = rcu_dereference_protected(mm->futex_phash,
						lockdep_is_held(&mm->futex_hash_lock));
		new = mm->futex_phash_new;
		mm->futex_phash_new = NULL;

		if (fph) {
			if (cur && !cur->hash_mask) {
				/*
				 * If two threads simultaneously request the global
				 * hash then the first one performs the switch,
				 * the second one returns here.
				 */
				free = fph;
				mm->futex_phash_new = new;
				return -EBUSY;
			}
			if (cur && !new) {
				/*
				 * If we have an existing hash, but do not yet have
				 * allocated a replacement hash, drop the initial
				 * reference on the existing hash.
				 */
				futex_ref_drop(cur);
			}

			if (new) {
				/*
				 * Two updates raced; throw out the lesser one.
				 */
				if (futex_hash_less(new, fph)) {
					free = new;
					new = fph;
				} else {
					free = fph;
				}
			} else {
				new = fph;
			}
			fph = NULL;
		}

		if (new) {
			/*
			 * Will set mm->futex_phash_new on failure;
			 * futex_private_hash_get() will try again.
			 */
			if (!__futex_pivot_hash(mm, new) && custom)
				goto again;
		}
	}
	return 0;
}

int futex_hash_allocate_default(void)
{
	unsigned int threads, buckets, current_buckets = 0;
	struct futex_private_hash *fph;

	if (!current->mm)
		return 0;

	scoped_guard(rcu) {
		threads = min_t(unsigned int,
				get_nr_threads(current),
				num_online_cpus());

		fph = rcu_dereference(current->mm->futex_phash);
		if (fph) {
			if (fph->custom)
				return 0;

			current_buckets = fph->hash_mask + 1;
		}
	}

	/*
	 * The default allocation will remain within
	 *   16 <= threads * 4 <= global hash size
	 */
	buckets = roundup_pow_of_two(4 * threads);
	buckets = clamp(buckets, 16, futex_hashmask + 1);

	if (current_buckets >= buckets)
		return 0;

	return futex_hash_allocate(buckets, 0);
}

static int futex_hash_get_slots(void)
{
	struct futex_private_hash *fph;

	guard(rcu)();
	fph = rcu_dereference(current->mm->futex_phash);
	if (fph && fph->hash_mask)
		return fph->hash_mask + 1;
	return 0;
}

#else

static int futex_hash_allocate(unsigned int hash_slots, unsigned int flags)
{
	return -EINVAL;
}

static int futex_hash_get_slots(void)
{
	return 0;
}

#endif

int futex_hash_prctl(unsigned long arg2, unsigned long arg3, unsigned long arg4)
{
	unsigned int flags = FH_CUSTOM;
	int ret;

	switch (arg2) {
	case PR_FUTEX_HASH_SET_SLOTS:
		if (arg4)
			return -EINVAL;
		ret = futex_hash_allocate(arg3, flags);
		break;

	case PR_FUTEX_HASH_GET_SLOTS:
		ret = futex_hash_get_slots();
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int __init futex_init(void)
{
	unsigned long hashsize, i;
	unsigned int order, n;
	unsigned long size;

#ifdef CONFIG_BASE_SMALL
	hashsize = 16;
#else
	hashsize = 256 * num_possible_cpus();
	hashsize /= num_possible_nodes();
	hashsize = max(4, hashsize);
	hashsize = roundup_pow_of_two(hashsize);
#endif
	futex_hashshift = ilog2(hashsize);
	size = sizeof(struct futex_hash_bucket) * hashsize;
	order = get_order(size);

	for_each_node(n) {
		struct futex_hash_bucket *table;

		if (order > MAX_PAGE_ORDER)
			table = vmalloc_huge_node(size, GFP_KERNEL, n);
		else
			table = alloc_pages_exact_nid(n, size, GFP_KERNEL);

		BUG_ON(!table);

		for (i = 0; i < hashsize; i++)
			futex_hash_bucket_init(&table[i], NULL);

		futex_queues[n] = table;
	}

	futex_hashmask = hashsize - 1;
	pr_info("futex hash table entries: %lu (%lu bytes on %d NUMA nodes, total %lu KiB, %s).\n",
		hashsize, size, num_possible_nodes(), size * num_possible_nodes() / 1024,
		order > MAX_PAGE_ORDER ? "vmalloc" : "linear");
	return 0;
}
core_initcall(futex_init);
