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
#include <linux/blkdev.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY         NFSDBG_PNFS_LD

static void
nfs4_encode_simple(__be32 *p, struct pnfs_block_volume *b)
{
	int i;

	*p++ = cpu_to_be32(1);
	*p++ = cpu_to_be32(b->type);
	*p++ = cpu_to_be32(b->simple.nr_sigs);
	for (i = 0; i < b->simple.nr_sigs; i++) {
		p = xdr_encode_hyper(p, b->simple.sigs[i].offset);
		p = xdr_encode_opaque(p, b->simple.sigs[i].sig,
					 b->simple.sigs[i].sig_len);
	}
}

dev_t
bl_resolve_deviceid(struct nfs_server *server, struct pnfs_block_volume *b,
		gfp_t gfp_mask)
{
	struct net *net = server->nfs_client->cl_net;
	struct nfs_net *nn = net_generic(net, nfs_net_id);
	struct bl_dev_msg *reply = &nn->bl_mount_reply;
	struct bl_pipe_msg bl_pipe_msg;
	struct rpc_pipe_msg *msg = &bl_pipe_msg.msg;
	struct bl_msg_hdr *bl_msg;
	DECLARE_WAITQUEUE(wq, current);
	dev_t dev = 0;
	int rc;

	dprintk("%s CREATING PIPEFS MESSAGE\n", __func__);

	mutex_lock(&nn->bl_mutex);
	bl_pipe_msg.bl_wq = &nn->bl_wq;

	b->simple.len += 4;	/* single volume */
	if (b->simple.len > PAGE_SIZE)
		goto out_unlock;

	memset(msg, 0, sizeof(*msg));
	msg->len = sizeof(*bl_msg) + b->simple.len;
	msg->data = kzalloc(msg->len, gfp_mask);
	if (!msg->data)
		goto out_unlock;

	bl_msg = msg->data;
	bl_msg->type = BL_DEVICE_MOUNT;
	bl_msg->totallen = b->simple.len;
	nfs4_encode_simple(msg->data + sizeof(*bl_msg), b);

	dprintk("%s CALLING USERSPACE DAEMON\n", __func__);
	add_wait_queue(&nn->bl_wq, &wq);
	rc = rpc_queue_upcall(nn->bl_device_pipe, msg);
	if (rc < 0) {
		remove_wait_queue(&nn->bl_wq, &wq);
		goto out_free_data;
	}

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule();
	remove_wait_queue(&nn->bl_wq, &wq);

	if (reply->status != BL_DEVICE_REQUEST_PROC) {
		printk(KERN_WARNING "%s failed to decode device: %d\n",
			__func__, reply->status);
		goto out_free_data;
	}

	dev = MKDEV(reply->major, reply->minor);
out_free_data:
	kfree(msg->data);
out_unlock:
	mutex_unlock(&nn->bl_mutex);
	return dev;
}

static ssize_t bl_pipe_downcall(struct file *filp, const char __user *src,
			 size_t mlen)
{
	struct nfs_net *nn = net_generic(file_inode(filp)->i_sb->s_fs_info,
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

	mutex_init(&nn->bl_mutex);
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

void bl_cleanup_pipefs(void)
{
	rpc_pipefs_notifier_unregister(&nfs4blocklayout_block);
	unregister_pernet_subsys(&nfs4blocklayout_net_ops);
}
