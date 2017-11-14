// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/exportfs.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "super.h"
#include "mds_client.h"

/*
 * Basic fh
 */
struct ceph_nfs_fh {
	u64 ino;
} __attribute__ ((packed));

/*
 * Larger fh that includes parent ino.
 */
struct ceph_nfs_confh {
	u64 ino, parent_ino;
} __attribute__ ((packed));

static int ceph_encode_fh(struct inode *inode, u32 *rawfh, int *max_len,
			  struct inode *parent_inode)
{
	int type;
	struct ceph_nfs_fh *fh = (void *)rawfh;
	struct ceph_nfs_confh *cfh = (void *)rawfh;
	int connected_handle_length = sizeof(*cfh)/4;
	int handle_length = sizeof(*fh)/4;

	/* don't re-export snaps */
	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EINVAL;

	if (parent_inode && (*max_len < connected_handle_length)) {
		*max_len = connected_handle_length;
		return FILEID_INVALID;
	} else if (*max_len < handle_length) {
		*max_len = handle_length;
		return FILEID_INVALID;
	}

	if (parent_inode) {
		dout("encode_fh %llx with parent %llx\n",
		     ceph_ino(inode), ceph_ino(parent_inode));
		cfh->ino = ceph_ino(inode);
		cfh->parent_ino = ceph_ino(parent_inode);
		*max_len = connected_handle_length;
		type = FILEID_INO32_GEN_PARENT;
	} else {
		dout("encode_fh %llx\n", ceph_ino(inode));
		fh->ino = ceph_ino(inode);
		*max_len = handle_length;
		type = FILEID_INO32_GEN;
	}
	return type;
}

static struct dentry *__fh_to_dentry(struct super_block *sb, u64 ino)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(sb)->mdsc;
	struct inode *inode;
	struct ceph_vino vino;
	int err;

	vino.ino = ino;
	vino.snap = CEPH_NOSNAP;
	inode = ceph_find_inode(sb, vino);
	if (!inode) {
		struct ceph_mds_request *req;
		int mask;

		req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPINO,
					       USE_ANY_MDS);
		if (IS_ERR(req))
			return ERR_CAST(req);

		mask = CEPH_STAT_CAP_INODE;
		if (ceph_security_xattr_wanted(d_inode(sb->s_root)))
			mask |= CEPH_CAP_XATTR_SHARED;
		req->r_args.getattr.mask = cpu_to_le32(mask);

		req->r_ino1 = vino;
		req->r_num_caps = 1;
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		inode = req->r_target_inode;
		if (inode)
			ihold(inode);
		ceph_mdsc_put_request(req);
		if (!inode)
			return ERR_PTR(-ESTALE);
		if (inode->i_nlink == 0) {
			iput(inode);
			return ERR_PTR(-ESTALE);
		}
	}

	return d_obtain_alias(inode);
}

/*
 * convert regular fh to dentry
 */
static struct dentry *ceph_fh_to_dentry(struct super_block *sb,
					struct fid *fid,
					int fh_len, int fh_type)
{
	struct ceph_nfs_fh *fh = (void *)fid->raw;

	if (fh_type != FILEID_INO32_GEN  &&
	    fh_type != FILEID_INO32_GEN_PARENT)
		return NULL;
	if (fh_len < sizeof(*fh) / 4)
		return NULL;

	dout("fh_to_dentry %llx\n", fh->ino);
	return __fh_to_dentry(sb, fh->ino);
}

static struct dentry *__get_parent(struct super_block *sb,
				   struct dentry *child, u64 ino)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(sb)->mdsc;
	struct ceph_mds_request *req;
	struct inode *inode;
	int mask;
	int err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPPARENT,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);

	if (child) {
		req->r_inode = d_inode(child);
		ihold(d_inode(child));
	} else {
		req->r_ino1 = (struct ceph_vino) {
			.ino = ino,
			.snap = CEPH_NOSNAP,
		};
	}

	mask = CEPH_STAT_CAP_INODE;
	if (ceph_security_xattr_wanted(d_inode(sb->s_root)))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.getattr.mask = cpu_to_le32(mask);

	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	inode = req->r_target_inode;
	if (inode)
		ihold(inode);
	ceph_mdsc_put_request(req);
	if (!inode)
		return ERR_PTR(-ENOENT);

	return d_obtain_alias(inode);
}

static struct dentry *ceph_get_parent(struct dentry *child)
{
	/* don't re-export snaps */
	if (ceph_snap(d_inode(child)) != CEPH_NOSNAP)
		return ERR_PTR(-EINVAL);

	dout("get_parent %p ino %llx.%llx\n",
	     child, ceph_vinop(d_inode(child)));
	return __get_parent(child->d_sb, child, 0);
}

/*
 * convert regular fh to parent
 */
static struct dentry *ceph_fh_to_parent(struct super_block *sb,
					struct fid *fid,
					int fh_len, int fh_type)
{
	struct ceph_nfs_confh *cfh = (void *)fid->raw;
	struct dentry *dentry;

	if (fh_type != FILEID_INO32_GEN_PARENT)
		return NULL;
	if (fh_len < sizeof(*cfh) / 4)
		return NULL;

	dout("fh_to_parent %llx\n", cfh->parent_ino);
	dentry = __get_parent(sb, NULL, cfh->ino);
	if (unlikely(dentry == ERR_PTR(-ENOENT)))
		dentry = __fh_to_dentry(sb, cfh->parent_ino);
	return dentry;
}

static int ceph_get_name(struct dentry *parent, char *name,
			 struct dentry *child)
{
	struct ceph_mds_client *mdsc;
	struct ceph_mds_request *req;
	int err;

	mdsc = ceph_inode_to_client(d_inode(child))->mdsc;
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPNAME,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);

	inode_lock(d_inode(parent));

	req->r_inode = d_inode(child);
	ihold(d_inode(child));
	req->r_ino2 = ceph_vino(d_inode(parent));
	req->r_parent = d_inode(parent);
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_num_caps = 2;
	err = ceph_mdsc_do_request(mdsc, NULL, req);

	inode_unlock(d_inode(parent));

	if (!err) {
		struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
		memcpy(name, rinfo->dname, rinfo->dname_len);
		name[rinfo->dname_len] = 0;
		dout("get_name %p ino %llx.%llx name %s\n",
		     child, ceph_vinop(d_inode(child)), name);
	} else {
		dout("get_name %p ino %llx.%llx err %d\n",
		     child, ceph_vinop(d_inode(child)), err);
	}

	ceph_mdsc_put_request(req);
	return err;
}

const struct export_operations ceph_export_ops = {
	.encode_fh = ceph_encode_fh,
	.fh_to_dentry = ceph_fh_to_dentry,
	.fh_to_parent = ceph_fh_to_parent,
	.get_parent = ceph_get_parent,
	.get_name = ceph_get_name,
};
