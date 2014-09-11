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
