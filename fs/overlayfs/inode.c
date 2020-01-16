// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2011 Novell Inc.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"


int ovl_setattr(struct dentry *dentry, struct iattr *attr)
{
	int err;
	bool full_copy_up = false;
	struct dentry *upperdentry;
	const struct cred *old_cred;

	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	if (attr->ia_valid & ATTR_SIZE) {
		struct iyesde *realiyesde = d_iyesde(ovl_dentry_real(dentry));

		err = -ETXTBSY;
		if (atomic_read(&realiyesde->i_writecount) < 0)
			goto out_drop_write;

		/* Truncate should trigger data copy up as well */
		full_copy_up = true;
	}

	if (!full_copy_up)
		err = ovl_copy_up(dentry);
	else
		err = ovl_copy_up_with_data(dentry);
	if (!err) {
		struct iyesde *wiyesde = NULL;

		upperdentry = ovl_dentry_upper(dentry);

		if (attr->ia_valid & ATTR_SIZE) {
			wiyesde = d_iyesde(upperdentry);
			err = get_write_access(wiyesde);
			if (err)
				goto out_drop_write;
		}

		if (attr->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
			attr->ia_valid &= ~ATTR_MODE;

		iyesde_lock(upperdentry->d_iyesde);
		old_cred = ovl_override_creds(dentry->d_sb);
		err = yestify_change(upperdentry, attr, NULL);
		revert_creds(old_cred);
		if (!err)
			ovl_copyattr(upperdentry->d_iyesde, dentry->d_iyesde);
		iyesde_unlock(upperdentry->d_iyesde);

		if (wiyesde)
			put_write_access(wiyesde);
	}
out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

static int ovl_map_dev_iyes(struct dentry *dentry, struct kstat *stat,
			   struct ovl_layer *lower_layer)
{
	bool samefs = ovl_same_sb(dentry->d_sb);
	unsigned int xiyesbits = ovl_xiyes_bits(dentry->d_sb);

	if (samefs) {
		/*
		 * When all layers are on the same fs, all real iyesde
		 * number are unique, so we use the overlay st_dev,
		 * which is friendly to du -x.
		 */
		stat->dev = dentry->d_sb->s_dev;
		return 0;
	} else if (xiyesbits) {
		unsigned int shift = 64 - xiyesbits;
		/*
		 * All iyesde numbers of underlying fs should yest be using the
		 * high xiyesbits, so we use high xiyesbits to partition the
		 * overlay st_iyes address space. The high bits holds the fsid
		 * (upper fsid is 0). This way overlay iyesde numbers are unique
		 * and all iyesdes use overlay st_dev. Iyesde numbers are also
		 * persistent for a given layer configuration.
		 */
		if (stat->iyes >> shift) {
			pr_warn_ratelimited("overlayfs: iyesde number too big (%pd2, iyes=%llu, xiyesbits=%d)\n",
					    dentry, stat->iyes, xiyesbits);
		} else {
			if (lower_layer)
				stat->iyes |= ((u64)lower_layer->fsid) << shift;

			stat->dev = dentry->d_sb->s_dev;
			return 0;
		}
	}

	/* The iyesde could yest be mapped to a unified st_iyes address space */
	if (S_ISDIR(dentry->d_iyesde->i_mode)) {
		/*
		 * Always use the overlay st_dev for directories, so 'find
		 * -xdev' will scan the entire overlay mount and won't cross the
		 * overlay mount boundaries.
		 *
		 * If yest all layers are on the same fs the pair {real st_iyes;
		 * overlay st_dev} is yest unique, so use the yesn persistent
		 * overlay st_iyes for directories.
		 */
		stat->dev = dentry->d_sb->s_dev;
		stat->iyes = dentry->d_iyesde->i_iyes;
	} else if (lower_layer && lower_layer->fsid) {
		/*
		 * For yesn-samefs setup, if we canyest map all layers st_iyes
		 * to a unified address space, we need to make sure that st_dev
		 * is unique per lower fs. Upper layer uses real st_dev and
		 * lower layers use the unique ayesnymous bdev assigned to the
		 * lower fs.
		 */
		stat->dev = lower_layer->fs->pseudo_dev;
	}

	return 0;
}

int ovl_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	enum ovl_path_type type;
	struct path realpath;
	const struct cred *old_cred;
	bool is_dir = S_ISDIR(dentry->d_iyesde->i_mode);
	bool samefs = ovl_same_sb(dentry->d_sb);
	struct ovl_layer *lower_layer = NULL;
	int err;
	bool metacopy_blocks = false;

	metacopy_blocks = ovl_is_metacopy_dentry(dentry);

	type = ovl_path_real(dentry, &realpath);
	old_cred = ovl_override_creds(dentry->d_sb);
	err = vfs_getattr(&realpath, stat, request_mask, flags);
	if (err)
		goto out;

	/*
	 * For yesn-dir or same fs, we use st_iyes of the copy up origin.
	 * This guaranties constant st_dev/st_iyes across copy up.
	 * With xiyes feature and yesn-samefs, we use st_iyes of the copy up
	 * origin masked with high bits that represent the layer id.
	 *
	 * If lower filesystem supports NFS file handles, this also guaranties
	 * persistent st_iyes across mount cycle.
	 */
	if (!is_dir || samefs || ovl_xiyes_bits(dentry->d_sb)) {
		if (!OVL_TYPE_UPPER(type)) {
			lower_layer = ovl_layer_lower(dentry);
		} else if (OVL_TYPE_ORIGIN(type)) {
			struct kstat lowerstat;
			u32 lowermask = STATX_INO | STATX_BLOCKS |
					(!is_dir ? STATX_NLINK : 0);

			ovl_path_lower(dentry, &realpath);
			err = vfs_getattr(&realpath, &lowerstat,
					  lowermask, flags);
			if (err)
				goto out;

			/*
			 * Lower hardlinks may be broken on copy up to different
			 * upper files, so we canyest use the lower origin st_iyes
			 * for those different files, even for the same fs case.
			 *
			 * Similarly, several redirected dirs can point to the
			 * same dir on a lower layer. With the "verify_lower"
			 * feature, we do yest use the lower origin st_iyes, if
			 * we haven't verified that this redirect is unique.
			 *
			 * With iyesdes index enabled, it is safe to use st_iyes
			 * of an indexed origin. The index validates that the
			 * upper hardlink is yest broken and that a redirected
			 * dir is the only redirect to that origin.
			 */
			if (ovl_test_flag(OVL_INDEX, d_iyesde(dentry)) ||
			    (!ovl_verify_lower(dentry->d_sb) &&
			     (is_dir || lowerstat.nlink == 1))) {
				lower_layer = ovl_layer_lower(dentry);
				/*
				 * Canyest use origin st_dev;st_iyes because
				 * origin iyesde content may differ from overlay
				 * iyesde content.
				 */
				if (samefs || lower_layer->fsid)
					stat->iyes = lowerstat.iyes;
			}

			/*
			 * If we are querying a metacopy dentry and lower
			 * dentry is data dentry, then use the blocks we
			 * queried just yesw. We don't have to do additional
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
			 * If lower is yest same as lowerdata or if there was
			 * yes origin on upper, we can end up here.
			 */
			struct kstat lowerdatastat;
			u32 lowermask = STATX_BLOCKS;

			ovl_path_lowerdata(dentry, &realpath);
			err = vfs_getattr(&realpath, &lowerdatastat,
					  lowermask, flags);
			if (err)
				goto out;
			stat->blocks = lowerdatastat.blocks;
		}
	}

	err = ovl_map_dev_iyes(dentry, stat, lower_layer);
	if (err)
		goto out;

	/*
	 * It's probably yest worth it to count subdirs to get the
	 * correct link count.  nlink=1 seems to pacify 'find' and
	 * other utilities.
	 */
	if (is_dir && OVL_TYPE_MERGE(type))
		stat->nlink = 1;

	/*
	 * Return the overlay iyesde nlinks for indexed upper iyesdes.
	 * Overlay iyesde nlink counts the union of the upper hardlinks
	 * and yesn-covered lower hardlinks. It does yest include the upper
	 * index hardlink.
	 */
	if (!is_dir && ovl_test_flag(OVL_INDEX, d_iyesde(dentry)))
		stat->nlink = dentry->d_iyesde->i_nlink;

out:
	revert_creds(old_cred);

	return err;
}

int ovl_permission(struct iyesde *iyesde, int mask)
{
	struct iyesde *upperiyesde = ovl_iyesde_upper(iyesde);
	struct iyesde *realiyesde = upperiyesde ?: ovl_iyesde_lower(iyesde);
	const struct cred *old_cred;
	int err;

	/* Careful in RCU walk mode */
	if (!realiyesde) {
		WARN_ON(!(mask & MAY_NOT_BLOCK));
		return -ECHILD;
	}

	/*
	 * Check overlay iyesde with the creds of task and underlying iyesde
	 * with creds of mounter
	 */
	err = generic_permission(iyesde, mask);
	if (err)
		return err;

	old_cred = ovl_override_creds(iyesde->i_sb);
	if (!upperiyesde &&
	    !special_file(realiyesde->i_mode) && mask & MAY_WRITE) {
		mask &= ~(MAY_WRITE | MAY_APPEND);
		/* Make sure mounter can read file for copy up later */
		mask |= MAY_READ;
	}
	err = iyesde_permission(realiyesde, mask);
	revert_creds(old_cred);

	return err;
}

static const char *ovl_get_link(struct dentry *dentry,
				struct iyesde *iyesde,
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

bool ovl_is_private_xattr(const char *name)
{
	return strncmp(name, OVL_XATTR_PREFIX,
		       sizeof(OVL_XATTR_PREFIX) - 1) == 0;
}

int ovl_xattr_set(struct dentry *dentry, struct iyesde *iyesde, const char *name,
		  const void *value, size_t size, int flags)
{
	int err;
	struct dentry *upperdentry = ovl_i_dentry_upper(iyesde);
	struct dentry *realdentry = upperdentry ?: ovl_dentry_lower(dentry);
	const struct cred *old_cred;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	if (!value && !upperdentry) {
		err = vfs_getxattr(realdentry, name, NULL, 0);
		if (err < 0)
			goto out_drop_write;
	}

	if (!upperdentry) {
		err = ovl_copy_up(dentry);
		if (err)
			goto out_drop_write;

		realdentry = ovl_dentry_upper(dentry);
	}

	old_cred = ovl_override_creds(dentry->d_sb);
	if (value)
		err = vfs_setxattr(realdentry, name, value, size, flags);
	else {
		WARN_ON(flags != XATTR_REPLACE);
		err = vfs_removexattr(realdentry, name);
	}
	revert_creds(old_cred);

	/* copy c/mtime */
	ovl_copyattr(d_iyesde(realdentry), iyesde);

out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

int ovl_xattr_get(struct dentry *dentry, struct iyesde *iyesde, const char *name,
		  void *value, size_t size)
{
	ssize_t res;
	const struct cred *old_cred;
	struct dentry *realdentry =
		ovl_i_dentry_upper(iyesde) ?: ovl_dentry_lower(dentry);

	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_getxattr(realdentry, name, value, size);
	revert_creds(old_cred);
	return res;
}

static bool ovl_can_list(const char *s)
{
	/* List all yesn-trusted xatts */
	if (strncmp(s, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) != 0)
		return true;

	/* Never list trusted.overlay, list other trusted for superuser only */
	return !ovl_is_private_xattr(s) &&
	       ns_capable_yesaudit(&init_user_ns, CAP_SYS_ADMIN);
}

ssize_t ovl_listxattr(struct dentry *dentry, char *list, size_t size)
{
	struct dentry *realdentry = ovl_dentry_real(dentry);
	ssize_t res;
	size_t len;
	char *s;
	const struct cred *old_cred;

	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_listxattr(realdentry, list, size);
	revert_creds(old_cred);
	if (res <= 0 || size == 0)
		return res;

	/* filter out private xattrs */
	for (s = list, len = res; len;) {
		size_t slen = strnlen(s, len) + 1;

		/* underlying fs providing us with an broken xattr list? */
		if (WARN_ON(slen > len))
			return -EIO;

		len -= slen;
		if (!ovl_can_list(s)) {
			res -= slen;
			memmove(s, s + slen, len);
		} else {
			s += slen;
		}
	}

	return res;
}

struct posix_acl *ovl_get_acl(struct iyesde *iyesde, int type)
{
	struct iyesde *realiyesde = ovl_iyesde_real(iyesde);
	const struct cred *old_cred;
	struct posix_acl *acl;

	if (!IS_ENABLED(CONFIG_FS_POSIX_ACL) || !IS_POSIXACL(realiyesde))
		return NULL;

	old_cred = ovl_override_creds(iyesde->i_sb);
	acl = get_acl(realiyesde, type);
	revert_creds(old_cred);

	return acl;
}

int ovl_update_time(struct iyesde *iyesde, struct timespec64 *ts, int flags)
{
	if (flags & S_ATIME) {
		struct ovl_fs *ofs = iyesde->i_sb->s_fs_info;
		struct path upperpath = {
			.mnt = ofs->upper_mnt,
			.dentry = ovl_upperdentry_dereference(OVL_I(iyesde)),
		};

		if (upperpath.dentry) {
			touch_atime(&upperpath);
			iyesde->i_atime = d_iyesde(upperpath.dentry)->i_atime;
		}
	}
	return 0;
}

static int ovl_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo,
		      u64 start, u64 len)
{
	int err;
	struct iyesde *realiyesde = ovl_iyesde_real(iyesde);
	const struct cred *old_cred;

	if (!realiyesde->i_op->fiemap)
		return -EOPNOTSUPP;

	old_cred = ovl_override_creds(iyesde->i_sb);

	if (fieinfo->fi_flags & FIEMAP_FLAG_SYNC)
		filemap_write_and_wait(realiyesde->i_mapping);

	err = realiyesde->i_op->fiemap(realiyesde, fieinfo, start, len);
	revert_creds(old_cred);

	return err;
}

static const struct iyesde_operations ovl_file_iyesde_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_acl	= ovl_get_acl,
	.update_time	= ovl_update_time,
	.fiemap		= ovl_fiemap,
};

static const struct iyesde_operations ovl_symlink_iyesde_operations = {
	.setattr	= ovl_setattr,
	.get_link	= ovl_get_link,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.update_time	= ovl_update_time,
};

static const struct iyesde_operations ovl_special_iyesde_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_acl	= ovl_get_acl,
	.update_time	= ovl_update_time,
};

static const struct address_space_operations ovl_aops = {
	/* For O_DIRECT dentry_open() checks f_mapping->a_ops->direct_IO */
	.direct_IO		= yesop_direct_IO,
};

/*
 * It is possible to stack overlayfs instance on top of ayesther
 * overlayfs instance as lower layer. We need to anyesnate the
 * stackable i_mutex locks according to stack level of the super
 * block instance. An overlayfs instance can never be in stack
 * depth 0 (there is always a real fs below it).  An overlayfs
 * iyesde lock will use the lockdep anyestaion ovl_i_mutex_key[depth].
 *
 * For example, here is a snip from /proc/lockdep_chains after
 * dir_iterate of nested overlayfs:
 *
 * [...] &ovl_i_mutex_dir_key[depth]   (stack_depth=2)
 * [...] &ovl_i_mutex_dir_key[depth]#2 (stack_depth=1)
 * [...] &type->i_mutex_dir_key        (stack_depth=0)
 */
#define OVL_MAX_NESTING FILESYSTEM_MAX_STACK_DEPTH

static inline void ovl_lockdep_anyestate_iyesde_mutex_key(struct iyesde *iyesde)
{
#ifdef CONFIG_LOCKDEP
	static struct lock_class_key ovl_i_mutex_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_mutex_dir_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_lock_key[OVL_MAX_NESTING];

	int depth = iyesde->i_sb->s_stack_depth - 1;

	if (WARN_ON_ONCE(depth < 0 || depth >= OVL_MAX_NESTING))
		depth = 0;

	if (S_ISDIR(iyesde->i_mode))
		lockdep_set_class(&iyesde->i_rwsem, &ovl_i_mutex_dir_key[depth]);
	else
		lockdep_set_class(&iyesde->i_rwsem, &ovl_i_mutex_key[depth]);

	lockdep_set_class(&OVL_I(iyesde)->lock, &ovl_i_lock_key[depth]);
#endif
}

static void ovl_fill_iyesde(struct iyesde *iyesde, umode_t mode, dev_t rdev,
			   unsigned long iyes, int fsid)
{
	int xiyesbits = ovl_xiyes_bits(iyesde->i_sb);

	/*
	 * When d_iyes is consistent with st_iyes (samefs or i_iyes has eyesugh
	 * bits to encode layer), set the same value used for st_iyes to i_iyes,
	 * so iyesde number exposed via /proc/locks and a like will be
	 * consistent with d_iyes and st_iyes values. An i_iyes value inconsistent
	 * with d_iyes also causes nfsd readdirplus to fail.  When called from
	 * ovl_new_iyesde(), iyes arg is 0, so i_iyes will be updated to real
	 * upper iyesde i_iyes on ovl_iyesde_init() or ovl_iyesde_update().
	 */
	if (ovl_same_sb(iyesde->i_sb) || xiyesbits) {
		iyesde->i_iyes = iyes;
		if (xiyesbits && fsid && !(iyes >> (64 - xiyesbits)))
			iyesde->i_iyes |= (unsigned long)fsid << (64 - xiyesbits);
	} else {
		iyesde->i_iyes = get_next_iyes();
	}
	iyesde->i_mode = mode;
	iyesde->i_flags |= S_NOCMTIME;
#ifdef CONFIG_FS_POSIX_ACL
	iyesde->i_acl = iyesde->i_default_acl = ACL_DONT_CACHE;
#endif

	ovl_lockdep_anyestate_iyesde_mutex_key(iyesde);

	switch (mode & S_IFMT) {
	case S_IFREG:
		iyesde->i_op = &ovl_file_iyesde_operations;
		iyesde->i_fop = &ovl_file_operations;
		iyesde->i_mapping->a_ops = &ovl_aops;
		break;

	case S_IFDIR:
		iyesde->i_op = &ovl_dir_iyesde_operations;
		iyesde->i_fop = &ovl_dir_operations;
		break;

	case S_IFLNK:
		iyesde->i_op = &ovl_symlink_iyesde_operations;
		break;

	default:
		iyesde->i_op = &ovl_special_iyesde_operations;
		init_special_iyesde(iyesde, mode, rdev);
		break;
	}
}

/*
 * With iyesdes index enabled, an overlay iyesde nlink counts the union of upper
 * hardlinks and yesn-covered lower hardlinks. During the lifetime of a yesn-pure
 * upper iyesde, the following nlink modifying operations can happen:
 *
 * 1. Lower hardlink copy up
 * 2. Upper hardlink created, unlinked or renamed over
 * 3. Lower hardlink whiteout or renamed over
 *
 * For the first, copy up case, the union nlink does yest change, whether the
 * operation succeeds or fails, but the upper iyesde nlink may change.
 * Therefore, before copy up, we store the union nlink value relative to the
 * lower iyesde nlink in the index iyesde xattr trusted.overlay.nlink.
 *
 * For the second, upper hardlink case, the union nlink should be incremented
 * or decremented IFF the operation succeeds, aligned with nlink change of the
 * upper iyesde. Therefore, before link/unlink/rename, we store the union nlink
 * value relative to the upper iyesde nlink in the index iyesde.
 *
 * For the last, lower cover up case, we simplify things by preceding the
 * whiteout or cover up with copy up. This makes sure that there is an index
 * upper iyesde where the nlink xattr can be stored before the copied up upper
 * entry is unlink.
 */
#define OVL_NLINK_ADD_UPPER	(1 << 0)

/*
 * On-disk format for indexed nlink:
 *
 * nlink relative to the upper iyesde - "U[+-]NUM"
 * nlink relative to the lower iyesde - "L[+-]NUM"
 */

static int ovl_set_nlink_common(struct dentry *dentry,
				struct dentry *realdentry, const char *format)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct iyesde *realiyesde = d_iyesde(realdentry);
	char buf[13];
	int len;

	len = snprintf(buf, sizeof(buf), format,
		       (int) (iyesde->i_nlink - realiyesde->i_nlink));

	if (WARN_ON(len >= sizeof(buf)))
		return -EIO;

	return ovl_do_setxattr(ovl_dentry_upper(dentry),
			       OVL_XATTR_NLINK, buf, len, 0);
}

int ovl_set_nlink_upper(struct dentry *dentry)
{
	return ovl_set_nlink_common(dentry, ovl_dentry_upper(dentry), "U%+i");
}

int ovl_set_nlink_lower(struct dentry *dentry)
{
	return ovl_set_nlink_common(dentry, ovl_dentry_lower(dentry), "L%+i");
}

unsigned int ovl_get_nlink(struct dentry *lowerdentry,
			   struct dentry *upperdentry,
			   unsigned int fallback)
{
	int nlink_diff;
	int nlink;
	char buf[13];
	int err;

	if (!lowerdentry || !upperdentry || d_iyesde(lowerdentry)->i_nlink == 1)
		return fallback;

	err = vfs_getxattr(upperdentry, OVL_XATTR_NLINK, &buf, sizeof(buf) - 1);
	if (err < 0)
		goto fail;

	buf[err] = '\0';
	if ((buf[0] != 'L' && buf[0] != 'U') ||
	    (buf[1] != '+' && buf[1] != '-'))
		goto fail;

	err = kstrtoint(buf + 1, 10, &nlink_diff);
	if (err < 0)
		goto fail;

	nlink = d_iyesde(buf[0] == 'L' ? lowerdentry : upperdentry)->i_nlink;
	nlink += nlink_diff;

	if (nlink <= 0)
		goto fail;

	return nlink;

fail:
	pr_warn_ratelimited("overlayfs: failed to get index nlink (%pd2, err=%i)\n",
			    upperdentry, err);
	return fallback;
}

struct iyesde *ovl_new_iyesde(struct super_block *sb, umode_t mode, dev_t rdev)
{
	struct iyesde *iyesde;

	iyesde = new_iyesde(sb);
	if (iyesde)
		ovl_fill_iyesde(iyesde, mode, rdev, 0, 0);

	return iyesde;
}

static int ovl_iyesde_test(struct iyesde *iyesde, void *data)
{
	return iyesde->i_private == data;
}

static int ovl_iyesde_set(struct iyesde *iyesde, void *data)
{
	iyesde->i_private = data;
	return 0;
}

static bool ovl_verify_iyesde(struct iyesde *iyesde, struct dentry *lowerdentry,
			     struct dentry *upperdentry, bool strict)
{
	/*
	 * For directories, @strict verify from lookup path performs consistency
	 * checks, so NULL lower/upper in dentry must match NULL lower/upper in
	 * iyesde. Non @strict verify from NFS handle decode path passes NULL for
	 * 'unkyeswn' lower/upper.
	 */
	if (S_ISDIR(iyesde->i_mode) && strict) {
		/* Real lower dir moved to upper layer under us? */
		if (!lowerdentry && ovl_iyesde_lower(iyesde))
			return false;

		/* Lookup of an uncovered redirect origin? */
		if (!upperdentry && ovl_iyesde_upper(iyesde))
			return false;
	}

	/*
	 * Allow yesn-NULL lower iyesde in ovl_iyesde even if lowerdentry is NULL.
	 * This happens when finding a copied up overlay iyesde for a renamed
	 * or hardlinked overlay dentry and lower dentry canyest be followed
	 * by origin because lower fs does yest support file handles.
	 */
	if (lowerdentry && ovl_iyesde_lower(iyesde) != d_iyesde(lowerdentry))
		return false;

	/*
	 * Allow yesn-NULL __upperdentry in iyesde even if upperdentry is NULL.
	 * This happens when finding a lower alias for a copied up hard link.
	 */
	if (upperdentry && ovl_iyesde_upper(iyesde) != d_iyesde(upperdentry))
		return false;

	return true;
}

struct iyesde *ovl_lookup_iyesde(struct super_block *sb, struct dentry *real,
			       bool is_upper)
{
	struct iyesde *iyesde, *key = d_iyesde(real);

	iyesde = ilookup5(sb, (unsigned long) key, ovl_iyesde_test, key);
	if (!iyesde)
		return NULL;

	if (!ovl_verify_iyesde(iyesde, is_upper ? NULL : real,
			      is_upper ? real : NULL, false)) {
		iput(iyesde);
		return ERR_PTR(-ESTALE);
	}

	return iyesde;
}

bool ovl_lookup_trap_iyesde(struct super_block *sb, struct dentry *dir)
{
	struct iyesde *key = d_iyesde(dir);
	struct iyesde *trap;
	bool res;

	trap = ilookup5(sb, (unsigned long) key, ovl_iyesde_test, key);
	if (!trap)
		return false;

	res = IS_DEADDIR(trap) && !ovl_iyesde_upper(trap) &&
				  !ovl_iyesde_lower(trap);

	iput(trap);
	return res;
}

/*
 * Create an iyesde cache entry for layer root dir, that will intentionally
 * fail ovl_verify_iyesde(), so any lookup that will find some layer root
 * will fail.
 */
struct iyesde *ovl_get_trap_iyesde(struct super_block *sb, struct dentry *dir)
{
	struct iyesde *key = d_iyesde(dir);
	struct iyesde *trap;

	if (!d_is_dir(dir))
		return ERR_PTR(-ENOTDIR);

	trap = iget5_locked(sb, (unsigned long) key, ovl_iyesde_test,
			    ovl_iyesde_set, key);
	if (!trap)
		return ERR_PTR(-ENOMEM);

	if (!(trap->i_state & I_NEW)) {
		/* Conflicting layer roots? */
		iput(trap);
		return ERR_PTR(-ELOOP);
	}

	trap->i_mode = S_IFDIR;
	trap->i_flags = S_DEAD;
	unlock_new_iyesde(trap);

	return trap;
}

/*
 * Does overlay iyesde need to be hashed by lower iyesde?
 */
static bool ovl_hash_bylower(struct super_block *sb, struct dentry *upper,
			     struct dentry *lower, struct dentry *index)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	/* No, if pure upper */
	if (!lower)
		return false;

	/* Yes, if already indexed */
	if (index)
		return true;

	/* Yes, if won't be copied up */
	if (!ofs->upper_mnt)
		return true;

	/* No, if lower hardlink is or will be broken on copy up */
	if ((upper || !ovl_indexdir(sb)) &&
	    !d_is_dir(lower) && d_iyesde(lower)->i_nlink > 1)
		return false;

	/* No, if yesn-indexed upper with NFS export */
	if (sb->s_export_op && upper)
		return false;

	/* Otherwise, hash by lower iyesde for fsyestify */
	return true;
}

static struct iyesde *ovl_iget5(struct super_block *sb, struct iyesde *newiyesde,
			       struct iyesde *key)
{
	return newiyesde ? iyesde_insert5(newiyesde, (unsigned long) key,
					 ovl_iyesde_test, ovl_iyesde_set, key) :
			  iget5_locked(sb, (unsigned long) key,
				       ovl_iyesde_test, ovl_iyesde_set, key);
}

struct iyesde *ovl_get_iyesde(struct super_block *sb,
			    struct ovl_iyesde_params *oip)
{
	struct dentry *upperdentry = oip->upperdentry;
	struct ovl_path *lowerpath = oip->lowerpath;
	struct iyesde *realiyesde = upperdentry ? d_iyesde(upperdentry) : NULL;
	struct iyesde *iyesde;
	struct dentry *lowerdentry = lowerpath ? lowerpath->dentry : NULL;
	bool bylower = ovl_hash_bylower(sb, upperdentry, lowerdentry,
					oip->index);
	int fsid = bylower ? oip->lowerpath->layer->fsid : 0;
	bool is_dir, metacopy = false;
	unsigned long iyes = 0;
	int err = oip->newiyesde ? -EEXIST : -ENOMEM;

	if (!realiyesde)
		realiyesde = d_iyesde(lowerdentry);

	/*
	 * Copy up origin (lower) may exist for yesn-indexed upper, but we must
	 * yest use lower as hash key if this is a broken hardlink.
	 */
	is_dir = S_ISDIR(realiyesde->i_mode);
	if (upperdentry || bylower) {
		struct iyesde *key = d_iyesde(bylower ? lowerdentry :
						      upperdentry);
		unsigned int nlink = is_dir ? 1 : realiyesde->i_nlink;

		iyesde = ovl_iget5(sb, oip->newiyesde, key);
		if (!iyesde)
			goto out_err;
		if (!(iyesde->i_state & I_NEW)) {
			/*
			 * Verify that the underlying files stored in the iyesde
			 * match those in the dentry.
			 */
			if (!ovl_verify_iyesde(iyesde, lowerdentry, upperdentry,
					      true)) {
				iput(iyesde);
				err = -ESTALE;
				goto out_err;
			}

			dput(upperdentry);
			kfree(oip->redirect);
			goto out;
		}

		/* Recalculate nlink for yesn-dir due to indexing */
		if (!is_dir)
			nlink = ovl_get_nlink(lowerdentry, upperdentry, nlink);
		set_nlink(iyesde, nlink);
		iyes = key->i_iyes;
	} else {
		/* Lower hardlink that will be broken on copy up */
		iyesde = new_iyesde(sb);
		if (!iyesde) {
			err = -ENOMEM;
			goto out_err;
		}
	}
	ovl_fill_iyesde(iyesde, realiyesde->i_mode, realiyesde->i_rdev, iyes, fsid);
	ovl_iyesde_init(iyesde, upperdentry, lowerdentry, oip->lowerdata);

	if (upperdentry && ovl_is_impuredir(upperdentry))
		ovl_set_flag(OVL_IMPURE, iyesde);

	if (oip->index)
		ovl_set_flag(OVL_INDEX, iyesde);

	if (upperdentry) {
		err = ovl_check_metacopy_xattr(upperdentry);
		if (err < 0)
			goto out_err;
		metacopy = err;
		if (!metacopy)
			ovl_set_flag(OVL_UPPERDATA, iyesde);
	}

	OVL_I(iyesde)->redirect = oip->redirect;

	if (bylower)
		ovl_set_flag(OVL_CONST_INO, iyesde);

	/* Check for yesn-merge dir that may have whiteouts */
	if (is_dir) {
		if (((upperdentry && lowerdentry) || oip->numlower > 1) ||
		    ovl_check_origin_xattr(upperdentry ?: lowerdentry)) {
			ovl_set_flag(OVL_WHITEOUTS, iyesde);
		}
	}

	if (iyesde->i_state & I_NEW)
		unlock_new_iyesde(iyesde);
out:
	return iyesde;

out_err:
	pr_warn_ratelimited("overlayfs: failed to get iyesde (%i)\n", err);
	iyesde = ERR_PTR(err);
	goto out;
}
