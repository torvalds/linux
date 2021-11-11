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
#include <linux/module.h>
#include <linux/backing-dev.h>

#include <linux/sunrpc/metrics.h>

#include "../nfs4session.h"
#include "../internal.h"
#include "../delegation.h"
#include "filelayout.h"
#include "../nfs4trace.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dean Hildebrand <dhildebz@umich.edu>");
MODULE_DESCRIPTION("The NFSv4 file layout driver");

#define FILELAYOUT_POLL_RETRY_MAX     (15*HZ)
static const struct pnfs_commit_ops filelayout_commit_ops;

static loff_t
filelayout_get_dense_offset(struct nfs4_filelayout_segment *flseg,
			    loff_t offset)
{
	u32 stripe_width = flseg->stripe_unit * flseg->dsaddr->stripe_count;
	u64 stripe_no;
	u32 rem;

	offset -= flseg->pattern_offset;
	stripe_no = div_u64(offset, stripe_width);
	div_u64_rem(offset, flseg->stripe_unit, &rem);

	return stripe_no * flseg->stripe_unit + rem;
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

static void filelayout_reset_write(struct nfs_pgio_header *hdr)
{
	struct rpc_task *task = &hdr->task;

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		dprintk("%s Reset task %5u for i/o through MDS "
			"(req %s/%llu, %u bytes @ offset %llu)\n", __func__,
			hdr->task.tk_pid,
			hdr->inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(hdr->inode),
			hdr->args.count,
			(unsigned long long)hdr->args.offset);

		task->tk_status = pnfs_write_done_resend_to_mds(hdr);
	}
}

static void filelayout_reset_read(struct nfs_pgio_header *hdr)
{
	struct rpc_task *task = &hdr->task;

	if (!test_and_set_bit(NFS_IOHDR_REDO, &hdr->flags)) {
		dprintk("%s Reset task %5u for i/o through MDS "
			"(req %s/%llu, %u bytes @ offset %llu)\n", __func__,
			hdr->task.tk_pid,
			hdr->inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(hdr->inode),
			hdr->args.count,
			(unsigned long long)hdr->args.offset);

		task->tk_status = pnfs_read_done_resend_to_mds(hdr);
	}
}

static int filelayout_async_handle_error(struct rpc_task *task,
					 struct nfs4_state *state,
					 struct nfs_client *clp,
					 struct pnfs_layout_segment *lseg)
{
	struct pnfs_layout_hdr *lo = lseg->pls_layout;
	struct inode *inode = lo->plh_inode;
	struct nfs4_deviceid_node *devid = FILELAYOUT_DEVID_NODE(lseg);
	struct nfs4_slot_table *tbl = &clp->cl_session->fc_slot_table;

	if (task->tk_status >= 0)
		return 0;

	switch (task->tk_status) {
	/* DS session errors */
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
		nfs4_schedule_session_recovery(clp->cl_session, task->tk_status);
		break;
	case -NFS4ERR_DELAY:
	case -NFS4ERR_GRACE:
		rpc_delay(task, FILELAYOUT_POLL_RETRY_MAX);
		break;
	case -NFS4ERR_RETRY_UNCACHED_REP:
		break;
	/* Invalidate Layout errors */
	case -NFS4ERR_ACCESS:
	case -NFS4ERR_PNFS_NO_LAYOUT:
	case -ESTALE:           /* mapped NFS4ERR_STALE */
	case -EBADHANDLE:       /* mapped NFS4ERR_BADHANDLE */
	case -EISDIR:           /* mapped NFS4ERR_ISDIR */
	case -NFS4ERR_FHEXPIRED:
	case -NFS4ERR_WRONG_TYPE:
		dprintk("%s Invalid layout error %d\n", __func__,
			task->tk_status);
		/*
		 * Destroy layout so new i/o will get a new layout.
		 * Layout will not be destroyed until all current lseg
		 * references are put. Mark layout as invalid to resend failed
		 * i/o and all i/o waiting on the slot table to the MDS until
		 * layout is destroyed and a new valid layout is obtained.
		 */
		pnfs_destroy_layout(NFS_I(inode));
		rpc_wake_up(&tbl->slot_tbl_waitq);
		goto reset;
	/* RPC connection errors */
	case -ECONNREFUSED:
	case -EHOSTDOWN:
	case -EHOSTUNREACH:
	case -ENETUNREACH:
	case -EIO:
	case -ETIMEDOUT:
	case -EPIPE:
		dprintk("%s DS connection error %d\n", __func__,
			task->tk_status);
		nfs4_mark_deviceid_unavailable(devid);
		pnfs_error_mark_layout_for_return(inode, lseg);
		pnfs_set_lo_fail(lseg);
		rpc_wake_up(&tbl->slot_tbl_waitq);
		fallthrough;
	default:
reset:
		dprintk("%s Retry through MDS. Error %d\n", __func__,
			task->tk_status);
		return -NFS4ERR_RESET_TO_MDS;
	}
	task->tk_status = 0;
	return -EAGAIN;
}

/* NFS_PROTO call done callback routines */

static int filelayout_read_done_cb(struct rpc_task *task,
				struct nfs_pgio_header *hdr)
{
	int err;

	trace_nfs4_pnfs_read(hdr, task->tk_status);
	err = filelayout_async_handle_error(task, hdr->args.context->state,
					    hdr->ds_clp, hdr->lseg);

	switch (err) {
	case -NFS4ERR_RESET_TO_MDS:
		filelayout_reset_read(hdr);
		return task->tk_status;
	case -EAGAIN:
		rpc_restart_call_prepare(task);
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
filelayout_set_layoutcommit(struct nfs_pgio_header *hdr)
{
	loff_t end_offs = 0;

	if (FILELAYOUT_LSEG(hdr->lseg)->commit_through_mds ||
	    hdr->res.verf->committed == NFS_FILE_SYNC)
		return;
	if (hdr->res.verf->committed == NFS_DATA_SYNC)
		end_offs = hdr->mds_offset + (loff_t)hdr->res.count;

	/* Note: if the write is unstable, don't set end_offs until commit */
	pnfs_set_layoutcommit(hdr->inode, hdr->lseg, end_offs);
	dprintk("%s inode %lu pls_end_pos %lu\n", __func__, hdr->inode->i_ino,
		(unsigned long) NFS_I(hdr->inode)->layout->plh_lwb);
}

bool
filelayout_test_devid_unavailable(struct nfs4_deviceid_node *node)
{
	return filelayout_test_devid_invalid(node) ||
		nfs4_test_deviceid_unavailable(node);
}

static bool
filelayout_reset_to_mds(struct pnfs_layout_segment *lseg)
{
	struct nfs4_deviceid_node *node = FILELAYOUT_DEVID_NODE(lseg);

	return filelayout_test_devid_unavailable(node);
}

/*
 * Call ops for the async read/write cases
 * In the case of dense layouts, the offset needs to be reset to its
 * original value.
 */
static void filelayout_read_prepare(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (unlikely(test_bit(NFS_CONTEXT_BAD, &hdr->args.context->flags))) {
		rpc_exit(task, -EIO);
		return;
	}
	if (filelayout_reset_to_mds(hdr->lseg)) {
		dprintk("%s task %u reset io to MDS\n", __func__, task->tk_pid);
		filelayout_reset_read(hdr);
		rpc_exit(task, 0);
		return;
	}
	hdr->pgio_done_cb = filelayout_read_done_cb;

	if (nfs4_setup_sequence(hdr->ds_clp,
			&hdr->args.seq_args,
			&hdr->res.seq_res,
			task))
		return;
	if (nfs4_set_rw_stateid(&hdr->args.stateid, hdr->args.context,
			hdr->args.lock_context, FMODE_READ) == -EIO)
		rpc_exit(task, -EIO); /* lost lock, terminate I/O */
}

static void filelayout_read_call_done(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags) &&
	    task->tk_status == 0) {
		nfs41_sequence_done(task, &hdr->res.seq_res);
		return;
	}

	/* Note this may cause RPC to be resent */
	hdr->mds_ops->rpc_call_done(task, data);
}

static void filelayout_read_count_stats(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	rpc_count_iostats(task, NFS_SERVER(hdr->inode)->client->cl_metrics);
}

static int filelayout_write_done_cb(struct rpc_task *task,
				struct nfs_pgio_header *hdr)
{
	int err;

	trace_nfs4_pnfs_write(hdr, task->tk_status);
	err = filelayout_async_handle_error(task, hdr->args.context->state,
					    hdr->ds_clp, hdr->lseg);

	switch (err) {
	case -NFS4ERR_RESET_TO_MDS:
		filelayout_reset_write(hdr);
		return task->tk_status;
	case -EAGAIN:
		rpc_restart_call_prepare(task);
		return -EAGAIN;
	}

	filelayout_set_layoutcommit(hdr);

	/* zero out the fattr */
	hdr->fattr.valid = 0;
	if (task->tk_status >= 0)
		nfs_writeback_update_inode(hdr);

	return 0;
}

static int filelayout_commit_done_cb(struct rpc_task *task,
				     struct nfs_commit_data *data)
{
	int err;

	trace_nfs4_pnfs_commit_ds(data, task->tk_status);
	err = filelayout_async_handle_error(task, NULL, data->ds_clp,
					    data->lseg);

	switch (err) {
	case -NFS4ERR_RESET_TO_MDS:
		pnfs_generic_prepare_to_resend_writes(data);
		return -EAGAIN;
	case -EAGAIN:
		rpc_restart_call_prepare(task);
		return -EAGAIN;
	}

	pnfs_set_layoutcommit(data->inode, data->lseg, data->lwb);

	return 0;
}

static void filelayout_write_prepare(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (unlikely(test_bit(NFS_CONTEXT_BAD, &hdr->args.context->flags))) {
		rpc_exit(task, -EIO);
		return;
	}
	if (filelayout_reset_to_mds(hdr->lseg)) {
		dprintk("%s task %u reset io to MDS\n", __func__, task->tk_pid);
		filelayout_reset_write(hdr);
		rpc_exit(task, 0);
		return;
	}
	if (nfs4_setup_sequence(hdr->ds_clp,
			&hdr->args.seq_args,
			&hdr->res.seq_res,
			task))
		return;
	if (nfs4_set_rw_stateid(&hdr->args.stateid, hdr->args.context,
			hdr->args.lock_context, FMODE_WRITE) == -EIO)
		rpc_exit(task, -EIO); /* lost lock, terminate I/O */
}

static void filelayout_write_call_done(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	if (test_bit(NFS_IOHDR_REDO, &hdr->flags) &&
	    task->tk_status == 0) {
		nfs41_sequence_done(task, &hdr->res.seq_res);
		return;
	}

	/* Note this may cause RPC to be resent */
	hdr->mds_ops->rpc_call_done(task, data);
}

static void filelayout_write_count_stats(struct rpc_task *task, void *data)
{
	struct nfs_pgio_header *hdr = data;

	rpc_count_iostats(task, NFS_SERVER(hdr->inode)->client->cl_metrics);
}

static void filelayout_commit_prepare(struct rpc_task *task, void *data)
{
	struct nfs_commit_data *wdata = data;

	nfs4_setup_sequence(wdata->ds_clp,
			&wdata->args.seq_args,
			&wdata->res.seq_res,
			task);
}

static void filelayout_commit_count_stats(struct rpc_task *task, void *data)
{
	struct nfs_commit_data *cdata = data;

	rpc_count_iostats(task, NFS_SERVER(cdata->inode)->client->cl_metrics);
}

static const struct rpc_call_ops filelayout_read_call_ops = {
	.rpc_call_prepare = filelayout_read_prepare,
	.rpc_call_done = filelayout_read_call_done,
	.rpc_count_stats = filelayout_read_count_stats,
	.rpc_release = pnfs_generic_rw_release,
};

static const struct rpc_call_ops filelayout_write_call_ops = {
	.rpc_call_prepare = filelayout_write_prepare,
	.rpc_call_done = filelayout_write_call_done,
	.rpc_count_stats = filelayout_write_count_stats,
	.rpc_release = pnfs_generic_rw_release,
};

static const struct rpc_call_ops filelayout_commit_call_ops = {
	.rpc_call_prepare = filelayout_commit_prepare,
	.rpc_call_done = pnfs_generic_write_commit_done,
	.rpc_count_stats = filelayout_commit_count_stats,
	.rpc_release = pnfs_generic_commit_release,
};

static enum pnfs_try_status
filelayout_read_pagelist(struct nfs_pgio_header *hdr)
{
	struct pnfs_layout_segment *lseg = hdr->lseg;
	struct nfs4_pnfs_ds *ds;
	struct rpc_clnt *ds_clnt;
	loff_t offset = hdr->args.offset;
	u32 j, idx;
	struct nfs_fh *fh;

	dprintk("--> %s ino %lu pgbase %u req %zu@%llu\n",
		__func__, hdr->inode->i_ino,
		hdr->args.pgbase, (size_t)hdr->args.count, offset);

	/* Retrieve the correct rpc_client for the byte range */
	j = nfs4_fl_calc_j_index(lseg, offset);
	idx = nfs4_fl_calc_ds_index(lseg, j);
	ds = nfs4_fl_prepare_ds(lseg, idx);
	if (!ds)
		return PNFS_NOT_ATTEMPTED;

	ds_clnt = nfs4_find_or_create_ds_client(ds->ds_clp, hdr->inode);
	if (IS_ERR(ds_clnt))
		return PNFS_NOT_ATTEMPTED;

	dprintk("%s USE DS: %s cl_count %d\n", __func__,
		ds->ds_remotestr, refcount_read(&ds->ds_clp->cl_count));

	/* No multipath support. Use first DS */
	refcount_inc(&ds->ds_clp->cl_count);
	hdr->ds_clp = ds->ds_clp;
	hdr->ds_commit_idx = idx;
	fh = nfs4_fl_select_ds_fh(lseg, j);
	if (fh)
		hdr->args.fh = fh;

	hdr->args.offset = filelayout_get_dserver_offset(lseg, offset);
	hdr->mds_offset = offset;

	/* Perform an asynchronous read to ds */
	nfs_initiate_pgio(ds_clnt, hdr, hdr->cred,
			  NFS_PROTO(hdr->inode), &filelayout_read_call_ops,
			  0, RPC_TASK_SOFTCONN);
	return PNFS_ATTEMPTED;
}

/* Perform async writes. */
static enum pnfs_try_status
filelayout_write_pagelist(struct nfs_pgio_header *hdr, int sync)
{
	struct pnfs_layout_segment *lseg = hdr->lseg;
	struct nfs4_pnfs_ds *ds;
	struct rpc_clnt *ds_clnt;
	loff_t offset = hdr->args.offset;
	u32 j, idx;
	struct nfs_fh *fh;

	/* Retrieve the correct rpc_client for the byte range */
	j = nfs4_fl_calc_j_index(lseg, offset);
	idx = nfs4_fl_calc_ds_index(lseg, j);
	ds = nfs4_fl_prepare_ds(lseg, idx);
	if (!ds)
		return PNFS_NOT_ATTEMPTED;

	ds_clnt = nfs4_find_or_create_ds_client(ds->ds_clp, hdr->inode);
	if (IS_ERR(ds_clnt))
		return PNFS_NOT_ATTEMPTED;

	dprintk("%s ino %lu sync %d req %zu@%llu DS: %s cl_count %d\n",
		__func__, hdr->inode->i_ino, sync, (size_t) hdr->args.count,
		offset, ds->ds_remotestr, refcount_read(&ds->ds_clp->cl_count));

	hdr->pgio_done_cb = filelayout_write_done_cb;
	refcount_inc(&ds->ds_clp->cl_count);
	hdr->ds_clp = ds->ds_clp;
	hdr->ds_commit_idx = idx;
	fh = nfs4_fl_select_ds_fh(lseg, j);
	if (fh)
		hdr->args.fh = fh;
	hdr->args.offset = filelayout_get_dserver_offset(lseg, offset);

	/* Perform an asynchronous write */
	nfs_initiate_pgio(ds_clnt, hdr, hdr->cred,
			  NFS_PROTO(hdr->inode), &filelayout_write_call_ops,
			  sync, RPC_TASK_SOFTCONN);
	return PNFS_ATTEMPTED;
}

static int
filelayout_check_deviceid(struct pnfs_layout_hdr *lo,
			  struct nfs4_filelayout_segment *fl,
			  gfp_t gfp_flags)
{
	struct nfs4_deviceid_node *d;
	struct nfs4_file_layout_dsaddr *dsaddr;
	int status = -EINVAL;

	/* Is the deviceid already set? If so, we're good. */
	if (fl->dsaddr != NULL)
		return 0;

	/* find and reference the deviceid */
	d = nfs4_find_get_deviceid(NFS_SERVER(lo->plh_inode), &fl->deviceid,
			lo->plh_lc_cred, gfp_flags);
	if (d == NULL)
		goto out;

	dsaddr = container_of(d, struct nfs4_file_layout_dsaddr, id_node);
	/* Found deviceid is unavailable */
	if (filelayout_test_devid_unavailable(&dsaddr->id_node))
		goto out_put;

	if (fl->first_stripe_index >= dsaddr->stripe_count) {
		dprintk("%s Bad first_stripe_index %u\n",
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
	status = 0;

	/*
	 * Atomic compare and xchange to ensure we don't scribble
	 * over a non-NULL pointer.
	 */
	if (cmpxchg(&fl->dsaddr, NULL, dsaddr) != NULL)
		goto out_put;
out:
	return status;
out_put:
	nfs4_fl_put_deviceid(dsaddr);
	goto out;
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
			gfp_t gfp_flags)
{
	int status = -EINVAL;

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

	if (!fl->stripe_unit) {
		dprintk("%s Invalid stripe unit (%u)\n",
			__func__, fl->stripe_unit);
		goto out;
	}

	status = 0;
out:
	dprintk("--> %s returns %d\n", __func__, status);
	return status;
}

static void _filelayout_free_lseg(struct nfs4_filelayout_segment *fl)
{
	int i;

	if (fl->fh_array) {
		for (i = 0; i < fl->num_fh; i++) {
			if (!fl->fh_array[i])
				break;
			kfree(fl->fh_array[i]);
		}
		kfree(fl->fh_array);
	}
	kfree(fl);
}

static int
filelayout_decode_layout(struct pnfs_layout_hdr *flo,
			 struct nfs4_filelayout_segment *fl,
			 struct nfs4_layoutget_res *lgr,
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
	xdr_set_scratch_page(&stream, scratch);

	/* 20 = ufl_util (4), first_stripe_index (4), pattern_offset (8),
	 * num_fh (4) */
	p = xdr_inline_decode(&stream, NFS4_DEVICEID4_SIZE + 20);
	if (unlikely(!p))
		goto out_err;

	memcpy(&fl->deviceid, p, sizeof(fl->deviceid));
	p += XDR_QUADLEN(NFS4_DEVICEID4_SIZE);
	nfs4_print_deviceid(&fl->deviceid);

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
	if (fl->num_fh >
	    max(NFS4_PNFS_MAX_STRIPE_CNT, NFS4_PNFS_MAX_MULTI_CNT))
		goto out_err;

	if (fl->num_fh > 0) {
		fl->fh_array = kcalloc(fl->num_fh, sizeof(fl->fh_array[0]),
				       gfp_flags);
		if (!fl->fh_array)
			goto out_err;
	}

	for (i = 0; i < fl->num_fh; i++) {
		/* Do we want to use a mempool here? */
		fl->fh_array[i] = kmalloc(sizeof(struct nfs_fh), gfp_flags);
		if (!fl->fh_array[i])
			goto out_err;

		p = xdr_inline_decode(&stream, 4);
		if (unlikely(!p))
			goto out_err;
		fl->fh_array[i]->size = be32_to_cpup(p++);
		if (fl->fh_array[i]->size > NFS_MAXFHSIZE) {
			printk(KERN_ERR "NFS: Too big fh %d received %d\n",
			       i, fl->fh_array[i]->size);
			goto out_err;
		}

		p = xdr_inline_decode(&stream, fl->fh_array[i]->size);
		if (unlikely(!p))
			goto out_err;
		memcpy(fl->fh_array[i]->data, p, fl->fh_array[i]->size);
		dprintk("DEBUG: %s: fh len %d\n", __func__,
			fl->fh_array[i]->size);
	}

	__free_page(scratch);
	return 0;

out_err:
	__free_page(scratch);
	return -EIO;
}

static void
filelayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	struct nfs4_filelayout_segment *fl = FILELAYOUT_LSEG(lseg);

	dprintk("--> %s\n", __func__);
	if (fl->dsaddr != NULL)
		nfs4_fl_put_deviceid(fl->dsaddr);
	/* This assumes a single RW lseg */
	if (lseg->pls_range.iomode == IOMODE_RW) {
		struct nfs4_filelayout *flo;
		struct inode *inode;

		flo = FILELAYOUT_FROM_HDR(lseg->pls_layout);
		inode = flo->generic_hdr.plh_inode;
		spin_lock(&inode->i_lock);
		pnfs_generic_ds_cinfo_release_lseg(&flo->commit_info, lseg);
		spin_unlock(&inode->i_lock);
	}
	_filelayout_free_lseg(fl);
}

static struct pnfs_layout_segment *
filelayout_alloc_lseg(struct pnfs_layout_hdr *layoutid,
		      struct nfs4_layoutget_res *lgr,
		      gfp_t gfp_flags)
{
	struct nfs4_filelayout_segment *fl;
	int rc;

	dprintk("--> %s\n", __func__);
	fl = kzalloc(sizeof(*fl), gfp_flags);
	if (!fl)
		return NULL;

	rc = filelayout_decode_layout(layoutid, fl, lgr, gfp_flags);
	if (rc != 0 || filelayout_check_layout(layoutid, fl, lgr, gfp_flags)) {
		_filelayout_free_lseg(fl);
		return NULL;
	}
	return &fl->generic_hdr;
}

/*
 * filelayout_pg_test(). Called by nfs_can_coalesce_requests()
 *
 * Return 0 if @req cannot be coalesced into @pgio, otherwise return the number
 * of bytes (maximum @req->wb_bytes) that can be coalesced.
 */
static size_t
filelayout_pg_test(struct nfs_pageio_descriptor *pgio, struct nfs_page *prev,
		   struct nfs_page *req)
{
	unsigned int size;
	u64 p_stripe, r_stripe;
	u32 stripe_offset;
	u64 segment_offset = pgio->pg_lseg->pls_range.offset;
	u32 stripe_unit = FILELAYOUT_LSEG(pgio->pg_lseg)->stripe_unit;

	/* calls nfs_generic_pg_test */
	size = pnfs_generic_pg_test(pgio, prev, req);
	if (!size)
		return 0;

	/* see if req and prev are in the same stripe */
	if (prev) {
		p_stripe = (u64)req_offset(prev) - segment_offset;
		r_stripe = (u64)req_offset(req) - segment_offset;
		do_div(p_stripe, stripe_unit);
		do_div(r_stripe, stripe_unit);

		if (p_stripe != r_stripe)
			return 0;
	}

	/* calculate remaining bytes in the current stripe */
	div_u64_rem((u64)req_offset(req) - segment_offset,
			stripe_unit,
			&stripe_offset);
	WARN_ON_ONCE(stripe_offset > stripe_unit);
	if (stripe_offset >= stripe_unit)
		return 0;
	return min(stripe_unit - (unsigned int)stripe_offset, size);
}

static struct pnfs_layout_segment *
fl_pnfs_update_layout(struct inode *ino,
		      struct nfs_open_context *ctx,
		      loff_t pos,
		      u64 count,
		      enum pnfs_iomode iomode,
		      bool strict_iomode,
		      gfp_t gfp_flags)
{
	struct pnfs_layout_segment *lseg = NULL;
	struct pnfs_layout_hdr *lo;
	struct nfs4_filelayout_segment *fl;
	int status;

	lseg = pnfs_update_layout(ino, ctx, pos, count, iomode, strict_iomode,
				  gfp_flags);
	if (IS_ERR_OR_NULL(lseg))
		goto out;

	lo = NFS_I(ino)->layout;
	fl = FILELAYOUT_LSEG(lseg);

	status = filelayout_check_deviceid(lo, fl, gfp_flags);
	if (status) {
		pnfs_put_lseg(lseg);
		lseg = NULL;
	}
out:
	return lseg;
}

static void
filelayout_pg_init_read(struct nfs_pageio_descriptor *pgio,
			struct nfs_page *req)
{
	pnfs_generic_pg_check_layout(pgio);
	if (!pgio->pg_lseg) {
		pgio->pg_lseg = fl_pnfs_update_layout(pgio->pg_inode,
						      nfs_req_openctx(req),
						      0,
						      NFS4_MAX_UINT64,
						      IOMODE_READ,
						      false,
						      GFP_KERNEL);
		if (IS_ERR(pgio->pg_lseg)) {
			pgio->pg_error = PTR_ERR(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			return;
		}
	}
	/* If no lseg, fall back to read through mds */
	if (pgio->pg_lseg == NULL)
		nfs_pageio_reset_read_mds(pgio);
}

static void
filelayout_pg_init_write(struct nfs_pageio_descriptor *pgio,
			 struct nfs_page *req)
{
	pnfs_generic_pg_check_layout(pgio);
	if (!pgio->pg_lseg) {
		pgio->pg_lseg = fl_pnfs_update_layout(pgio->pg_inode,
						      nfs_req_openctx(req),
						      0,
						      NFS4_MAX_UINT64,
						      IOMODE_RW,
						      false,
						      GFP_NOFS);
		if (IS_ERR(pgio->pg_lseg)) {
			pgio->pg_error = PTR_ERR(pgio->pg_lseg);
			pgio->pg_lseg = NULL;
			return;
		}
	}

	/* If no lseg, fall back to write through mds */
	if (pgio->pg_lseg == NULL)
		nfs_pageio_reset_write_mds(pgio);
}

static const struct nfs_pageio_ops filelayout_pg_read_ops = {
	.pg_init = filelayout_pg_init_read,
	.pg_test = filelayout_pg_test,
	.pg_doio = pnfs_generic_pg_readpages,
	.pg_cleanup = pnfs_generic_pg_cleanup,
};

static const struct nfs_pageio_ops filelayout_pg_write_ops = {
	.pg_init = filelayout_pg_init_write,
	.pg_test = filelayout_pg_test,
	.pg_doio = pnfs_generic_pg_writepages,
	.pg_cleanup = pnfs_generic_pg_cleanup,
};

static u32 select_bucket_index(struct nfs4_filelayout_segment *fl, u32 j)
{
	if (fl->stripe_type == STRIPE_SPARSE)
		return nfs4_fl_calc_ds_index(&fl->generic_hdr, j);
	else
		return j;
}

static void
filelayout_mark_request_commit(struct nfs_page *req,
			       struct pnfs_layout_segment *lseg,
			       struct nfs_commit_info *cinfo,
			       u32 ds_commit_idx)

{
	struct nfs4_filelayout_segment *fl = FILELAYOUT_LSEG(lseg);
	u32 i, j;

	if (fl->commit_through_mds) {
		nfs_request_add_commit_list(req, cinfo);
	} else {
		/* Note that we are calling nfs4_fl_calc_j_index on each page
		 * that ends up being committed to a data server.  An attractive
		 * alternative is to add a field to nfs_write_data and nfs_page
		 * to store the value calculated in filelayout_write_pagelist
		 * and just use that here.
		 */
		j = nfs4_fl_calc_j_index(lseg, req_offset(req));
		i = select_bucket_index(fl, j);
		pnfs_layout_mark_request_commit(req, lseg, cinfo, i);
	}
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

static int filelayout_initiate_commit(struct nfs_commit_data *data, int how)
{
	struct pnfs_layout_segment *lseg = data->lseg;
	struct nfs4_pnfs_ds *ds;
	struct rpc_clnt *ds_clnt;
	u32 idx;
	struct nfs_fh *fh;

	idx = calc_ds_index_from_commit(lseg, data->ds_commit_index);
	ds = nfs4_fl_prepare_ds(lseg, idx);
	if (!ds)
		goto out_err;

	ds_clnt = nfs4_find_or_create_ds_client(ds->ds_clp, data->inode);
	if (IS_ERR(ds_clnt))
		goto out_err;

	dprintk("%s ino %lu, how %d cl_count %d\n", __func__,
		data->inode->i_ino, how, refcount_read(&ds->ds_clp->cl_count));
	data->commit_done_cb = filelayout_commit_done_cb;
	refcount_inc(&ds->ds_clp->cl_count);
	data->ds_clp = ds->ds_clp;
	fh = select_ds_fh_from_commit(lseg, data->ds_commit_index);
	if (fh)
		data->args.fh = fh;
	return nfs_initiate_commit(ds_clnt, data, NFS_PROTO(data->inode),
				   &filelayout_commit_call_ops, how,
				   RPC_TASK_SOFTCONN);
out_err:
	pnfs_generic_prepare_to_resend_writes(data);
	pnfs_generic_commit_release(data);
	return -EAGAIN;
}

static int
filelayout_commit_pagelist(struct inode *inode, struct list_head *mds_pages,
			   int how, struct nfs_commit_info *cinfo)
{
	return pnfs_generic_commit_pagelist(inode, mds_pages, how, cinfo,
					    filelayout_initiate_commit);
}

static struct nfs4_deviceid_node *
filelayout_alloc_deviceid_node(struct nfs_server *server,
		struct pnfs_device *pdev, gfp_t gfp_flags)
{
	struct nfs4_file_layout_dsaddr *dsaddr;

	dsaddr = nfs4_fl_alloc_deviceid_node(server, pdev, gfp_flags);
	if (!dsaddr)
		return NULL;
	return &dsaddr->id_node;
}

static void
filelayout_free_deviceid_node(struct nfs4_deviceid_node *d)
{
	nfs4_fl_free_deviceid(container_of(d, struct nfs4_file_layout_dsaddr, id_node));
}

static struct pnfs_layout_hdr *
filelayout_alloc_layout_hdr(struct inode *inode, gfp_t gfp_flags)
{
	struct nfs4_filelayout *flo;

	flo = kzalloc(sizeof(*flo), gfp_flags);
	if (flo == NULL)
		return NULL;
	pnfs_init_ds_commit_info(&flo->commit_info);
	flo->commit_info.ops = &filelayout_commit_ops;
	return &flo->generic_hdr;
}

static void
filelayout_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	kfree_rcu(FILELAYOUT_FROM_HDR(lo), generic_hdr.plh_rcu);
}

static struct pnfs_ds_commit_info *
filelayout_get_ds_info(struct inode *inode)
{
	struct pnfs_layout_hdr *layout = NFS_I(inode)->layout;

	if (layout == NULL)
		return NULL;
	else
		return &FILELAYOUT_FROM_HDR(layout)->commit_info;
}

static void
filelayout_setup_ds_info(struct pnfs_ds_commit_info *fl_cinfo,
		struct pnfs_layout_segment *lseg)
{
	struct nfs4_filelayout_segment *fl = FILELAYOUT_LSEG(lseg);
	struct inode *inode = lseg->pls_layout->plh_inode;
	struct pnfs_commit_array *array, *new;
	unsigned int size = (fl->stripe_type == STRIPE_SPARSE) ?
		fl->dsaddr->ds_num : fl->dsaddr->stripe_count;

	new = pnfs_alloc_commit_array(size, GFP_NOIO);
	if (new) {
		spin_lock(&inode->i_lock);
		array = pnfs_add_commit_array(fl_cinfo, new, lseg);
		spin_unlock(&inode->i_lock);
		if (array != new)
			pnfs_free_commit_array(new);
	}
}

static void
filelayout_release_ds_info(struct pnfs_ds_commit_info *fl_cinfo,
		struct inode *inode)
{
	spin_lock(&inode->i_lock);
	pnfs_generic_ds_cinfo_destroy(fl_cinfo);
	spin_unlock(&inode->i_lock);
}

static const struct pnfs_commit_ops filelayout_commit_ops = {
	.setup_ds_info		= filelayout_setup_ds_info,
	.release_ds_info	= filelayout_release_ds_info,
	.mark_request_commit	= filelayout_mark_request_commit,
	.clear_request_commit	= pnfs_generic_clear_request_commit,
	.scan_commit_lists	= pnfs_generic_scan_commit_lists,
	.recover_commit_reqs	= pnfs_generic_recover_commit_reqs,
	.search_commit_reqs	= pnfs_generic_search_commit_reqs,
	.commit_pagelist	= filelayout_commit_pagelist,
};

static struct pnfs_layoutdriver_type filelayout_type = {
	.id			= LAYOUT_NFSV4_1_FILES,
	.name			= "LAYOUT_NFSV4_1_FILES",
	.owner			= THIS_MODULE,
	.flags			= PNFS_LAYOUTGET_ON_OPEN,
	.max_layoutget_response	= 4096, /* 1 page or so... */
	.alloc_layout_hdr	= filelayout_alloc_layout_hdr,
	.free_layout_hdr	= filelayout_free_layout_hdr,
	.alloc_lseg		= filelayout_alloc_lseg,
	.free_lseg		= filelayout_free_lseg,
	.pg_read_ops		= &filelayout_pg_read_ops,
	.pg_write_ops		= &filelayout_pg_write_ops,
	.get_ds_info		= &filelayout_get_ds_info,
	.read_pagelist		= filelayout_read_pagelist,
	.write_pagelist		= filelayout_write_pagelist,
	.alloc_deviceid_node	= filelayout_alloc_deviceid_node,
	.free_deviceid_node	= filelayout_free_deviceid_node,
	.sync			= pnfs_nfs_generic_sync,
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

MODULE_ALIAS("nfs-layouttype4-1");

module_init(nfs4filelayout_init);
module_exit(nfs4filelayout_exit);
