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

/* Open a block_device by device number. */
struct block_device *nfs4_blkdev_get(dev_t dev)
{
	struct block_device *bd;

	dprintk("%s enter\n", __func__);
	bd = blkdev_get_by_dev(dev, FMODE_READ, NULL);
	if (IS_ERR(bd))
		goto fail;
	return bd;
fail:
	dprintk("%s failed to open device : %ld\n",
			__func__, PTR_ERR(bd));
	return NULL;
}

/*
 * Release the block device
 */
int nfs4_blkdev_put(struct block_device *bdev)
{
	dprintk("%s for device %d:%d\n", __func__, MAJOR(bdev->bd_dev),
			MINOR(bdev->bd_dev));
	return blkdev_put(bdev, FMODE_READ);
}

/*
 * Shouldn't there be a rpc_generic_upcall() to do this for us?
 */
ssize_t bl_pipe_upcall(struct file *filp, struct rpc_pipe_msg *msg,
		       char __user *dst, size_t buflen)
{
	char *data = (char *)msg->data + msg->copied;
	size_t mlen = min(msg->len - msg->copied, buflen);
	unsigned long left;

	left = copy_to_user(dst, data, mlen);
	if (left == mlen) {
		msg->errno = -EFAULT;
		return -EFAULT;
	}

	mlen -= left;
	msg->copied += mlen;
	msg->errno = 0;
	return mlen;
}

static struct bl_dev_msg bl_mount_reply;

ssize_t bl_pipe_downcall(struct file *filp, const char __user *src,
			 size_t mlen)
{
	if (mlen != sizeof (struct bl_dev_msg))
		return -EINVAL;

	if (copy_from_user(&bl_mount_reply, src, mlen) != 0)
		return -EFAULT;

	wake_up(&bl_wq);

	return mlen;
}

void bl_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	if (msg->errno >= 0)
		return;
	wake_up(&bl_wq);
}

/*
 * Decodes pnfs_block_deviceaddr4 which is XDR encoded in dev->dev_addr_buf.
 */
struct pnfs_block_dev *
nfs4_blk_decode_device(struct nfs_server *server,
		       struct pnfs_device *dev,
		       struct list_head *sdlist)
{
	struct pnfs_block_dev *rv = NULL;
	struct block_device *bd = NULL;
	struct rpc_pipe_msg msg;
	struct bl_msg_hdr bl_msg = {
		.type = BL_DEVICE_MOUNT,
		.totallen = dev->mincount,
	};
	uint8_t *dataptr;
	DECLARE_WAITQUEUE(wq, current);
	struct bl_dev_msg *reply = &bl_mount_reply;

	dprintk("%s CREATING PIPEFS MESSAGE\n", __func__);
	dprintk("%s: deviceid: %s, mincount: %d\n", __func__, dev->dev_id.data,
		dev->mincount);

	memset(&msg, 0, sizeof(msg));
	msg.data = kzalloc(sizeof(bl_msg) + dev->mincount, GFP_NOFS);
	if (!msg.data) {
		rv = ERR_PTR(-ENOMEM);
		goto out;
	}

	memcpy(msg.data, &bl_msg, sizeof(bl_msg));
	dataptr = (uint8_t *) msg.data;
	memcpy(&dataptr[sizeof(bl_msg)], dev->area, dev->mincount);
	msg.len = sizeof(bl_msg) + dev->mincount;

	dprintk("%s CALLING USERSPACE DAEMON\n", __func__);
	add_wait_queue(&bl_wq, &wq);
	if (rpc_queue_upcall(bl_device_pipe->d_inode, &msg) < 0) {
		remove_wait_queue(&bl_wq, &wq);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&bl_wq, &wq);

	if (reply->status != BL_DEVICE_REQUEST_PROC) {
		dprintk("%s failed to open device: %d\n",
			__func__, reply->status);
		rv = ERR_PTR(-EINVAL);
		goto out;
	}

	bd = nfs4_blkdev_get(MKDEV(reply->major, reply->minor));
	if (IS_ERR(bd)) {
		dprintk("%s failed to open device : %ld\n",
			__func__, PTR_ERR(bd));
		goto out;
	}

	rv = kzalloc(sizeof(*rv), GFP_NOFS);
	if (!rv) {
		rv = ERR_PTR(-ENOMEM);
		goto out;
	}

	rv->bm_mdev = bd;
	memcpy(&rv->bm_mdevid, &dev->dev_id, sizeof(struct nfs4_deviceid));
	dprintk("%s Created device %s with bd_block_size %u\n",
		__func__,
		bd->bd_disk->disk_name,
		bd->bd_block_size);

out:
	kfree(msg.data);
	return rv;
}
