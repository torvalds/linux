// SPDX-License-Identifier: GPL-2.0
/*
 * The NFSD open file cache.
 *
 * (c) 2015 - Jeff Layton <jeff.layton@primarydata.com>
 *
 * An nfsd_file object is a per-file collection of open state that binds
 * together:
 *   - a struct file *
 *   - a user credential
 *   - a network namespace
 *   - a read-ahead context
 *   - monitoring for writeback errors
 *
 * nfsd_file objects are reference-counted. Consumers acquire a new
 * object via the nfsd_file_acquire API. They manage their interest in
 * the acquired object, and hence the object's reference count, via
 * nfsd_file_get and nfsd_file_put. There are two varieties of nfsd_file
 * object:
 *
 *  * non-garbage-collected: When a consumer wants to precisely control
 *    the lifetime of a file's open state, it acquires a non-garbage-
 *    collected nfsd_file. The final nfsd_file_put releases the open
 *    state immediately.
 *
 *  * garbage-collected: When a consumer does not control the lifetime
 *    of open state, it acquires a garbage-collected nfsd_file. The
 *    final nfsd_file_put allows the open state to linger for a period
 *    during which it may be re-used.
 */

#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/list_lru.h>
#include <linux/fsnotify_backend.h>
#include <linux/fsnotify.h>
#include <linux/seq_file.h>
#include <linux/rhashtable.h>

#include "vfs.h"
#include "nfsd.h"
#include "nfsfh.h"
#include "netns.h"
#include "filecache.h"
#include "trace.h"

#define NFSD_LAUNDRETTE_DELAY		     (2 * HZ)

#define NFSD_FILE_CACHE_UP		     (0)

/* We only care about NFSD_MAY_READ/WRITE for this cache */
#define NFSD_FILE_MAY_MASK	(NFSD_MAY_READ|NFSD_MAY_WRITE)

static DEFINE_PER_CPU(unsigned long, nfsd_file_cache_hits);
static DEFINE_PER_CPU(unsigned long, nfsd_file_acquisitions);
static DEFINE_PER_CPU(unsigned long, nfsd_file_releases);
static DEFINE_PER_CPU(unsigned long, nfsd_file_total_age);
static DEFINE_PER_CPU(unsigned long, nfsd_file_evictions);

struct nfsd_fcache_disposal {
	spinlock_t lock;
	struct list_head freeme;
};

static struct kmem_cache		*nfsd_file_slab;
static struct kmem_cache		*nfsd_file_mark_slab;
static struct list_lru			nfsd_file_lru;
static unsigned long			nfsd_file_flags;
static struct fsnotify_group		*nfsd_file_fsnotify_group;
static struct delayed_work		nfsd_filecache_laundrette;
static struct rhltable			nfsd_file_rhltable
						____cacheline_aligned_in_smp;

static bool
nfsd_match_cred(const struct cred *c1, const struct cred *c2)
{
	int i;

	if (!uid_eq(c1->fsuid, c2->fsuid))
		return false;
	if (!gid_eq(c1->fsgid, c2->fsgid))
		return false;
	if (c1->group_info == NULL || c2->group_info == NULL)
		return c1->group_info == c2->group_info;
	if (c1->group_info->ngroups != c2->group_info->ngroups)
		return false;
	for (i = 0; i < c1->group_info->ngroups; i++) {
		if (!gid_eq(c1->group_info->gid[i], c2->group_info->gid[i]))
			return false;
	}
	return true;
}

static const struct rhashtable_params nfsd_file_rhash_params = {
	.key_len		= sizeof_field(struct nfsd_file, nf_inode),
	.key_offset		= offsetof(struct nfsd_file, nf_inode),
	.head_offset		= offsetof(struct nfsd_file, nf_rlist),

	/*
	 * Start with a single page hash table to reduce resizing churn
	 * on light workloads.
	 */
	.min_size		= 256,
	.automatic_shrinking	= true,
};

static void
nfsd_file_schedule_laundrette(void)
{
	if (test_bit(NFSD_FILE_CACHE_UP, &nfsd_file_flags))
		queue_delayed_work(system_wq, &nfsd_filecache_laundrette,
				   NFSD_LAUNDRETTE_DELAY);
}

static void
nfsd_file_slab_free(struct rcu_head *rcu)
{
	struct nfsd_file *nf = container_of(rcu, struct nfsd_file, nf_rcu);

	put_cred(nf->nf_cred);
	kmem_cache_free(nfsd_file_slab, nf);
}

static void
nfsd_file_mark_free(struct fsnotify_mark *mark)
{
	struct nfsd_file_mark *nfm = container_of(mark, struct nfsd_file_mark,
						  nfm_mark);

	kmem_cache_free(nfsd_file_mark_slab, nfm);
}

static struct nfsd_file_mark *
nfsd_file_mark_get(struct nfsd_file_mark *nfm)
{
	if (!refcount_inc_not_zero(&nfm->nfm_ref))
		return NULL;
	return nfm;
}

static void
nfsd_file_mark_put(struct nfsd_file_mark *nfm)
{
	if (refcount_dec_and_test(&nfm->nfm_ref)) {
		fsnotify_destroy_mark(&nfm->nfm_mark, nfsd_file_fsnotify_group);
		fsnotify_put_mark(&nfm->nfm_mark);
	}
}

static struct nfsd_file_mark *
nfsd_file_mark_find_or_create(struct nfsd_file *nf, struct inode *inode)
{
	int			err;
	struct fsnotify_mark	*mark;
	struct nfsd_file_mark	*nfm = NULL, *new;

	do {
		fsnotify_group_lock(nfsd_file_fsnotify_group);
		mark = fsnotify_find_inode_mark(inode,
						nfsd_file_fsnotify_group);
		if (mark) {
			nfm = nfsd_file_mark_get(container_of(mark,
						 struct nfsd_file_mark,
						 nfm_mark));
			fsnotify_group_unlock(nfsd_file_fsnotify_group);
			if (nfm) {
				fsnotify_put_mark(mark);
				break;
			}
			/* Avoid soft lockup race with nfsd_file_mark_put() */
			fsnotify_destroy_mark(mark, nfsd_file_fsnotify_group);
			fsnotify_put_mark(mark);
		} else {
			fsnotify_group_unlock(nfsd_file_fsnotify_group);
		}

		/* allocate a new nfm */
		new = kmem_cache_alloc(nfsd_file_mark_slab, GFP_KERNEL);
		if (!new)
			return NULL;
		fsnotify_init_mark(&new->nfm_mark, nfsd_file_fsnotify_group);
		new->nfm_mark.mask = FS_ATTRIB|FS_DELETE_SELF;
		refcount_set(&new->nfm_ref, 1);

		err = fsnotify_add_inode_mark(&new->nfm_mark, inode, 0);

		/*
		 * If the add was successful, then return the object.
		 * Otherwise, we need to put the reference we hold on the
		 * nfm_mark. The fsnotify code will take a reference and put
		 * it on failure, so we can't just free it directly. It's also
		 * not safe to call fsnotify_destroy_mark on it as the
		 * mark->group will be NULL. Thus, we can't let the nfm_ref
		 * counter drive the destruction at this point.
		 */
		if (likely(!err))
			nfm = new;
		else
			fsnotify_put_mark(&new->nfm_mark);
	} while (unlikely(err == -EEXIST));

	return nfm;
}

static struct nfsd_file *
nfsd_file_alloc(struct net *net, struct inode *inode, unsigned char need,
		bool want_gc)
{
	struct nfsd_file *nf;

	nf = kmem_cache_alloc(nfsd_file_slab, GFP_KERNEL);
	if (unlikely(!nf))
		return NULL;

	INIT_LIST_HEAD(&nf->nf_lru);
	nf->nf_birthtime = ktime_get();
	nf->nf_file = NULL;
	nf->nf_cred = get_current_cred();
	nf->nf_net = net;
	nf->nf_flags = want_gc ?
		BIT(NFSD_FILE_HASHED) | BIT(NFSD_FILE_PENDING) | BIT(NFSD_FILE_GC) :
		BIT(NFSD_FILE_HASHED) | BIT(NFSD_FILE_PENDING);
	nf->nf_inode = inode;
	refcount_set(&nf->nf_ref, 1);
	nf->nf_may = need;
	nf->nf_mark = NULL;
	return nf;
}

/**
 * nfsd_file_check_write_error - check for writeback errors on a file
 * @nf: nfsd_file to check for writeback errors
 *
 * Check whether a nfsd_file has an unseen error. Reset the write
 * verifier if so.
 */
static void
nfsd_file_check_write_error(struct nfsd_file *nf)
{
	struct file *file = nf->nf_file;

	if ((file->f_mode & FMODE_WRITE) &&
	    filemap_check_wb_err(file->f_mapping, READ_ONCE(file->f_wb_err)))
		nfsd_reset_write_verifier(net_generic(nf->nf_net, nfsd_net_id));
}

static void
nfsd_file_hash_remove(struct nfsd_file *nf)
{
	trace_nfsd_file_unhash(nf);
	rhltable_remove(&nfsd_file_rhltable, &nf->nf_rlist,
			nfsd_file_rhash_params);
}

static bool
nfsd_file_unhash(struct nfsd_file *nf)
{
	if (test_and_clear_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		nfsd_file_hash_remove(nf);
		return true;
	}
	return false;
}

static void
nfsd_file_free(struct nfsd_file *nf)
{
	s64 age = ktime_to_ms(ktime_sub(ktime_get(), nf->nf_birthtime));

	trace_nfsd_file_free(nf);

	this_cpu_inc(nfsd_file_releases);
	this_cpu_add(nfsd_file_total_age, age);

	nfsd_file_unhash(nf);
	if (nf->nf_mark)
		nfsd_file_mark_put(nf->nf_mark);
	if (nf->nf_file) {
		nfsd_file_check_write_error(nf);
		nfsd_filp_close(nf->nf_file);
	}

	/*
	 * If this item is still linked via nf_lru, that's a bug.
	 * WARN and leak it to preserve system stability.
	 */
	if (WARN_ON_ONCE(!list_empty(&nf->nf_lru)))
		return;

	call_rcu(&nf->nf_rcu, nfsd_file_slab_free);
}

static bool
nfsd_file_check_writeback(struct nfsd_file *nf)
{
	struct file *file = nf->nf_file;
	struct address_space *mapping;

	/* File not open for write? */
	if (!(file->f_mode & FMODE_WRITE))
		return false;

	/*
	 * Some filesystems (e.g. NFS) flush all dirty data on close.
	 * On others, there is no need to wait for writeback.
	 */
	if (!(file_inode(file)->i_sb->s_export_op->flags & EXPORT_OP_FLUSH_ON_CLOSE))
		return false;

	mapping = file->f_mapping;
	return mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) ||
		mapping_tagged(mapping, PAGECACHE_TAG_WRITEBACK);
}


static bool nfsd_file_lru_add(struct nfsd_file *nf)
{
	set_bit(NFSD_FILE_REFERENCED, &nf->nf_flags);
	if (list_lru_add_obj(&nfsd_file_lru, &nf->nf_lru)) {
		trace_nfsd_file_lru_add(nf);
		return true;
	}
	return false;
}

static bool nfsd_file_lru_remove(struct nfsd_file *nf)
{
	if (list_lru_del_obj(&nfsd_file_lru, &nf->nf_lru)) {
		trace_nfsd_file_lru_del(nf);
		return true;
	}
	return false;
}

struct nfsd_file *
nfsd_file_get(struct nfsd_file *nf)
{
	if (nf && refcount_inc_not_zero(&nf->nf_ref))
		return nf;
	return NULL;
}

/**
 * nfsd_file_put - put the reference to a nfsd_file
 * @nf: nfsd_file of which to put the reference
 *
 * Put a reference to a nfsd_file. In the non-GC case, we just put the
 * reference immediately. In the GC case, if the reference would be
 * the last one, the put it on the LRU instead to be cleaned up later.
 */
void
nfsd_file_put(struct nfsd_file *nf)
{
	might_sleep();
	trace_nfsd_file_put(nf);

	if (test_bit(NFSD_FILE_GC, &nf->nf_flags) &&
	    test_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		/*
		 * If this is the last reference (nf_ref == 1), then try to
		 * transfer it to the LRU.
		 */
		if (refcount_dec_not_one(&nf->nf_ref))
			return;

		/* Try to add it to the LRU.  If that fails, decrement. */
		if (nfsd_file_lru_add(nf)) {
			/* If it's still hashed, we're done */
			if (test_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
				nfsd_file_schedule_laundrette();
				return;
			}

			/*
			 * We're racing with unhashing, so try to remove it from
			 * the LRU. If removal fails, then someone else already
			 * has our reference.
			 */
			if (!nfsd_file_lru_remove(nf))
				return;
		}
	}
	if (refcount_dec_and_test(&nf->nf_ref))
		nfsd_file_free(nf);
}

static void
nfsd_file_dispose_list(struct list_head *dispose)
{
	struct nfsd_file *nf;

	while (!list_empty(dispose)) {
		nf = list_first_entry(dispose, struct nfsd_file, nf_lru);
		list_del_init(&nf->nf_lru);
		nfsd_file_free(nf);
	}
}

/**
 * nfsd_file_dispose_list_delayed - move list of dead files to net's freeme list
 * @dispose: list of nfsd_files to be disposed
 *
 * Transfers each file to the "freeme" list for its nfsd_net, to eventually
 * be disposed of by the per-net garbage collector.
 */
static void
nfsd_file_dispose_list_delayed(struct list_head *dispose)
{
	while(!list_empty(dispose)) {
		struct nfsd_file *nf = list_first_entry(dispose,
						struct nfsd_file, nf_lru);
		struct nfsd_net *nn = net_generic(nf->nf_net, nfsd_net_id);
		struct nfsd_fcache_disposal *l = nn->fcache_disposal;

		spin_lock(&l->lock);
		list_move_tail(&nf->nf_lru, &l->freeme);
		spin_unlock(&l->lock);
		svc_wake_up(nn->nfsd_serv);
	}
}

/**
 * nfsd_file_net_dispose - deal with nfsd_files waiting to be disposed.
 * @nn: nfsd_net in which to find files to be disposed.
 *
 * When files held open for nfsv3 are removed from the filecache, whether
 * due to memory pressure or garbage collection, they are queued to
 * a per-net-ns queue.  This function completes the disposal, either
 * directly or by waking another nfsd thread to help with the work.
 */
void nfsd_file_net_dispose(struct nfsd_net *nn)
{
	struct nfsd_fcache_disposal *l = nn->fcache_disposal;

	if (!list_empty(&l->freeme)) {
		LIST_HEAD(dispose);
		int i;

		spin_lock(&l->lock);
		for (i = 0; i < 8 && !list_empty(&l->freeme); i++)
			list_move(l->freeme.next, &dispose);
		spin_unlock(&l->lock);
		if (!list_empty(&l->freeme))
			/* Wake up another thread to share the work
			 * *before* doing any actual disposing.
			 */
			svc_wake_up(nn->nfsd_serv);
		nfsd_file_dispose_list(&dispose);
	}
}

/**
 * nfsd_file_lru_cb - Examine an entry on the LRU list
 * @item: LRU entry to examine
 * @lru: controlling LRU
 * @lock: LRU list lock (unused)
 * @arg: dispose list
 *
 * Return values:
 *   %LRU_REMOVED: @item was removed from the LRU
 *   %LRU_ROTATE: @item is to be moved to the LRU tail
 *   %LRU_SKIP: @item cannot be evicted
 */
static enum lru_status
nfsd_file_lru_cb(struct list_head *item, struct list_lru_one *lru,
		 spinlock_t *lock, void *arg)
	__releases(lock)
	__acquires(lock)
{
	struct list_head *head = arg;
	struct nfsd_file *nf = list_entry(item, struct nfsd_file, nf_lru);

	/* We should only be dealing with GC entries here */
	WARN_ON_ONCE(!test_bit(NFSD_FILE_GC, &nf->nf_flags));

	/*
	 * Don't throw out files that are still undergoing I/O or
	 * that have uncleared errors pending.
	 */
	if (nfsd_file_check_writeback(nf)) {
		trace_nfsd_file_gc_writeback(nf);
		return LRU_SKIP;
	}

	/* If it was recently added to the list, skip it */
	if (test_and_clear_bit(NFSD_FILE_REFERENCED, &nf->nf_flags)) {
		trace_nfsd_file_gc_referenced(nf);
		return LRU_ROTATE;
	}

	/*
	 * Put the reference held on behalf of the LRU. If it wasn't the last
	 * one, then just remove it from the LRU and ignore it.
	 */
	if (!refcount_dec_and_test(&nf->nf_ref)) {
		trace_nfsd_file_gc_in_use(nf);
		list_lru_isolate(lru, &nf->nf_lru);
		return LRU_REMOVED;
	}

	/* Refcount went to zero. Unhash it and queue it to the dispose list */
	nfsd_file_unhash(nf);
	list_lru_isolate_move(lru, &nf->nf_lru, head);
	this_cpu_inc(nfsd_file_evictions);
	trace_nfsd_file_gc_disposed(nf);
	return LRU_REMOVED;
}

static void
nfsd_file_gc(void)
{
	LIST_HEAD(dispose);
	unsigned long ret;

	ret = list_lru_walk(&nfsd_file_lru, nfsd_file_lru_cb,
			    &dispose, list_lru_count(&nfsd_file_lru));
	trace_nfsd_file_gc_removed(ret, list_lru_count(&nfsd_file_lru));
	nfsd_file_dispose_list_delayed(&dispose);
}

static void
nfsd_file_gc_worker(struct work_struct *work)
{
	nfsd_file_gc();
	if (list_lru_count(&nfsd_file_lru))
		nfsd_file_schedule_laundrette();
}

static unsigned long
nfsd_file_lru_count(struct shrinker *s, struct shrink_control *sc)
{
	return list_lru_count(&nfsd_file_lru);
}

static unsigned long
nfsd_file_lru_scan(struct shrinker *s, struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	unsigned long ret;

	ret = list_lru_shrink_walk(&nfsd_file_lru, sc,
				   nfsd_file_lru_cb, &dispose);
	trace_nfsd_file_shrinker_removed(ret, list_lru_count(&nfsd_file_lru));
	nfsd_file_dispose_list_delayed(&dispose);
	return ret;
}

static struct shrinker *nfsd_file_shrinker;

/**
 * nfsd_file_cond_queue - conditionally unhash and queue a nfsd_file
 * @nf: nfsd_file to attempt to queue
 * @dispose: private list to queue successfully-put objects
 *
 * Unhash an nfsd_file, try to get a reference to it, and then put that
 * reference. If it's the last reference, queue it to the dispose list.
 */
static void
nfsd_file_cond_queue(struct nfsd_file *nf, struct list_head *dispose)
	__must_hold(RCU)
{
	int decrement = 1;

	/* If we raced with someone else unhashing, ignore it */
	if (!nfsd_file_unhash(nf))
		return;

	/* If we can't get a reference, ignore it */
	if (!nfsd_file_get(nf))
		return;

	/* Extra decrement if we remove from the LRU */
	if (nfsd_file_lru_remove(nf))
		++decrement;

	/* If refcount goes to 0, then put on the dispose list */
	if (refcount_sub_and_test(decrement, &nf->nf_ref)) {
		list_add(&nf->nf_lru, dispose);
		trace_nfsd_file_closing(nf);
	}
}

/**
 * nfsd_file_queue_for_close: try to close out any open nfsd_files for an inode
 * @inode:   inode on which to close out nfsd_files
 * @dispose: list on which to gather nfsd_files to close out
 *
 * An nfsd_file represents a struct file being held open on behalf of nfsd.
 * An open file however can block other activity (such as leases), or cause
 * undesirable behavior (e.g. spurious silly-renames when reexporting NFS).
 *
 * This function is intended to find open nfsd_files when this sort of
 * conflicting access occurs and then attempt to close those files out.
 *
 * Populates the dispose list with entries that have already had their
 * refcounts go to zero. The actual free of an nfsd_file can be expensive,
 * so we leave it up to the caller whether it wants to wait or not.
 */
static void
nfsd_file_queue_for_close(struct inode *inode, struct list_head *dispose)
{
	struct rhlist_head *tmp, *list;
	struct nfsd_file *nf;

	rcu_read_lock();
	list = rhltable_lookup(&nfsd_file_rhltable, &inode,
			       nfsd_file_rhash_params);
	rhl_for_each_entry_rcu(nf, tmp, list, nf_rlist) {
		if (!test_bit(NFSD_FILE_GC, &nf->nf_flags))
			continue;
		nfsd_file_cond_queue(nf, dispose);
	}
	rcu_read_unlock();
}

/**
 * nfsd_file_close_inode - attempt a delayed close of a nfsd_file
 * @inode: inode of the file to attempt to remove
 *
 * Close out any open nfsd_files that can be reaped for @inode. The
 * actual freeing is deferred to the dispose_list_delayed infrastructure.
 *
 * This is used by the fsnotify callbacks and setlease notifier.
 */
static void
nfsd_file_close_inode(struct inode *inode)
{
	LIST_HEAD(dispose);

	nfsd_file_queue_for_close(inode, &dispose);
	nfsd_file_dispose_list_delayed(&dispose);
}

/**
 * nfsd_file_close_inode_sync - attempt to forcibly close a nfsd_file
 * @inode: inode of the file to attempt to remove
 *
 * Close out any open nfsd_files that can be reaped for @inode. The
 * nfsd_files are closed out synchronously.
 *
 * This is called from nfsd_rename and nfsd_unlink to avoid silly-renames
 * when reexporting NFS.
 */
void
nfsd_file_close_inode_sync(struct inode *inode)
{
	struct nfsd_file *nf;
	LIST_HEAD(dispose);

	trace_nfsd_file_close(inode);

	nfsd_file_queue_for_close(inode, &dispose);
	while (!list_empty(&dispose)) {
		nf = list_first_entry(&dispose, struct nfsd_file, nf_lru);
		list_del_init(&nf->nf_lru);
		nfsd_file_free(nf);
	}
}

static int
nfsd_file_lease_notifier_call(struct notifier_block *nb, unsigned long arg,
			    void *data)
{
	struct file_lock *fl = data;

	/* Only close files for F_SETLEASE leases */
	if (fl->c.flc_flags & FL_LEASE)
		nfsd_file_close_inode(file_inode(fl->c.flc_file));
	return 0;
}

static struct notifier_block nfsd_file_lease_notifier = {
	.notifier_call = nfsd_file_lease_notifier_call,
};

static int
nfsd_file_fsnotify_handle_event(struct fsnotify_mark *mark, u32 mask,
				struct inode *inode, struct inode *dir,
				const struct qstr *name, u32 cookie)
{
	if (WARN_ON_ONCE(!inode))
		return 0;

	trace_nfsd_file_fsnotify_handle_event(inode, mask);

	/* Should be no marks on non-regular files */
	if (!S_ISREG(inode->i_mode)) {
		WARN_ON_ONCE(1);
		return 0;
	}

	/* don't close files if this was not the last link */
	if (mask & FS_ATTRIB) {
		if (inode->i_nlink)
			return 0;
	}

	nfsd_file_close_inode(inode);
	return 0;
}


static const struct fsnotify_ops nfsd_file_fsnotify_ops = {
	.handle_inode_event = nfsd_file_fsnotify_handle_event,
	.free_mark = nfsd_file_mark_free,
};

int
nfsd_file_cache_init(void)
{
	int ret;

	lockdep_assert_held(&nfsd_mutex);
	if (test_and_set_bit(NFSD_FILE_CACHE_UP, &nfsd_file_flags) == 1)
		return 0;

	ret = rhltable_init(&nfsd_file_rhltable, &nfsd_file_rhash_params);
	if (ret)
		return ret;

	ret = -ENOMEM;
	nfsd_file_slab = KMEM_CACHE(nfsd_file, 0);
	if (!nfsd_file_slab) {
		pr_err("nfsd: unable to create nfsd_file_slab\n");
		goto out_err;
	}

	nfsd_file_mark_slab = KMEM_CACHE(nfsd_file_mark, 0);
	if (!nfsd_file_mark_slab) {
		pr_err("nfsd: unable to create nfsd_file_mark_slab\n");
		goto out_err;
	}

	ret = list_lru_init(&nfsd_file_lru);
	if (ret) {
		pr_err("nfsd: failed to init nfsd_file_lru: %d\n", ret);
		goto out_err;
	}

	nfsd_file_shrinker = shrinker_alloc(0, "nfsd-filecache");
	if (!nfsd_file_shrinker) {
		ret = -ENOMEM;
		pr_err("nfsd: failed to allocate nfsd_file_shrinker\n");
		goto out_lru;
	}

	nfsd_file_shrinker->count_objects = nfsd_file_lru_count;
	nfsd_file_shrinker->scan_objects = nfsd_file_lru_scan;
	nfsd_file_shrinker->seeks = 1;

	shrinker_register(nfsd_file_shrinker);

	ret = lease_register_notifier(&nfsd_file_lease_notifier);
	if (ret) {
		pr_err("nfsd: unable to register lease notifier: %d\n", ret);
		goto out_shrinker;
	}

	nfsd_file_fsnotify_group = fsnotify_alloc_group(&nfsd_file_fsnotify_ops,
							FSNOTIFY_GROUP_NOFS);
	if (IS_ERR(nfsd_file_fsnotify_group)) {
		pr_err("nfsd: unable to create fsnotify group: %ld\n",
			PTR_ERR(nfsd_file_fsnotify_group));
		ret = PTR_ERR(nfsd_file_fsnotify_group);
		nfsd_file_fsnotify_group = NULL;
		goto out_notifier;
	}

	INIT_DELAYED_WORK(&nfsd_filecache_laundrette, nfsd_file_gc_worker);
out:
	return ret;
out_notifier:
	lease_unregister_notifier(&nfsd_file_lease_notifier);
out_shrinker:
	shrinker_free(nfsd_file_shrinker);
out_lru:
	list_lru_destroy(&nfsd_file_lru);
out_err:
	kmem_cache_destroy(nfsd_file_slab);
	nfsd_file_slab = NULL;
	kmem_cache_destroy(nfsd_file_mark_slab);
	nfsd_file_mark_slab = NULL;
	rhltable_destroy(&nfsd_file_rhltable);
	goto out;
}

/**
 * __nfsd_file_cache_purge: clean out the cache for shutdown
 * @net: net-namespace to shut down the cache (may be NULL)
 *
 * Walk the nfsd_file cache and close out any that match @net. If @net is NULL,
 * then close out everything. Called when an nfsd instance is being shut down,
 * and when the exports table is flushed.
 */
static void
__nfsd_file_cache_purge(struct net *net)
{
	struct rhashtable_iter iter;
	struct nfsd_file *nf;
	LIST_HEAD(dispose);

	rhltable_walk_enter(&nfsd_file_rhltable, &iter);
	do {
		rhashtable_walk_start(&iter);

		nf = rhashtable_walk_next(&iter);
		while (!IS_ERR_OR_NULL(nf)) {
			if (!net || nf->nf_net == net)
				nfsd_file_cond_queue(nf, &dispose);
			nf = rhashtable_walk_next(&iter);
		}

		rhashtable_walk_stop(&iter);
	} while (nf == ERR_PTR(-EAGAIN));
	rhashtable_walk_exit(&iter);

	nfsd_file_dispose_list(&dispose);
}

static struct nfsd_fcache_disposal *
nfsd_alloc_fcache_disposal(void)
{
	struct nfsd_fcache_disposal *l;

	l = kmalloc(sizeof(*l), GFP_KERNEL);
	if (!l)
		return NULL;
	spin_lock_init(&l->lock);
	INIT_LIST_HEAD(&l->freeme);
	return l;
}

static void
nfsd_free_fcache_disposal(struct nfsd_fcache_disposal *l)
{
	nfsd_file_dispose_list(&l->freeme);
	kfree(l);
}

static void
nfsd_free_fcache_disposal_net(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct nfsd_fcache_disposal *l = nn->fcache_disposal;

	nfsd_free_fcache_disposal(l);
}

int
nfsd_file_cache_start_net(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	nn->fcache_disposal = nfsd_alloc_fcache_disposal();
	return nn->fcache_disposal ? 0 : -ENOMEM;
}

/**
 * nfsd_file_cache_purge - Remove all cache items associated with @net
 * @net: target net namespace
 *
 */
void
nfsd_file_cache_purge(struct net *net)
{
	lockdep_assert_held(&nfsd_mutex);
	if (test_bit(NFSD_FILE_CACHE_UP, &nfsd_file_flags) == 1)
		__nfsd_file_cache_purge(net);
}

void
nfsd_file_cache_shutdown_net(struct net *net)
{
	nfsd_file_cache_purge(net);
	nfsd_free_fcache_disposal_net(net);
}

void
nfsd_file_cache_shutdown(void)
{
	int i;

	lockdep_assert_held(&nfsd_mutex);
	if (test_and_clear_bit(NFSD_FILE_CACHE_UP, &nfsd_file_flags) == 0)
		return;

	lease_unregister_notifier(&nfsd_file_lease_notifier);
	shrinker_free(nfsd_file_shrinker);
	/*
	 * make sure all callers of nfsd_file_lru_cb are done before
	 * calling nfsd_file_cache_purge
	 */
	cancel_delayed_work_sync(&nfsd_filecache_laundrette);
	__nfsd_file_cache_purge(NULL);
	list_lru_destroy(&nfsd_file_lru);
	rcu_barrier();
	fsnotify_put_group(nfsd_file_fsnotify_group);
	nfsd_file_fsnotify_group = NULL;
	kmem_cache_destroy(nfsd_file_slab);
	nfsd_file_slab = NULL;
	fsnotify_wait_marks_destroyed();
	kmem_cache_destroy(nfsd_file_mark_slab);
	nfsd_file_mark_slab = NULL;
	rhltable_destroy(&nfsd_file_rhltable);

	for_each_possible_cpu(i) {
		per_cpu(nfsd_file_cache_hits, i) = 0;
		per_cpu(nfsd_file_acquisitions, i) = 0;
		per_cpu(nfsd_file_releases, i) = 0;
		per_cpu(nfsd_file_total_age, i) = 0;
		per_cpu(nfsd_file_evictions, i) = 0;
	}
}

static struct nfsd_file *
nfsd_file_lookup_locked(const struct net *net, const struct cred *cred,
			struct inode *inode, unsigned char need,
			bool want_gc)
{
	struct rhlist_head *tmp, *list;
	struct nfsd_file *nf;

	list = rhltable_lookup(&nfsd_file_rhltable, &inode,
			       nfsd_file_rhash_params);
	rhl_for_each_entry_rcu(nf, tmp, list, nf_rlist) {
		if (nf->nf_may != need)
			continue;
		if (nf->nf_net != net)
			continue;
		if (!nfsd_match_cred(nf->nf_cred, cred))
			continue;
		if (test_bit(NFSD_FILE_GC, &nf->nf_flags) != want_gc)
			continue;
		if (test_bit(NFSD_FILE_HASHED, &nf->nf_flags) == 0)
			continue;

		if (!nfsd_file_get(nf))
			continue;
		return nf;
	}
	return NULL;
}

/**
 * nfsd_file_is_cached - are there any cached open files for this inode?
 * @inode: inode to check
 *
 * The lookup matches inodes in all net namespaces and is atomic wrt
 * nfsd_file_acquire().
 *
 * Return values:
 *   %true: filecache contains at least one file matching this inode
 *   %false: filecache contains no files matching this inode
 */
bool
nfsd_file_is_cached(struct inode *inode)
{
	struct rhlist_head *tmp, *list;
	struct nfsd_file *nf;
	bool ret = false;

	rcu_read_lock();
	list = rhltable_lookup(&nfsd_file_rhltable, &inode,
			       nfsd_file_rhash_params);
	rhl_for_each_entry_rcu(nf, tmp, list, nf_rlist)
		if (test_bit(NFSD_FILE_GC, &nf->nf_flags)) {
			ret = true;
			break;
		}
	rcu_read_unlock();

	trace_nfsd_file_is_cached(inode, (int)ret);
	return ret;
}

static __be32
nfsd_file_do_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		     unsigned int may_flags, struct file *file,
		     struct nfsd_file **pnf, bool want_gc)
{
	unsigned char need = may_flags & NFSD_FILE_MAY_MASK;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_file *new, *nf;
	bool stale_retry = true;
	bool open_retry = true;
	struct inode *inode;
	__be32 status;
	int ret;

retry:
	status = fh_verify(rqstp, fhp, S_IFREG,
				may_flags|NFSD_MAY_OWNER_OVERRIDE);
	if (status != nfs_ok)
		return status;
	inode = d_inode(fhp->fh_dentry);

	rcu_read_lock();
	nf = nfsd_file_lookup_locked(net, current_cred(), inode, need, want_gc);
	rcu_read_unlock();

	if (nf) {
		/*
		 * If the nf is on the LRU then it holds an extra reference
		 * that must be put if it's removed. It had better not be
		 * the last one however, since we should hold another.
		 */
		if (nfsd_file_lru_remove(nf))
			WARN_ON_ONCE(refcount_dec_and_test(&nf->nf_ref));
		goto wait_for_construction;
	}

	new = nfsd_file_alloc(net, inode, need, want_gc);
	if (!new) {
		status = nfserr_jukebox;
		goto out;
	}

	rcu_read_lock();
	spin_lock(&inode->i_lock);
	nf = nfsd_file_lookup_locked(net, current_cred(), inode, need, want_gc);
	if (unlikely(nf)) {
		spin_unlock(&inode->i_lock);
		rcu_read_unlock();
		nfsd_file_slab_free(&new->nf_rcu);
		goto wait_for_construction;
	}
	nf = new;
	ret = rhltable_insert(&nfsd_file_rhltable, &nf->nf_rlist,
			      nfsd_file_rhash_params);
	spin_unlock(&inode->i_lock);
	rcu_read_unlock();
	if (likely(ret == 0))
		goto open_file;

	if (ret == -EEXIST)
		goto retry;
	trace_nfsd_file_insert_err(rqstp, inode, may_flags, ret);
	status = nfserr_jukebox;
	goto construction_err;

wait_for_construction:
	wait_on_bit(&nf->nf_flags, NFSD_FILE_PENDING, TASK_UNINTERRUPTIBLE);

	/* Did construction of this file fail? */
	if (!test_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		trace_nfsd_file_cons_err(rqstp, inode, may_flags, nf);
		if (!open_retry) {
			status = nfserr_jukebox;
			goto construction_err;
		}
		open_retry = false;
		fh_put(fhp);
		goto retry;
	}
	this_cpu_inc(nfsd_file_cache_hits);

	status = nfserrno(nfsd_open_break_lease(file_inode(nf->nf_file), may_flags));
	if (status != nfs_ok) {
		nfsd_file_put(nf);
		nf = NULL;
	}

out:
	if (status == nfs_ok) {
		this_cpu_inc(nfsd_file_acquisitions);
		nfsd_file_check_write_error(nf);
		*pnf = nf;
	}
	trace_nfsd_file_acquire(rqstp, inode, may_flags, nf, status);
	return status;

open_file:
	trace_nfsd_file_alloc(nf);
	nf->nf_mark = nfsd_file_mark_find_or_create(nf, inode);
	if (nf->nf_mark) {
		if (file) {
			get_file(file);
			nf->nf_file = file;
			status = nfs_ok;
			trace_nfsd_file_opened(nf, status);
		} else {
			ret = nfsd_open_verified(rqstp, fhp, may_flags,
						 &nf->nf_file);
			if (ret == -EOPENSTALE && stale_retry) {
				stale_retry = false;
				nfsd_file_unhash(nf);
				clear_and_wake_up_bit(NFSD_FILE_PENDING,
						      &nf->nf_flags);
				if (refcount_dec_and_test(&nf->nf_ref))
					nfsd_file_free(nf);
				nf = NULL;
				fh_put(fhp);
				goto retry;
			}
			status = nfserrno(ret);
			trace_nfsd_file_open(nf, status);
		}
	} else
		status = nfserr_jukebox;
	/*
	 * If construction failed, or we raced with a call to unlink()
	 * then unhash.
	 */
	if (status != nfs_ok || inode->i_nlink == 0)
		nfsd_file_unhash(nf);
	clear_and_wake_up_bit(NFSD_FILE_PENDING, &nf->nf_flags);
	if (status == nfs_ok)
		goto out;

construction_err:
	if (refcount_dec_and_test(&nf->nf_ref))
		nfsd_file_free(nf);
	nf = NULL;
	goto out;
}

/**
 * nfsd_file_acquire_gc - Get a struct nfsd_file with an open file
 * @rqstp: the RPC transaction being executed
 * @fhp: the NFS filehandle of the file to be opened
 * @may_flags: NFSD_MAY_ settings for the file
 * @pnf: OUT: new or found "struct nfsd_file" object
 *
 * The nfsd_file object returned by this API is reference-counted
 * and garbage-collected. The object is retained for a few
 * seconds after the final nfsd_file_put() in case the caller
 * wants to re-use it.
 *
 * Return values:
 *   %nfs_ok - @pnf points to an nfsd_file with its reference
 *   count boosted.
 *
 * On error, an nfsstat value in network byte order is returned.
 */
__be32
nfsd_file_acquire_gc(struct svc_rqst *rqstp, struct svc_fh *fhp,
		     unsigned int may_flags, struct nfsd_file **pnf)
{
	return nfsd_file_do_acquire(rqstp, fhp, may_flags, NULL, pnf, true);
}

/**
 * nfsd_file_acquire - Get a struct nfsd_file with an open file
 * @rqstp: the RPC transaction being executed
 * @fhp: the NFS filehandle of the file to be opened
 * @may_flags: NFSD_MAY_ settings for the file
 * @pnf: OUT: new or found "struct nfsd_file" object
 *
 * The nfsd_file_object returned by this API is reference-counted
 * but not garbage-collected. The object is unhashed after the
 * final nfsd_file_put().
 *
 * Return values:
 *   %nfs_ok - @pnf points to an nfsd_file with its reference
 *   count boosted.
 *
 * On error, an nfsstat value in network byte order is returned.
 */
__be32
nfsd_file_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct nfsd_file **pnf)
{
	return nfsd_file_do_acquire(rqstp, fhp, may_flags, NULL, pnf, false);
}

/**
 * nfsd_file_acquire_opened - Get a struct nfsd_file using existing open file
 * @rqstp: the RPC transaction being executed
 * @fhp: the NFS filehandle of the file just created
 * @may_flags: NFSD_MAY_ settings for the file
 * @file: cached, already-open file (may be NULL)
 * @pnf: OUT: new or found "struct nfsd_file" object
 *
 * Acquire a nfsd_file object that is not GC'ed. If one doesn't already exist,
 * and @file is non-NULL, use it to instantiate a new nfsd_file instead of
 * opening a new one.
 *
 * Return values:
 *   %nfs_ok - @pnf points to an nfsd_file with its reference
 *   count boosted.
 *
 * On error, an nfsstat value in network byte order is returned.
 */
__be32
nfsd_file_acquire_opened(struct svc_rqst *rqstp, struct svc_fh *fhp,
			 unsigned int may_flags, struct file *file,
			 struct nfsd_file **pnf)
{
	return nfsd_file_do_acquire(rqstp, fhp, may_flags, file, pnf, false);
}

/*
 * Note that fields may be added, removed or reordered in the future. Programs
 * scraping this file for info should test the labels to ensure they're
 * getting the correct field.
 */
int nfsd_file_cache_stats_show(struct seq_file *m, void *v)
{
	unsigned long releases = 0, evictions = 0;
	unsigned long hits = 0, acquisitions = 0;
	unsigned int i, count = 0, buckets = 0;
	unsigned long lru = 0, total_age = 0;

	/* Serialize with server shutdown */
	mutex_lock(&nfsd_mutex);
	if (test_bit(NFSD_FILE_CACHE_UP, &nfsd_file_flags) == 1) {
		struct bucket_table *tbl;
		struct rhashtable *ht;

		lru = list_lru_count(&nfsd_file_lru);

		rcu_read_lock();
		ht = &nfsd_file_rhltable.ht;
		count = atomic_read(&ht->nelems);
		tbl = rht_dereference_rcu(ht->tbl, ht);
		buckets = tbl->size;
		rcu_read_unlock();
	}
	mutex_unlock(&nfsd_mutex);

	for_each_possible_cpu(i) {
		hits += per_cpu(nfsd_file_cache_hits, i);
		acquisitions += per_cpu(nfsd_file_acquisitions, i);
		releases += per_cpu(nfsd_file_releases, i);
		total_age += per_cpu(nfsd_file_total_age, i);
		evictions += per_cpu(nfsd_file_evictions, i);
	}

	seq_printf(m, "total inodes:  %u\n", count);
	seq_printf(m, "hash buckets:  %u\n", buckets);
	seq_printf(m, "lru entries:   %lu\n", lru);
	seq_printf(m, "cache hits:    %lu\n", hits);
	seq_printf(m, "acquisitions:  %lu\n", acquisitions);
	seq_printf(m, "releases:      %lu\n", releases);
	seq_printf(m, "evictions:     %lu\n", evictions);
	if (releases)
		seq_printf(m, "mean age (ms): %ld\n", total_age / releases);
	else
		seq_printf(m, "mean age (ms): -\n");
	return 0;
}
