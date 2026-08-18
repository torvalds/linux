// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014-2016 Christoph Hellwig.
 */
#include <linux/exportfs_block.h>
#include <linux/iomap.h>
#include <linux/slab.h>
#include <linux/pr.h>

#include <linux/nfsd/debug.h>

#include "blocklayoutxdr.h"
#include "pnfs.h"
#include "filecache.h"
#include "vfs.h"
#include "trace.h"

#define NFSDDBG_FACILITY	NFSDDBG_PNFS


/*
 * Get an extent from the file system that starts at offset or below
 * and may be shorter than the requested length.
 */
static __be32
nfsd4_block_map_extent(struct inode *inode, const struct svc_fh *fhp,
		u64 offset, u64 length, u32 iomode, u64 minlength,
		struct pnfs_block_extent *bex)
{
	struct super_block *sb = inode->i_sb;
	struct iomap iomap;
	u32 device_generation = 0;
	int error;

	error = sb->s_export_op->block_ops->map_blocks(inode, offset, length,
			&iomap, iomode != IOMODE_READ, &device_generation);
	if (error) {
		if (error == -ENXIO)
			return nfserr_layoutunavailable;
		return nfserrno(error);
	}

	switch (iomap.type) {
	case IOMAP_MAPPED:
		if (iomode == IOMODE_READ)
			bex->es = PNFS_BLOCK_READ_DATA;
		else
			bex->es = PNFS_BLOCK_READWRITE_DATA;
		bex->soff = iomap.addr;
		break;
	case IOMAP_UNWRITTEN:
		if (iomode & IOMODE_RW) {
			/*
			 * Crack monkey special case from section 2.3.1.
			 */
			if (minlength == 0) {
				dprintk("pnfsd: no soup for you!\n");
				return nfserr_layoutunavailable;
			}

			bex->es = PNFS_BLOCK_INVALID_DATA;
			bex->soff = iomap.addr;
			break;
		}
		fallthrough;
	case IOMAP_HOLE:
		if (iomode == IOMODE_READ) {
			bex->es = PNFS_BLOCK_NONE_DATA;
			break;
		}
		fallthrough;
	case IOMAP_DELALLOC:
	default:
		WARN(1, "pnfsd: filesystem returned %d extent\n", iomap.type);
		return nfserr_layoutunavailable;
	}

	error = nfsd4_set_deviceid(&bex->vol_id, fhp, device_generation);
	if (error)
		return nfserrno(error);

	bex->foff = iomap.offset;
	bex->len = iomap.length;
	return nfs_ok;
}

static __be32
nfsd4_block_proc_layoutget(struct svc_rqst *rqstp, struct inode *inode,
		const struct svc_fh *fhp, struct nfsd4_layoutget *args)
{
	struct nfsd4_layout_seg *seg = &args->lg_seg;
	struct pnfs_block_layout *bl;
	struct pnfs_block_extent *first_bex, *last_bex;
	u64 offset = seg->offset, length = seg->length;
	u32 i, nr_extents_max, block_size = i_blocksize(inode);
	__be32 nfserr;

	if (locks_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;

	nfserr = nfserr_layoutunavailable;
	if (seg->offset & (block_size - 1)) {
		dprintk("pnfsd: I/O misaligned\n");
		goto out_error;
	}

	/*
	 * RFC 8881, section 3.3.17:
	 *   The layout4 data type defines a layout for a file.
	 *
	 * RFC 8881, section 18.43.3:
	 *   The loga_maxcount field specifies the maximum layout size
	 *   (in bytes) that the client can handle. If the size of the
	 *   layout structure exceeds the size specified by maxcount,
	 *   the metadata server will return the NFS4ERR_TOOSMALL error.
	 */
	nfserr = nfserr_toosmall;
	if (args->lg_maxcount < PNFS_BLOCK_LAYOUT4_SIZE +
				PNFS_BLOCK_EXTENT_SIZE)
		goto out_error;

	/*
	 * Limit the maximum layout size to avoid allocating
	 * a large buffer on the server for each layout request.
	 */
	nr_extents_max = (min(args->lg_maxcount, PAGE_SIZE) -
			  PNFS_BLOCK_LAYOUT4_SIZE) / PNFS_BLOCK_EXTENT_SIZE;

	/*
	 * Some clients barf on non-zero block numbers for NONE or INVALID
	 * layouts, so make sure to zero the whole structure.
	 */
	nfserr = nfserrno(-ENOMEM);
	bl = kzalloc_flex(*bl, extents, nr_extents_max);
	if (!bl)
		goto out_error;
	bl->nr_extents = nr_extents_max;
	args->lg_content = bl;

	for (i = 0; i < bl->nr_extents; i++) {
		struct pnfs_block_extent *bex = bl->extents + i;
		u64 bex_length;

		nfserr = nfsd4_block_map_extent(inode, fhp, offset, length,
				seg->iomode, args->lg_minlength, bex);
		if (nfserr != nfs_ok)
			goto out_error;

		bex_length = bex->len - (offset - bex->foff);
		if (bex_length >= length) {
			bl->nr_extents = i + 1;
			break;
		}

		offset = bex->foff + bex->len;
		length -= bex_length;
	}

	first_bex = bl->extents;
	last_bex = bl->extents + bl->nr_extents - 1;

	nfserr = nfserr_layoutunavailable;
	length = last_bex->foff + last_bex->len - seg->offset;
	if (length < args->lg_minlength) {
		dprintk("pnfsd: extent smaller than minlength\n");
		goto out_error;
	}

	seg->offset = first_bex->foff;
	seg->length = last_bex->foff - first_bex->foff + last_bex->len;
	return nfs_ok;

out_error:
	seg->length = 0;
	return nfserr;
}

static __be32
nfsd4_block_commit_blocks(struct inode *inode, struct nfsd4_layoutcommit *lcp,
		struct iomap *iomaps, int nr_iomaps)
{
	int error;

	/*
	 * This ignores the client provided mtime in loca_time_modify, as a
	 * fully client specified mtime doesn't really fit into the Linux
	 * multi-grain timestamp architecture.
	 *
	 * RFC 8881 Section 18.42 makes it clear that the client provided
	 * timestamp is a "may" condition, and clients that want to force a
	 * specific timestamp should send a separate SETATTR in the compound.
	 */
	error = inode->i_sb->s_export_op->block_ops->commit_blocks(inode,
			iomaps, nr_iomaps,
			lcp->lc_size_chg ? lcp->lc_newsize : 0);
	kfree(iomaps);
	return nfserrno(error);
}

#ifdef CONFIG_NFSD_BLOCKLAYOUT
static int
nfsd4_block_get_device_info_simple(struct super_block *sb,
		struct nfsd4_getdeviceinfo *gdp)
{
	struct pnfs_block_deviceaddr *dev;
	struct pnfs_block_volume *b;

	dev = kzalloc_flex(*dev, volumes, 1);
	if (!dev)
		return -ENOMEM;
	gdp->gd_device = dev;

	dev->nr_volumes = 1;
	b = &dev->volumes[0];

	b->type = PNFS_BLOCK_VOLUME_SIMPLE;
	b->simple.sig_len = PNFS_BLOCK_UUID_LEN;
	return sb->s_export_op->block_ops->get_uuid(sb, b->simple.sig,
			&b->simple.sig_len, &b->simple.offset);
}

static __be32
nfsd4_block_proc_getdeviceinfo(struct super_block *sb,
		struct svc_rqst *rqstp,
		struct nfs4_client *clp,
		struct nfsd4_getdeviceinfo *gdp)
{
	if (bdev_is_partition(sb->s_bdev))
		return nfserr_inval;
	return nfserrno(nfsd4_block_get_device_info_simple(sb, gdp));
}

static __be32
nfsd4_block_proc_layoutcommit(struct inode *inode, struct svc_rqst *rqstp,
		struct nfsd4_layoutcommit *lcp)
{
	struct iomap *iomaps;
	int nr_iomaps;
	__be32 nfserr;

	rqstp->rq_arg = lcp->lc_up_layout;
	svcxdr_init_decode(rqstp);

	nfserr = nfsd4_block_decode_layoutupdate(&rqstp->rq_arg_stream,
			&iomaps, &nr_iomaps, i_blocksize(inode));
	if (nfserr != nfs_ok)
		return nfserr;

	return nfsd4_block_commit_blocks(inode, lcp, iomaps, nr_iomaps);
}

const struct nfsd4_layout_ops bl_layout_ops = {
	/*
	 * Pretend that we send notification to the client.  This is a blatant
	 * lie to force recent Linux clients to cache our device IDs.
	 * We rarely ever change the device ID, so the harm of leaking deviceids
	 * for a while isn't too bad.  Unfortunately RFC5661 is a complete mess
	 * in this regard, but I filed errata 4119 for this a while ago, and
	 * hopefully the Linux client will eventually start caching deviceids
	 * without this again.
	 */
	.notify_types		=
			NOTIFY_DEVICEID4_DELETE | NOTIFY_DEVICEID4_CHANGE,
	.proc_getdeviceinfo	= nfsd4_block_proc_getdeviceinfo,
	.encode_getdeviceinfo	= nfsd4_block_encode_getdeviceinfo,
	.proc_layoutget		= nfsd4_block_proc_layoutget,
	.encode_layoutget	= nfsd4_block_encode_layoutget,
	.proc_layoutcommit	= nfsd4_block_proc_layoutcommit,
};
#endif /* CONFIG_NFSD_BLOCKLAYOUT */

#ifdef CONFIG_NFSD_SCSILAYOUT

#define NFSD_MDS_PR_FENCED	XA_MARK_0

/*
 * Clear the fence flag if the device already has an entry. This occurs
 * when a client re-registers after a previous fence, allowing new
 * layouts for this device.
 *
 * Insert only on first registration. This bounds cl_dev_fences to the
 * count of devices this client has accessed, preventing unbounded growth.
 */
static inline int nfsd4_scsi_fence_insert(struct nfs4_client *clp,
					  dev_t device)
{
	struct xarray *xa = &clp->cl_dev_fences;
	int ret;

	xa_lock(xa);
	ret = __xa_insert(xa, device, XA_ZERO_ENTRY, GFP_KERNEL);
	if (ret == -EBUSY) {
		__xa_clear_mark(xa, device, NFSD_MDS_PR_FENCED);
		ret = 0;
	}
	xa_unlock(xa);
	clp->cl_fence_retry_warn = false;
	return ret;
}

static inline bool nfsd4_scsi_fence_set(struct nfs4_client *clp, dev_t device)
{
	struct xarray *xa = &clp->cl_dev_fences;
	bool skip;

	xa_lock(xa);
	skip = xa_get_mark(xa, device, NFSD_MDS_PR_FENCED);
	if (!skip)
		__xa_set_mark(xa, device, NFSD_MDS_PR_FENCED);
	xa_unlock(xa);
	return skip;
}

static inline void nfsd4_scsi_fence_clear(struct nfs4_client *clp, dev_t device)
{
	xa_clear_mark(&clp->cl_dev_fences, device, NFSD_MDS_PR_FENCED);
}

#define NFSD_MDS_PR_KEY		0x0100000000000000ULL

/*
 * We use the client ID as a unique key for the reservations.
 * This allows us to easily fence a client when recalls fail.
 */
static u64 nfsd4_scsi_pr_key(struct nfs4_client *clp)
{
	return ((u64)clp->cl_clientid.cl_boot << 32) | clp->cl_clientid.cl_id;
}

static const u8 designator_types[] = {
	PS_DESIGNATOR_EUI64,
	PS_DESIGNATOR_NAA,
};

static int
nfsd4_block_get_unique_id(struct gendisk *disk, struct pnfs_block_volume *b)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(designator_types); i++) {
		u8 type = designator_types[i];

		ret = disk->fops->get_unique_id(disk, b->scsi.designator, type);
		if (ret > 0) {
			b->scsi.code_set = PS_CODE_SET_BINARY;
			b->scsi.designator_type = type;
			b->scsi.designator_len = ret;
			return 0;
		}
	}

	return -EINVAL;
}

static int
nfsd4_block_get_device_info_scsi(struct super_block *sb,
		struct nfs4_client *clp,
		struct nfsd4_getdeviceinfo *gdp)
{
	struct pnfs_block_deviceaddr *dev;
	struct pnfs_block_volume *b;
	const struct pr_ops *ops;
	int ret;

	dev = kzalloc_flex(*dev, volumes, 1);
	if (!dev)
		return -ENOMEM;
	gdp->gd_device = dev;

	dev->nr_volumes = 1;
	b = &dev->volumes[0];

	b->type = PNFS_BLOCK_VOLUME_SCSI;
	b->scsi.pr_key = nfsd4_scsi_pr_key(clp);

	ret = nfsd4_block_get_unique_id(sb->s_bdev->bd_disk, b);
	if (ret < 0)
		goto out_free_dev;

	ret = -EINVAL;
	ops = sb->s_bdev->bd_disk->fops->pr_ops;
	if (!ops) {
		pr_err("pNFS: device %s does not support PRs.\n",
			sb->s_id);
		goto out_free_dev;
	}

	ret = nfsd4_scsi_fence_insert(clp, sb->s_bdev->bd_dev);
	if (ret < 0)
		goto out_free_dev;

	ret = ops->pr_register(sb->s_bdev, 0, NFSD_MDS_PR_KEY, true);
	if (ret) {
		pr_err("pNFS: failed to register key for device %s.\n",
			sb->s_id);
		goto out_free_dev;
	}

	ret = ops->pr_reserve(sb->s_bdev, NFSD_MDS_PR_KEY,
			PR_EXCLUSIVE_ACCESS_REG_ONLY, 0);
	if (ret) {
		pr_err("pNFS: failed to reserve device %s.\n",
			sb->s_id);
		goto out_free_dev;
	}

	return 0;

out_free_dev:
	kfree(dev);
	gdp->gd_device = NULL;
	return ret;
}

static __be32
nfsd4_scsi_proc_getdeviceinfo(struct super_block *sb,
		struct svc_rqst *rqstp,
		struct nfs4_client *clp,
		struct nfsd4_getdeviceinfo *gdp)
{
	if (bdev_is_partition(sb->s_bdev))
		return nfserr_inval;
	return nfserrno(nfsd4_block_get_device_info_scsi(sb, clp, gdp));
}
static __be32
nfsd4_scsi_proc_layoutcommit(struct inode *inode, struct svc_rqst *rqstp,
		struct nfsd4_layoutcommit *lcp)
{
	struct iomap *iomaps;
	int nr_iomaps;
	__be32 nfserr;

	rqstp->rq_arg = lcp->lc_up_layout;
	svcxdr_init_decode(rqstp);

	nfserr = nfsd4_scsi_decode_layoutupdate(&rqstp->rq_arg_stream,
			&iomaps, &nr_iomaps, i_blocksize(inode));
	if (nfserr != nfs_ok)
		return nfserr;

	return nfsd4_block_commit_blocks(inode, lcp, iomaps, nr_iomaps);
}

/*
 * Perform the fence operation to prevent the client from accessing the
 * block device. If a fence operation is already in progress, wait for
 * it to complete before checking the NFSD_MDS_PR_FENCED flag. Once the
 * operation is complete, check the flag. If NFSD_MDS_PR_FENCED is set,
 * update the layout stateid by setting the ls_fenced flag to indicate
 * that the client has been fenced.
 *
 * The cl_fence_mutex ensures that the fence operation has been fully
 * completed, rather than just in progress, when returning from this
 * function.
 *
 * Return true if client was fenced otherwise return false.
 */
static bool
nfsd4_scsi_fence_client(struct nfs4_layout_stateid *ls, struct nfsd_file *file)
{
	struct nfs4_client *clp = ls->ls_stid.sc_client;
	struct block_device *bdev = file->nf_file->f_path.mnt->mnt_sb->s_bdev;
	int status;
	bool ret;

	mutex_lock(&clp->cl_fence_mutex);
	if (nfsd4_scsi_fence_set(clp, bdev->bd_dev)) {
		mutex_unlock(&clp->cl_fence_mutex);
		return true;
	}

	status = bdev->bd_disk->fops->pr_ops->pr_preempt(bdev, NFSD_MDS_PR_KEY,
			nfsd4_scsi_pr_key(clp),
			PR_EXCLUSIVE_ACCESS_REG_ONLY, true);
	/*
	 * Reset to allow retry only when the command could not have
	 * reached the device. Negative status means a local error
	 * (e.g., -ENOMEM) prevented the command from being sent.
	 * PR_STS_PATH_FAILED, PR_STS_PATH_FAST_FAILED, and
	 * PR_STS_RETRY_PATH_FAILURE indicate transport path failures
	 * before device delivery.
	 *
	 * For all other errors, the command may have reached the device
	 * and the preempt may have succeeded. Avoid resetting, since
	 * retrying a successful preempt returns PR_STS_IOERR or
	 * PR_STS_RESERVATION_CONFLICT, which would cause an infinite
	 * retry loop.
	 */
	switch (status) {
	case 0:
	case PR_STS_IOERR:
	case PR_STS_RESERVATION_CONFLICT:
		ret = true;
		break;
	default:
		/* retry-able and other errors */
		ret = false;
		nfsd4_scsi_fence_clear(clp, bdev->bd_dev);
		break;
	}
	mutex_unlock(&clp->cl_fence_mutex);

	trace_nfsd_pnfs_fence(clp, bdev->bd_disk->disk_name, status);
	return ret;
}

const struct nfsd4_layout_ops scsi_layout_ops = {
	/*
	 * Pretend that we send notification to the client.  This is a blatant
	 * lie to force recent Linux clients to cache our device IDs.
	 * We rarely ever change the device ID, so the harm of leaking deviceids
	 * for a while isn't too bad.  Unfortunately RFC5661 is a complete mess
	 * in this regard, but I filed errata 4119 for this a while ago, and
	 * hopefully the Linux client will eventually start caching deviceids
	 * without this again.
	 */
	.notify_types		=
			NOTIFY_DEVICEID4_DELETE | NOTIFY_DEVICEID4_CHANGE,
	.proc_getdeviceinfo	= nfsd4_scsi_proc_getdeviceinfo,
	.encode_getdeviceinfo	= nfsd4_block_encode_getdeviceinfo,
	.proc_layoutget		= nfsd4_block_proc_layoutget,
	.encode_layoutget	= nfsd4_block_encode_layoutget,
	.proc_layoutcommit	= nfsd4_scsi_proc_layoutcommit,
	.fence_client		= nfsd4_scsi_fence_client,
};
#endif /* CONFIG_NFSD_SCSILAYOUT */
