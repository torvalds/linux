// SPDX-License-Identifier: GPL-2.0-or-later
/* getroot.c: get the root dentry for an NFS mount
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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

#include <linux/uaccess.h>

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
		 * d_splice_alias if a parent directory from the same
		 * filesystem gets mounted at a later time.
		 * This again causes shrink_dcache_for_umount_subtree() to
		 * Oops, since the test for IS_ROOT() will fail.
		 */
		spin_lock(&d_inode(sb->s_root)->i_lock);
		spin_lock(&sb->s_root->d_lock);
		hlist_del_init(&sb->s_root->d_u.d_alias);
		spin_unlock(&sb->s_root->d_lock);
		spin_unlock(&d_inode(sb->s_root)->i_lock);
	}
	return 0;
}

/*
 * get an NFS2/NFS3 root dentry from the root filehandle
 */
int nfs_get_root(struct super_block *s, struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	struct nfs_server *server = NFS_SB(s);
	struct nfs_fsinfo fsinfo;
	struct dentry *root;
	struct inode *inode;
	char *name;
	int error = -ENOMEM;

	name = kstrdup(fc->source, GFP_KERNEL);
	if (!name)
		goto out;

	/* get the actual root for this mount */
	fsinfo.fattr = nfs_alloc_fattr();
	if (fsinfo.fattr == NULL)
		goto out_name;

	error = server->nfs_client->rpc_ops->getroot(server, ctx->mntfh, &fsinfo);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		nfs_errorf(fc, "NFS: Couldn't getattr on root");
		goto out_fattr;
	}

	inode = nfs_fhget(s, ctx->mntfh, fsinfo.fattr, NULL);
	if (IS_ERR(inode)) {
		dprintk("nfs_get_root: get root inode failed\n");
		error = PTR_ERR(inode);
		nfs_errorf(fc, "NFS: Couldn't get root inode");
		goto out_fattr;
	}

	error = nfs_superblock_set_dummy_root(s, inode);
	if (error != 0)
		goto out_fattr;

	/* root dentries normally start off anonymous and get spliced in later
	 * if the dentry tree reaches them; however if the dentry already
	 * exists, we'll pick it up at this point and use it as the root
	 */
	root = d_obtain_root(inode);
	if (IS_ERR(root)) {
		dprintk("nfs_get_root: get root dentry failed\n");
		error = PTR_ERR(root);
		nfs_errorf(fc, "NFS: Couldn't get root dentry");
		goto out_fattr;
	}

	security_d_instantiate(root, inode);
	spin_lock(&root->d_lock);
	if (IS_ROOT(root) && !root->d_fsdata &&
	    !(root->d_flags & DCACHE_NFSFS_RENAMED)) {
		root->d_fsdata = name;
		name = NULL;
	}
	spin_unlock(&root->d_lock);
	fc->root = root;
	error = 0;

out_fattr:
	nfs_free_fattr(fsinfo.fattr);
out_name:
	kfree(name);
out:
	return error;
}
