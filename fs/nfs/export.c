// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, Primary Data, Inc. All rights reserved.
 *
 * Tao Peng <bergwolf@primarydata.com>
 */
#include <linux/dcache.h>
#include <linux/exportfs.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>

#include "internal.h"
#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

enum {
	FILEID_HIGH_OFF = 0,	/* inode fileid high */
	FILEID_LOW_OFF,		/* inode fileid low */
	FILE_I_TYPE_OFF,	/* inode type */
	EMBED_FH_OFF		/* embeded server fh */
};


static struct nfs_fh *nfs_exp_embedfh(__u32 *p)
{
	return (struct nfs_fh *)(p + EMBED_FH_OFF);
}

/*
 * Let's break subtree checking for now... otherwise we'll have to embed parent fh
 * but there might not be enough space.
 */
static int
nfs_encode_fh(struct inode *inode, __u32 *p, int *max_len, struct inode *parent)
{
	struct nfs_fh *server_fh = NFS_FH(inode);
	struct nfs_fh *clnt_fh = nfs_exp_embedfh(p);
	size_t fh_size = offsetof(struct nfs_fh, data) + server_fh->size;
	int len = EMBED_FH_OFF + XDR_QUADLEN(fh_size);

	dprintk("%s: max fh len %d inode %p parent %p",
		__func__, *max_len, inode, parent);

	if (*max_len < len || IS_AUTOMOUNT(inode)) {
		dprintk("%s: fh len %d too small, required %d\n",
			__func__, *max_len, len);
		*max_len = len;
		return FILEID_INVALID;
	}

	p[FILEID_HIGH_OFF] = NFS_FILEID(inode) >> 32;
	p[FILEID_LOW_OFF] = NFS_FILEID(inode);
	p[FILE_I_TYPE_OFF] = inode->i_mode & S_IFMT;
	p[len - 1] = 0; /* Padding */
	nfs_copy_fh(clnt_fh, server_fh);
	*max_len = len;
	dprintk("%s: result fh fileid %llu mode %u size %d\n",
		__func__, NFS_FILEID(inode), inode->i_mode, *max_len);
	return *max_len;
}

static struct dentry *
nfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		 int fh_len, int fh_type)
{
	struct nfs4_label *label = NULL;
	struct nfs_fattr *fattr = NULL;
	struct nfs_fh *server_fh = nfs_exp_embedfh(fid->raw);
	size_t fh_size = offsetof(struct nfs_fh, data) + server_fh->size;
	const struct nfs_rpc_ops *rpc_ops;
	struct dentry *dentry;
	struct inode *inode;
	int len = EMBED_FH_OFF + XDR_QUADLEN(fh_size);
	u32 *p = fid->raw;
	int ret;

	/* NULL translates to ESTALE */
	if (fh_len < len || fh_type != len)
		return NULL;

	fattr = nfs_alloc_fattr();
	if (fattr == NULL) {
		dentry = ERR_PTR(-ENOMEM);
		goto out;
	}

	fattr->fileid = ((u64)p[FILEID_HIGH_OFF] << 32) + p[FILEID_LOW_OFF];
	fattr->mode = p[FILE_I_TYPE_OFF];
	fattr->valid |= NFS_ATTR_FATTR_FILEID | NFS_ATTR_FATTR_TYPE;

	dprintk("%s: fileid %llu mode %d\n", __func__, fattr->fileid, fattr->mode);

	inode = nfs_ilookup(sb, fattr, server_fh);
	if (inode)
		goto out_found;

	label = nfs4_label_alloc(NFS_SB(sb), GFP_KERNEL);
	if (IS_ERR(label)) {
		dentry = ERR_CAST(label);
		goto out_free_fattr;
	}

	rpc_ops = NFS_SB(sb)->nfs_client->rpc_ops;
	ret = rpc_ops->getattr(NFS_SB(sb), server_fh, fattr, label, NULL);
	if (ret) {
		dprintk("%s: getattr failed %d\n", __func__, ret);
		trace_nfs_fh_to_dentry(sb, server_fh, fattr->fileid, ret);
		dentry = ERR_PTR(ret);
		goto out_free_label;
	}

	inode = nfs_fhget(sb, server_fh, fattr, label);

out_found:
	dentry = d_obtain_alias(inode);

out_free_label:
	nfs4_label_free(label);
out_free_fattr:
	nfs_free_fattr(fattr);
out:
	return dentry;
}

static struct dentry *
nfs_get_parent(struct dentry *dentry)
{
	int ret;
	struct inode *inode = d_inode(dentry), *pinode;
	struct super_block *sb = inode->i_sb;
	struct nfs_server *server = NFS_SB(sb);
	struct nfs_fattr *fattr = NULL;
	struct nfs4_label *label = NULL;
	struct dentry *parent;
	struct nfs_rpc_ops const *ops = server->nfs_client->rpc_ops;
	struct nfs_fh fh;

	if (!ops->lookupp)
		return ERR_PTR(-EACCES);

	fattr = nfs_alloc_fattr();
	if (fattr == NULL) {
		parent = ERR_PTR(-ENOMEM);
		goto out;
	}

	label = nfs4_label_alloc(server, GFP_KERNEL);
	if (IS_ERR(label)) {
		parent = ERR_CAST(label);
		goto out_free_fattr;
	}

	ret = ops->lookupp(inode, &fh, fattr, label);
	if (ret) {
		parent = ERR_PTR(ret);
		goto out_free_label;
	}

	pinode = nfs_fhget(sb, &fh, fattr, label);
	parent = d_obtain_alias(pinode);
out_free_label:
	nfs4_label_free(label);
out_free_fattr:
	nfs_free_fattr(fattr);
out:
	return parent;
}

const struct export_operations nfs_export_ops = {
	.encode_fh = nfs_encode_fh,
	.fh_to_dentry = nfs_fh_to_dentry,
	.get_parent = nfs_get_parent,
	.flags = EXPORT_OP_NOWCC|EXPORT_OP_NOSUBTREECHK|
		EXPORT_OP_CLOSE_BEFORE_UNLINK|EXPORT_OP_REMOTE_FS|
		EXPORT_OP_NOATOMIC_ATTR,
};
