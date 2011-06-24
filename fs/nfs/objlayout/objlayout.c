/*
 *  pNFS Objects layout driver high level definitions
 *
 *  Copyright (C) 2007 Panasas Inc. [year of first publication]
 *  All rights reserved.
 *
 *  Benny Halevy <bhalevy@panasas.com>
 *  Boaz Harrosh <bharrosh@panasas.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  See the file COPYING included with this distribution for more details.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the Panasas company nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <scsi/osd_initiator.h>
#include "objlayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD
/*
 * Create a objlayout layout structure for the given inode and return it.
 */
struct pnfs_layout_hdr *
objlayout_alloc_layout_hdr(struct inode *inode, gfp_t gfp_flags)
{
	struct objlayout *objlay;

	objlay = kzalloc(sizeof(struct objlayout), gfp_flags);
	if (objlay) {
		spin_lock_init(&objlay->lock);
		INIT_LIST_HEAD(&objlay->err_list);
	}
	dprintk("%s: Return %p\n", __func__, objlay);
	return &objlay->pnfs_layout;
}

/*
 * Free an objlayout layout structure
 */
void
objlayout_free_layout_hdr(struct pnfs_layout_hdr *lo)
{
	struct objlayout *objlay = OBJLAYOUT(lo);

	dprintk("%s: objlay %p\n", __func__, objlay);

	WARN_ON(!list_empty(&objlay->err_list));
	kfree(objlay);
}

/*
 * Unmarshall layout and store it in pnfslay.
 */
struct pnfs_layout_segment *
objlayout_alloc_lseg(struct pnfs_layout_hdr *pnfslay,
		     struct nfs4_layoutget_res *lgr,
		     gfp_t gfp_flags)
{
	int status = -ENOMEM;
	struct xdr_stream stream;
	struct xdr_buf buf = {
		.pages =  lgr->layoutp->pages,
		.page_len =  lgr->layoutp->len,
		.buflen =  lgr->layoutp->len,
		.len = lgr->layoutp->len,
	};
	struct page *scratch;
	struct pnfs_layout_segment *lseg;

	dprintk("%s: Begin pnfslay %p\n", __func__, pnfslay);

	scratch = alloc_page(gfp_flags);
	if (!scratch)
		goto err_nofree;

	xdr_init_decode(&stream, &buf, NULL);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	status = objio_alloc_lseg(&lseg, pnfslay, &lgr->range, &stream, gfp_flags);
	if (unlikely(status)) {
		dprintk("%s: objio_alloc_lseg Return err %d\n", __func__,
			status);
		goto err;
	}

	__free_page(scratch);

	dprintk("%s: Return %p\n", __func__, lseg);
	return lseg;

err:
	__free_page(scratch);
err_nofree:
	dprintk("%s: Err Return=>%d\n", __func__, status);
	return ERR_PTR(status);
}

/*
 * Free a layout segement
 */
void
objlayout_free_lseg(struct pnfs_layout_segment *lseg)
{
	dprintk("%s: freeing layout segment %p\n", __func__, lseg);

	if (unlikely(!lseg))
		return;

	objio_free_lseg(lseg);
}

/*
 * I/O Operations
 */
static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end : NFS4_MAX_UINT64;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	BUG_ON(!len);
	end = start + len;
	return end > start ? end - 1 : NFS4_MAX_UINT64;
}

static struct objlayout_io_state *
objlayout_alloc_io_state(struct pnfs_layout_hdr *pnfs_layout_type,
			struct page **pages,
			unsigned pgbase,
			loff_t offset,
			size_t count,
			struct pnfs_layout_segment *lseg,
			void *rpcdata,
			gfp_t gfp_flags)
{
	struct objlayout_io_state *state;
	u64 lseg_end_offset;

	dprintk("%s: allocating io_state\n", __func__);
	if (objio_alloc_io_state(lseg, &state, gfp_flags))
		return NULL;

	BUG_ON(offset < lseg->pls_range.offset);
	lseg_end_offset = end_offset(lseg->pls_range.offset,
				     lseg->pls_range.length);
	BUG_ON(offset >= lseg_end_offset);
	if (offset + count > lseg_end_offset) {
		count = lseg->pls_range.length -
				(offset - lseg->pls_range.offset);
		dprintk("%s: truncated count %Zd\n", __func__, count);
	}

	if (pgbase > PAGE_SIZE) {
		pages += pgbase >> PAGE_SHIFT;
		pgbase &= ~PAGE_MASK;
	}

	INIT_LIST_HEAD(&state->err_list);
	state->lseg = lseg;
	state->rpcdata = rpcdata;
	state->pages = pages;
	state->pgbase = pgbase;
	state->nr_pages = (pgbase + count + PAGE_SIZE - 1) >> PAGE_SHIFT;
	state->offset = offset;
	state->count = count;
	state->sync = 0;

	return state;
}

static void
objlayout_free_io_state(struct objlayout_io_state *state)
{
	dprintk("%s: freeing io_state\n", __func__);
	if (unlikely(!state))
		return;

	objio_free_io_state(state);
}

/*
 * I/O done common code
 */
static void
objlayout_iodone(struct objlayout_io_state *state)
{
	dprintk("%s: state %p status\n", __func__, state);

	if (likely(state->status >= 0)) {
		objlayout_free_io_state(state);
	} else {
		struct objlayout *objlay = OBJLAYOUT(state->lseg->pls_layout);

		spin_lock(&objlay->lock);
		objlay->delta_space_valid = OBJ_DSU_INVALID;
		list_add(&objlay->err_list, &state->err_list);
		spin_unlock(&objlay->lock);
	}
}

/*
 * objlayout_io_set_result - Set an osd_error code on a specific osd comp.
 *
 * The @index component IO failed (error returned from target). Register
 * the error for later reporting at layout-return.
 */
void
objlayout_io_set_result(struct objlayout_io_state *state, unsigned index,
			struct pnfs_osd_objid *pooid, int osd_error,
			u64 offset, u64 length, bool is_write)
{
	struct pnfs_osd_ioerr *ioerr = &state->ioerrs[index];

	BUG_ON(index >= state->num_comps);
	if (osd_error) {
		ioerr->oer_component = *pooid;
		ioerr->oer_comp_offset = offset;
		ioerr->oer_comp_length = length;
		ioerr->oer_iswrite = is_write;
		ioerr->oer_errno = osd_error;

		dprintk("%s: err[%d]: errno=%d is_write=%d dev(%llx:%llx) "
			"par=0x%llx obj=0x%llx offset=0x%llx length=0x%llx\n",
			__func__, index, ioerr->oer_errno,
			ioerr->oer_iswrite,
			_DEVID_LO(&ioerr->oer_component.oid_device_id),
			_DEVID_HI(&ioerr->oer_component.oid_device_id),
			ioerr->oer_component.oid_partition_id,
			ioerr->oer_component.oid_object_id,
			ioerr->oer_comp_offset,
			ioerr->oer_comp_length);
	} else {
		/* User need not call if no error is reported */
		ioerr->oer_errno = 0;
	}
}

/* Function scheduled on rpc workqueue to call ->nfs_readlist_complete().
 * This is because the osd completion is called with ints-off from
 * the block layer
 */
static void _rpc_read_complete(struct work_struct *work)
{
	struct rpc_task *task;
	struct nfs_read_data *rdata;

	dprintk("%s enter\n", __func__);
	task = container_of(work, struct rpc_task, u.tk_work);
	rdata = container_of(task, struct nfs_read_data, task);

	pnfs_ld_read_done(rdata);
}

void
objlayout_read_done(struct objlayout_io_state *state, ssize_t status, bool sync)
{
	int eof = state->eof;
	struct nfs_read_data *rdata;

	state->status = status;
	dprintk("%s: Begin status=%zd eof=%d\n", __func__, status, eof);
	rdata = state->rpcdata;
	rdata->task.tk_status = status;
	if (status >= 0) {
		rdata->res.count = status;
		rdata->res.eof = eof;
	}
	objlayout_iodone(state);
	/* must not use state after this point */

	if (sync)
		pnfs_ld_read_done(rdata);
	else {
		INIT_WORK(&rdata->task.u.tk_work, _rpc_read_complete);
		schedule_work(&rdata->task.u.tk_work);
	}
}

/*
 * Perform sync or async reads.
 */
enum pnfs_try_status
objlayout_read_pagelist(struct nfs_read_data *rdata)
{
	loff_t offset = rdata->args.offset;
	size_t count = rdata->args.count;
	struct objlayout_io_state *state;
	ssize_t status = 0;
	loff_t eof;

	dprintk("%s: Begin inode %p offset %llu count %d\n",
		__func__, rdata->inode, offset, (int)count);

	eof = i_size_read(rdata->inode);
	if (unlikely(offset + count > eof)) {
		if (offset >= eof) {
			status = 0;
			rdata->res.count = 0;
			rdata->res.eof = 1;
			goto out;
		}
		count = eof - offset;
	}

	state = objlayout_alloc_io_state(NFS_I(rdata->inode)->layout,
					 rdata->args.pages, rdata->args.pgbase,
					 offset, count,
					 rdata->lseg, rdata,
					 GFP_KERNEL);
	if (unlikely(!state)) {
		status = -ENOMEM;
		goto out;
	}

	state->eof = state->offset + state->count >= eof;

	status = objio_read_pagelist(state);
 out:
	dprintk("%s: Return status %Zd\n", __func__, status);
	rdata->pnfs_error = status;
	return PNFS_ATTEMPTED;
}

/* Function scheduled on rpc workqueue to call ->nfs_writelist_complete().
 * This is because the osd completion is called with ints-off from
 * the block layer
 */
static void _rpc_write_complete(struct work_struct *work)
{
	struct rpc_task *task;
	struct nfs_write_data *wdata;

	dprintk("%s enter\n", __func__);
	task = container_of(work, struct rpc_task, u.tk_work);
	wdata = container_of(task, struct nfs_write_data, task);

	pnfs_ld_write_done(wdata);
}

void
objlayout_write_done(struct objlayout_io_state *state, ssize_t status,
		     bool sync)
{
	struct nfs_write_data *wdata;

	dprintk("%s: Begin\n", __func__);
	wdata = state->rpcdata;
	state->status = status;
	wdata->task.tk_status = status;
	if (status >= 0) {
		wdata->res.count = status;
		wdata->verf.committed = state->committed;
		dprintk("%s: Return status %d committed %d\n",
			__func__, wdata->task.tk_status,
			wdata->verf.committed);
	} else
		dprintk("%s: Return status %d\n",
			__func__, wdata->task.tk_status);
	objlayout_iodone(state);
	/* must not use state after this point */

	if (sync)
		pnfs_ld_write_done(wdata);
	else {
		INIT_WORK(&wdata->task.u.tk_work, _rpc_write_complete);
		schedule_work(&wdata->task.u.tk_work);
	}
}

/*
 * Perform sync or async writes.
 */
enum pnfs_try_status
objlayout_write_pagelist(struct nfs_write_data *wdata,
			 int how)
{
	struct objlayout_io_state *state;
	ssize_t status;

	dprintk("%s: Begin inode %p offset %llu count %u\n",
		__func__, wdata->inode, wdata->args.offset, wdata->args.count);

	state = objlayout_alloc_io_state(NFS_I(wdata->inode)->layout,
					 wdata->args.pages,
					 wdata->args.pgbase,
					 wdata->args.offset,
					 wdata->args.count,
					 wdata->lseg, wdata,
					 GFP_NOFS);
	if (unlikely(!state)) {
		status = -ENOMEM;
		goto out;
	}

	state->sync = how & FLUSH_SYNC;

	status = objio_write_pagelist(state, how & FLUSH_STABLE);
 out:
	dprintk("%s: Return status %Zd\n", __func__, status);
	wdata->pnfs_error = status;
	return PNFS_ATTEMPTED;
}

void
objlayout_encode_layoutcommit(struct pnfs_layout_hdr *pnfslay,
			      struct xdr_stream *xdr,
			      const struct nfs4_layoutcommit_args *args)
{
	struct objlayout *objlay = OBJLAYOUT(pnfslay);
	struct pnfs_osd_layoutupdate lou;
	__be32 *start;

	dprintk("%s: Begin\n", __func__);

	spin_lock(&objlay->lock);
	lou.dsu_valid = (objlay->delta_space_valid == OBJ_DSU_VALID);
	lou.dsu_delta = objlay->delta_space_used;
	objlay->delta_space_used = 0;
	objlay->delta_space_valid = OBJ_DSU_INIT;
	lou.olu_ioerr_flag = !list_empty(&objlay->err_list);
	spin_unlock(&objlay->lock);

	start = xdr_reserve_space(xdr, 4);

	BUG_ON(pnfs_osd_xdr_encode_layoutupdate(xdr, &lou));

	*start = cpu_to_be32((xdr->p - start - 1) * 4);

	dprintk("%s: Return delta_space_used %lld err %d\n", __func__,
		lou.dsu_delta, lou.olu_ioerr_flag);
}

static int
err_prio(u32 oer_errno)
{
	switch (oer_errno) {
	case 0:
		return 0;

	case PNFS_OSD_ERR_RESOURCE:
		return OSD_ERR_PRI_RESOURCE;
	case PNFS_OSD_ERR_BAD_CRED:
		return OSD_ERR_PRI_BAD_CRED;
	case PNFS_OSD_ERR_NO_ACCESS:
		return OSD_ERR_PRI_NO_ACCESS;
	case PNFS_OSD_ERR_UNREACHABLE:
		return OSD_ERR_PRI_UNREACHABLE;
	case PNFS_OSD_ERR_NOT_FOUND:
		return OSD_ERR_PRI_NOT_FOUND;
	case PNFS_OSD_ERR_NO_SPACE:
		return OSD_ERR_PRI_NO_SPACE;
	default:
		WARN_ON(1);
		/* fallthrough */
	case PNFS_OSD_ERR_EIO:
		return OSD_ERR_PRI_EIO;
	}
}

static void
merge_ioerr(struct pnfs_osd_ioerr *dest_err,
	    const struct pnfs_osd_ioerr *src_err)
{
	u64 dest_end, src_end;

	if (!dest_err->oer_errno) {
		*dest_err = *src_err;
		/* accumulated device must be blank */
		memset(&dest_err->oer_component.oid_device_id, 0,
			sizeof(dest_err->oer_component.oid_device_id));

		return;
	}

	if (dest_err->oer_component.oid_partition_id !=
				src_err->oer_component.oid_partition_id)
		dest_err->oer_component.oid_partition_id = 0;

	if (dest_err->oer_component.oid_object_id !=
				src_err->oer_component.oid_object_id)
		dest_err->oer_component.oid_object_id = 0;

	if (dest_err->oer_comp_offset > src_err->oer_comp_offset)
		dest_err->oer_comp_offset = src_err->oer_comp_offset;

	dest_end = end_offset(dest_err->oer_comp_offset,
			      dest_err->oer_comp_length);
	src_end =  end_offset(src_err->oer_comp_offset,
			      src_err->oer_comp_length);
	if (dest_end < src_end)
		dest_end = src_end;

	dest_err->oer_comp_length = dest_end - dest_err->oer_comp_offset;

	if ((src_err->oer_iswrite == dest_err->oer_iswrite) &&
	    (err_prio(src_err->oer_errno) > err_prio(dest_err->oer_errno))) {
			dest_err->oer_errno = src_err->oer_errno;
	} else if (src_err->oer_iswrite) {
		dest_err->oer_iswrite = true;
		dest_err->oer_errno = src_err->oer_errno;
	}
}

static void
encode_accumulated_error(struct objlayout *objlay, __be32 *p)
{
	struct objlayout_io_state *state, *tmp;
	struct pnfs_osd_ioerr accumulated_err = {.oer_errno = 0};

	list_for_each_entry_safe(state, tmp, &objlay->err_list, err_list) {
		unsigned i;

		for (i = 0; i < state->num_comps; i++) {
			struct pnfs_osd_ioerr *ioerr = &state->ioerrs[i];

			if (!ioerr->oer_errno)
				continue;

			printk(KERN_ERR "%s: err[%d]: errno=%d is_write=%d "
				"dev(%llx:%llx) par=0x%llx obj=0x%llx "
				"offset=0x%llx length=0x%llx\n",
				__func__, i, ioerr->oer_errno,
				ioerr->oer_iswrite,
				_DEVID_LO(&ioerr->oer_component.oid_device_id),
				_DEVID_HI(&ioerr->oer_component.oid_device_id),
				ioerr->oer_component.oid_partition_id,
				ioerr->oer_component.oid_object_id,
				ioerr->oer_comp_offset,
				ioerr->oer_comp_length);

			merge_ioerr(&accumulated_err, ioerr);
		}
		list_del(&state->err_list);
		objlayout_free_io_state(state);
	}

	pnfs_osd_xdr_encode_ioerr(p, &accumulated_err);
}

void
objlayout_encode_layoutreturn(struct pnfs_layout_hdr *pnfslay,
			      struct xdr_stream *xdr,
			      const struct nfs4_layoutreturn_args *args)
{
	struct objlayout *objlay = OBJLAYOUT(pnfslay);
	struct objlayout_io_state *state, *tmp;
	__be32 *start;

	dprintk("%s: Begin\n", __func__);
	start = xdr_reserve_space(xdr, 4);
	BUG_ON(!start);

	spin_lock(&objlay->lock);

	list_for_each_entry_safe(state, tmp, &objlay->err_list, err_list) {
		__be32 *last_xdr = NULL, *p;
		unsigned i;
		int res = 0;

		for (i = 0; i < state->num_comps; i++) {
			struct pnfs_osd_ioerr *ioerr = &state->ioerrs[i];

			if (!ioerr->oer_errno)
				continue;

			dprintk("%s: err[%d]: errno=%d is_write=%d "
				"dev(%llx:%llx) par=0x%llx obj=0x%llx "
				"offset=0x%llx length=0x%llx\n",
				__func__, i, ioerr->oer_errno,
				ioerr->oer_iswrite,
				_DEVID_LO(&ioerr->oer_component.oid_device_id),
				_DEVID_HI(&ioerr->oer_component.oid_device_id),
				ioerr->oer_component.oid_partition_id,
				ioerr->oer_component.oid_object_id,
				ioerr->oer_comp_offset,
				ioerr->oer_comp_length);

			p = pnfs_osd_xdr_ioerr_reserve_space(xdr);
			if (unlikely(!p)) {
				res = -E2BIG;
				break; /* accumulated_error */
			}

			last_xdr = p;
			pnfs_osd_xdr_encode_ioerr(p, &state->ioerrs[i]);
		}

		/* TODO: use xdr_write_pages */
		if (unlikely(res)) {
			/* no space for even one error descriptor */
			BUG_ON(!last_xdr);

			/* we've encountered a situation with lots and lots of
			 * errors and no space to encode them all. Use the last
			 * available slot to report the union of all the
			 * remaining errors.
			 */
			encode_accumulated_error(objlay, last_xdr);
			goto loop_done;
		}
		list_del(&state->err_list);
		objlayout_free_io_state(state);
	}
loop_done:
	spin_unlock(&objlay->lock);

	*start = cpu_to_be32((xdr->p - start - 1) * 4);
	dprintk("%s: Return\n", __func__);
}


/*
 * Get Device Info API for io engines
 */
struct objlayout_deviceinfo {
	struct page *page;
	struct pnfs_osd_deviceaddr da; /* This must be last */
};

/* Initialize and call nfs_getdeviceinfo, then decode and return a
 * "struct pnfs_osd_deviceaddr *" Eventually objlayout_put_deviceinfo()
 * should be called.
 */
int objlayout_get_deviceinfo(struct pnfs_layout_hdr *pnfslay,
	struct nfs4_deviceid *d_id, struct pnfs_osd_deviceaddr **deviceaddr,
	gfp_t gfp_flags)
{
	struct objlayout_deviceinfo *odi;
	struct pnfs_device pd;
	struct super_block *sb;
	struct page *page, **pages;
	u32 *p;
	int err;

	page = alloc_page(gfp_flags);
	if (!page)
		return -ENOMEM;

	pages = &page;
	pd.pages = pages;

	memcpy(&pd.dev_id, d_id, sizeof(*d_id));
	pd.layout_type = LAYOUT_OSD2_OBJECTS;
	pd.pages = &page;
	pd.pgbase = 0;
	pd.pglen = PAGE_SIZE;
	pd.mincount = 0;

	sb = pnfslay->plh_inode->i_sb;
	err = nfs4_proc_getdeviceinfo(NFS_SERVER(pnfslay->plh_inode), &pd);
	dprintk("%s nfs_getdeviceinfo returned %d\n", __func__, err);
	if (err)
		goto err_out;

	p = page_address(page);
	odi = kzalloc(sizeof(*odi), gfp_flags);
	if (!odi) {
		err = -ENOMEM;
		goto err_out;
	}
	pnfs_osd_xdr_decode_deviceaddr(&odi->da, p);
	odi->page = page;
	*deviceaddr = &odi->da;
	return 0;

err_out:
	__free_page(page);
	return err;
}

void objlayout_put_deviceinfo(struct pnfs_osd_deviceaddr *deviceaddr)
{
	struct objlayout_deviceinfo *odi = container_of(deviceaddr,
						struct objlayout_deviceinfo,
						da);

	__free_page(odi->page);
	kfree(odi);
}
