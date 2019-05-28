// SPDX-License-Identifier: GPL-2.0
/*
 * DFS referral cache routines
 *
 * Copyright (c) 2018-2019 Paulo Alcantara <palcantara@suse.de>
 */

#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/jhash.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/nls.h>
#include <linux/workqueue.h>
#include "cifsglob.h"
#include "smb2pdu.h"
#include "smb2proto.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "smb2glob.h"

#include "dfs_cache.h"

#define DFS_CACHE_HTABLE_SIZE 32
#define DFS_CACHE_MAX_ENTRIES 64

#define IS_INTERLINK_SET(v) ((v) & (DFSREF_REFERRAL_SERVER | \
				    DFSREF_STORAGE_SERVER))

struct dfs_cache_tgt {
	char *t_name;
	struct list_head t_list;
};

struct dfs_cache_entry {
	struct hlist_node ce_hlist;
	const char *ce_path;
	int ce_ttl;
	int ce_srvtype;
	int ce_flags;
	struct timespec64 ce_etime;
	int ce_path_consumed;
	int ce_numtgts;
	struct list_head ce_tlist;
	struct dfs_cache_tgt *ce_tgthint;
	struct rcu_head ce_rcu;
};

static struct kmem_cache *dfs_cache_slab __read_mostly;

struct dfs_cache_vol_info {
	char *vi_fullpath;
	struct smb_vol vi_vol;
	char *vi_mntdata;
	struct list_head vi_list;
};

struct dfs_cache {
	struct mutex dc_lock;
	struct nls_table *dc_nlsc;
	struct list_head dc_vol_list;
	int dc_ttl;
	struct delayed_work dc_refresh;
};

static struct dfs_cache dfs_cache;

/*
 * Number of entries in the cache
 */
static size_t dfs_cache_count;

static DEFINE_MUTEX(dfs_cache_list_lock);
static struct hlist_head dfs_cache_htable[DFS_CACHE_HTABLE_SIZE];

static void refresh_cache_worker(struct work_struct *work);

static inline bool is_path_valid(const char *path)
{
	return path && (strchr(path + 1, '\\') || strchr(path + 1, '/'));
}

static inline int get_normalized_path(const char *path, char **npath)
{
	if (*path == '\\') {
		*npath = (char *)path;
	} else {
		*npath = kstrndup(path, strlen(path), GFP_KERNEL);
		if (!*npath)
			return -ENOMEM;
		convert_delimiter(*npath, '\\');
	}
	return 0;
}

static inline void free_normalized_path(const char *path, char *npath)
{
	if (path != npath)
		kfree(npath);
}

static inline bool cache_entry_expired(const struct dfs_cache_entry *ce)
{
	struct timespec64 ts;

	ktime_get_coarse_real_ts64(&ts);
	return timespec64_compare(&ts, &ce->ce_etime) >= 0;
}

static inline void free_tgts(struct dfs_cache_entry *ce)
{
	struct dfs_cache_tgt *t, *n;

	list_for_each_entry_safe(t, n, &ce->ce_tlist, t_list) {
		list_del(&t->t_list);
		kfree(t->t_name);
		kfree(t);
	}
}

static void free_cache_entry(struct rcu_head *rcu)
{
	struct dfs_cache_entry *ce = container_of(rcu, struct dfs_cache_entry,
						  ce_rcu);
	kmem_cache_free(dfs_cache_slab, ce);
}

static inline void flush_cache_ent(struct dfs_cache_entry *ce)
{
	if (hlist_unhashed(&ce->ce_hlist))
		return;

	hlist_del_init_rcu(&ce->ce_hlist);
	kfree(ce->ce_path);
	free_tgts(ce);
	dfs_cache_count--;
	call_rcu(&ce->ce_rcu, free_cache_entry);
}

static void flush_cache_ents(void)
{
	int i;

	rcu_read_lock();
	for (i = 0; i < DFS_CACHE_HTABLE_SIZE; i++) {
		struct hlist_head *l = &dfs_cache_htable[i];
		struct dfs_cache_entry *ce;

		hlist_for_each_entry_rcu(ce, l, ce_hlist)
			flush_cache_ent(ce);
	}
	rcu_read_unlock();
}

/*
 * dfs cache /proc file
 */
static int dfscache_proc_show(struct seq_file *m, void *v)
{
	int bucket;
	struct dfs_cache_entry *ce;
	struct dfs_cache_tgt *t;

	seq_puts(m, "DFS cache\n---------\n");

	mutex_lock(&dfs_cache_list_lock);

	rcu_read_lock();
	hash_for_each_rcu(dfs_cache_htable, bucket, ce, ce_hlist) {
		seq_printf(m,
			   "cache entry: path=%s,type=%s,ttl=%d,etime=%ld,"
			   "interlink=%s,path_consumed=%d,expired=%s\n",
			   ce->ce_path,
			   ce->ce_srvtype == DFS_TYPE_ROOT ? "root" : "link",
			   ce->ce_ttl, ce->ce_etime.tv_nsec,
			   IS_INTERLINK_SET(ce->ce_flags) ? "yes" : "no",
			   ce->ce_path_consumed,
			   cache_entry_expired(ce) ? "yes" : "no");

		list_for_each_entry(t, &ce->ce_tlist, t_list) {
			seq_printf(m, "  %s%s\n",
				   t->t_name,
				   ce->ce_tgthint == t ? " (target hint)" : "");
		}

	}
	rcu_read_unlock();

	mutex_unlock(&dfs_cache_list_lock);
	return 0;
}

static ssize_t dfscache_proc_write(struct file *file, const char __user *buffer,
				   size_t count, loff_t *ppos)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;

	if (c != '0')
		return -EINVAL;

	cifs_dbg(FYI, "clearing dfs cache");
	mutex_lock(&dfs_cache_list_lock);
	flush_cache_ents();
	mutex_unlock(&dfs_cache_list_lock);

	return count;
}

static int dfscache_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dfscache_proc_show, NULL);
}

const struct file_operations dfscache_proc_fops = {
	.open		= dfscache_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= dfscache_proc_write,
};

#ifdef CONFIG_CIFS_DEBUG2
static inline void dump_tgts(const struct dfs_cache_entry *ce)
{
	struct dfs_cache_tgt *t;

	cifs_dbg(FYI, "target list:\n");
	list_for_each_entry(t, &ce->ce_tlist, t_list) {
		cifs_dbg(FYI, "  %s%s\n", t->t_name,
			 ce->ce_tgthint == t ? " (target hint)" : "");
	}
}

static inline void dump_ce(const struct dfs_cache_entry *ce)
{
	cifs_dbg(FYI, "cache entry: path=%s,type=%s,ttl=%d,etime=%ld,"
		 "interlink=%s,path_consumed=%d,expired=%s\n", ce->ce_path,
		 ce->ce_srvtype == DFS_TYPE_ROOT ? "root" : "link", ce->ce_ttl,
		 ce->ce_etime.tv_nsec,
		 IS_INTERLINK_SET(ce->ce_flags) ? "yes" : "no",
		 ce->ce_path_consumed,
		 cache_entry_expired(ce) ? "yes" : "no");
	dump_tgts(ce);
}

static inline void dump_refs(const struct dfs_info3_param *refs, int numrefs)
{
	int i;

	cifs_dbg(FYI, "DFS referrals returned by the server:\n");
	for (i = 0; i < numrefs; i++) {
		const struct dfs_info3_param *ref = &refs[i];

		cifs_dbg(FYI,
			 "\n"
			 "flags:         0x%x\n"
			 "path_consumed: %d\n"
			 "server_type:   0x%x\n"
			 "ref_flag:      0x%x\n"
			 "path_name:     %s\n"
			 "node_name:     %s\n"
			 "ttl:           %d (%dm)\n",
			 ref->flags, ref->path_consumed, ref->server_type,
			 ref->ref_flag, ref->path_name, ref->node_name,
			 ref->ttl, ref->ttl / 60);
	}
}
#else
#define dump_tgts(e)
#define dump_ce(e)
#define dump_refs(r, n)
#endif

/**
 * dfs_cache_init - Initialize DFS referral cache.
 *
 * Return zero if initialized successfully, otherwise non-zero.
 */
int dfs_cache_init(void)
{
	int i;

	dfs_cache_slab = kmem_cache_create("cifs_dfs_cache",
					   sizeof(struct dfs_cache_entry), 0,
					   SLAB_HWCACHE_ALIGN, NULL);
	if (!dfs_cache_slab)
		return -ENOMEM;

	for (i = 0; i < DFS_CACHE_HTABLE_SIZE; i++)
		INIT_HLIST_HEAD(&dfs_cache_htable[i]);

	INIT_LIST_HEAD(&dfs_cache.dc_vol_list);
	mutex_init(&dfs_cache.dc_lock);
	INIT_DELAYED_WORK(&dfs_cache.dc_refresh, refresh_cache_worker);
	dfs_cache.dc_ttl = -1;
	dfs_cache.dc_nlsc = load_nls_default();

	cifs_dbg(FYI, "%s: initialized DFS referral cache\n", __func__);
	return 0;
}

static inline unsigned int cache_entry_hash(const void *data, int size)
{
	unsigned int h;

	h = jhash(data, size, 0);
	return h & (DFS_CACHE_HTABLE_SIZE - 1);
}

/* Check whether second path component of @path is SYSVOL or NETLOGON */
static inline bool is_sysvol_or_netlogon(const char *path)
{
	const char *s;
	char sep = path[0];

	s = strchr(path + 1, sep) + 1;
	return !strncasecmp(s, "sysvol", strlen("sysvol")) ||
		!strncasecmp(s, "netlogon", strlen("netlogon"));
}

/* Return target hint of a DFS cache entry */
static inline char *get_tgt_name(const struct dfs_cache_entry *ce)
{
	struct dfs_cache_tgt *t = ce->ce_tgthint;

	return t ? t->t_name : ERR_PTR(-ENOENT);
}

/* Return expire time out of a new entry's TTL */
static inline struct timespec64 get_expire_time(int ttl)
{
	struct timespec64 ts = {
		.tv_sec = ttl,
		.tv_nsec = 0,
	};
	struct timespec64 now;

	ktime_get_coarse_real_ts64(&now);
	return timespec64_add(now, ts);
}

/* Allocate a new DFS target */
static inline struct dfs_cache_tgt *alloc_tgt(const char *name)
{
	struct dfs_cache_tgt *t;

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return ERR_PTR(-ENOMEM);
	t->t_name = kstrndup(name, strlen(name), GFP_KERNEL);
	if (!t->t_name) {
		kfree(t);
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&t->t_list);
	return t;
}

/*
 * Copy DFS referral information to a cache entry and conditionally update
 * target hint.
 */
static int copy_ref_data(const struct dfs_info3_param *refs, int numrefs,
			 struct dfs_cache_entry *ce, const char *tgthint)
{
	int i;

	ce->ce_ttl = refs[0].ttl;
	ce->ce_etime = get_expire_time(ce->ce_ttl);
	ce->ce_srvtype = refs[0].server_type;
	ce->ce_flags = refs[0].ref_flag;
	ce->ce_path_consumed = refs[0].path_consumed;

	for (i = 0; i < numrefs; i++) {
		struct dfs_cache_tgt *t;

		t = alloc_tgt(refs[i].node_name);
		if (IS_ERR(t)) {
			free_tgts(ce);
			return PTR_ERR(t);
		}
		if (tgthint && !strcasecmp(t->t_name, tgthint)) {
			list_add(&t->t_list, &ce->ce_tlist);
			tgthint = NULL;
		} else {
			list_add_tail(&t->t_list, &ce->ce_tlist);
		}
		ce->ce_numtgts++;
	}

	ce->ce_tgthint = list_first_entry_or_null(&ce->ce_tlist,
						  struct dfs_cache_tgt, t_list);

	return 0;
}

/* Allocate a new cache entry */
static struct dfs_cache_entry *
alloc_cache_entry(const char *path, const struct dfs_info3_param *refs,
		  int numrefs)
{
	struct dfs_cache_entry *ce;
	int rc;

	ce = kmem_cache_zalloc(dfs_cache_slab, GFP_KERNEL);
	if (!ce)
		return ERR_PTR(-ENOMEM);

	ce->ce_path = kstrdup_const(path, GFP_KERNEL);
	if (!ce->ce_path) {
		kmem_cache_free(dfs_cache_slab, ce);
		return ERR_PTR(-ENOMEM);
	}
	INIT_HLIST_NODE(&ce->ce_hlist);
	INIT_LIST_HEAD(&ce->ce_tlist);

	rc = copy_ref_data(refs, numrefs, ce, NULL);
	if (rc) {
		kfree(ce->ce_path);
		kmem_cache_free(dfs_cache_slab, ce);
		ce = ERR_PTR(rc);
	}
	return ce;
}

static void remove_oldest_entry(void)
{
	int bucket;
	struct dfs_cache_entry *ce;
	struct dfs_cache_entry *to_del = NULL;

	rcu_read_lock();
	hash_for_each_rcu(dfs_cache_htable, bucket, ce, ce_hlist) {
		if (!to_del || timespec64_compare(&ce->ce_etime,
						  &to_del->ce_etime) < 0)
			to_del = ce;
	}
	if (!to_del) {
		cifs_dbg(FYI, "%s: no entry to remove", __func__);
		goto out;
	}
	cifs_dbg(FYI, "%s: removing entry", __func__);
	dump_ce(to_del);
	flush_cache_ent(to_del);
out:
	rcu_read_unlock();
}

/* Add a new DFS cache entry */
static inline struct dfs_cache_entry *
add_cache_entry(unsigned int hash, const char *path,
		const struct dfs_info3_param *refs, int numrefs)
{
	struct dfs_cache_entry *ce;

	ce = alloc_cache_entry(path, refs, numrefs);
	if (IS_ERR(ce))
		return ce;

	hlist_add_head_rcu(&ce->ce_hlist, &dfs_cache_htable[hash]);

	mutex_lock(&dfs_cache.dc_lock);
	if (dfs_cache.dc_ttl < 0) {
		dfs_cache.dc_ttl = ce->ce_ttl;
		queue_delayed_work(cifsiod_wq, &dfs_cache.dc_refresh,
				   dfs_cache.dc_ttl * HZ);
	} else {
		dfs_cache.dc_ttl = min_t(int, dfs_cache.dc_ttl, ce->ce_ttl);
		mod_delayed_work(cifsiod_wq, &dfs_cache.dc_refresh,
				 dfs_cache.dc_ttl * HZ);
	}
	mutex_unlock(&dfs_cache.dc_lock);

	return ce;
}

static struct dfs_cache_entry *__find_cache_entry(unsigned int hash,
						  const char *path)
{
	struct dfs_cache_entry *ce;
	bool found = false;

	rcu_read_lock();
	hlist_for_each_entry_rcu(ce, &dfs_cache_htable[hash], ce_hlist) {
		if (!strcasecmp(path, ce->ce_path)) {
#ifdef CONFIG_CIFS_DEBUG2
			char *name = get_tgt_name(ce);

			if (unlikely(IS_ERR(name))) {
				rcu_read_unlock();
				return ERR_CAST(name);
			}
			cifs_dbg(FYI, "%s: cache hit\n", __func__);
			cifs_dbg(FYI, "%s: target hint: %s\n", __func__, name);
#endif
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	return found ? ce : ERR_PTR(-ENOENT);
}

/*
 * Find a DFS cache entry in hash table and optionally check prefix path against
 * @path.
 * Use whole path components in the match.
 * Return ERR_PTR(-ENOENT) if the entry is not found.
 */
static inline struct dfs_cache_entry *find_cache_entry(const char *path,
						       unsigned int *hash)
{
	*hash = cache_entry_hash(path, strlen(path));
	return __find_cache_entry(*hash, path);
}

static inline void destroy_slab_cache(void)
{
	rcu_barrier();
	kmem_cache_destroy(dfs_cache_slab);
}

static inline void free_vol(struct dfs_cache_vol_info *vi)
{
	list_del(&vi->vi_list);
	kfree(vi->vi_fullpath);
	kfree(vi->vi_mntdata);
	cifs_cleanup_volume_info_contents(&vi->vi_vol);
	kfree(vi);
}

static inline void free_vol_list(void)
{
	struct dfs_cache_vol_info *vi, *nvi;

	list_for_each_entry_safe(vi, nvi, &dfs_cache.dc_vol_list, vi_list)
		free_vol(vi);
}

/**
 * dfs_cache_destroy - destroy DFS referral cache
 */
void dfs_cache_destroy(void)
{
	cancel_delayed_work_sync(&dfs_cache.dc_refresh);
	unload_nls(dfs_cache.dc_nlsc);
	free_vol_list();
	mutex_destroy(&dfs_cache.dc_lock);

	flush_cache_ents();
	destroy_slab_cache();
	mutex_destroy(&dfs_cache_list_lock);

	cifs_dbg(FYI, "%s: destroyed DFS referral cache\n", __func__);
}

static inline struct dfs_cache_entry *
__update_cache_entry(const char *path, const struct dfs_info3_param *refs,
		     int numrefs)
{
	int rc;
	unsigned int h;
	struct dfs_cache_entry *ce;
	char *s, *th = NULL;

	ce = find_cache_entry(path, &h);
	if (IS_ERR(ce))
		return ce;

	if (ce->ce_tgthint) {
		s = ce->ce_tgthint->t_name;
		th = kstrndup(s, strlen(s), GFP_KERNEL);
		if (!th)
			return ERR_PTR(-ENOMEM);
	}

	free_tgts(ce);
	ce->ce_numtgts = 0;

	rc = copy_ref_data(refs, numrefs, ce, th);
	kfree(th);

	if (rc)
		ce = ERR_PTR(rc);

	return ce;
}

/* Update an expired cache entry by getting a new DFS referral from server */
static struct dfs_cache_entry *
update_cache_entry(const unsigned int xid, struct cifs_ses *ses,
		   const struct nls_table *nls_codepage, int remap,
		   const char *path, struct dfs_cache_entry *ce)
{
	int rc;
	struct dfs_info3_param *refs = NULL;
	int numrefs = 0;

	cifs_dbg(FYI, "%s: update expired cache entry\n", __func__);
	/*
	 * Check if caller provided enough parameters to update an expired
	 * entry.
	 */
	if (!ses || !ses->server || !ses->server->ops->get_dfs_refer)
		return ERR_PTR(-ETIME);
	if (unlikely(!nls_codepage))
		return ERR_PTR(-ETIME);

	cifs_dbg(FYI, "%s: DFS referral request for %s\n", __func__, path);

	rc = ses->server->ops->get_dfs_refer(xid, ses, path, &refs, &numrefs,
					     nls_codepage, remap);
	if (rc)
		ce = ERR_PTR(rc);
	else
		ce = __update_cache_entry(path, refs, numrefs);

	dump_refs(refs, numrefs);
	free_dfs_info_array(refs, numrefs);

	return ce;
}

/*
 * Find, create or update a DFS cache entry.
 *
 * If the entry wasn't found, it will create a new one. Or if it was found but
 * expired, then it will update the entry accordingly.
 *
 * For interlinks, __cifs_dfs_mount() and expand_dfs_referral() are supposed to
 * handle them properly.
 */
static struct dfs_cache_entry *
do_dfs_cache_find(const unsigned int xid, struct cifs_ses *ses,
		  const struct nls_table *nls_codepage, int remap,
		  const char *path, bool noreq)
{
	int rc;
	unsigned int h;
	struct dfs_cache_entry *ce;
	struct dfs_info3_param *nrefs;
	int numnrefs;

	cifs_dbg(FYI, "%s: search path: %s\n", __func__, path);

	ce = find_cache_entry(path, &h);
	if (IS_ERR(ce)) {
		cifs_dbg(FYI, "%s: cache miss\n", __func__);
		/*
		 * If @noreq is set, no requests will be sent to the server for
		 * either updating or getting a new DFS referral.
		 */
		if (noreq)
			return ce;
		/*
		 * No cache entry was found, so check for valid parameters that
		 * will be required to get a new DFS referral and then create a
		 * new cache entry.
		 */
		if (!ses || !ses->server || !ses->server->ops->get_dfs_refer) {
			ce = ERR_PTR(-EOPNOTSUPP);
			return ce;
		}
		if (unlikely(!nls_codepage)) {
			ce = ERR_PTR(-EINVAL);
			return ce;
		}

		nrefs = NULL;
		numnrefs = 0;

		cifs_dbg(FYI, "%s: DFS referral request for %s\n", __func__,
			 path);

		rc = ses->server->ops->get_dfs_refer(xid, ses, path, &nrefs,
						     &numnrefs, nls_codepage,
						     remap);
		if (rc) {
			ce = ERR_PTR(rc);
			return ce;
		}

		dump_refs(nrefs, numnrefs);

		cifs_dbg(FYI, "%s: new cache entry\n", __func__);

		if (dfs_cache_count >= DFS_CACHE_MAX_ENTRIES) {
			cifs_dbg(FYI, "%s: reached max cache size (%d)",
				 __func__, DFS_CACHE_MAX_ENTRIES);
			remove_oldest_entry();
		}
		ce = add_cache_entry(h, path, nrefs, numnrefs);
		free_dfs_info_array(nrefs, numnrefs);

		if (IS_ERR(ce))
			return ce;

		dfs_cache_count++;
	}

	dump_ce(ce);

	/* Just return the found cache entry in case @noreq is set */
	if (noreq)
		return ce;

	if (cache_entry_expired(ce)) {
		cifs_dbg(FYI, "%s: expired cache entry\n", __func__);
		ce = update_cache_entry(xid, ses, nls_codepage, remap, path,
					ce);
		if (IS_ERR(ce)) {
			cifs_dbg(FYI, "%s: failed to update expired entry\n",
				 __func__);
		}
	}
	return ce;
}

/* Set up a new DFS referral from a given cache entry */
static int setup_ref(const char *path, const struct dfs_cache_entry *ce,
		     struct dfs_info3_param *ref, const char *tgt)
{
	int rc;

	cifs_dbg(FYI, "%s: set up new ref\n", __func__);

	memset(ref, 0, sizeof(*ref));

	ref->path_name = kstrndup(path, strlen(path), GFP_KERNEL);
	if (!ref->path_name)
		return -ENOMEM;

	ref->path_consumed = ce->ce_path_consumed;

	ref->node_name = kstrndup(tgt, strlen(tgt), GFP_KERNEL);
	if (!ref->node_name) {
		rc = -ENOMEM;
		goto err_free_path;
	}

	ref->ttl = ce->ce_ttl;
	ref->server_type = ce->ce_srvtype;
	ref->ref_flag = ce->ce_flags;

	return 0;

err_free_path:
	kfree(ref->path_name);
	ref->path_name = NULL;
	return rc;
}

/* Return target list of a DFS cache entry */
static int get_tgt_list(const struct dfs_cache_entry *ce,
			struct dfs_cache_tgt_list *tl)
{
	int rc;
	struct list_head *head = &tl->tl_list;
	struct dfs_cache_tgt *t;
	struct dfs_cache_tgt_iterator *it, *nit;

	memset(tl, 0, sizeof(*tl));
	INIT_LIST_HEAD(head);

	list_for_each_entry(t, &ce->ce_tlist, t_list) {
		it = kzalloc(sizeof(*it), GFP_KERNEL);
		if (!it) {
			rc = -ENOMEM;
			goto err_free_it;
		}

		it->it_name = kstrndup(t->t_name, strlen(t->t_name),
				       GFP_KERNEL);
		if (!it->it_name) {
			kfree(it);
			rc = -ENOMEM;
			goto err_free_it;
		}

		if (ce->ce_tgthint == t)
			list_add(&it->it_list, head);
		else
			list_add_tail(&it->it_list, head);
	}
	tl->tl_numtgts = ce->ce_numtgts;

	return 0;

err_free_it:
	list_for_each_entry_safe(it, nit, head, it_list) {
		kfree(it->it_name);
		kfree(it);
	}
	return rc;
}

/**
 * dfs_cache_find - find a DFS cache entry
 *
 * If it doesn't find the cache entry, then it will get a DFS referral
 * for @path and create a new entry.
 *
 * In case the cache entry exists but expired, it will get a DFS referral
 * for @path and then update the respective cache entry.
 *
 * These parameters are passed down to the get_dfs_refer() call if it
 * needs to be issued:
 * @xid: syscall xid
 * @ses: smb session to issue the request on
 * @nls_codepage: charset conversion
 * @remap: path character remapping type
 * @path: path to lookup in DFS referral cache.
 *
 * @ref: when non-NULL, store single DFS referral result in it.
 * @tgt_list: when non-NULL, store complete DFS target list in it.
 *
 * Return zero if the target was found, otherwise non-zero.
 */
int dfs_cache_find(const unsigned int xid, struct cifs_ses *ses,
		   const struct nls_table *nls_codepage, int remap,
		   const char *path, struct dfs_info3_param *ref,
		   struct dfs_cache_tgt_list *tgt_list)
{
	int rc;
	char *npath;
	struct dfs_cache_entry *ce;

	if (unlikely(!is_path_valid(path)))
		return -EINVAL;

	rc = get_normalized_path(path, &npath);
	if (rc)
		return rc;

	mutex_lock(&dfs_cache_list_lock);
	ce = do_dfs_cache_find(xid, ses, nls_codepage, remap, npath, false);
	if (!IS_ERR(ce)) {
		if (ref)
			rc = setup_ref(path, ce, ref, get_tgt_name(ce));
		else
			rc = 0;
		if (!rc && tgt_list)
			rc = get_tgt_list(ce, tgt_list);
	} else {
		rc = PTR_ERR(ce);
	}
	mutex_unlock(&dfs_cache_list_lock);
	free_normalized_path(path, npath);
	return rc;
}

/**
 * dfs_cache_noreq_find - find a DFS cache entry without sending any requests to
 * the currently connected server.
 *
 * NOTE: This function will neither update a cache entry in case it was
 * expired, nor create a new cache entry if @path hasn't been found. It heavily
 * relies on an existing cache entry.
 *
 * @path: path to lookup in the DFS referral cache.
 * @ref: when non-NULL, store single DFS referral result in it.
 * @tgt_list: when non-NULL, store complete DFS target list in it.
 *
 * Return 0 if successful.
 * Return -ENOENT if the entry was not found.
 * Return non-zero for other errors.
 */
int dfs_cache_noreq_find(const char *path, struct dfs_info3_param *ref,
			 struct dfs_cache_tgt_list *tgt_list)
{
	int rc;
	char *npath;
	struct dfs_cache_entry *ce;

	if (unlikely(!is_path_valid(path)))
		return -EINVAL;

	rc = get_normalized_path(path, &npath);
	if (rc)
		return rc;

	mutex_lock(&dfs_cache_list_lock);
	ce = do_dfs_cache_find(0, NULL, NULL, 0, npath, true);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out;
	}

	if (ref)
		rc = setup_ref(path, ce, ref, get_tgt_name(ce));
	else
		rc = 0;
	if (!rc && tgt_list)
		rc = get_tgt_list(ce, tgt_list);
out:
	mutex_unlock(&dfs_cache_list_lock);
	free_normalized_path(path, npath);
	return rc;
}

/**
 * dfs_cache_update_tgthint - update target hint of a DFS cache entry
 *
 * If it doesn't find the cache entry, then it will get a DFS referral for @path
 * and create a new entry.
 *
 * In case the cache entry exists but expired, it will get a DFS referral
 * for @path and then update the respective cache entry.
 *
 * @xid: syscall id
 * @ses: smb session
 * @nls_codepage: charset conversion
 * @remap: type of character remapping for paths
 * @path: path to lookup in DFS referral cache.
 * @it: DFS target iterator
 *
 * Return zero if the target hint was updated successfully, otherwise non-zero.
 */
int dfs_cache_update_tgthint(const unsigned int xid, struct cifs_ses *ses,
			     const struct nls_table *nls_codepage, int remap,
			     const char *path,
			     const struct dfs_cache_tgt_iterator *it)
{
	int rc;
	char *npath;
	struct dfs_cache_entry *ce;
	struct dfs_cache_tgt *t;

	if (unlikely(!is_path_valid(path)))
		return -EINVAL;

	rc = get_normalized_path(path, &npath);
	if (rc)
		return rc;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, npath);

	mutex_lock(&dfs_cache_list_lock);
	ce = do_dfs_cache_find(xid, ses, nls_codepage, remap, npath, false);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out;
	}

	rc = 0;

	t = ce->ce_tgthint;

	if (likely(!strcasecmp(it->it_name, t->t_name)))
		goto out;

	list_for_each_entry(t, &ce->ce_tlist, t_list) {
		if (!strcasecmp(t->t_name, it->it_name)) {
			ce->ce_tgthint = t;
			cifs_dbg(FYI, "%s: new target hint: %s\n", __func__,
				 it->it_name);
			break;
		}
	}

out:
	mutex_unlock(&dfs_cache_list_lock);
	free_normalized_path(path, npath);
	return rc;
}

/**
 * dfs_cache_noreq_update_tgthint - update target hint of a DFS cache entry
 * without sending any requests to the currently connected server.
 *
 * NOTE: This function will neither update a cache entry in case it was
 * expired, nor create a new cache entry if @path hasn't been found. It heavily
 * relies on an existing cache entry.
 *
 * @path: path to lookup in DFS referral cache.
 * @it: target iterator which contains the target hint to update the cache
 * entry with.
 *
 * Return zero if the target hint was updated successfully, otherwise non-zero.
 */
int dfs_cache_noreq_update_tgthint(const char *path,
				   const struct dfs_cache_tgt_iterator *it)
{
	int rc;
	char *npath;
	struct dfs_cache_entry *ce;
	struct dfs_cache_tgt *t;

	if (unlikely(!is_path_valid(path)) || !it)
		return -EINVAL;

	rc = get_normalized_path(path, &npath);
	if (rc)
		return rc;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, npath);

	mutex_lock(&dfs_cache_list_lock);

	ce = do_dfs_cache_find(0, NULL, NULL, 0, npath, true);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out;
	}

	rc = 0;

	t = ce->ce_tgthint;

	if (unlikely(!strcasecmp(it->it_name, t->t_name)))
		goto out;

	list_for_each_entry(t, &ce->ce_tlist, t_list) {
		if (!strcasecmp(t->t_name, it->it_name)) {
			ce->ce_tgthint = t;
			cifs_dbg(FYI, "%s: new target hint: %s\n", __func__,
				 it->it_name);
			break;
		}
	}

out:
	mutex_unlock(&dfs_cache_list_lock);
	free_normalized_path(path, npath);
	return rc;
}

/**
 * dfs_cache_get_tgt_referral - returns a DFS referral (@ref) from a given
 * target iterator (@it).
 *
 * @path: path to lookup in DFS referral cache.
 * @it: DFS target iterator.
 * @ref: DFS referral pointer to set up the gathered information.
 *
 * Return zero if the DFS referral was set up correctly, otherwise non-zero.
 */
int dfs_cache_get_tgt_referral(const char *path,
			       const struct dfs_cache_tgt_iterator *it,
			       struct dfs_info3_param *ref)
{
	int rc;
	char *npath;
	struct dfs_cache_entry *ce;
	unsigned int h;

	if (!it || !ref)
		return -EINVAL;
	if (unlikely(!is_path_valid(path)))
		return -EINVAL;

	rc = get_normalized_path(path, &npath);
	if (rc)
		return rc;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, npath);

	mutex_lock(&dfs_cache_list_lock);

	ce = find_cache_entry(npath, &h);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out;
	}

	cifs_dbg(FYI, "%s: target name: %s\n", __func__, it->it_name);

	rc = setup_ref(path, ce, ref, it->it_name);

out:
	mutex_unlock(&dfs_cache_list_lock);
	free_normalized_path(path, npath);
	return rc;
}

static int dup_vol(struct smb_vol *vol, struct smb_vol *new)
{
	memcpy(new, vol, sizeof(*new));

	if (vol->username) {
		new->username = kstrndup(vol->username, strlen(vol->username),
					GFP_KERNEL);
		if (!new->username)
			return -ENOMEM;
	}
	if (vol->password) {
		new->password = kstrndup(vol->password, strlen(vol->password),
					 GFP_KERNEL);
		if (!new->password)
			goto err_free_username;
	}
	if (vol->UNC) {
		cifs_dbg(FYI, "%s: vol->UNC: %s\n", __func__, vol->UNC);
		new->UNC = kstrndup(vol->UNC, strlen(vol->UNC), GFP_KERNEL);
		if (!new->UNC)
			goto err_free_password;
	}
	if (vol->domainname) {
		new->domainname = kstrndup(vol->domainname,
					  strlen(vol->domainname), GFP_KERNEL);
		if (!new->domainname)
			goto err_free_unc;
	}
	if (vol->iocharset) {
		new->iocharset = kstrndup(vol->iocharset,
					  strlen(vol->iocharset), GFP_KERNEL);
		if (!new->iocharset)
			goto err_free_domainname;
	}
	if (vol->prepath) {
		cifs_dbg(FYI, "%s: vol->prepath: %s\n", __func__, vol->prepath);
		new->prepath = kstrndup(vol->prepath, strlen(vol->prepath),
					GFP_KERNEL);
		if (!new->prepath)
			goto err_free_iocharset;
	}

	return 0;

err_free_iocharset:
	kfree(new->iocharset);
err_free_domainname:
	kfree(new->domainname);
err_free_unc:
	kfree(new->UNC);
err_free_password:
	kzfree(new->password);
err_free_username:
	kfree(new->username);
	kfree(new);
	return -ENOMEM;
}

/**
 * dfs_cache_add_vol - add a cifs volume during mount() that will be handled by
 * DFS cache refresh worker.
 *
 * @mntdata: mount data.
 * @vol: cifs volume.
 * @fullpath: origin full path.
 *
 * Return zero if volume was set up correctly, otherwise non-zero.
 */
int dfs_cache_add_vol(char *mntdata, struct smb_vol *vol, const char *fullpath)
{
	int rc;
	struct dfs_cache_vol_info *vi;

	if (!vol || !fullpath || !mntdata)
		return -EINVAL;

	cifs_dbg(FYI, "%s: fullpath: %s\n", __func__, fullpath);

	vi = kzalloc(sizeof(*vi), GFP_KERNEL);
	if (!vi)
		return -ENOMEM;

	vi->vi_fullpath = kstrndup(fullpath, strlen(fullpath), GFP_KERNEL);
	if (!vi->vi_fullpath) {
		rc = -ENOMEM;
		goto err_free_vi;
	}

	rc = dup_vol(vol, &vi->vi_vol);
	if (rc)
		goto err_free_fullpath;

	vi->vi_mntdata = mntdata;

	mutex_lock(&dfs_cache.dc_lock);
	list_add_tail(&vi->vi_list, &dfs_cache.dc_vol_list);
	mutex_unlock(&dfs_cache.dc_lock);
	return 0;

err_free_fullpath:
	kfree(vi->vi_fullpath);
err_free_vi:
	kfree(vi);
	return rc;
}

static inline struct dfs_cache_vol_info *find_vol(const char *fullpath)
{
	struct dfs_cache_vol_info *vi;

	list_for_each_entry(vi, &dfs_cache.dc_vol_list, vi_list) {
		cifs_dbg(FYI, "%s: vi->vi_fullpath: %s\n", __func__,
			 vi->vi_fullpath);
		if (!strcasecmp(vi->vi_fullpath, fullpath))
			return vi;
	}
	return ERR_PTR(-ENOENT);
}

/**
 * dfs_cache_update_vol - update vol info in DFS cache after failover
 *
 * @fullpath: fullpath to look up in volume list.
 * @server: TCP ses pointer.
 *
 * Return zero if volume was updated, otherwise non-zero.
 */
int dfs_cache_update_vol(const char *fullpath, struct TCP_Server_Info *server)
{
	int rc;
	struct dfs_cache_vol_info *vi;

	if (!fullpath || !server)
		return -EINVAL;

	cifs_dbg(FYI, "%s: fullpath: %s\n", __func__, fullpath);

	mutex_lock(&dfs_cache.dc_lock);

	vi = find_vol(fullpath);
	if (IS_ERR(vi)) {
		rc = PTR_ERR(vi);
		goto out;
	}

	cifs_dbg(FYI, "%s: updating volume info\n", __func__);
	memcpy(&vi->vi_vol.dstaddr, &server->dstaddr,
	       sizeof(vi->vi_vol.dstaddr));
	rc = 0;

out:
	mutex_unlock(&dfs_cache.dc_lock);
	return rc;
}

/**
 * dfs_cache_del_vol - remove volume info in DFS cache during umount()
 *
 * @fullpath: fullpath to look up in volume list.
 */
void dfs_cache_del_vol(const char *fullpath)
{
	struct dfs_cache_vol_info *vi;

	if (!fullpath || !*fullpath)
		return;

	cifs_dbg(FYI, "%s: fullpath: %s\n", __func__, fullpath);

	mutex_lock(&dfs_cache.dc_lock);
	vi = find_vol(fullpath);
	if (!IS_ERR(vi))
		free_vol(vi);
	mutex_unlock(&dfs_cache.dc_lock);
}

/* Get all tcons that are within a DFS namespace and can be refreshed */
static void get_tcons(struct TCP_Server_Info *server, struct list_head *head)
{
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;

	INIT_LIST_HEAD(head);

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
		list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
			if (!tcon->need_reconnect && !tcon->need_reopen_files &&
			    tcon->dfs_path) {
				tcon->tc_count++;
				list_add_tail(&tcon->ulist, head);
			}
		}
		if (ses->tcon_ipc && !ses->tcon_ipc->need_reconnect &&
		    ses->tcon_ipc->dfs_path) {
			list_add_tail(&ses->tcon_ipc->ulist, head);
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);
}

static inline bool is_dfs_link(const char *path)
{
	char *s;

	s = strchr(path + 1, '\\');
	if (!s)
		return false;
	return !!strchr(s + 1, '\\');
}

static inline char *get_dfs_root(const char *path)
{
	char *s, *npath;

	s = strchr(path + 1, '\\');
	if (!s)
		return ERR_PTR(-EINVAL);

	s = strchr(s + 1, '\\');
	if (!s)
		return ERR_PTR(-EINVAL);

	npath = kstrndup(path, s - path, GFP_KERNEL);
	if (!npath)
		return ERR_PTR(-ENOMEM);

	return npath;
}

/* Find root SMB session out of a DFS link path */
static struct cifs_ses *find_root_ses(struct dfs_cache_vol_info *vi,
				      struct cifs_tcon *tcon, const char *path)
{
	char *rpath;
	int rc;
	struct dfs_info3_param ref = {0};
	char *mdata = NULL, *devname = NULL;
	bool is_smb3 = tcon->ses->server->vals->header_preamble_size == 0;
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct smb_vol vol;

	rpath = get_dfs_root(path);
	if (IS_ERR(rpath))
		return ERR_CAST(rpath);

	memset(&vol, 0, sizeof(vol));

	rc = dfs_cache_noreq_find(rpath, &ref, NULL);
	if (rc) {
		ses = ERR_PTR(rc);
		goto out;
	}

	mdata = cifs_compose_mount_options(vi->vi_mntdata, rpath, &ref,
					   &devname);
	free_dfs_info_param(&ref);

	if (IS_ERR(mdata)) {
		ses = ERR_CAST(mdata);
		mdata = NULL;
		goto out;
	}

	rc = cifs_setup_volume_info(&vol, mdata, devname, is_smb3);
	kfree(devname);

	if (rc) {
		ses = ERR_PTR(rc);
		goto out;
	}

	server = cifs_find_tcp_session(&vol);
	if (IS_ERR_OR_NULL(server)) {
		ses = ERR_PTR(-EHOSTDOWN);
		goto out;
	}
	if (server->tcpStatus != CifsGood) {
		cifs_put_tcp_session(server, 0);
		ses = ERR_PTR(-EHOSTDOWN);
		goto out;
	}

	ses = cifs_get_smb_ses(server, &vol);

out:
	cifs_cleanup_volume_info_contents(&vol);
	kfree(mdata);
	kfree(rpath);

	return ses;
}

/* Refresh DFS cache entry from a given tcon */
static void do_refresh_tcon(struct dfs_cache *dc, struct dfs_cache_vol_info *vi,
			    struct cifs_tcon *tcon)
{
	int rc = 0;
	unsigned int xid;
	char *path, *npath;
	unsigned int h;
	struct dfs_cache_entry *ce;
	struct dfs_info3_param *refs = NULL;
	int numrefs = 0;
	struct cifs_ses *root_ses = NULL, *ses;

	xid = get_xid();

	path = tcon->dfs_path + 1;

	rc = get_normalized_path(path, &npath);
	if (rc)
		goto out;

	mutex_lock(&dfs_cache_list_lock);
	ce = find_cache_entry(npath, &h);
	mutex_unlock(&dfs_cache_list_lock);

	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out;
	}

	if (!cache_entry_expired(ce))
		goto out;

	/* If it's a DFS Link, then use root SMB session for refreshing it */
	if (is_dfs_link(npath)) {
		ses = root_ses = find_root_ses(vi, tcon, npath);
		if (IS_ERR(ses)) {
			rc = PTR_ERR(ses);
			root_ses = NULL;
			goto out;
		}
	} else {
		ses = tcon->ses;
	}

	if (unlikely(!ses->server->ops->get_dfs_refer)) {
		rc = -EOPNOTSUPP;
	} else {
		rc = ses->server->ops->get_dfs_refer(xid, ses, path, &refs,
						     &numrefs, dc->dc_nlsc,
						     tcon->remap);
		if (!rc) {
			mutex_lock(&dfs_cache_list_lock);
			ce = __update_cache_entry(npath, refs, numrefs);
			mutex_unlock(&dfs_cache_list_lock);
			dump_refs(refs, numrefs);
			free_dfs_info_array(refs, numrefs);
			if (IS_ERR(ce))
				rc = PTR_ERR(ce);
		}
	}

out:
	if (root_ses)
		cifs_put_smb_ses(root_ses);

	free_xid(xid);
	free_normalized_path(path, npath);
}

/*
 * Worker that will refresh DFS cache based on lowest TTL value from a DFS
 * referral.
 */
static void refresh_cache_worker(struct work_struct *work)
{
	struct dfs_cache *dc = container_of(work, struct dfs_cache,
					    dc_refresh.work);
	struct dfs_cache_vol_info *vi;
	struct TCP_Server_Info *server;
	LIST_HEAD(list);
	struct cifs_tcon *tcon, *ntcon;

	mutex_lock(&dc->dc_lock);

	list_for_each_entry(vi, &dc->dc_vol_list, vi_list) {
		server = cifs_find_tcp_session(&vi->vi_vol);
		if (IS_ERR_OR_NULL(server))
			continue;
		if (server->tcpStatus != CifsGood)
			goto next;
		get_tcons(server, &list);
		list_for_each_entry_safe(tcon, ntcon, &list, ulist) {
			do_refresh_tcon(dc, vi, tcon);
			list_del_init(&tcon->ulist);
			cifs_put_tcon(tcon);
		}
next:
		cifs_put_tcp_session(server, 0);
	}
	queue_delayed_work(cifsiod_wq, &dc->dc_refresh, dc->dc_ttl * HZ);
	mutex_unlock(&dc->dc_lock);
}
