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
#include <linux/mnt_namespace.h>
#include <linux/security.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT
#define NFS_PARANOIA 1

/*
 * get an NFS2/NFS3 root dentry from the root filehandle
 */
struct dentry *nfs_get_root(struct super_block *sb, struct nfs_fh *mntfh)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_fsinfo fsinfo;
	struct nfs_fattr fattr;
	struct dentry *mntroot;
	struct inode *inode;
	int error;

	/* create a dummy root dentry with dummy inode for this superblock */
	if (!sb->s_root) {
		struct nfs_fh dummyfh;
		struct dentry *root;
		struct inode *iroot;

		memset(&dummyfh, 0, sizeof(dummyfh));
		memset(&fattr, 0, sizeof(fattr));
		nfs_fattr_init(&fattr);
		fattr.valid = NFS_ATTR_FATTR;
		fattr.type = NFDIR;
		fattr.mode = S_IFDIR | S_IRUSR | S_IWUSR;
		fattr.nlink = 2;

		iroot = nfs_fhget(sb, &dummyfh, &fattr);
		if (IS_ERR(iroot))
			return ERR_PTR(PTR_ERR(iroot));

		root = d_alloc_root(iroot);
		if (!root) {
			iput(iroot);
			return ERR_PTR(-ENOMEM);
		}

		sb->s_root = root;
	}

	/* get the actual root for this mount */
	fsinfo.fattr = &fattr;

	error = server->nfs_client->rpc_ops->getroot(server, mntfh, &fsinfo);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		return ERR_PTR(error);
	}

	inode = nfs_fhget(sb, mntfh, fsinfo.fattr);
	if (IS_ERR(inode)) {
		dprintk("nfs_get_root: get root inode failed\n");
		return ERR_PTR(PTR_ERR(inode));
	}

	/* root dentries normally start off anonymous and get spliced in later
	 * if the dentry tree reaches them; however if the dentry already
	 * exists, we'll pick it up at this point and use it as the root
	 */
	mntroot = d_alloc_anon(inode);
	if (!mntroot) {
		iput(inode);
		dprintk("nfs_get_root: get root dentry failed\n");
		return ERR_PTR(-ENOMEM);
	}

	security_d_instantiate(mntroot, inode);

	if (!mntroot->d_op)
		mntroot->d_op = server->nfs_client->rpc_ops->dentry_ops;

	return mntroot;
}

#ifdef CONFIG_NFS_V4

/*
 * Do a simple pathwalk from the root FH of the server to the nominated target
 * of the mountpoint
 * - give error on symlinks
 * - give error on ".." occurring in the path
 * - follow traversals
 */
int nfs4_path_walk(struct nfs_server *server,
		   struct nfs_fh *mntfh,
		   const char *path)
{
	struct nfs_fsinfo fsinfo;
	struct nfs_fattr fattr;
	struct nfs_fh lastfh;
	struct qstr name;
	int ret;

	dprintk("--> nfs4_path_walk(,,%s)\n", path);

	fsinfo.fattr = &fattr;
	nfs_fattr_init(&fattr);

	/* Eat leading slashes */
	while (*path == '/')
		path++;

	/* Start by getting the root filehandle from the server */
	ret = server->nfs_client->rpc_ops->getroot(server, mntfh, &fsinfo);
	if (ret < 0) {
		dprintk("nfs4_get_root: getroot error = %d\n", -ret);
		return ret;
	}

	if (fattr.type != NFDIR) {
		printk(KERN_ERR "nfs4_get_root:"
		       " getroot encountered non-directory\n");
		return -ENOTDIR;
	}

	/* FIXME: It is quite valid for the server to return a referral here */
	if (fattr.valid & NFS_ATTR_FATTR_V4_REFERRAL) {
		printk(KERN_ERR "nfs4_get_root:"
		       " getroot obtained referral\n");
		return -EREMOTE;
	}

next_component:
	dprintk("Next: %s\n", path);

	/* extract the next bit of the path */
	if (!*path)
		goto path_walk_complete;

	name.name = path;
	while (*path && *path != '/')
		path++;
	name.len = path - (const char *) name.name;

eat_dot_dir:
	while (*path == '/')
		path++;

	if (path[0] == '.' && (path[1] == '/' || !path[1])) {
		path += 2;
		goto eat_dot_dir;
	}

	/* FIXME: Why shouldn't the user be able to use ".." in the path? */
	if (path[0] == '.' && path[1] == '.' && (path[2] == '/' || !path[2])
	    ) {
		printk(KERN_ERR "nfs4_get_root:"
		       " Mount path contains reference to \"..\"\n");
		return -EINVAL;
	}

	/* lookup the next FH in the sequence */
	memcpy(&lastfh, mntfh, sizeof(lastfh));

	dprintk("LookupFH: %*.*s [%s]\n", name.len, name.len, name.name, path);

	ret = server->nfs_client->rpc_ops->lookupfh(server, &lastfh, &name,
						    mntfh, &fattr);
	if (ret < 0) {
		dprintk("nfs4_get_root: getroot error = %d\n", -ret);
		return ret;
	}

	if (fattr.type != NFDIR) {
		printk(KERN_ERR "nfs4_get_root:"
		       " lookupfh encountered non-directory\n");
		return -ENOTDIR;
	}

	/* FIXME: Referrals are quite valid here too */
	if (fattr.valid & NFS_ATTR_FATTR_V4_REFERRAL) {
		printk(KERN_ERR "nfs4_get_root:"
		       " lookupfh obtained referral\n");
		return -EREMOTE;
	}

	goto next_component;

path_walk_complete:
	memcpy(&server->fsid, &fattr.fsid, sizeof(server->fsid));
	dprintk("<-- nfs4_path_walk() = 0\n");
	return 0;
}

/*
 * get an NFS4 root dentry from the root filehandle
 */
struct dentry *nfs4_get_root(struct super_block *sb, struct nfs_fh *mntfh)
{
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_fattr fattr;
	struct dentry *mntroot;
	struct inode *inode;
	int error;

	dprintk("--> nfs4_get_root()\n");

	/* create a dummy root dentry with dummy inode for this superblock */
	if (!sb->s_root) {
		struct nfs_fh dummyfh;
		struct dentry *root;
		struct inode *iroot;

		memset(&dummyfh, 0, sizeof(dummyfh));
		memset(&fattr, 0, sizeof(fattr));
		nfs_fattr_init(&fattr);
		fattr.valid = NFS_ATTR_FATTR;
		fattr.type = NFDIR;
		fattr.mode = S_IFDIR | S_IRUSR | S_IWUSR;
		fattr.nlink = 2;

		iroot = nfs_fhget(sb, &dummyfh, &fattr);
		if (IS_ERR(iroot))
			return ERR_PTR(PTR_ERR(iroot));

		root = d_alloc_root(iroot);
		if (!root) {
			iput(iroot);
			return ERR_PTR(-ENOMEM);
		}

		sb->s_root = root;
	}

	/* get the info about the server and filesystem */
	error = nfs4_server_capabilities(server, mntfh);
	if (error < 0) {
		dprintk("nfs_get_root: getcaps error = %d\n",
			-error);
		return ERR_PTR(error);
	}

	/* get the actual root for this mount */
	error = server->nfs_client->rpc_ops->getattr(server, mntfh, &fattr);
	if (error < 0) {
		dprintk("nfs_get_root: getattr error = %d\n", -error);
		return ERR_PTR(error);
	}

	inode = nfs_fhget(sb, mntfh, &fattr);
	if (IS_ERR(inode)) {
		dprintk("nfs_get_root: get root inode failed\n");
		return ERR_PTR(PTR_ERR(inode));
	}

	/* root dentries normally start off anonymous and get spliced in later
	 * if the dentry tree reaches them; however if the dentry already
	 * exists, we'll pick it up at this point and use it as the root
	 */
	mntroot = d_alloc_anon(inode);
	if (!mntroot) {
		iput(inode);
		dprintk("nfs_get_root: get root dentry failed\n");
		return ERR_PTR(-ENOMEM);
	}

	security_d_instantiate(mntroot, inode);

	if (!mntroot->d_op)
		mntroot->d_op = server->nfs_client->rpc_ops->dentry_ops;

	dprintk("<-- nfs4_get_root()\n");
	return mntroot;
}

#endif /* CONFIG_NFS_V4 */
