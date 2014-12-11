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

#include "internal.h"
#include "pnfs.h"

static void pnfs_generic_fenceme(struct inode *inode,
				 struct pnfs_layout_hdr *lo)
{
	if (!test_and_clear_bit(NFS_LAYOUT_RETURN, &lo->plh_flags))
		return;
	pnfs_return_layout(inode);
}

void pnfs_generic_rw_release(void *data)
{
	struct nfs_pgio_header *hdr = data;
	struct pnfs_layout_hdr *lo = hdr->lseg->pls_layout;

	pnfs_generic_fenceme(lo->plh_inode, lo);
	nfs_put_client(hdr->ds_clp);
	hdr->mds_ops->rpc_release(data);
}
EXPORT_SYMBOL_GPL(pnfs_generic_rw_release);

/* Fake up some data that will cause nfs_commit_release to retry the writes. */
void pnfs_generic_prepare_to_resend_writes(struct nfs_commit_data *data)
{
	struct nfs_page *first = nfs_list_entry(data->pages.next);

	data->task.tk_status = 0;
	memcpy(&data->verf.verifier, &first->wb_verf,
	       sizeof(data->verf.verifier));
	data->verf.verifier.data[0]++; /* ensure verifier mismatch */
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

/* The generic layer is about to remove the req from the commit list.
 * If this will make the bucket empty, it will need to put the lseg reference.
 * Note this must be called holding the inode (/cinfo) lock
 */
void
pnfs_generic_clear_request_commit(struct nfs_page *req,
				  struct nfs_commit_info *cinfo)
{
	struct pnfs_layout_segment *freeme = NULL;

	if (!test_and_clear_bit(PG_COMMIT_TO_DS, &req->wb_flags))
		goto out;
	cinfo->ds->nwritten--;
	if (list_is_singular(&req->wb_list)) {
		struct pnfs_commit_bucket *bucket;

		bucket = list_first_entry(&req->wb_list,
					  struct pnfs_commit_bucket,
					  written);
		freeme = bucket->wlseg;
		bucket->wlseg = NULL;
	}
out:
	nfs_request_remove_commit_list(req, cinfo);
	pnfs_put_lseg_locked(freeme);
}
EXPORT_SYMBOL_GPL(pnfs_generic_clear_request_commit);

static int
pnfs_generic_transfer_commit_list(struct list_head *src, struct list_head *dst,
				  struct nfs_commit_info *cinfo, int max)
{
	struct nfs_page *req, *tmp;
	int ret = 0;

	list_for_each_entry_safe(req, tmp, src, wb_list) {
		if (!nfs_lock_request(req))
			continue;
		kref_get(&req->wb_kref);
		if (cond_resched_lock(cinfo->lock))
			list_safe_reset_next(req, tmp, wb_list);
		nfs_request_remove_commit_list(req, cinfo);
		clear_bit(PG_COMMIT_TO_DS, &req->wb_flags);
		nfs_list_add_request(req, dst);
		ret++;
		if ((ret == max) && !cinfo->dreq)
			break;
	}
	return ret;
}

static int
pnfs_generic_scan_ds_commit_list(struct pnfs_commit_bucket *bucket,
				 struct nfs_commit_info *cinfo,
				 int max)
{
	struct list_head *src = &bucket->written;
	struct list_head *dst = &bucket->committing;
	int ret;

	lockdep_assert_held(cinfo->lock);
	ret = pnfs_generic_transfer_commit_list(src, dst, cinfo, max);
	if (ret) {
		cinfo->ds->nwritten -= ret;
		cinfo->ds->ncommitting += ret;
		bucket->clseg = bucket->wlseg;
		if (list_empty(src))
			bucket->wlseg = NULL;
		else
			pnfs_get_lseg(bucket->clseg);
	}
	return ret;
}

/* Move reqs from written to committing lists, returning count
 * of number moved.
 */
int pnfs_generic_scan_commit_lists(struct nfs_commit_info *cinfo,
				   int max)
{
	int i, rv = 0, cnt;

	lockdep_assert_held(cinfo->lock);
	for (i = 0; i < cinfo->ds->nbuckets && max != 0; i++) {
		cnt = pnfs_generic_scan_ds_commit_list(&cinfo->ds->buckets[i],
						       cinfo, max);
		max -= cnt;
		rv += cnt;
	}
	return rv;
}
EXPORT_SYMBOL_GPL(pnfs_generic_scan_commit_lists);

/* Pull everything off the committing lists and dump into @dst.  */
void pnfs_generic_recover_commit_reqs(struct list_head *dst,
				      struct nfs_commit_info *cinfo)
{
	struct pnfs_commit_bucket *b;
	struct pnfs_layout_segment *freeme;
	int i;

	lockdep_assert_held(cinfo->lock);
restart:
	for (i = 0, b = cinfo->ds->buckets; i < cinfo->ds->nbuckets; i++, b++) {
		if (pnfs_generic_transfer_commit_list(&b->written, dst,
						      cinfo, 0)) {
			freeme = b->wlseg;
			b->wlseg = NULL;
			spin_unlock(cinfo->lock);
			pnfs_put_lseg(freeme);
			spin_lock(cinfo->lock);
			goto restart;
		}
	}
	cinfo->ds->nwritten = 0;
}
EXPORT_SYMBOL_GPL(pnfs_generic_recover_commit_reqs);

static void pnfs_generic_retry_commit(struct nfs_commit_info *cinfo, int idx)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;
	struct pnfs_commit_bucket *bucket;
	struct pnfs_layout_segment *freeme;
	int i;

	for (i = idx; i < fl_cinfo->nbuckets; i++) {
		bucket = &fl_cinfo->buckets[i];
		if (list_empty(&bucket->committing))
			continue;
		nfs_retry_commit(&bucket->committing, bucket->clseg, cinfo);
		spin_lock(cinfo->lock);
		freeme = bucket->clseg;
		bucket->clseg = NULL;
		spin_unlock(cinfo->lock);
		pnfs_put_lseg(freeme);
	}
}

static unsigned int
pnfs_generic_alloc_ds_commits(struct nfs_commit_info *cinfo,
			      struct list_head *list)
{
	struct pnfs_ds_commit_info *fl_cinfo;
	struct pnfs_commit_bucket *bucket;
	struct nfs_commit_data *data;
	int i;
	unsigned int nreq = 0;

	fl_cinfo = cinfo->ds;
	bucket = fl_cinfo->buckets;
	for (i = 0; i < fl_cinfo->nbuckets; i++, bucket++) {
		if (list_empty(&bucket->committing))
			continue;
		data = nfs_commitdata_alloc();
		if (!data)
			break;
		data->ds_commit_index = i;
		spin_lock(cinfo->lock);
		data->lseg = bucket->clseg;
		bucket->clseg = NULL;
		spin_unlock(cinfo->lock);
		list_add(&data->pages, list);
		nreq++;
	}

	/* Clean up on error */
	pnfs_generic_retry_commit(cinfo, i);
	return nreq;
}

/* This follows nfs_commit_list pretty closely */
int
pnfs_generic_commit_pagelist(struct inode *inode, struct list_head *mds_pages,
			     int how, struct nfs_commit_info *cinfo,
			     int (*initiate_commit)(struct nfs_commit_data *data,
						    int how))
{
	struct nfs_commit_data *data, *tmp;
	LIST_HEAD(list);
	unsigned int nreq = 0;

	if (!list_empty(mds_pages)) {
		data = nfs_commitdata_alloc();
		if (data != NULL) {
			data->lseg = NULL;
			list_add(&data->pages, &list);
			nreq++;
		} else {
			nfs_retry_commit(mds_pages, NULL, cinfo);
			pnfs_generic_retry_commit(cinfo, 0);
			cinfo->completion_ops->error_cleanup(NFS_I(inode));
			return -ENOMEM;
		}
	}

	nreq += pnfs_generic_alloc_ds_commits(cinfo, &list);

	if (nreq == 0) {
		cinfo->completion_ops->error_cleanup(NFS_I(inode));
		goto out;
	}

	atomic_add(nreq, &cinfo->mds->rpcs_out);

	list_for_each_entry_safe(data, tmp, &list, pages) {
		list_del_init(&data->pages);
		if (!data->lseg) {
			nfs_init_commit(data, mds_pages, NULL, cinfo);
			nfs_initiate_commit(NFS_CLIENT(inode), data,
					    data->mds_ops, how, 0);
		} else {
			struct pnfs_commit_bucket *buckets;

			buckets = cinfo->ds->buckets;
			nfs_init_commit(data,
					&buckets[data->ds_commit_index].committing,
					data->lseg,
					cinfo);
			initiate_commit(data, how);
		}
	}
out:
	cinfo->ds->ncommitting = 0;
	return PNFS_ATTEMPTED;
}
EXPORT_SYMBOL_GPL(pnfs_generic_commit_pagelist);
