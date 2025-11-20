// SPDX-License-Identifier: GPL-2.0-only
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

#include <linux/ratelimit.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/fscrypt.h>
#include <linux/fsnotify.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/security.h>
#include <linux/seqlock.h>
#include <linux/memblock.h>
#include <linux/bit_spinlock.h>
#include <linux/rculist_bl.h>
#include <linux/list_lru.h>
#include "internal.h"
#include "mount.h"

#include <asm/runtime-const.h>

/*
 * Usage:
 * dcache->d_inode->i_lock protects:
 *   - i_dentry, d_u.d_alias, d_inode of aliases
 * dcache_hash_bucket lock protects:
 *   - the dcache hash table
 * s_roots bl list spinlock protects:
 *   - the s_roots list (see __d_drop)
 * dentry->d_sb->s_dentry_lru_lock protects:
 *   - the dcache lru lists and counters
 * d_lock protects:
 *   - d_flags
 *   - d_name
 *   - d_lru
 *   - d_count
 *   - d_unhashed()
 *   - d_parent and d_chilren
 *   - childrens' d_sib and d_parent
 *   - d_u.d_alias, d_inode
 *
 * Ordering:
 * dentry->d_inode->i_lock
 *   dentry->d_lock
 *     dentry->d_sb->s_dentry_lru_lock
 *     dcache_hash_bucket lock
 *     s_roots lock
 *
 * If there is an ancestor relationship:
 * dentry->d_parent->...->d_parent->d_lock
 *   ...
 *     dentry->d_parent->d_lock
 *       dentry->d_lock
 *
 * If no ancestor relationship:
 * arbitrary, since it's serialized on rename_lock
 */
static int sysctl_vfs_cache_pressure __read_mostly = 100;
static int sysctl_vfs_cache_pressure_denom __read_mostly = 100;

unsigned long vfs_pressure_ratio(unsigned long val)
{
	return mult_frac(val, sysctl_vfs_cache_pressure, sysctl_vfs_cache_pressure_denom);
}
EXPORT_SYMBOL_GPL(vfs_pressure_ratio);

__cacheline_aligned_in_smp DEFINE_SEQLOCK(rename_lock);

EXPORT_SYMBOL(rename_lock);

static struct kmem_cache *dentry_cache __ro_after_init;

const struct qstr empty_name = QSTR_INIT("", 0);
EXPORT_SYMBOL(empty_name);
const struct qstr slash_name = QSTR_INIT("/", 1);
EXPORT_SYMBOL(slash_name);
const struct qstr dotdot_name = QSTR_INIT("..", 2);
EXPORT_SYMBOL(dotdot_name);

/*
 * This is the single most critical data structure when it comes
 * to the dcache: the hashtable for lookups. Somebody should try
 * to make this good - I've just made it work.
 *
 * This hash-function tries to avoid losing too many bits of hash
 * information, yet avoid using a prime hash-size or similar.
 *
 * Marking the variables "used" ensures that the compiler doesn't
 * optimize them away completely on architectures with runtime
 * constant infrastructure, this allows debuggers to see their
 * values. But updating these values has no effect on those arches.
 */

static unsigned int d_hash_shift __ro_after_init __used;

static struct hlist_bl_head *dentry_hashtable __ro_after_init __used;

static inline struct hlist_bl_head *d_hash(unsigned long hashlen)
{
	return runtime_const_ptr(dentry_hashtable) +
		runtime_const_shift_right_32(hashlen, d_hash_shift);
}

#define IN_LOOKUP_SHIFT 10
static struct hlist_bl_head in_lookup_hashtable[1 << IN_LOOKUP_SHIFT];

static inline struct hlist_bl_head *in_lookup_hash(const struct dentry *parent,
					unsigned int hash)
{
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	return in_lookup_hashtable + hash_32(hash, IN_LOOKUP_SHIFT);
}

struct dentry_stat_t {
	long nr_dentry;
	long nr_unused;
	long age_limit;		/* age in seconds */
	long want_pages;	/* pages requested by system */
	long nr_negative;	/* # of unused negative dentries */
	long dummy;		/* Reserved for future use */
};

static DEFINE_PER_CPU(long, nr_dentry);
static DEFINE_PER_CPU(long, nr_dentry_unused);
static DEFINE_PER_CPU(long, nr_dentry_negative);
static int dentry_negative_policy;

#if defined(CONFIG_SYSCTL) && defined(CONFIG_PROC_FS)
/* Statistics gathering. */
static struct dentry_stat_t dentry_stat = {
	.age_limit = 45,
};

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

static long get_nr_dentry_negative(void)
{
	int i;
	long sum = 0;

	for_each_possible_cpu(i)
		sum += per_cpu(nr_dentry_negative, i);
	return sum < 0 ? 0 : sum;
}

static int proc_nr_dentry(const struct ctl_table *table, int write, void *buffer,
			  size_t *lenp, loff_t *ppos)
{
	dentry_stat.nr_dentry = get_nr_dentry();
	dentry_stat.nr_unused = get_nr_dentry_unused();
	dentry_stat.nr_negative = get_nr_dentry_negative();
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}

static const struct ctl_table fs_dcache_sysctls[] = {
	{
		.procname	= "dentry-state",
		.data		= &dentry_stat,
		.maxlen		= 6*sizeof(long),
		.mode		= 0444,
		.proc_handler	= proc_nr_dentry,
	},
	{
		.procname	= "dentry-negative",
		.data		= &dentry_negative_policy,
		.maxlen		= sizeof(dentry_negative_policy),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

static const struct ctl_table vm_dcache_sysctls[] = {
	{
		.procname	= "vfs_cache_pressure",
		.data		= &sysctl_vfs_cache_pressure,
		.maxlen		= sizeof(sysctl_vfs_cache_pressure),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "vfs_cache_pressure_denom",
		.data		= &sysctl_vfs_cache_pressure_denom,
		.maxlen		= sizeof(sysctl_vfs_cache_pressure_denom),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE_HUNDRED,
	},
};

static int __init init_fs_dcache_sysctls(void)
{
	register_sysctl_init("vm", vm_dcache_sysctls);
	register_sysctl_init("fs", fs_dcache_sysctls);
	return 0;
}
fs_initcall(init_fs_dcache_sysctls);
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
		a = read_word_at_a_time(cs);
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
	/*
	 * Be careful about RCU walk racing with rename:
	 * use 'READ_ONCE' to fetch the name pointer.
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
	const unsigned char *cs = READ_ONCE(dentry->d_name.name);

	return dentry_string_cmp(cs, ct, tcount);
}

/*
 * long names are allocated separately from dentry and never modified.
 * Refcounted, freeing is RCU-delayed.  See take_dentry_name_snapshot()
 * for the reason why ->count and ->head can't be combined into a union.
 * dentry_string_cmp() relies upon ->name[] being word-aligned.
 */
struct external_name {
	atomic_t count;
	struct rcu_head head;
	unsigned char name[] __aligned(sizeof(unsigned long));
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
	return dentry->d_name.name != dentry->d_shortname.string;
}

void take_dentry_name_snapshot(struct name_snapshot *name, struct dentry *dentry)
{
	unsigned seq;
	const unsigned char *s;

	rcu_read_lock();
retry:
	seq = read_seqcount_begin(&dentry->d_seq);
	s = READ_ONCE(dentry->d_name.name);
	name->name.hash_len = dentry->d_name.hash_len;
	name->name.name = name->inline_name.string;
	if (likely(s == dentry->d_shortname.string)) {
		name->inline_name = dentry->d_shortname;
	} else {
		struct external_name *p;
		p = container_of(s, struct external_name, name[0]);
		// get a valid reference
		if (unlikely(!atomic_inc_not_zero(&p->count)))
			goto retry;
		name->name.name = s;
	}
	if (read_seqcount_retry(&dentry->d_seq, seq)) {
		release_dentry_name_snapshot(name);
		goto retry;
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(take_dentry_name_snapshot);

void release_dentry_name_snapshot(struct name_snapshot *name)
{
	if (unlikely(name->name.name != name->inline_name.string)) {
		struct external_name *p;
		p = container_of(name->name.name, struct external_name, name[0]);
		if (unlikely(atomic_dec_and_test(&p->count)))
			kfree_rcu(p, head);
	}
}
EXPORT_SYMBOL(release_dentry_name_snapshot);

static inline void __d_set_inode_and_type(struct dentry *dentry,
					  struct inode *inode,
					  unsigned type_flags)
{
	unsigned flags;

	dentry->d_inode = inode;
	flags = READ_ONCE(dentry->d_flags);
	flags &= ~DCACHE_ENTRY_TYPE;
	flags |= type_flags;
	smp_store_release(&dentry->d_flags, flags);
}

static inline void __d_clear_type_and_inode(struct dentry *dentry)
{
	unsigned flags = READ_ONCE(dentry->d_flags);

	flags &= ~DCACHE_ENTRY_TYPE;
	WRITE_ONCE(dentry->d_flags, flags);
	dentry->d_inode = NULL;
	/*
	 * The negative counter only tracks dentries on the LRU. Don't inc if
	 * d_lru is on another list.
	 */
	if ((flags & (DCACHE_LRU_LIST|DCACHE_SHRINK_LIST)) == DCACHE_LRU_LIST)
		this_cpu_inc(nr_dentry_negative);
}

static void dentry_free(struct dentry *dentry)
{
	WARN_ON(!hlist_unhashed(&dentry->d_u.d_alias));
	if (unlikely(dname_external(dentry))) {
		struct external_name *p = external_name(dentry);
		if (likely(atomic_dec_and_test(&p->count))) {
			call_rcu(&dentry->d_u.d_rcu, __d_free_external);
			return;
		}
	}
	/* if dentry was never visible to RCU, immediate free is OK */
	if (dentry->d_flags & DCACHE_NORCU)
		__d_free(&dentry->d_u.d_rcu);
	else
		call_rcu(&dentry->d_u.d_rcu, __d_free);
}

/*
 * Release the dentry's inode, using the filesystem
 * d_iput() operation if defined.
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
 * The per-cpu "nr_dentry_negative" counters are only updated
 * when deleted from or added to the per-superblock LRU list, not
 * from/to the shrink list. That is to avoid an unneeded dec/inc
 * pair when moving from LRU to shrink list in select_collect().
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
	if (d_is_negative(dentry))
		this_cpu_inc(nr_dentry_negative);
	WARN_ON_ONCE(!list_lru_add_obj(
			&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
}

static void d_lru_del(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags &= ~DCACHE_LRU_LIST;
	this_cpu_dec(nr_dentry_unused);
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	WARN_ON_ONCE(!list_lru_del_obj(
			&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
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
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	list_lru_isolate(lru, &dentry->d_lru);
}

static void d_lru_shrink_move(struct list_lru_one *lru, struct dentry *dentry,
			      struct list_head *list)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags |= DCACHE_SHRINK_LIST;
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	list_lru_isolate_move(lru, &dentry->d_lru, list);
}

static void ___d_drop(struct dentry *dentry)
{
	struct hlist_bl_head *b;
	/*
	 * Hashed dentries are normally on the dentry hashtable,
	 * with the exception of those newly allocated by
	 * d_obtain_root, which are always IS_ROOT:
	 */
	if (unlikely(IS_ROOT(dentry)))
		b = &dentry->d_sb->s_roots;
	else
		b = d_hash(dentry->d_name.hash);

	hlist_bl_lock(b);
	__hlist_bl_del(&dentry->d_hash);
	hlist_bl_unlock(b);
}

void __d_drop(struct dentry *dentry)
{
	if (!d_unhashed(dentry)) {
		___d_drop(dentry);
		dentry->d_hash.pprev = NULL;
		write_seqcount_invalidate(&dentry->d_seq);
	}
}
EXPORT_SYMBOL(__d_drop);

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
 * __d_drop requires dentry->d_lock
 *
 * ___d_drop doesn't mark dentry as "unhashed"
 * (dentry->d_hash.pprev will be LIST_POISON2, not NULL).
 */
void d_drop(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(d_drop);

static inline void dentry_unlist(struct dentry *dentry)
{
	struct dentry *next;
	/*
	 * Inform d_walk() and shrink_dentry_list() that we are no longer
	 * attached to the dentry tree
	 */
	dentry->d_flags |= DCACHE_DENTRY_KILLED;
	if (unlikely(hlist_unhashed(&dentry->d_sib)))
		return;
	__hlist_del(&dentry->d_sib);
	/*
	 * Cursors can move around the list of children.  While we'd been
	 * a normal list member, it didn't matter - ->d_sib.next would've
	 * been updated.  However, from now on it won't be and for the
	 * things like d_walk() it might end up with a nasty surprise.
	 * Normally d_walk() doesn't care about cursors moving around -
	 * ->d_lock on parent prevents that and since a cursor has no children
	 * of its own, we get through it without ever unlocking the parent.
	 * There is one exception, though - if we ascend from a child that
	 * gets killed as soon as we unlock it, the next sibling is found
	 * using the value left in its ->d_sib.next.  And if _that_
	 * pointed to a cursor, and cursor got moved (e.g. by lseek())
	 * before d_walk() regains parent->d_lock, we'll end up skipping
	 * everything the cursor had been moved past.
	 *
	 * Solution: make sure that the pointer left behind in ->d_sib.next
	 * points to something that won't be moving around.  I.e. skip the
	 * cursors.
	 */
	while (dentry->d_sib.next) {
		next = hlist_entry(dentry->d_sib.next, struct dentry, d_sib);
		if (likely(!(next->d_flags & DCACHE_DENTRY_CURSOR)))
			break;
		dentry->d_sib.next = next->d_sib.next;
	}
}

static struct dentry *__dentry_kill(struct dentry *dentry)
{
	struct dentry *parent = NULL;
	bool can_free = true;

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
	if (dentry->d_inode)
		dentry_unlink_inode(dentry);
	else
		spin_unlock(&dentry->d_lock);
	this_cpu_dec(nr_dentry);
	if (dentry->d_op && dentry->d_op->d_release)
		dentry->d_op->d_release(dentry);

	cond_resched();
	/* now that it's negative, ->d_parent is stable */
	if (!IS_ROOT(dentry)) {
		parent = dentry->d_parent;
		spin_lock(&parent->d_lock);
	}
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	dentry_unlist(dentry);
	if (dentry->d_flags & DCACHE_SHRINK_LIST)
		can_free = false;
	spin_unlock(&dentry->d_lock);
	if (likely(can_free))
		dentry_free(dentry);
	if (parent && --parent->d_lockref.count) {
		spin_unlock(&parent->d_lock);
		return NULL;
	}
	return parent;
}

/*
 * Lock a dentry for feeding it to __dentry_kill().
 * Called under rcu_read_lock() and dentry->d_lock; the former
 * guarantees that nothing we access will be freed under us.
 * Note that dentry is *not* protected from concurrent dentry_kill(),
 * d_delete(), etc.
 *
 * Return false if dentry is busy.  Otherwise, return true and have
 * that dentry's inode locked.
 */

static bool lock_for_kill(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	if (unlikely(dentry->d_lockref.count))
		return false;

	if (!inode || likely(spin_trylock(&inode->i_lock)))
		return true;

	do {
		spin_unlock(&dentry->d_lock);
		spin_lock(&inode->i_lock);
		spin_lock(&dentry->d_lock);
		if (likely(inode == dentry->d_inode))
			break;
		spin_unlock(&inode->i_lock);
		inode = dentry->d_inode;
	} while (inode);
	if (likely(!dentry->d_lockref.count))
		return true;
	if (inode)
		spin_unlock(&inode->i_lock);
	return false;
}

/*
 * Decide if dentry is worth retaining.  Usually this is called with dentry
 * locked; if not locked, we are more limited and might not be able to tell
 * without a lock.  False in this case means "punt to locked path and recheck".
 *
 * In case we aren't locked, these predicates are not "stable". However, it is
 * sufficient that at some point after we dropped the reference the dentry was
 * hashed and the flags had the proper value. Other dentry users may have
 * re-gotten a reference to the dentry and change that, but our work is done -
 * we can leave the dentry around with a zero refcount.
 */
static inline bool retain_dentry(struct dentry *dentry, bool locked)
{
	unsigned int d_flags;

	smp_rmb();
	d_flags = READ_ONCE(dentry->d_flags);

	// Unreachable? Nobody would be able to look it up, no point retaining
	if (unlikely(d_unhashed(dentry)))
		return false;

	// Same if it's disconnected
	if (unlikely(d_flags & DCACHE_DISCONNECTED))
		return false;

	// ->d_delete() might tell us not to bother, but that requires
	// ->d_lock; can't decide without it
	if (unlikely(d_flags & DCACHE_OP_DELETE)) {
		if (!locked || dentry->d_op->d_delete(dentry))
			return false;
	}

	// Explicitly told not to bother
	if (unlikely(d_flags & DCACHE_DONTCACHE))
		return false;

	// At this point it looks like we ought to keep it.  We also might
	// need to do something - put it on LRU if it wasn't there already
	// and mark it referenced if it was on LRU, but not marked yet.
	// Unfortunately, both actions require ->d_lock, so in lockless
	// case we'd have to punt rather than doing those.
	if (unlikely(!(d_flags & DCACHE_LRU_LIST))) {
		if (!locked)
			return false;
		d_lru_add(dentry);
	} else if (unlikely(!(d_flags & DCACHE_REFERENCED))) {
		if (!locked)
			return false;
		dentry->d_flags |= DCACHE_REFERENCED;
	}
	return true;
}

void d_mark_dontcache(struct inode *inode)
{
	struct dentry *de;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(de, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&de->d_lock);
		de->d_flags |= DCACHE_DONTCACHE;
		spin_unlock(&de->d_lock);
	}
	inode->i_state |= I_DONTCACHE;
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(d_mark_dontcache);

/*
 * Try to do a lockless dput(), and return whether that was successful.
 *
 * If unsuccessful, we return false, having already taken the dentry lock.
 * In that case refcount is guaranteed to be zero and we have already
 * decided that it's not worth keeping around.
 *
 * The caller needs to hold the RCU read lock, so that the dentry is
 * guaranteed to stay around even if the refcount goes down to zero!
 */
static inline bool fast_dput(struct dentry *dentry)
{
	int ret;

	/*
	 * try to decrement the lockref optimistically.
	 */
	ret = lockref_put_return(&dentry->d_lockref);

	/*
	 * If the lockref_put_return() failed due to the lock being held
	 * by somebody else, the fast path has failed. We will need to
	 * get the lock, and then check the count again.
	 */
	if (unlikely(ret < 0)) {
		spin_lock(&dentry->d_lock);
		if (WARN_ON_ONCE(dentry->d_lockref.count <= 0)) {
			spin_unlock(&dentry->d_lock);
			return true;
		}
		dentry->d_lockref.count--;
		goto locked;
	}

	/*
	 * If we weren't the last ref, we're done.
	 */
	if (ret)
		return true;

	/*
	 * Can we decide that decrement of refcount is all we needed without
	 * taking the lock?  There's a very common case when it's all we need -
	 * dentry looks like it ought to be retained and there's nothing else
	 * to do.
	 */
	if (retain_dentry(dentry, false))
		return true;

	/*
	 * Either not worth retaining or we can't tell without the lock.
	 * Get the lock, then.  We've already decremented the refcount to 0,
	 * but we'll need to re-check the situation after getting the lock.
	 */
	spin_lock(&dentry->d_lock);

	/*
	 * Did somebody else grab a reference to it in the meantime, and
	 * we're no longer the last user after all? Alternatively, somebody
	 * else could have killed it and marked it dead. Either way, we
	 * don't need to do anything else.
	 */
locked:
	if (dentry->d_lockref.count || retain_dentry(dentry, true)) {
		spin_unlock(&dentry->d_lock);
		return true;
	}
	return false;
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
	if (!dentry)
		return;
	might_sleep();
	rcu_read_lock();
	if (likely(fast_dput(dentry))) {
		rcu_read_unlock();
		return;
	}
	while (lock_for_kill(dentry)) {
		rcu_read_unlock();
		dentry = __dentry_kill(dentry);
		if (!dentry)
			return;
		if (retain_dentry(dentry, true)) {
			spin_unlock(&dentry->d_lock);
			return;
		}
		rcu_read_lock();
	}
	rcu_read_unlock();
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(dput);

static void to_shrink_list(struct dentry *dentry, struct list_head *list)
__must_hold(&dentry->d_lock)
{
	if (!(dentry->d_flags & DCACHE_SHRINK_LIST)) {
		if (dentry->d_flags & DCACHE_LRU_LIST)
			d_lru_del(dentry);
		d_shrink_add(dentry, list);
	}
}

void dput_to_list(struct dentry *dentry, struct list_head *list)
{
	rcu_read_lock();
	if (likely(fast_dput(dentry))) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	to_shrink_list(dentry, list);
	spin_unlock(&dentry->d_lock);
}

struct dentry *dget_parent(struct dentry *dentry)
{
	int gotref;
	struct dentry *ret;
	unsigned seq;

	/*
	 * Do optimistic parent lookup without any
	 * locking.
	 */
	rcu_read_lock();
	seq = raw_seqcount_begin(&dentry->d_seq);
	ret = READ_ONCE(dentry->d_parent);
	gotref = lockref_get_not_zero(&ret->d_lockref);
	rcu_read_unlock();
	if (likely(gotref)) {
		if (!read_seqcount_retry(&dentry->d_seq, seq))
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

static struct dentry * __d_find_any_alias(struct inode *inode)
{
	struct dentry *alias;

	if (hlist_empty(&inode->i_dentry))
		return NULL;
	alias = hlist_entry(inode->i_dentry.first, struct dentry, d_u.d_alias);
	lockref_get(&alias->d_lockref);
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

static struct dentry *__d_find_alias(struct inode *inode)
{
	struct dentry *alias;

	if (S_ISDIR(inode->i_mode))
		return __d_find_any_alias(inode);

	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&alias->d_lock);
 		if (!d_unhashed(alias)) {
			dget_dlock(alias);
			spin_unlock(&alias->d_lock);
			return alias;
		}
		spin_unlock(&alias->d_lock);
	}
	return NULL;
}

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
 *  Caller MUST be holding rcu_read_lock() and be guaranteed
 *  that inode won't get freed until rcu_read_unlock().
 */
struct dentry *d_find_alias_rcu(struct inode *inode)
{
	struct hlist_head *l = &inode->i_dentry;
	struct dentry *de = NULL;

	spin_lock(&inode->i_lock);
	// ->i_dentry and ->i_rcu are colocated, but the latter won't be
	// used without having I_FREEING set, which means no aliases left
	if (likely(!(inode->i_state & I_FREEING) && !hlist_empty(l))) {
		if (S_ISDIR(inode->i_mode)) {
			de = hlist_entry(l->first, struct dentry, d_u.d_alias);
		} else {
			hlist_for_each_entry(de, l, d_u.d_alias)
				if (!d_unhashed(de))
					break;
		}
	}
	spin_unlock(&inode->i_lock);
	return de;
}

/*
 *	Try to kill dentries associated with this inode.
 * WARNING: you must own a reference to inode.
 */
void d_prune_aliases(struct inode *inode)
{
	LIST_HEAD(dispose);
	struct dentry *dentry;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&dentry->d_lock);
		if (!dentry->d_lockref.count)
			to_shrink_list(dentry, &dispose);
		spin_unlock(&dentry->d_lock);
	}
	spin_unlock(&inode->i_lock);
	shrink_dentry_list(&dispose);
}
EXPORT_SYMBOL(d_prune_aliases);

static inline void shrink_kill(struct dentry *victim)
{
	do {
		rcu_read_unlock();
		victim = __dentry_kill(victim);
		rcu_read_lock();
	} while (victim && lock_for_kill(victim));
	rcu_read_unlock();
	if (victim)
		spin_unlock(&victim->d_lock);
}

void shrink_dentry_list(struct list_head *list)
{
	while (!list_empty(list)) {
		struct dentry *dentry;

		dentry = list_entry(list->prev, struct dentry, d_lru);
		spin_lock(&dentry->d_lock);
		rcu_read_lock();
		if (!lock_for_kill(dentry)) {
			bool can_free;
			rcu_read_unlock();
			d_shrink_del(dentry);
			can_free = dentry->d_flags & DCACHE_DENTRY_KILLED;
			spin_unlock(&dentry->d_lock);
			if (can_free)
				dentry_free(dentry);
			continue;
		}
		d_shrink_del(dentry);
		shrink_kill(dentry);
	}
}

static enum lru_status dentry_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, void *arg)
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
		 * list_lru_add_obj and list_lru_del_obj. List movement in this file
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
		struct list_lru_one *lru, void *arg)
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
	do {
		LIST_HEAD(dispose);

		list_lru_walk(&sb->s_dentry_lru,
			dentry_lru_isolate_shrink, &dispose, 1024);
		shrink_dentry_list(&dispose);
	} while (list_lru_count(&sb->s_dentry_lru) > 0);
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
 *
 * The @enter() callbacks are called with d_lock held.
 */
static void d_walk(struct dentry *parent, void *data,
		   enum d_walk_ret (*enter)(void *, struct dentry *))
{
	struct dentry *this_parent, *dentry;
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
	dentry = d_first_child(this_parent);
resume:
	hlist_for_each_entry_from(dentry, d_sib) {
		if (unlikely(dentry->d_flags & DCACHE_DENTRY_CURSOR))
			continue;

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

		if (!hlist_empty(&dentry->d_children)) {
			spin_unlock(&this_parent->d_lock);
			spin_release(&dentry->d_lock.dep_map, _RET_IP_);
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
		dentry = this_parent;
		this_parent = dentry->d_parent;

		spin_unlock(&dentry->d_lock);
		spin_lock(&this_parent->d_lock);

		/* might go back up the wrong parent if we have had a rename. */
		if (need_seqretry(&rename_lock, seq))
			goto rename_retry;
		/* go into the first sibling still alive */
		hlist_for_each_entry_continue(dentry, d_sib) {
			if (likely(!(dentry->d_flags & DCACHE_DENTRY_KILLED))) {
				rcu_read_unlock();
				goto resume;
			}
		}
		goto ascend;
	}
	if (need_seqretry(&rename_lock, seq))
		goto rename_retry;
	rcu_read_unlock();

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

struct check_mount {
	struct vfsmount *mnt;
	unsigned int mounted;
};

/* locks: mount_locked_reader && dentry->d_lock */
static enum d_walk_ret path_check_mount(void *data, struct dentry *dentry)
{
	struct check_mount *info = data;
	struct path path = { .mnt = info->mnt, .dentry = dentry };

	if (likely(!d_mountpoint(dentry)))
		return D_WALK_CONTINUE;
	if (__path_is_mountpoint(&path)) {
		info->mounted = 1;
		return D_WALK_QUIT;
	}
	return D_WALK_CONTINUE;
}

/**
 * path_has_submounts - check for mounts over a dentry in the
 *                      current namespace.
 * @parent: path to check.
 *
 * Return true if the parent or its subdirectories contain
 * a mount point in the current namespace.
 */
int path_has_submounts(const struct path *parent)
{
	struct check_mount data = { .mnt = parent->mnt, .mounted = 0 };

	guard(mount_locked_reader)();
	d_walk(parent->dentry, &data, path_check_mount);

	return data.mounted;
}
EXPORT_SYMBOL(path_has_submounts);

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
	read_seqlock_excl(&rename_lock);
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
	read_sequnlock_excl(&rename_lock);
	return ret;
}

/*
 * Search the dentry child list of the specified parent,
 * and move any unused dentries to the end of the unused
 * list for prune_dcache(). We descend to the next level
 * whenever the d_children list is non-empty and continue
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
	union {
		long found;
		struct dentry *victim;
	};
	struct list_head dispose;
};

static enum d_walk_ret select_collect(void *_data, struct dentry *dentry)
{
	struct select_data *data = _data;
	enum d_walk_ret ret = D_WALK_CONTINUE;

	if (data->start == dentry)
		goto out;

	if (dentry->d_flags & DCACHE_SHRINK_LIST) {
		data->found++;
	} else if (!dentry->d_lockref.count) {
		to_shrink_list(dentry, &data->dispose);
		data->found++;
	} else if (dentry->d_lockref.count < 0) {
		data->found++;
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

static enum d_walk_ret select_collect2(void *_data, struct dentry *dentry)
{
	struct select_data *data = _data;
	enum d_walk_ret ret = D_WALK_CONTINUE;

	if (data->start == dentry)
		goto out;

	if (!dentry->d_lockref.count) {
		if (dentry->d_flags & DCACHE_SHRINK_LIST) {
			rcu_read_lock();
			data->victim = dentry;
			return D_WALK_QUIT;
		}
		to_shrink_list(dentry, &data->dispose);
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
		struct select_data data = {.start = parent};

		INIT_LIST_HEAD(&data.dispose);
		d_walk(parent, &data, select_collect);

		if (!list_empty(&data.dispose)) {
			shrink_dentry_list(&data.dispose);
			continue;
		}

		cond_resched();
		if (!data.found)
			break;
		data.victim = NULL;
		d_walk(parent, &data, select_collect2);
		if (data.victim) {
			spin_lock(&data.victim->d_lock);
			if (!lock_for_kill(data.victim)) {
				spin_unlock(&data.victim->d_lock);
				rcu_read_unlock();
			} else {
				shrink_kill(data.victim);
			}
		}
		if (!list_empty(&data.dispose))
			shrink_dentry_list(&data.dispose);
	}
}
EXPORT_SYMBOL(shrink_dcache_parent);

static enum d_walk_ret umount_check(void *_data, struct dentry *dentry)
{
	/* it has busy descendents; complain about those instead */
	if (!hlist_empty(&dentry->d_children))
		return D_WALK_CONTINUE;

	/* root with refcount 1 is fine */
	if (dentry == _data && dentry->d_lockref.count == 1)
		return D_WALK_CONTINUE;

	WARN(1, "BUG: Dentry %p{i=%lx,n=%pd} "
			" still in use (%d) [unmount of %s %s]\n",
		       dentry,
		       dentry->d_inode ?
		       dentry->d_inode->i_ino : 0UL,
		       dentry,
		       dentry->d_lockref.count,
		       dentry->d_sb->s_type->name,
		       dentry->d_sb->s_id);
	return D_WALK_CONTINUE;
}

static void do_one_tree(struct dentry *dentry)
{
	shrink_dcache_parent(dentry);
	d_walk(dentry, dentry, umount_check);
	d_drop(dentry);
	dput(dentry);
}

/*
 * destroy the dentries attached to a superblock on unmounting
 */
void shrink_dcache_for_umount(struct super_block *sb)
{
	struct dentry *dentry;

	rwsem_assert_held_write(&sb->s_umount);

	dentry = sb->s_root;
	sb->s_root = NULL;
	do_one_tree(dentry);

	while (!hlist_bl_empty(&sb->s_roots)) {
		dentry = dget(hlist_bl_entry(hlist_bl_first(&sb->s_roots), struct dentry, d_hash));
		do_one_tree(dentry);
	}
}

static enum d_walk_ret find_submount(void *_data, struct dentry *dentry)
{
	struct dentry **victim = _data;
	if (d_mountpoint(dentry)) {
		*victim = dget_dlock(dentry);
		return D_WALK_QUIT;
	}
	return D_WALK_CONTINUE;
}

/**
 * d_invalidate - detach submounts, prune dcache, and drop
 * @dentry: dentry to invalidate (aka detach, prune and drop)
 */
void d_invalidate(struct dentry *dentry)
{
	bool had_submounts = false;
	spin_lock(&dentry->d_lock);
	if (d_unhashed(dentry)) {
		spin_unlock(&dentry->d_lock);
		return;
	}
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);

	/* Negative dentries can be dropped without further checks */
	if (!dentry->d_inode)
		return;

	shrink_dcache_parent(dentry);
	for (;;) {
		struct dentry *victim = NULL;
		d_walk(dentry, &victim, find_submount);
		if (!victim) {
			if (had_submounts)
				shrink_dcache_parent(dentry);
			return;
		}
		had_submounts = true;
		detach_mounts(victim);
		dput(victim);
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
 
static struct dentry *__d_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;
	int err;

	dentry = kmem_cache_alloc_lru(dentry_cache, &sb->s_dentry_lru,
				      GFP_KERNEL);
	if (!dentry)
		return NULL;

	/*
	 * We guarantee that the inline name is always NUL-terminated.
	 * This way the memcpy() done by the name switching in rename
	 * will still always have a NUL at the end, even if we might
	 * be overwriting an internal NUL character
	 */
	dentry->d_shortname.string[DNAME_INLINE_LEN-1] = 0;
	if (unlikely(!name)) {
		name = &slash_name;
		dname = dentry->d_shortname.string;
	} else if (name->len > DNAME_INLINE_LEN-1) {
		size_t size = offsetof(struct external_name, name[1]);
		struct external_name *p = kmalloc(size + name->len,
						  GFP_KERNEL_ACCOUNT |
						  __GFP_RECLAIMABLE);
		if (!p) {
			kmem_cache_free(dentry_cache, dentry); 
			return NULL;
		}
		atomic_set(&p->count, 1);
		dname = p->name;
	} else  {
		dname = dentry->d_shortname.string;
	}	

	dentry->__d_name.len = name->len;
	dentry->__d_name.hash = name->hash;
	memcpy(dname, name->name, name->len);
	dname[name->len] = 0;

	/* Make sure we always see the terminating NUL character */
	smp_store_release(&dentry->__d_name.name, dname); /* ^^^ */

	dentry->d_flags = 0;
	lockref_init(&dentry->d_lockref);
	seqcount_spinlock_init(&dentry->d_seq, &dentry->d_lock);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	dentry->d_op = sb->__s_d_op;
	dentry->d_flags = sb->s_d_flags;
	dentry->d_fsdata = NULL;
	INIT_HLIST_BL_NODE(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_HLIST_HEAD(&dentry->d_children);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_HLIST_NODE(&dentry->d_sib);

	if (dentry->d_op && dentry->d_op->d_init) {
		err = dentry->d_op->d_init(dentry);
		if (err) {
			if (dname_external(dentry))
				kfree(external_name(dentry));
			kmem_cache_free(dentry_cache, dentry);
			return NULL;
		}
	}

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
	spin_lock(&parent->d_lock);
	/*
	 * don't need child lock because it is not subject
	 * to concurrency here
	 */
	dentry->d_parent = dget_dlock(parent);
	hlist_add_head(&dentry->d_sib, &parent->d_children);
	spin_unlock(&parent->d_lock);

	return dentry;
}
EXPORT_SYMBOL(d_alloc);

struct dentry *d_alloc_anon(struct super_block *sb)
{
	return __d_alloc(sb, NULL);
}
EXPORT_SYMBOL(d_alloc_anon);

struct dentry *d_alloc_cursor(struct dentry * parent)
{
	struct dentry *dentry = d_alloc_anon(parent->d_sb);
	if (dentry) {
		dentry->d_flags |= DCACHE_DENTRY_CURSOR;
		dentry->d_parent = dget(parent);
	}
	return dentry;
}

/**
 * d_alloc_pseudo - allocate a dentry (for lookup-less filesystems)
 * @sb: the superblock
 * @name: qstr of the name
 *
 * For a filesystem that just pins its dentries in memory and never
 * performs lookups at all, return an unhashed IS_ROOT dentry.
 * This is used for pipes, sockets et.al. - the stuff that should
 * never be anyone's children or parents.  Unlike all other
 * dentries, these will not have RCU delay between dropping the
 * last reference and freeing them.
 *
 * The only user is alloc_file_pseudo() and that's what should
 * be considered a public interface.  Don't use directly.
 */
struct dentry *d_alloc_pseudo(struct super_block *sb, const struct qstr *name)
{
	static const struct dentry_operations anon_ops = {
		.d_dname = simple_dname
	};
	struct dentry *dentry = __d_alloc(sb, name);
	if (likely(dentry)) {
		dentry->d_flags |= DCACHE_NORCU;
		/* d_op_flags(&anon_ops) is 0 */
		if (!dentry->d_op)
			dentry->d_op = &anon_ops;
	}
	return dentry;
}

struct dentry *d_alloc_name(struct dentry *parent, const char *name)
{
	struct qstr q;

	q.name = name;
	q.hash_len = hashlen_string(parent, name);
	return d_alloc(parent, &q);
}
EXPORT_SYMBOL(d_alloc_name);

#define DCACHE_OP_FLAGS \
	(DCACHE_OP_HASH | DCACHE_OP_COMPARE | DCACHE_OP_REVALIDATE | \
	 DCACHE_OP_WEAK_REVALIDATE | DCACHE_OP_DELETE | DCACHE_OP_PRUNE | \
	 DCACHE_OP_REAL)

static unsigned int d_op_flags(const struct dentry_operations *op)
{
	unsigned int flags = 0;
	if (op) {
		if (op->d_hash)
			flags |= DCACHE_OP_HASH;
		if (op->d_compare)
			flags |= DCACHE_OP_COMPARE;
		if (op->d_revalidate)
			flags |= DCACHE_OP_REVALIDATE;
		if (op->d_weak_revalidate)
			flags |= DCACHE_OP_WEAK_REVALIDATE;
		if (op->d_delete)
			flags |= DCACHE_OP_DELETE;
		if (op->d_prune)
			flags |= DCACHE_OP_PRUNE;
		if (op->d_real)
			flags |= DCACHE_OP_REAL;
	}
	return flags;
}

static void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
{
	unsigned int flags = d_op_flags(op);
	WARN_ON_ONCE(dentry->d_op);
	WARN_ON_ONCE(dentry->d_flags & DCACHE_OP_FLAGS);
	dentry->d_op = op;
	if (flags)
		dentry->d_flags |= flags;
}

void set_default_d_op(struct super_block *s, const struct dentry_operations *ops)
{
	unsigned int flags = d_op_flags(ops);
	s->__s_d_op = ops;
	s->s_d_flags = (s->s_d_flags & ~DCACHE_OP_FLAGS) | flags;
}
EXPORT_SYMBOL(set_default_d_op);

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
		if (unlikely(inode->i_op->get_link)) {
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
	WARN_ON(d_in_lookup(dentry));

	spin_lock(&dentry->d_lock);
	/*
	 * The negative counter only tracks dentries on the LRU. Don't dec if
	 * d_lru is on another list.
	 */
	if ((dentry->d_flags &
	     (DCACHE_LRU_LIST|DCACHE_SHRINK_LIST)) == DCACHE_LRU_LIST)
		this_cpu_dec(nr_dentry_negative);
	hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
	raw_write_seqcount_begin(&dentry->d_seq);
	__d_set_inode_and_type(dentry, inode, add_flags);
	raw_write_seqcount_end(&dentry->d_seq);
	fsnotify_update_flags(dentry);
	spin_unlock(&dentry->d_lock);
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
	if (inode) {
		security_d_instantiate(entry, inode);
		spin_lock(&inode->i_lock);
		__d_instantiate(entry, inode);
		spin_unlock(&inode->i_lock);
	}
}
EXPORT_SYMBOL(d_instantiate);

/*
 * This should be equivalent to d_instantiate() + unlock_new_inode(),
 * with lockdep-related part of unlock_new_inode() done before
 * anything else.  Use that instead of open-coding d_instantiate()/
 * unlock_new_inode() combinations.
 */
void d_instantiate_new(struct dentry *entry, struct inode *inode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));
	BUG_ON(!inode);
	lockdep_annotate_inode_mutex_key(inode);
	security_d_instantiate(entry, inode);
	spin_lock(&inode->i_lock);
	__d_instantiate(entry, inode);
	WARN_ON(!(inode->i_state & I_NEW));
	inode->i_state &= ~I_NEW & ~I_CREATING;
	/*
	 * Pairs with the barrier in prepare_to_wait_event() to make sure
	 * ___wait_var_event() either sees the bit cleared or
	 * waitqueue_active() check in wake_up_var() sees the waiter.
	 */
	smp_mb();
	inode_wake_up_bit(inode, __I_NEW);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(d_instantiate_new);

struct dentry *d_make_root(struct inode *root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = d_alloc_anon(root_inode->i_sb);
		if (res)
			d_instantiate(res, root_inode);
		else
			iput(root_inode);
	}
	return res;
}
EXPORT_SYMBOL(d_make_root);

static struct dentry *__d_obtain_alias(struct inode *inode, bool disconnected)
{
	struct super_block *sb;
	struct dentry *new, *res;

	if (!inode)
		return ERR_PTR(-ESTALE);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	sb = inode->i_sb;

	res = d_find_any_alias(inode); /* existing alias? */
	if (res)
		goto out;

	new = d_alloc_anon(sb);
	if (!new) {
		res = ERR_PTR(-ENOMEM);
		goto out;
	}

	security_d_instantiate(new, inode);
	spin_lock(&inode->i_lock);
	res = __d_find_any_alias(inode); /* recheck under lock */
	if (likely(!res)) { /* still no alias, attach a disconnected dentry */
		unsigned add_flags = d_flags_for_inode(inode);

		if (disconnected)
			add_flags |= DCACHE_DISCONNECTED;

		spin_lock(&new->d_lock);
		__d_set_inode_and_type(new, inode, add_flags);
		hlist_add_head(&new->d_u.d_alias, &inode->i_dentry);
		if (!disconnected) {
			hlist_bl_lock(&sb->s_roots);
			hlist_bl_add_head(&new->d_hash, &sb->s_roots);
			hlist_bl_unlock(&sb->s_roots);
		}
		spin_unlock(&new->d_lock);
		spin_unlock(&inode->i_lock);
		inode = NULL; /* consumed by new->d_inode */
		res = new;
	} else {
		spin_unlock(&inode->i_lock);
		dput(new);
	}

 out:
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
	return __d_obtain_alias(inode, true);
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
	return __d_obtain_alias(inode, false);
}
EXPORT_SYMBOL(d_obtain_root);

/**
 * d_add_ci - lookup or allocate new dentry with case-exact name
 * @dentry: the negative dentry that was passed to the parent's lookup func
 * @inode:  the inode case-insensitive lookup has found
 * @name:   the case-exact name to be associated with the returned dentry
 *
 * This is to avoid filling the dcache with case-insensitive names to the
 * same inode, only the actual correct case is stored in the dcache for
 * case-insensitive filesystems.
 *
 * For a case-insensitive lookup match and if the case-exact dentry
 * already exists in the dcache, use it and return it.
 *
 * If no entry exists with the exact case name, allocate new dentry with
 * the exact case, and return the spliced entry.
 */
struct dentry *d_add_ci(struct dentry *dentry, struct inode *inode,
			struct qstr *name)
{
	struct dentry *found, *res;

	/*
	 * First check if a dentry matching the name already exists,
	 * if not go ahead and create it now.
	 */
	found = d_hash_and_lookup(dentry->d_parent, name);
	if (found) {
		iput(inode);
		return found;
	}
	if (d_in_lookup(dentry)) {
		found = d_alloc_parallel(dentry->d_parent, name,
					dentry->d_wait);
		if (IS_ERR(found) || !d_in_lookup(found)) {
			iput(inode);
			return found;
		}
	} else {
		found = d_alloc(dentry->d_parent, name);
		if (!found) {
			iput(inode);
			return ERR_PTR(-ENOMEM);
		} 
	}
	res = d_splice_alias(inode, found);
	if (res) {
		d_lookup_done(found);
		dput(found);
		return res;
	}
	return found;
}
EXPORT_SYMBOL(d_add_ci);

/**
 * d_same_name - compare dentry name with case-exact name
 * @dentry: the negative dentry that was passed to the parent's lookup func
 * @parent: parent dentry
 * @name:   the case-exact name to be associated with the returned dentry
 *
 * Return: true if names are same, or false
 */
bool d_same_name(const struct dentry *dentry, const struct dentry *parent,
		 const struct qstr *name)
{
	if (likely(!(parent->d_flags & DCACHE_OP_COMPARE))) {
		if (dentry->d_name.len != name->len)
			return false;
		return dentry_cmp(dentry, name->name, name->len) == 0;
	}
	return parent->d_op->d_compare(dentry,
				       dentry->d_name.len, dentry->d_name.name,
				       name) == 0;
}
EXPORT_SYMBOL_GPL(d_same_name);

/*
 * This is __d_lookup_rcu() when the parent dentry has
 * DCACHE_OP_COMPARE, which makes things much nastier.
 */
static noinline struct dentry *__d_lookup_rcu_op_compare(
	const struct dentry *parent,
	const struct qstr *name,
	unsigned *seqp)
{
	u64 hashlen = name->hash_len;
	struct hlist_bl_head *b = d_hash(hashlen);
	struct hlist_bl_node *node;
	struct dentry *dentry;

	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		int tlen;
		const char *tname;
		unsigned seq;

seqretry:
		seq = raw_seqcount_begin(&dentry->d_seq);
		if (dentry->d_parent != parent)
			continue;
		if (d_unhashed(dentry))
			continue;
		if (dentry->d_name.hash != hashlen_hash(hashlen))
			continue;
		tlen = dentry->d_name.len;
		tname = dentry->d_name.name;
		/* we want a consistent (name,len) pair */
		if (read_seqcount_retry(&dentry->d_seq, seq)) {
			cpu_relax();
			goto seqretry;
		}
		if (parent->d_op->d_compare(dentry, tlen, tname, name) != 0)
			continue;
		*seqp = seq;
		return dentry;
	}
	return NULL;
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
	struct hlist_bl_head *b = d_hash(hashlen);
	struct hlist_bl_node *node;
	struct dentry *dentry;

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	if (unlikely(parent->d_flags & DCACHE_OP_COMPARE))
		return __d_lookup_rcu_op_compare(parent, name, seqp);

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
		 *
		 * Note that raw_seqcount_begin still *does* smp_rmb(), so
		 * we are still guaranteed NUL-termination of ->d_name.name.
		 */
		seq = raw_seqcount_begin(&dentry->d_seq);
		if (dentry->d_parent != parent)
			continue;
		if (d_unhashed(dentry))
			continue;
		if (dentry->d_name.hash_len != hashlen)
			continue;
		if (dentry_cmp(dentry, str, hashlen_len(hashlen)) != 0)
			continue;
		*seqp = seq;
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
	unsigned int hash = name->hash;
	struct hlist_bl_head *b = d_hash(hash);
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

		if (!d_same_name(dentry, parent, name))
			goto next;

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
	name->hash = full_name_hash(dir, name->name, name->len);
	if (dir->d_flags & DCACHE_OP_HASH) {
		int err = dir->d_op->d_hash(dir, name);
		if (unlikely(err < 0))
			return ERR_PTR(err);
	}
	return d_lookup(dir, name);
}

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
	struct inode *inode = dentry->d_inode;

	spin_lock(&inode->i_lock);
	spin_lock(&dentry->d_lock);
	/*
	 * Are we the only user?
	 */
	if (dentry->d_lockref.count == 1) {
		if (dentry_negative_policy)
			__d_drop(dentry);
		dentry->d_flags &= ~DCACHE_CANT_MOUNT;
		dentry_unlink_inode(dentry);
	} else {
		__d_drop(dentry);
		spin_unlock(&dentry->d_lock);
		spin_unlock(&inode->i_lock);
	}
}
EXPORT_SYMBOL(d_delete);

static void __d_rehash(struct dentry *entry)
{
	struct hlist_bl_head *b = d_hash(entry->d_name.hash);

	hlist_bl_lock(b);
	hlist_bl_add_head_rcu(&entry->d_hash, b);
	hlist_bl_unlock(b);
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
	__d_rehash(entry);
	spin_unlock(&entry->d_lock);
}
EXPORT_SYMBOL(d_rehash);

static inline unsigned start_dir_add(struct inode *dir)
{
	preempt_disable_nested();
	for (;;) {
		unsigned n = READ_ONCE(dir->i_dir_seq);
		if (!(n & 1) && try_cmpxchg(&dir->i_dir_seq, &n, n + 1))
			return n;
		cpu_relax();
	}
}

static inline void end_dir_add(struct inode *dir, unsigned int n,
			       wait_queue_head_t *d_wait)
{
	smp_store_release(&dir->i_dir_seq, n + 2);
	preempt_enable_nested();
	if (wq_has_sleeper(d_wait))
		wake_up_all(d_wait);
}

static void d_wait_lookup(struct dentry *dentry)
{
	if (d_in_lookup(dentry)) {
		DECLARE_WAITQUEUE(wait, current);
		add_wait_queue(dentry->d_wait, &wait);
		do {
			set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock(&dentry->d_lock);
			schedule();
			spin_lock(&dentry->d_lock);
		} while (d_in_lookup(dentry));
	}
}

struct dentry *d_alloc_parallel(struct dentry *parent,
				const struct qstr *name,
				wait_queue_head_t *wq)
{
	unsigned int hash = name->hash;
	struct hlist_bl_head *b = in_lookup_hash(parent, hash);
	struct hlist_bl_node *node;
	struct dentry *new = __d_alloc(parent->d_sb, name);
	struct dentry *dentry;
	unsigned seq, r_seq, d_seq;

	if (unlikely(!new))
		return ERR_PTR(-ENOMEM);

	new->d_flags |= DCACHE_PAR_LOOKUP;
	spin_lock(&parent->d_lock);
	new->d_parent = dget_dlock(parent);
	hlist_add_head(&new->d_sib, &parent->d_children);
	if (parent->d_flags & DCACHE_DISCONNECTED)
		new->d_flags |= DCACHE_DISCONNECTED;
	spin_unlock(&parent->d_lock);

retry:
	rcu_read_lock();
	seq = smp_load_acquire(&parent->d_inode->i_dir_seq);
	r_seq = read_seqbegin(&rename_lock);
	dentry = __d_lookup_rcu(parent, name, &d_seq);
	if (unlikely(dentry)) {
		if (!lockref_get_not_dead(&dentry->d_lockref)) {
			rcu_read_unlock();
			goto retry;
		}
		if (read_seqcount_retry(&dentry->d_seq, d_seq)) {
			rcu_read_unlock();
			dput(dentry);
			goto retry;
		}
		rcu_read_unlock();
		dput(new);
		return dentry;
	}
	if (unlikely(read_seqretry(&rename_lock, r_seq))) {
		rcu_read_unlock();
		goto retry;
	}

	if (unlikely(seq & 1)) {
		rcu_read_unlock();
		goto retry;
	}

	hlist_bl_lock(b);
	if (unlikely(READ_ONCE(parent->d_inode->i_dir_seq) != seq)) {
		hlist_bl_unlock(b);
		rcu_read_unlock();
		goto retry;
	}
	/*
	 * No changes for the parent since the beginning of d_lookup().
	 * Since all removals from the chain happen with hlist_bl_lock(),
	 * any potential in-lookup matches are going to stay here until
	 * we unlock the chain.  All fields are stable in everything
	 * we encounter.
	 */
	hlist_bl_for_each_entry(dentry, node, b, d_u.d_in_lookup_hash) {
		if (dentry->d_name.hash != hash)
			continue;
		if (dentry->d_parent != parent)
			continue;
		if (!d_same_name(dentry, parent, name))
			continue;
		hlist_bl_unlock(b);
		/* now we can try to grab a reference */
		if (!lockref_get_not_dead(&dentry->d_lockref)) {
			rcu_read_unlock();
			goto retry;
		}

		rcu_read_unlock();
		/*
		 * somebody is likely to be still doing lookup for it;
		 * wait for them to finish
		 */
		spin_lock(&dentry->d_lock);
		d_wait_lookup(dentry);
		/*
		 * it's not in-lookup anymore; in principle we should repeat
		 * everything from dcache lookup, but it's likely to be what
		 * d_lookup() would've found anyway.  If it is, just return it;
		 * otherwise we really have to repeat the whole thing.
		 */
		if (unlikely(dentry->d_name.hash != hash))
			goto mismatch;
		if (unlikely(dentry->d_parent != parent))
			goto mismatch;
		if (unlikely(d_unhashed(dentry)))
			goto mismatch;
		if (unlikely(!d_same_name(dentry, parent, name)))
			goto mismatch;
		/* OK, it *is* a hashed match; return it */
		spin_unlock(&dentry->d_lock);
		dput(new);
		return dentry;
	}
	rcu_read_unlock();
	new->d_wait = wq;
	hlist_bl_add_head(&new->d_u.d_in_lookup_hash, b);
	hlist_bl_unlock(b);
	return new;
mismatch:
	spin_unlock(&dentry->d_lock);
	dput(dentry);
	goto retry;
}
EXPORT_SYMBOL(d_alloc_parallel);

/*
 * - Unhash the dentry
 * - Retrieve and clear the waitqueue head in dentry
 * - Return the waitqueue head
 */
static wait_queue_head_t *__d_lookup_unhash(struct dentry *dentry)
{
	wait_queue_head_t *d_wait;
	struct hlist_bl_head *b;

	lockdep_assert_held(&dentry->d_lock);

	b = in_lookup_hash(dentry->d_parent, dentry->d_name.hash);
	hlist_bl_lock(b);
	dentry->d_flags &= ~DCACHE_PAR_LOOKUP;
	__hlist_bl_del(&dentry->d_u.d_in_lookup_hash);
	d_wait = dentry->d_wait;
	dentry->d_wait = NULL;
	hlist_bl_unlock(b);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_LIST_HEAD(&dentry->d_lru);
	return d_wait;
}

void __d_lookup_unhash_wake(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	wake_up_all(__d_lookup_unhash(dentry));
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(__d_lookup_unhash_wake);

/* inode->i_lock held if inode is non-NULL */

static inline void __d_add(struct dentry *dentry, struct inode *inode,
			   const struct dentry_operations *ops)
{
	wait_queue_head_t *d_wait;
	struct inode *dir = NULL;
	unsigned n;
	spin_lock(&dentry->d_lock);
	if (unlikely(d_in_lookup(dentry))) {
		dir = dentry->d_parent->d_inode;
		n = start_dir_add(dir);
		d_wait = __d_lookup_unhash(dentry);
	}
	if (unlikely(ops))
		d_set_d_op(dentry, ops);
	if (inode) {
		unsigned add_flags = d_flags_for_inode(inode);
		hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
		raw_write_seqcount_begin(&dentry->d_seq);
		__d_set_inode_and_type(dentry, inode, add_flags);
		raw_write_seqcount_end(&dentry->d_seq);
		fsnotify_update_flags(dentry);
	}
	__d_rehash(dentry);
	if (dir)
		end_dir_add(dir, n, d_wait);
	spin_unlock(&dentry->d_lock);
	if (inode)
		spin_unlock(&inode->i_lock);
}

/**
 * d_add - add dentry to hash queues
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */

void d_add(struct dentry *entry, struct inode *inode)
{
	if (inode) {
		security_d_instantiate(entry, inode);
		spin_lock(&inode->i_lock);
	}
	__d_add(entry, inode, NULL);
}
EXPORT_SYMBOL(d_add);

static void swap_names(struct dentry *dentry, struct dentry *target)
{
	if (unlikely(dname_external(target))) {
		if (unlikely(dname_external(dentry))) {
			/*
			 * Both external: swap the pointers
			 */
			swap(target->__d_name.name, dentry->__d_name.name);
		} else {
			/*
			 * dentry:internal, target:external.  Steal target's
			 * storage and make target internal.
			 */
			dentry->__d_name.name = target->__d_name.name;
			target->d_shortname = dentry->d_shortname;
			target->__d_name.name = target->d_shortname.string;
		}
	} else {
		if (unlikely(dname_external(dentry))) {
			/*
			 * dentry:external, target:internal.  Give dentry's
			 * storage to target and make dentry internal
			 */
			target->__d_name.name = dentry->__d_name.name;
			dentry->d_shortname = target->d_shortname;
			dentry->__d_name.name = dentry->d_shortname.string;
		} else {
			/*
			 * Both are internal.
			 */
			for (int i = 0; i < DNAME_INLINE_WORDS; i++)
				swap(dentry->d_shortname.words[i],
				     target->d_shortname.words[i]);
		}
	}
	swap(dentry->__d_name.hash_len, target->__d_name.hash_len);
}

static void copy_name(struct dentry *dentry, struct dentry *target)
{
	struct external_name *old_name = NULL;
	if (unlikely(dname_external(dentry)))
		old_name = external_name(dentry);
	if (unlikely(dname_external(target))) {
		atomic_inc(&external_name(target)->count);
		dentry->__d_name = target->__d_name;
	} else {
		dentry->d_shortname = target->d_shortname;
		dentry->__d_name.name = dentry->d_shortname.string;
		dentry->__d_name.hash_len = target->__d_name.hash_len;
	}
	if (old_name && likely(atomic_dec_and_test(&old_name->count)))
		kfree_rcu(old_name, head);
}

/*
 * __d_move - move a dentry
 * @dentry: entry to move
 * @target: new dentry
 * @exchange: exchange the two dentries
 *
 * Update the dcache to reflect the move of a file name. Negative dcache
 * entries should not be moved in this way. Caller must hold rename_lock, the
 * i_rwsem of the source and target directories (exclusively), and the sb->
 * s_vfs_rename_mutex if they differ. See lock_rename().
 */
static void __d_move(struct dentry *dentry, struct dentry *target,
		     bool exchange)
{
	struct dentry *old_parent, *p;
	wait_queue_head_t *d_wait;
	struct inode *dir = NULL;
	unsigned n;

	WARN_ON(!dentry->d_inode);
	if (WARN_ON(dentry == target))
		return;

	BUG_ON(d_ancestor(target, dentry));
	old_parent = dentry->d_parent;
	p = d_ancestor(old_parent, target);
	if (IS_ROOT(dentry)) {
		BUG_ON(p);
		spin_lock(&target->d_parent->d_lock);
	} else if (!p) {
		/* target is not a descendent of dentry->d_parent */
		spin_lock(&target->d_parent->d_lock);
		spin_lock_nested(&old_parent->d_lock, DENTRY_D_LOCK_NESTED);
	} else {
		BUG_ON(p == dentry);
		spin_lock(&old_parent->d_lock);
		if (p != target)
			spin_lock_nested(&target->d_parent->d_lock,
					DENTRY_D_LOCK_NESTED);
	}
	spin_lock_nested(&dentry->d_lock, 2);
	spin_lock_nested(&target->d_lock, 3);

	if (unlikely(d_in_lookup(target))) {
		dir = target->d_parent->d_inode;
		n = start_dir_add(dir);
		d_wait = __d_lookup_unhash(target);
	}

	write_seqcount_begin(&dentry->d_seq);
	write_seqcount_begin_nested(&target->d_seq, DENTRY_D_LOCK_NESTED);

	/* unhash both */
	if (!d_unhashed(dentry))
		___d_drop(dentry);
	if (!d_unhashed(target))
		___d_drop(target);

	/* ... and switch them in the tree */
	dentry->d_parent = target->d_parent;
	if (!exchange) {
		copy_name(dentry, target);
		target->d_hash.pprev = NULL;
		dentry->d_parent->d_lockref.count++;
		if (dentry != old_parent) /* wasn't IS_ROOT */
			WARN_ON(!--old_parent->d_lockref.count);
	} else {
		target->d_parent = old_parent;
		swap_names(dentry, target);
		if (!hlist_unhashed(&target->d_sib))
			__hlist_del(&target->d_sib);
		hlist_add_head(&target->d_sib, &target->d_parent->d_children);
		__d_rehash(target);
		fsnotify_update_flags(target);
	}
	if (!hlist_unhashed(&dentry->d_sib))
		__hlist_del(&dentry->d_sib);
	hlist_add_head(&dentry->d_sib, &dentry->d_parent->d_children);
	__d_rehash(dentry);
	fsnotify_update_flags(dentry);
	fscrypt_handle_d_move(dentry);

	write_seqcount_end(&target->d_seq);
	write_seqcount_end(&dentry->d_seq);

	if (dir)
		end_dir_add(dir, n, d_wait);

	if (dentry->d_parent != old_parent)
		spin_unlock(&dentry->d_parent->d_lock);
	if (dentry != old_parent)
		spin_unlock(&old_parent->d_lock);
	spin_unlock(&target->d_lock);
	spin_unlock(&dentry->d_lock);
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
EXPORT_SYMBOL(d_exchange);

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
 * dentry->d_parent->d_inode->i_rwsem, and rename_lock
 *
 * Note: If ever the locking in lock_rename() changes, then please
 * remember to update this too...
 */
static int __d_unalias(struct dentry *dentry, struct dentry *alias)
{
	struct mutex *m1 = NULL;
	struct rw_semaphore *m2 = NULL;
	int ret = -ESTALE;

	/* If alias and dentry share a parent, then no extra locks required */
	if (alias->d_parent == dentry->d_parent)
		goto out_unalias;

	/* See lock_rename() */
	if (!mutex_trylock(&dentry->d_sb->s_vfs_rename_mutex))
		goto out_err;
	m1 = &dentry->d_sb->s_vfs_rename_mutex;
	if (!inode_trylock_shared(alias->d_parent->d_inode))
		goto out_err;
	m2 = &alias->d_parent->d_inode->i_rwsem;
out_unalias:
	if (alias->d_op && alias->d_op->d_unalias_trylock &&
	    !alias->d_op->d_unalias_trylock(alias))
		goto out_err;
	__d_move(alias, dentry, false);
	if (alias->d_op && alias->d_op->d_unalias_unlock)
		alias->d_op->d_unalias_unlock(alias);
	ret = 0;
out_err:
	if (m2)
		up_read(m2);
	if (m1)
		mutex_unlock(m1);
	return ret;
}

struct dentry *d_splice_alias_ops(struct inode *inode, struct dentry *dentry,
				  const struct dentry_operations *ops)
{
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	BUG_ON(!d_unhashed(dentry));

	if (!inode)
		goto out;

	security_d_instantiate(dentry, inode);
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
				struct dentry *old_parent = dget(new->d_parent);
				int err = __d_unalias(dentry, new);
				write_sequnlock(&rename_lock);
				if (err) {
					dput(new);
					new = ERR_PTR(err);
				}
				dput(old_parent);
			} else {
				__d_move(new, dentry, false);
				write_sequnlock(&rename_lock);
			}
			iput(inode);
			return new;
		}
	}
out:
	__d_add(dentry, inode, ops);
	return NULL;
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
	return d_splice_alias_ops(inode, dentry, NULL);
}
EXPORT_SYMBOL(d_splice_alias);

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
 * Returns true if new_dentry is a subdirectory of the parent (at any depth).
 * Returns false otherwise.
 * Caller must ensure that "new_dentry" is pinned before calling is_subdir()
 */
  
bool is_subdir(struct dentry *new_dentry, struct dentry *old_dentry)
{
	bool subdir;
	unsigned seq;

	if (new_dentry == old_dentry)
		return true;

	/* Access d_parent under rcu as d_move() may change it. */
	rcu_read_lock();
	seq = read_seqbegin(&rename_lock);
	subdir = d_ancestor(old_dentry, new_dentry);
	 /* Try lockless once... */
	if (read_seqretry(&rename_lock, seq)) {
		/* ...else acquire lock for progress even on deep chains. */
		read_seqlock_excl(&rename_lock);
		subdir = d_ancestor(old_dentry, new_dentry);
		read_sequnlock_excl(&rename_lock);
	}
	rcu_read_unlock();
	return subdir;
}
EXPORT_SYMBOL(is_subdir);

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
	d_walk(parent, parent, d_genocide_kill);
}

void d_mark_tmpfile(struct file *file, struct inode *inode)
{
	struct dentry *dentry = file->f_path.dentry;

	BUG_ON(dname_external(dentry) ||
		!hlist_unhashed(&dentry->d_u.d_alias) ||
		!d_unlinked(dentry));
	spin_lock(&dentry->d_parent->d_lock);
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	dentry->__d_name.len = sprintf(dentry->d_shortname.string, "#%llu",
				(unsigned long long)inode->i_ino);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dentry->d_parent->d_lock);
}
EXPORT_SYMBOL(d_mark_tmpfile);

void d_tmpfile(struct file *file, struct inode *inode)
{
	struct dentry *dentry = file->f_path.dentry;

	inode_dec_link_count(inode);
	d_mark_tmpfile(file, inode);
	d_instantiate(dentry, inode);
}
EXPORT_SYMBOL(d_tmpfile);

/*
 * Obtain inode number of the parent dentry.
 */
ino_t d_parent_ino(struct dentry *dentry)
{
	struct dentry *parent;
	struct inode *iparent;
	unsigned seq;
	ino_t ret;

	scoped_guard(rcu) {
		seq = raw_seqcount_begin(&dentry->d_seq);
		parent = READ_ONCE(dentry->d_parent);
		iparent = d_inode_rcu(parent);
		if (likely(iparent)) {
			ret = iparent->i_ino;
			if (!read_seqcount_retry(&dentry->d_seq, seq))
				return ret;
		}
	}

	spin_lock(&dentry->d_lock);
	ret = dentry->d_parent->d_inode->i_ino;
	spin_unlock(&dentry->d_lock);
	return ret;
}
EXPORT_SYMBOL(d_parent_ino);

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
					HASH_EARLY | HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					0);
	d_hash_shift = 32 - d_hash_shift;

	runtime_const_init(shift, d_hash_shift);
	runtime_const_init(ptr, dentry_hashtable);
}

static void __init dcache_init(void)
{
	/*
	 * A constructor could be added for stable state like the lists,
	 * but it is probably not worth it because of the cache nature
	 * of the dcache.
	 */
	dentry_cache = KMEM_CACHE_USERCOPY(dentry,
		SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|SLAB_ACCOUNT,
		d_shortname.string);

	/* Hash may have been set up in dcache_init_early */
	if (!hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					0);
	d_hash_shift = 32 - d_hash_shift;

	runtime_const_init(shift, d_hash_shift);
	runtime_const_init(ptr, dentry_hashtable);
}

/* SLAB cache for __getname() consumers */
struct kmem_cache *names_cachep __ro_after_init;
EXPORT_SYMBOL(names_cachep);

void __init vfs_caches_init_early(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(in_lookup_hashtable); i++)
		INIT_HLIST_BL_HEAD(&in_lookup_hashtable[i]);

	dcache_init_early();
	inode_init_early();
}

void __init vfs_caches_init(void)
{
	names_cachep = kmem_cache_create_usercopy("names_cache", PATH_MAX, 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, 0, PATH_MAX, NULL);

	dcache_init();
	inode_init();
	files_init();
	files_maxfiles_init();
	mnt_init();
	bdev_cache_init();
	chrdev_init();
}
