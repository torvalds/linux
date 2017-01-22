/*
 * fs/dcache.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

/*
 * Notes on the allocation strategy:
 *
 * The dcache is a master of the icache - whenever a dcache entry
 * exists, the inode will always exist. "iput()" is done either when
 * the dcache entry is deleted or garbage collected.
 */

#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <asm/uaccess.h>
#include <linux/security.h>
#include <linux/seqlock.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/fs_struct.h>
#include <linux/hardirq.h>
#include <linux/bit_spinlock.h>
#include <linux/rculist_bl.h>
#include <linux/prefetch.h>
#include <linux/ratelimit.h>
#include <linux/list_lru.h>
#include <linux/kasan.h>

#include "internal.h"
#include "mount.h"

/*
 * Usage:
 * dcache->d_inode->i_lock protects:
 *   - i_dentry, d_u.d_alias, d_inode of aliases
 * dcache_hash_bucket lock protects:
 *   - the dcache hash table
 * s_anon bl list spinlock protects:
 *   - the s_anon list (see __d_drop)
 * dentry->d_sb->s_dentry_lru_lock protects:
 *   - the dcache lru lists and counters
 * d_lock protects:
 *   - d_flags
 *   - d_name
 *   - d_lru
 *   - d_count
 *   - d_unhashed()
 *   - d_parent and d_subdirs
 *   - childrens' d_child and d_parent
 *   - d_u.d_alias, d_inode
 *
 * Ordering:
 * dentry->d_inode->i_lock
 *   dentry->d_lock
 *     dentry->d_sb->s_dentry_lru_lock
 *     dcache_hash_bucket lock
 *     s_anon lock
 *
 * If there is an ancestor relationship:
 * dentry->d_parent->...->d_parent->d_lock
 *   ...
 *     dentry->d_parent->d_lock
 *       dentry->d_lock
 *
 * If no ancestor relationship:
 * if (dentry1 < dentry2)
 *   dentry1->d_lock
 *     dentry2->d_lock
 */
int sysctl_vfs_cache_pressure __read_mostly = 100;
EXPORT_SYMBOL_GPL(sysctl_vfs_cache_pressure);

__cacheline_aligned_in_smp DEFINE_SEQLOCK(rename_lock);

EXPORT_SYMBOL(rename_lock);

static struct kmem_cache *dentry_cache __read_mostly;

/*
 * This is the single most critical data structure when it comes
 * to the dcache: the hashtable for lookups. Somebody should try
 * to make this good - I've just made it work.
 *
 * This hash-function tries to avoid losing too many bits of hash
 * information, yet avoid using a prime hash-size or similar.
 */

static unsigned int d_hash_mask __read_mostly;
static unsigned int d_hash_shift __read_mostly;

static struct hlist_bl_head *dentry_hashtable __read_mostly;

static inline struct hlist_bl_head *d_hash(const struct dentry *parent,
					unsigned int hash)
{
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	return dentry_hashtable + hash_32(hash, d_hash_shift);
}

/* Statistics gathering. */
struct dentry_stat_t dentry_stat = {
	.age_limit = 45,
};

static DEFINE_PER_CPU(long, nr_dentry);
static DEFINE_PER_CPU(long, nr_dentry_unused);

#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)

/*
 * Here we resort to our own counters instead of using generic per-cpu counters
 * for consistency with what the vfs inode code does. We are expected to harvest
 * better code and performance by having our own specialized counters.
 *
 * Please note that the loop is done over all possible CPUs, not over all online
 * CPUs. The reason for this is that we don't want to play games with CPUs going
 * on and off. If one of them goes off, we will just keep their counters.
 *
 * glommer: See cffbc8a for details, and if you ever intend to change this,
 * please update all vfs counters to match.
 */
static long get_nr_dentry(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_dentry, i);
	return sum < 0 ? 0 : sum;
}

static long get_nr_dentry_unused(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_dentry_unused, i);
	return sum < 0 ? 0 : sum;
}

int proc_nr_dentry(struct ctl_table *table, int write, void __user *buffer,
		   size_t *lenp, loff_t *ppos)
{
	dentry_stat.nr_dentry = get_nr_dentry();
	dentry_stat.nr_unused = get_nr_dentry_unused();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}
#endif

/*
 * Compare 2 name strings, return 0 if they match, otherwise non-zero.
 * The strings are both count bytes long, and count is non-zero.
 */
#ifdef CONFIG_DCACHE_WORD_ACCESS

#include <asm/word-at-a-time.h>
/*
 * NOTE! 'cs' and 'scount' come from a dentry, so it has a
 * aligned allocation for this particular component. We don't
 * strictly need the load_unaligned_zeropad() safety, but it
 * doesn't hurt either.
 *
 * In contrast, 'ct' and 'tcount' can be from a pathname, and do
 * need the careful unaligned handling.
 */
static inline int dentry_string_cmp(const unsigned char *cs, const unsigned char *ct, unsigned tcount)
{
	unsigned long a,b,mask;

	for (;;) {
		a = *(unsigned long *)cs;
		b = load_unaligned_zeropad(ct);
		if (tcount < sizeof(unsigned long))
			break;
		if (unlikely(a != b))
			return 1;
		cs += sizeof(unsigned long);
		ct += sizeof(unsigned long);
		tcount -= sizeof(unsigned long);
		if (!tcount)
			return 0;
	}
	mask = bytemask_from_count(tcount);
	return unlikely(!!((a ^ b) & mask));
}

#else

static inline int dentry_string_cmp(const unsigned char *cs, const unsigned char *ct, unsigned tcount)
{
	do {
		if (*cs != *ct)
			return 1;
		cs++;
		ct++;
		tcount--;
	} while (tcount);
	return 0;
}

#endif

static inline int dentry_cmp(const struct dentry *dentry, const unsigned char *ct, unsigned tcount)
{
	const unsigned char *cs;
	/*
	 * Be careful about RCU walk racing with rename:
	 * use ACCESS_ONCE to fetch the name pointer.
	 *
	 * NOTE! Even if a rename will mean that the length
	 * was not loaded atomically, we don't care. The
	 * RCU walk will check the sequence count eventually,
	 * and catch it. And we won't overrun the buffer,
	 * because we're reading the name pointer atomically,
	 * and a dentry name is guaranteed to be properly
	 * terminated with a NUL byte.
	 *
	 * End result: even if 'len' is wrong, we'll exit
	 * early because the data cannot match (there can
	 * be no NUL in the ct/tcount data)
	 */
	cs = ACCESS_ONCE(dentry->d_name.name);
	smp_read_barrier_depends();
	return dentry_string_cmp(cs, ct, tcount);
}

struct external_name {
	union {
		atomic_t count;
		struct rcu_head head;
	} u;
	unsigned char name[];
};

static inline struct external_name *external_name(struct dentry *dentry)
{
	return container_of(dentry->d_name.name, struct external_name, name[0]);
}

static void __d_free(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);

	kmem_cache_free(dentry_cache, dentry); 
}

static void __d_free_external(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);
	kfree(external_name(dentry));
	kmem_cache_free(dentry_cache, dentry); 
}

static inline int dname_external(const struct dentry *dentry)
{
	return dentry->d_name.name != dentry->d_iname;
}

static inline void __d_set_inode_and_type(struct dentry *dentry,
					  struct inode *inode,
					  unsigned type_flags)
{
	unsigned flags;

	dentry->d_inode = inode;
	flags = READ_ONCE(dentry->d_flags);
	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	flags |= type_flags;
	WRITE_ONCE(dentry->d_flags, flags);
}

static inline void __d_clear_type_and_inode(struct dentry *dentry)
{
	unsigned flags = READ_ONCE(dentry->d_flags);

	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	WRITE_ONCE(dentry->d_flags, flags);
	dentry->d_inode = NULL;
}

static void dentry_free(struct dentry *dentry)
{
	WARN_ON(!hlist_unhashed(&dentry->d_u.d_alias));
	if (unlikely(dname_external(dentry))) {
		struct external_name *p = external_name(dentry);
		if (likely(atomic_dec_and_test(&p->u.count))) {
			call_rcu(&dentry->d_u.d_rcu, __d_free_external);
			return;
		}
	}
	/* if dentry was never visible to RCU, immediate free is OK */
	if (!(dentry->d_flags & DCACHE_RCUACCESS))
		__d_free(&dentry->d_u.d_rcu);
	else
		call_rcu(&dentry->d_u.d_rcu, __d_free);
}

/**
 * dentry_rcuwalk_invalidate - invalidate in-progress rcu-walk lookups
 * @dentry: the target dentry
 * After this call, in-progress rcu-walk path lookup will fail. This
 * should be called after unhashing, and after changing d_inode (if
 * the dentry has not already been unhashed).
 */
static inline void dentry_rcuwalk_invalidate(struct dentry *dentry)
{
	lockdep_assert_held(&dentry->d_lock);
	/* Go through am invalidation barrier */
	write_seqcount_invalidate(&dentry->d_seq);
}

/*
 * Release the dentry's inode, using the filesystem
 * d_iput() operation if defined. Dentry has no refcount
 * and is unhashed.
 */
static void dentry_iput(struct dentry * dentry)
	__releases(dentry->d_lock)
	__releases(dentry->d_inode->i_lock)
{
	struct inode *inode = dentry->d_inode;
	if (inode) {
		__d_clear_type_and_inode(dentry);
		hlist_del_init(&dentry->d_u.d_alias);
		spin_unlock(&dentry->d_lock);
		spin_unlock(&inode->i_lock);
		if (!inode->i_nlink)
			fsnotify_inoderemove(inode);
		if (dentry->d_op && dentry->d_op->d_iput)
			dentry->d_op->d_iput(dentry, inode);
		else
			iput(inode);
	} else {
		spin_unlock(&dentry->d_lock);
	}
}

/*
 * Release the dentry's inode, using the filesystem
 * d_iput() operation if defined. dentry remains in-use.
 */
static void dentry_unlink_inode(struct dentry * dentry)
	__releases(dentry->d_lock)
	__releases(dentry->d_inode->i_lock)
{
	struct inode *inode = dentry->d_inode;

	raw_write_seqcount_begin(&dentry->d_seq);
	__d_clear_type_and_inode(dentry);
	hlist_del_init(&dentry->d_u.d_alias);
	raw_write_seqcount_end(&dentry->d_seq);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);
	if (!inode->i_nlink)
		fsnotify_inoderemove(inode);
	if (dentry->d_op && dentry->d_op->d_iput)
		dentry->d_op->d_iput(dentry, inode);
	else
		iput(inode);
}

/*
 * The DCACHE_LRU_LIST bit is set whenever the 'd_lru' entry
 * is in use - which includes both the "real" per-superblock
 * LRU list _and_ the DCACHE_SHRINK_LIST use.
 *
 * The DCACHE_SHRINK_LIST bit is set whenever the dentry is
 * on the shrink list (ie not on the superblock LRU list).
 *
 * The per-cpu "nr_dentry_unused" counters are updated with
 * the DCACHE_LRU_LIST bit.
 *
 * These helper functions make sure we always follow the
 * rules. d_lock must be held by the caller.
 */
#define D_FLAG_VERIFY(dentry,x) WARN_ON_ONCE(((dentry)->d_flags & (DCACHE_LRU_LIST | DCACHE_SHRINK_LIST)) != (x))
static void d_lru_add(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, 0);
	dentry->d_flags |= DCACHE_LRU_LIST;
	this_cpu_inc(nr_dentry_unused);
	WARN_ON_ONCE(!list_lru_add(&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
}

static void d_lru_del(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags &= ~DCACHE_LRU_LIST;
	this_cpu_dec(nr_dentry_unused);
	WARN_ON_ONCE(!list_lru_del(&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
}

static void d_shrink_del(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_SHRINK_LIST | DCACHE_LRU_LIST);
	list_del_init(&dentry->d_lru);
	dentry->d_flags &= ~(DCACHE_SHRINK_LIST | DCACHE_LRU_LIST);
	this_cpu_dec(nr_dentry_unused);
}

static void d_shrink_add(struct dentry *dentry, struct list_head *list)
{
	D_FLAG_VERIFY(dentry, 0);
	list_add(&dentry->d_lru, list);
	dentry->d_flags |= DCACHE_SHRINK_LIST | DCACHE_LRU_LIST;
	this_cpu_inc(nr_dentry_unused);
}

/*
 * These can only be called under the global LRU lock, ie during the
 * callback for freeing the LRU list. "isolate" removes it from the
 * LRU lists entirely, while shrink_move moves it to the indicated
 * private list.
 */
static void d_lru_isolate(struct list_lru_one *lru, struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags &= ~DCACHE_LRU_LIST;
	this_cpu_dec(nr_dentry_unused);
	list_lru_isolate(lru, &dentry->d_lru);
}

static void d_lru_shrink_move(struct list_lru_one *lru, struct dentry *dentry,
			      struct list_head *list)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags |= DCACHE_SHRINK_LIST;
	list_lru_isolate_move(lru, &dentry->d_lru, list);
}

/*
 * dentry_lru_(add|del)_list) must be called with d_lock held.
 */
static void dentry_lru_add(struct dentry *dentry)
{
	if (unlikely(!(dentry->d_flags & DCACHE_LRU_LIST)))
		d_lru_add(dentry);
}

/**
 * d_drop - drop a dentry
 * @dentry: dentry to drop
 *
 * d_drop() unhashes the entry from the parent dentry hashes, so that it won't
 * be found through a VFS lookup any more. Note that this is different from
 * deleting the dentry - d_delete will try to mark the dentry negative if
 * possible, giving a successful _negative_ lookup, while d_drop will
 * just make the cache lookup fail.
 *
 * d_drop() is used mainly for stuff that wants to invalidate a dentry for some
 * reason (NFS timeouts or autofs deletes).
 *
 * __d_drop requires dentry->d_lock.
 */
void __d_drop(struct dentry *dentry)
{
	if (!d_unhashed(dentry)) {
		struct hlist_bl_head *b;
		/*
		 * Hashed dentries are normally on the dentry hashtable,
		 * with the exception of those newly allocated by
		 * d_obtain_alias, which are always IS_ROOT:
		 */
		if (unlikely(IS_ROOT(dentry)))
			b = &dentry->d_sb->s_anon;
		else
			b = d_hash(dentry->d_parent, dentry->d_name.hash);

		hlist_bl_lock(b);
		__hlist_bl_del(&dentry->d_hash);
		dentry->d_hash.pprev = NULL;
		hlist_bl_unlock(b);
		dentry_rcuwalk_invalidate(dentry);
	}
}
EXPORT_SYMBOL(__d_drop);

void d_drop(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(d_drop);

static void __dentry_kill(struct dentry *dentry)
{
	struct dentry *parent = NULL;
	bool can_free = true;
	if (!IS_ROOT(dentry))
		parent = dentry->d_parent;

	/*
	 * The dentry is now unrecoverably dead to the world.
	 */
	lockref_mark_dead(&dentry->d_lockref);

	/*
	 * inform the fs via d_prune that this dentry is about to be
	 * unhashed and destroyed.
	 */
	if (dentry->d_flags & DCACHE_OP_PRUNE)
		dentry->d_op->d_prune(dentry);

	if (dentry->d_flags & DCACHE_LRU_LIST) {
		if (!(dentry->d_flags & DCACHE_SHRINK_LIST))
			d_lru_del(dentry);
	}
	/* if it was on the hash then remove it */
	__d_drop(dentry);
	__list_del_entry(&dentry->d_child);
	/*
	 * Inform d_walk() that we are no longer attached to the
	 * dentry tree
	 */
	dentry->d_flags |= DCACHE_DENTRY_KILLED;
	if (parent)
		spin_unlock(&parent->d_lock);
	dentry_iput(dentry);
	/*
	 * dentry_iput drops the locks, at which point nobody (except
	 * transient RCU lookups) can reach this dentry.
	 */
	BUG_ON(dentry->d_lockref.count > 0);
	this_cpu_dec(nr_dentry);
	if (dentry->d_op && dentry->d_op->d_release)
		dentry->d_op->d_release(dentry);

	spin_lock(&dentry->d_lock);
	if (dentry->d_flags & DCACHE_SHRINK_LIST) {
		dentry->d_flags |= DCACHE_MAY_FREE;
		can_free = false;
	}
	spin_unlock(&dentry->d_lock);
	if (likely(can_free))
		dentry_free(dentry);
}

/*
 * Finish off a dentry we've decided to kill.
 * dentry->d_lock must be held, returns with it unlocked.
 * If ref is non-zero, then decrement the refcount too.
 * Returns dentry requiring refcount drop, or NULL if we're done.
 */
static struct dentry *dentry_kill(struct dentry *dentry)
	__releases(dentry->d_lock)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *parent = NULL;

	if (inode && unlikely(!spin_trylock(&inode->i_lock)))
		goto failed;

	if (!IS_ROOT(dentry)) {
		parent = dentry->d_parent;
		if (unlikely(!spin_trylock(&parent->d_lock))) {
			if (inode)
				spin_unlock(&inode->i_lock);
			goto failed;
		}
	}

	__dentry_kill(dentry);
	return parent;

failed:
	spin_unlock(&dentry->d_lock);
	return dentry; /* try again with same dentry */
}

static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;
	if (IS_ROOT(dentry))
		return NULL;
	if (unlikely(dentry->d_lockref.count < 0))
		return NULL;
	if (likely(spin_trylock(&parent->d_lock)))
		return parent;
	rcu_read_lock();
	spin_unlock(&dentry->d_lock);
again:
	parent = ACCESS_ONCE(dentry->d_parent);
	spin_lock(&parent->d_lock);
	/*
	 * We can't blindly lock dentry until we are sure
	 * that we won't violate the locking order.
	 * Any changes of dentry->d_parent must have
	 * been done with parent->d_lock held, so
	 * spin_lock() above is enough of a barrier
	 * for checking if it's still our child.
	 */
	if (unlikely(parent != dentry->d_parent)) {
		spin_unlock(&parent->d_lock);
		goto again;
	}
	rcu_read_unlock();
	if (parent != dentry)
		spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	else
		parent = NULL;
	return parent;
}

/*
 * Try to do a lockless dput(), and return whether that was successful.
 *
 * If unsuccessful, we return false, having already taken the dentry lock.
 *
 * The caller needs to hold the RCU read lock, so that the dentry is
 * guaranteed to stay around even if the refcount goes down to zero!
 */
static inline bool fast_dput(struct dentry *dentry)
{
	int ret;
	unsigned int d_flags;

	/*
	 * If we have a d_op->d_delete() operation, we sould not
	 * let the dentry count go to zero, so use "put_or_lock".
	 */
	if (unlikely(dentry->d_flags & DCACHE_OP_DELETE))
		return lockref_put_or_lock(&dentry->d_lockref);

	/*
	 * .. otherwise, we can try to just decrement the
	 * lockref optimistically.
	 */
	ret = lockref_put_return(&dentry->d_lockref);

	/*
	 * If the lockref_put_return() failed due to the lock being held
	 * by somebody else, the fast path has failed. We will need to
	 * get the lock, and then check the count again.
	 */
	if (unlikely(ret < 0)) {
		spin_lock(&dentry->d_lock);
		if (dentry->d_lockref.count > 1) {
			dentry->d_lockref.count--;
			spin_unlock(&dentry->d_lock);
			return 1;
		}
		return 0;
	}

	/*
	 * If we weren't the last ref, we're done.
	 */
	if (ret)
		return 1;

	/*
	 * Careful, careful. The reference count went down
	 * to zero, but we don't hold the dentry lock, so
	 * somebody else could get it again, and do another
	 * dput(), and we need to not race with that.
	 *
	 * However, there is a very special and common case
	 * where we don't care, because there is nothing to
	 * do: the dentry is still hashed, it does not have
	 * a 'delete' op, and it's referenced and already on
	 * the LRU list.
	 *
	 * NOTE! Since we aren't locked, these values are
	 * not "stable". However, it is sufficient that at
	 * some point after we dropped the reference the
	 * dentry was hashed and the flags had the proper
	 * value. Other dentry users may have re-gotten
	 * a reference to the dentry and change that, but
	 * our work is done - we can leave the dentry
	 * around with a zero refcount.
	 */
	smp_rmb();
	d_flags = ACCESS_ONCE(dentry->d_flags);
	d_flags &= DCACHE_REFERENCED | DCACHE_LRU_LIST | DCACHE_DISCONNECTED;

	/* Nothing to do? Dropping the reference was all we needed? */
	if (d_flags == (DCACHE_REFERENCED | DCACHE_LRU_LIST) && !d_unhashed(dentry))
		return 1;

	/*
	 * Not the fast normal case? Get the lock. We've already decremented
	 * the refcount, but we'll need to re-check the situation after
	 * getting the lock.
	 */
	spin_lock(&dentry->d_lock);

	/*
	 * Did somebody else grab a reference to it in the meantime, and
	 * we're no longer the last user after all? Alternatively, somebody
	 * else could have killed it and marked it dead. Either way, we
	 * don't need to do anything else.
	 */
	if (dentry->d_lockref.count) {
		spin_unlock(&dentry->d_lock);
		return 1;
	}

	/*
	 * Re-get the reference we optimistically dropped. We hold the
	 * lock, and we just tested that it was zero, so we can just
	 * set it to 1.
	 */
	dentry->d_lockref.count = 1;
	return 0;
}


/* 
 * This is dput
 *
 * This is complicated by the fact that we do not want to put
 * dentries that are no longer on any hash chain on the unused
 * list: we'd much rather just get rid of them immediately.
 *
 * However, that implies that we have to traverse the dentry
 * tree upwards to the parents which might _also_ now be
 * scheduled for deletion (it may have been only waiting for
 * its last child to go away).
 *
 * This tail recursion is done by hand as we don't want to depend
 * on the compiler to always get this right (gcc generally doesn't).
 * Real recursion would eat up our stack space.
 */

/*
 * dput - release a dentry
 * @dentry: dentry to release 
 *
 * Release a dentry. This will drop the usage count and if appropriate
 * call the dentry unlink method as well as removing it from the queues and
 * releasing its resources. If the parent dentries were scheduled for release
 * they too may now get deleted.
 */
void dput(struct dentry *dentry)
{
	if (unlikely(!dentry))
		return;

repeat:
	might_sleep();

	rcu_read_lock();
	if (likely(fast_dput(dentry))) {
		rcu_read_unlock();
		return;
	}

	/* Slow case: now with the dentry lock held */
	rcu_read_unlock();

	/* Unreachable? Get rid of it */
	if (unlikely(d_unhashed(dentry)))
		goto kill_it;

	if (unlikely(dentry->d_flags & DCACHE_DISCONNECTED))
		goto kill_it;

	if (unlikely(dentry->d_flags & DCACHE_OP_DELETE)) {
		if (dentry->d_op->d_delete(dentry))
			goto kill_it;
	}

	if (!(dentry->d_flags & DCACHE_REFERENCED))
		dentry->d_flags |= DCACHE_REFERENCED;
	dentry_lru_add(dentry);

	dentry->d_lockref.count--;
	spin_unlock(&dentry->d_lock);
	return;

kill_it:
	dentry = dentry_kill(dentry);
	if (dentry) {
		cond_resched();
		goto repeat;
	}
}
EXPORT_SYMBOL(dput);


/* This must be called with d_lock held */
static inline void __dget_dlock(struct dentry *dentry)
{
	dentry->d_lockref.count++;
}

static inline void __dget(struct dentry *dentry)
{
	lockref_get(&dentry->d_lockref);
}

struct dentry *dget_parent(struct dentry *dentry)
{
	int gotref;
	struct dentry *ret;

	/*
	 * Do optimistic parent lookup without any
	 * locking.
	 */
	rcu_read_lock();
	ret = ACCESS_ONCE(dentry->d_parent);
	gotref = lockref_get_not_zero(&ret->d_lockref);
	rcu_read_unlock();
	if (likely(gotref)) {
		if (likely(ret == ACCESS_ONCE(dentry->d_parent)))
			return ret;
		dput(ret);
	}

repeat:
	/*
	 * Don't need rcu_dereference because we re-check it was correct under
	 * the lock.
	 */
	rcu_read_lock();
	ret = dentry->d_parent;
	spin_lock(&ret->d_lock);
	if (unlikely(ret != dentry->d_parent)) {
		spin_unlock(&ret->d_lock);
		rcu_read_unlock();
		goto repeat;
	}
	rcu_read_unlock();
	BUG_ON(!ret->d_lockref.count);
	ret->d_lockref.count++;
	spin_unlock(&ret->d_lock);
	return ret;
}
EXPORT_SYMBOL(dget_parent);

/**
 * d_find_alias - grab a hashed alias of inode
 * @inode: inode in question
 *
 * If inode has a hashed alias, or is a directory and has any alias,
 * acquire the reference to alias and return it. Otherwise return NULL.
 * Notice that if inode is a directory there can be only one alias and
 * it can be unhashed only if it has no children, or if it is the root
 * of a filesystem, or if the directory was renamed and d_revalidate
 * was the first vfs operation to notice.
 *
 * If the inode has an IS_ROOT, DCACHE_DISCONNECTED alias, then prefer
 * any other hashed alias over that one.
 */
static struct dentry *__d_find_alias(struct inode *inode)
{
	struct dentry *alias, *discon_alias;

again:
	discon_alias = NULL;
	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&alias->d_lock);
 		if (S_ISDIR(inode->i_mode) || !d_unhashed(alias)) {
			if (IS_ROOT(alias) &&
			    (alias->d_flags & DCACHE_DISCONNECTED)) {
				discon_alias = alias;
			} else {
				__dget_dlock(alias);
				spin_unlock(&alias->d_lock);
				return alias;
			}
		}
		spin_unlock(&alias->d_lock);
	}
	if (discon_alias) {
		alias = discon_alias;
		spin_lock(&alias->d_lock);
		if (S_ISDIR(inode->i_mode) || !d_unhashed(alias)) {
			__dget_dlock(alias);
			spin_unlock(&alias->d_lock);
			return alias;
		}
		spin_unlock(&alias->d_lock);
		goto again;
	}
	return NULL;
}

struct dentry *d_find_alias(struct inode *inode)
{
	struct dentry *de = NULL;

	if (!hlist_empty(&inode->i_dentry)) {
		spin_lock(&inode->i_lock);
		de = __d_find_alias(inode);
		spin_unlock(&inode->i_lock);
	}
	return de;
}
EXPORT_SYMBOL(d_find_alias);

/*
 *	Try to kill dentries associated with this inode.
 * WARNING: you must own a reference to inode.
 */
void d_prune_aliases(struct inode *inode)
{
	struct dentry *dentry;
restart:
	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&dentry->d_lock);
		if (!dentry->d_lockref.count) {
			struct dentry *parent = lock_parent(dentry);
			if (likely(!dentry->d_lockref.count)) {
				__dentry_kill(dentry);
				dput(parent);
				goto restart;
			}
			if (parent)
				spin_unlock(&parent->d_lock);
		}
		spin_unlock(&dentry->d_lock);
	}
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(d_prune_aliases);

static void shrink_dentry_list(struct list_head *list)
{
	struct dentry *dentry, *parent;

	while (!list_empty(list)) {
		struct inode *inode;
		dentry = list_entry(list->prev, struct dentry, d_lru);
		spin_lock(&dentry->d_lock);
		parent = lock_parent(dentry);

		/*
		 * The dispose list is isolated and dentries are not accounted
		 * to the LRU here, so we can simply remove it from the list
		 * here regardless of whether it is referenced or not.
		 */
		d_shrink_del(dentry);

		/*
		 * We found an inuse dentry which was not removed from
		 * the LRU because of laziness during lookup. Do not free it.
		 */
		if (dentry->d_lockref.count > 0) {
			spin_unlock(&dentry->d_lock);
			if (parent)
				spin_unlock(&parent->d_lock);
			continue;
		}


		if (unlikely(dentry->d_flags & DCACHE_DENTRY_KILLED)) {
			bool can_free = dentry->d_flags & DCACHE_MAY_FREE;
			spin_unlock(&dentry->d_lock);
			if (parent)
				spin_unlock(&parent->d_lock);
			if (can_free)
				dentry_free(dentry);
			continue;
		}

		inode = dentry->d_inode;
		if (inode && unlikely(!spin_trylock(&inode->i_lock))) {
			d_shrink_add(dentry, list);
			spin_unlock(&dentry->d_lock);
			if (parent)
				spin_unlock(&parent->d_lock);
			continue;
		}

		__dentry_kill(dentry);

		/*
		 * We need to prune ancestors too. This is necessary to prevent
		 * quadratic behavior of shrink_dcache_parent(), but is also
		 * expected to be beneficial in reducing dentry cache
		 * fragmentation.
		 */
		dentry = parent;
		while (dentry && !lockref_put_or_lock(&dentry->d_lockref)) {
			parent = lock_parent(dentry);
			if (dentry->d_lockref.count != 1) {
				dentry->d_lockref.count--;
				spin_unlock(&dentry->d_lock);
				if (parent)
					spin_unlock(&parent->d_lock);
				break;
			}
			inode = dentry->d_inode;	/* can't be NULL */
			if (unlikely(!spin_trylock(&inode->i_lock))) {
				spin_unlock(&dentry->d_lock);
				if (parent)
					spin_unlock(&parent->d_lock);
				cpu_relax();
				continue;
			}
			__dentry_kill(dentry);
			dentry = parent;
		}
	}
}

static enum lru_status dentry_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *freeable = arg;
	struct dentry	*dentry = container_of(item, struct dentry, d_lru);


	/*
	 * we are inverting the lru lock/dentry->d_lock here,
	 * so use a trylock. If we fail to get the lock, just skip
	 * it
	 */
	if (!spin_trylock(&dentry->d_lock))
		return LRU_SKIP;

	/*
	 * Referenced dentries are still in use. If they have active
	 * counts, just remove them from the LRU. Otherwise give them
	 * another pass through the LRU.
	 */
	if (dentry->d_lockref.count) {
		d_lru_isolate(lru, dentry);
		spin_unlock(&dentry->d_lock);
		return LRU_REMOVED;
	}

	if (dentry->d_flags & DCACHE_REFERENCED) {
		dentry->d_flags &= ~DCACHE_REFERENCED;
		spin_unlock(&dentry->d_lock);

		/*
		 * The list move itself will be made by the common LRU code. At
		 * this point, we've dropped the dentry->d_lock but keep the
		 * lru lock. This is safe to do, since every list movement is
		 * protected by the lru lock even if both locks are held.
		 *
		 * This is guaranteed by the fact that all LRU management
		 * functions are intermediated by the LRU API calls like
		 * list_lru_add and list_lru_del. List movement in this file
		 * only ever occur through this functions or through callbacks
		 * like this one, that are called from the LRU API.
		 *
		 * The only exceptions to this are functions like
		 * shrink_dentry_list, and code that first checks for the
		 * DCACHE_SHRINK_LIST flag.  Those are guaranteed to be
		 * operating only with stack provided lists after they are
		 * properly isolated from the main list.  It is thus, always a
		 * local access.
		 */
		return LRU_ROTATE;
	}

	d_lru_shrink_move(lru, dentry, freeable);
	spin_unlock(&dentry->d_lock);

	return LRU_REMOVED;
}

/**
 * prune_dcache_sb - shrink the dcache
 * @sb: superblock
 * @sc: shrink control, passed to list_lru_shrink_walk()
 *
 * Attempt to shrink the superblock dcache LRU by @sc->nr_to_scan entries. This
 * is done when we need more memory and called from the superblock shrinker
 * function.
 *
 * This function may fail to free any resources if all the dentries are in
 * use.
 */
long prune_dcache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_dentry_lru, sc,
				     dentry_lru_isolate, &dispose);
	shrink_dentry_list(&dispose);
	return freed;
}

static enum lru_status dentry_lru_isolate_shrink(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	struct list_head *freeable = arg;
	struct dentry	*dentry = container_of(item, struct dentry, d_lru);

	/*
	 * we are inverting the lru lock/dentry->d_lock here,
	 * so use a trylock. If we fail to get the lock, just skip
	 * it
	 */
	if (!spin_trylock(&dentry->d_lock))
		return LRU_SKIP;

	d_lru_shrink_move(lru, dentry, freeable);
	spin_unlock(&dentry->d_lock);

	return LRU_REMOVED;
}


/**
 * shrink_dcache_sb - shrink dcache for a superblock
 * @sb: superblock
 *
 * Shrink the dcache for the specified super block. This is used to free
 * the dcache before unmounting a file system.
 */
void shrink_dcache_sb(struct super_block *sb)
{
	long freed;

	do {
		LIST_HEAD(dispose);

		freed = list_lru_walk(&sb->s_dentry_lru,
			dentry_lru_isolate_shrink, &dispose, UINT_MAX);

		this_cpu_sub(nr_dentry_unused, freed);
		shrink_dentry_list(&dispose);
	} while (freed > 0);
}
EXPORT_SYMBOL(shrink_dcache_sb);

/**
 * enum d_walk_ret - action to talke during tree walk
 * @D_WALK_CONTINUE:	contrinue walk
 * @D_WALK_QUIT:	quit walk
 * @D_WALK_NORETRY:	quit when retry is needed
 * @D_WALK_SKIP:	skip this dentry and its children
 */
enum d_walk_ret {
	D_WALK_CONTINUE,
	D_WALK_QUIT,
	D_WALK_NORETRY,
	D_WALK_SKIP,
};

/**
 * d_walk - walk the dentry tree
 * @parent:	start of walk
 * @data:	data passed to @enter() and @finish()
 * @enter:	callback when first entering the dentry
 * @finish:	callback when successfully finished the walk
 *
 * The @enter() and @finish() callbacks are called with d_lock held.
 */
static void d_walk(struct dentry *parent, void *data,
		   enum d_walk_ret (*enter)(void *, struct dentry *),
		   void (*finish)(void *))
{
	struct dentry *this_parent;
	struct list_head *next;
	unsigned seq = 0;
	enum d_walk_ret ret;
	bool retry = true;

again:
	read_seqbegin_or_lock(&rename_lock, &seq);
	this_parent = parent;
	spin_lock(&this_parent->d_lock);

	ret = enter(data, this_parent);
	switch (ret) {
	case D_WALK_CONTINUE:
		break;
	case D_WALK_QUIT:
	case D_WALK_SKIP:
		goto out_unlock;
	case D_WALK_NORETRY:
		retry = false;
		break;
	}
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
		next = tmp->next;

		spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);

		ret = enter(data, dentry);
		switch (ret) {
		case D_WALK_CONTINUE:
			break;
		case D_WALK_QUIT:
			spin_unlock(&dentry->d_lock);
			goto out_unlock;
		case D_WALK_NORETRY:
			retry = false;
			break;
		case D_WALK_SKIP:
			spin_unlock(&dentry->d_lock);
			continue;
		}

		if (!list_empty(&dentry->d_subdirs)) {
			spin_unlock(&this_parent->d_lock);
			spin_release(&dentry->d_lock.dep_map, 1, _RET_IP_);
			this_parent = dentry;
			spin_acquire(&this_parent->d_lock.dep_map, 0, 1, _RET_IP_);
			goto repeat;
		}
		spin_unlock(&dentry->d_lock);
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	rcu_read_lock();
ascend:
	if (this_parent != parent) {
		struct dentry *child = this_parent;
		this_parent = child->d_parent;

		spin_unlock(&child->d_lock);
		spin_lock(&this_parent->d_lock);

		/* might go back up the wrong parent if we have had a rename. */
		if (need_seqretry(&rename_lock, seq))
			goto rename_retry;
		/* go into the first sibling still alive */
		do {
			next = child->d_child.next;
			if (next == &this_parent->d_subdirs)
				goto ascend;
			child = list_entry(next, struct dentry, d_child);
		} while (unlikely(child->d_flags & DCACHE_DENTRY_KILLED));
		rcu_read_unlock();
		goto resume;
	}
	if (need_seqretry(&rename_lock, seq))
		goto rename_retry;
	rcu_read_unlock();
	if (finish)
		finish(data);

out_unlock:
	spin_unlock(&this_parent->d_lock);
	done_seqretry(&rename_lock, seq);
	return;

rename_retry:
	spin_unlock(&this_parent->d_lock);
	rcu_read_unlock();
	BUG_ON(seq & 1);
	if (!retry)
		return;
	seq = 1;
	goto again;
}

/*
 * Search for at least 1 mount point in the dentry's subdirs.
 * We descend to the next level whenever the d_subdirs
 * list is non-empty and continue searching.
 */

static enum d_walk_ret check_mount(void *data, struct dentry *dentry)
{
	int *ret = data;
	if (d_mountpoint(dentry)) {
		*ret = 1;
		return D_WALK_QUIT;
	}
	return D_WALK_CONTINUE;
}

/**
 * have_submounts - check for mounts over a dentry
 * @parent: dentry to check.
 *
 * Return true if the parent or its subdirectories contain
 * a mount point
 */
int have_submounts(struct dentry *parent)
{
	int ret = 0;

	d_walk(parent, &ret, check_mount, NULL);

	return ret;
}
EXPORT_SYMBOL(have_submounts);

/*
 * Called by mount code to set a mountpoint and check if the mountpoint is
 * reachable (e.g. NFS can unhash a directory dentry and then the complete
 * subtree can become unreachable).
 *
 * Only one of d_invalidate() and d_set_mounted() must succeed.  For
 * this reason take rename_lock and d_lock on dentry and ancestors.
 */
int d_set_mounted(struct dentry *dentry)
{
	struct dentry *p;
	int ret = -ENOENT;
	write_seqlock(&rename_lock);
	for (p = dentry->d_parent; !IS_ROOT(p); p = p->d_parent) {
		/* Need exclusion wrt. d_invalidate() */
		spin_lock(&p->d_lock);
		if (unlikely(d_unhashed(p))) {
			spin_unlock(&p->d_lock);
			goto out;
		}
		spin_unlock(&p->d_lock);
	}
	spin_lock(&dentry->d_lock);
	if (!d_unlinked(dentry)) {
		ret = -EBUSY;
		if (!d_mountpoint(dentry)) {
			dentry->d_flags |= DCACHE_MOUNTED;
			ret = 0;
		}
	}
 	spin_unlock(&dentry->d_lock);
out:
	write_sequnlock(&rename_lock);
	return ret;
}

/*
 * Search the dentry child list of the specified parent,
 * and move any unused dentries to the end of the unused
 * list for prune_dcache(). We descend to the next level
 * whenever the d_subdirs list is non-empty and continue
 * searching.
 *
 * It returns zero iff there are no unused children,
 * otherwise  it returns the number of children moved to
 * the end of the unused list. This may not be the total
 * number of unused children, because select_parent can
 * drop the lock and return early due to latency
 * constraints.
 */

struct select_data {
	struct dentry *start;
	struct list_head dispose;
	int found;
};

static enum d_walk_ret select_collect(void *_data, struct dentry *dentry)
{
	struct select_data *data = _data;
	enum d_walk_ret ret = D_WALK_CONTINUE;

	if (data->start == dentry)
		goto out;

	if (dentry->d_flags & DCACHE_SHRINK_LIST) {
		data->found++;
	} else {
		if (dentry->d_flags & DCACHE_LRU_LIST)
			d_lru_del(dentry);
		if (!dentry->d_lockref.count) {
			d_shrink_add(dentry, &data->dispose);
			data->found++;
		}
	}
	/*
	 * We can return to the caller if we have found some (this
	 * ensures forward progress). We'll be coming back to find
	 * the rest.
	 */
	if (!list_empty(&data->dispose))
		ret = need_resched() ? D_WALK_QUIT : D_WALK_NORETRY;
out:
	return ret;
}

/**
 * shrink_dcache_parent - prune dcache
 * @parent: parent of entries to prune
 *
 * Prune the dcache to remove unused children of the parent dentry.
 */
void shrink_dcache_parent(struct dentry *parent)
{
	for (;;) {
		struct select_data data;

		INIT_LIST_HEAD(&data.dispose);
		data.start = parent;
		data.found = 0;

		d_walk(parent, &data, select_collect, NULL);
		if (!data.found)
			break;

		shrink_dentry_list(&data.dispose);
		cond_resched();
	}
}
EXPORT_SYMBOL(shrink_dcache_parent);

static enum d_walk_ret umount_check(void *_data, struct dentry *dentry)
{
	/* it has busy descendents; complain about those instead */
	if (!list_empty(&dentry->d_subdirs))
		return D_WALK_CONTINUE;

	/* root with refcount 1 is fine */
	if (dentry == _data && dentry->d_lockref.count == 1)
		return D_WALK_CONTINUE;

	printk(KERN_ERR "BUG: Dentry %p{i=%lx,n=%pd} "
			" still in use (%d) [unmount of %s %s]\n",
		       dentry,
		       dentry->d_inode ?
		       dentry->d_inode->i_ino : 0UL,
		       dentry,
		       dentry->d_lockref.count,
		       dentry->d_sb->s_type->name,
		       dentry->d_sb->s_id);
	WARN_ON(1);
	return D_WALK_CONTINUE;
}

static void do_one_tree(struct dentry *dentry)
{
	shrink_dcache_parent(dentry);
	d_walk(dentry, dentry, umount_check, NULL);
	d_drop(dentry);
	dput(dentry);
}

/*
 * destroy the dentries attached to a superblock on unmounting
 */
void shrink_dcache_for_umount(struct super_block *sb)
{
	struct dentry *dentry;

	WARN(down_read_trylock(&sb->s_umount), "s_umount should've been locked");

	dentry = sb->s_root;
	sb->s_root = NULL;
	do_one_tree(dentry);

	while (!hlist_bl_empty(&sb->s_anon)) {
		dentry = dget(hlist_bl_entry(hlist_bl_first(&sb->s_anon), struct dentry, d_hash));
		do_one_tree(dentry);
	}
}

struct detach_data {
	struct select_data select;
	struct dentry *mountpoint;
};
static enum d_walk_ret detach_and_collect(void *_data, struct dentry *dentry)
{
	struct detach_data *data = _data;

	if (d_mountpoint(dentry)) {
		__dget_dlock(dentry);
		data->mountpoint = dentry;
		return D_WALK_QUIT;
	}

	return select_collect(&data->select, dentry);
}

static void check_and_drop(void *_data)
{
	struct detach_data *data = _data;

	if (!data->mountpoint && !data->select.found)
		__d_drop(data->select.start);
}

/**
 * d_invalidate - detach submounts, prune dcache, and drop
 * @dentry: dentry to invalidate (aka detach, prune and drop)
 *
 * no dcache lock.
 *
 * The final d_drop is done as an atomic operation relative to
 * rename_lock ensuring there are no races with d_set_mounted.  This
 * ensures there are no unhashed dentries on the path to a mountpoint.
 */
void d_invalidate(struct dentry *dentry)
{
	/*
	 * If it's already been dropped, return OK.
	 */
	spin_lock(&dentry->d_lock);
	if (d_unhashed(dentry)) {
		spin_unlock(&dentry->d_lock);
		return;
	}
	spin_unlock(&dentry->d_lock);

	/* Negative dentries can be dropped without further checks */
	if (!dentry->d_inode) {
		d_drop(dentry);
		return;
	}

	for (;;) {
		struct detach_data data;

		data.mountpoint = NULL;
		INIT_LIST_HEAD(&data.select.dispose);
		data.select.start = dentry;
		data.select.found = 0;

		d_walk(dentry, &data, detach_and_collect, check_and_drop);

		if (data.select.found)
			shrink_dentry_list(&data.select.dispose);

		if (data.mountpoint) {
			detach_mounts(data.mountpoint);
			dput(data.mountpoint);
		}

		if (!data.mountpoint && !data.select.found)
			break;

		cond_resched();
	}
}
EXPORT_SYMBOL(d_invalidate);

/**
 * __d_alloc	-	allocate a dcache entry
 * @sb: filesystem it will belong to
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */
 
struct dentry *__d_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;

	dentry = kmem_cache_alloc(dentry_cache, GFP_KERNEL);
	if (!dentry)
		return NULL;

	/*
	 * We guarantee that the inline name is always NUL-terminated.
	 * This way the memcpy() done by the name switching in rename
	 * will still always have a NUL at the end, even if we might
	 * be overwriting an internal NUL character
	 */
	dentry->d_iname[DNAME_INLINE_LEN-1] = 0;
	if (name->len > DNAME_INLINE_LEN-1) {
		size_t size = offsetof(struct external_name, name[1]);
		struct external_name *p = kmalloc(size + name->len, GFP_KERNEL);
		if (!p) {
			kmem_cache_free(dentry_cache, dentry); 
			return NULL;
		}
		atomic_set(&p->u.count, 1);
		dname = p->name;
		if (IS_ENABLED(CONFIG_DCACHE_WORD_ACCESS))
			kasan_unpoison_shadow(dname,
				round_up(name->len + 1,	sizeof(unsigned long)));
	} else  {
		dname = dentry->d_iname;
	}	

	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	memcpy(dname, name->name, name->len);
	dname[name->len] = 0;

	/* Make sure we always see the terminating NUL character */
	smp_wmb();
	dentry->d_name.name = dname;

	dentry->d_lockref.count = 1;
	dentry->d_flags = 0;
	spin_lock_init(&dentry->d_lock);
	seqcount_init(&dentry->d_seq);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	dentry->d_op = NULL;
	dentry->d_fsdata = NULL;
	INIT_HLIST_BL_NODE(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_LIST_HEAD(&dentry->d_subdirs);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_LIST_HEAD(&dentry->d_child);
	d_set_d_op(dentry, dentry->d_sb->s_d_op);

	this_cpu_inc(nr_dentry);

	return dentry;
}

/**
 * d_alloc	-	allocate a dcache entry
 * @parent: parent of entry to allocate
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */
struct dentry *d_alloc(struct dentry * parent, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(parent->d_sb, name);
	if (!dentry)
		return NULL;
	dentry->d_flags |= DCACHE_RCUACCESS;
	spin_lock(&parent->d_lock);
	/*
	 * don't need child lock because it is not subject
	 * to concurrency here
	 */
	__dget_dlock(parent);
	dentry->d_parent = parent;
	list_add(&dentry->d_child, &parent->d_subdirs);
	spin_unlock(&parent->d_lock);

	return dentry;
}
EXPORT_SYMBOL(d_alloc);

/**
 * d_alloc_pseudo - allocate a dentry (for lookup-less filesystems)
 * @sb: the superblock
 * @name: qstr of the name
 *
 * For a filesystem that just pins its dentries in memory and never
 * performs lookups at all, return an unhashed IS_ROOT dentry.
 */
struct dentry *d_alloc_pseudo(struct super_block *sb, const struct qstr *name)
{
	return __d_alloc(sb, name);
}
EXPORT_SYMBOL(d_alloc_pseudo);

struct dentry *d_alloc_name(struct dentry *parent, const char *name)
{
	struct qstr q;

	q.name = name;
	q.len = strlen(name);
	q.hash = full_name_hash(q.name, q.len);
	return d_alloc(parent, &q);
}
EXPORT_SYMBOL(d_alloc_name);

void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
{
	WARN_ON_ONCE(dentry->d_op);
	WARN_ON_ONCE(dentry->d_flags & (DCACHE_OP_HASH	|
				DCACHE_OP_COMPARE	|
				DCACHE_OP_REVALIDATE	|
				DCACHE_OP_WEAK_REVALIDATE	|
				DCACHE_OP_DELETE	|
				DCACHE_OP_SELECT_INODE	|
				DCACHE_OP_REAL));
	dentry->d_op = op;
	if (!op)
		return;
	if (op->d_hash)
		dentry->d_flags |= DCACHE_OP_HASH;
	if (op->d_compare)
		dentry->d_flags |= DCACHE_OP_COMPARE;
	if (op->d_revalidate)
		dentry->d_flags |= DCACHE_OP_REVALIDATE;
	if (op->d_weak_revalidate)
		dentry->d_flags |= DCACHE_OP_WEAK_REVALIDATE;
	if (op->d_delete)
		dentry->d_flags |= DCACHE_OP_DELETE;
	if (op->d_prune)
		dentry->d_flags |= DCACHE_OP_PRUNE;
	if (op->d_select_inode)
		dentry->d_flags |= DCACHE_OP_SELECT_INODE;
	if (op->d_real)
		dentry->d_flags |= DCACHE_OP_REAL;

}
EXPORT_SYMBOL(d_set_d_op);


/*
 * d_set_fallthru - Mark a dentry as falling through to a lower layer
 * @dentry - The dentry to mark
 *
 * Mark a dentry as falling through to the lower layer (as set with
 * d_pin_lower()).  This flag may be recorded on the medium.
 */
void d_set_fallthru(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_FALLTHRU;
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(d_set_fallthru);

static unsigned d_flags_for_inode(struct inode *inode)
{
	unsigned add_flags = DCACHE_REGULAR_TYPE;

	if (!inode)
		return DCACHE_MISS_TYPE;

	if (S_ISDIR(inode->i_mode)) {
		add_flags = DCACHE_DIRECTORY_TYPE;
		if (unlikely(!(inode->i_opflags & IOP_LOOKUP))) {
			if (unlikely(!inode->i_op->lookup))
				add_flags = DCACHE_AUTODIR_TYPE;
			else
				inode->i_opflags |= IOP_LOOKUP;
		}
		goto type_determined;
	}

	if (unlikely(!(inode->i_opflags & IOP_NOFOLLOW))) {
		if (unlikely(inode->i_op->follow_link)) {
			add_flags = DCACHE_SYMLINK_TYPE;
			goto type_determined;
		}
		inode->i_opflags |= IOP_NOFOLLOW;
	}

	if (unlikely(!S_ISREG(inode->i_mode)))
		add_flags = DCACHE_SPECIAL_TYPE;

type_determined:
	if (unlikely(IS_AUTOMOUNT(inode)))
		add_flags |= DCACHE_NEED_AUTOMOUNT;
	return add_flags;
}

static void __d_instantiate(struct dentry *dentry, struct inode *inode)
{
	unsigned add_flags = d_flags_for_inode(inode);

	spin_lock(&dentry->d_lock);
	if (inode)
		hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
	raw_write_seqcount_begin(&dentry->d_seq);
	__d_set_inode_and_type(dentry, inode, add_flags);
	raw_write_seqcount_end(&dentry->d_seq);
	spin_unlock(&dentry->d_lock);
	fsnotify_d_instantiate(dentry, inode);
}

/**
 * d_instantiate - fill in inode information for a dentry
 * @entry: dentry to complete
 * @inode: inode to attach to this dentry
 *
 * Fill in inode information in the entry.
 *
 * This turns negative dentries into productive full members
 * of society.
 *
 * NOTE! This assumes that the inode count has been incremented
 * (or otherwise set) by the caller to indicate that it is now
 * in use by the dcache.
 */
 
void d_instantiate(struct dentry *entry, struct inode * inode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));
	if (inode)
		spin_lock(&inode->i_lock);
	__d_instantiate(entry, inode);
	if (inode)
		spin_unlock(&inode->i_lock);
	security_d_instantiate(entry, inode);
}
EXPORT_SYMBOL(d_instantiate);

/**
 * d_instantiate_unique - instantiate a non-aliased dentry
 * @entry: dentry to instantiate
 * @inode: inode to attach to this dentry
 *
 * Fill in inode information in the entry. On success, it returns NULL.
 * If an unhashed alias of "entry" already exists, then we return the
 * aliased dentry instead and drop one reference to inode.
 *
 * Note that in order to avoid conflicts with rename() etc, the caller
 * had better be holding the parent directory semaphore.
 *
 * This also assumes that the inode count has been incremented
 * (or otherwise set) by the caller to indicate that it is now
 * in use by the dcache.
 */
static struct dentry *__d_instantiate_unique(struct dentry *entry,
					     struct inode *inode)
{
	struct dentry *alias;
	int len = entry->d_name.len;
	const char *name = entry->d_name.name;
	unsigned int hash = entry->d_name.hash;

	if (!inode) {
		__d_instantiate(entry, NULL);
		return NULL;
	}

	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		/*
		 * Don't need alias->d_lock here, because aliases with
		 * d_parent == entry->d_parent are not subject to name or
		 * parent changes, because the parent inode i_mutex is held.
		 */
		if (alias->d_name.hash != hash)
			continue;
		if (alias->d_parent != entry->d_parent)
			continue;
		if (alias->d_name.len != len)
			continue;
		if (dentry_cmp(alias, name, len))
			continue;
		__dget(alias);
		return alias;
	}

	__d_instantiate(entry, inode);
	return NULL;
}

struct dentry *d_instantiate_unique(struct dentry *entry, struct inode *inode)
{
	struct dentry *result;

	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));

	if (inode)
		spin_lock(&inode->i_lock);
	result = __d_instantiate_unique(entry, inode);
	if (inode)
		spin_unlock(&inode->i_lock);

	if (!result) {
		security_d_instantiate(entry, inode);
		return NULL;
	}

	BUG_ON(!d_unhashed(result));
	iput(inode);
	return result;
}

EXPORT_SYMBOL(d_instantiate_unique);

/**
 * d_instantiate_no_diralias - instantiate a non-aliased dentry
 * @entry: dentry to complete
 * @inode: inode to attach to this dentry
 *
 * Fill in inode information in the entry.  If a directory alias is found, then
 * return an error (and drop inode).  Together with d_materialise_unique() this
 * guarantees that a directory inode may never have more than one alias.
 */
int d_instantiate_no_diralias(struct dentry *entry, struct inode *inode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));

	spin_lock(&inode->i_lock);
	if (S_ISDIR(inode->i_mode) && !hlist_empty(&inode->i_dentry)) {
		spin_unlock(&inode->i_lock);
		iput(inode);
		return -EBUSY;
	}
	__d_instantiate(entry, inode);
	spin_unlock(&inode->i_lock);
	security_d_instantiate(entry, inode);

	return 0;
}
EXPORT_SYMBOL(d_instantiate_no_diralias);

struct dentry *d_make_root(struct inode *root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		static const struct qstr name = QSTR_INIT("/", 1);

		res = __d_alloc(root_inode->i_sb, &name);
		if (res)
			d_instantiate(res, root_inode);
		else
			iput(root_inode);
	}
	return res;
}
EXPORT_SYMBOL(d_make_root);

static struct dentry * __d_find_any_alias(struct inode *inode)
{
	struct dentry *alias;

	if (hlist_empty(&inode->i_dentry))
		return NULL;
	alias = hlist_entry(inode->i_dentry.first, struct dentry, d_u.d_alias);
	__dget(alias);
	return alias;
}

/**
 * d_find_any_alias - find any alias for a given inode
 * @inode: inode to find an alias for
 *
 * If any aliases exist for the given inode, take and return a
 * reference for one of them.  If no aliases exist, return %NULL.
 */
struct dentry *d_find_any_alias(struct inode *inode)
{
	struct dentry *de;

	spin_lock(&inode->i_lock);
	de = __d_find_any_alias(inode);
	spin_unlock(&inode->i_lock);
	return de;
}
EXPORT_SYMBOL(d_find_any_alias);

static struct dentry *__d_obtain_alias(struct inode *inode, int disconnected)
{
	static const struct qstr anonstring = QSTR_INIT("/", 1);
	struct dentry *tmp;
	struct dentry *res;
	unsigned add_flags;

	if (!inode)
		return ERR_PTR(-ESTALE);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	res = d_find_any_alias(inode);
	if (res)
		goto out_iput;

	tmp = __d_alloc(inode->i_sb, &anonstring);
	if (!tmp) {
		res = ERR_PTR(-ENOMEM);
		goto out_iput;
	}

	spin_lock(&inode->i_lock);
	res = __d_find_any_alias(inode);
	if (res) {
		spin_unlock(&inode->i_lock);
		dput(tmp);
		goto out_iput;
	}

	/* attach a disconnected dentry */
	add_flags = d_flags_for_inode(inode);

	if (disconnected)
		add_flags |= DCACHE_DISCONNECTED;

	spin_lock(&tmp->d_lock);
	__d_set_inode_and_type(tmp, inode, add_flags);
	hlist_add_head(&tmp->d_u.d_alias, &inode->i_dentry);
	hlist_bl_lock(&tmp->d_sb->s_anon);
	hlist_bl_add_head(&tmp->d_hash, &tmp->d_sb->s_anon);
	hlist_bl_unlock(&tmp->d_sb->s_anon);
	spin_unlock(&tmp->d_lock);
	spin_unlock(&inode->i_lock);
	security_d_instantiate(tmp, inode);

	return tmp;

 out_iput:
	if (res && !IS_ERR(res))
		security_d_instantiate(res, inode);
	iput(inode);
	return res;
}

/**
 * d_obtain_alias - find or allocate a DISCONNECTED dentry for a given inode
 * @inode: inode to allocate the dentry for
 *
 * Obtain a dentry for an inode resulting from NFS filehandle conversion or
 * similar open by handle operations.  The returned dentry may be anonymous,
 * or may have a full name (if the inode was already in the cache).
 *
 * When called on a directory inode, we must ensure that the inode only ever
 * has one dentry.  If a dentry is found, that is returned instead of
 * allocating a new one.
 *
 * On successful return, the reference to the inode has been transferred
 * to the dentry.  In case of an error the reference on the inode is released.
 * To make it easier to use in export operations a %NULL or IS_ERR inode may
 * be passed in and the error will be propagated to the return value,
 * with a %NULL @inode replaced by ERR_PTR(-ESTALE).
 */
struct dentry *d_obtain_alias(struct inode *inode)
{
	return __d_obtain_alias(inode, 1);
}
EXPORT_SYMBOL(d_obtain_alias);

/**
 * d_obtain_root - find or allocate a dentry for a given inode
 * @inode: inode to allocate the dentry for
 *
 * Obtain an IS_ROOT dentry for the root of a filesystem.
 *
 * We must ensure that directory inodes only ever have one dentry.  If a
 * dentry is found, that is returned instead of allocating a new one.
 *
 * On successful return, the reference to the inode has been transferred
 * to the dentry.  In case of an error the reference on the inode is
 * released.  A %NULL or IS_ERR inode may be passed in and will be the
 * error will be propagate to the return value, with a %NULL @inode
 * replaced by ERR_PTR(-ESTALE).
 */
struct dentry *d_obtain_root(struct inode *inode)
{
	return __d_obtain_alias(inode, 0);
}
EXPORT_SYMBOL(d_obtain_root);

/**
 * d_add_ci - lookup or allocate new dentry with case-exact name
 * @inode:  the inode case-insensitive lookup has found
 * @dentry: the negative dentry that was passed to the parent's lookup func
 * @name:   the case-exact name to be associated with the returned dentry
 *
 * This is to avoid filling the dcache with case-insensitive names to the
 * same inode, only the actual correct case is stored in the dcache for
 * case-insensitive filesystems.
 *
 * For a case-insensitive lookup match and if the the case-exact dentry
 * already exists in in the dcache, use it and return it.
 *
 * If no entry exists with the exact case name, allocate new dentry with
 * the exact case, and return the spliced entry.
 */
struct dentry *d_add_ci(struct dentry *dentry, struct inode *inode,
			struct qstr *name)
{
	struct dentry *found;
	struct dentry *new;

	/*
	 * First check if a dentry matching the name already exists,
	 * if not go ahead and create it now.
	 */
	found = d_hash_and_lookup(dentry->d_parent, name);
	if (!found) {
		new = d_alloc(dentry->d_parent, name);
		if (!new) {
			found = ERR_PTR(-ENOMEM);
		} else {
			found = d_splice_alias(inode, new);
			if (found) {
				dput(new);
				return found;
			}
			return new;
		}
	}
	iput(inode);
	return found;
}
EXPORT_SYMBOL(d_add_ci);

/*
 * Do the slow-case of the dentry name compare.
 *
 * Unlike the dentry_cmp() function, we need to atomically
 * load the name and length information, so that the
 * filesystem can rely on them, and can use the 'name' and
 * 'len' information without worrying about walking off the
 * end of memory etc.
 *
 * Thus the read_seqcount_retry() and the "duplicate" info
 * in arguments (the low-level filesystem should not look
 * at the dentry inode or name contents directly, since
 * rename can change them while we're in RCU mode).
 */
enum slow_d_compare {
	D_COMP_OK,
	D_COMP_NOMATCH,
	D_COMP_SEQRETRY,
};

static noinline enum slow_d_compare slow_dentry_cmp(
		const struct dentry *parent,
		struct dentry *dentry,
		unsigned int seq,
		const struct qstr *name)
{
	int tlen = dentry->d_name.len;
	const char *tname = dentry->d_name.name;

	if (read_seqcount_retry(&dentry->d_seq, seq)) {
		cpu_relax();
		return D_COMP_SEQRETRY;
	}
	if (parent->d_op->d_compare(parent, dentry, tlen, tname, name))
		return D_COMP_NOMATCH;
	return D_COMP_OK;
}

/**
 * __d_lookup_rcu - search for a dentry (racy, store-free)
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * @seqp: returns d_seq value at the point where the dentry was found
 * Returns: dentry, or NULL
 *
 * __d_lookup_rcu is the dcache lookup function for rcu-walk name
 * resolution (store-free path walking) design described in
 * Documentation/filesystems/path-lookup.txt.
 *
 * This is not to be used outside core vfs.
 *
 * __d_lookup_rcu must only be used in rcu-walk mode, ie. with vfsmount lock
 * held, and rcu_read_lock held. The returned dentry must not be stored into
 * without taking d_lock and checking d_seq sequence count against @seq
 * returned here.
 *
 * A refcount may be taken on the found dentry with the d_rcu_to_refcount
 * function.
 *
 * Alternatively, __d_lookup_rcu may be called again to look up the child of
 * the returned dentry, so long as its parent's seqlock is checked after the
 * child is looked up. Thus, an interlocking stepping of sequence lock checks
 * is formed, giving integrity down the path walk.
 *
 * NOTE! The caller *has* to check the resulting dentry against the sequence
 * number we've returned before using any of the resulting dentry state!
 */
struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name,
				unsigned *seqp)
{
	u64 hashlen = name->hash_len;
	const unsigned char *str = name->name;
	struct hlist_bl_head *b = d_hash(parent, hashlen_hash(hashlen));
	struct hlist_bl_node *node;
	struct dentry *dentry;

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	/*
	 * The hash list is protected using RCU.
	 *
	 * Carefully use d_seq when comparing a candidate dentry, to avoid
	 * races with d_move().
	 *
	 * It is possible that concurrent renames can mess up our list
	 * walk here and result in missing our dentry, resulting in the
	 * false-negative result. d_lookup() protects against concurrent
	 * renames using rename_lock seqlock.
	 *
	 * See Documentation/filesystems/path-lookup.txt for more details.
	 */
	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		unsigned seq;

seqretry:
		/*
		 * The dentry sequence count protects us from concurrent
		 * renames, and thus protects parent and name fields.
		 *
		 * The caller must perform a seqcount check in order
		 * to do anything useful with the returned dentry.
		 *
		 * NOTE! We do a "raw" seqcount_begin here. That means that
		 * we don't wait for the sequence count to stabilize if it
		 * is in the middle of a sequence change. If we do the slow
		 * dentry compare, we will do seqretries until it is stable,
		 * and if we end up with a successful lookup, we actually
		 * want to exit RCU lookup anyway.
		 */
		seq = raw_seqcount_begin(&dentry->d_seq);
		if (dentry->d_parent != parent)
			continue;
		if (d_unhashed(dentry))
			continue;

		if (unlikely(parent->d_flags & DCACHE_OP_COMPARE)) {
			if (dentry->d_name.hash != hashlen_hash(hashlen))
				continue;
			*seqp = seq;
			switch (slow_dentry_cmp(parent, dentry, seq, name)) {
			case D_COMP_OK:
				return dentry;
			case D_COMP_NOMATCH:
				continue;
			default:
				goto seqretry;
			}
		}

		if (dentry->d_name.hash_len != hashlen)
			continue;
		*seqp = seq;
		if (!dentry_cmp(dentry, str, hashlen_len(hashlen)))
			return dentry;
	}
	return NULL;
}

/**
 * d_lookup - search for a dentry
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * Returns: dentry, or NULL
 *
 * d_lookup searches the children of the parent dentry for the name in
 * question. If the dentry is found its reference count is incremented and the
 * dentry is returned. The caller must use dput to free the entry when it has
 * finished using it. %NULL is returned if the dentry does not exist.
 */
struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry;
	unsigned seq;

	do {
		seq = read_seqbegin(&rename_lock);
		dentry = __d_lookup(parent, name);
		if (dentry)
			break;
	} while (read_seqretry(&rename_lock, seq));
	return dentry;
}
EXPORT_SYMBOL(d_lookup);

/**
 * __d_lookup - search for a dentry (racy)
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * Returns: dentry, or NULL
 *
 * __d_lookup is like d_lookup, however it may (rarely) return a
 * false-negative result due to unrelated rename activity.
 *
 * __d_lookup is slightly faster by avoiding rename_lock read seqlock,
 * however it must be used carefully, eg. with a following d_lookup in
 * the case of failure.
 *
 * __d_lookup callers must be commented.
 */
struct dentry *__d_lookup(const struct dentry *parent, const struct qstr *name)
{
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct hlist_bl_head *b = d_hash(parent, hash);
	struct hlist_bl_node *node;
	struct dentry *found = NULL;
	struct dentry *dentry;

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	/*
	 * The hash list is protected using RCU.
	 *
	 * Take d_lock when comparing a candidate dentry, to avoid races
	 * with d_move().
	 *
	 * It is possible that concurrent renames can mess up our list
	 * walk here and result in missing our dentry, resulting in the
	 * false-negative result. d_lookup() protects against concurrent
	 * renames using rename_lock seqlock.
	 *
	 * See Documentation/filesystems/path-lookup.txt for more details.
	 */
	rcu_read_lock();
	
	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {

		if (dentry->d_name.hash != hash)
			continue;

		spin_lock(&dentry->d_lock);
		if (dentry->d_parent != parent)
			goto next;
		if (d_unhashed(dentry))
			goto next;

		/*
		 * It is safe to compare names since d_move() cannot
		 * change the qstr (protected by d_lock).
		 */
		if (parent->d_flags & DCACHE_OP_COMPARE) {
			int tlen = dentry->d_name.len;
			const char *tname = dentry->d_name.name;
			if (parent->d_op->d_compare(parent, dentry, tlen, tname, name))
				goto next;
		} else {
			if (dentry->d_name.len != len)
				goto next;
			if (dentry_cmp(dentry, str, len))
				goto next;
		}

		dentry->d_lockref.count++;
		found = dentry;
		spin_unlock(&dentry->d_lock);
		break;
next:
		spin_unlock(&dentry->d_lock);
 	}
 	rcu_read_unlock();

 	return found;
}

/**
 * d_hash_and_lookup - hash the qstr then search for a dentry
 * @dir: Directory to search in
 * @name: qstr of name we wish to find
 *
 * On lookup failure NULL is returned; on bad name - ERR_PTR(-error)
 */
struct dentry *d_hash_and_lookup(struct dentry *dir, struct qstr *name)
{
	/*
	 * Check for a fs-specific hash function. Note that we must
	 * calculate the standard hash first, as the d_op->d_hash()
	 * routine may choose to leave the hash value unchanged.
	 */
	name->hash = full_name_hash(name->name, name->len);
	if (dir->d_flags & DCACHE_OP_HASH) {
		int err = dir->d_op->d_hash(dir, name);
		if (unlikely(err < 0))
			return ERR_PTR(err);
	}
	return d_lookup(dir, name);
}
EXPORT_SYMBOL(d_hash_and_lookup);

/*
 * When a file is deleted, we have two options:
 * - turn this dentry into a negative dentry
 * - unhash this dentry and free it.
 *
 * Usually, we want to just turn this into
 * a negative dentry, but if anybody else is
 * currently using the dentry or the inode
 * we can't do that and we fall back on removing
 * it from the hash queues and waiting for
 * it to be deleted later when it has no users
 */
 
/**
 * d_delete - delete a dentry
 * @dentry: The dentry to delete
 *
 * Turn the dentry into a negative dentry if possible, otherwise
 * remove it from the hash queues so it can be deleted later
 */
 
void d_delete(struct dentry * dentry)
{
	struct inode *inode;
	int isdir = 0;
	/*
	 * Are we the only user?
	 */
again:
	spin_lock(&dentry->d_lock);
	inode = dentry->d_inode;
	isdir = S_ISDIR(inode->i_mode);
	if (dentry->d_lockref.count == 1) {
		if (!spin_trylock(&inode->i_lock)) {
			spin_unlock(&dentry->d_lock);
			cpu_relax();
			goto again;
		}
		dentry->d_flags &= ~DCACHE_CANT_MOUNT;
		dentry_unlink_inode(dentry);
		fsnotify_nameremove(dentry, isdir);
		return;
	}

	if (!d_unhashed(dentry))
		__d_drop(dentry);

	spin_unlock(&dentry->d_lock);

	fsnotify_nameremove(dentry, isdir);
}
EXPORT_SYMBOL(d_delete);

static void __d_rehash(struct dentry * entry, struct hlist_bl_head *b)
{
	BUG_ON(!d_unhashed(entry));
	hlist_bl_lock(b);
	hlist_bl_add_head_rcu(&entry->d_hash, b);
	hlist_bl_unlock(b);
}

static void _d_rehash(struct dentry * entry)
{
	__d_rehash(entry, d_hash(entry->d_parent, entry->d_name.hash));
}

/**
 * d_rehash	- add an entry back to the hash
 * @entry: dentry to add to the hash
 *
 * Adds a dentry to the hash according to its name.
 */
 
void d_rehash(struct dentry * entry)
{
	spin_lock(&entry->d_lock);
	_d_rehash(entry);
	spin_unlock(&entry->d_lock);
}
EXPORT_SYMBOL(d_rehash);

/**
 * dentry_update_name_case - update case insensitive dentry with a new name
 * @dentry: dentry to be updated
 * @name: new name
 *
 * Update a case insensitive dentry with new case of name.
 *
 * dentry must have been returned by d_lookup with name @name. Old and new
 * name lengths must match (ie. no d_compare which allows mismatched name
 * lengths).
 *
 * Parent inode i_mutex must be held over d_lookup and into this call (to
 * keep renames and concurrent inserts, and readdir(2) away).
 */
void dentry_update_name_case(struct dentry *dentry, struct qstr *name)
{
	BUG_ON(!mutex_is_locked(&dentry->d_parent->d_inode->i_mutex));
	BUG_ON(dentry->d_name.len != name->len); /* d_lookup gives this */

	spin_lock(&dentry->d_lock);
	write_seqcount_begin(&dentry->d_seq);
	memcpy((unsigned char *)dentry->d_name.name, name->name, name->len);
	write_seqcount_end(&dentry->d_seq);
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(dentry_update_name_case);

static void swap_names(struct dentry *dentry, struct dentry *target)
{
	if (unlikely(dname_external(target))) {
		if (unlikely(dname_external(dentry))) {
			/*
			 * Both external: swap the pointers
			 */
			swap(target->d_name.name, dentry->d_name.name);
		} else {
			/*
			 * dentry:internal, target:external.  Steal target's
			 * storage and make target internal.
			 */
			memcpy(target->d_iname, dentry->d_name.name,
					dentry->d_name.len + 1);
			dentry->d_name.name = target->d_name.name;
			target->d_name.name = target->d_iname;
		}
	} else {
		if (unlikely(dname_external(dentry))) {
			/*
			 * dentry:external, target:internal.  Give dentry's
			 * storage to target and make dentry internal
			 */
			memcpy(dentry->d_iname, target->d_name.name,
					target->d_name.len + 1);
			target->d_name.name = dentry->d_name.name;
			dentry->d_name.name = dentry->d_iname;
		} else {
			/*
			 * Both are internal.
			 */
			unsigned int i;
			BUILD_BUG_ON(!IS_ALIGNED(DNAME_INLINE_LEN, sizeof(long)));
			kmemcheck_mark_initialized(dentry->d_iname, DNAME_INLINE_LEN);
			kmemcheck_mark_initialized(target->d_iname, DNAME_INLINE_LEN);
			for (i = 0; i < DNAME_INLINE_LEN / sizeof(long); i++) {
				swap(((long *) &dentry->d_iname)[i],
				     ((long *) &target->d_iname)[i]);
			}
		}
	}
	swap(dentry->d_name.hash_len, target->d_name.hash_len);
}

static void copy_name(struct dentry *dentry, struct dentry *target)
{
	struct external_name *old_name = NULL;
	if (unlikely(dname_external(dentry)))
		old_name = external_name(dentry);
	if (unlikely(dname_external(target))) {
		atomic_inc(&external_name(target)->u.count);
		dentry->d_name = target->d_name;
	} else {
		memcpy(dentry->d_iname, target->d_name.name,
				target->d_name.len + 1);
		dentry->d_name.name = dentry->d_iname;
		dentry->d_name.hash_len = target->d_name.hash_len;
	}
	if (old_name && likely(atomic_dec_and_test(&old_name->u.count)))
		kfree_rcu(old_name, u.head);
}

static void dentry_lock_for_move(struct dentry *dentry, struct dentry *target)
{
	/*
	 * XXXX: do we really need to take target->d_lock?
	 */
	if (IS_ROOT(dentry) || dentry->d_parent == target->d_parent)
		spin_lock(&target->d_parent->d_lock);
	else {
		if (d_ancestor(dentry->d_parent, target->d_parent)) {
			spin_lock(&dentry->d_parent->d_lock);
			spin_lock_nested(&target->d_parent->d_lock,
						DENTRY_D_LOCK_NESTED);
		} else {
			spin_lock(&target->d_parent->d_lock);
			spin_lock_nested(&dentry->d_parent->d_lock,
						DENTRY_D_LOCK_NESTED);
		}
	}
	if (target < dentry) {
		spin_lock_nested(&target->d_lock, 2);
		spin_lock_nested(&dentry->d_lock, 3);
	} else {
		spin_lock_nested(&dentry->d_lock, 2);
		spin_lock_nested(&target->d_lock, 3);
	}
}

static void dentry_unlock_for_move(struct dentry *dentry, struct dentry *target)
{
	if (target->d_parent != dentry->d_parent)
		spin_unlock(&dentry->d_parent->d_lock);
	if (target->d_parent != target)
		spin_unlock(&target->d_parent->d_lock);
	spin_unlock(&target->d_lock);
	spin_unlock(&dentry->d_lock);
}

/*
 * When switching names, the actual string doesn't strictly have to
 * be preserved in the target - because we're dropping the target
 * anyway. As such, we can just do a simple memcpy() to copy over
 * the new name before we switch, unless we are going to rehash
 * it.  Note that if we *do* unhash the target, we are not allowed
 * to rehash it without giving it a new name/hash key - whether
 * we swap or overwrite the names here, resulting name won't match
 * the reality in filesystem; it's only there for d_path() purposes.
 * Note that all of this is happening under rename_lock, so the
 * any hash lookup seeing it in the middle of manipulations will
 * be discarded anyway.  So we do not care what happens to the hash
 * key in that case.
 */
/*
 * __d_move - move a dentry
 * @dentry: entry to move
 * @target: new dentry
 * @exchange: exchange the two dentries
 *
 * Update the dcache to reflect the move of a file name. Negative
 * dcache entries should not be moved in this way. Caller must hold
 * rename_lock, the i_mutex of the source and target directories,
 * and the sb->s_vfs_rename_mutex if they differ. See lock_rename().
 */
static void __d_move(struct dentry *dentry, struct dentry *target,
		     bool exchange)
{
	if (!dentry->d_inode)
		printk(KERN_WARNING "VFS: moving negative dcache entry\n");

	BUG_ON(d_ancestor(dentry, target));
	BUG_ON(d_ancestor(target, dentry));

	dentry_lock_for_move(dentry, target);

	write_seqcount_begin(&dentry->d_seq);
	write_seqcount_begin_nested(&target->d_seq, DENTRY_D_LOCK_NESTED);

	/* __d_drop does write_seqcount_barrier, but they're OK to nest. */

	/*
	 * Move the dentry to the target hash queue. Don't bother checking
	 * for the same hash queue because of how unlikely it is.
	 */
	__d_drop(dentry);
	__d_rehash(dentry, d_hash(target->d_parent, target->d_name.hash));

	/*
	 * Unhash the target (d_delete() is not usable here).  If exchanging
	 * the two dentries, then rehash onto the other's hash queue.
	 */
	__d_drop(target);
	if (exchange) {
		__d_rehash(target,
			   d_hash(dentry->d_parent, dentry->d_name.hash));
	}

	/* Switch the names.. */
	if (exchange)
		swap_names(dentry, target);
	else
		copy_name(dentry, target);

	/* ... and switch them in the tree */
	if (IS_ROOT(dentry)) {
		/* splicing a tree */
		dentry->d_flags |= DCACHE_RCUACCESS;
		dentry->d_parent = target->d_parent;
		target->d_parent = target;
		list_del_init(&target->d_child);
		list_move(&dentry->d_child, &dentry->d_parent->d_subdirs);
	} else {
		/* swapping two dentries */
		swap(dentry->d_parent, target->d_parent);
		list_move(&target->d_child, &target->d_parent->d_subdirs);
		list_move(&dentry->d_child, &dentry->d_parent->d_subdirs);
		if (exchange)
			fsnotify_d_move(target);
		fsnotify_d_move(dentry);
	}

	write_seqcount_end(&target->d_seq);
	write_seqcount_end(&dentry->d_seq);

	dentry_unlock_for_move(dentry, target);
}

/*
 * d_move - move a dentry
 * @dentry: entry to move
 * @target: new dentry
 *
 * Update the dcache to reflect the move of a file name. Negative
 * dcache entries should not be moved in this way. See the locking
 * requirements for __d_move.
 */
void d_move(struct dentry *dentry, struct dentry *target)
{
	write_seqlock(&rename_lock);
	__d_move(dentry, target, false);
	write_sequnlock(&rename_lock);
}
EXPORT_SYMBOL(d_move);

/*
 * d_exchange - exchange two dentries
 * @dentry1: first dentry
 * @dentry2: second dentry
 */
void d_exchange(struct dentry *dentry1, struct dentry *dentry2)
{
	write_seqlock(&rename_lock);

	WARN_ON(!dentry1->d_inode);
	WARN_ON(!dentry2->d_inode);
	WARN_ON(IS_ROOT(dentry1));
	WARN_ON(IS_ROOT(dentry2));

	__d_move(dentry1, dentry2, true);

	write_sequnlock(&rename_lock);
}

/**
 * d_ancestor - search for an ancestor
 * @p1: ancestor dentry
 * @p2: child dentry
 *
 * Returns the ancestor dentry of p2 which is a child of p1, if p1 is
 * an ancestor of p2, else NULL.
 */
struct dentry *d_ancestor(struct dentry *p1, struct dentry *p2)
{
	struct dentry *p;

	for (p = p2; !IS_ROOT(p); p = p->d_parent) {
		if (p->d_parent == p1)
			return p;
	}
	return NULL;
}

/*
 * This helper attempts to cope with remotely renamed directories
 *
 * It assumes that the caller is already holding
 * dentry->d_parent->d_inode->i_mutex, and rename_lock
 *
 * Note: If ever the locking in lock_rename() changes, then please
 * remember to update this too...
 */
static int __d_unalias(struct inode *inode,
		struct dentry *dentry, struct dentry *alias)
{
	struct mutex *m1 = NULL, *m2 = NULL;
	int ret = -ESTALE;

	/* If alias and dentry share a parent, then no extra locks required */
	if (alias->d_parent == dentry->d_parent)
		goto out_unalias;

	/* See lock_rename() */
	if (!mutex_trylock(&dentry->d_sb->s_vfs_rename_mutex))
		goto out_err;
	m1 = &dentry->d_sb->s_vfs_rename_mutex;
	if (!mutex_trylock(&alias->d_parent->d_inode->i_mutex))
		goto out_err;
	m2 = &alias->d_parent->d_inode->i_mutex;
out_unalias:
	__d_move(alias, dentry, false);
	ret = 0;
out_err:
	if (m2)
		mutex_unlock(m2);
	if (m1)
		mutex_unlock(m1);
	return ret;
}

/**
 * d_splice_alias - splice a disconnected dentry into the tree if one exists
 * @inode:  the inode which may have a disconnected dentry
 * @dentry: a negative dentry which we want to point to the inode.
 *
 * If inode is a directory and has an IS_ROOT alias, then d_move that in
 * place of the given dentry and return it, else simply d_add the inode
 * to the dentry and return NULL.
 *
 * If a non-IS_ROOT directory is found, the filesystem is corrupt, and
 * we should error out: directories can't have multiple aliases.
 *
 * This is needed in the lookup routine of any filesystem that is exportable
 * (via knfsd) so that we can build dcache paths to directories effectively.
 *
 * If a dentry was found and moved, then it is returned.  Otherwise NULL
 * is returned.  This matches the expected return value of ->lookup.
 *
 * Cluster filesystems may call this function with a negative, hashed dentry.
 * In that case, we know that the inode will be a regular file, and also this
 * will only occur during atomic_open. So we need to check for the dentry
 * being already hashed only in the final case.
 */
struct dentry *d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	BUG_ON(!d_unhashed(dentry));

	if (!inode) {
		__d_instantiate(dentry, NULL);
		goto out;
	}
	spin_lock(&inode->i_lock);
	if (S_ISDIR(inode->i_mode)) {
		struct dentry *new = __d_find_any_alias(inode);
		if (unlikely(new)) {
			/* The reference to new ensures it remains an alias */
			spin_unlock(&inode->i_lock);
			write_seqlock(&rename_lock);
			if (unlikely(d_ancestor(new, dentry))) {
				write_sequnlock(&rename_lock);
				dput(new);
				new = ERR_PTR(-ELOOP);
				pr_warn_ratelimited(
					"VFS: Lookup of '%s' in %s %s"
					" would have caused loop\n",
					dentry->d_name.name,
					inode->i_sb->s_type->name,
					inode->i_sb->s_id);
			} else if (!IS_ROOT(new)) {
				int err = __d_unalias(inode, dentry, new);
				write_sequnlock(&rename_lock);
				if (err) {
					dput(new);
					new = ERR_PTR(err);
				}
			} else {
				__d_move(new, dentry, false);
				write_sequnlock(&rename_lock);
				security_d_instantiate(new, inode);
			}
			iput(inode);
			return new;
		}
	}
	/* already taking inode->i_lock, so d_add() by hand */
	__d_instantiate(dentry, inode);
	spin_unlock(&inode->i_lock);
out:
	security_d_instantiate(dentry, inode);
	d_rehash(dentry);
	return NULL;
}
EXPORT_SYMBOL(d_splice_alias);

static int prepend(char **buffer, int *buflen, const char *str, int namelen)
{
	*buflen -= namelen;
	if (*buflen < 0)
		return -ENAMETOOLONG;
	*buffer -= namelen;
	memcpy(*buffer, str, namelen);
	return 0;
}

/**
 * prepend_name - prepend a pathname in front of current buffer pointer
 * @buffer: buffer pointer
 * @buflen: allocated length of the buffer
 * @name:   name string and length qstr structure
 *
 * With RCU path tracing, it may race with d_move(). Use ACCESS_ONCE() to
 * make sure that either the old or the new name pointer and length are
 * fetched. However, there may be mismatch between length and pointer.
 * The length cannot be trusted, we need to copy it byte-by-byte until
 * the length is reached or a null byte is found. It also prepends "/" at
 * the beginning of the name. The sequence number check at the caller will
 * retry it again when a d_move() does happen. So any garbage in the buffer
 * due to mismatched pointer and length will be discarded.
 *
 * Data dependency barrier is needed to make sure that we see that terminating
 * NUL.  Alpha strikes again, film at 11...
 */
static int prepend_name(char **buffer, int *buflen, struct qstr *name)
{
	const char *dname = ACCESS_ONCE(name->name);
	u32 dlen = ACCESS_ONCE(name->len);
	char *p;

	smp_read_barrier_depends();

	*buflen -= dlen + 1;
	if (*buflen < 0)
		return -ENAMETOOLONG;
	p = *buffer -= dlen + 1;
	*p++ = '/';
	while (dlen--) {
		char c = *dname++;
		if (!c)
			break;
		*p++ = c;
	}
	return 0;
}

/**
 * prepend_path - Prepend path string to a buffer
 * @path: the dentry/vfsmount to report
 * @root: root vfsmnt/dentry
 * @buffer: pointer to the end of the buffer
 * @buflen: pointer to buffer length
 *
 * The function will first try to write out the pathname without taking any
 * lock other than the RCU read lock to make sure that dentries won't go away.
 * It only checks the sequence number of the global rename_lock as any change
 * in the dentry's d_seq will be preceded by changes in the rename_lock
 * sequence number. If the sequence number had been changed, it will restart
 * the whole pathname back-tracing sequence again by taking the rename_lock.
 * In this case, there is no need to take the RCU read lock as the recursive
 * parent pointer references will keep the dentry chain alive as long as no
 * rename operation is performed.
 */
static int prepend_path(const struct path *path,
			const struct path *root,
			char **buffer, int *buflen)
{
	struct dentry *dentry;
	struct vfsmount *vfsmnt;
	struct mount *mnt;
	int error = 0;
	unsigned seq, m_seq = 0;
	char *bptr;
	int blen;

	rcu_read_lock();
restart_mnt:
	read_seqbegin_or_lock(&mount_lock, &m_seq);
	seq = 0;
	rcu_read_lock();
restart:
	bptr = *buffer;
	blen = *buflen;
	error = 0;
	dentry = path->dentry;
	vfsmnt = path->mnt;
	mnt = real_mount(vfsmnt);
	read_seqbegin_or_lock(&rename_lock, &seq);
	while (dentry != root->dentry || vfsmnt != root->mnt) {
		struct dentry * parent;

		if (dentry == vfsmnt->mnt_root || IS_ROOT(dentry)) {
			struct mount *parent = ACCESS_ONCE(mnt->mnt_parent);
			/* Escaped? */
			if (dentry != vfsmnt->mnt_root) {
				bptr = *buffer;
				blen = *buflen;
				error = 3;
				break;
			}
			/* Global root? */
			if (mnt != parent) {
				dentry = ACCESS_ONCE(mnt->mnt_mountpoint);
				mnt = parent;
				vfsmnt = &mnt->mnt;
				continue;
			}
			if (!error)
				error = is_mounted(vfsmnt) ? 1 : 2;
			break;
		}
		parent = dentry->d_parent;
		prefetch(parent);
		error = prepend_name(&bptr, &blen, &dentry->d_name);
		if (error)
			break;

		dentry = parent;
	}
	if (!(seq & 1))
		rcu_read_unlock();
	if (need_seqretry(&rename_lock, seq)) {
		seq = 1;
		goto restart;
	}
	done_seqretry(&rename_lock, seq);

	if (!(m_seq & 1))
		rcu_read_unlock();
	if (need_seqretry(&mount_lock, m_seq)) {
		m_seq = 1;
		goto restart_mnt;
	}
	done_seqretry(&mount_lock, m_seq);

	if (error >= 0 && bptr == *buffer) {
		if (--blen < 0)
			error = -ENAMETOOLONG;
		else
			*--bptr = '/';
	}
	*buffer = bptr;
	*buflen = blen;
	return error;
}

/**
 * __d_path - return the path of a dentry
 * @path: the dentry/vfsmount to report
 * @root: root vfsmnt/dentry
 * @buf: buffer to return value in
 * @buflen: buffer length
 *
 * Convert a dentry into an ASCII path name.
 *
 * Returns a pointer into the buffer or an error code if the
 * path was too long.
 *
 * "buflen" should be positive.
 *
 * If the path is not reachable from the supplied root, return %NULL.
 */
char *__d_path(const struct path *path,
	       const struct path *root,
	       char *buf, int buflen)
{
	char *res = buf + buflen;
	int error;

	prepend(&res, &buflen, "\0", 1);
	error = prepend_path(path, root, &res, &buflen);

	if (error < 0)
		return ERR_PTR(error);
	if (error > 0)
		return NULL;
	return res;
}

char *d_absolute_path(const struct path *path,
	       char *buf, int buflen)
{
	struct path root = {};
	char *res = buf + buflen;
	int error;

	prepend(&res, &buflen, "\0", 1);
	error = prepend_path(path, &root, &res, &buflen);

	if (error > 1)
		error = -EINVAL;
	if (error < 0)
		return ERR_PTR(error);
	return res;
}

/*
 * same as __d_path but appends "(deleted)" for unlinked files.
 */
static int path_with_deleted(const struct path *path,
			     const struct path *root,
			     char **buf, int *buflen)
{
	prepend(buf, buflen, "\0", 1);
	if (d_unlinked(path->dentry)) {
		int error = prepend(buf, buflen, " (deleted)", 10);
		if (error)
			return error;
	}

	return prepend_path(path, root, buf, buflen);
}

static int prepend_unreachable(char **buffer, int *buflen)
{
	return prepend(buffer, buflen, "(unreachable)", 13);
}

static void get_fs_root_rcu(struct fs_struct *fs, struct path *root)
{
	unsigned seq;

	do {
		seq = read_seqcount_begin(&fs->seq);
		*root = fs->root;
	} while (read_seqcount_retry(&fs->seq, seq));
}

/**
 * d_path - return the path of a dentry
 * @path: path to report
 * @buf: buffer to return value in
 * @buflen: buffer length
 *
 * Convert a dentry into an ASCII path name. If the entry has been deleted
 * the string " (deleted)" is appended. Note that this is ambiguous.
 *
 * Returns a pointer into the buffer or an error code if the path was
 * too long. Note: Callers should use the returned pointer, not the passed
 * in buffer, to use the name! The implementation often starts at an offset
 * into the buffer, and may leave 0 bytes at the start.
 *
 * "buflen" should be positive.
 */
char *d_path(const struct path *path, char *buf, int buflen)
{
	char *res = buf + buflen;
	struct path root;
	int error;

	/*
	 * We have various synthetic filesystems that never get mounted.  On
	 * these filesystems dentries are never used for lookup purposes, and
	 * thus don't need to be hashed.  They also don't need a name until a
	 * user wants to identify the object in /proc/pid/fd/.  The little hack
	 * below allows us to generate a name for these objects on demand:
	 *
	 * Some pseudo inodes are mountable.  When they are mounted
	 * path->dentry == path->mnt->mnt_root.  In that case don't call d_dname
	 * and instead have d_path return the mounted path.
	 */
	if (path->dentry->d_op && path->dentry->d_op->d_dname &&
	    (!IS_ROOT(path->dentry) || path->dentry != path->mnt->mnt_root))
		return path->dentry->d_op->d_dname(path->dentry, buf, buflen);

	rcu_read_lock();
	get_fs_root_rcu(current->fs, &root);
	error = path_with_deleted(path, &root, &res, &buflen);
	rcu_read_unlock();

	if (error < 0)
		res = ERR_PTR(error);
	return res;
}
EXPORT_SYMBOL(d_path);

/*
 * Helper function for dentry_operations.d_dname() members
 */
char *dynamic_dname(struct dentry *dentry, char *buffer, int buflen,
			const char *fmt, ...)
{
	va_list args;
	char temp[64];
	int sz;

	va_start(args, fmt);
	sz = vsnprintf(temp, sizeof(temp), fmt, args) + 1;
	va_end(args);

	if (sz > sizeof(temp) || sz > buflen)
		return ERR_PTR(-ENAMETOOLONG);

	buffer += buflen - sz;
	return memcpy(buffer, temp, sz);
}

char *simple_dname(struct dentry *dentry, char *buffer, int buflen)
{
	char *end = buffer + buflen;
	/* these dentries are never renamed, so d_lock is not needed */
	if (prepend(&end, &buflen, " (deleted)", 11) ||
	    prepend(&end, &buflen, dentry->d_name.name, dentry->d_name.len) ||
	    prepend(&end, &buflen, "/", 1))  
		end = ERR_PTR(-ENAMETOOLONG);
	return end;
}
EXPORT_SYMBOL(simple_dname);

/*
 * Write full pathname from the root of the filesystem into the buffer.
 */
static char *__dentry_path(struct dentry *d, char *buf, int buflen)
{
	struct dentry *dentry;
	char *end, *retval;
	int len, seq = 0;
	int error = 0;

	if (buflen < 2)
		goto Elong;

	rcu_read_lock();
restart:
	dentry = d;
	end = buf + buflen;
	len = buflen;
	prepend(&end, &len, "\0", 1);
	/* Get '/' right */
	retval = end-1;
	*retval = '/';
	read_seqbegin_or_lock(&rename_lock, &seq);
	while (!IS_ROOT(dentry)) {
		struct dentry *parent = dentry->d_parent;

		prefetch(parent);
		error = prepend_name(&end, &len, &dentry->d_name);
		if (error)
			break;

		retval = end;
		dentry = parent;
	}
	if (!(seq & 1))
		rcu_read_unlock();
	if (need_seqretry(&rename_lock, seq)) {
		seq = 1;
		goto restart;
	}
	done_seqretry(&rename_lock, seq);
	if (error)
		goto Elong;
	return retval;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

char *dentry_path_raw(struct dentry *dentry, char *buf, int buflen)
{
	return __dentry_path(dentry, buf, buflen);
}
EXPORT_SYMBOL(dentry_path_raw);

char *dentry_path(struct dentry *dentry, char *buf, int buflen)
{
	char *p = NULL;
	char *retval;

	if (d_unlinked(dentry)) {
		p = buf + buflen;
		if (prepend(&p, &buflen, "//deleted", 10) != 0)
			goto Elong;
		buflen++;
	}
	retval = __dentry_path(dentry, buf, buflen);
	if (!IS_ERR(retval) && p)
		*p = '/';	/* restore '/' overriden with '\0' */
	return retval;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

static void get_fs_root_and_pwd_rcu(struct fs_struct *fs, struct path *root,
				    struct path *pwd)
{
	unsigned seq;

	do {
		seq = read_seqcount_begin(&fs->seq);
		*root = fs->root;
		*pwd = fs->pwd;
	} while (read_seqcount_retry(&fs->seq, seq));
}

/*
 * NOTE! The user-level library version returns a
 * character pointer. The kernel system call just
 * returns the length of the buffer filled (which
 * includes the ending '\0' character), or a negative
 * error value. So libc would do something like
 *
 *	char *getcwd(char * buf, size_t size)
 *	{
 *		int retval;
 *
 *		retval = sys_getcwd(buf, size);
 *		if (retval >= 0)
 *			return buf;
 *		errno = -retval;
 *		return NULL;
 *	}
 */
SYSCALL_DEFINE2(getcwd, char __user *, buf, unsigned long, size)
{
	int error;
	struct path pwd, root;
	char *page = __getname();

	if (!page)
		return -ENOMEM;

	rcu_read_lock();
	get_fs_root_and_pwd_rcu(current->fs, &root, &pwd);

	error = -ENOENT;
	if (!d_unlinked(pwd.dentry)) {
		unsigned long len;
		char *cwd = page + PATH_MAX;
		int buflen = PATH_MAX;

		prepend(&cwd, &buflen, "\0", 1);
		error = prepend_path(&pwd, &root, &cwd, &buflen);
		rcu_read_unlock();

		if (error < 0)
			goto out;

		/* Unreachable from current root */
		if (error > 0) {
			error = prepend_unreachable(&cwd, &buflen);
			if (error)
				goto out;
		}

		error = -ERANGE;
		len = PATH_MAX + page - cwd;
		if (len <= size) {
			error = len;
			if (copy_to_user(buf, cwd, len))
				error = -EFAULT;
		}
	} else {
		rcu_read_unlock();
	}

out:
	__putname(page);
	return error;
}

/*
 * Test whether new_dentry is a subdirectory of old_dentry.
 *
 * Trivially implemented using the dcache structure
 */

/**
 * is_subdir - is new dentry a subdirectory of old_dentry
 * @new_dentry: new dentry
 * @old_dentry: old dentry
 *
 * Returns 1 if new_dentry is a subdirectory of the parent (at any depth).
 * Returns 0 otherwise.
 * Caller must ensure that "new_dentry" is pinned before calling is_subdir()
 */
  
int is_subdir(struct dentry *new_dentry, struct dentry *old_dentry)
{
	int result;
	unsigned seq;

	if (new_dentry == old_dentry)
		return 1;

	do {
		/* for restarting inner loop in case of seq retry */
		seq = read_seqbegin(&rename_lock);
		/*
		 * Need rcu_readlock to protect against the d_parent trashing
		 * due to d_move
		 */
		rcu_read_lock();
		if (d_ancestor(old_dentry, new_dentry))
			result = 1;
		else
			result = 0;
		rcu_read_unlock();
	} while (read_seqretry(&rename_lock, seq));

	return result;
}

static enum d_walk_ret d_genocide_kill(void *data, struct dentry *dentry)
{
	struct dentry *root = data;
	if (dentry != root) {
		if (d_unhashed(dentry) || !dentry->d_inode)
			return D_WALK_SKIP;

		if (!(dentry->d_flags & DCACHE_GENOCIDE)) {
			dentry->d_flags |= DCACHE_GENOCIDE;
			dentry->d_lockref.count--;
		}
	}
	return D_WALK_CONTINUE;
}

void d_genocide(struct dentry *parent)
{
	d_walk(parent, parent, d_genocide_kill, NULL);
}

void d_tmpfile(struct dentry *dentry, struct inode *inode)
{
	inode_dec_link_count(inode);
	BUG_ON(dentry->d_name.name != dentry->d_iname ||
		!hlist_unhashed(&dentry->d_u.d_alias) ||
		!d_unlinked(dentry));
	spin_lock(&dentry->d_parent->d_lock);
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	dentry->d_name.len = sprintf(dentry->d_iname, "#%llu",
				(unsigned long long)inode->i_ino);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dentry->d_parent->d_lock);
	d_instantiate(dentry, inode);
}
EXPORT_SYMBOL(d_tmpfile);

static __initdata unsigned long dhash_entries;
static int __init set_dhash_entries(char *str)
{
	if (!str)
		return 0;
	dhash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("dhash_entries=", set_dhash_entries);

static void __init dcache_init_early(void)
{
	unsigned int loop;

	/* If hashes are distributed across NUMA nodes, defer
	 * hash allocation until vmalloc space is available.
	 */
	if (hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_EARLY,
					&d_hash_shift,
					&d_hash_mask,
					0,
					0);

	for (loop = 0; loop < (1U << d_hash_shift); loop++)
		INIT_HLIST_BL_HEAD(dentry_hashtable + loop);
}

static void __init dcache_init(void)
{
	unsigned int loop;

	/* 
	 * A constructor could be added for stable state like the lists,
	 * but it is probably not worth it because of the cache nature
	 * of the dcache. 
	 */
	dentry_cache = KMEM_CACHE(dentry,
		SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|SLAB_MEM_SPREAD);

	/* Hash may have been set up in dcache_init_early */
	if (!hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					0,
					&d_hash_shift,
					&d_hash_mask,
					0,
					0);

	for (loop = 0; loop < (1U << d_hash_shift); loop++)
		INIT_HLIST_BL_HEAD(dentry_hashtable + loop);
}

/* SLAB cache for __getname() consumers */
struct kmem_cache *names_cachep __read_mostly;
EXPORT_SYMBOL(names_cachep);

EXPORT_SYMBOL(d_genocide);

void __init vfs_caches_init_early(void)
{
	dcache_init_early();
	inode_init_early();
}

void __init vfs_caches_init(void)
{
	names_cachep = kmem_cache_create("names_cache", PATH_MAX, 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);

	dcache_init();
	inode_init();
	files_init();
	files_maxfiles_init();
	mnt_init();
	bdev_cache_init();
	chrdev_init();
}
