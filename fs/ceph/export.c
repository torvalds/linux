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

/*
 * fh for snapped inode
 */
struct ceph_nfs_snapfh {
	u64 ino;
	u64 snapid;
	u64 parent_ino;
	u32 hash;
} __attribute__ ((packed));

static int ceph_encode_snapfh(struct inode *inode, u32 *rawfh, int *max_len,
			      struct inode *parent_inode)
{
	static const int snap_handle_length =
		sizeof(struct ceph_nfs_snapfh) >> 2;
	struct ceph_nfs_snapfh *sfh = (void *)rawfh;
	u64 snapid = ceph_snap(inode);
	int ret;
	bool no_parent = true;

	if (*max_len < snap_handle_length) {
		*max_len = snap_handle_length;
		ret = FILEID_INVALID;
		goto out;
	}

	ret =  -EINVAL;
	if (snapid != CEPH_SNAPDIR) {
		struct inode *dir;
		struct dentry *dentry = d_find_alias(inode);
		if (!dentry)
			goto out;

		rcu_read_lock();
		dir = d_inode_rcu(dentry->d_parent);
		if (ceph_snap(dir) != CEPH_SNAPDIR) {
			sfh->parent_ino = ceph_ino(dir);
			sfh->hash = ceph_dentry_hash(dir, dentry);
			no_parent = false;
		}
		rcu_read_unlock();
		dput(dentry);
	}

	if (no_parent) {
		if (!S_ISDIR(inode->i_mode))
			goto out;
		sfh->parent_ino = sfh->ino;
		sfh->hash = 0;
	}
	sfh->ino = ceph_ino(inode);
	sfh->snapid = snapid;

	*max_len = snap_handle_length;
	ret = FILEID_BTRFS_WITH_PARENT;
out:
	dout("encode_snapfh %llx.%llx ret=%d\n", ceph_vinop(inode), ret);
	return ret;
}

static int ceph_encode_fh(struct inode *inode, u32 *rawfh, int *max_len,
			  struct inode *parent_inode)
{
	static const int handle_length =
		sizeof(struct ceph_nfs_fh) >> 2;
	static const int connected_handle_length =
		sizeof(struct ceph_nfs_confh) >> 2;
	int type;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return ceph_encode_snapfh(inode, rawfh, max_len, parent_inode);

	if (parent_inode && (*max_len < connected_handle_length)) {
		*max_len = connected_handle_length;
		return FILEID_INVALID;
	} else if (*max_len < handle_length) {
		*max_len = handle_length;
		return FILEID_INVALID;
	}

	if (parent_inode) {
		struct ceph_nfs_confh *cfh = (void *)rawfh;
		dout("encode_fh %llx with parent %llx\n",
		     ceph_ino(inode), ceph_ino(parent_inode));
		cfh->ino = ceph_ino(inode);
		cfh->parent_ino = ceph_ino(parent_inode);
		*max_len = connected_handle_length;
		type = FILEID_INO32_GEN_PARENT;
	} else {
		struct ceph_nfs_fh *fh = (void *)rawfh;
		dout("encode_fh %llx\n", ceph_ino(inode));
		fh->ino = ceph_ino(inode);
		*max_len = handle_length;
		type = FILEID_INO32_GEN;
	}
	return type;
}

static struct inode *__lookup_inode(struct super_block *sb, u64 ino)
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
		req->r_args.lookupino.mask = cpu_to_le32(mask);

		req->r_ino1 = vino;
		req->r_num_caps = 1;
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		inode = req->r_target_inode;
		if (inode)
			ihold(inode);
		ceph_mdsc_put_request(req);
		if (!inode)
			return err < 0 ? ERR_PTR(err) : ERR_PTR(-ESTALE);
	}
	return inode;
}

struct inode *ceph_lookup_inode(struct super_block *sb, u64 ino)
{
	struct inode *inode = __lookup_inode(sb, ino);
	if (IS_ERR(inode))
		return inode;
	if (inode->i_nlink == 0) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *__fh_to_dentry(struct super_block *sb, u64 ino)
{
	struct inode *inode = __lookup_inode(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (inode->i_nlink == 0) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(inode);
}

static struct dentry *__snapfh_to_dentry(struct super_block *sb,
					  struct ceph_nfs_snapfh *sfh,
					  bool want_parent)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(sb)->mdsc;
	struct ceph_mds_request *req;
	struct inode *inode;
	struct ceph_vino vino;
	int mask;
	int err;
	bool unlinked = false;

	if (want_parent) {
		vino.ino = sfh->parent_ino;
		if (sfh->snapid == CEPH_SNAPDIR)
			vino.snap = CEPH_NOSNAP;
		else if (sfh->ino == sfh->parent_ino)
			vino.snap = CEPH_SNAPDIR;
		else
			vino.snap = sfh->snapid;
	} else {
		vino.ino = sfh->ino;
		vino.snap = sfh->snapid;
	}
	inode = ceph_find_inode(sb, vino);
	if (inode)
		return d_obtain_alias(inode);

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPINO,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);

	mask = CEPH_STAT_CAP_INODE;
	if (ceph_security_xattr_wanted(d_inode(sb->s_root)))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.lookupino.mask = cpu_to_le32(mask);
	if (vino.snap < CEPH_NOSNAP) {
		req->r_args.lookupino.snapid = cpu_to_le64(vino.snap);
		if (!want_parent && sfh->ino != sfh->parent_ino) {
			req->r_args.lookupino.parent =
					cpu_to_le64(sfh->parent_ino);
			req->r_args.lookupino.hash =
					cpu_to_le32(sfh->hash);
		}
	}

	req->r_ino1 = vino;
	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	inode = req->r_target_inode;
	if (inode) {
		if (vino.snap == CEPH_SNAPDIR) {
			if (inode->i_nlink == 0)
				unlinked = true;
			inode = ceph_get_snapdir(inode);
		} else if (ceph_snap(inode) == vino.snap) {
			ihold(inode);
		} else {
			/* mds does not support lookup snapped inode */
			err = -EOPNOTSUPP;
			inode = NULL;
		}
	}
	ceph_mdsc_put_request(req);

	if (want_parent) {
		dout("snapfh_to_parent %llx.%llx\n err=%d\n",
		     vino.ino, vino.snap, err);
	} else {
		dout("snapfh_to_dentry %llx.%llx parent %llx hash %x err=%d",
		      vino.ino, vino.snap, sfh->parent_ino, sfh->hash, err);
	}
	if (!inode)
		return ERR_PTR(-ESTALE);
	/* see comments in ceph_get_parent() */
	return unlinked ? d_obtain_root(inode) : d_obtain_alias(inode);
}

/*
 * convert regular fh to dentry
 */
static struct dentry *ceph_fh_to_dentry(struct super_block *sb,
					struct fid *fid,
					int fh_len, int fh_type)
{
	struct ceph_nfs_fh *fh = (void *)fid->raw;

	if (fh_type == FILEID_BTRFS_WITH_PARENT) {
		struct ceph_nfs_snapfh *sfh = (void *)fid->raw;
		return __snapfh_to_dentry(sb, sfh, false);
	}

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
	if (err) {
		ceph_mdsc_put_request(req);
		return ERR_PTR(err);
	}

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
	struct inode *inode = d_inode(child);
	struct dentry *dn;

	if (ceph_snap(inode) != CEPH_NOSNAP) {
		struct inode* dir;
		bool unlinked = false;
		/* do not support non-directory */
		if (!d_is_dir(child)) {
			dn = ERR_PTR(-EINVAL);
			goto out;
		}
		dir = __lookup_inode(inode->i_sb, ceph_ino(inode));
		if (IS_ERR(dir)) {
			dn = ERR_CAST(dir);
			goto out;
		}
		/* There can be multiple paths to access snapped inode.
		 * For simplicity, treat snapdir of head inode as parent */
		if (ceph_snap(inode) != CEPH_SNAPDIR) {
			struct inode *snapdir = ceph_get_snapdir(dir);
			if (dir->i_nlink == 0)
				unlinked = true;
			iput(dir);
			if (IS_ERR(snapdir)) {
				dn = ERR_CAST(snapdir);
				goto out;
			}
			dir = snapdir;
		}
		/* If directory has already been deleted, futher get_parent
		 * will fail. Do not mark snapdir dentry as disconnected,
		 * this prevent exportfs from doing futher get_parent. */
		if (unlinked)
			dn = d_obtain_root(dir);
		else
			dn = d_obtain_alias(dir);
	} else {
		dn = __get_parent(child->d_sb, child, 0);
	}
out:
	dout("get_parent %p ino %llx.%llx err=%ld\n",
	     child, ceph_vinop(inode), (long)PTR_ERR_OR_ZERO(dn));
	return dn;
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

	if (fh_type == FILEID_BTRFS_WITH_PARENT) {
		struct ceph_nfs_snapfh *sfh = (void *)fid->raw;
		return __snapfh_to_dentry(sb, sfh, true);
	}

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

static int __get_snap_name(struct dentry *parent, char *name,
			   struct dentry *child)
{
	struct inode *inode = d_inode(child);
	struct inode *dir = d_inode(parent);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	struct ceph_mds_request *req = NULL;
	char *last_name = NULL;
	unsigned next_offset = 2;
	int err = -EINVAL;

	if (ceph_ino(inode) != ceph_ino(dir))
		goto out;
	if (ceph_snap(inode) == CEPH_SNAPDIR) {
		if (ceph_snap(dir) == CEPH_NOSNAP) {
			strcpy(name, fsc->mount_options->snapdir_name);
			err = 0;
		}
		goto out;
	}
	if (ceph_snap(dir) != CEPH_SNAPDIR)
		goto out;

	while (1) {
		struct ceph_mds_reply_info_parsed *rinfo;
		struct ceph_mds_reply_dir_entry *rde;
		int i;

		req = ceph_mdsc_create_request(fsc->mdsc, CEPH_MDS_OP_LSSNAP,
					       USE_AUTH_MDS);
		if (IS_ERR(req)) {
			err = PTR_ERR(req);
			req = NULL;
			goto out;
		}
		err = ceph_alloc_readdir_reply_buffer(req, inode);
		if (err)
			goto out;

		req->r_direct_mode = USE_AUTH_MDS;
		req->r_readdir_offset = next_offset;
		req->r_args.readdir.flags =
				cpu_to_le16(CEPH_READDIR_REPLY_BITFLAGS);
		if (last_name) {
			req->r_path2 = last_name;
			last_name = NULL;
		}

		req->r_inode = dir;
		ihold(dir);
		req->r_dentry = dget(parent);

		inode_lock(dir);
		err = ceph_mdsc_do_request(fsc->mdsc, NULL, req);
		inode_unlock(dir);

		if (err < 0)
			goto out;

		rinfo = &req->r_reply_info;
		for (i = 0; i < rinfo->dir_nr; i++) {
			rde = rinfo->dir_entries + i;
			BUG_ON(!rde->inode.in);
			if (ceph_snap(inode) ==
			    le64_to_cpu(rde->inode.in->snapid)) {
				memcpy(name, rde->name, rde->name_len);
				name[rde->name_len] = '\0';
				err = 0;
				goto out;
			}
		}

		if (rinfo->dir_end)
			break;

		BUG_ON(rinfo->dir_nr <= 0);
		rde = rinfo->dir_entries + (rinfo->dir_nr - 1);
		next_offset += rinfo->dir_nr;
		last_name = kstrndup(rde->name, rde->name_len, GFP_KERNEL);
		if (!last_name) {
			err = -ENOMEM;
			goto out;
		}

		ceph_mdsc_put_request(req);
		req = NULL;
	}
	err = -ENOENT;
out:
	if (req)
		ceph_mdsc_put_request(req);
	kfree(last_name);
	dout("get_snap_name %p ino %llx.%llx err=%d\n",
	     child, ceph_vinop(inode), err);
	return err;
}

static int ceph_get_name(struct dentry *parent, char *name,
			 struct dentry *child)
{
	struct ceph_mds_client *mdsc;
	struct ceph_mds_request *req;
	struct inode *inode = d_inode(child);
	int err;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return __get_snap_name(parent, name, child);

	mdsc = ceph_inode_to_client(inode)->mdsc;
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPNAME,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);

	inode_lock(d_inode(parent));

	req->r_inode = inode;
	ihold(inode);
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
		     child, ceph_vinop(inode), name);
	} else {
		dout("get_name %p ino %llx.%llx err %d\n",
		     child, ceph_vinop(inode), err);
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
