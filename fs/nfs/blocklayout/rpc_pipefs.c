/*
 *  Copyright (c) 2006,2007 The Regents of the University of Michigan.
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
#include <linux/genhd.h>
#include <linux/blkdev.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

static void bl_dm_remove(struct net *net, dev_t dev)
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
	msg->len = sizeof(bl_msg) + bl_msg.totallen;
	msg->data = kzalloc(msg->len, GFP_NOFS);
	if (!msg->data)
		goto out;

	memset(&bl_umount_request, 0, sizeof(bl_umount_request));
	bl_umount_request.major = MAJOR(dev);
	bl_umount_request.minor = MINOR(dev);

	memcpy(msg->data, &bl_msg, sizeof(bl_msg));
	dataptr = (uint8_t *) msg->data;
	memcpy(&dataptr[sizeof(bl_msg)], &bl_umount_request, sizeof(bl_umount_request));

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

static ssize_t bl_pipe_downcall(struct file *filp, const char __user *src,
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

static void bl_pipe_destroy_msg(struct rpc_pipe_msg *msg)
{
	struct bl_pipe_msg *bl_pipe_msg =
		container_of(msg, struct bl_pipe_msg, msg);

	if (msg->errno >= 0)
		return;
	wake_up(bl_pipe_msg->bl_wq);
}

static const struct rpc_pipe_ops bl_upcall_ops = {
	.upcall		= rpc_pipe_generic_upcall,
	.downcall	= bl_pipe_downcall,
	.destroy_msg	= bl_pipe_destroy_msg,
};

static struct dentry *nfs4blocklayout_register_sb(struct super_block *sb,
					    struct rpc_pipe *pipe)
{
	struct dentry *dir, *dentry;

	dir = rpc_d_lookup_sb(sb, NFS_PIPE_DIRNAME);
	if (dir == NULL)
		return ERR_PTR(-ENOENT);
	dentry = rpc_mkpipe_dentry(dir, "blocklayout", NULL, pipe);
	dput(dir);
	return dentry;
}

static void nfs4blocklayout_unregister_sb(struct super_block *sb,
					  struct rpc_pipe *pipe)
{
	if (pipe->dentry)
		rpc_unlink(pipe->dentry);
}

static int rpc_pipefs_event(struct notifier_block *nb, unsigned long event,
			   void *ptr)
{
	struct super_block *sb = ptr;
	struct net *net = sb->s_fs_info;
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct dentry *dentry;
	int ret = 0;

	if (!try_module_get(THIS_MODULE))
		return 0;

	if (nn->bl_device_pipe == NULL) {
		module_put(THIS_MODULE);
		return 0;
	}

	switch (event) {
	case RPC_PIPEFS_MOUNT:
		dentry = nfs4blocklayout_register_sb(sb, nn->bl_device_pipe);
		if (IS_ERR(dentry)) {
			ret = PTR_ERR(dentry);
			break;
		}
		nn->bl_device_pipe->dentry = dentry;
		break;
	case RPC_PIPEFS_UMOUNT:
		if (nn->bl_device_pipe->dentry)
			nfs4blocklayout_unregister_sb(sb, nn->bl_device_pipe);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	module_put(THIS_MODULE);
	return ret;
}

static struct notifier_block nfs4blocklayout_block = {
	.notifier_call = rpc_pipefs_event,
};

static struct dentry *nfs4blocklayout_register_net(struct net *net,
						   struct rpc_pipe *pipe)
{
	struct super_block *pipefs_sb;
	struct dentry *dentry;

	pipefs_sb = rpc_get_sb_net(net);
	if (!pipefs_sb)
		return NULL;
	dentry = nfs4blocklayout_register_sb(pipefs_sb, pipe);
	rpc_put_sb_net(net);
	return dentry;
}

static void nfs4blocklayout_unregister_net(struct net *net,
					   struct rpc_pipe *pipe)
{
	struct super_block *pipefs_sb;

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		nfs4blocklayout_unregister_sb(pipefs_sb, pipe);
		rpc_put_sb_net(net);
	}
}

static int nfs4blocklayout_net_init(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct dentry *dentry;

	init_waitqueue_head(&nn->bl_wq);
	nn->bl_device_pipe = rpc_mkpipe_data(&bl_upcall_ops, 0);
	if (IS_ERR(nn->bl_device_pipe))
		return PTR_ERR(nn->bl_device_pipe);
	dentry = nfs4blocklayout_register_net(net, nn->bl_device_pipe);
	if (IS_ERR(dentry)) {
		rpc_destroy_pipe_data(nn->bl_device_pipe);
		return PTR_ERR(dentry);
	}
	nn->bl_device_pipe->dentry = dentry;
	return 0;
}

static void nfs4blocklayout_net_exit(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	nfs4blocklayout_unregister_net(net, nn->bl_device_pipe);
	rpc_destroy_pipe_data(nn->bl_device_pipe);
	nn->bl_device_pipe = NULL;
}

static struct pernet_operations nfs4blocklayout_net_ops = {
	.init = nfs4blocklayout_net_init,
	.exit = nfs4blocklayout_net_exit,
};

int __init bl_init_pipefs(void)
{
	int ret;

	ret = rpc_pipefs_notifier_register(&nfs4blocklayout_block);
	if (ret)
		goto out;
	ret = register_pernet_subsys(&nfs4blocklayout_net_ops);
	if (ret)
		goto out_unregister_notifier;
	return 0;

out_unregister_notifier:
	rpc_pipefs_notifier_unregister(&nfs4blocklayout_block);
out:
	return ret;
}

void __exit bl_cleanup_pipefs(void)
{
	rpc_pipefs_notifier_unregister(&nfs4blocklayout_block);
	unregister_pernet_subsys(&nfs4blocklayout_net_ops);
}
