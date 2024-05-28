// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common NFS I/O  operations for the pnfs file based
 * layout drivers.
 *
 * Copyright (c) 2014, Primary Data, Inc. All rights reserved.
 *
 * Tom Haynes <loghyr@primarydata.com>
 */

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/sunrpc/addr.h>
#include <linux/module.h>

#include "nfs4session.h"
#include "internal.h"
#include "pnfs.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS

void pnfs_generic_rw_release(void *data)
{
	struct nfs_pgio_header *hdr = data;

	nfs_put_client(hdr->ds_clp);
	hdr->mds_ops->rpc_release(data);
}
EXPORT_SYMBOL_GPL(pnfs_generic_rw_release);

/* Fake up some data that will cause nfs_commit_release to retry the writes. */
void pnfs_generic_prepare_to_resend_writes(struct nfs_commit_data *data)
{
	struct nfs_writeverf *verf = data->res.verf;

	data->task.tk_status = 0;
	memset(&verf->verifier, 0, sizeof(verf->verifier));
	verf->committed = NFS_UNSTABLE;
}
EXPORT_SYMBOL_GPL(pnfs_generic_prepare_to_resend_writes);

void pnfs_generic_write_commit_done(struct rpc_task *task, void *data)
{
	struct nfs_commit_data *wdata = data;

	/* Note this may cause RPC to be resent */
	wdata->mds_ops->rpc_call_done(task, data);
}
EXPORT_SYMBOL_GPL(pnfs_generic_write_commit_done);

void pnfs_generic_commit_release(void *calldata)
{
	struct nfs_commit_data *data = calldata;

	data->completion_ops->completion(data);
	pnfs_put_lseg(data->lseg);
	nfs_put_client(data->ds_clp);
	nfs_commitdata_release(data);
}
EXPORT_SYMBOL_GPL(pnfs_generic_commit_release);

static struct pnfs_layout_segment *
pnfs_free_bucket_lseg(struct pnfs_commit_bucket *bucket)
{
	if (list_empty(&bucket->committing) && list_empty(&bucket->written)) {
		struct pnfs_layout_segment *freeme = bucket->lseg;
		bucket->lseg = NULL;
		return freeme;
	}
	return NULL;
}

/* The generic layer is about to remove the req from the commit list.
 * If this will make the bucket empty, it will need to put the lseg reference.
 * Note this must be called holding nfsi->commit_mutex
 */
void
pnfs_generic_clear_request_commit(struct nfs_page *req,
				  struct nfs_commit_info *cinfo)
{
	struct pnfs_commit_bucket *bucket = NULL;

	if (!test_and_clear_bit(PG_COMMIT_TO_DS, &req->wb_flags))
		goto out;
	cinfo->ds->nwritten--;
	if (list_is_singular(&req->wb_list))
		bucket = list_first_entry(&req->wb_list,
					  struct pnfs_commit_bucket, written);
out:
	nfs_request_remove_commit_list(req, cinfo);
	if (bucket)
		pnfs_put_lseg(pnfs_free_bucket_lseg(bucket));
}
EXPORT_SYMBOL_GPL(pnfs_generic_clear_request_commit);

struct pnfs_commit_array *
pnfs_alloc_commit_array(size_t n, gfp_t gfp_flags)
{
	struct pnfs_commit_array *p;
	struct pnfs_commit_bucket *b;

	p = kmalloc(struct_size(p, buckets, n), gfp_flags);
	if (!p)
		return NULL;
	p->nbuckets = n;
	INIT_LIST_HEAD(&p->cinfo_list);
	INIT_LIST_HEAD(&p->lseg_list);
	p->lseg = NULL;
	for (b = &p->buckets[0]; n != 0; b++, n--) {
		INIT_LIST_HEAD(&b->written);
		INIT_LIST_HEAD(&b->committing);
		b->lseg = NULL;
		b->direct_verf.committed = NFS_INVALID_STABLE_HOW;
	}
	return p;
}
EXPORT_SYMBOL_GPL(pnfs_alloc_commit_array);

void
pnfs_free_commit_array(struct pnfs_commit_array *p)
{
	kfree_rcu(p, rcu);
}
EXPORT_SYMBOL_GPL(pnfs_free_commit_array);

static struct pnfs_commit_array *
pnfs_find_commit_array_by_lseg(struct pnfs_ds_commit_info *fl_cinfo,
		struct pnfs_layout_segment *lseg)
{
	struct pnfs_commit_array *array;

	list_for_each_entry_rcu(array, &fl_cinfo->commits, cinfo_list) {
		if (array->lseg == lseg)
			return array;
	}
	return NULL;
}

struct pnfs_commit_array *
pnfs_add_commit_array(struct pnfs_ds_commit_info *fl_cinfo,
		struct pnfs_commit_array *new,
		struct pnfs_layout_segment *lseg)
{
	struct pnfs_commit_array *array;

	array = pnfs_find_commit_array_by_lseg(fl_cinfo, lseg);
	if (array)
		return array;
	new->lseg = lseg;
	refcount_set(&new->refcount, 1);
	list_add_rcu(&new->cinfo_list, &fl_cinfo->commits);
	list_add(&new->lseg_list, &lseg->pls_commits);
	return new;
}
EXPORT_SYMBOL_GPL(pnfs_add_commit_array);

static struct pnfs_commit_array *
pnfs_lookup_commit_array(struct pnfs_ds_commit_info *fl_cinfo,
		struct pnfs_layout_segment *lseg)
{
	struct pnfs_commit_array *array;

	rcu_read_lock();
	array = pnfs_find_commit_array_by_lseg(fl_cinfo, lseg);
	if (!array) {
		rcu_read_unlock();
		fl_cinfo->ops->setup_ds_info(fl_cinfo, lseg);
		rcu_read_lock();
		array = pnfs_find_commit_array_by_lseg(fl_cinfo, lseg);
	}
	rcu_read_unlock();
	return array;
}

static void
pnfs_release_commit_array_locked(struct pnfs_commit_array *array)
{
	list_del_rcu(&array->cinfo_list);
	list_del(&array->lseg_list);
	pnfs_free_commit_array(array);
}

static void
pnfs_put_commit_array_locked(struct pnfs_commit_array *array)
{
	if (refcount_dec_and_test(&array->refcount))
		pnfs_release_commit_array_locked(array);
}

static void
pnfs_put_commit_array(struct pnfs_commit_array *array, struct inode *inode)
{
	if (refcount_dec_and_lock(&array->refcount, &inode->i_lock)) {
		pnfs_release_commit_array_locked(array);
		spin_unlock(&inode->i_lock);
	}
}

static struct pnfs_commit_array *
pnfs_get_commit_array(struct pnfs_commit_array *array)
{
	if (refcount_inc_not_zero(&array->refcount))
		return array;
	return NULL;
}

static void
pnfs_remove_and_free_commit_array(struct pnfs_commit_array *array)
{
	array->lseg = NULL;
	list_del_init(&array->lseg_list);
	pnfs_put_commit_array_locked(array);
}

void
pnfs_generic_ds_cinfo_release_lseg(struct pnfs_ds_commit_info *fl_cinfo,
		struct pnfs_layout_segment *lseg)
{
	struct pnfs_commit_array *array, *tmp;

	list_for_each_entry_safe(array, tmp, &lseg->pls_commits, lseg_list)
		pnfs_remove_and_free_commit_array(array);
}
EXPORT_SYMBOL_GPL(pnfs_generic_ds_cinfo_release_lseg);

void
pnfs_generic_ds_cinfo_destroy(struct pnfs_ds_commit_info *fl_cinfo)
{
	struct pnfs_commit_array *array, *tmp;

	list_for_each_entry_safe(array, tmp, &fl_cinfo->commits, cinfo_list)
		pnfs_remove_and_free_commit_array(array);
}
EXPORT_SYMBOL_GPL(pnfs_generic_ds_cinfo_destroy);

/*
 * Locks the nfs_page requests for commit and moves them to
 * @bucket->committing.
 */
static int
pnfs_bucket_scan_ds_commit_list(struct pnfs_commit_bucket *bucket,
				struct nfs_commit_info *cinfo,
				int max)
{
	struct list_head *src = &bucket->written;
	struct list_head *dst = &bucket->committing;
	int ret;

	lockdep_assert_held(&NFS_I(cinfo->inode)->commit_mutex);
	ret = nfs_scan_commit_list(src, dst, cinfo, max);
	if (ret) {
		cinfo->ds->nwritten -= ret;
		cinfo->ds->ncommitting += ret;
	}
	return ret;
}

static int pnfs_bucket_scan_array(struct nfs_commit_info *cinfo,
				  struct pnfs_commit_bucket *buckets,
				  unsigned int nbuckets,
				  int max)
{
	unsigned int i;
	int rv = 0, cnt;

	for (i = 0; i < nbuckets && max != 0; i++) {
		cnt = pnfs_bucket_scan_ds_commit_list(&buckets[i], cinfo, max);
		rv += cnt;
		max -= cnt;
	}
	return rv;
}

/* Move reqs from written to committing lists, returning count
 * of number moved.
 */
int pnfs_generic_scan_commit_lists(struct nfs_commit_info *cinfo, int max)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;
	struct pnfs_commit_array *array;
	int rv = 0, cnt;

	rcu_read_lock();
	list_for_each_entry_rcu(array, &fl_cinfo->commits, cinfo_list) {
		if (!array->lseg || !pnfs_get_commit_array(array))
			continue;
		rcu_read_unlock();
		cnt = pnfs_bucket_scan_array(cinfo, array->buckets,
				array->nbuckets, max);
		rcu_read_lock();
		pnfs_put_commit_array(array, cinfo->inode);
		rv += cnt;
		max -= cnt;
		if (!max)
			break;
	}
	rcu_read_unlock();
	return rv;
}
EXPORT_SYMBOL_GPL(pnfs_generic_scan_commit_lists);

static unsigned int
pnfs_bucket_recover_commit_reqs(struct list_head *dst,
			        struct pnfs_commit_bucket *buckets,
				unsigned int nbuckets,
				struct nfs_commit_info *cinfo)
{
	struct pnfs_commit_bucket *b;
	struct pnfs_layout_segment *freeme;
	unsigned int nwritten, ret = 0;
	unsigned int i;

restart:
	for (i = 0, b = buckets; i < nbuckets; i++, b++) {
		nwritten = nfs_scan_commit_list(&b->written, dst, cinfo, 0);
		if (!nwritten)
			continue;
		ret += nwritten;
		freeme = pnfs_free_bucket_lseg(b);
		if (freeme) {
			pnfs_put_lseg(freeme);
			goto restart;
		}
	}
	return ret;
}

/* Pull everything off the committing lists and dump into @dst.  */
void pnfs_generic_recover_commit_reqs(struct list_head *dst,
				      struct nfs_commit_info *cinfo)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;
	struct pnfs_commit_array *array;
	unsigned int nwritten;

	lockdep_assert_held(&NFS_I(cinfo->inode)->commit_mutex);
	rcu_read_lock();
	list_for_each_entry_rcu(array, &fl_cinfo->commits, cinfo_list) {
		if (!array->lseg || !pnfs_get_commit_array(array))
			continue;
		rcu_read_unlock();
		nwritten = pnfs_bucket_recover_commit_reqs(dst,
							   array->buckets,
							   array->nbuckets,
							   cinfo);
		rcu_read_lock();
		pnfs_put_commit_array(array, cinfo->inode);
		fl_cinfo->nwritten -= nwritten;
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(pnfs_generic_recover_commit_reqs);

static struct nfs_page *
pnfs_bucket_search_commit_reqs(struct pnfs_commit_bucket *buckets,
			       unsigned int nbuckets, struct folio *folio)
{
	struct nfs_page *req;
	struct pnfs_commit_bucket *b;
	unsigned int i;

	/* Linearly search the commit lists for each bucket until a matching
	 * request is found */
	for (i = 0, b = buckets; i < nbuckets; i++, b++) {
		list_for_each_entry(req, &b->written, wb_list) {
			if (nfs_page_to_folio(req) == folio)
				return req->wb_head;
		}
		list_for_each_entry(req, &b->committing, wb_list) {
			if (nfs_page_to_folio(req) == folio)
				return req->wb_head;
		}
	}
	return NULL;
}

/* pnfs_generic_search_commit_reqs - Search lists in @cinfo for the head request
 *				   for @folio
 * @cinfo - commit info for current inode
 * @folio - page to search for matching head request
 *
 * Return: the head request if one is found, otherwise %NULL.
 */
struct nfs_page *pnfs_generic_search_commit_reqs(struct nfs_commit_info *cinfo,
						 struct folio *folio)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;
	struct pnfs_commit_array *array;
	struct nfs_page *req;

	list_for_each_entry(array, &fl_cinfo->commits, cinfo_list) {
		req = pnfs_bucket_search_commit_reqs(array->buckets,
						     array->nbuckets, folio);
		if (req)
			return req;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(pnfs_generic_search_commit_reqs);

static struct pnfs_layout_segment *
pnfs_bucket_get_committing(struct list_head *head,
			   struct pnfs_commit_bucket *bucket,
			   struct nfs_commit_info *cinfo)
{
	struct pnfs_layout_segment *lseg;
	struct list_head *pos;

	list_for_each(pos, &bucket->committing)
		cinfo->ds->ncommitting--;
	list_splice_init(&bucket->committing, head);
	lseg = pnfs_free_bucket_lseg(bucket);
	if (!lseg)
		lseg = pnfs_get_lseg(bucket->lseg);
	return lseg;
}

static struct nfs_commit_data *
pnfs_bucket_fetch_commitdata(struct pnfs_commit_bucket *bucket,
			     struct nfs_commit_info *cinfo)
{
	struct nfs_commit_data *data = nfs_commitdata_alloc();

	if (!data)
		return NULL;
	data->lseg = pnfs_bucket_get_committing(&data->pages, bucket, cinfo);
	return data;
}

static void pnfs_generic_retry_commit(struct pnfs_commit_bucket *buckets,
				      unsigned int nbuckets,
				      struct nfs_commit_info *cinfo,
				      unsigned int idx)
{
	struct pnfs_commit_bucket *bucket;
	struct pnfs_layout_segment *freeme;
	LIST_HEAD(pages);

	for (bucket = buckets; idx < nbuckets; bucket++, idx++) {
		if (list_empty(&bucket->committing))
			continue;
		mutex_lock(&NFS_I(cinfo->inode)->commit_mutex);
		freeme = pnfs_bucket_get_committing(&pages, bucket, cinfo);
		mutex_unlock(&NFS_I(cinfo->inode)->commit_mutex);
		nfs_retry_commit(&pages, freeme, cinfo, idx);
		pnfs_put_lseg(freeme);
	}
}

static unsigned int
pnfs_bucket_alloc_ds_commits(struct list_head *list,
			     struct pnfs_commit_bucket *buckets,
			     unsigned int nbuckets,
			     struct nfs_commit_info *cinfo)
{
	struct pnfs_commit_bucket *bucket;
	struct nfs_commit_data *data;
	unsigned int i;
	unsigned int nreq = 0;

	for (i = 0, bucket = buckets; i < nbuckets; i++, bucket++) {
		if (list_empty(&bucket->committing))
			continue;
		mutex_lock(&NFS_I(cinfo->inode)->commit_mutex);
		if (!list_empty(&bucket->committing)) {
			data = pnfs_bucket_fetch_commitdata(bucket, cinfo);
			if (!data)
				goto out_error;
			data->ds_commit_index = i;
			list_add_tail(&data->list, list);
			nreq++;
		}
		mutex_unlock(&NFS_I(cinfo->inode)->commit_mutex);
	}
	return nreq;
out_error:
	mutex_unlock(&NFS_I(cinfo->inode)->commit_mutex);
	/* Clean up on error */
	pnfs_generic_retry_commit(buckets, nbuckets, cinfo, i);
	return nreq;
}

static unsigned int
pnfs_alloc_ds_commits_list(struct list_head *list,
			   struct pnfs_ds_commit_info *fl_cinfo,
			   struct nfs_commit_info *cinfo)
{
	struct pnfs_commit_array *array;
	unsigned int ret = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(array, &fl_cinfo->commits, cinfo_list) {
		if (!array->lseg || !pnfs_get_commit_array(array))
			continue;
		rcu_read_unlock();
		ret += pnfs_bucket_alloc_ds_commits(list, array->buckets,
				array->nbuckets, cinfo);
		rcu_read_lock();
		pnfs_put_commit_array(array, cinfo->inode);
	}
	rcu_read_unlock();
	return ret;
}

/* This follows nfs_commit_list pretty closely */
int
pnfs_generic_commit_pagelist(struct inode *inode, struct list_head *mds_pages,
			     int how, struct nfs_commit_info *cinfo,
			     int (*initiate_commit)(struct nfs_commit_data *data,
						    int how))
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;
	struct nfs_commit_data *data, *tmp;
	LIST_HEAD(list);
	unsigned int nreq = 0;

	if (!list_empty(mds_pages)) {
		data = nfs_commitdata_alloc();
		if (!data) {
			nfs_retry_commit(mds_pages, NULL, cinfo, -1);
			return -ENOMEM;
		}
		data->ds_commit_index = -1;
		list_splice_init(mds_pages, &data->pages);
		list_add_tail(&data->list, &list);
		nreq++;
	}

	nreq += pnfs_alloc_ds_commits_list(&list, fl_cinfo, cinfo);
	if (nreq == 0)
		goto out;

	list_for_each_entry_safe(data, tmp, &list, list) {
		list_del(&data->list);
		if (data->ds_commit_index < 0) {
			nfs_init_commit(data, NULL, NULL, cinfo);
			nfs_initiate_commit(NFS_CLIENT(inode), data,
					    NFS_PROTO(data->inode),
					    data->mds_ops, how,
					    RPC_TASK_CRED_NOREF);
		} else {
			nfs_init_commit(data, NULL, data->lseg, cinfo);
			initiate_commit(data, how);
		}
	}
out:
	return PNFS_ATTEMPTED;
}
EXPORT_SYMBOL_GPL(pnfs_generic_commit_pagelist);

/*
 * Data server cache
 *
 * Data servers can be mapped to different device ids.
 * nfs4_pnfs_ds reference counting
 *   - set to 1 on allocation
 *   - incremented when a device id maps a data server already in the cache.
 *   - decremented when deviceid is removed from the cache.
 */
static DEFINE_SPINLOCK(nfs4_ds_cache_lock);
static LIST_HEAD(nfs4_data_server_cache);

/* Debug routines */
static void
print_ds(struct nfs4_pnfs_ds *ds)
{
	if (ds == NULL) {
		printk(KERN_WARNING "%s NULL device\n", __func__);
		return;
	}
	printk(KERN_WARNING "        ds %s\n"
		"        ref count %d\n"
		"        client %p\n"
		"        cl_exchange_flags %x\n",
		ds->ds_remotestr,
		refcount_read(&ds->ds_count), ds->ds_clp,
		ds->ds_clp ? ds->ds_clp->cl_exchange_flags : 0);
}

static bool
same_sockaddr(struct sockaddr *addr1, struct sockaddr *addr2)
{
	struct sockaddr_in *a, *b;
	struct sockaddr_in6 *a6, *b6;

	if (addr1->sa_family != addr2->sa_family)
		return false;

	switch (addr1->sa_family) {
	case AF_INET:
		a = (struct sockaddr_in *)addr1;
		b = (struct sockaddr_in *)addr2;

		if (a->sin_addr.s_addr == b->sin_addr.s_addr &&
		    a->sin_port == b->sin_port)
			return true;
		break;

	case AF_INET6:
		a6 = (struct sockaddr_in6 *)addr1;
		b6 = (struct sockaddr_in6 *)addr2;

		/* LINKLOCAL addresses must have matching scope_id */
		if (ipv6_addr_src_scope(&a6->sin6_addr) ==
		    IPV6_ADDR_SCOPE_LINKLOCAL &&
		    a6->sin6_scope_id != b6->sin6_scope_id)
			return false;

		if (ipv6_addr_equal(&a6->sin6_addr, &b6->sin6_addr) &&
		    a6->sin6_port == b6->sin6_port)
			return true;
		break;

	default:
		dprintk("%s: unhandled address family: %u\n",
			__func__, addr1->sa_family);
		return false;
	}

	return false;
}

/*
 * Checks if 'dsaddrs1' contains a subset of 'dsaddrs2'. If it does,
 * declare a match.
 */
static bool
_same_data_server_addrs_locked(const struct list_head *dsaddrs1,
			       const struct list_head *dsaddrs2)
{
	struct nfs4_pnfs_ds_addr *da1, *da2;
	struct sockaddr *sa1, *sa2;
	bool match = false;

	list_for_each_entry(da1, dsaddrs1, da_node) {
		sa1 = (struct sockaddr *)&da1->da_addr;
		match = false;
		list_for_each_entry(da2, dsaddrs2, da_node) {
			sa2 = (struct sockaddr *)&da2->da_addr;
			match = same_sockaddr(sa1, sa2);
			if (match)
				break;
		}
		if (!match)
			break;
	}
	return match;
}

/*
 * Lookup DS by addresses.  nfs4_ds_cache_lock is held
 */
static struct nfs4_pnfs_ds *
_data_server_lookup_locked(const struct list_head *dsaddrs)
{
	struct nfs4_pnfs_ds *ds;

	list_for_each_entry(ds, &nfs4_data_server_cache, ds_node)
		if (_same_data_server_addrs_locked(&ds->ds_addrs, dsaddrs))
			return ds;
	return NULL;
}

static struct nfs4_pnfs_ds_addr *nfs4_pnfs_ds_addr_alloc(gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds_addr *da = kzalloc(sizeof(*da), gfp_flags);
	if (da)
		INIT_LIST_HEAD(&da->da_node);
	return da;
}

static void nfs4_pnfs_ds_addr_free(struct nfs4_pnfs_ds_addr *da)
{
	kfree(da->da_remotestr);
	kfree(da->da_netid);
	kfree(da);
}

static void destroy_ds(struct nfs4_pnfs_ds *ds)
{
	struct nfs4_pnfs_ds_addr *da;

	dprintk("--> %s\n", __func__);
	ifdebug(FACILITY)
		print_ds(ds);

	nfs_put_client(ds->ds_clp);

	while (!list_empty(&ds->ds_addrs)) {
		da = list_first_entry(&ds->ds_addrs,
				      struct nfs4_pnfs_ds_addr,
				      da_node);
		list_del_init(&da->da_node);
		nfs4_pnfs_ds_addr_free(da);
	}

	kfree(ds->ds_remotestr);
	kfree(ds);
}

void nfs4_pnfs_ds_put(struct nfs4_pnfs_ds *ds)
{
	if (refcount_dec_and_lock(&ds->ds_count,
				&nfs4_ds_cache_lock)) {
		list_del_init(&ds->ds_node);
		spin_unlock(&nfs4_ds_cache_lock);
		destroy_ds(ds);
	}
}
EXPORT_SYMBOL_GPL(nfs4_pnfs_ds_put);

/*
 * Create a string with a human readable address and port to avoid
 * complicated setup around many dprinks.
 */
static char *
nfs4_pnfs_remotestr(struct list_head *dsaddrs, gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds_addr *da;
	char *remotestr;
	size_t len;
	char *p;

	len = 3;        /* '{', '}' and eol */
	list_for_each_entry(da, dsaddrs, da_node) {
		len += strlen(da->da_remotestr) + 1;    /* string plus comma */
	}

	remotestr = kzalloc(len, gfp_flags);
	if (!remotestr)
		return NULL;

	p = remotestr;
	*(p++) = '{';
	len--;
	list_for_each_entry(da, dsaddrs, da_node) {
		size_t ll = strlen(da->da_remotestr);

		if (ll > len)
			goto out_err;

		memcpy(p, da->da_remotestr, ll);
		p += ll;
		len -= ll;

		if (len < 1)
			goto out_err;
		(*p++) = ',';
		len--;
	}
	if (len < 2)
		goto out_err;
	*(p++) = '}';
	*p = '\0';
	return remotestr;
out_err:
	kfree(remotestr);
	return NULL;
}

/*
 * Given a list of multipath struct nfs4_pnfs_ds_addr, add it to ds cache if
 * uncached and return cached struct nfs4_pnfs_ds.
 */
struct nfs4_pnfs_ds *
nfs4_pnfs_ds_add(struct list_head *dsaddrs, gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds *tmp_ds, *ds = NULL;
	char *remotestr;

	if (list_empty(dsaddrs)) {
		dprintk("%s: no addresses defined\n", __func__);
		goto out;
	}

	ds = kzalloc(sizeof(*ds), gfp_flags);
	if (!ds)
		goto out;

	/* this is only used for debugging, so it's ok if its NULL */
	remotestr = nfs4_pnfs_remotestr(dsaddrs, gfp_flags);

	spin_lock(&nfs4_ds_cache_lock);
	tmp_ds = _data_server_lookup_locked(dsaddrs);
	if (tmp_ds == NULL) {
		INIT_LIST_HEAD(&ds->ds_addrs);
		list_splice_init(dsaddrs, &ds->ds_addrs);
		ds->ds_remotestr = remotestr;
		refcount_set(&ds->ds_count, 1);
		INIT_LIST_HEAD(&ds->ds_node);
		ds->ds_clp = NULL;
		list_add(&ds->ds_node, &nfs4_data_server_cache);
		dprintk("%s add new data server %s\n", __func__,
			ds->ds_remotestr);
	} else {
		kfree(remotestr);
		kfree(ds);
		refcount_inc(&tmp_ds->ds_count);
		dprintk("%s data server %s found, inc'ed ds_count to %d\n",
			__func__, tmp_ds->ds_remotestr,
			refcount_read(&tmp_ds->ds_count));
		ds = tmp_ds;
	}
	spin_unlock(&nfs4_ds_cache_lock);
out:
	return ds;
}
EXPORT_SYMBOL_GPL(nfs4_pnfs_ds_add);

static int nfs4_wait_ds_connect(struct nfs4_pnfs_ds *ds)
{
	might_sleep();
	return wait_on_bit(&ds->ds_state, NFS4DS_CONNECTING, TASK_KILLABLE);
}

static void nfs4_clear_ds_conn_bit(struct nfs4_pnfs_ds *ds)
{
	smp_mb__before_atomic();
	clear_and_wake_up_bit(NFS4DS_CONNECTING, &ds->ds_state);
}

static struct nfs_client *(*get_v3_ds_connect)(
			struct nfs_server *mds_srv,
			const struct sockaddr_storage *ds_addr,
			int ds_addrlen,
			int ds_proto,
			unsigned int ds_timeo,
			unsigned int ds_retrans);

static bool load_v3_ds_connect(void)
{
	if (!get_v3_ds_connect) {
		get_v3_ds_connect = symbol_request(nfs3_set_ds_client);
		WARN_ON_ONCE(!get_v3_ds_connect);
	}

	return(get_v3_ds_connect != NULL);
}

void nfs4_pnfs_v3_ds_connect_unload(void)
{
	if (get_v3_ds_connect) {
		symbol_put(nfs3_set_ds_client);
		get_v3_ds_connect = NULL;
	}
}

static int _nfs4_pnfs_v3_ds_connect(struct nfs_server *mds_srv,
				 struct nfs4_pnfs_ds *ds,
				 unsigned int timeo,
				 unsigned int retrans)
{
	struct nfs_client *clp = ERR_PTR(-EIO);
	struct nfs4_pnfs_ds_addr *da;
	unsigned long connect_timeout = timeo * (retrans + 1) * HZ / 10;
	int status = 0;

	dprintk("--> %s DS %s\n", __func__, ds->ds_remotestr);

	if (!load_v3_ds_connect())
		return -EPROTONOSUPPORT;

	list_for_each_entry(da, &ds->ds_addrs, da_node) {
		dprintk("%s: DS %s: trying address %s\n",
			__func__, ds->ds_remotestr, da->da_remotestr);

		if (!IS_ERR(clp)) {
			struct xprt_create xprt_args = {
				.ident = da->da_transport,
				.net = clp->cl_net,
				.dstaddr = (struct sockaddr *)&da->da_addr,
				.addrlen = da->da_addrlen,
				.servername = clp->cl_hostname,
				.connect_timeout = connect_timeout,
				.reconnect_timeout = connect_timeout,
			};

			if (da->da_transport != clp->cl_proto)
				continue;
			if (da->da_addr.ss_family != clp->cl_addr.ss_family)
				continue;
			/* Add this address as an alias */
			rpc_clnt_add_xprt(clp->cl_rpcclient, &xprt_args,
					rpc_clnt_test_and_add_xprt, NULL);
			continue;
		}
		clp = get_v3_ds_connect(mds_srv,
				&da->da_addr,
				da->da_addrlen, da->da_transport,
				timeo, retrans);
		if (IS_ERR(clp))
			continue;
		clp->cl_rpcclient->cl_softerr = 0;
		clp->cl_rpcclient->cl_softrtry = 0;
	}

	if (IS_ERR(clp)) {
		status = PTR_ERR(clp);
		goto out;
	}

	smp_wmb();
	WRITE_ONCE(ds->ds_clp, clp);
	dprintk("%s [new] addr: %s\n", __func__, ds->ds_remotestr);
out:
	return status;
}

static int _nfs4_pnfs_v4_ds_connect(struct nfs_server *mds_srv,
				 struct nfs4_pnfs_ds *ds,
				 unsigned int timeo,
				 unsigned int retrans,
				 u32 minor_version)
{
	struct nfs_client *clp = ERR_PTR(-EIO);
	struct nfs4_pnfs_ds_addr *da;
	int status = 0;

	dprintk("--> %s DS %s\n", __func__, ds->ds_remotestr);

	list_for_each_entry(da, &ds->ds_addrs, da_node) {
		char servername[48];

		dprintk("%s: DS %s: trying address %s\n",
			__func__, ds->ds_remotestr, da->da_remotestr);

		if (!IS_ERR(clp) && clp->cl_mvops->session_trunk) {
			struct xprt_create xprt_args = {
				.ident = da->da_transport,
				.net = clp->cl_net,
				.dstaddr = (struct sockaddr *)&da->da_addr,
				.addrlen = da->da_addrlen,
				.servername = clp->cl_hostname,
				.xprtsec = clp->cl_xprtsec,
			};
			struct nfs4_add_xprt_data xprtdata = {
				.clp = clp,
			};
			struct rpc_add_xprt_test rpcdata = {
				.add_xprt_test = clp->cl_mvops->session_trunk,
				.data = &xprtdata,
			};

			if (da->da_transport != clp->cl_proto &&
					clp->cl_proto != XPRT_TRANSPORT_TCP_TLS)
				continue;
			if (da->da_transport == XPRT_TRANSPORT_TCP &&
				mds_srv->nfs_client->cl_proto ==
					XPRT_TRANSPORT_TCP_TLS) {
				struct sockaddr *addr =
					(struct sockaddr *)&da->da_addr;
				struct sockaddr_in *sin =
					(struct sockaddr_in *)&da->da_addr;
				struct sockaddr_in6 *sin6 =
					(struct sockaddr_in6 *)&da->da_addr;

				/* for NFS with TLS we need to supply a correct
				 * servername of the trunked transport, not the
				 * servername of the main transport stored in
				 * clp->cl_hostname. And set the protocol to
				 * indicate to use TLS
				 */
				servername[0] = '\0';
				switch(addr->sa_family) {
				case AF_INET:
					snprintf(servername, sizeof(servername),
						"%pI4", &sin->sin_addr.s_addr);
					break;
				case AF_INET6:
					snprintf(servername, sizeof(servername),
						"%pI6", &sin6->sin6_addr);
					break;
				default:
					/* do not consider this address */
					continue;
				}
				xprt_args.ident = XPRT_TRANSPORT_TCP_TLS;
				xprt_args.servername = servername;
			}
			if (da->da_addr.ss_family != clp->cl_addr.ss_family)
				continue;

			/**
			* Test this address for session trunking and
			* add as an alias
			*/
			xprtdata.cred = nfs4_get_clid_cred(clp);
			rpc_clnt_add_xprt(clp->cl_rpcclient, &xprt_args,
					  rpc_clnt_setup_test_and_add_xprt,
					  &rpcdata);
			if (xprtdata.cred)
				put_cred(xprtdata.cred);
		} else {
			if (da->da_transport == XPRT_TRANSPORT_TCP &&
				mds_srv->nfs_client->cl_proto ==
					XPRT_TRANSPORT_TCP_TLS)
				da->da_transport = XPRT_TRANSPORT_TCP_TLS;
			clp = nfs4_set_ds_client(mds_srv,
						&da->da_addr,
						da->da_addrlen,
						da->da_transport, timeo,
						retrans, minor_version);
			if (IS_ERR(clp))
				continue;

			status = nfs4_init_ds_session(clp,
					mds_srv->nfs_client->cl_lease_time);
			if (status) {
				nfs_put_client(clp);
				clp = ERR_PTR(-EIO);
				continue;
			}

		}
	}

	if (IS_ERR(clp)) {
		status = PTR_ERR(clp);
		goto out;
	}

	smp_wmb();
	WRITE_ONCE(ds->ds_clp, clp);
	dprintk("%s [new] addr: %s\n", __func__, ds->ds_remotestr);
out:
	return status;
}

/*
 * Create an rpc connection to the nfs4_pnfs_ds data server.
 * Currently only supports IPv4 and IPv6 addresses.
 * If connection fails, make devid unavailable and return a -errno.
 */
int nfs4_pnfs_ds_connect(struct nfs_server *mds_srv, struct nfs4_pnfs_ds *ds,
			  struct nfs4_deviceid_node *devid, unsigned int timeo,
			  unsigned int retrans, u32 version, u32 minor_version)
{
	int err;

	do {
		err = nfs4_wait_ds_connect(ds);
		if (err || ds->ds_clp)
			goto out;
		if (nfs4_test_deviceid_unavailable(devid))
			return -ENODEV;
	} while (test_and_set_bit(NFS4DS_CONNECTING, &ds->ds_state) != 0);

	if (ds->ds_clp)
		goto connect_done;

	switch (version) {
	case 3:
		err = _nfs4_pnfs_v3_ds_connect(mds_srv, ds, timeo, retrans);
		break;
	case 4:
		err = _nfs4_pnfs_v4_ds_connect(mds_srv, ds, timeo, retrans,
					       minor_version);
		break;
	default:
		dprintk("%s: unsupported DS version %d\n", __func__, version);
		err = -EPROTONOSUPPORT;
	}

connect_done:
	nfs4_clear_ds_conn_bit(ds);
out:
	/*
	 * At this point the ds->ds_clp should be ready, but it might have
	 * hit an error.
	 */
	if (!err) {
		if (!ds->ds_clp || !nfs_client_init_is_complete(ds->ds_clp)) {
			WARN_ON_ONCE(ds->ds_clp ||
				!nfs4_test_deviceid_unavailable(devid));
			return -EINVAL;
		}
		err = nfs_client_init_status(ds->ds_clp);
	}

	return err;
}
EXPORT_SYMBOL_GPL(nfs4_pnfs_ds_connect);

/*
 * Currently only supports ipv4, ipv6 and one multi-path address.
 */
struct nfs4_pnfs_ds_addr *
nfs4_decode_mp_ds_addr(struct net *net, struct xdr_stream *xdr, gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds_addr *da = NULL;
	char *buf, *portstr;
	__be16 port;
	ssize_t nlen, rlen;
	int tmp[2];
	char *netid;
	size_t len;
	char *startsep = "";
	char *endsep = "";


	/* r_netid */
	nlen = xdr_stream_decode_string_dup(xdr, &netid, XDR_MAX_NETOBJ,
					    gfp_flags);
	if (unlikely(nlen < 0))
		goto out_err;

	/* r_addr: ip/ip6addr with port in dec octets - see RFC 5665 */
	/* port is ".ABC.DEF", 8 chars max */
	rlen = xdr_stream_decode_string_dup(xdr, &buf, INET6_ADDRSTRLEN +
					    IPV6_SCOPE_ID_LEN + 8, gfp_flags);
	if (unlikely(rlen < 0))
		goto out_free_netid;

	/* replace port '.' with '-' */
	portstr = strrchr(buf, '.');
	if (!portstr) {
		dprintk("%s: Failed finding expected dot in port\n",
			__func__);
		goto out_free_buf;
	}
	*portstr = '-';

	/* find '.' between address and port */
	portstr = strrchr(buf, '.');
	if (!portstr) {
		dprintk("%s: Failed finding expected dot between address and "
			"port\n", __func__);
		goto out_free_buf;
	}
	*portstr = '\0';

	da = nfs4_pnfs_ds_addr_alloc(gfp_flags);
	if (unlikely(!da))
		goto out_free_buf;

	if (!rpc_pton(net, buf, portstr-buf, (struct sockaddr *)&da->da_addr,
		      sizeof(da->da_addr))) {
		dprintk("%s: error parsing address %s\n", __func__, buf);
		goto out_free_da;
	}

	portstr++;
	sscanf(portstr, "%d-%d", &tmp[0], &tmp[1]);
	port = htons((tmp[0] << 8) | (tmp[1]));

	switch (da->da_addr.ss_family) {
	case AF_INET:
		((struct sockaddr_in *)&da->da_addr)->sin_port = port;
		da->da_addrlen = sizeof(struct sockaddr_in);
		break;

	case AF_INET6:
		((struct sockaddr_in6 *)&da->da_addr)->sin6_port = port;
		da->da_addrlen = sizeof(struct sockaddr_in6);
		startsep = "[";
		endsep = "]";
		break;

	default:
		dprintk("%s: unsupported address family: %u\n",
			__func__, da->da_addr.ss_family);
		goto out_free_da;
	}

	da->da_transport = xprt_find_transport_ident(netid);
	if (da->da_transport < 0) {
		dprintk("%s: ERROR: unknown r_netid \"%s\"\n",
			__func__, netid);
		goto out_free_da;
	}

	da->da_netid = netid;

	/* save human readable address */
	len = strlen(startsep) + strlen(buf) + strlen(endsep) + 7;
	da->da_remotestr = kzalloc(len, gfp_flags);

	/* NULL is ok, only used for dprintk */
	if (da->da_remotestr)
		snprintf(da->da_remotestr, len, "%s%s%s:%u", startsep,
			 buf, endsep, ntohs(port));

	dprintk("%s: Parsed DS addr %s\n", __func__, da->da_remotestr);
	kfree(buf);
	return da;

out_free_da:
	kfree(da);
out_free_buf:
	dprintk("%s: Error parsing DS addr: %s\n", __func__, buf);
	kfree(buf);
out_free_netid:
	kfree(netid);
out_err:
	return NULL;
}
EXPORT_SYMBOL_GPL(nfs4_decode_mp_ds_addr);

void
pnfs_layout_mark_request_commit(struct nfs_page *req,
				struct pnfs_layout_segment *lseg,
				struct nfs_commit_info *cinfo,
				u32 ds_commit_idx)
{
	struct list_head *list;
	struct pnfs_commit_array *array;
	struct pnfs_commit_bucket *bucket;

	mutex_lock(&NFS_I(cinfo->inode)->commit_mutex);
	array = pnfs_lookup_commit_array(cinfo->ds, lseg);
	if (!array || !pnfs_is_valid_lseg(lseg))
		goto out_resched;
	bucket = &array->buckets[ds_commit_idx];
	list = &bucket->written;
	/* Non-empty buckets hold a reference on the lseg.  That ref
	 * is normally transferred to the COMMIT call and released
	 * there.  It could also be released if the last req is pulled
	 * off due to a rewrite, in which case it will be done in
	 * pnfs_common_clear_request_commit
	 */
	if (!bucket->lseg)
		bucket->lseg = pnfs_get_lseg(lseg);
	set_bit(PG_COMMIT_TO_DS, &req->wb_flags);
	cinfo->ds->nwritten++;

	nfs_request_add_commit_list_locked(req, list, cinfo);
	mutex_unlock(&NFS_I(cinfo->inode)->commit_mutex);
	nfs_folio_mark_unstable(nfs_page_to_folio(req), cinfo);
	return;
out_resched:
	mutex_unlock(&NFS_I(cinfo->inode)->commit_mutex);
	cinfo->completion_ops->resched_write(cinfo, req);
}
EXPORT_SYMBOL_GPL(pnfs_layout_mark_request_commit);

int
pnfs_nfs_generic_sync(struct inode *inode, bool datasync)
{
	int ret;

	if (!pnfs_layoutcommit_outstanding(inode))
		return 0;
	ret = nfs_commit_inode(inode, FLUSH_SYNC);
	if (ret < 0)
		return ret;
	if (datasync)
		return 0;
	return pnfs_layoutcommit_inode(inode, true);
}
EXPORT_SYMBOL_GPL(pnfs_nfs_generic_sync);

