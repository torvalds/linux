// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014-2016 Christoph Hellwig.
 */
#include <linux/exportfs.h>
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

	error = sb->s_export_op->map_blocks(inode, offset, length, &iomap,
			iomode != IOMODE_READ, &device_generation);
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
	bl = kzalloc(struct_size(bl, extents, nr_extents_max), GFP_KERNEL);
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
	struct timespec64 mtime = inode_get_mtime(inode);
	struct iattr iattr = { .ia_valid = 0 };
	int error;

	if (lcp->lc_mtime.tv_nsec == UTIME_NOW ||
	    timespec64_compare(&lcp->lc_mtime, &mtime) < 0)
		lcp->lc_mtime = current_time(inode);
	iattr.ia_valid |= ATTR_ATIME | ATTR_CTIME | ATTR_MTIME;
	iattr.ia_atime = iattr.ia_ctime = iattr.ia_mtime = lcp->lc_mtime;

	if (lcp->lc_size_chg) {
		iattr.ia_valid |= ATTR_SIZE;
		iattr.ia_size = lcp->lc_newsize;
	}

	error = inode->i_sb->s_export_op->commit_blocks(inode, iomaps,
			nr_iomaps, &iattr);
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

	dev = kzalloc(struct_size(dev, volumes, 1), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	gdp->gd_device = dev;

	dev->nr_volumes = 1;
	b = &dev->volumes[0];

	b->type = PNFS_BLOCK_VOLUME_SIMPLE;
	b->simple.sig_len = PNFS_BLOCK_UUID_LEN;
	return sb->s_export_op->get_uuid(sb, b->simple.sig, &b->simple.sig_len,
			&b->simple.offset);
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

	dev = kzalloc(struct_size(dev, volumes, 1), GFP_KERNEL);
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

static void
nfsd4_scsi_fence_client(struct nfs4_layout_stateid *ls, struct nfsd_file *file)
{
	struct nfs4_client *clp = ls->ls_stid.sc_client;
	struct block_device *bdev = file->nf_file->f_path.mnt->mnt_sb->s_bdev;
	int status;

	status = bdev->bd_disk->fops->pr_ops->pr_preempt(bdev, NFSD_MDS_PR_KEY,
			nfsd4_scsi_pr_key(clp),
			PR_EXCLUSIVE_ACCESS_REG_ONLY, true);
	trace_nfsd_pnfs_fence(clp, bdev->bd_disk->disk_name, status);
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
