/*
 * Open file cache.
 *
 * (c) 2015 - Jeff Layton <jeff.layton@primarydata.com>
 */

#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/list_lru.h>
#include <linux/fsnotify_backend.h>
#include <linux/fsnotify.h>
#include <linux/seq_file.h>

#include "vfs.h"
#include "nfsd.h"
#include "nfsfh.h"
#include "netns.h"
#include "filecache.h"
#include "trace.h"

#define NFSDDBG_FACILITY	NFSDDBG_FH

/* FIXME: dynamically size this for the machine somehow? */
#define NFSD_FILE_HASH_BITS                   12
#define NFSD_FILE_HASH_SIZE                  (1 << NFSD_FILE_HASH_BITS)
#define NFSD_LAUNDRETTE_DELAY		     (2 * HZ)

#define NFSD_FILE_SHUTDOWN		     (1)
#define NFSD_FILE_LRU_THRESHOLD		     (4096UL)
#define NFSD_FILE_LRU_LIMIT		     (NFSD_FILE_LRU_THRESHOLD << 2)

/* We only care about NFSD_MAY_READ/WRITE for this cache */
#define NFSD_FILE_MAY_MASK	(NFSD_MAY_READ|NFSD_MAY_WRITE)

struct nfsd_fcache_bucket {
	struct hlist_head	nfb_head;
	spinlock_t		nfb_lock;
	unsigned int		nfb_count;
	unsigned int		nfb_maxcount;
};

static DEFINE_PER_CPU(unsigned long, nfsd_file_cache_hits);

struct nfsd_fcache_disposal {
	struct list_head list;
	struct work_struct work;
	struct net *net;
	spinlock_t lock;
	struct list_head freeme;
	struct rcu_head rcu;
};

static struct workqueue_struct *nfsd_filecache_wq __read_mostly;

static struct kmem_cache		*nfsd_file_slab;
static struct kmem_cache		*nfsd_file_mark_slab;
static struct nfsd_fcache_bucket	*nfsd_file_hashtbl;
static struct list_lru			nfsd_file_lru;
static long				nfsd_file_lru_flags;
static struct fsnotify_group		*nfsd_file_fsnotify_group;
static atomic_long_t			nfsd_filecache_count;
static struct delayed_work		nfsd_filecache_laundrette;
static DEFINE_SPINLOCK(laundrette_lock);
static LIST_HEAD(laundrettes);

static void nfsd_file_gc(void);

static void
nfsd_file_schedule_laundrette(void)
{
	long count = atomic_long_read(&nfsd_filecache_count);

	if (count == 0 || test_bit(NFSD_FILE_SHUTDOWN, &nfsd_file_lru_flags))
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
nfsd_file_mark_find_or_create(struct nfsd_file *nf)
{
	int			err;
	struct fsnotify_mark	*mark;
	struct nfsd_file_mark	*nfm = NULL, *new;
	struct inode *inode = nf->nf_inode;

	do {
		mutex_lock(&nfsd_file_fsnotify_group->mark_mutex);
		mark = fsnotify_find_mark(&inode->i_fsnotify_marks,
				nfsd_file_fsnotify_group);
		if (mark) {
			nfm = nfsd_file_mark_get(container_of(mark,
						 struct nfsd_file_mark,
						 nfm_mark));
			mutex_unlock(&nfsd_file_fsnotify_group->mark_mutex);
			if (nfm) {
				fsnotify_put_mark(mark);
				break;
			}
			/* Avoid soft lockup race with nfsd_file_mark_put() */
			fsnotify_destroy_mark(mark, nfsd_file_fsnotify_group);
			fsnotify_put_mark(mark);
		} else
			mutex_unlock(&nfsd_file_fsnotify_group->mark_mutex);

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
nfsd_file_alloc(struct inode *inode, unsigned int may, unsigned int hashval,
		struct net *net)
{
	struct nfsd_file *nf;

	nf = kmem_cache_alloc(nfsd_file_slab, GFP_KERNEL);
	if (nf) {
		INIT_HLIST_NODE(&nf->nf_node);
		INIT_LIST_HEAD(&nf->nf_lru);
		nf->nf_file = NULL;
		nf->nf_cred = get_current_cred();
		nf->nf_net = net;
		nf->nf_flags = 0;
		nf->nf_inode = inode;
		nf->nf_hashval = hashval;
		refcount_set(&nf->nf_ref, 1);
		nf->nf_may = may & NFSD_FILE_MAY_MASK;
		if (may & NFSD_MAY_NOT_BREAK_LEASE) {
			if (may & NFSD_MAY_WRITE)
				__set_bit(NFSD_FILE_BREAK_WRITE, &nf->nf_flags);
			if (may & NFSD_MAY_READ)
				__set_bit(NFSD_FILE_BREAK_READ, &nf->nf_flags);
		}
		nf->nf_mark = NULL;
		init_rwsem(&nf->nf_rwsem);
		trace_nfsd_file_alloc(nf);
	}
	return nf;
}

static bool
nfsd_file_free(struct nfsd_file *nf)
{
	bool flush = false;

	trace_nfsd_file_put_final(nf);
	if (nf->nf_mark)
		nfsd_file_mark_put(nf->nf_mark);
	if (nf->nf_file) {
		get_file(nf->nf_file);
		filp_close(nf->nf_file, NULL);
		fput(nf->nf_file);
		flush = true;
	}
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
nfsd_file_do_unhash(struct nfsd_file *nf)
{
	lockdep_assert_held(&nfsd_file_hashtbl[nf->nf_hashval].nfb_lock);

	trace_nfsd_file_unhash(nf);

	if (nfsd_file_check_write_error(nf))
		nfsd_reset_boot_verifier(net_generic(nf->nf_net, nfsd_net_id));
	--nfsd_file_hashtbl[nf->nf_hashval].nfb_count;
	hlist_del_rcu(&nf->nf_node);
	atomic_long_dec(&nfsd_filecache_count);
}

static bool
nfsd_file_unhash(struct nfsd_file *nf)
{
	if (test_and_clear_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		nfsd_file_do_unhash(nf);
		if (!list_empty(&nf->nf_lru))
			list_lru_del(&nfsd_file_lru, &nf->nf_lru);
		return true;
	}
	return false;
}

/*
 * Return true if the file was unhashed.
 */
static bool
nfsd_file_unhash_and_release_locked(struct nfsd_file *nf, struct list_head *dispose)
{
	lockdep_assert_held(&nfsd_file_hashtbl[nf->nf_hashval].nfb_lock);

	trace_nfsd_file_unhash_and_release_locked(nf);
	if (!nfsd_file_unhash(nf))
		return false;
	/* keep final reference for nfsd_file_lru_dispose */
	if (refcount_dec_not_one(&nf->nf_ref))
		return true;

	list_add(&nf->nf_lru, dispose);
	return true;
}

static void
nfsd_file_put_noref(struct nfsd_file *nf)
{
	trace_nfsd_file_put(nf);

	if (refcount_dec_and_test(&nf->nf_ref)) {
		WARN_ON(test_bit(NFSD_FILE_HASHED, &nf->nf_flags));
		nfsd_file_free(nf);
	}
}

void
nfsd_file_put(struct nfsd_file *nf)
{
	bool is_hashed;

	set_bit(NFSD_FILE_REFERENCED, &nf->nf_flags);
	if (refcount_read(&nf->nf_ref) > 2 || !nf->nf_file) {
		nfsd_file_put_noref(nf);
		return;
	}

	filemap_flush(nf->nf_file->f_mapping);
	is_hashed = test_bit(NFSD_FILE_HASHED, &nf->nf_flags) != 0;
	nfsd_file_put_noref(nf);
	if (is_hashed)
		nfsd_file_schedule_laundrette();
	if (atomic_long_read(&nfsd_filecache_count) >= NFSD_FILE_LRU_LIMIT)
		nfsd_file_gc();
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
		list_del(&nf->nf_lru);
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
		list_del(&nf->nf_lru);
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
	struct nfsd_fcache_disposal *l;

	rcu_read_lock();
	list_for_each_entry_rcu(l, &laundrettes, list) {
		if (l->net == net) {
			spin_lock(&l->lock);
			list_splice_tail_init(files, &l->freeme);
			spin_unlock(&l->lock);
			queue_work(nfsd_filecache_wq, &l->work);
			break;
		}
	}
	rcu_read_unlock();
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

/*
 * Note this can deadlock with nfsd_file_cache_purge.
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
	if (refcount_read(&nf->nf_ref) > 1)
		goto out_skip;

	/*
	 * Don't throw out files that are still undergoing I/O or
	 * that have uncleared errors pending.
	 */
	if (nfsd_file_check_writeback(nf))
		goto out_skip;

	if (test_and_clear_bit(NFSD_FILE_REFERENCED, &nf->nf_flags))
		goto out_skip;

	if (!test_and_clear_bit(NFSD_FILE_HASHED, &nf->nf_flags))
		goto out_skip;

	list_lru_isolate_move(lru, &nf->nf_lru, head);
	return LRU_REMOVED;
out_skip:
	return LRU_SKIP;
}

static unsigned long
nfsd_file_lru_walk_list(struct shrink_control *sc)
{
	LIST_HEAD(head);
	struct nfsd_file *nf;
	unsigned long ret;

	if (sc)
		ret = list_lru_shrink_walk(&nfsd_file_lru, sc,
				nfsd_file_lru_cb, &head);
	else
		ret = list_lru_walk(&nfsd_file_lru,
				nfsd_file_lru_cb,
				&head, LONG_MAX);
	list_for_each_entry(nf, &head, nf_lru) {
		spin_lock(&nfsd_file_hashtbl[nf->nf_hashval].nfb_lock);
		nfsd_file_do_unhash(nf);
		spin_unlock(&nfsd_file_hashtbl[nf->nf_hashval].nfb_lock);
	}
	nfsd_file_dispose_list_delayed(&head);
	return ret;
}

static void
nfsd_file_gc(void)
{
	nfsd_file_lru_walk_list(NULL);
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
	return nfsd_file_lru_walk_list(sc);
}

static struct shrinker	nfsd_file_shrinker = {
	.scan_objects = nfsd_file_lru_scan,
	.count_objects = nfsd_file_lru_count,
	.seeks = 1,
};

static void
__nfsd_file_close_inode(struct inode *inode, unsigned int hashval,
			struct list_head *dispose)
{
	struct nfsd_file	*nf;
	struct hlist_node	*tmp;

	spin_lock(&nfsd_file_hashtbl[hashval].nfb_lock);
	hlist_for_each_entry_safe(nf, tmp, &nfsd_file_hashtbl[hashval].nfb_head, nf_node) {
		if (inode == nf->nf_inode)
			nfsd_file_unhash_and_release_locked(nf, dispose);
	}
	spin_unlock(&nfsd_file_hashtbl[hashval].nfb_lock);
}

/**
 * nfsd_file_close_inode_sync - attempt to forcibly close a nfsd_file
 * @inode: inode of the file to attempt to remove
 *
 * Walk the whole hash bucket, looking for any files that correspond to "inode".
 * If any do, then unhash them and put the hashtable reference to them and
 * destroy any that had their last reference put. Also ensure that any of the
 * fputs also have their final __fput done as well.
 */
void
nfsd_file_close_inode_sync(struct inode *inode)
{
	unsigned int		hashval = (unsigned int)hash_long(inode->i_ino,
						NFSD_FILE_HASH_BITS);
	LIST_HEAD(dispose);

	__nfsd_file_close_inode(inode, hashval, &dispose);
	trace_nfsd_file_close_inode_sync(inode, hashval, !list_empty(&dispose));
	nfsd_file_dispose_list_sync(&dispose);
}

/**
 * nfsd_file_close_inode_sync - attempt to forcibly close a nfsd_file
 * @inode: inode of the file to attempt to remove
 *
 * Walk the whole hash bucket, looking for any files that correspond to "inode".
 * If any do, then unhash them and put the hashtable reference to them and
 * destroy any that had their last reference put.
 */
static void
nfsd_file_close_inode(struct inode *inode)
{
	unsigned int		hashval = (unsigned int)hash_long(inode->i_ino,
						NFSD_FILE_HASH_BITS);
	LIST_HEAD(dispose);

	__nfsd_file_close_inode(inode, hashval, &dispose);
	trace_nfsd_file_close_inode(inode, hashval, !list_empty(&dispose));
	nfsd_file_dispose_list_delayed(&dispose);
}

/**
 * nfsd_file_delayed_close - close unused nfsd_files
 * @work: dummy
 *
 * Walk the LRU list and close any entries that have not been used since
 * the last scan.
 *
 * Note this can deadlock with nfsd_file_cache_purge.
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
	int		ret = -ENOMEM;
	unsigned int	i;

	clear_bit(NFSD_FILE_SHUTDOWN, &nfsd_file_lru_flags);

	if (nfsd_file_hashtbl)
		return 0;

	nfsd_filecache_wq = alloc_workqueue("nfsd_filecache", 0, 0);
	if (!nfsd_filecache_wq)
		goto out;

	nfsd_file_hashtbl = kcalloc(NFSD_FILE_HASH_SIZE,
				sizeof(*nfsd_file_hashtbl), GFP_KERNEL);
	if (!nfsd_file_hashtbl) {
		pr_err("nfsd: unable to allocate nfsd_file_hashtbl\n");
		goto out_err;
	}

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

	ret = register_shrinker(&nfsd_file_shrinker);
	if (ret) {
		pr_err("nfsd: failed to register nfsd_file_shrinker: %d\n", ret);
		goto out_lru;
	}

	ret = lease_register_notifier(&nfsd_file_lease_notifier);
	if (ret) {
		pr_err("nfsd: unable to register lease notifier: %d\n", ret);
		goto out_shrinker;
	}

	nfsd_file_fsnotify_group = fsnotify_alloc_group(&nfsd_file_fsnotify_ops);
	if (IS_ERR(nfsd_file_fsnotify_group)) {
		pr_err("nfsd: unable to create fsnotify group: %ld\n",
			PTR_ERR(nfsd_file_fsnotify_group));
		ret = PTR_ERR(nfsd_file_fsnotify_group);
		nfsd_file_fsnotify_group = NULL;
		goto out_notifier;
	}

	for (i = 0; i < NFSD_FILE_HASH_SIZE; i++) {
		INIT_HLIST_HEAD(&nfsd_file_hashtbl[i].nfb_head);
		spin_lock_init(&nfsd_file_hashtbl[i].nfb_lock);
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
	kfree(nfsd_file_hashtbl);
	nfsd_file_hashtbl = NULL;
	destroy_workqueue(nfsd_filecache_wq);
	nfsd_filecache_wq = NULL;
	goto out;
}

/*
 * Note this can deadlock with nfsd_file_lru_cb.
 */
void
nfsd_file_cache_purge(struct net *net)
{
	unsigned int		i;
	struct nfsd_file	*nf;
	struct hlist_node	*next;
	LIST_HEAD(dispose);
	bool del;

	if (!nfsd_file_hashtbl)
		return;

	for (i = 0; i < NFSD_FILE_HASH_SIZE; i++) {
		struct nfsd_fcache_bucket *nfb = &nfsd_file_hashtbl[i];

		spin_lock(&nfb->nfb_lock);
		hlist_for_each_entry_safe(nf, next, &nfb->nfb_head, nf_node) {
			if (net && nf->nf_net != net)
				continue;
			del = nfsd_file_unhash_and_release_locked(nf, &dispose);

			/*
			 * Deadlock detected! Something marked this entry as
			 * unhased, but hasn't removed it from the hash list.
			 */
			WARN_ON_ONCE(!del);
		}
		spin_unlock(&nfb->nfb_lock);
		nfsd_file_dispose_list(&dispose);
	}
}

static struct nfsd_fcache_disposal *
nfsd_alloc_fcache_disposal(struct net *net)
{
	struct nfsd_fcache_disposal *l;

	l = kmalloc(sizeof(*l), GFP_KERNEL);
	if (!l)
		return NULL;
	INIT_WORK(&l->work, nfsd_file_delayed_close);
	l->net = net;
	spin_lock_init(&l->lock);
	INIT_LIST_HEAD(&l->freeme);
	return l;
}

static void
nfsd_free_fcache_disposal(struct nfsd_fcache_disposal *l)
{
	rcu_assign_pointer(l->net, NULL);
	cancel_work_sync(&l->work);
	nfsd_file_dispose_list(&l->freeme);
	kfree_rcu(l, rcu);
}

static void
nfsd_add_fcache_disposal(struct nfsd_fcache_disposal *l)
{
	spin_lock(&laundrette_lock);
	list_add_tail_rcu(&l->list, &laundrettes);
	spin_unlock(&laundrette_lock);
}

static void
nfsd_del_fcache_disposal(struct nfsd_fcache_disposal *l)
{
	spin_lock(&laundrette_lock);
	list_del_rcu(&l->list);
	spin_unlock(&laundrette_lock);
}

static int
nfsd_alloc_fcache_disposal_net(struct net *net)
{
	struct nfsd_fcache_disposal *l;

	l = nfsd_alloc_fcache_disposal(net);
	if (!l)
		return -ENOMEM;
	nfsd_add_fcache_disposal(l);
	return 0;
}

static void
nfsd_free_fcache_disposal_net(struct net *net)
{
	struct nfsd_fcache_disposal *l;

	rcu_read_lock();
	list_for_each_entry_rcu(l, &laundrettes, list) {
		if (l->net != net)
			continue;
		nfsd_del_fcache_disposal(l);
		rcu_read_unlock();
		nfsd_free_fcache_disposal(l);
		return;
	}
	rcu_read_unlock();
}

int
nfsd_file_cache_start_net(struct net *net)
{
	return nfsd_alloc_fcache_disposal_net(net);
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
	set_bit(NFSD_FILE_SHUTDOWN, &nfsd_file_lru_flags);

	lease_unregister_notifier(&nfsd_file_lease_notifier);
	unregister_shrinker(&nfsd_file_shrinker);
	/*
	 * make sure all callers of nfsd_file_lru_cb are done before
	 * calling nfsd_file_cache_purge
	 */
	cancel_delayed_work_sync(&nfsd_filecache_laundrette);
	nfsd_file_cache_purge(NULL);
	list_lru_destroy(&nfsd_file_lru);
	rcu_barrier();
	fsnotify_put_group(nfsd_file_fsnotify_group);
	nfsd_file_fsnotify_group = NULL;
	kmem_cache_destroy(nfsd_file_slab);
	nfsd_file_slab = NULL;
	fsnotify_wait_marks_destroyed();
	kmem_cache_destroy(nfsd_file_mark_slab);
	nfsd_file_mark_slab = NULL;
	kfree(nfsd_file_hashtbl);
	nfsd_file_hashtbl = NULL;
	destroy_workqueue(nfsd_filecache_wq);
	nfsd_filecache_wq = NULL;
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

static struct nfsd_file *
nfsd_file_find_locked(struct inode *inode, unsigned int may_flags,
			unsigned int hashval, struct net *net)
{
	struct nfsd_file *nf;
	unsigned char need = may_flags & NFSD_FILE_MAY_MASK;

	hlist_for_each_entry_rcu(nf, &nfsd_file_hashtbl[hashval].nfb_head,
				 nf_node, lockdep_is_held(&nfsd_file_hashtbl[hashval].nfb_lock)) {
		if (nf->nf_may != need)
			continue;
		if (nf->nf_inode != inode)
			continue;
		if (nf->nf_net != net)
			continue;
		if (!nfsd_match_cred(nf->nf_cred, current_cred()))
			continue;
		if (!test_bit(NFSD_FILE_HASHED, &nf->nf_flags))
			continue;
		if (nfsd_file_get(nf) != NULL)
			return nf;
	}
	return NULL;
}

/**
 * nfsd_file_is_cached - are there any cached open files for this fh?
 * @inode: inode of the file to check
 *
 * Scan the hashtable for open files that match this fh. Returns true if there
 * are any, and false if not.
 */
bool
nfsd_file_is_cached(struct inode *inode)
{
	bool			ret = false;
	struct nfsd_file	*nf;
	unsigned int		hashval;

        hashval = (unsigned int)hash_long(inode->i_ino, NFSD_FILE_HASH_BITS);

	rcu_read_lock();
	hlist_for_each_entry_rcu(nf, &nfsd_file_hashtbl[hashval].nfb_head,
				 nf_node) {
		if (inode == nf->nf_inode) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();
	trace_nfsd_file_is_cached(inode, hashval, (int)ret);
	return ret;
}

__be32
nfsd_file_acquire(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  unsigned int may_flags, struct nfsd_file **pnf)
{
	__be32	status;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_file *nf, *new;
	struct inode *inode;
	unsigned int hashval;
	bool retry = true;

	/* FIXME: skip this if fh_dentry is already set? */
	status = fh_verify(rqstp, fhp, S_IFREG,
				may_flags|NFSD_MAY_OWNER_OVERRIDE);
	if (status != nfs_ok)
		return status;

	inode = d_inode(fhp->fh_dentry);
	hashval = (unsigned int)hash_long(inode->i_ino, NFSD_FILE_HASH_BITS);
retry:
	rcu_read_lock();
	nf = nfsd_file_find_locked(inode, may_flags, hashval, net);
	rcu_read_unlock();
	if (nf)
		goto wait_for_construction;

	new = nfsd_file_alloc(inode, may_flags, hashval, net);
	if (!new) {
		trace_nfsd_file_acquire(rqstp, hashval, inode, may_flags,
					NULL, nfserr_jukebox);
		return nfserr_jukebox;
	}

	spin_lock(&nfsd_file_hashtbl[hashval].nfb_lock);
	nf = nfsd_file_find_locked(inode, may_flags, hashval, net);
	if (nf == NULL)
		goto open_file;
	spin_unlock(&nfsd_file_hashtbl[hashval].nfb_lock);
	nfsd_file_slab_free(&new->nf_rcu);

wait_for_construction:
	wait_on_bit(&nf->nf_flags, NFSD_FILE_PENDING, TASK_UNINTERRUPTIBLE);

	/* Did construction of this file fail? */
	if (!test_bit(NFSD_FILE_HASHED, &nf->nf_flags)) {
		if (!retry) {
			status = nfserr_jukebox;
			goto out;
		}
		retry = false;
		nfsd_file_put_noref(nf);
		goto retry;
	}

	this_cpu_inc(nfsd_file_cache_hits);

	if (!(may_flags & NFSD_MAY_NOT_BREAK_LEASE)) {
		bool write = (may_flags & NFSD_MAY_WRITE);

		if (test_bit(NFSD_FILE_BREAK_READ, &nf->nf_flags) ||
		    (test_bit(NFSD_FILE_BREAK_WRITE, &nf->nf_flags) && write)) {
			status = nfserrno(nfsd_open_break_lease(
					file_inode(nf->nf_file), may_flags));
			if (status == nfs_ok) {
				clear_bit(NFSD_FILE_BREAK_READ, &nf->nf_flags);
				if (write)
					clear_bit(NFSD_FILE_BREAK_WRITE,
						  &nf->nf_flags);
			}
		}
	}
out:
	if (status == nfs_ok) {
		*pnf = nf;
	} else {
		nfsd_file_put(nf);
		nf = NULL;
	}

	trace_nfsd_file_acquire(rqstp, hashval, inode, may_flags, nf, status);
	return status;
open_file:
	nf = new;
	/* Take reference for the hashtable */
	refcount_inc(&nf->nf_ref);
	__set_bit(NFSD_FILE_HASHED, &nf->nf_flags);
	__set_bit(NFSD_FILE_PENDING, &nf->nf_flags);
	list_lru_add(&nfsd_file_lru, &nf->nf_lru);
	hlist_add_head_rcu(&nf->nf_node, &nfsd_file_hashtbl[hashval].nfb_head);
	++nfsd_file_hashtbl[hashval].nfb_count;
	nfsd_file_hashtbl[hashval].nfb_maxcount = max(nfsd_file_hashtbl[hashval].nfb_maxcount,
			nfsd_file_hashtbl[hashval].nfb_count);
	spin_unlock(&nfsd_file_hashtbl[hashval].nfb_lock);
	if (atomic_long_inc_return(&nfsd_filecache_count) >= NFSD_FILE_LRU_THRESHOLD)
		nfsd_file_gc();

	nf->nf_mark = nfsd_file_mark_find_or_create(nf);
	if (nf->nf_mark)
		status = nfsd_open_verified(rqstp, fhp, S_IFREG,
				may_flags, &nf->nf_file);
	else
		status = nfserr_jukebox;
	/*
	 * If construction failed, or we raced with a call to unlink()
	 * then unhash.
	 */
	if (status != nfs_ok || inode->i_nlink == 0) {
		bool do_free;
		spin_lock(&nfsd_file_hashtbl[hashval].nfb_lock);
		do_free = nfsd_file_unhash(nf);
		spin_unlock(&nfsd_file_hashtbl[hashval].nfb_lock);
		if (do_free)
			nfsd_file_put_noref(nf);
	}
	clear_bit_unlock(NFSD_FILE_PENDING, &nf->nf_flags);
	smp_mb__after_atomic();
	wake_up_bit(&nf->nf_flags, NFSD_FILE_PENDING);
	goto out;
}

/*
 * Note that fields may be added, removed or reordered in the future. Programs
 * scraping this file for info should test the labels to ensure they're
 * getting the correct field.
 */
static int nfsd_file_cache_stats_show(struct seq_file *m, void *v)
{
	unsigned int i, count = 0, longest = 0;
	unsigned long hits = 0;

	/*
	 * No need for spinlocks here since we're not terribly interested in
	 * accuracy. We do take the nfsd_mutex simply to ensure that we
	 * don't end up racing with server shutdown
	 */
	mutex_lock(&nfsd_mutex);
	if (nfsd_file_hashtbl) {
		for (i = 0; i < NFSD_FILE_HASH_SIZE; i++) {
			count += nfsd_file_hashtbl[i].nfb_count;
			longest = max(longest, nfsd_file_hashtbl[i].nfb_count);
		}
	}
	mutex_unlock(&nfsd_mutex);

	for_each_possible_cpu(i)
		hits += per_cpu(nfsd_file_cache_hits, i);

	seq_printf(m, "total entries: %u\n", count);
	seq_printf(m, "longest chain: %u\n", longest);
	seq_printf(m, "cache hits:    %lu\n", hits);
	return 0;
}

int nfsd_file_cache_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, nfsd_file_cache_stats_show, NULL);
}
