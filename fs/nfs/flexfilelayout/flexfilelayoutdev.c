// SPDX-License-Identifier: GPL-2.0
/*
 * Device operations for the pnfs nfs4 file layout driver.
 *
 * Copyright (c) 2014, Primary Data, Inc. All rights reserved.
 *
 * Tao Peng <bergwolf@primarydata.com>
 */

#include <linux/nfs_fs.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/sunrpc/addr.h>

#include "../internal.h"
#include "../nfs4session.h"
#include "flexfilelayout.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS_LD

static unsigned int dataserver_timeo = NFS_DEF_TCP_TIMEO;
static unsigned int dataserver_retrans;

static bool ff_layout_has_available_ds(struct pnfs_layout_segment *lseg);

void nfs4_ff_layout_put_deviceid(struct nfs4_ff_layout_ds *mirror_ds)
{
	if (!IS_ERR_OR_NULL(mirror_ds))
		nfs4_put_deviceid_node(&mirror_ds->id_node);
}

void nfs4_ff_layout_free_deviceid(struct nfs4_ff_layout_ds *mirror_ds)
{
	nfs4_print_deviceid(&mirror_ds->id_node.deviceid);
	nfs4_pnfs_ds_put(mirror_ds->ds);
	kfree(mirror_ds->ds_versions);
	kfree_rcu(mirror_ds, id_node.rcu);
}

/* Decode opaque device data and construct new_ds using it */
struct nfs4_ff_layout_ds *
nfs4_ff_alloc_deviceid_node(struct nfs_server *server, struct pnfs_device *pdev,
			    gfp_t gfp_flags)
{
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch;
	struct list_head dsaddrs;
	struct nfs4_pnfs_ds_addr *da;
	struct nfs4_ff_layout_ds *new_ds = NULL;
	struct nfs4_ff_ds_version *ds_versions = NULL;
	u32 mp_count;
	u32 version_count;
	__be32 *p;
	int i, ret = -ENOMEM;

	/* set up xdr stream */
	scratch = alloc_page(gfp_flags);
	if (!scratch)
		goto out_err;

	new_ds = kzalloc(sizeof(struct nfs4_ff_layout_ds), gfp_flags);
	if (!new_ds)
		goto out_scratch;

	nfs4_init_deviceid_node(&new_ds->id_node,
				server,
				&pdev->dev_id);
	INIT_LIST_HEAD(&dsaddrs);

	xdr_init_decode_pages(&stream, &buf, pdev->pages, pdev->pglen);
	xdr_set_scratch_page(&stream, scratch);

	/* multipath count */
	p = xdr_inline_decode(&stream, 4);
	if (unlikely(!p))
		goto out_err_drain_dsaddrs;
	mp_count = be32_to_cpup(p);
	dprintk("%s: multipath ds count %d\n", __func__, mp_count);

	for (i = 0; i < mp_count; i++) {
		/* multipath ds */
		da = nfs4_decode_mp_ds_addr(server->nfs_client->cl_net,
					    &stream, gfp_flags);
		if (da)
			list_add_tail(&da->da_node, &dsaddrs);
	}
	if (list_empty(&dsaddrs)) {
		dprintk("%s: no suitable DS addresses found\n",
			__func__);
		ret = -ENOMEDIUM;
		goto out_err_drain_dsaddrs;
	}

	/* version count */
	p = xdr_inline_decode(&stream, 4);
	if (unlikely(!p))
		goto out_err_drain_dsaddrs;
	version_count = be32_to_cpup(p);
	dprintk("%s: version count %d\n", __func__, version_count);

	ds_versions = kcalloc(version_count,
			      sizeof(struct nfs4_ff_ds_version),
			      gfp_flags);
	if (!ds_versions)
		goto out_scratch;

	for (i = 0; i < version_count; i++) {
		/* 20 = version(4) + minor_version(4) + rsize(4) + wsize(4) +
		 * tightly_coupled(4) */
		p = xdr_inline_decode(&stream, 20);
		if (unlikely(!p))
			goto out_err_drain_dsaddrs;
		ds_versions[i].version = be32_to_cpup(p++);
		ds_versions[i].minor_version = be32_to_cpup(p++);
		ds_versions[i].rsize = nfs_block_size(be32_to_cpup(p++), NULL);
		ds_versions[i].wsize = nfs_block_size(be32_to_cpup(p++), NULL);
		ds_versions[i].tightly_coupled = be32_to_cpup(p);

		if (ds_versions[i].rsize > NFS_MAX_FILE_IO_SIZE)
			ds_versions[i].rsize = NFS_MAX_FILE_IO_SIZE;
		if (ds_versions[i].wsize > NFS_MAX_FILE_IO_SIZE)
			ds_versions[i].wsize = NFS_MAX_FILE_IO_SIZE;

		/*
		 * check for valid major/minor combination.
		 * currently we support dataserver which talk:
		 *   v3, v4.0, v4.1, v4.2
		 */
		if (!((ds_versions[i].version == 3 && ds_versions[i].minor_version == 0) ||
			(ds_versions[i].version == 4 && ds_versions[i].minor_version < 3))) {
			dprintk("%s: [%d] unsupported ds version %d-%d\n", __func__,
				i, ds_versions[i].version,
				ds_versions[i].minor_version);
			ret = -EPROTONOSUPPORT;
			goto out_err_drain_dsaddrs;
		}

		dprintk("%s: [%d] vers %u minor_ver %u rsize %u wsize %u coupled %d\n",
			__func__, i, ds_versions[i].version,
			ds_versions[i].minor_version,
			ds_versions[i].rsize,
			ds_versions[i].wsize,
			ds_versions[i].tightly_coupled);
	}

	new_ds->ds_versions = ds_versions;
	new_ds->ds_versions_cnt = version_count;

	new_ds->ds = nfs4_pnfs_ds_add(&dsaddrs, gfp_flags);
	if (!new_ds->ds)
		goto out_err_drain_dsaddrs;

	/* If DS was already in cache, free ds addrs */
	while (!list_empty(&dsaddrs)) {
		da = list_first_entry(&dsaddrs,
				      struct nfs4_pnfs_ds_addr,
				      da_node);
		list_del_init(&da->da_node);
		kfree(da->da_remotestr);
		kfree(da);
	}

	__free_page(scratch);
	return new_ds;

out_err_drain_dsaddrs:
	while (!list_empty(&dsaddrs)) {
		da = list_first_entry(&dsaddrs, struct nfs4_pnfs_ds_addr,
				      da_node);
		list_del_init(&da->da_node);
		kfree(da->da_remotestr);
		kfree(da);
	}

	kfree(ds_versions);
out_scratch:
	__free_page(scratch);
out_err:
	kfree(new_ds);

	dprintk("%s ERROR: returning %d\n", __func__, ret);
	return NULL;
}

static void extend_ds_error(struct nfs4_ff_layout_ds_err *err,
			    u64 offset, u64 length)
{
	u64 end;

	end = max_t(u64, pnfs_end_offset(err->offset, err->length),
		    pnfs_end_offset(offset, length));
	err->offset = min_t(u64, err->offset, offset);
	err->length = end - err->offset;
}

static int
ff_ds_error_match(const struct nfs4_ff_layout_ds_err *e1,
		const struct nfs4_ff_layout_ds_err *e2)
{
	int ret;

	if (e1->opnum != e2->opnum)
		return e1->opnum < e2->opnum ? -1 : 1;
	if (e1->status != e2->status)
		return e1->status < e2->status ? -1 : 1;
	ret = memcmp(e1->stateid.data, e2->stateid.data,
			sizeof(e1->stateid.data));
	if (ret != 0)
		return ret;
	ret = memcmp(&e1->deviceid, &e2->deviceid, sizeof(e1->deviceid));
	if (ret != 0)
		return ret;
	if (pnfs_end_offset(e1->offset, e1->length) < e2->offset)
		return -1;
	if (e1->offset > pnfs_end_offset(e2->offset, e2->length))
		return 1;
	/* If ranges overlap or are contiguous, they are the same */
	return 0;
}

static void
ff_layout_add_ds_error_locked(struct nfs4_flexfile_layout *flo,
			      struct nfs4_ff_layout_ds_err *dserr)
{
	struct nfs4_ff_layout_ds_err *err, *tmp;
	struct list_head *head = &flo->error_list;
	int match;

	/* Do insertion sort w/ merges */
	list_for_each_entry_safe(err, tmp, &flo->error_list, list) {
		match = ff_ds_error_match(err, dserr);
		if (match < 0)
			continue;
		if (match > 0) {
			/* Add entry "dserr" _before_ entry "err" */
			head = &err->list;
			break;
		}
		/* Entries match, so merge "err" into "dserr" */
		extend_ds_error(dserr, err->offset, err->length);
		list_replace(&err->list, &dserr->list);
		kfree(err);
		return;
	}

	list_add_tail(&dserr->list, head);
}

int ff_layout_track_ds_error(struct nfs4_flexfile_layout *flo,
			     struct nfs4_ff_layout_mirror *mirror, u64 offset,
			     u64 length, int status, enum nfs_opnum4 opnum,
			     gfp_t gfp_flags)
{
	struct nfs4_ff_layout_ds_err *dserr;

	if (status == 0)
		return 0;

	if (IS_ERR_OR_NULL(mirror->mirror_ds))
		return -EINVAL;

	dserr = kmalloc(sizeof(*dserr), gfp_flags);
	if (!dserr)
		return -ENOMEM;

	INIT_LIST_HEAD(&dserr->list);
	dserr->offset = offset;
	dserr->length = length;
	dserr->status = status;
	dserr->opnum = opnum;
	nfs4_stateid_copy(&dserr->stateid, &mirror->stateid);
	memcpy(&dserr->deviceid, &mirror->mirror_ds->id_node.deviceid,
	       NFS4_DEVICEID4_SIZE);

	spin_lock(&flo->generic_hdr.plh_inode->i_lock);
	ff_layout_add_ds_error_locked(flo, dserr);
	spin_unlock(&flo->generic_hdr.plh_inode->i_lock);
	return 0;
}

static const struct cred *
ff_layout_get_mirror_cred(struct nfs4_ff_layout_mirror *mirror, u32 iomode)
{
	const struct cred *cred, __rcu **pcred;

	if (iomode == IOMODE_READ)
		pcred = &mirror->ro_cred;
	else
		pcred = &mirror->rw_cred;

	rcu_read_lock();
	do {
		cred = rcu_dereference(*pcred);
		if (!cred)
			break;

		cred = get_cred_rcu(cred);
	} while(!cred);
	rcu_read_unlock();
	return cred;
}

struct nfs_fh *
nfs4_ff_layout_select_ds_fh(struct nfs4_ff_layout_mirror *mirror)
{
	/* FIXME: For now assume there is only 1 version available for the DS */
	return &mirror->fh_versions[0];
}

void
nfs4_ff_layout_select_ds_stateid(const struct nfs4_ff_layout_mirror *mirror,
		nfs4_stateid *stateid)
{
	if (nfs4_ff_layout_ds_version(mirror) == 4)
		nfs4_stateid_copy(stateid, &mirror->stateid);
}

static bool
ff_layout_init_mirror_ds(struct pnfs_layout_hdr *lo,
			 struct nfs4_ff_layout_mirror *mirror)
{
	if (mirror == NULL)
		goto outerr;
	if (mirror->mirror_ds == NULL) {
		struct nfs4_deviceid_node *node;
		struct nfs4_ff_layout_ds *mirror_ds = ERR_PTR(-ENODEV);

		node = nfs4_find_get_deviceid(NFS_SERVER(lo->plh_inode),
				&mirror->devid, lo->plh_lc_cred,
				GFP_KERNEL);
		if (node)
			mirror_ds = FF_LAYOUT_MIRROR_DS(node);

		/* check for race with another call to this function */
		if (cmpxchg(&mirror->mirror_ds, NULL, mirror_ds) &&
		    mirror_ds != ERR_PTR(-ENODEV))
			nfs4_put_deviceid_node(node);
	}

	if (IS_ERR(mirror->mirror_ds))
		goto outerr;

	return true;
outerr:
	return false;
}

/**
 * nfs4_ff_layout_prepare_ds - prepare a DS connection for an RPC call
 * @lseg: the layout segment we're operating on
 * @mirror: layout mirror describing the DS to use
 * @fail_return: return layout on connect failure?
 *
 * Try to prepare a DS connection to accept an RPC call. This involves
 * selecting a mirror to use and connecting the client to it if it's not
 * already connected.
 *
 * Since we only need a single functioning mirror to satisfy a read, we don't
 * want to return the layout if there is one. For writes though, any down
 * mirror should result in a LAYOUTRETURN. @fail_return is how we distinguish
 * between the two cases.
 *
 * Returns a pointer to a connected DS object on success or NULL on failure.
 */
struct nfs4_pnfs_ds *
nfs4_ff_layout_prepare_ds(struct pnfs_layout_segment *lseg,
			  struct nfs4_ff_layout_mirror *mirror,
			  bool fail_return)
{
	struct nfs4_pnfs_ds *ds = NULL;
	struct inode *ino = lseg->pls_layout->plh_inode;
	struct nfs_server *s = NFS_SERVER(ino);
	unsigned int max_payload;
	int status;

	if (!ff_layout_init_mirror_ds(lseg->pls_layout, mirror))
		goto noconnect;

	ds = mirror->mirror_ds->ds;
	/* matching smp_wmb() in _nfs4_pnfs_v3/4_ds_connect */
	smp_rmb();
	if (ds->ds_clp)
		goto out;

	/* FIXME: For now we assume the server sent only one version of NFS
	 * to use for the DS.
	 */
	status = nfs4_pnfs_ds_connect(s, ds, &mirror->mirror_ds->id_node,
			     dataserver_timeo, dataserver_retrans,
			     mirror->mirror_ds->ds_versions[0].version,
			     mirror->mirror_ds->ds_versions[0].minor_version);

	/* connect success, check rsize/wsize limit */
	if (!status) {
		max_payload =
			nfs_block_size(rpc_max_payload(ds->ds_clp->cl_rpcclient),
				       NULL);
		if (mirror->mirror_ds->ds_versions[0].rsize > max_payload)
			mirror->mirror_ds->ds_versions[0].rsize = max_payload;
		if (mirror->mirror_ds->ds_versions[0].wsize > max_payload)
			mirror->mirror_ds->ds_versions[0].wsize = max_payload;
		goto out;
	}
noconnect:
	ff_layout_track_ds_error(FF_LAYOUT_FROM_HDR(lseg->pls_layout),
				 mirror, lseg->pls_range.offset,
				 lseg->pls_range.length, NFS4ERR_NXIO,
				 OP_ILLEGAL, GFP_NOIO);
	ff_layout_send_layouterror(lseg);
	if (fail_return || !ff_layout_has_available_ds(lseg))
		pnfs_error_mark_layout_for_return(ino, lseg);
	ds = NULL;
out:
	return ds;
}

const struct cred *
ff_layout_get_ds_cred(struct nfs4_ff_layout_mirror *mirror,
		      const struct pnfs_layout_range *range,
		      const struct cred *mdscred)
{
	const struct cred *cred;

	if (mirror && !mirror->mirror_ds->ds_versions[0].tightly_coupled) {
		cred = ff_layout_get_mirror_cred(mirror, range->iomode);
		if (!cred)
			cred = get_cred(mdscred);
	} else {
		cred = get_cred(mdscred);
	}
	return cred;
}

/**
 * nfs4_ff_find_or_create_ds_client - Find or create a DS rpc client
 * @mirror: pointer to the mirror
 * @ds_clp: nfs_client for the DS
 * @inode: pointer to inode
 *
 * Find or create a DS rpc client with th MDS server rpc client auth flavor
 * in the nfs_client cl_ds_clients list.
 */
struct rpc_clnt *
nfs4_ff_find_or_create_ds_client(struct nfs4_ff_layout_mirror *mirror,
				 struct nfs_client *ds_clp, struct inode *inode)
{
	switch (mirror->mirror_ds->ds_versions[0].version) {
	case 3:
		/* For NFSv3 DS, flavor is set when creating DS connections */
		return ds_clp->cl_rpcclient;
	case 4:
		return nfs4_find_or_create_ds_client(ds_clp, inode);
	default:
		BUG();
	}
}

void ff_layout_free_ds_ioerr(struct list_head *head)
{
	struct nfs4_ff_layout_ds_err *err;

	while (!list_empty(head)) {
		err = list_first_entry(head,
				struct nfs4_ff_layout_ds_err,
				list);
		list_del(&err->list);
		kfree(err);
	}
}

/* called with inode i_lock held */
int ff_layout_encode_ds_ioerr(struct xdr_stream *xdr, const struct list_head *head)
{
	struct nfs4_ff_layout_ds_err *err;
	__be32 *p;

	list_for_each_entry(err, head, list) {
		/* offset(8) + length(8) + stateid(NFS4_STATEID_SIZE)
		 * + array length + deviceid(NFS4_DEVICEID4_SIZE)
		 * + status(4) + opnum(4)
		 */
		p = xdr_reserve_space(xdr,
				28 + NFS4_STATEID_SIZE + NFS4_DEVICEID4_SIZE);
		if (unlikely(!p))
			return -ENOBUFS;
		p = xdr_encode_hyper(p, err->offset);
		p = xdr_encode_hyper(p, err->length);
		p = xdr_encode_opaque_fixed(p, &err->stateid,
					    NFS4_STATEID_SIZE);
		/* Encode 1 error */
		*p++ = cpu_to_be32(1);
		p = xdr_encode_opaque_fixed(p, &err->deviceid,
					    NFS4_DEVICEID4_SIZE);
		*p++ = cpu_to_be32(err->status);
		*p++ = cpu_to_be32(err->opnum);
		dprintk("%s: offset %llu length %llu status %d op %d\n",
			__func__, err->offset, err->length, err->status,
			err->opnum);
	}

	return 0;
}

static
unsigned int do_layout_fetch_ds_ioerr(struct pnfs_layout_hdr *lo,
				      const struct pnfs_layout_range *range,
				      struct list_head *head,
				      unsigned int maxnum)
{
	struct nfs4_flexfile_layout *flo = FF_LAYOUT_FROM_HDR(lo);
	struct inode *inode = lo->plh_inode;
	struct nfs4_ff_layout_ds_err *err, *n;
	unsigned int ret = 0;

	spin_lock(&inode->i_lock);
	list_for_each_entry_safe(err, n, &flo->error_list, list) {
		if (!pnfs_is_range_intersecting(err->offset,
				pnfs_end_offset(err->offset, err->length),
				range->offset,
				pnfs_end_offset(range->offset, range->length)))
			continue;
		if (!maxnum)
			break;
		list_move(&err->list, head);
		maxnum--;
		ret++;
	}
	spin_unlock(&inode->i_lock);
	return ret;
}

unsigned int ff_layout_fetch_ds_ioerr(struct pnfs_layout_hdr *lo,
				      const struct pnfs_layout_range *range,
				      struct list_head *head,
				      unsigned int maxnum)
{
	unsigned int ret;

	ret = do_layout_fetch_ds_ioerr(lo, range, head, maxnum);
	/* If we're over the max, discard all remaining entries */
	if (ret == maxnum) {
		LIST_HEAD(discard);
		do_layout_fetch_ds_ioerr(lo, range, &discard, -1);
		ff_layout_free_ds_ioerr(&discard);
	}
	return ret;
}

static bool ff_read_layout_has_available_ds(struct pnfs_layout_segment *lseg)
{
	struct nfs4_ff_layout_mirror *mirror;
	struct nfs4_deviceid_node *devid;
	u32 idx;

	for (idx = 0; idx < FF_LAYOUT_MIRROR_COUNT(lseg); idx++) {
		mirror = FF_LAYOUT_COMP(lseg, idx);
		if (mirror) {
			if (!mirror->mirror_ds)
				return true;
			if (IS_ERR(mirror->mirror_ds))
				continue;
			devid = &mirror->mirror_ds->id_node;
			if (!nfs4_test_deviceid_unavailable(devid))
				return true;
		}
	}

	return false;
}

static bool ff_rw_layout_has_available_ds(struct pnfs_layout_segment *lseg)
{
	struct nfs4_ff_layout_mirror *mirror;
	struct nfs4_deviceid_node *devid;
	u32 idx;

	for (idx = 0; idx < FF_LAYOUT_MIRROR_COUNT(lseg); idx++) {
		mirror = FF_LAYOUT_COMP(lseg, idx);
		if (!mirror || IS_ERR(mirror->mirror_ds))
			return false;
		if (!mirror->mirror_ds)
			continue;
		devid = &mirror->mirror_ds->id_node;
		if (nfs4_test_deviceid_unavailable(devid))
			return false;
	}

	return FF_LAYOUT_MIRROR_COUNT(lseg) != 0;
}

static bool ff_layout_has_available_ds(struct pnfs_layout_segment *lseg)
{
	if (lseg->pls_range.iomode == IOMODE_READ)
		return  ff_read_layout_has_available_ds(lseg);
	/* Note: RW layout needs all mirrors available */
	return ff_rw_layout_has_available_ds(lseg);
}

bool ff_layout_avoid_mds_available_ds(struct pnfs_layout_segment *lseg)
{
	return ff_layout_no_fallback_to_mds(lseg) ||
	       ff_layout_has_available_ds(lseg);
}

bool ff_layout_avoid_read_on_rw(struct pnfs_layout_segment *lseg)
{
	return lseg->pls_range.iomode == IOMODE_RW &&
	       ff_layout_no_read_on_rw(lseg);
}

module_param(dataserver_retrans, uint, 0644);
MODULE_PARM_DESC(dataserver_retrans, "The  number of times the NFSv4.1 client "
			"retries a request before it attempts further "
			" recovery  action.");
module_param(dataserver_timeo, uint, 0644);
MODULE_PARM_DESC(dataserver_timeo, "The time (in tenths of a second) the "
			"NFSv4.1  client  waits for a response from a "
			" data server before it retries an NFS request.");
