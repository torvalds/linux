// SPDX-License-Identifier: GPL-2.0
/*
 * DFS referral cache routines
 *
 * Copyright (c) 2018-2019 Paulo Alcantara <palcantara@suse.de>
 */

#include <linux/jhash.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/nls.h>
#include <linux/workqueue.h>
#include <linux/uuid.h>
#include "cifsglob.h"
#include "smb2pdu.h"
#include "smb2proto.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "smb2glob.h"
#include "dns_resolve.h"

#include "dfs_cache.h"

#define CACHE_HTABLE_SIZE 32
#define CACHE_MAX_ENTRIES 64
#define CACHE_MIN_TTL 120 /* 2 minutes */

#define IS_DFS_INTERLINK(v) (((v) & DFSREF_REFERRAL_SERVER) && !((v) & DFSREF_STORAGE_SERVER))

struct cache_dfs_tgt {
	char *name;
	int path_consumed;
	struct list_head list;
};

struct cache_entry {
	struct hlist_node hlist;
	const char *path;
	int hdr_flags; /* RESP_GET_DFS_REFERRAL.ReferralHeaderFlags */
	int ttl; /* DFS_REREFERRAL_V3.TimeToLive */
	int srvtype; /* DFS_REREFERRAL_V3.ServerType */
	int ref_flags; /* DFS_REREFERRAL_V3.ReferralEntryFlags */
	struct timespec64 etime;
	int path_consumed; /* RESP_GET_DFS_REFERRAL.PathConsumed */
	int numtgts;
	struct list_head tlist;
	struct cache_dfs_tgt *tgthint;
};

/* List of referral server sessions per dfs mount */
struct mount_group {
	struct list_head list;
	uuid_t id;
	struct cifs_ses *sessions[CACHE_MAX_ENTRIES];
	int num_sessions;
	spinlock_t lock;
	struct list_head refresh_list;
	struct kref refcount;
};

static struct kmem_cache *cache_slab __read_mostly;
static struct workqueue_struct *dfscache_wq __read_mostly;

static int cache_ttl;
static DEFINE_SPINLOCK(cache_ttl_lock);

static struct nls_table *cache_cp;

/*
 * Number of entries in the cache
 */
static atomic_t cache_count;

static struct hlist_head cache_htable[CACHE_HTABLE_SIZE];
static DECLARE_RWSEM(htable_rw_lock);

static LIST_HEAD(mount_group_list);
static DEFINE_MUTEX(mount_group_list_lock);

static void refresh_cache_worker(struct work_struct *work);

static DECLARE_DELAYED_WORK(refresh_task, refresh_cache_worker);

static void get_ipc_unc(const char *ref_path, char *ipc, size_t ipclen)
{
	const char *host;
	size_t len;

	extract_unc_hostname(ref_path, &host, &len);
	scnprintf(ipc, ipclen, "\\\\%.*s\\IPC$", (int)len, host);
}

static struct cifs_ses *find_ipc_from_server_path(struct cifs_ses **ses, const char *path)
{
	char unc[SERVER_NAME_LENGTH + sizeof("//x/IPC$")] = {0};

	get_ipc_unc(path, unc, sizeof(unc));
	for (; *ses; ses++) {
		if (!strcasecmp(unc, (*ses)->tcon_ipc->treeName))
			return *ses;
	}
	return ERR_PTR(-ENOENT);
}

static void __mount_group_release(struct mount_group *mg)
{
	int i;

	for (i = 0; i < mg->num_sessions; i++)
		cifs_put_smb_ses(mg->sessions[i]);
	kfree(mg);
}

static void mount_group_release(struct kref *kref)
{
	struct mount_group *mg = container_of(kref, struct mount_group, refcount);

	mutex_lock(&mount_group_list_lock);
	list_del(&mg->list);
	mutex_unlock(&mount_group_list_lock);
	__mount_group_release(mg);
}

static struct mount_group *find_mount_group_locked(const uuid_t *id)
{
	struct mount_group *mg;

	list_for_each_entry(mg, &mount_group_list, list) {
		if (uuid_equal(&mg->id, id))
			return mg;
	}
	return ERR_PTR(-ENOENT);
}

static struct mount_group *__get_mount_group_locked(const uuid_t *id)
{
	struct mount_group *mg;

	mg = find_mount_group_locked(id);
	if (!IS_ERR(mg))
		return mg;

	mg = kmalloc(sizeof(*mg), GFP_KERNEL);
	if (!mg)
		return ERR_PTR(-ENOMEM);
	kref_init(&mg->refcount);
	uuid_copy(&mg->id, id);
	mg->num_sessions = 0;
	spin_lock_init(&mg->lock);
	list_add(&mg->list, &mount_group_list);
	return mg;
}

static struct mount_group *get_mount_group(const uuid_t *id)
{
	struct mount_group *mg;

	mutex_lock(&mount_group_list_lock);
	mg = __get_mount_group_locked(id);
	if (!IS_ERR(mg))
		kref_get(&mg->refcount);
	mutex_unlock(&mount_group_list_lock);

	return mg;
}

static void free_mount_group_list(void)
{
	struct mount_group *mg, *tmp_mg;

	list_for_each_entry_safe(mg, tmp_mg, &mount_group_list, list) {
		list_del_init(&mg->list);
		__mount_group_release(mg);
	}
}

/**
 * dfs_cache_canonical_path - get a canonical DFS path
 *
 * @path: DFS path
 * @cp: codepage
 * @remap: mapping type
 *
 * Return canonical path if success, otherwise error.
 */
char *dfs_cache_canonical_path(const char *path, const struct nls_table *cp, int remap)
{
	char *tmp;
	int plen = 0;
	char *npath;

	if (!path || strlen(path) < 3 || (*path != '\\' && *path != '/'))
		return ERR_PTR(-EINVAL);

	if (unlikely(strcmp(cp->charset, cache_cp->charset))) {
		tmp = (char *)cifs_strndup_to_utf16(path, strlen(path), &plen, cp, remap);
		if (!tmp) {
			cifs_dbg(VFS, "%s: failed to convert path to utf16\n", __func__);
			return ERR_PTR(-EINVAL);
		}

		npath = cifs_strndup_from_utf16(tmp, plen, true, cache_cp);
		kfree(tmp);

		if (!npath) {
			cifs_dbg(VFS, "%s: failed to convert path from utf16\n", __func__);
			return ERR_PTR(-EINVAL);
		}
	} else {
		npath = kstrdup(path, GFP_KERNEL);
		if (!npath)
			return ERR_PTR(-ENOMEM);
	}
	convert_delimiter(npath, '\\');
	return npath;
}

static inline bool cache_entry_expired(const struct cache_entry *ce)
{
	struct timespec64 ts;

	ktime_get_coarse_real_ts64(&ts);
	return timespec64_compare(&ts, &ce->etime) >= 0;
}

static inline void free_tgts(struct cache_entry *ce)
{
	struct cache_dfs_tgt *t, *n;

	list_for_each_entry_safe(t, n, &ce->tlist, list) {
		list_del(&t->list);
		kfree(t->name);
		kfree(t);
	}
}

static inline void flush_cache_ent(struct cache_entry *ce)
{
	hlist_del_init(&ce->hlist);
	kfree(ce->path);
	free_tgts(ce);
	atomic_dec(&cache_count);
	kmem_cache_free(cache_slab, ce);
}

static void flush_cache_ents(void)
{
	int i;

	for (i = 0; i < CACHE_HTABLE_SIZE; i++) {
		struct hlist_head *l = &cache_htable[i];
		struct hlist_node *n;
		struct cache_entry *ce;

		hlist_for_each_entry_safe(ce, n, l, hlist) {
			if (!hlist_unhashed(&ce->hlist))
				flush_cache_ent(ce);
		}
	}
}

/*
 * dfs cache /proc file
 */
static int dfscache_proc_show(struct seq_file *m, void *v)
{
	int i;
	struct cache_entry *ce;
	struct cache_dfs_tgt *t;

	seq_puts(m, "DFS cache\n---------\n");

	down_read(&htable_rw_lock);
	for (i = 0; i < CACHE_HTABLE_SIZE; i++) {
		struct hlist_head *l = &cache_htable[i];

		hlist_for_each_entry(ce, l, hlist) {
			if (hlist_unhashed(&ce->hlist))
				continue;

			seq_printf(m,
				   "cache entry: path=%s,type=%s,ttl=%d,etime=%ld,hdr_flags=0x%x,ref_flags=0x%x,interlink=%s,path_consumed=%d,expired=%s\n",
				   ce->path, ce->srvtype == DFS_TYPE_ROOT ? "root" : "link",
				   ce->ttl, ce->etime.tv_nsec, ce->hdr_flags, ce->ref_flags,
				   IS_DFS_INTERLINK(ce->hdr_flags) ? "yes" : "no",
				   ce->path_consumed, cache_entry_expired(ce) ? "yes" : "no");

			list_for_each_entry(t, &ce->tlist, list) {
				seq_printf(m, "  %s%s\n",
					   t->name,
					   ce->tgthint == t ? " (target hint)" : "");
			}
		}
	}
	up_read(&htable_rw_lock);

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

	cifs_dbg(FYI, "clearing dfs cache\n");

	down_write(&htable_rw_lock);
	flush_cache_ents();
	up_write(&htable_rw_lock);

	return count;
}

static int dfscache_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dfscache_proc_show, NULL);
}

const struct proc_ops dfscache_proc_ops = {
	.proc_open	= dfscache_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= dfscache_proc_write,
};

#ifdef CONFIG_CIFS_DEBUG2
static inline void dump_tgts(const struct cache_entry *ce)
{
	struct cache_dfs_tgt *t;

	cifs_dbg(FYI, "target list:\n");
	list_for_each_entry(t, &ce->tlist, list) {
		cifs_dbg(FYI, "  %s%s\n", t->name,
			 ce->tgthint == t ? " (target hint)" : "");
	}
}

static inline void dump_ce(const struct cache_entry *ce)
{
	cifs_dbg(FYI, "cache entry: path=%s,type=%s,ttl=%d,etime=%ld,hdr_flags=0x%x,ref_flags=0x%x,interlink=%s,path_consumed=%d,expired=%s\n",
		 ce->path,
		 ce->srvtype == DFS_TYPE_ROOT ? "root" : "link", ce->ttl,
		 ce->etime.tv_nsec,
		 ce->hdr_flags, ce->ref_flags,
		 IS_DFS_INTERLINK(ce->hdr_flags) ? "yes" : "no",
		 ce->path_consumed,
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
	int rc;
	int i;

	dfscache_wq = alloc_workqueue("cifs-dfscache", WQ_FREEZABLE | WQ_UNBOUND, 1);
	if (!dfscache_wq)
		return -ENOMEM;

	cache_slab = kmem_cache_create("cifs_dfs_cache",
				       sizeof(struct cache_entry), 0,
				       SLAB_HWCACHE_ALIGN, NULL);
	if (!cache_slab) {
		rc = -ENOMEM;
		goto out_destroy_wq;
	}

	for (i = 0; i < CACHE_HTABLE_SIZE; i++)
		INIT_HLIST_HEAD(&cache_htable[i]);

	atomic_set(&cache_count, 0);
	cache_cp = load_nls("utf8");
	if (!cache_cp)
		cache_cp = load_nls_default();

	cifs_dbg(FYI, "%s: initialized DFS referral cache\n", __func__);
	return 0;

out_destroy_wq:
	destroy_workqueue(dfscache_wq);
	return rc;
}

static int cache_entry_hash(const void *data, int size, unsigned int *hash)
{
	int i, clen;
	const unsigned char *s = data;
	wchar_t c;
	unsigned int h = 0;

	for (i = 0; i < size; i += clen) {
		clen = cache_cp->char2uni(&s[i], size - i, &c);
		if (unlikely(clen < 0)) {
			cifs_dbg(VFS, "%s: can't convert char\n", __func__);
			return clen;
		}
		c = cifs_toupper(c);
		h = jhash(&c, sizeof(c), h);
	}
	*hash = h % CACHE_HTABLE_SIZE;
	return 0;
}

/* Return target hint of a DFS cache entry */
static inline char *get_tgt_name(const struct cache_entry *ce)
{
	struct cache_dfs_tgt *t = ce->tgthint;

	return t ? t->name : ERR_PTR(-ENOENT);
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
static struct cache_dfs_tgt *alloc_target(const char *name, int path_consumed)
{
	struct cache_dfs_tgt *t;

	t = kmalloc(sizeof(*t), GFP_ATOMIC);
	if (!t)
		return ERR_PTR(-ENOMEM);
	t->name = kstrdup(name, GFP_ATOMIC);
	if (!t->name) {
		kfree(t);
		return ERR_PTR(-ENOMEM);
	}
	t->path_consumed = path_consumed;
	INIT_LIST_HEAD(&t->list);
	return t;
}

/*
 * Copy DFS referral information to a cache entry and conditionally update
 * target hint.
 */
static int copy_ref_data(const struct dfs_info3_param *refs, int numrefs,
			 struct cache_entry *ce, const char *tgthint)
{
	int i;

	ce->ttl = max_t(int, refs[0].ttl, CACHE_MIN_TTL);
	ce->etime = get_expire_time(ce->ttl);
	ce->srvtype = refs[0].server_type;
	ce->hdr_flags = refs[0].flags;
	ce->ref_flags = refs[0].ref_flag;
	ce->path_consumed = refs[0].path_consumed;

	for (i = 0; i < numrefs; i++) {
		struct cache_dfs_tgt *t;

		t = alloc_target(refs[i].node_name, refs[i].path_consumed);
		if (IS_ERR(t)) {
			free_tgts(ce);
			return PTR_ERR(t);
		}
		if (tgthint && !strcasecmp(t->name, tgthint)) {
			list_add(&t->list, &ce->tlist);
			tgthint = NULL;
		} else {
			list_add_tail(&t->list, &ce->tlist);
		}
		ce->numtgts++;
	}

	ce->tgthint = list_first_entry_or_null(&ce->tlist,
					       struct cache_dfs_tgt, list);

	return 0;
}

/* Allocate a new cache entry */
static struct cache_entry *alloc_cache_entry(struct dfs_info3_param *refs, int numrefs)
{
	struct cache_entry *ce;
	int rc;

	ce = kmem_cache_zalloc(cache_slab, GFP_KERNEL);
	if (!ce)
		return ERR_PTR(-ENOMEM);

	ce->path = refs[0].path_name;
	refs[0].path_name = NULL;

	INIT_HLIST_NODE(&ce->hlist);
	INIT_LIST_HEAD(&ce->tlist);

	rc = copy_ref_data(refs, numrefs, ce, NULL);
	if (rc) {
		kfree(ce->path);
		kmem_cache_free(cache_slab, ce);
		ce = ERR_PTR(rc);
	}
	return ce;
}

static void remove_oldest_entry_locked(void)
{
	int i;
	struct cache_entry *ce;
	struct cache_entry *to_del = NULL;

	WARN_ON(!rwsem_is_locked(&htable_rw_lock));

	for (i = 0; i < CACHE_HTABLE_SIZE; i++) {
		struct hlist_head *l = &cache_htable[i];

		hlist_for_each_entry(ce, l, hlist) {
			if (hlist_unhashed(&ce->hlist))
				continue;
			if (!to_del || timespec64_compare(&ce->etime,
							  &to_del->etime) < 0)
				to_del = ce;
		}
	}

	if (!to_del) {
		cifs_dbg(FYI, "%s: no entry to remove\n", __func__);
		return;
	}

	cifs_dbg(FYI, "%s: removing entry\n", __func__);
	dump_ce(to_del);
	flush_cache_ent(to_del);
}

/* Add a new DFS cache entry */
static int add_cache_entry_locked(struct dfs_info3_param *refs, int numrefs)
{
	int rc;
	struct cache_entry *ce;
	unsigned int hash;

	WARN_ON(!rwsem_is_locked(&htable_rw_lock));

	if (atomic_read(&cache_count) >= CACHE_MAX_ENTRIES) {
		cifs_dbg(FYI, "%s: reached max cache size (%d)\n", __func__, CACHE_MAX_ENTRIES);
		remove_oldest_entry_locked();
	}

	rc = cache_entry_hash(refs[0].path_name, strlen(refs[0].path_name), &hash);
	if (rc)
		return rc;

	ce = alloc_cache_entry(refs, numrefs);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	spin_lock(&cache_ttl_lock);
	if (!cache_ttl) {
		cache_ttl = ce->ttl;
		queue_delayed_work(dfscache_wq, &refresh_task, cache_ttl * HZ);
	} else {
		cache_ttl = min_t(int, cache_ttl, ce->ttl);
		mod_delayed_work(dfscache_wq, &refresh_task, cache_ttl * HZ);
	}
	spin_unlock(&cache_ttl_lock);

	hlist_add_head(&ce->hlist, &cache_htable[hash]);
	dump_ce(ce);

	atomic_inc(&cache_count);

	return 0;
}

/* Check if two DFS paths are equal.  @s1 and @s2 are expected to be in @cache_cp's charset */
static bool dfs_path_equal(const char *s1, int len1, const char *s2, int len2)
{
	int i, l1, l2;
	wchar_t c1, c2;

	if (len1 != len2)
		return false;

	for (i = 0; i < len1; i += l1) {
		l1 = cache_cp->char2uni(&s1[i], len1 - i, &c1);
		l2 = cache_cp->char2uni(&s2[i], len2 - i, &c2);
		if (unlikely(l1 < 0 && l2 < 0)) {
			if (s1[i] != s2[i])
				return false;
			l1 = 1;
			continue;
		}
		if (l1 != l2)
			return false;
		if (cifs_toupper(c1) != cifs_toupper(c2))
			return false;
	}
	return true;
}

static struct cache_entry *__lookup_cache_entry(const char *path, unsigned int hash, int len)
{
	struct cache_entry *ce;

	hlist_for_each_entry(ce, &cache_htable[hash], hlist) {
		if (dfs_path_equal(ce->path, strlen(ce->path), path, len)) {
			dump_ce(ce);
			return ce;
		}
	}
	return ERR_PTR(-EEXIST);
}

/*
 * Find a DFS cache entry in hash table and optionally check prefix path against normalized @path.
 *
 * Use whole path components in the match.  Must be called with htable_rw_lock held.
 *
 * Return ERR_PTR(-EEXIST) if the entry is not found.
 */
static struct cache_entry *lookup_cache_entry(const char *path)
{
	struct cache_entry *ce;
	int cnt = 0;
	const char *s = path, *e;
	char sep = *s;
	unsigned int hash;
	int rc;

	while ((s = strchr(s, sep)) && ++cnt < 3)
		s++;

	if (cnt < 3) {
		rc = cache_entry_hash(path, strlen(path), &hash);
		if (rc)
			return ERR_PTR(rc);
		return __lookup_cache_entry(path, hash, strlen(path));
	}
	/*
	 * Handle paths that have more than two path components and are a complete prefix of the DFS
	 * referral request path (@path).
	 *
	 * See MS-DFSC 3.2.5.5 "Receiving a Root Referral Request or Link Referral Request".
	 */
	e = path + strlen(path) - 1;
	while (e > s) {
		int len;

		/* skip separators */
		while (e > s && *e == sep)
			e--;
		if (e == s)
			break;

		len = e + 1 - path;
		rc = cache_entry_hash(path, len, &hash);
		if (rc)
			return ERR_PTR(rc);
		ce = __lookup_cache_entry(path, hash, len);
		if (!IS_ERR(ce))
			return ce;

		/* backward until separator */
		while (e > s && *e != sep)
			e--;
	}
	return ERR_PTR(-EEXIST);
}

/**
 * dfs_cache_destroy - destroy DFS referral cache
 */
void dfs_cache_destroy(void)
{
	cancel_delayed_work_sync(&refresh_task);
	unload_nls(cache_cp);
	free_mount_group_list();
	flush_cache_ents();
	kmem_cache_destroy(cache_slab);
	destroy_workqueue(dfscache_wq);

	cifs_dbg(FYI, "%s: destroyed DFS referral cache\n", __func__);
}

/* Update a cache entry with the new referral in @refs */
static int update_cache_entry_locked(struct cache_entry *ce, const struct dfs_info3_param *refs,
				     int numrefs)
{
	int rc;
	char *s, *th = NULL;

	WARN_ON(!rwsem_is_locked(&htable_rw_lock));

	if (ce->tgthint) {
		s = ce->tgthint->name;
		th = kstrdup(s, GFP_ATOMIC);
		if (!th)
			return -ENOMEM;
	}

	free_tgts(ce);
	ce->numtgts = 0;

	rc = copy_ref_data(refs, numrefs, ce, th);

	kfree(th);

	return rc;
}

static int get_dfs_referral(const unsigned int xid, struct cifs_ses *ses, const char *path,
			    struct dfs_info3_param **refs, int *numrefs)
{
	int rc;
	int i;

	cifs_dbg(FYI, "%s: get an DFS referral for %s\n", __func__, path);

	*refs = NULL;
	*numrefs = 0;

	if (!ses || !ses->server || !ses->server->ops->get_dfs_refer)
		return -EOPNOTSUPP;
	if (unlikely(!cache_cp))
		return -EINVAL;

	rc =  ses->server->ops->get_dfs_refer(xid, ses, path, refs, numrefs, cache_cp,
					      NO_MAP_UNI_RSVD);
	if (!rc) {
		struct dfs_info3_param *ref = *refs;

		for (i = 0; i < *numrefs; i++)
			convert_delimiter(ref[i].path_name, '\\');
	}
	return rc;
}

/*
 * Find, create or update a DFS cache entry.
 *
 * If the entry wasn't found, it will create a new one. Or if it was found but
 * expired, then it will update the entry accordingly.
 *
 * For interlinks, cifs_mount() and expand_dfs_referral() are supposed to
 * handle them properly.
 */
static int cache_refresh_path(const unsigned int xid, struct cifs_ses *ses, const char *path)
{
	int rc;
	struct cache_entry *ce;
	struct dfs_info3_param *refs = NULL;
	int numrefs = 0;
	bool newent = false;

	cifs_dbg(FYI, "%s: search path: %s\n", __func__, path);

	down_write(&htable_rw_lock);

	ce = lookup_cache_entry(path);
	if (!IS_ERR(ce)) {
		if (!cache_entry_expired(ce)) {
			dump_ce(ce);
			up_write(&htable_rw_lock);
			return 0;
		}
	} else {
		newent = true;
	}

	/*
	 * Either the entry was not found, or it is expired.
	 * Request a new DFS referral in order to create or update a cache entry.
	 */
	rc = get_dfs_referral(xid, ses, path, &refs, &numrefs);
	if (rc)
		goto out_unlock;

	dump_refs(refs, numrefs);

	if (!newent) {
		rc = update_cache_entry_locked(ce, refs, numrefs);
		goto out_unlock;
	}

	rc = add_cache_entry_locked(refs, numrefs);

out_unlock:
	up_write(&htable_rw_lock);
	free_dfs_info_array(refs, numrefs);
	return rc;
}

/*
 * Set up a DFS referral from a given cache entry.
 *
 * Must be called with htable_rw_lock held.
 */
static int setup_referral(const char *path, struct cache_entry *ce,
			  struct dfs_info3_param *ref, const char *target)
{
	int rc;

	cifs_dbg(FYI, "%s: set up new ref\n", __func__);

	memset(ref, 0, sizeof(*ref));

	ref->path_name = kstrdup(path, GFP_ATOMIC);
	if (!ref->path_name)
		return -ENOMEM;

	ref->node_name = kstrdup(target, GFP_ATOMIC);
	if (!ref->node_name) {
		rc = -ENOMEM;
		goto err_free_path;
	}

	ref->path_consumed = ce->path_consumed;
	ref->ttl = ce->ttl;
	ref->server_type = ce->srvtype;
	ref->ref_flag = ce->ref_flags;
	ref->flags = ce->hdr_flags;

	return 0;

err_free_path:
	kfree(ref->path_name);
	ref->path_name = NULL;
	return rc;
}

/* Return target list of a DFS cache entry */
static int get_targets(struct cache_entry *ce, struct dfs_cache_tgt_list *tl)
{
	int rc;
	struct list_head *head = &tl->tl_list;
	struct cache_dfs_tgt *t;
	struct dfs_cache_tgt_iterator *it, *nit;

	memset(tl, 0, sizeof(*tl));
	INIT_LIST_HEAD(head);

	list_for_each_entry(t, &ce->tlist, list) {
		it = kzalloc(sizeof(*it), GFP_ATOMIC);
		if (!it) {
			rc = -ENOMEM;
			goto err_free_it;
		}

		it->it_name = kstrdup(t->name, GFP_ATOMIC);
		if (!it->it_name) {
			kfree(it);
			rc = -ENOMEM;
			goto err_free_it;
		}
		it->it_path_consumed = t->path_consumed;

		if (ce->tgthint == t)
			list_add(&it->it_list, head);
		else
			list_add_tail(&it->it_list, head);
	}

	tl->tl_numtgts = ce->numtgts;

	return 0;

err_free_it:
	list_for_each_entry_safe(it, nit, head, it_list) {
		list_del(&it->it_list);
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
 * @cp: codepage
 * @remap: path character remapping type
 * @path: path to lookup in DFS referral cache.
 *
 * @ref: when non-NULL, store single DFS referral result in it.
 * @tgt_list: when non-NULL, store complete DFS target list in it.
 *
 * Return zero if the target was found, otherwise non-zero.
 */
int dfs_cache_find(const unsigned int xid, struct cifs_ses *ses, const struct nls_table *cp,
		   int remap, const char *path, struct dfs_info3_param *ref,
		   struct dfs_cache_tgt_list *tgt_list)
{
	int rc;
	const char *npath;
	struct cache_entry *ce;

	npath = dfs_cache_canonical_path(path, cp, remap);
	if (IS_ERR(npath))
		return PTR_ERR(npath);

	rc = cache_refresh_path(xid, ses, npath);
	if (rc)
		goto out_free_path;

	down_read(&htable_rw_lock);

	ce = lookup_cache_entry(npath);
	if (IS_ERR(ce)) {
		up_read(&htable_rw_lock);
		rc = PTR_ERR(ce);
		goto out_free_path;
	}

	if (ref)
		rc = setup_referral(path, ce, ref, get_tgt_name(ce));
	else
		rc = 0;
	if (!rc && tgt_list)
		rc = get_targets(ce, tgt_list);

	up_read(&htable_rw_lock);

out_free_path:
	kfree(npath);
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
 * @path: canonical DFS path to lookup in the DFS referral cache.
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
	struct cache_entry *ce;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, path);

	down_read(&htable_rw_lock);

	ce = lookup_cache_entry(path);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out_unlock;
	}

	if (ref)
		rc = setup_referral(path, ce, ref, get_tgt_name(ce));
	else
		rc = 0;
	if (!rc && tgt_list)
		rc = get_targets(ce, tgt_list);

out_unlock:
	up_read(&htable_rw_lock);
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
 * @cp: codepage
 * @remap: type of character remapping for paths
 * @path: path to lookup in DFS referral cache
 * @it: DFS target iterator
 *
 * Return zero if the target hint was updated successfully, otherwise non-zero.
 */
int dfs_cache_update_tgthint(const unsigned int xid, struct cifs_ses *ses,
			     const struct nls_table *cp, int remap, const char *path,
			     const struct dfs_cache_tgt_iterator *it)
{
	int rc;
	const char *npath;
	struct cache_entry *ce;
	struct cache_dfs_tgt *t;

	npath = dfs_cache_canonical_path(path, cp, remap);
	if (IS_ERR(npath))
		return PTR_ERR(npath);

	cifs_dbg(FYI, "%s: update target hint - path: %s\n", __func__, npath);

	rc = cache_refresh_path(xid, ses, npath);
	if (rc)
		goto out_free_path;

	down_write(&htable_rw_lock);

	ce = lookup_cache_entry(npath);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out_unlock;
	}

	t = ce->tgthint;

	if (likely(!strcasecmp(it->it_name, t->name)))
		goto out_unlock;

	list_for_each_entry(t, &ce->tlist, list) {
		if (!strcasecmp(t->name, it->it_name)) {
			ce->tgthint = t;
			cifs_dbg(FYI, "%s: new target hint: %s\n", __func__,
				 it->it_name);
			break;
		}
	}

out_unlock:
	up_write(&htable_rw_lock);
out_free_path:
	kfree(npath);
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
 * @path: canonical DFS path to lookup in DFS referral cache.
 * @it: target iterator which contains the target hint to update the cache
 * entry with.
 *
 * Return zero if the target hint was updated successfully, otherwise non-zero.
 */
int dfs_cache_noreq_update_tgthint(const char *path, const struct dfs_cache_tgt_iterator *it)
{
	int rc;
	struct cache_entry *ce;
	struct cache_dfs_tgt *t;

	if (!it)
		return -EINVAL;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, path);

	down_write(&htable_rw_lock);

	ce = lookup_cache_entry(path);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out_unlock;
	}

	rc = 0;
	t = ce->tgthint;

	if (unlikely(!strcasecmp(it->it_name, t->name)))
		goto out_unlock;

	list_for_each_entry(t, &ce->tlist, list) {
		if (!strcasecmp(t->name, it->it_name)) {
			ce->tgthint = t;
			cifs_dbg(FYI, "%s: new target hint: %s\n", __func__,
				 it->it_name);
			break;
		}
	}

out_unlock:
	up_write(&htable_rw_lock);
	return rc;
}

/**
 * dfs_cache_get_tgt_referral - returns a DFS referral (@ref) from a given
 * target iterator (@it).
 *
 * @path: canonical DFS path to lookup in DFS referral cache.
 * @it: DFS target iterator.
 * @ref: DFS referral pointer to set up the gathered information.
 *
 * Return zero if the DFS referral was set up correctly, otherwise non-zero.
 */
int dfs_cache_get_tgt_referral(const char *path, const struct dfs_cache_tgt_iterator *it,
			       struct dfs_info3_param *ref)
{
	int rc;
	struct cache_entry *ce;

	if (!it || !ref)
		return -EINVAL;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, path);

	down_read(&htable_rw_lock);

	ce = lookup_cache_entry(path);
	if (IS_ERR(ce)) {
		rc = PTR_ERR(ce);
		goto out_unlock;
	}

	cifs_dbg(FYI, "%s: target name: %s\n", __func__, it->it_name);

	rc = setup_referral(path, ce, ref, it->it_name);

out_unlock:
	up_read(&htable_rw_lock);
	return rc;
}

/**
 * dfs_cache_add_refsrv_session - add SMB session of referral server
 *
 * @mount_id: mount group uuid to lookup.
 * @ses: reference counted SMB session of referral server.
 */
void dfs_cache_add_refsrv_session(const uuid_t *mount_id, struct cifs_ses *ses)
{
	struct mount_group *mg;

	if (WARN_ON_ONCE(!mount_id || uuid_is_null(mount_id) || !ses))
		return;

	mg = get_mount_group(mount_id);
	if (WARN_ON_ONCE(IS_ERR(mg)))
		return;

	spin_lock(&mg->lock);
	if (mg->num_sessions < ARRAY_SIZE(mg->sessions))
		mg->sessions[mg->num_sessions++] = ses;
	spin_unlock(&mg->lock);
	kref_put(&mg->refcount, mount_group_release);
}

/**
 * dfs_cache_put_refsrv_sessions - put all referral server sessions
 *
 * Put all SMB sessions from the given mount group id.
 *
 * @mount_id: mount group uuid to lookup.
 */
void dfs_cache_put_refsrv_sessions(const uuid_t *mount_id)
{
	struct mount_group *mg;

	if (!mount_id || uuid_is_null(mount_id))
		return;

	mutex_lock(&mount_group_list_lock);
	mg = find_mount_group_locked(mount_id);
	if (IS_ERR(mg)) {
		mutex_unlock(&mount_group_list_lock);
		return;
	}
	mutex_unlock(&mount_group_list_lock);
	kref_put(&mg->refcount, mount_group_release);
}

/**
 * dfs_cache_get_tgt_share - parse a DFS target
 *
 * @path: DFS full path
 * @it: DFS target iterator.
 * @share: tree name.
 * @prefix: prefix path.
 *
 * Return zero if target was parsed correctly, otherwise non-zero.
 */
int dfs_cache_get_tgt_share(char *path, const struct dfs_cache_tgt_iterator *it, char **share,
			    char **prefix)
{
	char *s, sep, *p;
	size_t len;
	size_t plen1, plen2;

	if (!it || !path || !share || !prefix || strlen(path) < it->it_path_consumed)
		return -EINVAL;

	*share = NULL;
	*prefix = NULL;

	sep = it->it_name[0];
	if (sep != '\\' && sep != '/')
		return -EINVAL;

	s = strchr(it->it_name + 1, sep);
	if (!s)
		return -EINVAL;

	/* point to prefix in target node */
	s = strchrnul(s + 1, sep);

	/* extract target share */
	*share = kstrndup(it->it_name, s - it->it_name, GFP_KERNEL);
	if (!*share)
		return -ENOMEM;

	/* skip separator */
	if (*s)
		s++;
	/* point to prefix in DFS path */
	p = path + it->it_path_consumed;
	if (*p == sep)
		p++;

	/* merge prefix paths from DFS path and target node */
	plen1 = it->it_name + strlen(it->it_name) - s;
	plen2 = path + strlen(path) - p;
	if (plen1 || plen2) {
		len = plen1 + plen2 + 2;
		*prefix = kmalloc(len, GFP_KERNEL);
		if (!*prefix) {
			kfree(*share);
			*share = NULL;
			return -ENOMEM;
		}
		if (plen1)
			scnprintf(*prefix, len, "%.*s%c%.*s", (int)plen1, s, sep, (int)plen2, p);
		else
			strscpy(*prefix, p, len);
	}
	return 0;
}

static bool target_share_equal(struct TCP_Server_Info *server, const char *s1, const char *s2)
{
	char unc[sizeof("\\\\") + SERVER_NAME_LENGTH] = {0};
	const char *host;
	size_t hostlen;
	char *ip = NULL;
	struct sockaddr sa;
	bool match;
	int rc;

	if (strcasecmp(s1, s2))
		return false;

	/*
	 * Resolve share's hostname and check if server address matches.  Otherwise just ignore it
	 * as we could not have upcall to resolve hostname or failed to convert ip address.
	 */
	match = true;
	extract_unc_hostname(s1, &host, &hostlen);
	scnprintf(unc, sizeof(unc), "\\\\%.*s", (int)hostlen, host);

	rc = dns_resolve_server_name_to_ip(unc, &ip, NULL);
	if (rc < 0) {
		cifs_dbg(FYI, "%s: could not resolve %.*s. assuming server address matches.\n",
			 __func__, (int)hostlen, host);
		return true;
	}

	if (!cifs_convert_address(&sa, ip, strlen(ip))) {
		cifs_dbg(VFS, "%s: failed to convert address \'%s\'. skip address matching.\n",
			 __func__, ip);
	} else {
		mutex_lock(&server->srv_mutex);
		match = cifs_match_ipaddr((struct sockaddr *)&server->dstaddr, &sa);
		mutex_unlock(&server->srv_mutex);
	}

	kfree(ip);
	return match;
}

/*
 * Mark dfs tcon for reconnecting when the currently connected tcon does not match any of the new
 * target shares in @refs.
 */
static void mark_for_reconnect_if_needed(struct cifs_tcon *tcon, struct dfs_cache_tgt_list *tl,
					 const struct dfs_info3_param *refs, int numrefs)
{
	struct dfs_cache_tgt_iterator *it;
	int i;

	for (it = dfs_cache_get_tgt_iterator(tl); it; it = dfs_cache_get_next_tgt(tl, it)) {
		for (i = 0; i < numrefs; i++) {
			if (target_share_equal(tcon->ses->server, dfs_cache_get_tgt_name(it),
					       refs[i].node_name))
				return;
		}
	}

	cifs_dbg(FYI, "%s: no cached or matched targets. mark dfs share for reconnect.\n", __func__);
	cifs_signal_cifsd_for_reconnect(tcon->ses->server, true);
}

/* Refresh dfs referral of tcon and mark it for reconnect if needed */
static int __refresh_tcon(const char *path, struct cifs_ses **sessions, struct cifs_tcon *tcon,
			  bool force_refresh)
{
	struct cifs_ses *ses;
	struct cache_entry *ce;
	struct dfs_info3_param *refs = NULL;
	int numrefs = 0;
	bool needs_refresh = false;
	struct dfs_cache_tgt_list tl = DFS_CACHE_TGT_LIST_INIT(tl);
	int rc = 0;
	unsigned int xid;

	ses = find_ipc_from_server_path(sessions, path);
	if (IS_ERR(ses)) {
		cifs_dbg(FYI, "%s: could not find ipc session\n", __func__);
		return PTR_ERR(ses);
	}

	down_read(&htable_rw_lock);
	ce = lookup_cache_entry(path);
	needs_refresh = force_refresh || IS_ERR(ce) || cache_entry_expired(ce);
	if (!IS_ERR(ce)) {
		rc = get_targets(ce, &tl);
		if (rc)
			cifs_dbg(FYI, "%s: could not get dfs targets: %d\n", __func__, rc);
	}
	up_read(&htable_rw_lock);

	if (!needs_refresh) {
		rc = 0;
		goto out;
	}

	xid = get_xid();
	rc = get_dfs_referral(xid, ses, path, &refs, &numrefs);
	free_xid(xid);

	/* Create or update a cache entry with the new referral */
	if (!rc) {
		dump_refs(refs, numrefs);

		down_write(&htable_rw_lock);
		ce = lookup_cache_entry(path);
		if (IS_ERR(ce))
			add_cache_entry_locked(refs, numrefs);
		else if (force_refresh || cache_entry_expired(ce))
			update_cache_entry_locked(ce, refs, numrefs);
		up_write(&htable_rw_lock);

		mark_for_reconnect_if_needed(tcon, &tl, refs, numrefs);
	}

out:
	dfs_cache_free_tgts(&tl);
	free_dfs_info_array(refs, numrefs);
	return rc;
}

static int refresh_tcon(struct cifs_ses **sessions, struct cifs_tcon *tcon, bool force_refresh)
{
	struct TCP_Server_Info *server = tcon->ses->server;

	mutex_lock(&server->refpath_lock);
	if (server->origin_fullpath) {
		if (server->leaf_fullpath && strcasecmp(server->leaf_fullpath,
							server->origin_fullpath))
			__refresh_tcon(server->leaf_fullpath + 1, sessions, tcon, force_refresh);
		__refresh_tcon(server->origin_fullpath + 1, sessions, tcon, force_refresh);
	}
	mutex_unlock(&server->refpath_lock);

	return 0;
}

/**
 * dfs_cache_remount_fs - remount a DFS share
 *
 * Reconfigure dfs mount by forcing a new DFS referral and if the currently cached targets do not
 * match any of the new targets, mark it for reconnect.
 *
 * @cifs_sb: cifs superblock.
 *
 * Return zero if remounted, otherwise non-zero.
 */
int dfs_cache_remount_fs(struct cifs_sb_info *cifs_sb)
{
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct mount_group *mg;
	struct cifs_ses *sessions[CACHE_MAX_ENTRIES + 1] = {NULL};
	int rc;

	if (!cifs_sb || !cifs_sb->master_tlink)
		return -EINVAL;

	tcon = cifs_sb_master_tcon(cifs_sb);
	server = tcon->ses->server;

	if (!server->origin_fullpath) {
		cifs_dbg(FYI, "%s: not a dfs mount\n", __func__);
		return 0;
	}

	if (uuid_is_null(&cifs_sb->dfs_mount_id)) {
		cifs_dbg(FYI, "%s: no dfs mount group id\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&mount_group_list_lock);
	mg = find_mount_group_locked(&cifs_sb->dfs_mount_id);
	if (IS_ERR(mg)) {
		mutex_unlock(&mount_group_list_lock);
		cifs_dbg(FYI, "%s: no ipc session for refreshing referral\n", __func__);
		return PTR_ERR(mg);
	}
	kref_get(&mg->refcount);
	mutex_unlock(&mount_group_list_lock);

	spin_lock(&mg->lock);
	memcpy(&sessions, mg->sessions, mg->num_sessions * sizeof(mg->sessions[0]));
	spin_unlock(&mg->lock);

	/*
	 * After reconnecting to a different server, unique ids won't match anymore, so we disable
	 * serverino. This prevents dentry revalidation to think the dentry are stale (ESTALE).
	 */
	cifs_autodisable_serverino(cifs_sb);
	/*
	 * Force the use of prefix path to support failover on DFS paths that resolve to targets
	 * that have different prefix paths.
	 */
	cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
	rc = refresh_tcon(sessions, tcon, true);

	kref_put(&mg->refcount, mount_group_release);
	return rc;
}

/*
 * Refresh all active dfs mounts regardless of whether they are in cache or not.
 * (cache can be cleared)
 */
static void refresh_mounts(struct cifs_ses **sessions)
{
	struct TCP_Server_Info *server;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon, *ntcon;
	struct list_head tcons;

	INIT_LIST_HEAD(&tcons);

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		if (!server->is_dfs_conn)
			continue;

		list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
			list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
				if (!tcon->ipc && !tcon->need_reconnect) {
					tcon->tc_count++;
					list_add_tail(&tcon->ulist, &tcons);
				}
			}
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	list_for_each_entry_safe(tcon, ntcon, &tcons, ulist) {
		struct TCP_Server_Info *server = tcon->ses->server;

		list_del_init(&tcon->ulist);

		mutex_lock(&server->refpath_lock);
		if (server->origin_fullpath) {
			if (server->leaf_fullpath && strcasecmp(server->leaf_fullpath,
								server->origin_fullpath))
				__refresh_tcon(server->leaf_fullpath + 1, sessions, tcon, false);
			__refresh_tcon(server->origin_fullpath + 1, sessions, tcon, false);
		}
		mutex_unlock(&server->refpath_lock);

		cifs_put_tcon(tcon);
	}
}

static void refresh_cache(struct cifs_ses **sessions)
{
	int i;
	struct cifs_ses *ses;
	unsigned int xid;
	char *ref_paths[CACHE_MAX_ENTRIES];
	int count = 0;
	struct cache_entry *ce;

	/*
	 * Refresh all cached entries.  Get all new referrals outside critical section to avoid
	 * starvation while performing SMB2 IOCTL on broken or slow connections.

	 * The cache entries may cover more paths than the active mounts
	 * (e.g. domain-based DFS referrals or multi tier DFS setups).
	 */
	down_read(&htable_rw_lock);
	for (i = 0; i < CACHE_HTABLE_SIZE; i++) {
		struct hlist_head *l = &cache_htable[i];

		hlist_for_each_entry(ce, l, hlist) {
			if (count == ARRAY_SIZE(ref_paths))
				goto out_unlock;
			if (hlist_unhashed(&ce->hlist) || !cache_entry_expired(ce) ||
			    IS_ERR(find_ipc_from_server_path(sessions, ce->path)))
				continue;
			ref_paths[count++] = kstrdup(ce->path, GFP_ATOMIC);
		}
	}

out_unlock:
	up_read(&htable_rw_lock);

	for (i = 0; i < count; i++) {
		char *path = ref_paths[i];
		struct dfs_info3_param *refs = NULL;
		int numrefs = 0;
		int rc = 0;

		if (!path)
			continue;

		ses = find_ipc_from_server_path(sessions, path);
		if (IS_ERR(ses))
			goto next_referral;

		xid = get_xid();
		rc = get_dfs_referral(xid, ses, path, &refs, &numrefs);
		free_xid(xid);

		if (!rc) {
			down_write(&htable_rw_lock);
			ce = lookup_cache_entry(path);
			/*
			 * We need to re-check it because other tasks might have it deleted or
			 * updated.
			 */
			if (!IS_ERR(ce) && cache_entry_expired(ce))
				update_cache_entry_locked(ce, refs, numrefs);
			up_write(&htable_rw_lock);
		}

next_referral:
		kfree(path);
		free_dfs_info_array(refs, numrefs);
	}
}

/*
 * Worker that will refresh DFS cache and active mounts based on lowest TTL value from a DFS
 * referral.
 */
static void refresh_cache_worker(struct work_struct *work)
{
	struct list_head mglist;
	struct mount_group *mg, *tmp_mg;
	struct cifs_ses *sessions[CACHE_MAX_ENTRIES + 1] = {NULL};
	int max_sessions = ARRAY_SIZE(sessions) - 1;
	int i = 0, count;

	INIT_LIST_HEAD(&mglist);

	/* Get refereces of mount groups */
	mutex_lock(&mount_group_list_lock);
	list_for_each_entry(mg, &mount_group_list, list) {
		kref_get(&mg->refcount);
		list_add(&mg->refresh_list, &mglist);
	}
	mutex_unlock(&mount_group_list_lock);

	/* Fill in local array with an NULL-terminated list of all referral server sessions */
	list_for_each_entry(mg, &mglist, refresh_list) {
		if (i >= max_sessions)
			break;

		spin_lock(&mg->lock);
		if (i + mg->num_sessions > max_sessions)
			count = max_sessions - i;
		else
			count = mg->num_sessions;
		memcpy(&sessions[i], mg->sessions, count * sizeof(mg->sessions[0]));
		spin_unlock(&mg->lock);
		i += count;
	}

	if (sessions[0]) {
		/* Refresh all active mounts and cached entries */
		refresh_mounts(sessions);
		refresh_cache(sessions);
	}

	list_for_each_entry_safe(mg, tmp_mg, &mglist, refresh_list) {
		list_del_init(&mg->refresh_list);
		kref_put(&mg->refcount, mount_group_release);
	}

	spin_lock(&cache_ttl_lock);
	queue_delayed_work(dfscache_wq, &refresh_task, cache_ttl * HZ);
	spin_unlock(&cache_ttl_lock);
}
