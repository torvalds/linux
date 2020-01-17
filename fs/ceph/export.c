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
	u64 iyes;
} __attribute__ ((packed));

/*
 * Larger fh that includes parent iyes.
 */
struct ceph_nfs_confh {
	u64 iyes, parent_iyes;
} __attribute__ ((packed));

/*
 * fh for snapped iyesde
 */
struct ceph_nfs_snapfh {
	u64 iyes;
	u64 snapid;
	u64 parent_iyes;
	u32 hash;
} __attribute__ ((packed));

static int ceph_encode_snapfh(struct iyesde *iyesde, u32 *rawfh, int *max_len,
			      struct iyesde *parent_iyesde)
{
	static const int snap_handle_length =
		sizeof(struct ceph_nfs_snapfh) >> 2;
	struct ceph_nfs_snapfh *sfh = (void *)rawfh;
	u64 snapid = ceph_snap(iyesde);
	int ret;
	bool yes_parent = true;

	if (*max_len < snap_handle_length) {
		*max_len = snap_handle_length;
		ret = FILEID_INVALID;
		goto out;
	}

	ret =  -EINVAL;
	if (snapid != CEPH_SNAPDIR) {
		struct iyesde *dir;
		struct dentry *dentry = d_find_alias(iyesde);
		if (!dentry)
			goto out;

		rcu_read_lock();
		dir = d_iyesde_rcu(dentry->d_parent);
		if (ceph_snap(dir) != CEPH_SNAPDIR) {
			sfh->parent_iyes = ceph_iyes(dir);
			sfh->hash = ceph_dentry_hash(dir, dentry);
			yes_parent = false;
		}
		rcu_read_unlock();
		dput(dentry);
	}

	if (yes_parent) {
		if (!S_ISDIR(iyesde->i_mode))
			goto out;
		sfh->parent_iyes = sfh->iyes;
		sfh->hash = 0;
	}
	sfh->iyes = ceph_iyes(iyesde);
	sfh->snapid = snapid;

	*max_len = snap_handle_length;
	ret = FILEID_BTRFS_WITH_PARENT;
out:
	dout("encode_snapfh %llx.%llx ret=%d\n", ceph_viyesp(iyesde), ret);
	return ret;
}

static int ceph_encode_fh(struct iyesde *iyesde, u32 *rawfh, int *max_len,
			  struct iyesde *parent_iyesde)
{
	static const int handle_length =
		sizeof(struct ceph_nfs_fh) >> 2;
	static const int connected_handle_length =
		sizeof(struct ceph_nfs_confh) >> 2;
	int type;

	if (ceph_snap(iyesde) != CEPH_NOSNAP)
		return ceph_encode_snapfh(iyesde, rawfh, max_len, parent_iyesde);

	if (parent_iyesde && (*max_len < connected_handle_length)) {
		*max_len = connected_handle_length;
		return FILEID_INVALID;
	} else if (*max_len < handle_length) {
		*max_len = handle_length;
		return FILEID_INVALID;
	}

	if (parent_iyesde) {
		struct ceph_nfs_confh *cfh = (void *)rawfh;
		dout("encode_fh %llx with parent %llx\n",
		     ceph_iyes(iyesde), ceph_iyes(parent_iyesde));
		cfh->iyes = ceph_iyes(iyesde);
		cfh->parent_iyes = ceph_iyes(parent_iyesde);
		*max_len = connected_handle_length;
		type = FILEID_INO32_GEN_PARENT;
	} else {
		struct ceph_nfs_fh *fh = (void *)rawfh;
		dout("encode_fh %llx\n", ceph_iyes(iyesde));
		fh->iyes = ceph_iyes(iyesde);
		*max_len = handle_length;
		type = FILEID_INO32_GEN;
	}
	return type;
}

static struct iyesde *__lookup_iyesde(struct super_block *sb, u64 iyes)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(sb)->mdsc;
	struct iyesde *iyesde;
	struct ceph_viyes viyes;
	int err;

	viyes.iyes = iyes;
	viyes.snap = CEPH_NOSNAP;
	iyesde = ceph_find_iyesde(sb, viyes);
	if (!iyesde) {
		struct ceph_mds_request *req;
		int mask;

		req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPINO,
					       USE_ANY_MDS);
		if (IS_ERR(req))
			return ERR_CAST(req);

		mask = CEPH_STAT_CAP_INODE;
		if (ceph_security_xattr_wanted(d_iyesde(sb->s_root)))
			mask |= CEPH_CAP_XATTR_SHARED;
		req->r_args.lookupiyes.mask = cpu_to_le32(mask);

		req->r_iyes1 = viyes;
		req->r_num_caps = 1;
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		iyesde = req->r_target_iyesde;
		if (iyesde)
			ihold(iyesde);
		ceph_mdsc_put_request(req);
		if (!iyesde)
			return err < 0 ? ERR_PTR(err) : ERR_PTR(-ESTALE);
	}
	return iyesde;
}

struct iyesde *ceph_lookup_iyesde(struct super_block *sb, u64 iyes)
{
	struct iyesde *iyesde = __lookup_iyesde(sb, iyes);
	if (IS_ERR(iyesde))
		return iyesde;
	if (iyesde->i_nlink == 0) {
		iput(iyesde);
		return ERR_PTR(-ESTALE);
	}
	return iyesde;
}

static struct dentry *__fh_to_dentry(struct super_block *sb, u64 iyes)
{
	struct iyesde *iyesde = __lookup_iyesde(sb, iyes);
	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);
	if (iyesde->i_nlink == 0) {
		iput(iyesde);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(iyesde);
}

static struct dentry *__snapfh_to_dentry(struct super_block *sb,
					  struct ceph_nfs_snapfh *sfh,
					  bool want_parent)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(sb)->mdsc;
	struct ceph_mds_request *req;
	struct iyesde *iyesde;
	struct ceph_viyes viyes;
	int mask;
	int err;
	bool unlinked = false;

	if (want_parent) {
		viyes.iyes = sfh->parent_iyes;
		if (sfh->snapid == CEPH_SNAPDIR)
			viyes.snap = CEPH_NOSNAP;
		else if (sfh->iyes == sfh->parent_iyes)
			viyes.snap = CEPH_SNAPDIR;
		else
			viyes.snap = sfh->snapid;
	} else {
		viyes.iyes = sfh->iyes;
		viyes.snap = sfh->snapid;
	}
	iyesde = ceph_find_iyesde(sb, viyes);
	if (iyesde)
		return d_obtain_alias(iyesde);

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPINO,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);

	mask = CEPH_STAT_CAP_INODE;
	if (ceph_security_xattr_wanted(d_iyesde(sb->s_root)))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.lookupiyes.mask = cpu_to_le32(mask);
	if (viyes.snap < CEPH_NOSNAP) {
		req->r_args.lookupiyes.snapid = cpu_to_le64(viyes.snap);
		if (!want_parent && sfh->iyes != sfh->parent_iyes) {
			req->r_args.lookupiyes.parent =
					cpu_to_le64(sfh->parent_iyes);
			req->r_args.lookupiyes.hash =
					cpu_to_le32(sfh->hash);
		}
	}

	req->r_iyes1 = viyes;
	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	iyesde = req->r_target_iyesde;
	if (iyesde) {
		if (viyes.snap == CEPH_SNAPDIR) {
			if (iyesde->i_nlink == 0)
				unlinked = true;
			iyesde = ceph_get_snapdir(iyesde);
		} else if (ceph_snap(iyesde) == viyes.snap) {
			ihold(iyesde);
		} else {
			/* mds does yest support lookup snapped iyesde */
			err = -EOPNOTSUPP;
			iyesde = NULL;
		}
	}
	ceph_mdsc_put_request(req);

	if (want_parent) {
		dout("snapfh_to_parent %llx.%llx\n err=%d\n",
		     viyes.iyes, viyes.snap, err);
	} else {
		dout("snapfh_to_dentry %llx.%llx parent %llx hash %x err=%d",
		      viyes.iyes, viyes.snap, sfh->parent_iyes, sfh->hash, err);
	}
	if (!iyesde)
		return ERR_PTR(-ESTALE);
	/* see comments in ceph_get_parent() */
	return unlinked ? d_obtain_root(iyesde) : d_obtain_alias(iyesde);
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

	dout("fh_to_dentry %llx\n", fh->iyes);
	return __fh_to_dentry(sb, fh->iyes);
}

static struct dentry *__get_parent(struct super_block *sb,
				   struct dentry *child, u64 iyes)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(sb)->mdsc;
	struct ceph_mds_request *req;
	struct iyesde *iyesde;
	int mask;
	int err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPPARENT,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);

	if (child) {
		req->r_iyesde = d_iyesde(child);
		ihold(d_iyesde(child));
	} else {
		req->r_iyes1 = (struct ceph_viyes) {
			.iyes = iyes,
			.snap = CEPH_NOSNAP,
		};
	}

	mask = CEPH_STAT_CAP_INODE;
	if (ceph_security_xattr_wanted(d_iyesde(sb->s_root)))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.getattr.mask = cpu_to_le32(mask);

	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	iyesde = req->r_target_iyesde;
	if (iyesde)
		ihold(iyesde);
	ceph_mdsc_put_request(req);
	if (!iyesde)
		return ERR_PTR(-ENOENT);

	return d_obtain_alias(iyesde);
}

static struct dentry *ceph_get_parent(struct dentry *child)
{
	struct iyesde *iyesde = d_iyesde(child);
	struct dentry *dn;

	if (ceph_snap(iyesde) != CEPH_NOSNAP) {
		struct iyesde* dir;
		bool unlinked = false;
		/* do yest support yesn-directory */
		if (!d_is_dir(child)) {
			dn = ERR_PTR(-EINVAL);
			goto out;
		}
		dir = __lookup_iyesde(iyesde->i_sb, ceph_iyes(iyesde));
		if (IS_ERR(dir)) {
			dn = ERR_CAST(dir);
			goto out;
		}
		/* There can be multiple paths to access snapped iyesde.
		 * For simplicity, treat snapdir of head iyesde as parent */
		if (ceph_snap(iyesde) != CEPH_SNAPDIR) {
			struct iyesde *snapdir = ceph_get_snapdir(dir);
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
		 * will fail. Do yest mark snapdir dentry as disconnected,
		 * this prevent exportfs from doing futher get_parent. */
		if (unlinked)
			dn = d_obtain_root(dir);
		else
			dn = d_obtain_alias(dir);
	} else {
		dn = __get_parent(child->d_sb, child, 0);
	}
out:
	dout("get_parent %p iyes %llx.%llx err=%ld\n",
	     child, ceph_viyesp(iyesde), (long)PTR_ERR_OR_ZERO(dn));
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

	dout("fh_to_parent %llx\n", cfh->parent_iyes);
	dentry = __get_parent(sb, NULL, cfh->iyes);
	if (unlikely(dentry == ERR_PTR(-ENOENT)))
		dentry = __fh_to_dentry(sb, cfh->parent_iyes);
	return dentry;
}

static int __get_snap_name(struct dentry *parent, char *name,
			   struct dentry *child)
{
	struct iyesde *iyesde = d_iyesde(child);
	struct iyesde *dir = d_iyesde(parent);
	struct ceph_fs_client *fsc = ceph_iyesde_to_client(iyesde);
	struct ceph_mds_request *req = NULL;
	char *last_name = NULL;
	unsigned next_offset = 2;
	int err = -EINVAL;

	if (ceph_iyes(iyesde) != ceph_iyes(dir))
		goto out;
	if (ceph_snap(iyesde) == CEPH_SNAPDIR) {
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
		err = ceph_alloc_readdir_reply_buffer(req, iyesde);
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

		req->r_iyesde = dir;
		ihold(dir);
		req->r_dentry = dget(parent);

		iyesde_lock(dir);
		err = ceph_mdsc_do_request(fsc->mdsc, NULL, req);
		iyesde_unlock(dir);

		if (err < 0)
			goto out;

		rinfo = &req->r_reply_info;
		for (i = 0; i < rinfo->dir_nr; i++) {
			rde = rinfo->dir_entries + i;
			BUG_ON(!rde->iyesde.in);
			if (ceph_snap(iyesde) ==
			    le64_to_cpu(rde->iyesde.in->snapid)) {
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
	dout("get_snap_name %p iyes %llx.%llx err=%d\n",
	     child, ceph_viyesp(iyesde), err);
	return err;
}

static int ceph_get_name(struct dentry *parent, char *name,
			 struct dentry *child)
{
	struct ceph_mds_client *mdsc;
	struct ceph_mds_request *req;
	struct iyesde *iyesde = d_iyesde(child);
	int err;

	if (ceph_snap(iyesde) != CEPH_NOSNAP)
		return __get_snap_name(parent, name, child);

	mdsc = ceph_iyesde_to_client(iyesde)->mdsc;
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPNAME,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);

	iyesde_lock(d_iyesde(parent));

	req->r_iyesde = iyesde;
	ihold(iyesde);
	req->r_iyes2 = ceph_viyes(d_iyesde(parent));
	req->r_parent = d_iyesde(parent);
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_num_caps = 2;
	err = ceph_mdsc_do_request(mdsc, NULL, req);

	iyesde_unlock(d_iyesde(parent));

	if (!err) {
		struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
		memcpy(name, rinfo->dname, rinfo->dname_len);
		name[rinfo->dname_len] = 0;
		dout("get_name %p iyes %llx.%llx name %s\n",
		     child, ceph_viyesp(iyesde), name);
	} else {
		dout("get_name %p iyes %llx.%llx err %d\n",
		     child, ceph_viyesp(iyesde), err);
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
