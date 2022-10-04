/*
 * Open file cache.
 *
 * (c) 2015 - Jeff Layton <jeff.layton@primarydata.com>
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
static DEFINE_PER_CPU(unsigned long, nfsd_file_pages_flushed);
static DEFINE_PER_CPU(unsigned long, nfsd_file_evictions);

struct nfsd_fcache_disposal {
	struct work_struct work;
	spinlock_t lock;
	struct list_head freeme;
};

static struct workqueue_struct *nfsd_filecache_wq __read_mostly;

static struct kmem_cache		*nfsd_file_slab;
static struct kmem_cache		*nfsd_file_mark_slab;
static struct list_lru			nfsd_file_lru;
static unsigned long			nfsd_file_flags;
static struct fsnotify_group		*nfsd_file_fsnotify_group;
static struct delayed_work		nfsd_filecache_laundrette;
static struct rhashtable		nfsd_file_rhash_tbl
						____cacheline_aligned_in_smp;

enum nfsd_file_lookup_type {
	NFSD_FILE_KEY_INODE,
	NFSD_FILE_KEY_FULL,
};

struct nfsd_file_lookup_key {
	struct inode			*inode;
	struct net			*net;
	const struct cred		*cred;
	unsigned char			need;
	enum nfsd_file_lookup_type	type;
};

/*
 * The returned hash value is based solely on the address of an in-code
 * inode, a pointer to a slab-allocated object. The entropy in such a
 * pointer is concentrated in its middle bits.
 */
static u32 nfsd_file_inode_hash(const struct inode *inode, u32 seed)
{
	unsigned long ptr = (unsigned long)inode;
	u32 k;

	k = ptr >> L1_CACHE_SHIFT;
	k &= 0x00ffffff;
	return jhash2(&k, 1, seed);
}

/**
 * nfsd_file_key_hashfn - Compute the hash value of a lookup key
 * @data: key on which to compute the hash value
 * @len: rhash table's key_len parameter (unused)
 * @seed: rhash table's random seed of the day
 *
 * Return value:
 *   Computed 32-bit hash value
 */
static u32 nfsd_file_key_hashfn(const void *data, u32 len, u32 seed)
{
	const struct nfsd_file_lookup_key *key = data;

	return nfsd_file_inode_hash(key->inode, seed);
}

/**
 * nfsd_file_obj_hashfn - Compute the hash value of an nfsd_file
 * @data: object on which to compute the hash value
 * @len: rhash table's key_len parameter (unused)
 * @seed: rhash table's random seed of the day
 *
 * Return value:
 *   Computed 32-bit hash value
 */
static u32 nfsd_file_obj_hashfn(const void *data, u32 len, u32 seed)
{
	const struct nfsd_file *nf = data;

	return nfsd_file_inode_hash(nf->nf_inode, seed);
}

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

/**
 * nfsd_file_obj_cmpfn - Match a cache item against search criteria
 * @arg: search criteria
 * @ptr: cache item to check
 *
 * Return values:
 *   %0 - Item matches search criteria
 *   %1 - Item does not match search criteria
 */
static int nfsd_file_obj_cmpfn(struct rhashtable_compare_arg *arg,
			       const void *ptr)
{
	const struct nfsd_file_lookup_key *key = arg->key;
	const struct nfsd_file *nf = ptr;

	switch (key->type) {
	case NFSD_FILE_KEY_INODE:
		if (nf->nf_inode != key->inode)
			return 1;
		break;
	case NFSD_FILE_KEY_FULL:
		if (nf->nf_inode != key->inode)
			return 1;
		if (nf->nf_may != key->need)
			return 1;
		if (nf->nf_net != key->net)
			return 1;
		if (!nfsd_match_cred(nf->nf_cred, key->cred))
			return 1;
		if (test_bit(NFSD_FILE_HASHED, &nf->nf_flags) == 0)
			return 1;
		break;
	}
	return 0;
}

static const struct rhashtable_params nfsd_file_rhash_params = {
	.key_len		= sizeof_field(struct nfsd_file, nf_inode),
	.key_offset		= offsetof(struct nfsd_file, nf_inode),
	.head_offset		= offsetof(struct nfsd_file, nf_rhash),
	.hashfn			= nfsd_file_key_hashfn,
	.obj_hashfn		= nfsd_file_obj_hashfn,
	.obj_cmpfn		= nfsd_file_obj_cmpfn,
	/* Reduce resizing churn on light workloads */
	.min_size		= 512,		/* buckets */
	.automatic_shrinking	= true,
};

static void
nfsd_file_schedule_laundrette(void)
{
	if ((atomic_read(&nfsd_file_rhash_tbl.nelems) == 0) ||
	    test_bit(NFSD_FILE_CACHE_UP, &nfsd_file_flags) == 0)
		return;

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
		mark = fsnotify_find_mark(&inode->i_fsnotify_marks,
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
nfsd_file_alloc(struct nfsd_file_lookup_key *key, unsigned int may)
{
	struct nfsd_file *nf;

	nf = kmem_cache_alloc(nfsd_file_slab, GFP_KERNEL);
	if (nf) {
		INIT_LIST_HEAD(&nf->nf_lru);
		nf->nf_birthtime = ktime_get();
		nf->nf_file = NULL;
		nf->nf_cred = get_current_cred();
		nf->nf_net = key->net;
		nf->nf_flags = 0;
		__set_bit(NFSD_FILE_HASHED, &nf->nf_flags);
		__set_bit(NFSD_FILE_PENDING, &nf->nf_flags);
		nf->nf_inode = key->inode;
		/* nf_ref is pre-incremented for hash table */
		refcount_set(&nf->nf_ref, 2);
		nf->nf_may = key->need;
		nf->nf_mark = NULL;
	}
	return nf;
}

static bool
nfsd_file_free(struct nfsd_file *nf)
{
	s64 age = ktime_to_ms(ktime_sub(ktime_get(), nf->nf_birthtime));
	bool flush = false;

	this_cpu_inc(nfsd_file_releases);
	this_cpu_add(nfsd_file_total_age, age);

	trace_nfsd_file_put_final(nf);
	if (nf->nf_mark)
		nfsd_file_mark_put(nf->nf_mark);
	if (nf->nf_file) {
		get_file(nf->nf_file);
		filp_close(nf->nf_file, NULL);
		fput(nf->nf_file);
		flush = true;
	}

	/*
	 * If this item is still linked via nf_lru, that's a bug.
	 * WARN and leak it to preserve system stability.
	 */
	if (WARN_ON_ONCE(!list_empty(&nf->nf_lru)))
		return flush;

	call_rcu(&nf->nf_rcu, nfsd_file_slab_free);
	return flush;
}

static bool
nfsd_file_check_writeback(struct nfsd_file *nf)
{
	struct file *file = nf->nf_file;
	struct address_space *mapping;

	if (!file || !(file->f_mode & FMODE_WRITE))
		return false;
	mapping = file->f_mapping;
	return mapping_tagged(mapping, PAGECACHE_TAG_DIRTY) ||
		mapping_tagged(mapping, PAGECACHE_TAG_WRITEBACK);
}

static int
nfsd_file_check_write_error(struct nfsd_file *nf)
{
	struct file *file = nf->nf_file;

	if (!file || !(file->f_mode & FMODE_WRITE))
		return 0;
	return filemap_check_wb_err(file->f_mapping, READ_ONCE(file->f_wb_err));
}

static void
nfsd_file_flush(struct nfsd_file *nf)
{
	struct file *file = nf->nf_file;

	if (!file || !(file->f_mode & FMODE_WRITE))
		return;
	this_cpu_add(nfsd_file_pages_flushed, file->f_mapping->nrpages);
	if (vfs_fsync(file, 1) != 0)
		nfsd_reset_write_verifier(net_generic(nf->nf_net, nfsd_net_id));
}

static void nfsd_file_lru_add(struct nfsd_file *nf)
{
	set_bit(NFSD_FILE_REFERENCED, &nf->nf_flags);
	if (list_lru_add(&nfsd_file_lru, &nf->nf_lru))
		trace_nfsd_file_lru_add(nf);
}

static void nfsd_file_lru_remove(struct nfsd_file *nf)
{
	if (list_lru_del(&nfsd_file_lru, &nf->nf_lru))
		trace_nfsd_file_lru_del(nf);
}

static void
nfsd_file_hash_remove(struct nfsd_file *nf)
{
	trace_nfsd_file_unhash(nf);

	if (nfsd_file_check_write_error(nf))
		nfsd_reset_write_verifier(net_generic(nf->nf_net, nfsd_net_id));
	rhashtable_remove_fast(&nfsd_file_rhash_tbl, &nf->nf_rhash,
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
nfsd_file_unhash_and_dispose(struct nfsd_file *nf, struct list_head *dispose)
{
	trace_nfsd_file_unhash_and_dispose(nf);
	if (nfsd_file_unhash(nf)) {
		/* caller must call nfsd_file_dispose_list() later */
		nfsd_file_lru_remove(nf);
		list_add(&nf->nf_lru, dispose);
	}
}

static void
nfsd_file_put_noref(struct nfsd_file *nf)
{
	trace_nfsd_file_put(nf);

	if (refcount_dec_and_test(&nf->nf_ref)) {
		WARN_ON(test_bit(NFSD_FILE_HASHED, &nf->nf_flags));
		nfsd_file_lru_remove(nf);
		nfsd_file_free(nf);
	}
}

void
nfsd_file_put(struct nfsd_file *nf)
{
	might_sleep();

	nfsd_file_lru_add(nf);
	if (test_bit(NFSD_FILE_HASHED, &nf->nf_flags) == 0) {
		nfsd_file_flush(nf);
		nfsd_file_put_noref(nf);
	} else if (nf->nf_file) {
		nfsd_file_put_noref(nf);
		nfsd_file_schedule_laundrette();
	} else
		nfsd_file_put_noref(nf);
}

/**
 * nfsd_file_close - Close an nfsd_file
 * @nf: nfsd_file to close
 *
 * If this is the final reference for @nf, free it immediately.
 * This reflects an on-the-wire CLOSE or DELEGRETURN into the
 * VFS and exported filesystem.
 */
void nfsd_file_close(struct nfsd_file *nf)
{
	nfsd_file_put(nf);
	if (refcount_dec_if_one(&nf->nf_ref)) {
		nfsd_file_unhash(nf);
		nfsd_file_lru_remove(nf);
		nfsd_file_free(nf);
	}
}

struct nfsd_file *
nfsd_file_get(struct nfsd_file *nf)
{
	if (likely(refcount_inc_not_zero(&nf->nf_ref)))
		return nf;
	return NULL;
}

static void
nfsd_file_dispose_list(struct list_head *dispose)
{
	struct nfsd_file *nf;

	while(!list_empty(dispose)) {
		nf = list_first_entry(dispose, struct nfsd_file, nf_lru);
		list_del_init(&nf->nf_lru);
		nfsd_file_flush(nf);
		nfsd_file_put_noref(nf);
	}
}

static void
nfsd_file_dispose_list_sync(struct list_head *dispose)
{
	bool flush = false;
	struct nfsd_file *nf;

	while(!list_empty(dispose)) {
		nf = list_first_entry(dispose, struct nfsd_file, nf_lru);
		list_del_init(&nf->nf_lru);
		nfsd_file_flush(nf);
		if (!refcount_dec_and_test(&nf->nf_ref))
			continue;
		if (nfsd_file_free(nf))
			flush = true;
	}
	if (flush)
		flush_delayed_fput();
}

static void
nfsd_file_list_remove_disposal(struct list_head *dst,
		struct nfsd_fcache_disposal *l)
{
	spin_lock(&l->lock);
	list_splice_init(&l->freeme, dst);
	spin_unlock(&l->lock);
}

static void
nfsd_file_list_add_disposal(struct list_head *files, struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct nfsd_fcache_disposal *l = nn->fcache_disposal;

	spin_lock(&l->lock);
	list_splice_tail_init(files, &l->freeme);
	spin_unlock(&l->lock);
	queue_work(nfsd_filecache_wq, &l->work);
}

static void
nfsd_file_list_add_pernet(struct list_head *dst, struct list_head *src,
		struct net *net)
{
	struct nfsd_file *nf, *tmp;

	list_for_each_entry_safe(nf, tmp, src, nf_lru) {
		if (nf->nf_net == net)
			list_move_tail(&nf->nf_lru, dst);
	}
}

static void
nfsd_file_dispose_list_delayed(struct list_head *dispose)
{
	LIST_HEAD(list);
	struct nfsd_file *nf;

	while(!list_empty(dispose)) {
		nf = list_first_entry(dispose, struct nfsd_file, nf_lru);
		nfsd_file_list_add_pernet(&list, dispose, nf->nf_net);
		nfsd_file_list_add_disposal(&list, nf->nf_net);
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

	/*
	 * Do a lockless refcount check. The hashtable holds one reference, so
	 * we look to see if anything else has a reference, or if any have
	 * been put since the shrinker last ran. Those don't get unhashed and
	 * released.
	 *
	 * Note that in the put path, we set the flag and then decrement the
	 * counter. Here we check the counter and then test and clear the flag.
	 * That order is deliberate to ensure that we can do this locklessly.
	 */
	if (refcount_read(&nf->nf_ref) > 1) {
		list_lru_isolate(lru, &nf->nf_lru);
		trace_nfsd_file_gc_in_use(nf);
		return LRU_REMOVED;
	}

	/*
	 * Don't throw out files that are still undergoing I/O or
	 * that have uncleared errors pending.
	 */
	if (nfsd_file_check_writeback(nf)) {
		trace_nfsd_file_gc_writeback(nf);
		return LRU_SKIP;
	}

	if (test_and_clear_bit(NFSD_FILE_REFERENCED, &nf->nf_flags)) {
		trace_nfsd_file_gc_referenced(nf);
		return LRU_ROTATE;
	}

	if (!test_and_clear_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		trace_nfsd_file_gc_hashed(nf);
		return LRU_SKIP;
	}

	list_lru_isolate_move(lru, &nf->nf_lru, head);
	this_cpu_inc(nfsd_file_evictions);
	trace_nfsd_file_gc_disposed(nf);
	return LRU_REMOVED;
}

/*
 * Unhash items on @dispose immediately, then queue them on the
 * disposal workqueue to finish releasing them in the background.
 *
 * cel: Note that between the time list_lru_shrink_walk runs and
 * now, these items are in the hash table but marked unhashed.
 * Why release these outside of lru_cb ? There's no lock ordering
 * problem since lru_cb currently takes no lock.
 */
static void nfsd_file_gc_dispose_list(struct list_head *dispose)
{
	struct nfsd_file *nf;

	list_for_each_entry(nf, dispose, nf_lru)
		nfsd_file_hash_remove(nf);
	nfsd_file_dispose_list_delayed(dispose);
}

static void
nfsd_file_gc(void)
{
	LIST_HEAD(dispose);
	unsigned long ret;

	ret = list_lru_walk(&nfsd_file_lru, nfsd_file_lru_cb,
			    &dispose, list_lru_count(&nfsd_file_lru));
	trace_nfsd_file_gc_removed(ret, list_lru_count(&nfsd_file_lru));
	nfsd_file_gc_dispose_list(&dispose);
}

static void
nfsd_file_gc_worker(struct work_struct *work)
{
	nfsd_file_gc();
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
	nfsd_file_gc_dispose_list(&dispose);
	return ret;
}

static struct shrinker	nfsd_file_shrinker = {
	.scan_objects = nfsd_file_lru_scan,
	.count_objects = nfsd_file_lru_count,
	.seeks = 1,
};

/*
 * Find all cache items across all net namespaces that match @inode and
 * move them to @dispose. The lookup is atomic wrt nfsd_file_acquire().
 */
static unsigned int
__nfsd_file_close_inode(struct inode *inode, struct list_head *dispose)
{
	struct nfsd_file_lookup_key key = {
		.type	= NFSD_FILE_KEY_INODE,
		.inode	= inode,
	};
	unsigned int count = 0;
	struct nfsd_file *nf;

	rcu_read_lock();
	do {
		nf = rhashtable_lookup(&nfsd_file_rhash_tbl, &key,
				       nfsd_file_rhash_params);
		if (!nf)
			break;
		nfsd_file_unhash_and_dispose(nf, dispose);
		count++;
	} while (1);
	rcu_read_unlock();
	return count;
}

/**
 * nfsd_file_close_inode_sync - attempt to forcibly close a nfsd_file
 * @inode: inode of the file to attempt to remove
 *
 * Unhash and put, then flush and fput all cache items associated with @inode.
 */
void
nfsd_file_close_inode_sync(struct inode *inode)
{
	LIST_HEAD(dispose);
	unsigned int count;

	count = __nfsd_file_close_inode(inode, &dispose);
	trace_nfsd_file_close_inode_sync(inode, count);
	nfsd_file_dispose_list_sync(&dispose);
}

/**
 * nfsd_file_close_inode - attempt a delayed close of a nfsd_file
 * @inode: inode of the file to attempt to remove
 *
 * Unhash and put all cache item associated with @inode.
 */
static void
nfsd_file_close_inode(struct inode *inode)
{
	LIST_HEAD(dispose);
	unsigned int count;

	count = __nfsd_file_close_inode(inode, &dispose);
	trace_nfsd_file_close_inode(inode, count);
	nfsd_file_dispose_list_delayed(&dispose);
}

/**
 * nfsd_file_delayed_close - close unused nfsd_files
 * @work: dummy
 *
 * Walk the LRU list and close any entries that have not been used since
 * the last scan.
 */
static void
nfsd_file_delayed_close(struct work_struct *work)
{
	LIST_HEAD(head);
	struct nfsd_fcache_disposal *l = container_of(work,
			struct nfsd_fcache_disposal, work);

	nfsd_file_list_remove_disposal(&head, l);
	nfsd_file_dispose_list(&head);
}

static int
nfsd_file_lease_notifier_call(struct notifier_block *nb, unsigned long arg,
			    void *data)
{
	struct file_lock *fl = data;

	/* Only close files for F_SETLEASE leases */
	if (fl->fl_flags & FL_LEASE)
		nfsd_file_close_inode_sync(file_inode(fl->fl_file));
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

	ret = rhashtable_init(&nfsd_file_rhash_tbl, &nfsd_file_rhash_params);
	if (ret)
		return ret;

	ret = -ENOMEM;
	nfsd_filecache_wq = alloc_workqueue("nfsd_filecache", 0, 0);
	if (!nfsd_filecache_wq)
		goto out;

	nfsd_file_slab = kmem_cache_create("nfsd_file",
				sizeof(struct nfsd_file), 0, 0, NULL);
	if (!nfsd_file_slab) {
		pr_err("nfsd: unable to create nfsd_file_slab\n");
		goto out_err;
	}

	nfsd_file_mark_slab = kmem_cache_create("nfsd_file_mark",
					sizeof(struct nfsd_file_mark), 0, 0, NULL);
	if (!nfsd_file_mark_slab) {
		pr_err("nfsd: unable to create nfsd_file_mark_slab\n");
		goto out_err;
	}


	ret = list_lru_init(&nfsd_file_lru);
	if (ret) {
		pr_err("nfsd: failed to init nfsd_file_lru: %d\n", ret);
		goto out_err;
	}

	ret = register_shrinker(&nfsd_file_shrinker, "nfsd-filecache");
	if (ret) {
		pr_err("nfsd: failed to register nfsd_file_shrinker: %d\n", ret);
		goto out_lru;
	}

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
	unregister_shrinker(&nfsd_file_shrinker);
out_lru:
	list_lru_destroy(&nfsd_file_lru);
out_err:
	kmem_cache_destroy(nfsd_file_slab);
	nfsd_file_slab = NULL;
	kmem_cache_destroy(nfsd_file_mark_slab);
	nfsd_file_mark_slab = NULL;
	destroy_workqueue(nfsd_filecache_wq);
	nfsd_filecache_wq = NULL;
	rhashtable_destroy(&nfsd_file_rhash_tbl);
	goto out;
}

static void
__nfsd_file_cache_purge(struct net *net)
{
	struct rhashtable_iter iter;
	struct nfsd_file *nf;
	LIST_HEAD(dispose);

	rhashtable_walk_enter(&nfsd_file_rhash_tbl, &iter);
	do {
		rhashtable_walk_start(&iter);

		nf = rhashtable_walk_next(&iter);
		while (!IS_ERR_OR_NULL(nf)) {
			if (net && nf->nf_net != net)
				continue;
			nfsd_file_unhash_and_dispose(nf, &dispose);
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
	INIT_WORK(&l->work, nfsd_file_delayed_close);
	spin_lock_init(&l->lock);
	INIT_LIST_HEAD(&l->freeme);
	return l;
}

static void
nfsd_free_fcache_disposal(struct nfsd_fcache_disposal *l)
{
	cancel_work_sync(&l->work);
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
	unregister_shrinker(&nfsd_file_shrinker);
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
	destroy_workqueue(nfsd_filecache_wq);
	nfsd_filecache_wq = NULL;
	rhashtable_destroy(&nfsd_file_rhash_tbl);

	for_each_possible_cpu(i) {
		per_cpu(nfsd_file_cache_hits, i) = 0;
		per_cpu(nfsd_file_acquisitions, i) = 0;
		per_cpu(nfsd_file_releases, i) = 0;
		per_cpu(nfsd_file_total_age, i) = 0;
		per_cpu(nfsd_file_pages_flushed, i) = 0;
		per_cpu(nfsd_file_evictions, i) = 0;
	}
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
	struct nfsd_file_lookup_key key = {
		.type	= NFSD_FILE_KEY_INODE,
		.inode	= inode,
	};
	bool ret = false;

	if (rhashtable_lookup_fast(&nfsd_file_rhash_tbl, &key,
				   nfsd_file_rhash_params) != NULL)
		ret = true;
	trace_nfsd_file_is_cached(inode, (int)ret);
	return ret;
}

static __be32
nfsd_file_do_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		     unsigned int may_flags, struct nfsd_file **pnf, bool open)
{
	struct nfsd_file_lookup_key key = {
		.type	= NFSD_FILE_KEY_FULL,
		.need	= may_flags & NFSD_FILE_MAY_MASK,
		.net	= SVC_NET(rqstp),
	};
	bool open_retry = true;
	struct nfsd_file *nf;
	__be32 status;
	int ret;

	status = fh_verify(rqstp, fhp, S_IFREG,
				may_flags|NFSD_MAY_OWNER_OVERRIDE);
	if (status != nfs_ok)
		return status;
	key.inode = d_inode(fhp->fh_dentry);
	key.cred = get_current_cred();

retry:
	rcu_read_lock();
	nf = rhashtable_lookup(&nfsd_file_rhash_tbl, &key,
			       nfsd_file_rhash_params);
	if (nf)
		nf = nfsd_file_get(nf);
	rcu_read_unlock();
	if (nf)
		goto wait_for_construction;

	nf = nfsd_file_alloc(&key, may_flags);
	if (!nf) {
		status = nfserr_jukebox;
		goto out_status;
	}

	ret = rhashtable_lookup_insert_key(&nfsd_file_rhash_tbl,
					   &key, &nf->nf_rhash,
					   nfsd_file_rhash_params);
	if (likely(ret == 0))
		goto open_file;

	nfsd_file_slab_free(&nf->nf_rcu);
	if (ret == -EEXIST)
		goto retry;
	trace_nfsd_file_insert_err(rqstp, key.inode, may_flags, ret);
	status = nfserr_jukebox;
	goto out_status;

wait_for_construction:
	wait_on_bit(&nf->nf_flags, NFSD_FILE_PENDING, TASK_UNINTERRUPTIBLE);

	/* Did construction of this file fail? */
	if (!test_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		trace_nfsd_file_cons_err(rqstp, key.inode, may_flags, nf);
		if (!open_retry) {
			status = nfserr_jukebox;
			goto out;
		}
		open_retry = false;
		nfsd_file_put_noref(nf);
		goto retry;
	}

	nfsd_file_lru_remove(nf);
	this_cpu_inc(nfsd_file_cache_hits);

	status = nfserrno(nfsd_open_break_lease(file_inode(nf->nf_file), may_flags));
out:
	if (status == nfs_ok) {
		if (open)
			this_cpu_inc(nfsd_file_acquisitions);
		*pnf = nf;
	} else {
		nfsd_file_put(nf);
		nf = NULL;
	}

out_status:
	put_cred(key.cred);
	if (open)
		trace_nfsd_file_acquire(rqstp, key.inode, may_flags, nf, status);
	return status;

open_file:
	trace_nfsd_file_alloc(nf);
	nf->nf_mark = nfsd_file_mark_find_or_create(nf, key.inode);
	if (nf->nf_mark) {
		if (open) {
			status = nfsd_open_verified(rqstp, fhp, may_flags,
						    &nf->nf_file);
			trace_nfsd_file_open(nf, status);
		} else
			status = nfs_ok;
	} else
		status = nfserr_jukebox;
	/*
	 * If construction failed, or we raced with a call to unlink()
	 * then unhash.
	 */
	if (status != nfs_ok || key.inode->i_nlink == 0)
		if (nfsd_file_unhash(nf))
			nfsd_file_put_noref(nf);
	clear_bit_unlock(NFSD_FILE_PENDING, &nf->nf_flags);
	smp_mb__after_atomic();
	wake_up_bit(&nf->nf_flags, NFSD_FILE_PENDING);
	goto out;
}

/**
 * nfsd_file_acquire - Get a struct nfsd_file with an open file
 * @rqstp: the RPC transaction being executed
 * @fhp: the NFS filehandle of the file to be opened
 * @may_flags: NFSD_MAY_ settings for the file
 * @pnf: OUT: new or found "struct nfsd_file" object
 *
 * Returns nfs_ok and sets @pnf on success; otherwise an nfsstat in
 * network byte order is returned.
 */
__be32
nfsd_file_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct nfsd_file **pnf)
{
	return nfsd_file_do_acquire(rqstp, fhp, may_flags, pnf, true);
}

/**
 * nfsd_file_create - Get a struct nfsd_file, do not open
 * @rqstp: the RPC transaction being executed
 * @fhp: the NFS filehandle of the file just created
 * @may_flags: NFSD_MAY_ settings for the file
 * @pnf: OUT: new or found "struct nfsd_file" object
 *
 * Returns nfs_ok and sets @pnf on success; otherwise an nfsstat in
 * network byte order is returned.
 */
__be32
nfsd_file_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
		 unsigned int may_flags, struct nfsd_file **pnf)
{
	return nfsd_file_do_acquire(rqstp, fhp, may_flags, pnf, false);
}

/*
 * Note that fields may be added, removed or reordered in the future. Programs
 * scraping this file for info should test the labels to ensure they're
 * getting the correct field.
 */
int nfsd_file_cache_stats_show(struct seq_file *m, void *v)
{
	unsigned long releases = 0, pages_flushed = 0, evictions = 0;
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
		ht = &nfsd_file_rhash_tbl;
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
		pages_flushed += per_cpu(nfsd_file_pages_flushed, i);
	}

	seq_printf(m, "total entries: %u\n", count);
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
	seq_printf(m, "pages flushed: %lu\n", pages_flushed);
	return 0;
}
