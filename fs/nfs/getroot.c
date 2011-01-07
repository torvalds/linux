/* getroot.c: get the root dentry for an NFS mount
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>
#include <linux/namei.h>
#include <linux/security.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

/*
 * Set the superblock root dentry.
 * Note that this function frees the inode in case of error.
 */
static int nfs_superblock_set_dummy_root(struct super_block *sb, struct inode *inode)
{
	/* The mntroot acts as the dummy root dentry for this superblock */
	if (sb->s_root == NULL) {
		sb->s_root = d_alloc_root(inode);
		if (sb->s_root == NULL) {
			iput(inode);
			return -ENOMEM;
		}
		ihold(inode);
		/*
		 * Ensure that this dentry is invisible to d_find_alias().
		 * Otherwise, it may be spliced into the tree by
		 * d_materialise_unique if a parent directory from the same
		 * filesystem gets mounted at a later time.
		 * This again causes shrink_dcache_for_umount_subtree() to
		 * Oops, since the test for IS_ROOT() will fail.
		 */
		spin_lock(&dcache_inode_lock);
		spin_lock(&sb->s_root->d_lock);
		list_del_init(&sb->s_root->d_alias);
		spin_unlock(&sb->s_root->d_lock);
		spin_unlock(&dcache_inode_lock);
	}
	return 0;
}

/*
 * get an NFS2/NFS3 root dentry from the root filehandle
 */
struct dentry *nfs_get_root(struct super_block *sb, struct nfs_fh *mntfh)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_fsinfo fsinfo;
	struct dentry *ret;
	struct inode *inode;
	int error;

	/* get the actual root for this mount */
	fsinfo.fattr = nfs_alloc_fattr();
	if (fsinfo.fattr == NULL)
		return ERR_PTR(-ENOMEM);

	error = server->nfs_client->rpc_ops->getroot(server, mntfh, &fsinfo);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		ret = ERR_PTR(error);
		goto out;
	}

	inode = nfs_fhget(sb, mntfh, fsinfo.fattr);
	if (IS_ERR(inode)) {
		dprintk("nfs_get_root: get root inode failed\n");
		ret = ERR_CAST(inode);
		goto out;
	}

	error = nfs_superblock_set_dummy_root(sb, inode);
	if (error != 0) {
		ret = ERR_PTR(error);
		goto out;
	}

	/* root dentries normally start off anonymous and get spliced in later
	 * if the dentry tree reaches them; however if the dentry already
	 * exists, we'll pick it up at this point and use it as the root
	 */
	ret = d_obtain_alias(inode);
	if (IS_ERR(ret)) {
		dprintk("nfs_get_root: get root dentry failed\n");
		goto out;
	}

	security_d_instantiate(ret, inode);

	if (ret->d_op == NULL)
		ret->d_op = server->nfs_client->rpc_ops->dentry_ops;
out:
	nfs_free_fattr(fsinfo.fattr);
	return ret;
}

#ifdef CONFIG_NFS_V4

int nfs4_get_rootfh(struct nfs_server *server, struct nfs_fh *mntfh)
{
	struct nfs_fsinfo fsinfo;
	int ret = -ENOMEM;

	dprintk("--> nfs4_get_rootfh()\n");

	fsinfo.fattr = nfs_alloc_fattr();
	if (fsinfo.fattr == NULL)
		goto out;

	/* Start by getting the root filehandle from the server */
	ret = server->nfs_client->rpc_ops->getroot(server, mntfh, &fsinfo);
	if (ret < 0) {
		dprintk("nfs4_get_rootfh: getroot error = %d\n", -ret);
		goto out;
	}

	if (!(fsinfo.fattr->valid & NFS_ATTR_FATTR_TYPE)
			|| !S_ISDIR(fsinfo.fattr->mode)) {
		printk(KERN_ERR "nfs4_get_rootfh:"
		       " getroot encountered non-directory\n");
		ret = -ENOTDIR;
		goto out;
	}

	if (fsinfo.fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) {
		printk(KERN_ERR "nfs4_get_rootfh:"
		       " getroot obtained referral\n");
		ret = -EREMOTE;
		goto out;
	}

	memcpy(&server->fsid, &fsinfo.fattr->fsid, sizeof(server->fsid));
out:
	nfs_free_fattr(fsinfo.fattr);
	dprintk("<-- nfs4_get_rootfh() = %d\n", ret);
	return ret;
}

/*
 * get an NFS4 root dentry from the root filehandle
 */
struct dentry *nfs4_get_root(struct super_block *sb, struct nfs_fh *mntfh)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_fattr *fattr = NULL;
	struct dentry *ret;
	struct inode *inode;
	int error;

	dprintk("--> nfs4_get_root()\n");

	/* get the info about the server and filesystem */
	error = nfs4_server_capabilities(server, mntfh);
	if (error < 0) {
		dprintk("nfs_get_root: getcaps error = %d\n",
			-error);
		return ERR_PTR(error);
	}

	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		return ERR_PTR(-ENOMEM);;

	/* get the actual root for this mount */
	error = server->nfs_client->rpc_ops->getattr(server, mntfh, fattr);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		ret = ERR_PTR(error);
		goto out;
	}

	inode = nfs_fhget(sb, mntfh, fattr);
	if (IS_ERR(inode)) {
		dprintk("nfs_get_root: get root inode failed\n");
		ret = ERR_CAST(inode);
		goto out;
	}

	error = nfs_superblock_set_dummy_root(sb, inode);
	if (error != 0) {
		ret = ERR_PTR(error);
		goto out;
	}

	/* root dentries normally start off anonymous and get spliced in later
	 * if the dentry tree reaches them; however if the dentry already
	 * exists, we'll pick it up at this point and use it as the root
	 */
	ret = d_obtain_alias(inode);
	if (IS_ERR(ret)) {
		dprintk("nfs_get_root: get root dentry failed\n");
		goto out;
	}

	security_d_instantiate(ret, inode);

	if (ret->d_op == NULL)
		ret->d_op = server->nfs_client->rpc_ops->dentry_ops;

out:
	nfs_free_fattr(fattr);
	dprintk("<-- nfs4_get_root()\n");
	return ret;
}

#endif /* CONFIG_NFS_V4 */
