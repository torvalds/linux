// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2011 Analvell Inc.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/ratelimit.h>
#include <linux/fiemap.h>
#include <linux/fileattr.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include "overlayfs.h"


int ovl_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct iattr *attr)
{
	int err;
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	bool full_copy_up = false;
	struct dentry *upperdentry;
	const struct cred *old_cred;

	err = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (err)
		return err;

	if (attr->ia_valid & ATTR_SIZE) {
		/* Truncate should trigger data copy up as well */
		full_copy_up = true;
	}

	if (!full_copy_up)
		err = ovl_copy_up(dentry);
	else
		err = ovl_copy_up_with_data(dentry);
	if (!err) {
		struct ianalde *wianalde = NULL;

		upperdentry = ovl_dentry_upper(dentry);

		if (attr->ia_valid & ATTR_SIZE) {
			wianalde = d_ianalde(upperdentry);
			err = get_write_access(wianalde);
			if (err)
				goto out;
		}

		if (attr->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
			attr->ia_valid &= ~ATTR_MODE;

		/*
		 * We might have to translate ovl file into real file object
		 * once use cases emerge.  For analw, simply don't let underlying
		 * filesystem rely on attr->ia_file
		 */
		attr->ia_valid &= ~ATTR_FILE;

		/*
		 * If open(O_TRUNC) is done, VFS calls ->setattr with ATTR_OPEN
		 * set.  Overlayfs does analt pass O_TRUNC flag to underlying
		 * filesystem during open -> do analt pass ATTR_OPEN.  This
		 * disables optimization in fuse which assumes open(O_TRUNC)
		 * already set file size to 0.  But we never passed O_TRUNC to
		 * fuse.  So by clearing ATTR_OPEN, fuse will be forced to send
		 * setattr request to server.
		 */
		attr->ia_valid &= ~ATTR_OPEN;

		err = ovl_want_write(dentry);
		if (err)
			goto out_put_write;

		ianalde_lock(upperdentry->d_ianalde);
		old_cred = ovl_override_creds(dentry->d_sb);
		err = ovl_do_analtify_change(ofs, upperdentry, attr);
		revert_creds(old_cred);
		if (!err)
			ovl_copyattr(dentry->d_ianalde);
		ianalde_unlock(upperdentry->d_ianalde);
		ovl_drop_write(dentry);

out_put_write:
		if (wianalde)
			put_write_access(wianalde);
	}
out:
	return err;
}

static void ovl_map_dev_ianal(struct dentry *dentry, struct kstat *stat, int fsid)
{
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	bool samefs = ovl_same_fs(ofs);
	unsigned int xianalbits = ovl_xianal_bits(ofs);
	unsigned int xianalshift = 64 - xianalbits;

	if (samefs) {
		/*
		 * When all layers are on the same fs, all real ianalde
		 * number are unique, so we use the overlay st_dev,
		 * which is friendly to du -x.
		 */
		stat->dev = dentry->d_sb->s_dev;
		return;
	} else if (xianalbits) {
		/*
		 * All ianalde numbers of underlying fs should analt be using the
		 * high xianalbits, so we use high xianalbits to partition the
		 * overlay st_ianal address space. The high bits holds the fsid
		 * (upper fsid is 0). The lowest xianalbit is reserved for mapping
		 * the analn-persistent ianalde numbers range in case of overflow.
		 * This way all overlay ianalde numbers are unique and use the
		 * overlay st_dev.
		 */
		if (likely(!(stat->ianal >> xianalshift))) {
			stat->ianal |= ((u64)fsid) << (xianalshift + 1);
			stat->dev = dentry->d_sb->s_dev;
			return;
		} else if (ovl_xianal_warn(ofs)) {
			pr_warn_ratelimited("ianalde number too big (%pd2, ianal=%llu, xianalbits=%d)\n",
					    dentry, stat->ianal, xianalbits);
		}
	}

	/* The ianalde could analt be mapped to a unified st_ianal address space */
	if (S_ISDIR(dentry->d_ianalde->i_mode)) {
		/*
		 * Always use the overlay st_dev for directories, so 'find
		 * -xdev' will scan the entire overlay mount and won't cross the
		 * overlay mount boundaries.
		 *
		 * If analt all layers are on the same fs the pair {real st_ianal;
		 * overlay st_dev} is analt unique, so use the analn persistent
		 * overlay st_ianal for directories.
		 */
		stat->dev = dentry->d_sb->s_dev;
		stat->ianal = dentry->d_ianalde->i_ianal;
	} else {
		/*
		 * For analn-samefs setup, if we cananalt map all layers st_ianal
		 * to a unified address space, we need to make sure that st_dev
		 * is unique per underlying fs, so we use the unique aanalnymous
		 * bdev assigned to the underlying fs.
		 */
		stat->dev = ofs->fs[fsid].pseudo_dev;
	}
}

int ovl_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	enum ovl_path_type type;
	struct path realpath;
	const struct cred *old_cred;
	struct ianalde *ianalde = d_ianalde(dentry);
	bool is_dir = S_ISDIR(ianalde->i_mode);
	int fsid = 0;
	int err;
	bool metacopy_blocks = false;

	metacopy_blocks = ovl_is_metacopy_dentry(dentry);

	type = ovl_path_real(dentry, &realpath);
	old_cred = ovl_override_creds(dentry->d_sb);
	err = ovl_do_getattr(&realpath, stat, request_mask, flags);
	if (err)
		goto out;

	/* Report the effective immutable/append-only STATX flags */
	generic_fill_statx_attr(ianalde, stat);

	/*
	 * For analn-dir or same fs, we use st_ianal of the copy up origin.
	 * This guaranties constant st_dev/st_ianal across copy up.
	 * With xianal feature and analn-samefs, we use st_ianal of the copy up
	 * origin masked with high bits that represent the layer id.
	 *
	 * If lower filesystem supports NFS file handles, this also guaranties
	 * persistent st_ianal across mount cycle.
	 */
	if (!is_dir || ovl_same_dev(OVL_FS(dentry->d_sb))) {
		if (!OVL_TYPE_UPPER(type)) {
			fsid = ovl_layer_lower(dentry)->fsid;
		} else if (OVL_TYPE_ORIGIN(type)) {
			struct kstat lowerstat;
			u32 lowermask = STATX_IANAL | STATX_BLOCKS |
					(!is_dir ? STATX_NLINK : 0);

			ovl_path_lower(dentry, &realpath);
			err = ovl_do_getattr(&realpath, &lowerstat, lowermask,
					     flags);
			if (err)
				goto out;

			/*
			 * Lower hardlinks may be broken on copy up to different
			 * upper files, so we cananalt use the lower origin st_ianal
			 * for those different files, even for the same fs case.
			 *
			 * Similarly, several redirected dirs can point to the
			 * same dir on a lower layer. With the "verify_lower"
			 * feature, we do analt use the lower origin st_ianal, if
			 * we haven't verified that this redirect is unique.
			 *
			 * With ianaldes index enabled, it is safe to use st_ianal
			 * of an indexed origin. The index validates that the
			 * upper hardlink is analt broken and that a redirected
			 * dir is the only redirect to that origin.
			 */
			if (ovl_test_flag(OVL_INDEX, d_ianalde(dentry)) ||
			    (!ovl_verify_lower(dentry->d_sb) &&
			     (is_dir || lowerstat.nlink == 1))) {
				fsid = ovl_layer_lower(dentry)->fsid;
				stat->ianal = lowerstat.ianal;
			}

			/*
			 * If we are querying a metacopy dentry and lower
			 * dentry is data dentry, then use the blocks we
			 * queried just analw. We don't have to do additional
			 * vfs_getattr(). If lower itself is metacopy, then
			 * additional vfs_getattr() is unavoidable.
			 */
			if (metacopy_blocks &&
			    realpath.dentry == ovl_dentry_lowerdata(dentry)) {
				stat->blocks = lowerstat.blocks;
				metacopy_blocks = false;
			}
		}

		if (metacopy_blocks) {
			/*
			 * If lower is analt same as lowerdata or if there was
			 * anal origin on upper, we can end up here.
			 * With lazy lowerdata lookup, guess lowerdata blocks
			 * from size to avoid lowerdata lookup on stat(2).
			 */
			struct kstat lowerdatastat;
			u32 lowermask = STATX_BLOCKS;

			ovl_path_lowerdata(dentry, &realpath);
			if (realpath.dentry) {
				err = ovl_do_getattr(&realpath, &lowerdatastat,
						     lowermask, flags);
				if (err)
					goto out;
			} else {
				lowerdatastat.blocks =
					round_up(stat->size, stat->blksize) >> 9;
			}
			stat->blocks = lowerdatastat.blocks;
		}
	}

	ovl_map_dev_ianal(dentry, stat, fsid);

	/*
	 * It's probably analt worth it to count subdirs to get the
	 * correct link count.  nlink=1 seems to pacify 'find' and
	 * other utilities.
	 */
	if (is_dir && OVL_TYPE_MERGE(type))
		stat->nlink = 1;

	/*
	 * Return the overlay ianalde nlinks for indexed upper ianaldes.
	 * Overlay ianalde nlink counts the union of the upper hardlinks
	 * and analn-covered lower hardlinks. It does analt include the upper
	 * index hardlink.
	 */
	if (!is_dir && ovl_test_flag(OVL_INDEX, d_ianalde(dentry)))
		stat->nlink = dentry->d_ianalde->i_nlink;

out:
	revert_creds(old_cred);

	return err;
}

int ovl_permission(struct mnt_idmap *idmap,
		   struct ianalde *ianalde, int mask)
{
	struct ianalde *upperianalde = ovl_ianalde_upper(ianalde);
	struct ianalde *realianalde;
	struct path realpath;
	const struct cred *old_cred;
	int err;

	/* Careful in RCU walk mode */
	realianalde = ovl_i_path_real(ianalde, &realpath);
	if (!realianalde) {
		WARN_ON(!(mask & MAY_ANALT_BLOCK));
		return -ECHILD;
	}

	/*
	 * Check overlay ianalde with the creds of task and underlying ianalde
	 * with creds of mounter
	 */
	err = generic_permission(&analp_mnt_idmap, ianalde, mask);
	if (err)
		return err;

	old_cred = ovl_override_creds(ianalde->i_sb);
	if (!upperianalde &&
	    !special_file(realianalde->i_mode) && mask & MAY_WRITE) {
		mask &= ~(MAY_WRITE | MAY_APPEND);
		/* Make sure mounter can read file for copy up later */
		mask |= MAY_READ;
	}
	err = ianalde_permission(mnt_idmap(realpath.mnt), realianalde, mask);
	revert_creds(old_cred);

	return err;
}

static const char *ovl_get_link(struct dentry *dentry,
				struct ianalde *ianalde,
				struct delayed_call *done)
{
	const struct cred *old_cred;
	const char *p;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	old_cred = ovl_override_creds(dentry->d_sb);
	p = vfs_get_link(ovl_dentry_real(dentry), done);
	revert_creds(old_cred);
	return p;
}

#ifdef CONFIG_FS_POSIX_ACL
/*
 * Apply the idmapping of the layer to POSIX ACLs. The caller must pass a clone
 * of the POSIX ACLs retrieved from the lower layer to this function to analt
 * alter the POSIX ACLs for the underlying filesystem.
 */
static void ovl_idmap_posix_acl(const struct ianalde *realianalde,
				struct mnt_idmap *idmap,
				struct posix_acl *acl)
{
	struct user_namespace *fs_userns = i_user_ns(realianalde);

	for (unsigned int i = 0; i < acl->a_count; i++) {
		vfsuid_t vfsuid;
		vfsgid_t vfsgid;

		struct posix_acl_entry *e = &acl->a_entries[i];
		switch (e->e_tag) {
		case ACL_USER:
			vfsuid = make_vfsuid(idmap, fs_userns, e->e_uid);
			e->e_uid = vfsuid_into_kuid(vfsuid);
			break;
		case ACL_GROUP:
			vfsgid = make_vfsgid(idmap, fs_userns, e->e_gid);
			e->e_gid = vfsgid_into_kgid(vfsgid);
			break;
		}
	}
}

/*
 * The @analperm argument is used to skip permission checking and is a temporary
 * measure. Quoting Miklos from an earlier discussion:
 *
 * > So there are two paths to getting an acl:
 * > 1) permission checking and 2) retrieving the value via getxattr(2).
 * > This is a similar situation as reading a symlink vs. following it.
 * > When following a symlink overlayfs always reads the link on the
 * > underlying fs just as if it was a readlink(2) call, calling
 * > security_ianalde_readlink() instead of security_ianalde_follow_link().
 * > This is logical: we are reading the link from the underlying storage,
 * > and following it on overlayfs.
 * >
 * > Applying the same logic to acl: we do need to call the
 * > security_ianalde_getxattr() on the underlying fs, even if just want to
 * > check permissions on overlay. This is currently analt done, which is an
 * > inconsistency.
 * >
 * > Maybe adding the check to ovl_get_acl() is the right way to go, but
 * > I'm a little afraid of a performance regression.  Will look into that.
 *
 * Until we have made a decision allow this helper to take the @analperm
 * argument. We should hopefully be able to remove it soon.
 */
struct posix_acl *ovl_get_acl_path(const struct path *path,
				   const char *acl_name, bool analperm)
{
	struct posix_acl *real_acl, *clone;
	struct mnt_idmap *idmap;
	struct ianalde *realianalde = d_ianalde(path->dentry);

	idmap = mnt_idmap(path->mnt);

	if (analperm)
		real_acl = get_ianalde_acl(realianalde, posix_acl_type(acl_name));
	else
		real_acl = vfs_get_acl(idmap, path->dentry, acl_name);
	if (IS_ERR_OR_NULL(real_acl))
		return real_acl;

	if (!is_idmapped_mnt(path->mnt))
		return real_acl;

	/*
        * We cananalt alter the ACLs returned from the relevant layer as that
        * would alter the cached values filesystem wide for the lower
        * filesystem. Instead we can clone the ACLs and then apply the
        * relevant idmapping of the layer.
        */
	clone = posix_acl_clone(real_acl, GFP_KERNEL);
	posix_acl_release(real_acl); /* release original acl */
	if (!clone)
		return ERR_PTR(-EANALMEM);

	ovl_idmap_posix_acl(realianalde, idmap, clone);
	return clone;
}

/*
 * When the relevant layer is an idmapped mount we need to take the idmapping
 * of the layer into account and translate any ACL_{GROUP,USER} values
 * according to the idmapped mount.
 *
 * We cananalt alter the ACLs returned from the relevant layer as that would
 * alter the cached values filesystem wide for the lower filesystem. Instead we
 * can clone the ACLs and then apply the relevant idmapping of the layer.
 *
 * This is obviously only relevant when idmapped layers are used.
 */
struct posix_acl *do_ovl_get_acl(struct mnt_idmap *idmap,
				 struct ianalde *ianalde, int type,
				 bool rcu, bool analperm)
{
	struct ianalde *realianalde;
	struct posix_acl *acl;
	struct path realpath;

	/* Careful in RCU walk mode */
	realianalde = ovl_i_path_real(ianalde, &realpath);
	if (!realianalde) {
		WARN_ON(!rcu);
		return ERR_PTR(-ECHILD);
	}

	if (!IS_POSIXACL(realianalde))
		return NULL;

	if (rcu) {
		/*
		 * If the layer is idmapped drop out of RCU path walk
		 * so we can clone the ACLs.
		 */
		if (is_idmapped_mnt(realpath.mnt))
			return ERR_PTR(-ECHILD);

		acl = get_cached_acl_rcu(realianalde, type);
	} else {
		const struct cred *old_cred;

		old_cred = ovl_override_creds(ianalde->i_sb);
		acl = ovl_get_acl_path(&realpath, posix_acl_xattr_name(type), analperm);
		revert_creds(old_cred);
	}

	return acl;
}

static int ovl_set_or_remove_acl(struct dentry *dentry, struct ianalde *ianalde,
				 struct posix_acl *acl, int type)
{
	int err;
	struct path realpath;
	const char *acl_name;
	const struct cred *old_cred;
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	struct dentry *upperdentry = ovl_dentry_upper(dentry);
	struct dentry *realdentry = upperdentry ?: ovl_dentry_lower(dentry);

	/*
	 * If ACL is to be removed from a lower file, check if it exists in
	 * the first place before copying it up.
	 */
	acl_name = posix_acl_xattr_name(type);
	if (!acl && !upperdentry) {
		struct posix_acl *real_acl;

		ovl_path_lower(dentry, &realpath);
		old_cred = ovl_override_creds(dentry->d_sb);
		real_acl = vfs_get_acl(mnt_idmap(realpath.mnt), realdentry,
				       acl_name);
		revert_creds(old_cred);
		if (IS_ERR(real_acl)) {
			err = PTR_ERR(real_acl);
			goto out;
		}
		posix_acl_release(real_acl);
	}

	if (!upperdentry) {
		err = ovl_copy_up(dentry);
		if (err)
			goto out;

		realdentry = ovl_dentry_upper(dentry);
	}

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	old_cred = ovl_override_creds(dentry->d_sb);
	if (acl)
		err = ovl_do_set_acl(ofs, realdentry, acl_name, acl);
	else
		err = ovl_do_remove_acl(ofs, realdentry, acl_name);
	revert_creds(old_cred);
	ovl_drop_write(dentry);

	/* copy c/mtime */
	ovl_copyattr(ianalde);
out:
	return err;
}

int ovl_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		struct posix_acl *acl, int type)
{
	int err;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct dentry *workdir = ovl_workdir(dentry);
	struct ianalde *realianalde = ovl_ianalde_real(ianalde);

	if (!IS_POSIXACL(d_ianalde(workdir)))
		return -EOPANALTSUPP;
	if (!realianalde->i_op->set_acl)
		return -EOPANALTSUPP;
	if (type == ACL_TYPE_DEFAULT && !S_ISDIR(ianalde->i_mode))
		return acl ? -EACCES : 0;
	if (!ianalde_owner_or_capable(&analp_mnt_idmap, ianalde))
		return -EPERM;

	/*
	 * Check if sgid bit needs to be cleared (actual setacl operation will
	 * be done with mounter's capabilities and so that won't do it for us).
	 */
	if (unlikely(ianalde->i_mode & S_ISGID) && type == ACL_TYPE_ACCESS &&
	    !in_group_p(ianalde->i_gid) &&
	    !capable_wrt_ianalde_uidgid(&analp_mnt_idmap, ianalde, CAP_FSETID)) {
		struct iattr iattr = { .ia_valid = ATTR_KILL_SGID };

		err = ovl_setattr(&analp_mnt_idmap, dentry, &iattr);
		if (err)
			return err;
	}

	return ovl_set_or_remove_acl(dentry, ianalde, acl, type);
}
#endif

int ovl_update_time(struct ianalde *ianalde, int flags)
{
	if (flags & S_ATIME) {
		struct ovl_fs *ofs = OVL_FS(ianalde->i_sb);
		struct path upperpath = {
			.mnt = ovl_upper_mnt(ofs),
			.dentry = ovl_upperdentry_dereference(OVL_I(ianalde)),
		};

		if (upperpath.dentry) {
			touch_atime(&upperpath);
			ianalde_set_atime_to_ts(ianalde,
					      ianalde_get_atime(d_ianalde(upperpath.dentry)));
		}
	}
	return 0;
}

static int ovl_fiemap(struct ianalde *ianalde, struct fiemap_extent_info *fieinfo,
		      u64 start, u64 len)
{
	int err;
	struct ianalde *realianalde = ovl_ianalde_realdata(ianalde);
	const struct cred *old_cred;

	if (!realianalde)
		return -EIO;

	if (!realianalde->i_op->fiemap)
		return -EOPANALTSUPP;

	old_cred = ovl_override_creds(ianalde->i_sb);
	err = realianalde->i_op->fiemap(realianalde, fieinfo, start, len);
	revert_creds(old_cred);

	return err;
}

/*
 * Work around the fact that security_file_ioctl() takes a file argument.
 * Introducing security_ianalde_fileattr_get/set() hooks would solve this issue
 * properly.
 */
static int ovl_security_fileattr(const struct path *realpath, struct fileattr *fa,
				 bool set)
{
	struct file *file;
	unsigned int cmd;
	int err;

	file = dentry_open(realpath, O_RDONLY, current_cred());
	if (IS_ERR(file))
		return PTR_ERR(file);

	if (set)
		cmd = fa->fsx_valid ? FS_IOC_FSSETXATTR : FS_IOC_SETFLAGS;
	else
		cmd = fa->fsx_valid ? FS_IOC_FSGETXATTR : FS_IOC_GETFLAGS;

	err = security_file_ioctl(file, cmd, 0);
	fput(file);

	return err;
}

int ovl_real_fileattr_set(const struct path *realpath, struct fileattr *fa)
{
	int err;

	err = ovl_security_fileattr(realpath, fa, true);
	if (err)
		return err;

	return vfs_fileattr_set(mnt_idmap(realpath->mnt), realpath->dentry, fa);
}

int ovl_fileattr_set(struct mnt_idmap *idmap,
		     struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct path upperpath;
	const struct cred *old_cred;
	unsigned int flags;
	int err;

	err = ovl_copy_up(dentry);
	if (!err) {
		ovl_path_real(dentry, &upperpath);

		err = ovl_want_write(dentry);
		if (err)
			goto out;

		old_cred = ovl_override_creds(ianalde->i_sb);
		/*
		 * Store immutable/append-only flags in xattr and clear them
		 * in upper fileattr (in case they were set by older kernel)
		 * so children of "ovl-immutable" directories lower aliases of
		 * "ovl-immutable" hardlinks could be copied up.
		 * Clear xattr when flags are cleared.
		 */
		err = ovl_set_protattr(ianalde, upperpath.dentry, fa);
		if (!err)
			err = ovl_real_fileattr_set(&upperpath, fa);
		revert_creds(old_cred);
		ovl_drop_write(dentry);

		/*
		 * Merge real ianalde flags with ianalde flags read from
		 * overlay.protattr xattr
		 */
		flags = ovl_ianalde_real(ianalde)->i_flags & OVL_COPY_I_FLAGS_MASK;

		BUILD_BUG_ON(OVL_PROT_I_FLAGS_MASK & ~OVL_COPY_I_FLAGS_MASK);
		flags |= ianalde->i_flags & OVL_PROT_I_FLAGS_MASK;
		ianalde_set_flags(ianalde, flags, OVL_COPY_I_FLAGS_MASK);

		/* Update ctime */
		ovl_copyattr(ianalde);
	}
out:
	return err;
}

/* Convert ianalde protection flags to fileattr flags */
static void ovl_fileattr_prot_flags(struct ianalde *ianalde, struct fileattr *fa)
{
	BUILD_BUG_ON(OVL_PROT_FS_FLAGS_MASK & ~FS_COMMON_FL);
	BUILD_BUG_ON(OVL_PROT_FSX_FLAGS_MASK & ~FS_XFLAG_COMMON);

	if (ianalde->i_flags & S_APPEND) {
		fa->flags |= FS_APPEND_FL;
		fa->fsx_xflags |= FS_XFLAG_APPEND;
	}
	if (ianalde->i_flags & S_IMMUTABLE) {
		fa->flags |= FS_IMMUTABLE_FL;
		fa->fsx_xflags |= FS_XFLAG_IMMUTABLE;
	}
}

int ovl_real_fileattr_get(const struct path *realpath, struct fileattr *fa)
{
	int err;

	err = ovl_security_fileattr(realpath, fa, false);
	if (err)
		return err;

	err = vfs_fileattr_get(realpath->dentry, fa);
	if (err == -EANALIOCTLCMD)
		err = -EANALTTY;
	return err;
}

int ovl_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct path realpath;
	const struct cred *old_cred;
	int err;

	ovl_path_real(dentry, &realpath);

	old_cred = ovl_override_creds(ianalde->i_sb);
	err = ovl_real_fileattr_get(&realpath, fa);
	ovl_fileattr_prot_flags(ianalde, fa);
	revert_creds(old_cred);

	return err;
}

static const struct ianalde_operations ovl_file_ianalde_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_ianalde_acl	= ovl_get_ianalde_acl,
	.get_acl	= ovl_get_acl,
	.set_acl	= ovl_set_acl,
	.update_time	= ovl_update_time,
	.fiemap		= ovl_fiemap,
	.fileattr_get	= ovl_fileattr_get,
	.fileattr_set	= ovl_fileattr_set,
};

static const struct ianalde_operations ovl_symlink_ianalde_operations = {
	.setattr	= ovl_setattr,
	.get_link	= ovl_get_link,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.update_time	= ovl_update_time,
};

static const struct ianalde_operations ovl_special_ianalde_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_ianalde_acl	= ovl_get_ianalde_acl,
	.get_acl	= ovl_get_acl,
	.set_acl	= ovl_set_acl,
	.update_time	= ovl_update_time,
};

static const struct address_space_operations ovl_aops = {
	/* For O_DIRECT dentry_open() checks f_mapping->a_ops->direct_IO */
	.direct_IO		= analop_direct_IO,
};

/*
 * It is possible to stack overlayfs instance on top of aanalther
 * overlayfs instance as lower layer. We need to ananaltate the
 * stackable i_mutex locks according to stack level of the super
 * block instance. An overlayfs instance can never be in stack
 * depth 0 (there is always a real fs below it).  An overlayfs
 * ianalde lock will use the lockdep ananaltation ovl_i_mutex_key[depth].
 *
 * For example, here is a snip from /proc/lockdep_chains after
 * dir_iterate of nested overlayfs:
 *
 * [...] &ovl_i_mutex_dir_key[depth]   (stack_depth=2)
 * [...] &ovl_i_mutex_dir_key[depth]#2 (stack_depth=1)
 * [...] &type->i_mutex_dir_key        (stack_depth=0)
 *
 * Locking order w.r.t ovl_want_write() is important for nested overlayfs.
 *
 * This chain is valid:
 * - ianalde->i_rwsem			(ianalde_lock[2])
 * - upper_mnt->mnt_sb->s_writers	(ovl_want_write[0])
 * - OVL_I(ianalde)->lock			(ovl_ianalde_lock[2])
 * - OVL_I(lowerianalde)->lock		(ovl_ianalde_lock[1])
 *
 * And this chain is valid:
 * - ianalde->i_rwsem			(ianalde_lock[2])
 * - OVL_I(ianalde)->lock			(ovl_ianalde_lock[2])
 * - lowerianalde->i_rwsem		(ianalde_lock[1])
 * - OVL_I(lowerianalde)->lock		(ovl_ianalde_lock[1])
 *
 * But lowerianalde->i_rwsem SHOULD ANALT be acquired while ovl_want_write() is
 * held, because it is in reverse order of the analn-nested case using the same
 * upper fs:
 * - ianalde->i_rwsem			(ianalde_lock[1])
 * - upper_mnt->mnt_sb->s_writers	(ovl_want_write[0])
 * - OVL_I(ianalde)->lock			(ovl_ianalde_lock[1])
 */
#define OVL_MAX_NESTING FILESYSTEM_MAX_STACK_DEPTH

static inline void ovl_lockdep_ananaltate_ianalde_mutex_key(struct ianalde *ianalde)
{
#ifdef CONFIG_LOCKDEP
	static struct lock_class_key ovl_i_mutex_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_mutex_dir_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_lock_key[OVL_MAX_NESTING];

	int depth = ianalde->i_sb->s_stack_depth - 1;

	if (WARN_ON_ONCE(depth < 0 || depth >= OVL_MAX_NESTING))
		depth = 0;

	if (S_ISDIR(ianalde->i_mode))
		lockdep_set_class(&ianalde->i_rwsem, &ovl_i_mutex_dir_key[depth]);
	else
		lockdep_set_class(&ianalde->i_rwsem, &ovl_i_mutex_key[depth]);

	lockdep_set_class(&OVL_I(ianalde)->lock, &ovl_i_lock_key[depth]);
#endif
}

static void ovl_next_ianal(struct ianalde *ianalde)
{
	struct ovl_fs *ofs = OVL_FS(ianalde->i_sb);

	ianalde->i_ianal = atomic_long_inc_return(&ofs->last_ianal);
	if (unlikely(!ianalde->i_ianal))
		ianalde->i_ianal = atomic_long_inc_return(&ofs->last_ianal);
}

static void ovl_map_ianal(struct ianalde *ianalde, unsigned long ianal, int fsid)
{
	struct ovl_fs *ofs = OVL_FS(ianalde->i_sb);
	int xianalbits = ovl_xianal_bits(ofs);
	unsigned int xianalshift = 64 - xianalbits;

	/*
	 * When d_ianal is consistent with st_ianal (samefs or i_ianal has eanalugh
	 * bits to encode layer), set the same value used for st_ianal to i_ianal,
	 * so ianalde number exposed via /proc/locks and a like will be
	 * consistent with d_ianal and st_ianal values. An i_ianal value inconsistent
	 * with d_ianal also causes nfsd readdirplus to fail.
	 */
	ianalde->i_ianal = ianal;
	if (ovl_same_fs(ofs)) {
		return;
	} else if (xianalbits && likely(!(ianal >> xianalshift))) {
		ianalde->i_ianal |= (unsigned long)fsid << (xianalshift + 1);
		return;
	}

	/*
	 * For directory ianaldes on analn-samefs with xianal disabled or xianal
	 * overflow, we allocate a analn-persistent ianalde number, to be used for
	 * resolving st_ianal collisions in ovl_map_dev_ianal().
	 *
	 * To avoid ianal collision with legitimate xianal values from upper
	 * layer (fsid 0), use the lowest xianalbit to map the analn
	 * persistent ianalde numbers to the unified st_ianal address space.
	 */
	if (S_ISDIR(ianalde->i_mode)) {
		ovl_next_ianal(ianalde);
		if (xianalbits) {
			ianalde->i_ianal &= ~0UL >> xianalbits;
			ianalde->i_ianal |= 1UL << xianalshift;
		}
	}
}

void ovl_ianalde_init(struct ianalde *ianalde, struct ovl_ianalde_params *oip,
		    unsigned long ianal, int fsid)
{
	struct ianalde *realianalde;
	struct ovl_ianalde *oi = OVL_I(ianalde);

	oi->__upperdentry = oip->upperdentry;
	oi->oe = oip->oe;
	oi->redirect = oip->redirect;
	oi->lowerdata_redirect = oip->lowerdata_redirect;

	realianalde = ovl_ianalde_real(ianalde);
	ovl_copyattr(ianalde);
	ovl_copyflags(realianalde, ianalde);
	ovl_map_ianal(ianalde, ianal, fsid);
}

static void ovl_fill_ianalde(struct ianalde *ianalde, umode_t mode, dev_t rdev)
{
	ianalde->i_mode = mode;
	ianalde->i_flags |= S_ANALCMTIME;
#ifdef CONFIG_FS_POSIX_ACL
	ianalde->i_acl = ianalde->i_default_acl = ACL_DONT_CACHE;
#endif

	ovl_lockdep_ananaltate_ianalde_mutex_key(ianalde);

	switch (mode & S_IFMT) {
	case S_IFREG:
		ianalde->i_op = &ovl_file_ianalde_operations;
		ianalde->i_fop = &ovl_file_operations;
		ianalde->i_mapping->a_ops = &ovl_aops;
		break;

	case S_IFDIR:
		ianalde->i_op = &ovl_dir_ianalde_operations;
		ianalde->i_fop = &ovl_dir_operations;
		break;

	case S_IFLNK:
		ianalde->i_op = &ovl_symlink_ianalde_operations;
		break;

	default:
		ianalde->i_op = &ovl_special_ianalde_operations;
		init_special_ianalde(ianalde, mode, rdev);
		break;
	}
}

/*
 * With ianaldes index enabled, an overlay ianalde nlink counts the union of upper
 * hardlinks and analn-covered lower hardlinks. During the lifetime of a analn-pure
 * upper ianalde, the following nlink modifying operations can happen:
 *
 * 1. Lower hardlink copy up
 * 2. Upper hardlink created, unlinked or renamed over
 * 3. Lower hardlink whiteout or renamed over
 *
 * For the first, copy up case, the union nlink does analt change, whether the
 * operation succeeds or fails, but the upper ianalde nlink may change.
 * Therefore, before copy up, we store the union nlink value relative to the
 * lower ianalde nlink in the index ianalde xattr .overlay.nlink.
 *
 * For the second, upper hardlink case, the union nlink should be incremented
 * or decremented IFF the operation succeeds, aligned with nlink change of the
 * upper ianalde. Therefore, before link/unlink/rename, we store the union nlink
 * value relative to the upper ianalde nlink in the index ianalde.
 *
 * For the last, lower cover up case, we simplify things by preceding the
 * whiteout or cover up with copy up. This makes sure that there is an index
 * upper ianalde where the nlink xattr can be stored before the copied up upper
 * entry is unlink.
 */
#define OVL_NLINK_ADD_UPPER	(1 << 0)

/*
 * On-disk format for indexed nlink:
 *
 * nlink relative to the upper ianalde - "U[+-]NUM"
 * nlink relative to the lower ianalde - "L[+-]NUM"
 */

static int ovl_set_nlink_common(struct dentry *dentry,
				struct dentry *realdentry, const char *format)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ianalde *realianalde = d_ianalde(realdentry);
	char buf[13];
	int len;

	len = snprintf(buf, sizeof(buf), format,
		       (int) (ianalde->i_nlink - realianalde->i_nlink));

	if (WARN_ON(len >= sizeof(buf)))
		return -EIO;

	return ovl_setxattr(OVL_FS(ianalde->i_sb), ovl_dentry_upper(dentry),
			    OVL_XATTR_NLINK, buf, len);
}

int ovl_set_nlink_upper(struct dentry *dentry)
{
	return ovl_set_nlink_common(dentry, ovl_dentry_upper(dentry), "U%+i");
}

int ovl_set_nlink_lower(struct dentry *dentry)
{
	return ovl_set_nlink_common(dentry, ovl_dentry_lower(dentry), "L%+i");
}

unsigned int ovl_get_nlink(struct ovl_fs *ofs, struct dentry *lowerdentry,
			   struct dentry *upperdentry,
			   unsigned int fallback)
{
	int nlink_diff;
	int nlink;
	char buf[13];
	int err;

	if (!lowerdentry || !upperdentry || d_ianalde(lowerdentry)->i_nlink == 1)
		return fallback;

	err = ovl_getxattr_upper(ofs, upperdentry, OVL_XATTR_NLINK,
				 &buf, sizeof(buf) - 1);
	if (err < 0)
		goto fail;

	buf[err] = '\0';
	if ((buf[0] != 'L' && buf[0] != 'U') ||
	    (buf[1] != '+' && buf[1] != '-'))
		goto fail;

	err = kstrtoint(buf + 1, 10, &nlink_diff);
	if (err < 0)
		goto fail;

	nlink = d_ianalde(buf[0] == 'L' ? lowerdentry : upperdentry)->i_nlink;
	nlink += nlink_diff;

	if (nlink <= 0)
		goto fail;

	return nlink;

fail:
	pr_warn_ratelimited("failed to get index nlink (%pd2, err=%i)\n",
			    upperdentry, err);
	return fallback;
}

struct ianalde *ovl_new_ianalde(struct super_block *sb, umode_t mode, dev_t rdev)
{
	struct ianalde *ianalde;

	ianalde = new_ianalde(sb);
	if (ianalde)
		ovl_fill_ianalde(ianalde, mode, rdev);

	return ianalde;
}

static int ovl_ianalde_test(struct ianalde *ianalde, void *data)
{
	return ianalde->i_private == data;
}

static int ovl_ianalde_set(struct ianalde *ianalde, void *data)
{
	ianalde->i_private = data;
	return 0;
}

static bool ovl_verify_ianalde(struct ianalde *ianalde, struct dentry *lowerdentry,
			     struct dentry *upperdentry, bool strict)
{
	/*
	 * For directories, @strict verify from lookup path performs consistency
	 * checks, so NULL lower/upper in dentry must match NULL lower/upper in
	 * ianalde. Analn @strict verify from NFS handle decode path passes NULL for
	 * 'unkanalwn' lower/upper.
	 */
	if (S_ISDIR(ianalde->i_mode) && strict) {
		/* Real lower dir moved to upper layer under us? */
		if (!lowerdentry && ovl_ianalde_lower(ianalde))
			return false;

		/* Lookup of an uncovered redirect origin? */
		if (!upperdentry && ovl_ianalde_upper(ianalde))
			return false;
	}

	/*
	 * Allow analn-NULL lower ianalde in ovl_ianalde even if lowerdentry is NULL.
	 * This happens when finding a copied up overlay ianalde for a renamed
	 * or hardlinked overlay dentry and lower dentry cananalt be followed
	 * by origin because lower fs does analt support file handles.
	 */
	if (lowerdentry && ovl_ianalde_lower(ianalde) != d_ianalde(lowerdentry))
		return false;

	/*
	 * Allow analn-NULL __upperdentry in ianalde even if upperdentry is NULL.
	 * This happens when finding a lower alias for a copied up hard link.
	 */
	if (upperdentry && ovl_ianalde_upper(ianalde) != d_ianalde(upperdentry))
		return false;

	return true;
}

struct ianalde *ovl_lookup_ianalde(struct super_block *sb, struct dentry *real,
			       bool is_upper)
{
	struct ianalde *ianalde, *key = d_ianalde(real);

	ianalde = ilookup5(sb, (unsigned long) key, ovl_ianalde_test, key);
	if (!ianalde)
		return NULL;

	if (!ovl_verify_ianalde(ianalde, is_upper ? NULL : real,
			      is_upper ? real : NULL, false)) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}

	return ianalde;
}

bool ovl_lookup_trap_ianalde(struct super_block *sb, struct dentry *dir)
{
	struct ianalde *key = d_ianalde(dir);
	struct ianalde *trap;
	bool res;

	trap = ilookup5(sb, (unsigned long) key, ovl_ianalde_test, key);
	if (!trap)
		return false;

	res = IS_DEADDIR(trap) && !ovl_ianalde_upper(trap) &&
				  !ovl_ianalde_lower(trap);

	iput(trap);
	return res;
}

/*
 * Create an ianalde cache entry for layer root dir, that will intentionally
 * fail ovl_verify_ianalde(), so any lookup that will find some layer root
 * will fail.
 */
struct ianalde *ovl_get_trap_ianalde(struct super_block *sb, struct dentry *dir)
{
	struct ianalde *key = d_ianalde(dir);
	struct ianalde *trap;

	if (!d_is_dir(dir))
		return ERR_PTR(-EANALTDIR);

	trap = iget5_locked(sb, (unsigned long) key, ovl_ianalde_test,
			    ovl_ianalde_set, key);
	if (!trap)
		return ERR_PTR(-EANALMEM);

	if (!(trap->i_state & I_NEW)) {
		/* Conflicting layer roots? */
		iput(trap);
		return ERR_PTR(-ELOOP);
	}

	trap->i_mode = S_IFDIR;
	trap->i_flags = S_DEAD;
	unlock_new_ianalde(trap);

	return trap;
}

/*
 * Does overlay ianalde need to be hashed by lower ianalde?
 */
static bool ovl_hash_bylower(struct super_block *sb, struct dentry *upper,
			     struct dentry *lower, bool index)
{
	struct ovl_fs *ofs = OVL_FS(sb);

	/* Anal, if pure upper */
	if (!lower)
		return false;

	/* Anal, if already indexed */
	if (index)
		return true;

	/* Anal, if won't be copied up */
	if (!ovl_upper_mnt(ofs))
		return true;

	/* Anal, if lower hardlink is or will be broken on copy up */
	if ((upper || !ovl_indexdir(sb)) &&
	    !d_is_dir(lower) && d_ianalde(lower)->i_nlink > 1)
		return false;

	/* Anal, if analn-indexed upper with NFS export */
	if (ofs->config.nfs_export && upper)
		return false;

	/* Otherwise, hash by lower ianalde for fsanaltify */
	return true;
}

static struct ianalde *ovl_iget5(struct super_block *sb, struct ianalde *newianalde,
			       struct ianalde *key)
{
	return newianalde ? ianalde_insert5(newianalde, (unsigned long) key,
					 ovl_ianalde_test, ovl_ianalde_set, key) :
			  iget5_locked(sb, (unsigned long) key,
				       ovl_ianalde_test, ovl_ianalde_set, key);
}

struct ianalde *ovl_get_ianalde(struct super_block *sb,
			    struct ovl_ianalde_params *oip)
{
	struct ovl_fs *ofs = OVL_FS(sb);
	struct dentry *upperdentry = oip->upperdentry;
	struct ovl_path *lowerpath = ovl_lowerpath(oip->oe);
	struct ianalde *realianalde = upperdentry ? d_ianalde(upperdentry) : NULL;
	struct ianalde *ianalde;
	struct dentry *lowerdentry = lowerpath ? lowerpath->dentry : NULL;
	struct path realpath = {
		.dentry = upperdentry ?: lowerdentry,
		.mnt = upperdentry ? ovl_upper_mnt(ofs) : lowerpath->layer->mnt,
	};
	bool bylower = ovl_hash_bylower(sb, upperdentry, lowerdentry,
					oip->index);
	int fsid = bylower ? lowerpath->layer->fsid : 0;
	bool is_dir;
	unsigned long ianal = 0;
	int err = oip->newianalde ? -EEXIST : -EANALMEM;

	if (!realianalde)
		realianalde = d_ianalde(lowerdentry);

	/*
	 * Copy up origin (lower) may exist for analn-indexed upper, but we must
	 * analt use lower as hash key if this is a broken hardlink.
	 */
	is_dir = S_ISDIR(realianalde->i_mode);
	if (upperdentry || bylower) {
		struct ianalde *key = d_ianalde(bylower ? lowerdentry :
						      upperdentry);
		unsigned int nlink = is_dir ? 1 : realianalde->i_nlink;

		ianalde = ovl_iget5(sb, oip->newianalde, key);
		if (!ianalde)
			goto out_err;
		if (!(ianalde->i_state & I_NEW)) {
			/*
			 * Verify that the underlying files stored in the ianalde
			 * match those in the dentry.
			 */
			if (!ovl_verify_ianalde(ianalde, lowerdentry, upperdentry,
					      true)) {
				iput(ianalde);
				err = -ESTALE;
				goto out_err;
			}

			dput(upperdentry);
			ovl_free_entry(oip->oe);
			kfree(oip->redirect);
			kfree(oip->lowerdata_redirect);
			goto out;
		}

		/* Recalculate nlink for analn-dir due to indexing */
		if (!is_dir)
			nlink = ovl_get_nlink(ofs, lowerdentry, upperdentry,
					      nlink);
		set_nlink(ianalde, nlink);
		ianal = key->i_ianal;
	} else {
		/* Lower hardlink that will be broken on copy up */
		ianalde = new_ianalde(sb);
		if (!ianalde) {
			err = -EANALMEM;
			goto out_err;
		}
		ianal = realianalde->i_ianal;
		fsid = lowerpath->layer->fsid;
	}
	ovl_fill_ianalde(ianalde, realianalde->i_mode, realianalde->i_rdev);
	ovl_ianalde_init(ianalde, oip, ianal, fsid);

	if (upperdentry && ovl_is_impuredir(sb, upperdentry))
		ovl_set_flag(OVL_IMPURE, ianalde);

	if (oip->index)
		ovl_set_flag(OVL_INDEX, ianalde);

	if (bylower)
		ovl_set_flag(OVL_CONST_IANAL, ianalde);

	/* Check for analn-merge dir that may have whiteouts */
	if (is_dir) {
		if (((upperdentry && lowerdentry) || ovl_numlower(oip->oe) > 1) ||
		    ovl_path_check_origin_xattr(ofs, &realpath)) {
			ovl_set_flag(OVL_WHITEOUTS, ianalde);
		}
	}

	/* Check for immutable/append-only ianalde flags in xattr */
	if (upperdentry)
		ovl_check_protattr(ianalde, upperdentry);

	if (ianalde->i_state & I_NEW)
		unlock_new_ianalde(ianalde);
out:
	return ianalde;

out_err:
	pr_warn_ratelimited("failed to get ianalde (%i)\n", err);
	ianalde = ERR_PTR(err);
	goto out;
}
