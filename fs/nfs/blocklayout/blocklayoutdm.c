/*
 *  linux/fs/nfs/blocklayout/blocklayoutdm.c
 *
 *  Module for the NFSv4.1 pNFS block layout driver.
 *
 *  Copyright (c) 2007 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Fred Isaman <iisaman@umich.edu>
 *  Andy Adamson <andros@citi.umich.edu>
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

#include <linux/genhd.h> /* gendisk - used in a dprintk*/
#include <linux/sched.h>
#include <linux/hash.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

static void dev_remove(struct net *net, dev_t dev)
{
	struct bl_pipe_msg bl_pipe_msg;
	struct rpc_pipe_msg *msg = &bl_pipe_msg.msg;
	struct bl_dev_msg bl_umount_request;
	struct bl_msg_hdr bl_msg = {
		.type = BL_DEVICE_UMOUNT,
		.totallen = sizeof(bl_umount_request),
	};
	uint8_t *dataptr;
	DECLARE_WAITQUEUE(wq, current);
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	dprintk("Entering %s\n", __func__);

	bl_pipe_msg.bl_wq = &nn->bl_wq;
	memset(msg, 0, sizeof(*msg));
	msg->data = kzalloc(1 + sizeof(bl_umount_request), GFP_NOFS);
	if (!msg->data)
		goto out;

	memset(&bl_umount_request, 0, sizeof(bl_umount_request));
	bl_umount_request.major = MAJOR(dev);
	bl_umount_request.minor = MINOR(dev);

	memcpy(msg->data, &bl_msg, sizeof(bl_msg));
	dataptr = (uint8_t *) msg->data;
	memcpy(&dataptr[sizeof(bl_msg)], &bl_umount_request, sizeof(bl_umount_request));
	msg->len = sizeof(bl_msg) + bl_msg.totallen;

	add_wait_queue(&nn->bl_wq, &wq);
	if (rpc_queue_upcall(nn->bl_device_pipe, msg) < 0) {
		remove_wait_queue(&nn->bl_wq, &wq);
		goto out;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&nn->bl_wq, &wq);

out:
	kfree(msg->data);
}

/*
 * Release meta device
 */
static void nfs4_blk_metadev_release(struct pnfs_block_dev *bdev)
{
	int rv;

	dprintk("%s Releasing\n", __func__);
	rv = nfs4_blkdev_put(bdev->bm_mdev);
	if (rv)
		printk(KERN_ERR "NFS: %s nfs4_blkdev_put returns %d\n",
				__func__, rv);

	dev_remove(bdev->net, bdev->bm_mdev->bd_dev);
}

void bl_free_block_dev(struct pnfs_block_dev *bdev)
{
	if (bdev) {
		if (bdev->bm_mdev) {
			dprintk("%s Removing DM device: %d:%d\n",
				__func__,
				MAJOR(bdev->bm_mdev->bd_dev),
				MINOR(bdev->bm_mdev->bd_dev));
			nfs4_blk_metadev_release(bdev);
		}
		kfree(bdev);
	}
}
