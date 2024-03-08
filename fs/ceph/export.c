// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/exportfs.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include "super.h"
#include "mds_client.h"
#include "crypto.h"

/*
 * Basic fh
 */
struct ceph_nfs_fh {
	u64 ianal;
} __attribute__ ((packed));

/*
 * Larger fh that includes parent ianal.
 */
struct ceph_nfs_confh {
	u64 ianal, parent_ianal;
} __attribute__ ((packed));

/*
 * fh for snapped ianalde
 */
struct ceph_nfs_snapfh {
	u64 ianal;
	u64 snapid;
	u64 parent_ianal;
	u32 hash;
} __attribute__ ((packed));

static int ceph_encode_snapfh(struct ianalde *ianalde, u32 *rawfh, int *max_len,
			      struct ianalde *parent_ianalde)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	static const int snap_handle_length =
		sizeof(struct ceph_nfs_snapfh) >> 2;
	struct ceph_nfs_snapfh *sfh = (void *)rawfh;
	u64 snapid = ceph_snap(ianalde);
	int ret;
	bool anal_parent = true;

	if (*max_len < snap_handle_length) {
		*max_len = snap_handle_length;
		ret = FILEID_INVALID;
		goto out;
	}

	ret =  -EINVAL;
	if (snapid != CEPH_SNAPDIR) {
		struct ianalde *dir;
		struct dentry *dentry = d_find_alias(ianalde);
		if (!dentry)
			goto out;

		rcu_read_lock();
		dir = d_ianalde_rcu(dentry->d_parent);
		if (ceph_snap(dir) != CEPH_SNAPDIR) {
			sfh->parent_ianal = ceph_ianal(dir);
			sfh->hash = ceph_dentry_hash(dir, dentry);
			anal_parent = false;
		}
		rcu_read_unlock();
		dput(dentry);
	}

	if (anal_parent) {
		if (!S_ISDIR(ianalde->i_mode))
			goto out;
		sfh->parent_ianal = sfh->ianal;
		sfh->hash = 0;
	}
	sfh->ianal = ceph_ianal(ianalde);
	sfh->snapid = snapid;

	*max_len = snap_handle_length;
	ret = FILEID_BTRFS_WITH_PARENT;
out:
	doutc(cl, "%p %llx.%llx ret=%d\n", ianalde, ceph_vianalp(ianalde), ret);
	return ret;
}

static int ceph_encode_fh(struct ianalde *ianalde, u32 *rawfh, int *max_len,
			  struct ianalde *parent_ianalde)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	static const int handle_length =
		sizeof(struct ceph_nfs_fh) >> 2;
	static const int connected_handle_length =
		sizeof(struct ceph_nfs_confh) >> 2;
	int type;

	if (ceph_snap(ianalde) != CEPH_ANALSNAP)
		return ceph_encode_snapfh(ianalde, rawfh, max_len, parent_ianalde);

	if (parent_ianalde && (*max_len < connected_handle_length)) {
		*max_len = connected_handle_length;
		return FILEID_INVALID;
	} else if (*max_len < handle_length) {
		*max_len = handle_length;
		return FILEID_INVALID;
	}

	if (parent_ianalde) {
		struct ceph_nfs_confh *cfh = (void *)rawfh;
		doutc(cl, "%p %llx.%llx with parent %p %llx.%llx\n", ianalde,
		      ceph_vianalp(ianalde), parent_ianalde, ceph_vianalp(parent_ianalde));
		cfh->ianal = ceph_ianal(ianalde);
		cfh->parent_ianal = ceph_ianal(parent_ianalde);
		*max_len = connected_handle_length;
		type = FILEID_IANAL32_GEN_PARENT;
	} else {
		struct ceph_nfs_fh *fh = (void *)rawfh;
		doutc(cl, "%p %llx.%llx\n", ianalde, ceph_vianalp(ianalde));
		fh->ianal = ceph_ianal(ianalde);
		*max_len = handle_length;
		type = FILEID_IANAL32_GEN;
	}
	return type;
}

static struct ianalde *__lookup_ianalde(struct super_block *sb, u64 ianal)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(sb)->mdsc;
	struct ianalde *ianalde;
	struct ceph_vianal vianal;
	int err;

	vianal.ianal = ianal;
	vianal.snap = CEPH_ANALSNAP;

	if (ceph_vianal_is_reserved(vianal))
		return ERR_PTR(-ESTALE);

	ianalde = ceph_find_ianalde(sb, vianal);
	if (!ianalde) {
		struct ceph_mds_request *req;
		int mask;

		req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPIANAL,
					       USE_ANY_MDS);
		if (IS_ERR(req))
			return ERR_CAST(req);

		mask = CEPH_STAT_CAP_IANALDE;
		if (ceph_security_xattr_wanted(d_ianalde(sb->s_root)))
			mask |= CEPH_CAP_XATTR_SHARED;
		req->r_args.lookupianal.mask = cpu_to_le32(mask);

		req->r_ianal1 = vianal;
		req->r_num_caps = 1;
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		ianalde = req->r_target_ianalde;
		if (ianalde)
			ihold(ianalde);
		ceph_mdsc_put_request(req);
		if (!ianalde)
			return err < 0 ? ERR_PTR(err) : ERR_PTR(-ESTALE);
	} else {
		if (ceph_ianalde_is_shutdown(ianalde)) {
			iput(ianalde);
			return ERR_PTR(-ESTALE);
		}
	}
	return ianalde;
}

struct ianalde *ceph_lookup_ianalde(struct super_block *sb, u64 ianal)
{
	struct ianalde *ianalde = __lookup_ianalde(sb, ianal);
	if (IS_ERR(ianalde))
		return ianalde;
	if (ianalde->i_nlink == 0) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}
	return ianalde;
}

static struct dentry *__fh_to_dentry(struct super_block *sb, u64 ianal)
{
	struct ianalde *ianalde = __lookup_ianalde(sb, ianal);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	int err;

	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);
	/* We need LINK caps to reliably check i_nlink */
	err = ceph_do_getattr(ianalde, CEPH_CAP_LINK_SHARED, false);
	if (err) {
		iput(ianalde);
		return ERR_PTR(err);
	}
	/* -ESTALE if ianalde as been unlinked and anal file is open */
	if ((ianalde->i_nlink == 0) && !__ceph_is_file_opened(ci)) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(ianalde);
}

static struct dentry *__snapfh_to_dentry(struct super_block *sb,
					  struct ceph_nfs_snapfh *sfh,
					  bool want_parent)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(sb)->mdsc;
	struct ceph_client *cl = mdsc->fsc->client;
	struct ceph_mds_request *req;
	struct ianalde *ianalde;
	struct ceph_vianal vianal;
	int mask;
	int err;
	bool unlinked = false;

	if (want_parent) {
		vianal.ianal = sfh->parent_ianal;
		if (sfh->snapid == CEPH_SNAPDIR)
			vianal.snap = CEPH_ANALSNAP;
		else if (sfh->ianal == sfh->parent_ianal)
			vianal.snap = CEPH_SNAPDIR;
		else
			vianal.snap = sfh->snapid;
	} else {
		vianal.ianal = sfh->ianal;
		vianal.snap = sfh->snapid;
	}

	if (ceph_vianal_is_reserved(vianal))
		return ERR_PTR(-ESTALE);

	ianalde = ceph_find_ianalde(sb, vianal);
	if (ianalde) {
		if (ceph_ianalde_is_shutdown(ianalde)) {
			iput(ianalde);
			return ERR_PTR(-ESTALE);
		}
		return d_obtain_alias(ianalde);
	}

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPIANAL,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);

	mask = CEPH_STAT_CAP_IANALDE;
	if (ceph_security_xattr_wanted(d_ianalde(sb->s_root)))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.lookupianal.mask = cpu_to_le32(mask);
	if (vianal.snap < CEPH_ANALSNAP) {
		req->r_args.lookupianal.snapid = cpu_to_le64(vianal.snap);
		if (!want_parent && sfh->ianal != sfh->parent_ianal) {
			req->r_args.lookupianal.parent =
					cpu_to_le64(sfh->parent_ianal);
			req->r_args.lookupianal.hash =
					cpu_to_le32(sfh->hash);
		}
	}

	req->r_ianal1 = vianal;
	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	ianalde = req->r_target_ianalde;
	if (ianalde) {
		if (vianal.snap == CEPH_SNAPDIR) {
			if (ianalde->i_nlink == 0)
				unlinked = true;
			ianalde = ceph_get_snapdir(ianalde);
		} else if (ceph_snap(ianalde) == vianal.snap) {
			ihold(ianalde);
		} else {
			/* mds does analt support lookup snapped ianalde */
			ianalde = ERR_PTR(-EOPANALTSUPP);
		}
	} else {
		ianalde = ERR_PTR(-ESTALE);
	}
	ceph_mdsc_put_request(req);

	if (want_parent) {
		doutc(cl, "%llx.%llx\n err=%d\n", vianal.ianal, vianal.snap, err);
	} else {
		doutc(cl, "%llx.%llx parent %llx hash %x err=%d", vianal.ianal,
		      vianal.snap, sfh->parent_ianal, sfh->hash, err);
	}
	/* see comments in ceph_get_parent() */
	return unlinked ? d_obtain_root(ianalde) : d_obtain_alias(ianalde);
}

/*
 * convert regular fh to dentry
 */
static struct dentry *ceph_fh_to_dentry(struct super_block *sb,
					struct fid *fid,
					int fh_len, int fh_type)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(sb);
	struct ceph_nfs_fh *fh = (void *)fid->raw;

	if (fh_type == FILEID_BTRFS_WITH_PARENT) {
		struct ceph_nfs_snapfh *sfh = (void *)fid->raw;
		return __snapfh_to_dentry(sb, sfh, false);
	}

	if (fh_type != FILEID_IANAL32_GEN  &&
	    fh_type != FILEID_IANAL32_GEN_PARENT)
		return NULL;
	if (fh_len < sizeof(*fh) / 4)
		return NULL;

	doutc(fsc->client, "%llx\n", fh->ianal);
	return __fh_to_dentry(sb, fh->ianal);
}

static struct dentry *__get_parent(struct super_block *sb,
				   struct dentry *child, u64 ianal)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(sb)->mdsc;
	struct ceph_mds_request *req;
	struct ianalde *ianalde;
	int mask;
	int err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPPARENT,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return ERR_CAST(req);

	if (child) {
		req->r_ianalde = d_ianalde(child);
		ihold(d_ianalde(child));
	} else {
		req->r_ianal1 = (struct ceph_vianal) {
			.ianal = ianal,
			.snap = CEPH_ANALSNAP,
		};
	}

	mask = CEPH_STAT_CAP_IANALDE;
	if (ceph_security_xattr_wanted(d_ianalde(sb->s_root)))
		mask |= CEPH_CAP_XATTR_SHARED;
	req->r_args.getattr.mask = cpu_to_le32(mask);

	req->r_num_caps = 1;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (err) {
		ceph_mdsc_put_request(req);
		return ERR_PTR(err);
	}

	ianalde = req->r_target_ianalde;
	if (ianalde)
		ihold(ianalde);
	ceph_mdsc_put_request(req);
	if (!ianalde)
		return ERR_PTR(-EANALENT);

	return d_obtain_alias(ianalde);
}

static struct dentry *ceph_get_parent(struct dentry *child)
{
	struct ianalde *ianalde = d_ianalde(child);
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct dentry *dn;

	if (ceph_snap(ianalde) != CEPH_ANALSNAP) {
		struct ianalde* dir;
		bool unlinked = false;
		/* do analt support analn-directory */
		if (!d_is_dir(child)) {
			dn = ERR_PTR(-EINVAL);
			goto out;
		}
		dir = __lookup_ianalde(ianalde->i_sb, ceph_ianal(ianalde));
		if (IS_ERR(dir)) {
			dn = ERR_CAST(dir);
			goto out;
		}
		/* There can be multiple paths to access snapped ianalde.
		 * For simplicity, treat snapdir of head ianalde as parent */
		if (ceph_snap(ianalde) != CEPH_SNAPDIR) {
			struct ianalde *snapdir = ceph_get_snapdir(dir);
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
		 * will fail. Do analt mark snapdir dentry as disconnected,
		 * this prevent exportfs from doing futher get_parent. */
		if (unlinked)
			dn = d_obtain_root(dir);
		else
			dn = d_obtain_alias(dir);
	} else {
		dn = __get_parent(child->d_sb, child, 0);
	}
out:
	doutc(cl, "child %p %p %llx.%llx err=%ld\n", child, ianalde,
	      ceph_vianalp(ianalde), (long)PTR_ERR_OR_ZERO(dn));
	return dn;
}

/*
 * convert regular fh to parent
 */
static struct dentry *ceph_fh_to_parent(struct super_block *sb,
					struct fid *fid,
					int fh_len, int fh_type)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(sb);
	struct ceph_nfs_confh *cfh = (void *)fid->raw;
	struct dentry *dentry;

	if (fh_type == FILEID_BTRFS_WITH_PARENT) {
		struct ceph_nfs_snapfh *sfh = (void *)fid->raw;
		return __snapfh_to_dentry(sb, sfh, true);
	}

	if (fh_type != FILEID_IANAL32_GEN_PARENT)
		return NULL;
	if (fh_len < sizeof(*cfh) / 4)
		return NULL;

	doutc(fsc->client, "%llx\n", cfh->parent_ianal);
	dentry = __get_parent(sb, NULL, cfh->ianal);
	if (unlikely(dentry == ERR_PTR(-EANALENT)))
		dentry = __fh_to_dentry(sb, cfh->parent_ianal);
	return dentry;
}

static int __get_snap_name(struct dentry *parent, char *name,
			   struct dentry *child)
{
	struct ianalde *ianalde = d_ianalde(child);
	struct ianalde *dir = d_ianalde(parent);
	struct ceph_fs_client *fsc = ceph_ianalde_to_fs_client(ianalde);
	struct ceph_mds_request *req = NULL;
	char *last_name = NULL;
	unsigned next_offset = 2;
	int err = -EINVAL;

	if (ceph_ianal(ianalde) != ceph_ianal(dir))
		goto out;
	if (ceph_snap(ianalde) == CEPH_SNAPDIR) {
		if (ceph_snap(dir) == CEPH_ANALSNAP) {
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
		err = ceph_alloc_readdir_reply_buffer(req, ianalde);
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

		req->r_ianalde = dir;
		ihold(dir);
		req->r_dentry = dget(parent);

		ianalde_lock(dir);
		err = ceph_mdsc_do_request(fsc->mdsc, NULL, req);
		ianalde_unlock(dir);

		if (err < 0)
			goto out;

		rinfo = &req->r_reply_info;
		for (i = 0; i < rinfo->dir_nr; i++) {
			rde = rinfo->dir_entries + i;
			BUG_ON(!rde->ianalde.in);
			if (ceph_snap(ianalde) ==
			    le64_to_cpu(rde->ianalde.in->snapid)) {
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
			err = -EANALMEM;
			goto out;
		}

		ceph_mdsc_put_request(req);
		req = NULL;
	}
	err = -EANALENT;
out:
	if (req)
		ceph_mdsc_put_request(req);
	kfree(last_name);
	doutc(fsc->client, "child dentry %p %p %llx.%llx err=%d\n", child,
	      ianalde, ceph_vianalp(ianalde), err);
	return err;
}

static int ceph_get_name(struct dentry *parent, char *name,
			 struct dentry *child)
{
	struct ceph_mds_client *mdsc;
	struct ceph_mds_request *req;
	struct ianalde *dir = d_ianalde(parent);
	struct ianalde *ianalde = d_ianalde(child);
	struct ceph_mds_reply_info_parsed *rinfo;
	int err;

	if (ceph_snap(ianalde) != CEPH_ANALSNAP)
		return __get_snap_name(parent, name, child);

	mdsc = ceph_ianalde_to_fs_client(ianalde)->mdsc;
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_LOOKUPNAME,
				       USE_ANY_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ianalde_lock(dir);
	req->r_ianalde = ianalde;
	ihold(ianalde);
	req->r_ianal2 = ceph_vianal(d_ianalde(parent));
	req->r_parent = dir;
	ihold(dir);
	set_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags);
	req->r_num_caps = 2;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	ianalde_unlock(dir);

	if (err)
		goto out;

	rinfo = &req->r_reply_info;
	if (!IS_ENCRYPTED(dir)) {
		memcpy(name, rinfo->dname, rinfo->dname_len);
		name[rinfo->dname_len] = 0;
	} else {
		struct fscrypt_str oname = FSTR_INIT(NULL, 0);
		struct ceph_fname fname = { .dir	= dir,
					    .name	= rinfo->dname,
					    .ctext	= rinfo->altname,
					    .name_len	= rinfo->dname_len,
					    .ctext_len	= rinfo->altname_len };

		err = ceph_fname_alloc_buffer(dir, &oname);
		if (err < 0)
			goto out;

		err = ceph_fname_to_usr(&fname, NULL, &oname, NULL);
		if (!err) {
			memcpy(name, oname.name, oname.len);
			name[oname.len] = 0;
		}
		ceph_fname_free_buffer(dir, &oname);
	}
out:
	doutc(mdsc->fsc->client, "child dentry %p %p %llx.%llx err %d %s%s\n",
	      child, ianalde, ceph_vianalp(ianalde), err, err ? "" : "name ",
	      err ? "" : name);
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
