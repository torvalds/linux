/*
 *  linux/fs/nfs/blocklayout/blocklayoutdev.c
 *
 *  Device operations for the pnfs nfs4 file layout driver.
 *
 *  Copyright (c) 2006 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Andy Adamson <andros@citi.umich.edu>
 *  Fred Isaman <iisaman@umich.edu>
 *
 * permission is granted to use, copy, create derivative works and
 * redistribute this software and such derivative works for any purpose,
 * so long as the name of the university of michigan is not used in
 * any advertising or publicity pertaining to the use or distribution
 * of this software without specific, written prior authorization.  if
 * the above copyright notice or any other identification of the
 * university of michigan is included in any copy of any portion of
 * this software, then the disclaimer below must also be included.
 *
 * this software is provided as is, without representation from the
 * university of michigan as to its fitness for any purpose, and without
 * warranty by the university of michigan of any kind, either express
 * or implied, including without limitation the implied warranties of
 * merchantability and fitness for a particular purpose.  the regents
 * of the university of michigan shall not be liable for any damages,
 * including special, indirect, incidental, or consequential damages,
 * with respect to any claim arising out or in connection with the use
 * of the software, even if it has been or is hereafter advised of the
 * possibility of such damages.
 */
#include <linux/module.h>
#include <linux/buffer_head.h> /* __bread */

#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hash.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

static int decode_sector_number(__be32 **rp, sector_t *sp)
{
	uint64_t s;

	*rp = xdr_decode_hyper(*rp, &s);
	if (s & 0x1ff) {
		printk(KERN_WARNING "NFS: %s: sector not aligned\n", __func__);
		return -1;
	}
	*sp = s >> SECTOR_SHIFT;
	return 0;
}

ssize_t bl_pipe_downcall(struct file *filp, const char __user *src,
			 size_t mlen)
{
	struct nfs_net *nn = net_generic(filp->f_dentry->d_sb->s_fs_info,
					 nfs_net_id);

	if (mlen != sizeof (struct bl_dev_msg))
		return -EINVAL;

	if (copy_from_user(&nn->bl_mount_reply, src, mlen) != 0)
		return -EFAULT;

	wake_up(&nn->bl_wq);

	return mlen;
}

void bl_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct bl_pipe_msg *bl_pipe_msg = container_of(msg, struct bl_pipe_msg, msg);

	if (msg->errno >= 0)
		return;
	wake_up(bl_pipe_msg->bl_wq);
}

/*
 * Decodes pnfs_block_deviceaddr4 which is XDR encoded in dev->dev_addr_buf.
 */
struct nfs4_deviceid_node *
bl_alloc_deviceid_node(struct nfs_server *server, struct pnfs_device *dev,
		gfp_t gfp_mask)
{
	struct pnfs_block_dev *rv;
	struct block_device *bd;
	struct bl_pipe_msg bl_pipe_msg;
	struct rpc_pipe_msg *msg = &bl_pipe_msg.msg;
	struct bl_msg_hdr bl_msg = {
		.type = BL_DEVICE_MOUNT,
		.totallen = dev->mincount,
	};
	uint8_t *dataptr;
	DECLARE_WAITQUEUE(wq, current);
	int offset, len, i, rc;
	struct net *net = server->nfs_client->cl_net;
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct bl_dev_msg *reply = &nn->bl_mount_reply;

	dprintk("%s CREATING PIPEFS MESSAGE\n", __func__);
	dprintk("%s: deviceid: %s, mincount: %d\n", __func__, dev->dev_id.data,
		dev->mincount);

	bl_pipe_msg.bl_wq = &nn->bl_wq;
	memset(msg, 0, sizeof(*msg));
	msg->data = kzalloc(sizeof(bl_msg) + dev->mincount, gfp_mask);
	if (!msg->data)
		goto out;

	memcpy(msg->data, &bl_msg, sizeof(bl_msg));
	dataptr = (uint8_t *) msg->data;
	len = dev->mincount;
	offset = sizeof(bl_msg);
	for (i = 0; len > 0; i++) {
		memcpy(&dataptr[offset], page_address(dev->pages[i]),
				len < PAGE_CACHE_SIZE ? len : PAGE_CACHE_SIZE);
		len -= PAGE_CACHE_SIZE;
		offset += PAGE_CACHE_SIZE;
	}
	msg->len = sizeof(bl_msg) + dev->mincount;

	dprintk("%s CALLING USERSPACE DAEMON\n", __func__);
	add_wait_queue(&nn->bl_wq, &wq);
	rc = rpc_queue_upcall(nn->bl_device_pipe, msg);
	if (rc < 0) {
		remove_wait_queue(&nn->bl_wq, &wq);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&nn->bl_wq, &wq);

	if (reply->status != BL_DEVICE_REQUEST_PROC) {
		printk(KERN_WARNING "%s failed to decode device: %d\n",
			__func__, reply->status);
		goto out;
	}

	bd = blkdev_get_by_dev(MKDEV(reply->major, reply->minor),
			       FMODE_READ, NULL);
	if (IS_ERR(bd)) {
		printk(KERN_WARNING "%s failed to open device %d:%d (%ld)\n",
			__func__, reply->major, reply->minor,
			PTR_ERR(bd));
		goto out;
	}

	rv = kzalloc(sizeof(*rv), gfp_mask);
	if (!rv)
		goto out;

	nfs4_init_deviceid_node(&rv->d_node, server, &dev->dev_id);
	rv->d_bdev = bd;

	dprintk("%s Created device %s with bd_block_size %u\n",
		__func__,
		bd->bd_disk->disk_name,
		bd->bd_block_size);

	kfree(msg->data);
	return &rv->d_node;

out:
	kfree(msg->data);
	return NULL;
}

void
bl_free_deviceid_node(struct nfs4_deviceid_node *d)
{
	struct pnfs_block_dev *dev =
		container_of(d, struct pnfs_block_dev, d_node);
	struct net *net = d->nfs_client->cl_net;

	blkdev_put(dev->d_bdev, FMODE_READ);
	bl_dm_remove(net, dev->d_bdev->bd_dev);

	kfree(dev);
}

/* Tracks info needed to ensure extents in layout obey constraints of spec */
struct layout_verification {
	u32 mode;	/* R or RW */
	u64 start;	/* Expected start of next non-COW extent */
	u64 inval;	/* Start of INVAL coverage */
	u64 cowread;	/* End of COW read coverage */
};

/* Verify the extent meets the layout requirements of the pnfs-block draft,
 * section 2.3.1.
 */
static int verify_extent(struct pnfs_block_extent *be,
			 struct layout_verification *lv)
{
	if (lv->mode == IOMODE_READ) {
		if (be->be_state == PNFS_BLOCK_READWRITE_DATA ||
		    be->be_state == PNFS_BLOCK_INVALID_DATA)
			return -EIO;
		if (be->be_f_offset != lv->start)
			return -EIO;
		lv->start += be->be_length;
		return 0;
	}
	/* lv->mode == IOMODE_RW */
	if (be->be_state == PNFS_BLOCK_READWRITE_DATA) {
		if (be->be_f_offset != lv->start)
			return -EIO;
		if (lv->cowread > lv->start)
			return -EIO;
		lv->start += be->be_length;
		lv->inval = lv->start;
		return 0;
	} else if (be->be_state == PNFS_BLOCK_INVALID_DATA) {
		if (be->be_f_offset != lv->start)
			return -EIO;
		lv->start += be->be_length;
		return 0;
	} else if (be->be_state == PNFS_BLOCK_READ_DATA) {
		if (be->be_f_offset > lv->start)
			return -EIO;
		if (be->be_f_offset < lv->inval)
			return -EIO;
		if (be->be_f_offset < lv->cowread)
			return -EIO;
		/* It looks like you might want to min this with lv->start,
		 * but you really don't.
		 */
		lv->inval = lv->inval + be->be_length;
		lv->cowread = be->be_f_offset + be->be_length;
		return 0;
	} else
		return -EIO;
}

/* XDR decode pnfs_block_layout4 structure */
int
nfs4_blk_process_layoutget(struct pnfs_layout_hdr *lo,
			   struct nfs4_layoutget_res *lgr, gfp_t gfp_flags)
{
	struct pnfs_block_layout *bl = BLK_LO2EXT(lo);
	int i, status = -EIO;
	uint32_t count;
	struct pnfs_block_extent *be = NULL, *save;
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch;
	__be32 *p;
	struct layout_verification lv = {
		.mode = lgr->range.iomode,
		.start = lgr->range.offset >> SECTOR_SHIFT,
		.inval = lgr->range.offset >> SECTOR_SHIFT,
		.cowread = lgr->range.offset >> SECTOR_SHIFT,
	};
	LIST_HEAD(extents);

	dprintk("---> %s\n", __func__);

	scratch = alloc_page(gfp_flags);
	if (!scratch)
		return -ENOMEM;

	xdr_init_decode_pages(&stream, &buf, lgr->layoutp->pages, lgr->layoutp->len);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	p = xdr_inline_decode(&stream, 4);
	if (unlikely(!p))
		goto out_err;

	count = be32_to_cpup(p++);

	dprintk("%s enter, number of extents %i\n", __func__, count);
	p = xdr_inline_decode(&stream, (28 + NFS4_DEVICEID4_SIZE) * count);
	if (unlikely(!p))
		goto out_err;

	/* Decode individual extents, putting them in temporary
	 * staging area until whole layout is decoded to make error
	 * recovery easier.
	 */
	for (i = 0; i < count; i++) {
		struct nfs4_deviceid id;

		be = kzalloc(sizeof(struct pnfs_block_extent), GFP_NOFS);
		if (!be) {
			status = -ENOMEM;
			goto out_err;
		}
		memcpy(&id, p, NFS4_DEVICEID4_SIZE);
		p += XDR_QUADLEN(NFS4_DEVICEID4_SIZE);

		be->be_device =
			nfs4_find_get_deviceid(NFS_SERVER(lo->plh_inode), &id,
						lo->plh_lc_cred, gfp_flags);
		if (!be->be_device)
			goto out_err;

		/* The next three values are read in as bytes,
		 * but stored as 512-byte sector lengths
		 */
		if (decode_sector_number(&p, &be->be_f_offset) < 0)
			goto out_err;
		if (decode_sector_number(&p, &be->be_length) < 0)
			goto out_err;
		if (decode_sector_number(&p, &be->be_v_offset) < 0)
			goto out_err;
		be->be_state = be32_to_cpup(p++);
		if (verify_extent(be, &lv)) {
			dprintk("%s verify failed\n", __func__);
			goto out_err;
		}
		list_add_tail(&be->be_list, &extents);
	}
	if (lgr->range.offset + lgr->range.length !=
			lv.start << SECTOR_SHIFT) {
		dprintk("%s Final length mismatch\n", __func__);
		be = NULL;
		goto out_err;
	}
	if (lv.start < lv.cowread) {
		dprintk("%s Final uncovered COW extent\n", __func__);
		be = NULL;
		goto out_err;
	}
	/* Extents decoded properly, now try to merge them in to
	 * existing layout extents.
	 */
	list_for_each_entry_safe(be, save, &extents, be_list) {
		list_del(&be->be_list);

		status = ext_tree_insert(bl, be);
		if (status)
			goto out_free_list;
	}
	status = 0;
 out:
	__free_page(scratch);
	dprintk("%s returns %i\n", __func__, status);
	return status;

 out_err:
	nfs4_put_deviceid_node(be->be_device);
	kfree(be);
 out_free_list:
	while (!list_empty(&extents)) {
		be = list_first_entry(&extents, struct pnfs_block_extent,
				      be_list);
		list_del(&be->be_list);
		nfs4_put_deviceid_node(be->be_device);
		kfree(be);
	}
	goto out;
}
