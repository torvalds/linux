// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/nfs/namespace.c
 *
 * Copyright (C) 2005 Trond Myklebust <Trond.Myklebust@netapp.com>
 * - Modified by David Howells <dhowells@redhat.com>
 *
 * NFS namespace
 */

#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/gfp.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/nfs_fs.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/vfs.h>
#include <linux/sunrpc/gss_api.h>
#include "internal.h"
#include "nfs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static void nfs_expire_automounts(struct work_struct *work);

static LIST_HEAD(nfs_automount_list);
static DECLARE_DELAYED_WORK(nfs_automount_task, nfs_expire_automounts);
int nfs_mountpoint_expiry_timeout = 500 * HZ;

/*
 * nfs_path - reconstruct the path given an arbitrary dentry
 * @base - used to return pointer to the end of devname part of path
 * @dentry - pointer to dentry
 * @buffer - result buffer
 * @buflen - length of buffer
 * @flags - options (see below)
 *
 * Helper function for constructing the server pathname
 * by arbitrary hashed dentry.
 *
 * This is mainly for use in figuring out the path on the
 * server side when automounting on top of an existing partition
 * and in generating /proc/mounts and friends.
 *
 * Supported flags:
 * NFS_PATH_CANONICAL: ensure there is exactly one slash after
 *		       the original device (export) name
 *		       (if unset, the original name is returned verbatim)
 */
char *nfs_path(char **p, struct dentry *dentry, char *buffer, ssize_t buflen,
	       unsigned flags)
{
	char *end;
	int namelen;
	unsigned seq;
	const char *base;

rename_retry:
	end = buffer+buflen;
	*--end = '\0';
	buflen--;

	seq = read_seqbegin(&rename_lock);
	rcu_read_lock();
	while (1) {
		spin_lock(&dentry->d_lock);
		if (IS_ROOT(dentry))
			break;
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			goto Elong_unlock;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		spin_unlock(&dentry->d_lock);
		dentry = dentry->d_parent;
	}
	if (read_seqretry(&rename_lock, seq)) {
		spin_unlock(&dentry->d_lock);
		rcu_read_unlock();
		goto rename_retry;
	}
	if ((flags & NFS_PATH_CANONICAL) && *end != '/') {
		if (--buflen < 0) {
			spin_unlock(&dentry->d_lock);
			rcu_read_unlock();
			goto Elong;
		}
		*--end = '/';
	}
	*p = end;
	base = dentry->d_fsdata;
	if (!base) {
		spin_unlock(&dentry->d_lock);
		rcu_read_unlock();
		WARN_ON(1);
		return end;
	}
	namelen = strlen(base);
	if (*end == '/') {
		/* Strip off excess slashes in base string */
		while (namelen > 0 && base[namelen - 1] == '/')
			namelen--;
	}
	buflen -= namelen;
	if (buflen < 0) {
		spin_unlock(&dentry->d_lock);
		rcu_read_unlock();
		goto Elong;
	}
	end -= namelen;
	memcpy(end, base, namelen);
	spin_unlock(&dentry->d_lock);
	rcu_read_unlock();
	return end;
Elong_unlock:
	spin_unlock(&dentry->d_lock);
	rcu_read_unlock();
	if (read_seqretry(&rename_lock, seq))
		goto rename_retry;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}
EXPORT_SYMBOL_GPL(nfs_path);

/*
 * nfs_d_automount - Handle crossing a mountpoint on the server
 * @path - The mountpoint
 *
 * When we encounter a mountpoint on the server, we want to set up
 * a mountpoint on the client too, to prevent inode numbers from
 * colliding, and to allow "df" to work properly.
 * On NFSv4, we also want to allow for the fact that different
 * filesystems may be migrated to different servers in a failover
 * situation, and that different filesystems may want to use
 * different security flavours.
 */
struct vfsmount *nfs_d_automount(struct path *path)
{
	struct nfs_fs_context *ctx;
	struct fs_context *fc;
	struct vfsmount *mnt = ERR_PTR(-ENOMEM);
	struct nfs_server *server = NFS_SERVER(d_inode(path->dentry));
	struct nfs_client *client = server->nfs_client;
	int ret;

	if (IS_ROOT(path->dentry))
		return ERR_PTR(-ESTALE);

	/* Open a new filesystem context, transferring parameters from the
	 * parent superblock, including the network namespace.
	 */
	fc = fs_context_for_submount(&nfs_fs_type, path->dentry);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	ctx = nfs_fc2context(fc);
	ctx->clone_data.dentry	= path->dentry;
	ctx->clone_data.sb	= path->dentry->d_sb;
	ctx->clone_data.fattr	= nfs_alloc_fattr();
	if (!ctx->clone_data.fattr)
		goto out_fc;

	if (fc->net_ns != client->cl_net) {
		put_net(fc->net_ns);
		fc->net_ns = get_net(client->cl_net);
	}

	/* for submounts we want the same server; referrals will reassign */
	memcpy(&ctx->nfs_server.address, &client->cl_addr, client->cl_addrlen);
	ctx->nfs_server.addrlen	= client->cl_addrlen;
	ctx->nfs_server.port	= server->port;

	ctx->version		= client->rpc_ops->version;
	ctx->minorversion	= client->cl_minorversion;
	ctx->nfs_mod		= client->cl_nfs_mod;
	__module_get(ctx->nfs_mod->owner);

	ret = client->rpc_ops->submount(fc, server);
	if (ret < 0) {
		mnt = ERR_PTR(ret);
		goto out_fc;
	}

	up_write(&fc->root->d_sb->s_umount);
	mnt = vfs_create_mount(fc);
	if (IS_ERR(mnt))
		goto out_fc;

	if (nfs_mountpoint_expiry_timeout < 0)
		goto out_fc;

	mntget(mnt); /* prevent immediate expiration */
	mnt_set_expiry(mnt, &nfs_automount_list);
	schedule_delayed_work(&nfs_automount_task, nfs_mountpoint_expiry_timeout);

out_fc:
	put_fs_context(fc);
	return mnt;
}

static int
nfs_namespace_getattr(const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int query_flags)
{
	if (NFS_FH(d_inode(path->dentry))->size != 0)
		return nfs_getattr(path, stat, request_mask, query_flags);
	generic_fillattr(d_inode(path->dentry), stat);
	return 0;
}

static int
nfs_namespace_setattr(struct dentry *dentry, struct iattr *attr)
{
	if (NFS_FH(d_inode(dentry))->size != 0)
		return nfs_setattr(dentry, attr);
	return -EACCES;
}

const struct inode_operations nfs_mountpoint_inode_operations = {
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
};

const struct inode_operations nfs_referral_inode_operations = {
	.getattr	= nfs_namespace_getattr,
	.setattr	= nfs_namespace_setattr,
};

static void nfs_expire_automounts(struct work_struct *work)
{
	struct list_head *list = &nfs_automount_list;

	mark_mounts_for_expiry(list);
	if (!list_empty(list))
		schedule_delayed_work(&nfs_automount_task, nfs_mountpoint_expiry_timeout);
}

void nfs_release_automount_timer(void)
{
	if (list_empty(&nfs_automount_list))
		cancel_delayed_work(&nfs_automount_task);
}

/**
 * nfs_do_submount - set up mountpoint when crossing a filesystem boundary
 * @dentry: parent directory
 * @fh: filehandle for new root dentry
 * @fattr: attributes for new root inode
 * @authflavor: security flavor to use when performing the mount
 *
 */
int nfs_do_submount(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct dentry *dentry = ctx->clone_data.dentry;
	struct nfs_server *server;
	char *buffer, *p;
	int ret;

	/* create a new volume representation */
	server = ctx->nfs_mod->rpc_ops->clone_server(NFS_SB(ctx->clone_data.sb),
						     ctx->mntfh,
						     ctx->clone_data.fattr,
						     ctx->selected_flavor);

	if (IS_ERR(server))
		return PTR_ERR(server);

	ctx->server = server;

	buffer = kmalloc(4096, GFP_USER);
	if (!buffer)
		return -ENOMEM;

	ctx->internal		= true;
	ctx->clone_data.inherited_bsize = ctx->clone_data.sb->s_blocksize_bits;

	p = nfs_devname(dentry, buffer, 4096);
	if (IS_ERR(p)) {
		nfs_errorf(fc, "NFS: Couldn't determine submount pathname");
		ret = PTR_ERR(p);
	} else {
		ret = vfs_parse_fs_string(fc, "source", p, buffer + 4096 - p);
		if (!ret)
			ret = vfs_get_tree(fc);
	}
	kfree(buffer);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_do_submount);

int nfs_submount(struct fs_context *fc, struct nfs_server *server)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct dentry *dentry = ctx->clone_data.dentry;
	struct dentry *parent = dget_parent(dentry);
	int err;

	/* Look it up again to get its attributes */
	err = server->nfs_client->rpc_ops->lookup(d_inode(parent), dentry,
						  ctx->mntfh, ctx->clone_data.fattr,
						  NULL);
	dput(parent);
	if (err != 0)
		return err;

	ctx->selected_flavor = server->client->cl_auth->au_flavor;
	return nfs_do_submount(fc);
}
EXPORT_SYMBOL_GPL(nfs_submount);
