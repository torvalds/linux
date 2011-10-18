/*
 *  Module for the pnfs nfs4 file layout driver.
 *  Defines all I/O and Policy interface operations, plus code
 *  to register itself with the pNFS client.
 *
 *  Copyright (c) 2002
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *
 *  Permission is granted to use, copy, create derivative works, and
 *  redistribute this software and such derivative works for any purpose,
 *  so long as the name of the University of Michigan is not used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright notice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or noninfringement.  The Regents of the University of
 *  Michigan shall not be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>

#include "internal.h"
#include "nfs4filelayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dean Hildebrand <dhildebz@umich.edu>");
MODULE_DESCRIPTION("The NFSv4 file layout driver");

#define FILELAYOUT_POLL_RETRY_MAX     (15*HZ)

static loff_t
filelayout_get_dense_offset(struct nfs4_filelayout_segment *flseg,
			    loff_t offset)
{
	u32 stripe_width = flseg->stripe_unit * flseg->dsaddr->stripe_count;
	u64 tmp;

	offset -= flseg->pattern_offset;
	tmp = offset;
	do_div(tmp, stripe_width);

	return tmp * flseg->stripe_unit + do_div(offset, flseg->stripe_unit);
}

/* This function is used by the layout driver to calculate the
 * offset of the file on the dserver based on whether the
 * layout type is STRIPE_DENSE or STRIPE_SPARSE
 */
static loff_t
filelayout_get_dserver_offset(struct pnfs_layout_segment *lseg, loff_t offset)
{
	struct nfs4_filelayout_segment *flseg = FILELAYOUT_LSEG(lseg);

	switch (flseg->stripe_type) {
	case STRIPE_SPARSE:
		return offset;

	case STRIPE_DENSE:
		return filelayout_get_dense_offset(flseg, offset);
	}

	BUG();
}

/* For data server errors we don't recover from */
static void
filelayout_set_lo_fail(struct pnfs_layout_segment *lseg)
{
	if (lseg->pls_range.iomode == IOMODE_RW) {
		dprintk("%s Setting layout IOMODE_RW fail bit\n", __func__);
		set_bit(lo_fail_bit(IOMODE_RW), &lseg->pls_layout->plh_flags);
	} else {
		dprintk("%s Setting layout IOMODE_READ fail bit\n", __func__);
		set_bit(lo_fail_bit(IOMODE_READ), &lseg->pls_layout->plh_flags);
	}
}

static int filelayout_async_handle_error(struct rpc_task *task,
					 struct nfs4_state *state,
					 struct nfs_client *clp,
					 int *reset)
{
	if (task->tk_status >= 0)
		return 0;

	*reset = 0;

	switch (task->tk_status) {
	case -NFS4ERR_BADSESSION:
	case -NFS4ERR_BADSLOT:
	case -NFS4ERR_BAD_HIGH_SLOT:
	case -NFS4ERR_DEADSESSION:
	case -NFS4ERR_CONN_NOT_BOUND_TO_SESSION:
	case -NFS4ERR_SEQ_FALSE_RETRY:
	case -NFS4ERR_SEQ_MISORDERED:
		dprintk("%s ERROR %d, Reset session. Exchangeid "
			"flags 0x%x\n", __func__, task->tk_status,
			clp->cl_exchange_flags);
		nfs4_schedule_session_recovery(clp->cl_session);
		break;
	case -NFS4ERR_DELAY:
	case -NFS4ERR_GRACE:
	case -EKEYEXPIRED:
		rpc_delay(task, FILELAYOUT_POLL_RETRY_MAX);
		break;
	case -NFS4ERR_RETRY_UNCACHED_REP:
		break;
	default:
		dprintk("%s DS error. Retry through MDS %d\n", __func__,
			task->tk_status);
		*reset = 1;
		break;
	}
	task->tk_status = 0;
	return -EAGAIN;
}

/* NFS_PROTO call done callback routines */

static int filelayout_read_done_cb(struct rpc_task *task,
				struct nfs_read_data *data)
{
	struct nfs_client *clp = data->ds_clp;
	int reset = 0;

	dprintk("%s DS read\n", __func__);

	if (filelayout_async_handle_error(task, data->args.context->state,
					  data->ds_clp, &reset) == -EAGAIN) {
		dprintk("%s calling restart ds_clp %p ds_clp->cl_session %p\n",
			__func__, data->ds_clp, data->ds_clp->cl_session);
		if (reset) {
			filelayout_set_lo_fail(data->lseg);
			nfs4_reset_read(task, data);
			clp = NFS_SERVER(data->inode)->nfs_client;
		}
		nfs_restart_rpc(task, clp);
		return -EAGAIN;
	}

	return 0;
}

/*
 * We reference the rpc_cred of the first WRITE that triggers the need for
 * a LAYOUTCOMMIT, and use it to send the layoutcommit compound.
 * rfc5661 is not clear about which credential should be used.
 */
static void
filelayout_set_layoutcommit(struct nfs_write_data *wdata)
{
	if (FILELAYOUT_LSEG(wdata->lseg)->commit_through_mds ||
	    wdata->res.verf->committed == NFS_FILE_SYNC)
		return;

	pnfs_set_layoutcommit(wdata);
	dprintk("%s ionde %lu pls_end_pos %lu\n", __func__, wdata->inode->i_ino,
		(unsigned long) NFS_I(wdata->inode)->layout->plh_lwb);
}

/*
 * Call ops for the async read/write cases
 * In the case of dense layouts, the offset needs to be reset to its
 * original value.
 */
static void filelayout_read_prepare(struct rpc_task *task, void *data)
{
	struct nfs_read_data *rdata = (struct nfs_read_data *)data;

	rdata->read_done_cb = filelayout_read_done_cb;

	if (nfs41_setup_sequence(rdata->ds_clp->cl_session,
				&rdata->args.seq_args, &rdata->res.seq_res,
				0, task))
		return;

	rpc_call_start(task);
}

static void filelayout_read_call_done(struct rpc_task *task, void *data)
{
	struct nfs_read_data *rdata = (struct nfs_read_data *)data;

	dprintk("--> %s task->tk_status %d\n", __func__, task->tk_status);

	/* Note this may cause RPC to be resent */
	rdata->mds_ops->rpc_call_done(task, data);
}

static void filelayout_read_release(void *data)
{
	struct nfs_read_data *rdata = (struct nfs_read_data *)data;

	rdata->mds_ops->rpc_release(data);
}

static int filelayout_write_done_cb(struct rpc_task *task,
				struct nfs_write_data *data)
{
	int reset = 0;

	if (filelayout_async_handle_error(task, data->args.context->state,
					  data->ds_clp, &reset) == -EAGAIN) {
		struct nfs_client *clp;

		dprintk("%s calling restart ds_clp %p ds_clp->cl_session %p\n",
			__func__, data->ds_clp, data->ds_clp->cl_session);
		if (reset) {
			filelayout_set_lo_fail(data->lseg);
			nfs4_reset_write(task, data);
			clp = NFS_SERVER(data->inode)->nfs_client;
		} else
			clp = data->ds_clp;
		nfs_restart_rpc(task, clp);
		return -EAGAIN;
	}

	filelayout_set_layoutcommit(data);
	return 0;
}

/* Fake up some data that will cause nfs_commit_release to retry the writes. */
static void prepare_to_resend_writes(struct nfs_write_data *data)
{
	struct nfs_page *first = nfs_list_entry(data->pages.next);

	data->task.tk_status = 0;
	memcpy(data->verf.verifier, first->wb_verf.verifier,
	       sizeof(first->wb_verf.verifier));
	data->verf.verifier[0]++; /* ensure verifier mismatch */
}

static int filelayout_commit_done_cb(struct rpc_task *task,
				     struct nfs_write_data *data)
{
	int reset = 0;

	if (filelayout_async_handle_error(task, data->args.context->state,
					  data->ds_clp, &reset) == -EAGAIN) {
		dprintk("%s calling restart ds_clp %p ds_clp->cl_session %p\n",
			__func__, data->ds_clp, data->ds_clp->cl_session);
		if (reset) {
			prepare_to_resend_writes(data);
			filelayout_set_lo_fail(data->lseg);
		} else
			nfs_restart_rpc(task, data->ds_clp);
		return -EAGAIN;
	}

	return 0;
}

static void filelayout_write_prepare(struct rpc_task *task, void *data)
{
	struct nfs_write_data *wdata = (struct nfs_write_data *)data;

	if (nfs41_setup_sequence(wdata->ds_clp->cl_session,
				&wdata->args.seq_args, &wdata->res.seq_res,
				0, task))
		return;

	rpc_call_start(task);
}

static void filelayout_write_call_done(struct rpc_task *task, void *data)
{
	struct nfs_write_data *wdata = (struct nfs_write_data *)data;

	/* Note this may cause RPC to be resent */
	wdata->mds_ops->rpc_call_done(task, data);
}

static void filelayout_write_release(void *data)
{
	struct nfs_write_data *wdata = (struct nfs_write_data *)data;

	wdata->mds_ops->rpc_release(data);
}

static void filelayout_commit_release(void *data)
{
	struct nfs_write_data *wdata = (struct nfs_write_data *)data;

	nfs_commit_release_pages(wdata);
	if (atomic_dec_and_test(&NFS_I(wdata->inode)->commits_outstanding))
		nfs_commit_clear_lock(NFS_I(wdata->inode));
	nfs_commitdata_release(wdata);
}

struct rpc_call_ops filelayout_read_call_ops = {
	.rpc_call_prepare = filelayout_read_prepare,
	.rpc_call_done = filelayout_read_call_done,
	.rpc_release = filelayout_read_release,
};

struct rpc_call_ops filelayout_write_call_ops = {
	.rpc_call_prepare = filelayout_write_prepare,
	.rpc_call_done = filelayout_write_call_done,
	.rpc_release = filelayout_write_release,
};

struct rpc_call_ops filelayout_commit_call_ops = {
	.rpc_call_prepare = filelayout_write_prepare,
	.rpc_call_done = filelayout_write_call_done,
	.rpc_release = filelayout_commit_release,
};

static enum pnfs_try_status
filelayout_read_pagelist(struct nfs_read_data *data)
{
	struct pnfs_layout_segment *lseg = data->lseg;
	struct nfs4_pnfs_ds *ds;
	loff_t offset = data->args.offset;
	u32 j, idx;
	struct nfs_fh *fh;
	int status;

	dprintk("--> %s ino %lu pgbase %u req %Zu@%llu\n",
		__func__, data->inode->i_ino,
		data->args.pgbase, (size_t)data->args.count, offset);

	/* Retrieve the correct rpc_client for the byte range */
	j = nfs4_fl_calc_j_index(lseg, offset);
	idx = nfs4_fl_calc_ds_index(lseg, j);
	ds = nfs4_fl_prepare_ds(lseg, idx);
	if (!ds) {
		/* Either layout fh index faulty, or ds connect failed */
		set_bit(lo_fail_bit(IOMODE_RW), &lseg->pls_layout->plh_flags);
		set_bit(lo_fail_bit(IOMODE_READ), &lseg->pls_layout->plh_flags);
		return PNFS_NOT_ATTEMPTED;
	}
	dprintk("%s USE DS:ip %x %hu\n", __func__,
		ntohl(ds->ds_ip_addr), ntohs(ds->ds_port));

	/* No multipath support. Use first DS */
	data->ds_clp = ds->ds_clp;
	fh = nfs4_fl_select_ds_fh(lseg, j);
	if (fh)
		data->args.fh = fh;

	data->args.offset = filelayout_get_dserver_offset(lseg, offset);
	data->mds_offset = offset;

	/* Perform an asynchronous read to ds */
	status = nfs_initiate_read(data, ds->ds_clp->cl_rpcclient,
				   &filelayout_read_call_ops);
	BUG_ON(status != 0);
	return PNFS_ATTEMPTED;
}

/* Perform async writes. */
static enum pnfs_try_status
filelayout_write_pagelist(struct nfs_write_data *data, int sync)
{
	struct pnfs_layout_segment *lseg = data->lseg;
	struct nfs4_pnfs_ds *ds;
	loff_t offset = data->args.offset;
	u32 j, idx;
	struct nfs_fh *fh;
	int status;

	/* Retrieve the correct rpc_client for the byte range */
	j = nfs4_fl_calc_j_index(lseg, offset);
	idx = nfs4_fl_calc_ds_index(lseg, j);
	ds = nfs4_fl_prepare_ds(lseg, idx);
	if (!ds) {
		printk(KERN_ERR "%s: prepare_ds failed, use MDS\n", __func__);
		set_bit(lo_fail_bit(IOMODE_RW), &lseg->pls_layout->plh_flags);
		set_bit(lo_fail_bit(IOMODE_READ), &lseg->pls_layout->plh_flags);
		return PNFS_NOT_ATTEMPTED;
	}
	dprintk("%s ino %lu sync %d req %Zu@%llu DS:%x:%hu\n", __func__,
		data->inode->i_ino, sync, (size_t) data->args.count, offset,
		ntohl(ds->ds_ip_addr), ntohs(ds->ds_port));

	data->write_done_cb = filelayout_write_done_cb;
	data->ds_clp = ds->ds_clp;
	fh = nfs4_fl_select_ds_fh(lseg, j);
	if (fh)
		data->args.fh = fh;
	/*
	 * Get the file offset on the dserver. Set the write offset to
	 * this offset and save the original offset.
	 */
	data->args.offset = filelayout_get_dserver_offset(lseg, offset);

	/* Perform an asynchronous write */
	status = nfs_initiate_write(data, ds->ds_clp->cl_rpcclient,
				    &filelayout_write_call_ops, sync);
	BUG_ON(status != 0);
	return PNFS_ATTEMPTED;
}

/*
 * filelayout_check_layout()
 *
 * Make sure layout segment parameters are sane WRT the device.
 * At this point no generic layer initialization of the lseg has occurred,
 * and nothing has been added to the layout_hdr cache.
 *
 */
static int
filelayout_check_layout(struct pnfs_layout_hdr *lo,
			struct nfs4_filelayout_segment *fl,
			struct nfs4_layoutget_res *lgr,
			struct nfs4_deviceid *id,
			gfp_t gfp_flags)
{
	struct nfs4_deviceid_node *d;
	struct nfs4_file_layout_dsaddr *dsaddr;
	int status = -EINVAL;
	struct nfs_server *nfss = NFS_SERVER(lo->plh_inode);

	dprintk("--> %s\n", __func__);

	/* FIXME: remove this check when layout segment support is added */
	if (lgr->range.offset != 0 ||
	    lgr->range.length != NFS4_MAX_UINT64) {
		dprintk("%s Only whole file layouts supported. Use MDS i/o\n",
			__func__);
		goto out;
	}

	if (fl->pattern_offset > lgr->range.offset) {
		dprintk("%s pattern_offset %lld too large\n",
				__func__, fl->pattern_offset);
		goto out;
	}

	if (!fl->stripe_unit || fl->stripe_unit % PAGE_SIZE) {
		dprintk("%s Invalid stripe unit (%u)\n",
			__func__, fl->stripe_unit);
		goto out;
	}

	/* find and reference the deviceid */
	d = nfs4_find_get_deviceid(NFS_SERVER(lo->plh_inode)->pnfs_curr_ld,
				   NFS_SERVER(lo->plh_inode)->nfs_client, id);
	if (d == NULL) {
		dsaddr = get_device_info(lo->plh_inode, id, gfp_flags);
		if (dsaddr == NULL)
			goto out;
	} else
		dsaddr = container_of(d, struct nfs4_file_layout_dsaddr, id_node);
	fl->dsaddr = dsaddr;

	if (fl->first_stripe_index < 0 ||
	    fl->first_stripe_index >= dsaddr->stripe_count) {
		dprintk("%s Bad first_stripe_index %d\n",
				__func__, fl->first_stripe_index);
		goto out_put;
	}

	if ((fl->stripe_type == STRIPE_SPARSE &&
	    fl->num_fh > 1 && fl->num_fh != dsaddr->ds_num) ||
	    (fl->stripe_type == STRIPE_DENSE &&
	    fl->num_fh != dsaddr->stripe_count)) {
		dprintk("%s num_fh %u not valid for given packing\n",
			__func__, fl->num_fh);
		goto out_put;
	}

	if (fl->stripe_unit % nfss->rsize || fl->stripe_unit % nfss->wsize) {
		dprintk("%s Stripe unit (%u) not aligned with rsize %u "
			"wsize %u\n", __func__, fl->stripe_unit, nfss->rsize,
			nfss->wsize);
	}

	status = 0;
out:
	dprintk("--> %s returns %d\n", __func__, status);
	return status;
out_put:
	nfs4_fl_put_deviceid(dsaddr);
	goto out;
}

static void filelayout_free_fh_array(struct nfs4_filelayout_segment *fl)
{
	int i;

	for (i = 0; i < fl->num_fh; i++) {
		if (!fl->fh_array[i])
			break;
		kfree(fl->fh_array[i]);
	}
	kfree(fl->fh_array);
	fl->fh_array = NULL;
}

static void
_filelayout_free_lseg(struct nfs4_filelayout_segment *fl)
{
	filelayout_free_fh_array(fl);
	kfree(fl);
}

static int
filelayout_decode_layout(struct pnfs_layout_hdr *flo,
			 struct nfs4_filelayout_segment *fl,
			 struct nfs4_layoutget_res *lgr,
			 struct nfs4_deviceid *id,
			 gfp_t gfp_flags)
{
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch;
	__be32 *p;
	uint32_t nfl_util;
	int i;

	dprintk("%s: set_layout_map Begin\n", __func__);

	scratch = alloc_page(gfp_flags);
	if (!scratch)
		return -ENOMEM;

	xdr_init_decode_pages(&stream, &buf, lgr->layoutp->pages, lgr->layoutp->len);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	/* 20 = ufl_util (4), first_stripe_index (4), pattern_offset (8),
	 * num_fh (4) */
	p = xdr_inline_decode(&stream, NFS4_DEVICEID4_SIZE + 20);
	if (unlikely(!p))
		goto out_err;

	memcpy(id, p, sizeof(*id));
	p += XDR_QUADLEN(NFS4_DEVICEID4_SIZE);
	nfs4_print_deviceid(id);

	nfl_util = be32_to_cpup(p++);
	if (nfl_util & NFL4_UFLG_COMMIT_THRU_MDS)
		fl->commit_through_mds = 1;
	if (nfl_util & NFL4_UFLG_DENSE)
		fl->stripe_type = STRIPE_DENSE;
	else
		fl->stripe_type = STRIPE_SPARSE;
	fl->stripe_unit = nfl_util & ~NFL4_UFLG_MASK;

	fl->first_stripe_index = be32_to_cpup(p++);
	p = xdr_decode_hyper(p, &fl->pattern_offset);
	fl->num_fh = be32_to_cpup(p++);

	dprintk("%s: nfl_util 0x%X num_fh %u fsi %u po %llu\n",
		__func__, nfl_util, fl->num_fh, fl->first_stripe_index,
		fl->pattern_offset);

	/* Note that a zero value for num_fh is legal for STRIPE_SPARSE.
	 * Futher checking is done in filelayout_check_layout */
	if (fl->num_fh < 0 || fl->num_fh >
	    max(NFS4_PNFS_MAX_STRIPE_CNT, NFS4_PNFS_MAX_MULTI_CNT))
		goto out_err;

	if (fl->num_fh > 0) {
		fl->fh_array = kzalloc(fl->num_fh * sizeof(struct nfs_fh *),
				       gfp_flags);
		if (!fl->fh_array)
			goto out_err;
	}

	for (i = 0; i < fl->num_fh; i++) {
		/* Do we want to use a mempool here? */
		fl->fh_array[i] = kmalloc(sizeof(struct nfs_fh), gfp_flags);
		if (!fl->fh_array[i])
			goto out_err_free;

		p = xdr_inline_decode(&stream, 4);
		if (unlikely(!p))
			goto out_err_free;
		fl->fh_array[i]->size = be32_to_cpup(p++);
		if (sizeof(struct nfs_fh) < fl->fh_array[i]->size) {
			printk(KERN_ERR "Too big fh %d received %d\n",
			       i, fl->fh_array[i]->size);
			goto out_err_free;
		}

		p = xdr_inline_decode(&stream, fl->fh_array[i]->size);
		if (unlikely(!p))
			goto out_err_free;
		memcpy(fl->fh_array[i]->data, p, fl->fh_array[i]->size);
		dprintk("DEBUG: %s: fh len %d\n", __func__,
			fl->fh_array[i]->size);
	}

	__free_page(scratch);
	return 0;

out_err_free:
	filelayout_free_fh_array(fl);
out_err:
	__free_page(scratch);
	return -EIO;
}

static void
filelayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct nfs4_filelayout_segment *fl = FILELAYOUT_LSEG(lseg);

	dprintk("--> %s\n", __func__);
	nfs4_fl_put_deviceid(fl->dsaddr);
	kfree(fl->commit_buckets);
	_filelayout_free_lseg(fl);
}

static struct pnfs_layout_segment *
filelayout_alloc_lseg(struct pnfs_layout_hdr *layoutid,
		      struct nfs4_layoutget_res *lgr,
		      gfp_t gfp_flags)
{
	struct nfs4_filelayout_segment *fl;
	int rc;
	struct nfs4_deviceid id;

	dprintk("--> %s\n", __func__);
	fl = kzalloc(sizeof(*fl), gfp_flags);
	if (!fl)
		return NULL;

	rc = filelayout_decode_layout(layoutid, fl, lgr, &id, gfp_flags);
	if (rc != 0 || filelayout_check_layout(layoutid, fl, lgr, &id, gfp_flags)) {
		_filelayout_free_lseg(fl);
		return NULL;
	}

	/* This assumes there is only one IOMODE_RW lseg.  What
	 * we really want to do is have a layout_hdr level
	 * dictionary of <multipath_list4, fh> keys, each
	 * associated with a struct list_head, populated by calls
	 * to filelayout_write_pagelist().
	 * */
	if ((!fl->commit_through_mds) && (lgr->range.iomode == IOMODE_RW)) {
		int i;
		int size = (fl->stripe_type == STRIPE_SPARSE) ?
			fl->dsaddr->ds_num : fl->dsaddr->stripe_count;

		fl->commit_buckets = kcalloc(size, sizeof(struct list_head), gfp_flags);
		if (!fl->commit_buckets) {
			filelayout_free_lseg(&fl->generic_hdr);
			return NULL;
		}
		fl->number_of_buckets = size;
		for (i = 0; i < size; i++)
			INIT_LIST_HEAD(&fl->commit_buckets[i]);
	}
	return &fl->generic_hdr;
}

/*
 * filelayout_pg_test(). Called by nfs_can_coalesce_requests()
 *
 * return true  : coalesce page
 * return false : don't coalesce page
 */
bool
filelayout_pg_test(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
		   struct nfs_page *req)
{
	u64 p_stripe, r_stripe;
	u32 stripe_unit;

	if (!pnfs_generic_pg_test(pgio, prev, req) ||
	    !nfs_generic_pg_test(pgio, prev, req))
		return false;

	if (!pgio->pg_lseg)
		return 1;
	p_stripe = (u64)prev->wb_index << PAGE_CACHE_SHIFT;
	r_stripe = (u64)req->wb_index << PAGE_CACHE_SHIFT;
	stripe_unit = FILELAYOUT_LSEG(pgio->pg_lseg)->stripe_unit;

	do_div(p_stripe, stripe_unit);
	do_div(r_stripe, stripe_unit);

	return (p_stripe == r_stripe);
}

static bool filelayout_mark_pnfs_commit(struct pnfs_layout_segment *lseg)
{
	return !FILELAYOUT_LSEG(lseg)->commit_through_mds;
}

static u32 select_bucket_index(struct nfs4_filelayout_segment *fl, u32 j)
{
	if (fl->stripe_type == STRIPE_SPARSE)
		return nfs4_fl_calc_ds_index(&fl->generic_hdr, j);
	else
		return j;
}

struct list_head *filelayout_choose_commit_list(struct nfs_page *req)
{
	struct pnfs_layout_segment *lseg = req->wb_commit_lseg;
	struct nfs4_filelayout_segment *fl = FILELAYOUT_LSEG(lseg);
	u32 i, j;
	struct list_head *list;

	/* Note that we are calling nfs4_fl_calc_j_index on each page
	 * that ends up being committed to a data server.  An attractive
	 * alternative is to add a field to nfs_write_data and nfs_page
	 * to store the value calculated in filelayout_write_pagelist
	 * and just use that here.
	 */
	j = nfs4_fl_calc_j_index(lseg,
				 (loff_t)req->wb_index << PAGE_CACHE_SHIFT);
	i = select_bucket_index(fl, j);
	list = &fl->commit_buckets[i];
	if (list_empty(list)) {
		/* Non-empty buckets hold a reference on the lseg */
		get_lseg(lseg);
	}
	return list;
}

static u32 calc_ds_index_from_commit(struct pnfs_layout_segment *lseg, u32 i)
{
	struct nfs4_filelayout_segment *flseg = FILELAYOUT_LSEG(lseg);

	if (flseg->stripe_type == STRIPE_SPARSE)
		return i;
	else
		return nfs4_fl_calc_ds_index(lseg, i);
}

static struct nfs_fh *
select_ds_fh_from_commit(struct pnfs_layout_segment *lseg, u32 i)
{
	struct nfs4_filelayout_segment *flseg = FILELAYOUT_LSEG(lseg);

	if (flseg->stripe_type == STRIPE_SPARSE) {
		if (flseg->num_fh == 1)
			i = 0;
		else if (flseg->num_fh == 0)
			/* Use the MDS OPEN fh set in nfs_read_rpcsetup */
			return NULL;
	}
	return flseg->fh_array[i];
}

static int filelayout_initiate_commit(struct nfs_write_data *data, int how)
{
	struct pnfs_layout_segment *lseg = data->lseg;
	struct nfs4_pnfs_ds *ds;
	u32 idx;
	struct nfs_fh *fh;

	idx = calc_ds_index_from_commit(lseg, data->ds_commit_index);
	ds = nfs4_fl_prepare_ds(lseg, idx);
	if (!ds) {
		printk(KERN_ERR "%s: prepare_ds failed, use MDS\n", __func__);
		set_bit(lo_fail_bit(IOMODE_RW), &lseg->pls_layout->plh_flags);
		set_bit(lo_fail_bit(IOMODE_READ), &lseg->pls_layout->plh_flags);
		prepare_to_resend_writes(data);
		data->mds_ops->rpc_release(data);
		return -EAGAIN;
	}
	dprintk("%s ino %lu, how %d\n", __func__, data->inode->i_ino, how);
	data->write_done_cb = filelayout_commit_done_cb;
	data->ds_clp = ds->ds_clp;
	fh = select_ds_fh_from_commit(lseg, data->ds_commit_index);
	if (fh)
		data->args.fh = fh;
	return nfs_initiate_commit(data, ds->ds_clp->cl_rpcclient,
				   &filelayout_commit_call_ops, how);
}

/*
 * This is only useful while we are using whole file layouts.
 */
static struct pnfs_layout_segment *find_only_write_lseg(struct inode *inode)
{
	struct pnfs_layout_segment *lseg, *rv = NULL;

	spin_lock(&inode->i_lock);
	list_for_each_entry(lseg, &NFS_I(inode)->layout->plh_segs, pls_list)
		if (lseg->pls_range.iomode == IOMODE_RW)
			rv = get_lseg(lseg);
	spin_unlock(&inode->i_lock);
	return rv;
}

static int alloc_ds_commits(struct inode *inode, struct list_head *list)
{
	struct pnfs_layout_segment *lseg;
	struct nfs4_filelayout_segment *fl;
	struct nfs_write_data *data;
	int i, j;

	/* Won't need this when non-whole file layout segments are supported
	 * instead we will use a pnfs_layout_hdr structure */
	lseg = find_only_write_lseg(inode);
	if (!lseg)
		return 0;
	fl = FILELAYOUT_LSEG(lseg);
	for (i = 0; i < fl->number_of_buckets; i++) {
		if (list_empty(&fl->commit_buckets[i]))
			continue;
		data = nfs_commitdata_alloc();
		if (!data)
			goto out_bad;
		data->ds_commit_index = i;
		data->lseg = lseg;
		list_add(&data->pages, list);
	}
	put_lseg(lseg);
	return 0;

out_bad:
	for (j = i; j < fl->number_of_buckets; j++) {
		if (list_empty(&fl->commit_buckets[i]))
			continue;
		nfs_retry_commit(&fl->commit_buckets[i], lseg);
		put_lseg(lseg);  /* associated with emptying bucket */
	}
	put_lseg(lseg);
	/* Caller will clean up entries put on list */
	return -ENOMEM;
}

/* This follows nfs_commit_list pretty closely */
static int
filelayout_commit_pagelist(struct inode *inode, struct list_head *mds_pages,
			   int how)
{
	struct nfs_write_data	*data, *tmp;
	LIST_HEAD(list);

	if (!list_empty(mds_pages)) {
		data = nfs_commitdata_alloc();
		if (!data)
			goto out_bad;
		data->lseg = NULL;
		list_add(&data->pages, &list);
	}

	if (alloc_ds_commits(inode, &list))
		goto out_bad;

	list_for_each_entry_safe(data, tmp, &list, pages) {
		list_del_init(&data->pages);
		atomic_inc(&NFS_I(inode)->commits_outstanding);
		if (!data->lseg) {
			nfs_init_commit(data, mds_pages, NULL);
			nfs_initiate_commit(data, NFS_CLIENT(inode),
					    data->mds_ops, how);
		} else {
			nfs_init_commit(data, &FILELAYOUT_LSEG(data->lseg)->commit_buckets[data->ds_commit_index], data->lseg);
			filelayout_initiate_commit(data, how);
		}
	}
	return 0;
 out_bad:
	list_for_each_entry_safe(data, tmp, &list, pages) {
		nfs_retry_commit(&data->pages, data->lseg);
		list_del_init(&data->pages);
		nfs_commit_free(data);
	}
	nfs_retry_commit(mds_pages, NULL);
	nfs_commit_clear_lock(NFS_I(inode));
	return -ENOMEM;
}

static void
filelayout_free_deveiceid_node(struct nfs4_deviceid_node *d)
{
	nfs4_fl_free_deviceid(container_of(d, struct nfs4_file_layout_dsaddr, id_node));
}

static struct pnfs_layoutdriver_type filelayout_type = {
	.id			= LAYOUT_NFSV4_1_FILES,
	.name			= "LAYOUT_NFSV4_1_FILES",
	.owner			= THIS_MODULE,
	.alloc_lseg		= filelayout_alloc_lseg,
	.free_lseg		= filelayout_free_lseg,
	.pg_test		= filelayout_pg_test,
	.mark_pnfs_commit	= filelayout_mark_pnfs_commit,
	.choose_commit_list	= filelayout_choose_commit_list,
	.commit_pagelist	= filelayout_commit_pagelist,
	.read_pagelist		= filelayout_read_pagelist,
	.write_pagelist		= filelayout_write_pagelist,
	.free_deviceid_node	= filelayout_free_deveiceid_node,
};

static int __init nfs4filelayout_init(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Registering...\n",
	       __func__);
	return pnfs_register_layoutdriver(&filelayout_type);
}

static void __exit nfs4filelayout_exit(void)
{
	printk(KERN_INFO "%s: NFSv4 File Layout Driver Unregistering...\n",
	       __func__);
	pnfs_unregister_layoutdriver(&filelayout_type);
}

module_init(nfs4filelayout_init);
module_exit(nfs4filelayout_exit);
