/*
 * Copyright (c) 2014 Christoph Hellwig.
 */
#include <linux/exportfs.h>
#include <linux/genhd.h>
#include <linux/slab.h>

#include <linux/nfsd/debug.h>

#include "blocklayoutxdr.h"
#include "pnfs.h"

#define NFSDDBG_FACILITY	NFSDDBG_PNFS


static int
nfsd4_block_get_device_info_simple(struct super_block *sb,
		struct nfsd4_getdeviceinfo *gdp)
{
	struct pnfs_block_deviceaddr *dev;
	struct pnfs_block_volume *b;

	dev = kzalloc(sizeof(struct pnfs_block_deviceaddr) +
		      sizeof(struct pnfs_block_volume), GFP_KERNEL);
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
		struct nfsd4_getdeviceinfo *gdp)
{
	if (sb->s_bdev != sb->s_bdev->bd_contains)
		return nfserr_inval;
	return nfserrno(nfsd4_block_get_device_info_simple(sb, gdp));
}

static __be32
nfsd4_block_proc_layoutget(struct inode *inode, const struct svc_fh *fhp,
		struct nfsd4_layoutget *args)
{
	struct nfsd4_layout_seg *seg = &args->lg_seg;
	struct super_block *sb = inode->i_sb;
	u32 block_size = i_blocksize(inode);
	struct pnfs_block_extent *bex;
	struct iomap iomap;
	u32 device_generation = 0;
	int error;

	if (seg->offset & (block_size - 1)) {
		dprintk("pnfsd: I/O misaligned\n");
		goto out_layoutunavailable;
	}

	/*
	 * Some clients barf on non-zero block numbers for NONE or INVALID
	 * layouts, so make sure to zero the whole structure.
	 */
	error = -ENOMEM;
	bex = kzalloc(sizeof(*bex), GFP_KERNEL);
	if (!bex)
		goto out_error;
	args->lg_content = bex;

	error = sb->s_export_op->map_blocks(inode, seg->offset, seg->length,
					    &iomap, seg->iomode != IOMODE_READ,
					    &device_generation);
	if (error) {
		if (error == -ENXIO)
			goto out_layoutunavailable;
		goto out_error;
	}

	if (iomap.length < args->lg_minlength) {
		dprintk("pnfsd: extent smaller than minlength\n");
		goto out_layoutunavailable;
	}

	switch (iomap.type) {
	case IOMAP_MAPPED:
		if (seg->iomode == IOMODE_READ)
			bex->es = PNFS_BLOCK_READ_DATA;
		else
			bex->es = PNFS_BLOCK_READWRITE_DATA;
		bex->soff = (iomap.blkno << 9);
		break;
	case IOMAP_UNWRITTEN:
		if (seg->iomode & IOMODE_RW) {
			/*
			 * Crack monkey special case from section 2.3.1.
			 */
			if (args->lg_minlength == 0) {
				dprintk("pnfsd: no soup for you!\n");
				goto out_layoutunavailable;
			}

			bex->es = PNFS_BLOCK_INVALID_DATA;
			bex->soff = (iomap.blkno << 9);
			break;
		}
		/*FALLTHRU*/
	case IOMAP_HOLE:
		if (seg->iomode == IOMODE_READ) {
			bex->es = PNFS_BLOCK_NONE_DATA;
			break;
		}
		/*FALLTHRU*/
	case IOMAP_DELALLOC:
	default:
		WARN(1, "pnfsd: filesystem returned %d extent\n", iomap.type);
		goto out_layoutunavailable;
	}

	error = nfsd4_set_deviceid(&bex->vol_id, fhp, device_generation);
	if (error)
		goto out_error;
	bex->foff = iomap.offset;
	bex->len = iomap.length;

	seg->offset = iomap.offset;
	seg->length = iomap.length;

	dprintk("GET: 0x%llx:0x%llx %d\n", bex->foff, bex->len, bex->es);
	return 0;

out_error:
	seg->length = 0;
	return nfserrno(error);
out_layoutunavailable:
	seg->length = 0;
	return nfserr_layoutunavailable;
}

static __be32
nfsd4_block_proc_layoutcommit(struct inode *inode,
		struct nfsd4_layoutcommit *lcp)
{
	loff_t new_size = lcp->lc_last_wr + 1;
	struct iattr iattr = { .ia_valid = 0 };
	struct iomap *iomaps;
	int nr_iomaps;
	int error;

	nr_iomaps = nfsd4_block_decode_layoutupdate(lcp->lc_up_layout,
			lcp->lc_up_len, &iomaps, i_blocksize(inode));
	if (nr_iomaps < 0)
		return nfserrno(nr_iomaps);

	if (lcp->lc_mtime.tv_nsec == UTIME_NOW ||
	    timespec_compare(&lcp->lc_mtime, &inode->i_mtime) < 0)
		lcp->lc_mtime = current_fs_time(inode->i_sb);
	iattr.ia_valid |= ATTR_ATIME | ATTR_CTIME | ATTR_MTIME;
	iattr.ia_atime = iattr.ia_ctime = iattr.ia_mtime = lcp->lc_mtime;

	if (new_size > i_size_read(inode)) {
		iattr.ia_valid |= ATTR_SIZE;
		iattr.ia_size = new_size;
	}

	error = inode->i_sb->s_export_op->commit_blocks(inode, iomaps,
			nr_iomaps, &iattr);
	kfree(iomaps);
	return nfserrno(error);
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
