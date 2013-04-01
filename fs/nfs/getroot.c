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
#include <linux/lockd/bind.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/namei.h>
#include <linux/security.h>

#include <asm/uaccess.h>

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
		sb->s_root = d_make_root(inode);
		if (sb->s_root == NULL)
			return -ENOMEM;
		ihold(inode);
		/*
		 * Ensure that this dentry is invisible to d_find_alias().
		 * Otherwise, it may be spliced into the tree by
		 * d_materialise_unique if a parent directory from the same
		 * filesystem gets mounted at a later time.
		 * This again causes shrink_dcache_for_umount_subtree() to
		 * Oops, since the test for IS_ROOT() will fail.
		 */
		spin_lock(&sb->s_root->d_inode->i_lock);
		spin_lock(&sb->s_root->d_lock);
		hlist_del_init(&sb->s_root->d_alias);
		spin_unlock(&sb->s_root->d_lock);
		spin_unlock(&sb->s_root->d_inode->i_lock);
	}
	return 0;
}

/*
 * get an NFS2/NFS3 root dentry from the root filehandle
 */
struct dentry *nfs_get_root(struct super_block *sb, struct nfs_fh *mntfh,
			    const char *devname)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_fsinfo fsinfo;
	struct dentry *ret;
	struct inode *inode;
	void *name = kstrdup(devname, GFP_KERNEL);
	int error;

	if (!name)
		return ERR_PTR(-ENOMEM);

	/* get the actual root for this mount */
	fsinfo.fattr = nfs_alloc_fattr();
	if (fsinfo.fattr == NULL) {
		kfree(name);
		return ERR_PTR(-ENOMEM);
	}

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
	spin_lock(&ret->d_lock);
	if (IS_ROOT(ret) && !(ret->d_flags & DCACHE_NFSFS_RENAMED)) {
		ret->d_fsdata = name;
		name = NULL;
	}
	spin_unlock(&ret->d_lock);
out:
	kfree(name);
	nfs_free_fattr(fsinfo.fattr);
	return ret;
}
