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
#include "dfs.h"

#include "dfs_cache.h"

#define CACHE_HTABLE_SIZE	512
#define CACHE_MAX_ENTRIES	1024
#define CACHE_MIN_TTL		120 /* 2 minutes */
#define CACHE_DEFAULT_TTL	300 /* 5 minutes */

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

static struct kmem_cache *cache_slab __read_mostly;
struct workqueue_struct *dfscache_wq;

atomic_t dfs_cache_ttl;

static struct nls_table *cache_cp;

/*
 * Number of entries in the cache
 */
static atomic_t cache_count;

static struct hlist_head cache_htable[CACHE_HTABLE_SIZE];
static DECLARE_RWSEM(htable_rw_lock);

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
	cifs_dbg(FYI, "%s: %s\n", __func__, ce->path);
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
				   str_yes_no(DFS_INTERLINK(ce->hdr_flags)),
				   ce->path_consumed, str_yes_no(cache_entry_expired(ce)));

			list_for_each_entry(t, &ce->tlist, list) {
				seq_printf(m, "  %s%s\n",
					   t->name,
					   READ_ONCE(ce->tgthint) == t ? " (target hint)" : "");
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
			 READ_ONCE(ce->tgthint) == t ? " (target hint)" : "");
	}
}

static inline void dump_ce(const struct cache_entry *ce)
{
	cifs_dbg(FYI, "cache entry: path=%s,type=%s,ttl=%d,etime=%ld,hdr_flags=0x%x,ref_flags=0x%x,interlink=%s,path_consumed=%d,expired=%s\n",
		 ce->path,
		 ce->srvtype == DFS_TYPE_ROOT ? "root" : "link", ce->ttl,
		 ce->etime.tv_nsec,
		 ce->hdr_flags, ce->ref_flags,
		 str_yes_no(DFS_INTERLINK(ce->hdr_flags)),
		 ce->path_consumed,
		 str_yes_no(cache_entry_expired(ce)));
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

	dfscache_wq = alloc_workqueue("cifs-dfscache",
				      WQ_UNBOUND|WQ_FREEZABLE|WQ_MEM_RECLAIM,
				      0);
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
	atomic_set(&dfs_cache_ttl, CACHE_DEFAULT_TTL);
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
	struct cache_dfs_tgt *t = READ_ONCE(ce->tgthint);

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
	struct cache_dfs_tgt *target;
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

	target = list_first_entry_or_null(&ce->tlist, struct cache_dfs_tgt,
					  list);
	WRITE_ONCE(ce->tgthint, target);

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

/* Remove all referrals that have a single target or oldest entry */
static void purge_cache(void)
{
	int i;
	struct cache_entry *ce;
	struct cache_entry *oldest = NULL;

	for (i = 0; i < CACHE_HTABLE_SIZE; i++) {
		struct hlist_head *l = &cache_htable[i];
		struct hlist_node *n;

		hlist_for_each_entry_safe(ce, n, l, hlist) {
			if (hlist_unhashed(&ce->hlist))
				continue;
			if (ce->numtgts == 1)
				flush_cache_ent(ce);
			else if (!oldest ||
				 timespec64_compare(&ce->etime,
						    &oldest->etime) < 0)
				oldest = ce;
		}
	}

	if (atomic_read(&cache_count) >= CACHE_MAX_ENTRIES && oldest)
		flush_cache_ent(oldest);
}

/* Add a new DFS cache entry */
static struct cache_entry *add_cache_entry_locked(struct dfs_info3_param *refs,
						  int numrefs)
{
	int rc;
	struct cache_entry *ce;
	unsigned int hash;
	int ttl;

	WARN_ON(!rwsem_is_locked(&htable_rw_lock));

	if (atomic_read(&cache_count) >= CACHE_MAX_ENTRIES) {
		cifs_dbg(FYI, "%s: reached max cache size (%d)\n", __func__, CACHE_MAX_ENTRIES);
		purge_cache();
	}

	rc = cache_entry_hash(refs[0].path_name, strlen(refs[0].path_name), &hash);
	if (rc)
		return ERR_PTR(rc);

	ce = alloc_cache_entry(refs, numrefs);
	if (IS_ERR(ce))
		return ce;

	ttl = min_t(int, atomic_read(&dfs_cache_ttl), ce->ttl);
	atomic_set(&dfs_cache_ttl, ttl);

	hlist_add_head(&ce->hlist, &cache_htable[hash]);
	dump_ce(ce);

	atomic_inc(&cache_count);

	return ce;
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
	return ERR_PTR(-ENOENT);
}

/*
 * Find a DFS cache entry in hash table and optionally check prefix path against normalized @path.
 *
 * Use whole path components in the match.  Must be called with htable_rw_lock held.
 *
 * Return cached entry if successful.
 * Return ERR_PTR(-ENOENT) if the entry is not found.
 * Return error ptr otherwise.
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
	return ERR_PTR(-ENOENT);
}

/**
 * dfs_cache_destroy - destroy DFS referral cache
 */
void dfs_cache_destroy(void)
{
	unload_nls(cache_cp);
	flush_cache_ents();
	kmem_cache_destroy(cache_slab);
	destroy_workqueue(dfscache_wq);

	cifs_dbg(FYI, "%s: destroyed DFS referral cache\n", __func__);
}

/* Update a cache entry with the new referral in @refs */
static int update_cache_entry_locked(struct cache_entry *ce, const struct dfs_info3_param *refs,
				     int numrefs)
{
	struct cache_dfs_tgt *target;
	char *th = NULL;
	int rc;

	WARN_ON(!rwsem_is_locked(&htable_rw_lock));

	target = READ_ONCE(ce->tgthint);
	if (target) {
		th = kstrdup(target->name, GFP_ATOMIC);
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

	*refs = NULL;
	*numrefs = 0;

	if (!ses || !ses->server || !ses->server->ops->get_dfs_refer)
		return -EOPNOTSUPP;
	if (unlikely(!cache_cp))
		return -EINVAL;

	cifs_dbg(FYI, "%s: ipc=%s referral=%s\n", __func__, ses->tcon_ipc->tree_name, path);
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
 *
 * On success, return entry with acquired lock for reading, otherwise error ptr.
 */
static struct cache_entry *cache_refresh_path(const unsigned int xid,
					      struct cifs_ses *ses,
					      const char *path,
					      bool force_refresh)
{
	struct dfs_info3_param *refs = NULL;
	struct cache_entry *ce;
	int numrefs = 0;
	int rc;

	cifs_dbg(FYI, "%s: search path: %s\n", __func__, path);

	down_read(&htable_rw_lock);

	ce = lookup_cache_entry(path);
	if (!IS_ERR(ce)) {
		if (!force_refresh && !cache_entry_expired(ce))
			return ce;
	} else if (PTR_ERR(ce) != -ENOENT) {
		up_read(&htable_rw_lock);
		return ce;
	}

	/*
	 * Unlock shared access as we don't want to hold any locks while getting
	 * a new referral.  The @ses used for performing the I/O could be
	 * reconnecting and it acquires @htable_rw_lock to look up the dfs cache
	 * in order to failover -- if necessary.
	 */
	up_read(&htable_rw_lock);

	/*
	 * Either the entry was not found, or it is expired, or it is a forced
	 * refresh.
	 * Request a new DFS referral in order to create or update a cache entry.
	 */
	rc = get_dfs_referral(xid, ses, path, &refs, &numrefs);
	if (rc) {
		ce = ERR_PTR(rc);
		goto out;
	}

	dump_refs(refs, numrefs);

	down_write(&htable_rw_lock);
	/* Re-check as another task might have it added or refreshed already */
	ce = lookup_cache_entry(path);
	if (!IS_ERR(ce)) {
		if (force_refresh || cache_entry_expired(ce)) {
			rc = update_cache_entry_locked(ce, refs, numrefs);
			if (rc)
				ce = ERR_PTR(rc);
		}
	} else if (PTR_ERR(ce) == -ENOENT) {
		ce = add_cache_entry_locked(refs, numrefs);
	}

	if (IS_ERR(ce)) {
		up_write(&htable_rw_lock);
		goto out;
	}

	downgrade_write(&htable_rw_lock);
out:
	free_dfs_info_array(refs, numrefs);
	return ce;
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

		if (READ_ONCE(ce->tgthint) == t)
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

	ce = cache_refresh_path(xid, ses, npath, false);
	if (IS_ERR(ce)) {
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
void dfs_cache_noreq_update_tgthint(const char *path, const struct dfs_cache_tgt_iterator *it)
{
	struct cache_dfs_tgt *t;
	struct cache_entry *ce;

	if (!path || !it)
		return;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, path);

	down_read(&htable_rw_lock);

	ce = lookup_cache_entry(path);
	if (IS_ERR(ce))
		goto out_unlock;

	t = READ_ONCE(ce->tgthint);

	if (unlikely(!strcasecmp(it->it_name, t->name)))
		goto out_unlock;

	list_for_each_entry(t, &ce->tlist, list) {
		if (!strcasecmp(t->name, it->it_name)) {
			WRITE_ONCE(ce->tgthint, t);
			cifs_dbg(FYI, "%s: new target hint: %s\n", __func__,
				 it->it_name);
			break;
		}
	}

out_unlock:
	up_read(&htable_rw_lock);
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

/* Extract share from DFS target and return a pointer to prefix path or NULL */
static const char *parse_target_share(const char *target, char **share)
{
	const char *s, *seps = "/\\";
	size_t len;

	s = strpbrk(target + 1, seps);
	if (!s)
		return ERR_PTR(-EINVAL);

	len = strcspn(s + 1, seps);
	if (!len)
		return ERR_PTR(-EINVAL);
	s += len;

	len = s - target + 1;
	*share = kstrndup(target, len, GFP_KERNEL);
	if (!*share)
		return ERR_PTR(-ENOMEM);

	s = target + len;
	return s + strspn(s, seps);
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
	char sep;
	char *target_share;
	char *ppath = NULL;
	const char *target_ppath, *dfsref_ppath;
	size_t target_pplen, dfsref_pplen;
	size_t len, c;

	if (!it || !path || !share || !prefix || strlen(path) < it->it_path_consumed)
		return -EINVAL;

	sep = it->it_name[0];
	if (sep != '\\' && sep != '/')
		return -EINVAL;

	target_ppath = parse_target_share(it->it_name, &target_share);
	if (IS_ERR(target_ppath))
		return PTR_ERR(target_ppath);

	/* point to prefix in DFS referral path */
	dfsref_ppath = path + it->it_path_consumed;
	dfsref_ppath += strspn(dfsref_ppath, "/\\");

	target_pplen = strlen(target_ppath);
	dfsref_pplen = strlen(dfsref_ppath);

	/* merge prefix paths from DFS referral path and target node */
	if (target_pplen || dfsref_pplen) {
		len = target_pplen + dfsref_pplen + 2;
		ppath = kzalloc(len, GFP_KERNEL);
		if (!ppath) {
			kfree(target_share);
			return -ENOMEM;
		}
		c = strscpy(ppath, target_ppath, len);
		if (c && dfsref_pplen)
			ppath[c] = sep;
		strlcat(ppath, dfsref_ppath, len);
	}
	*share = target_share;
	*prefix = ppath;
	return 0;
}

static bool target_share_equal(struct cifs_tcon *tcon, const char *s1)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	const char *s2 = &tcon->tree_name[1];
	struct sockaddr_storage ss;
	bool match;
	int rc;

	if (strcasecmp(s2, s1))
		return false;

	/*
	 * Resolve share's hostname and check if server address matches.  Otherwise just ignore it
	 * as we could not have upcall to resolve hostname or failed to convert ip address.
	 */
	rc = dns_resolve_unc(server->dns_dom, s1, (struct sockaddr *)&ss);
	if (rc < 0)
		return true;

	cifs_server_lock(server);
	match = cifs_match_ipaddr((struct sockaddr *)&server->dstaddr, (struct sockaddr *)&ss);
	cifs_dbg(FYI, "%s: [share=%s] ipaddr matched: %s\n", __func__, s1, str_yes_no(match));
	cifs_server_unlock(server);

	return match;
}

static bool is_ses_good(struct cifs_tcon *tcon, struct cifs_ses *ses)
{
	struct TCP_Server_Info *server = ses->server;
	struct cifs_tcon *ipc = NULL;
	bool ret;

	spin_lock(&cifs_tcp_ses_lock);
	spin_lock(&ses->ses_lock);
	spin_lock(&ses->chan_lock);

	ret = !cifs_chan_needs_reconnect(ses, server) &&
		ses->ses_status == SES_GOOD;

	spin_unlock(&ses->chan_lock);

	if (!ret)
		goto out;

	if (likely(ses->tcon_ipc)) {
		if (ses->tcon_ipc->need_reconnect) {
			ret = false;
			goto out;
		}
	} else {
		spin_unlock(&ses->ses_lock);
		spin_unlock(&cifs_tcp_ses_lock);

		ipc = cifs_setup_ipc(ses, tcon->seal);

		spin_lock(&cifs_tcp_ses_lock);
		spin_lock(&ses->ses_lock);
		if (!IS_ERR(ipc)) {
			if (!ses->tcon_ipc) {
				ses->tcon_ipc = ipc;
				ipc = NULL;
			}
		} else {
			ret = false;
			ipc = NULL;
		}
	}

out:
	spin_unlock(&ses->ses_lock);
	spin_unlock(&cifs_tcp_ses_lock);
	if (ipc && server->ops->tree_disconnect) {
		unsigned int xid = get_xid();

		(void)server->ops->tree_disconnect(xid, ipc);
		_free_xid(xid);
	}
	tconInfoFree(ipc, netfs_trace_tcon_ref_free_ipc);
	return ret;
}

/* Refresh dfs referral of @ses */
static void refresh_ses_referral(struct cifs_tcon *tcon, struct cifs_ses *ses)
{
	struct cache_entry *ce;
	unsigned int xid;
	const char *path;
	int rc = 0;

	xid = get_xid();

	path = dfs_ses_refpath(ses);
	if (IS_ERR(path)) {
		rc = PTR_ERR(path);
		goto out;
	}

	ses = CIFS_DFS_ROOT_SES(ses);
	if (!is_ses_good(tcon, ses)) {
		cifs_dbg(FYI, "%s: skip cache refresh due to disconnected ipc\n",
			 __func__);
		goto out;
	}

	ce = cache_refresh_path(xid, ses, path, false);
	if (!IS_ERR(ce))
		up_read(&htable_rw_lock);
	else
		rc = PTR_ERR(ce);

out:
	free_xid(xid);
}

static int __refresh_tcon_referral(struct cifs_tcon *tcon,
				   const char *path,
				   struct dfs_info3_param *refs,
				   int numrefs, bool force_refresh)
{
	struct cache_entry *ce;
	bool reconnect = force_refresh;
	int rc = 0;
	int i;

	if (unlikely(!numrefs))
		return 0;

	if (force_refresh) {
		for (i = 0; i < numrefs; i++) {
			/* TODO: include prefix paths in the matching */
			if (target_share_equal(tcon, refs[i].node_name)) {
				reconnect = false;
				break;
			}
		}
	}

	down_write(&htable_rw_lock);
	ce = lookup_cache_entry(path);
	if (!IS_ERR(ce)) {
		if (force_refresh || cache_entry_expired(ce))
			rc = update_cache_entry_locked(ce, refs, numrefs);
	} else if (PTR_ERR(ce) == -ENOENT) {
		ce = add_cache_entry_locked(refs, numrefs);
	}
	up_write(&htable_rw_lock);

	if (IS_ERR(ce))
		rc = PTR_ERR(ce);
	if (reconnect) {
		cifs_tcon_dbg(FYI, "%s: mark for reconnect\n", __func__);
		cifs_signal_cifsd_for_reconnect(tcon->ses->server, true);
	}
	return rc;
}

static void refresh_tcon_referral(struct cifs_tcon *tcon, bool force_refresh)
{
	struct dfs_info3_param *refs = NULL;
	struct cache_entry *ce;
	struct cifs_ses *ses;
	bool needs_refresh;
	const char *path;
	unsigned int xid;
	int numrefs = 0;
	int rc = 0;

	xid = get_xid();
	ses = tcon->ses;

	path = dfs_ses_refpath(ses);
	if (IS_ERR(path)) {
		rc = PTR_ERR(path);
		goto out;
	}

	down_read(&htable_rw_lock);
	ce = lookup_cache_entry(path);
	needs_refresh = force_refresh || IS_ERR(ce) || cache_entry_expired(ce);
	if (!needs_refresh) {
		up_read(&htable_rw_lock);
		goto out;
	}
	up_read(&htable_rw_lock);

	ses = CIFS_DFS_ROOT_SES(ses);
	if (!is_ses_good(tcon, ses)) {
		cifs_dbg(FYI, "%s: skip cache refresh due to disconnected ipc\n",
			 __func__);
		goto out;
	}

	rc = get_dfs_referral(xid, ses, path, &refs, &numrefs);
	if (!rc) {
		rc = __refresh_tcon_referral(tcon, path, refs,
					     numrefs, force_refresh);
	}

out:
	free_xid(xid);
	free_dfs_info_array(refs, numrefs);
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

	if (!cifs_sb || !cifs_sb->master_tlink)
		return -EINVAL;

	tcon = cifs_sb_master_tcon(cifs_sb);

	spin_lock(&tcon->tc_lock);
	if (!tcon->origin_fullpath) {
		spin_unlock(&tcon->tc_lock);
		cifs_dbg(FYI, "%s: not a dfs mount\n", __func__);
		return 0;
	}
	spin_unlock(&tcon->tc_lock);

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

	refresh_tcon_referral(tcon, true);
	return 0;
}

/* Refresh all DFS referrals related to DFS tcon */
void dfs_cache_refresh(struct work_struct *work)
{
	struct cifs_tcon *tcon;
	struct cifs_ses *ses;

	tcon = container_of(work, struct cifs_tcon, dfs_cache_work.work);

	list_for_each_entry(ses, &tcon->dfs_ses_list, dlist)
		refresh_ses_referral(tcon, ses);
	refresh_tcon_referral(tcon, false);

	queue_delayed_work(dfscache_wq, &tcon->dfs_cache_work,
			   atomic_read(&dfs_cache_ttl) * HZ);
}
