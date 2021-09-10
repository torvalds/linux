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
#include <linux/fiemap.h>
#include <linux/fileattr.h>
#include <linux/security.h>
#include <linux/namei.h>
#include "overlayfs.h"


int ovl_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		struct iattr *attr)
{
	int err;
	bool full_copy_up = false;
	struct dentry *upperdentry;
	const struct cred *old_cred;

	err = setattr_prepare(&init_user_ns, dentry, attr);
	if (err)
		return err;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	if (attr->ia_valid & ATTR_SIZE) {
		/* Truncate should trigger data copy up as well */
		full_copy_up = true;
	}

	if (!full_copy_up)
		err = ovl_copy_up(dentry);
	else
		err = ovl_copy_up_with_data(dentry);
	if (!err) {
		struct inode *winode = NULL;

		upperdentry = ovl_dentry_upper(dentry);

		if (attr->ia_valid & ATTR_SIZE) {
			winode = d_inode(upperdentry);
			err = get_write_access(winode);
			if (err)
				goto out_drop_write;
		}

		if (attr->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
			attr->ia_valid &= ~ATTR_MODE;

		/*
		 * We might have to translate ovl file into real file object
		 * once use cases emerge.  For now, simply don't let underlying
		 * filesystem rely on attr->ia_file
		 */
		attr->ia_valid &= ~ATTR_FILE;

		/*
		 * If open(O_TRUNC) is done, VFS calls ->setattr with ATTR_OPEN
		 * set.  Overlayfs does not pass O_TRUNC flag to underlying
		 * filesystem during open -> do not pass ATTR_OPEN.  This
		 * disables optimization in fuse which assumes open(O_TRUNC)
		 * already set file size to 0.  But we never passed O_TRUNC to
		 * fuse.  So by clearing ATTR_OPEN, fuse will be forced to send
		 * setattr request to server.
		 */
		attr->ia_valid &= ~ATTR_OPEN;

		inode_lock(upperdentry->d_inode);
		old_cred = ovl_override_creds(dentry->d_sb);
		err = notify_change(&init_user_ns, upperdentry, attr, NULL);
		revert_creds(old_cred);
		if (!err)
			ovl_copyattr(upperdentry->d_inode, dentry->d_inode);
		inode_unlock(upperdentry->d_inode);

		if (winode)
			put_write_access(winode);
	}
out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

static void ovl_map_dev_ino(struct dentry *dentry, struct kstat *stat, int fsid)
{
	bool samefs = ovl_same_fs(dentry->d_sb);
	unsigned int xinobits = ovl_xino_bits(dentry->d_sb);
	unsigned int xinoshift = 64 - xinobits;

	if (samefs) {
		/*
		 * When all layers are on the same fs, all real inode
		 * number are unique, so we use the overlay st_dev,
		 * which is friendly to du -x.
		 */
		stat->dev = dentry->d_sb->s_dev;
		return;
	} else if (xinobits) {
		/*
		 * All inode numbers of underlying fs should not be using the
		 * high xinobits, so we use high xinobits to partition the
		 * overlay st_ino address space. The high bits holds the fsid
		 * (upper fsid is 0). The lowest xinobit is reserved for mapping
		 * the non-persistent inode numbers range in case of overflow.
		 * This way all overlay inode numbers are unique and use the
		 * overlay st_dev.
		 */
		if (likely(!(stat->ino >> xinoshift))) {
			stat->ino |= ((u64)fsid) << (xinoshift + 1);
			stat->dev = dentry->d_sb->s_dev;
			return;
		} else if (ovl_xino_warn(dentry->d_sb)) {
			pr_warn_ratelimited("inode number too big (%pd2, ino=%llu, xinobits=%d)\n",
					    dentry, stat->ino, xinobits);
		}
	}

	/* The inode could not be mapped to a unified st_ino address space */
	if (S_ISDIR(dentry->d_inode->i_mode)) {
		/*
		 * Always use the overlay st_dev for directories, so 'find
		 * -xdev' will scan the entire overlay mount and won't cross the
		 * overlay mount boundaries.
		 *
		 * If not all layers are on the same fs the pair {real st_ino;
		 * overlay st_dev} is not unique, so use the non persistent
		 * overlay st_ino for directories.
		 */
		stat->dev = dentry->d_sb->s_dev;
		stat->ino = dentry->d_inode->i_ino;
	} else {
		/*
		 * For non-samefs setup, if we cannot map all layers st_ino
		 * to a unified address space, we need to make sure that st_dev
		 * is unique per underlying fs, so we use the unique anonymous
		 * bdev assigned to the underlying fs.
		 */
		stat->dev = OVL_FS(dentry->d_sb)->fs[fsid].pseudo_dev;
	}
}

int ovl_getattr(struct user_namespace *mnt_userns, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	enum ovl_path_type type;
	struct path realpath;
	const struct cred *old_cred;
	struct inode *inode = d_inode(dentry);
	bool is_dir = S_ISDIR(inode->i_mode);
	int fsid = 0;
	int err;
	bool metacopy_blocks = false;

	metacopy_blocks = ovl_is_metacopy_dentry(dentry);

	type = ovl_path_real(dentry, &realpath);
	old_cred = ovl_override_creds(dentry->d_sb);
	err = vfs_getattr(&realpath, stat, request_mask, flags);
	if (err)
		goto out;

	/* Report the effective immutable/append-only STATX flags */
	generic_fill_statx_attr(inode, stat);

	/*
	 * For non-dir or same fs, we use st_ino of the copy up origin.
	 * This guaranties constant st_dev/st_ino across copy up.
	 * With xino feature and non-samefs, we use st_ino of the copy up
	 * origin masked with high bits that represent the layer id.
	 *
	 * If lower filesystem supports NFS file handles, this also guaranties
	 * persistent st_ino across mount cycle.
	 */
	if (!is_dir || ovl_same_dev(dentry->d_sb)) {
		if (!OVL_TYPE_UPPER(type)) {
			fsid = ovl_layer_lower(dentry)->fsid;
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
			 * upper files, so we cannot use the lower origin st_ino
			 * for those different files, even for the same fs case.
			 *
			 * Similarly, several redirected dirs can point to the
			 * same dir on a lower layer. With the "verify_lower"
			 * feature, we do not use the lower origin st_ino, if
			 * we haven't verified that this redirect is unique.
			 *
			 * With inodes index enabled, it is safe to use st_ino
			 * of an indexed origin. The index validates that the
			 * upper hardlink is not broken and that a redirected
			 * dir is the only redirect to that origin.
			 */
			if (ovl_test_flag(OVL_INDEX, d_inode(dentry)) ||
			    (!ovl_verify_lower(dentry->d_sb) &&
			     (is_dir || lowerstat.nlink == 1))) {
				fsid = ovl_layer_lower(dentry)->fsid;
				stat->ino = lowerstat.ino;
			}

			/*
			 * If we are querying a metacopy dentry and lower
			 * dentry is data dentry, then use the blocks we
			 * queried just now. We don't have to do additional
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
			 * If lower is not same as lowerdata or if there was
			 * no origin on upper, we can end up here.
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

	ovl_map_dev_ino(dentry, stat, fsid);

	/*
	 * It's probably not worth it to count subdirs to get the
	 * correct link count.  nlink=1 seems to pacify 'find' and
	 * other utilities.
	 */
	if (is_dir && OVL_TYPE_MERGE(type))
		stat->nlink = 1;

	/*
	 * Return the overlay inode nlinks for indexed upper inodes.
	 * Overlay inode nlink counts the union of the upper hardlinks
	 * and non-covered lower hardlinks. It does not include the upper
	 * index hardlink.
	 */
	if (!is_dir && ovl_test_flag(OVL_INDEX, d_inode(dentry)))
		stat->nlink = dentry->d_inode->i_nlink;

out:
	revert_creds(old_cred);

	return err;
}

int ovl_permission(struct user_namespace *mnt_userns,
		   struct inode *inode, int mask)
{
	struct inode *upperinode = ovl_inode_upper(inode);
	struct inode *realinode = upperinode ?: ovl_inode_lower(inode);
	const struct cred *old_cred;
	int err;

	/* Careful in RCU walk mode */
	if (!realinode) {
		WARN_ON(!(mask & MAY_NOT_BLOCK));
		return -ECHILD;
	}

	/*
	 * Check overlay inode with the creds of task and underlying inode
	 * with creds of mounter
	 */
	err = generic_permission(&init_user_ns, inode, mask);
	if (err)
		return err;

	old_cred = ovl_override_creds(inode->i_sb);
	if (!upperinode &&
	    !special_file(realinode->i_mode) && mask & MAY_WRITE) {
		mask &= ~(MAY_WRITE | MAY_APPEND);
		/* Make sure mounter can read file for copy up later */
		mask |= MAY_READ;
	}
	err = inode_permission(&init_user_ns, realinode, mask);
	revert_creds(old_cred);

	return err;
}

static const char *ovl_get_link(struct dentry *dentry,
				struct inode *inode,
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

bool ovl_is_private_xattr(struct super_block *sb, const char *name)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	if (ofs->config.userxattr)
		return strncmp(name, OVL_XATTR_USER_PREFIX,
			       sizeof(OVL_XATTR_USER_PREFIX) - 1) == 0;
	else
		return strncmp(name, OVL_XATTR_TRUSTED_PREFIX,
			       sizeof(OVL_XATTR_TRUSTED_PREFIX) - 1) == 0;
}

int ovl_xattr_set(struct dentry *dentry, struct inode *inode, const char *name,
		  const void *value, size_t size, int flags)
{
	int err;
	struct dentry *upperdentry = ovl_i_dentry_upper(inode);
	struct dentry *realdentry = upperdentry ?: ovl_dentry_lower(dentry);
	const struct cred *old_cred;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	if (!value && !upperdentry) {
		old_cred = ovl_override_creds(dentry->d_sb);
		err = vfs_getxattr(&init_user_ns, realdentry, name, NULL, 0);
		revert_creds(old_cred);
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
		err = vfs_setxattr(&init_user_ns, realdentry, name, value, size,
				   flags);
	else {
		WARN_ON(flags != XATTR_REPLACE);
		err = vfs_removexattr(&init_user_ns, realdentry, name);
	}
	revert_creds(old_cred);

	/* copy c/mtime */
	ovl_copyattr(d_inode(realdentry), inode);

out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

int ovl_xattr_get(struct dentry *dentry, struct inode *inode, const char *name,
		  void *value, size_t size)
{
	ssize_t res;
	const struct cred *old_cred;
	struct dentry *realdentry =
		ovl_i_dentry_upper(inode) ?: ovl_dentry_lower(dentry);

	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_getxattr(&init_user_ns, realdentry, name, value, size);
	revert_creds(old_cred);
	return res;
}

static bool ovl_can_list(struct super_block *sb, const char *s)
{
	/* Never list private (.overlay) */
	if (ovl_is_private_xattr(sb, s))
		return false;

	/* List all non-trusted xattrs */
	if (strncmp(s, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) != 0)
		return true;

	/* list other trusted for superuser only */
	return ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN);
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
		if (!ovl_can_list(dentry->d_sb, s)) {
			res -= slen;
			memmove(s, s + slen, len);
		} else {
			s += slen;
		}
	}

	return res;
}

struct posix_acl *ovl_get_acl(struct inode *inode, int type, bool rcu)
{
	struct inode *realinode = ovl_inode_real(inode);
	const struct cred *old_cred;
	struct posix_acl *acl;

	if (!IS_ENABLED(CONFIG_FS_POSIX_ACL) || !IS_POSIXACL(realinode))
		return NULL;

	if (rcu)
		return get_cached_acl_rcu(realinode, type);

	old_cred = ovl_override_creds(inode->i_sb);
	acl = get_acl(realinode, type);
	revert_creds(old_cred);

	return acl;
}

int ovl_update_time(struct inode *inode, struct timespec64 *ts, int flags)
{
	if (flags & S_ATIME) {
		struct ovl_fs *ofs = inode->i_sb->s_fs_info;
		struct path upperpath = {
			.mnt = ovl_upper_mnt(ofs),
			.dentry = ovl_upperdentry_dereference(OVL_I(inode)),
		};

		if (upperpath.dentry) {
			touch_atime(&upperpath);
			inode->i_atime = d_inode(upperpath.dentry)->i_atime;
		}
	}
	return 0;
}

static int ovl_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		      u64 start, u64 len)
{
	int err;
	struct inode *realinode = ovl_inode_realdata(inode);
	const struct cred *old_cred;

	if (!realinode->i_op->fiemap)
		return -EOPNOTSUPP;

	old_cred = ovl_override_creds(inode->i_sb);
	err = realinode->i_op->fiemap(realinode, fieinfo, start, len);
	revert_creds(old_cred);

	return err;
}

/*
 * Work around the fact that security_file_ioctl() takes a file argument.
 * Introducing security_inode_fileattr_get/set() hooks would solve this issue
 * properly.
 */
static int ovl_security_fileattr(struct path *realpath, struct fileattr *fa,
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

int ovl_real_fileattr_set(struct path *realpath, struct fileattr *fa)
{
	int err;

	err = ovl_security_fileattr(realpath, fa, true);
	if (err)
		return err;

	return vfs_fileattr_set(&init_user_ns, realpath->dentry, fa);
}

int ovl_fileattr_set(struct user_namespace *mnt_userns,
		     struct dentry *dentry, struct fileattr *fa)
{
	struct inode *inode = d_inode(dentry);
	struct path upperpath;
	const struct cred *old_cred;
	unsigned int flags;
	int err;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	err = ovl_copy_up(dentry);
	if (!err) {
		ovl_path_real(dentry, &upperpath);

		old_cred = ovl_override_creds(inode->i_sb);
		/*
		 * Store immutable/append-only flags in xattr and clear them
		 * in upper fileattr (in case they were set by older kernel)
		 * so children of "ovl-immutable" directories lower aliases of
		 * "ovl-immutable" hardlinks could be copied up.
		 * Clear xattr when flags are cleared.
		 */
		err = ovl_set_protattr(inode, upperpath.dentry, fa);
		if (!err)
			err = ovl_real_fileattr_set(&upperpath, fa);
		revert_creds(old_cred);

		/*
		 * Merge real inode flags with inode flags read from
		 * overlay.protattr xattr
		 */
		flags = ovl_inode_real(inode)->i_flags & OVL_COPY_I_FLAGS_MASK;

		BUILD_BUG_ON(OVL_PROT_I_FLAGS_MASK & ~OVL_COPY_I_FLAGS_MASK);
		flags |= inode->i_flags & OVL_PROT_I_FLAGS_MASK;
		inode_set_flags(inode, flags, OVL_COPY_I_FLAGS_MASK);

		/* Update ctime */
		ovl_copyattr(ovl_inode_real(inode), inode);
	}
	ovl_drop_write(dentry);
out:
	return err;
}

/* Convert inode protection flags to fileattr flags */
static void ovl_fileattr_prot_flags(struct inode *inode, struct fileattr *fa)
{
	BUILD_BUG_ON(OVL_PROT_FS_FLAGS_MASK & ~FS_COMMON_FL);
	BUILD_BUG_ON(OVL_PROT_FSX_FLAGS_MASK & ~FS_XFLAG_COMMON);

	if (inode->i_flags & S_APPEND) {
		fa->flags |= FS_APPEND_FL;
		fa->fsx_xflags |= FS_XFLAG_APPEND;
	}
	if (inode->i_flags & S_IMMUTABLE) {
		fa->flags |= FS_IMMUTABLE_FL;
		fa->fsx_xflags |= FS_XFLAG_IMMUTABLE;
	}
}

int ovl_real_fileattr_get(struct path *realpath, struct fileattr *fa)
{
	int err;

	err = ovl_security_fileattr(realpath, fa, false);
	if (err)
		return err;

	return vfs_fileattr_get(realpath->dentry, fa);
}

int ovl_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct inode *inode = d_inode(dentry);
	struct path realpath;
	const struct cred *old_cred;
	int err;

	ovl_path_real(dentry, &realpath);

	old_cred = ovl_override_creds(inode->i_sb);
	err = ovl_real_fileattr_get(&realpath, fa);
	ovl_fileattr_prot_flags(inode, fa);
	revert_creds(old_cred);

	return err;
}

static const struct inode_operations ovl_file_inode_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_acl	= ovl_get_acl,
	.update_time	= ovl_update_time,
	.fiemap		= ovl_fiemap,
	.fileattr_get	= ovl_fileattr_get,
	.fileattr_set	= ovl_fileattr_set,
};

static const struct inode_operations ovl_symlink_inode_operations = {
	.setattr	= ovl_setattr,
	.get_link	= ovl_get_link,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.update_time	= ovl_update_time,
};

static const struct inode_operations ovl_special_inode_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_acl	= ovl_get_acl,
	.update_time	= ovl_update_time,
};

static const struct address_space_operations ovl_aops = {
	/* For O_DIRECT dentry_open() checks f_mapping->a_ops->direct_IO */
	.direct_IO		= noop_direct_IO,
};

/*
 * It is possible to stack overlayfs instance on top of another
 * overlayfs instance as lower layer. We need to annotate the
 * stackable i_mutex locks according to stack level of the super
 * block instance. An overlayfs instance can never be in stack
 * depth 0 (there is always a real fs below it).  An overlayfs
 * inode lock will use the lockdep annotation ovl_i_mutex_key[depth].
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
 * - inode->i_rwsem			(inode_lock[2])
 * - upper_mnt->mnt_sb->s_writers	(ovl_want_write[0])
 * - OVL_I(inode)->lock			(ovl_inode_lock[2])
 * - OVL_I(lowerinode)->lock		(ovl_inode_lock[1])
 *
 * And this chain is valid:
 * - inode->i_rwsem			(inode_lock[2])
 * - OVL_I(inode)->lock			(ovl_inode_lock[2])
 * - lowerinode->i_rwsem		(inode_lock[1])
 * - OVL_I(lowerinode)->lock		(ovl_inode_lock[1])
 *
 * But lowerinode->i_rwsem SHOULD NOT be acquired while ovl_want_write() is
 * held, because it is in reverse order of the non-nested case using the same
 * upper fs:
 * - inode->i_rwsem			(inode_lock[1])
 * - upper_mnt->mnt_sb->s_writers	(ovl_want_write[0])
 * - OVL_I(inode)->lock			(ovl_inode_lock[1])
 */
#define OVL_MAX_NESTING FILESYSTEM_MAX_STACK_DEPTH

static inline void ovl_lockdep_annotate_inode_mutex_key(struct inode *inode)
{
#ifdef CONFIG_LOCKDEP
	static struct lock_class_key ovl_i_mutex_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_mutex_dir_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_lock_key[OVL_MAX_NESTING];

	int depth = inode->i_sb->s_stack_depth - 1;

	if (WARN_ON_ONCE(depth < 0 || depth >= OVL_MAX_NESTING))
		depth = 0;

	if (S_ISDIR(inode->i_mode))
		lockdep_set_class(&inode->i_rwsem, &ovl_i_mutex_dir_key[depth]);
	else
		lockdep_set_class(&inode->i_rwsem, &ovl_i_mutex_key[depth]);

	lockdep_set_class(&OVL_I(inode)->lock, &ovl_i_lock_key[depth]);
#endif
}

static void ovl_next_ino(struct inode *inode)
{
	struct ovl_fs *ofs = inode->i_sb->s_fs_info;

	inode->i_ino = atomic_long_inc_return(&ofs->last_ino);
	if (unlikely(!inode->i_ino))
		inode->i_ino = atomic_long_inc_return(&ofs->last_ino);
}

static void ovl_map_ino(struct inode *inode, unsigned long ino, int fsid)
{
	int xinobits = ovl_xino_bits(inode->i_sb);
	unsigned int xinoshift = 64 - xinobits;

	/*
	 * When d_ino is consistent with st_ino (samefs or i_ino has enough
	 * bits to encode layer), set the same value used for st_ino to i_ino,
	 * so inode number exposed via /proc/locks and a like will be
	 * consistent with d_ino and st_ino values. An i_ino value inconsistent
	 * with d_ino also causes nfsd readdirplus to fail.
	 */
	inode->i_ino = ino;
	if (ovl_same_fs(inode->i_sb)) {
		return;
	} else if (xinobits && likely(!(ino >> xinoshift))) {
		inode->i_ino |= (unsigned long)fsid << (xinoshift + 1);
		return;
	}

	/*
	 * For directory inodes on non-samefs with xino disabled or xino
	 * overflow, we allocate a non-persistent inode number, to be used for
	 * resolving st_ino collisions in ovl_map_dev_ino().
	 *
	 * To avoid ino collision with legitimate xino values from upper
	 * layer (fsid 0), use the lowest xinobit to map the non
	 * persistent inode numbers to the unified st_ino address space.
	 */
	if (S_ISDIR(inode->i_mode)) {
		ovl_next_ino(inode);
		if (xinobits) {
			inode->i_ino &= ~0UL >> xinobits;
			inode->i_ino |= 1UL << xinoshift;
		}
	}
}

void ovl_inode_init(struct inode *inode, struct ovl_inode_params *oip,
		    unsigned long ino, int fsid)
{
	struct inode *realinode;

	if (oip->upperdentry)
		OVL_I(inode)->__upperdentry = oip->upperdentry;
	if (oip->lowerpath && oip->lowerpath->dentry)
		OVL_I(inode)->lower = igrab(d_inode(oip->lowerpath->dentry));
	if (oip->lowerdata)
		OVL_I(inode)->lowerdata = igrab(d_inode(oip->lowerdata));

	realinode = ovl_inode_real(inode);
	ovl_copyattr(realinode, inode);
	ovl_copyflags(realinode, inode);
	ovl_map_ino(inode, ino, fsid);
}

static void ovl_fill_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_mode = mode;
	inode->i_flags |= S_NOCMTIME;
#ifdef CONFIG_FS_POSIX_ACL
	inode->i_acl = inode->i_default_acl = ACL_DONT_CACHE;
#endif

	ovl_lockdep_annotate_inode_mutex_key(inode);

	switch (mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &ovl_file_inode_operations;
		inode->i_fop = &ovl_file_operations;
		inode->i_mapping->a_ops = &ovl_aops;
		break;

	case S_IFDIR:
		inode->i_op = &ovl_dir_inode_operations;
		inode->i_fop = &ovl_dir_operations;
		break;

	case S_IFLNK:
		inode->i_op = &ovl_symlink_inode_operations;
		break;

	default:
		inode->i_op = &ovl_special_inode_operations;
		init_special_inode(inode, mode, rdev);
		break;
	}
}

/*
 * With inodes index enabled, an overlay inode nlink counts the union of upper
 * hardlinks and non-covered lower hardlinks. During the lifetime of a non-pure
 * upper inode, the following nlink modifying operations can happen:
 *
 * 1. Lower hardlink copy up
 * 2. Upper hardlink created, unlinked or renamed over
 * 3. Lower hardlink whiteout or renamed over
 *
 * For the first, copy up case, the union nlink does not change, whether the
 * operation succeeds or fails, but the upper inode nlink may change.
 * Therefore, before copy up, we store the union nlink value relative to the
 * lower inode nlink in the index inode xattr .overlay.nlink.
 *
 * For the second, upper hardlink case, the union nlink should be incremented
 * or decremented IFF the operation succeeds, aligned with nlink change of the
 * upper inode. Therefore, before link/unlink/rename, we store the union nlink
 * value relative to the upper inode nlink in the index inode.
 *
 * For the last, lower cover up case, we simplify things by preceding the
 * whiteout or cover up with copy up. This makes sure that there is an index
 * upper inode where the nlink xattr can be stored before the copied up upper
 * entry is unlink.
 */
#define OVL_NLINK_ADD_UPPER	(1 << 0)

/*
 * On-disk format for indexed nlink:
 *
 * nlink relative to the upper inode - "U[+-]NUM"
 * nlink relative to the lower inode - "L[+-]NUM"
 */

static int ovl_set_nlink_common(struct dentry *dentry,
				struct dentry *realdentry, const char *format)
{
	struct inode *inode = d_inode(dentry);
	struct inode *realinode = d_inode(realdentry);
	char buf[13];
	int len;

	len = snprintf(buf, sizeof(buf), format,
		       (int) (inode->i_nlink - realinode->i_nlink));

	if (WARN_ON(len >= sizeof(buf)))
		return -EIO;

	return ovl_do_setxattr(OVL_FS(inode->i_sb), ovl_dentry_upper(dentry),
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

	if (!lowerdentry || !upperdentry || d_inode(lowerdentry)->i_nlink == 1)
		return fallback;

	err = ovl_do_getxattr(ofs, upperdentry, OVL_XATTR_NLINK,
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

	nlink = d_inode(buf[0] == 'L' ? lowerdentry : upperdentry)->i_nlink;
	nlink += nlink_diff;

	if (nlink <= 0)
		goto fail;

	return nlink;

fail:
	pr_warn_ratelimited("failed to get index nlink (%pd2, err=%i)\n",
			    upperdentry, err);
	return fallback;
}

struct inode *ovl_new_inode(struct super_block *sb, umode_t mode, dev_t rdev)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode)
		ovl_fill_inode(inode, mode, rdev);

	return inode;
}

static int ovl_inode_test(struct inode *inode, void *data)
{
	return inode->i_private == data;
}

static int ovl_inode_set(struct inode *inode, void *data)
{
	inode->i_private = data;
	return 0;
}

static bool ovl_verify_inode(struct inode *inode, struct dentry *lowerdentry,
			     struct dentry *upperdentry, bool strict)
{
	/*
	 * For directories, @strict verify from lookup path performs consistency
	 * checks, so NULL lower/upper in dentry must match NULL lower/upper in
	 * inode. Non @strict verify from NFS handle decode path passes NULL for
	 * 'unknown' lower/upper.
	 */
	if (S_ISDIR(inode->i_mode) && strict) {
		/* Real lower dir moved to upper layer under us? */
		if (!lowerdentry && ovl_inode_lower(inode))
			return false;

		/* Lookup of an uncovered redirect origin? */
		if (!upperdentry && ovl_inode_upper(inode))
			return false;
	}

	/*
	 * Allow non-NULL lower inode in ovl_inode even if lowerdentry is NULL.
	 * This happens when finding a copied up overlay inode for a renamed
	 * or hardlinked overlay dentry and lower dentry cannot be followed
	 * by origin because lower fs does not support file handles.
	 */
	if (lowerdentry && ovl_inode_lower(inode) != d_inode(lowerdentry))
		return false;

	/*
	 * Allow non-NULL __upperdentry in inode even if upperdentry is NULL.
	 * This happens when finding a lower alias for a copied up hard link.
	 */
	if (upperdentry && ovl_inode_upper(inode) != d_inode(upperdentry))
		return false;

	return true;
}

struct inode *ovl_lookup_inode(struct super_block *sb, struct dentry *real,
			       bool is_upper)
{
	struct inode *inode, *key = d_inode(real);

	inode = ilookup5(sb, (unsigned long) key, ovl_inode_test, key);
	if (!inode)
		return NULL;

	if (!ovl_verify_inode(inode, is_upper ? NULL : real,
			      is_upper ? real : NULL, false)) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return inode;
}

bool ovl_lookup_trap_inode(struct super_block *sb, struct dentry *dir)
{
	struct inode *key = d_inode(dir);
	struct inode *trap;
	bool res;

	trap = ilookup5(sb, (unsigned long) key, ovl_inode_test, key);
	if (!trap)
		return false;

	res = IS_DEADDIR(trap) && !ovl_inode_upper(trap) &&
				  !ovl_inode_lower(trap);

	iput(trap);
	return res;
}

/*
 * Create an inode cache entry for layer root dir, that will intentionally
 * fail ovl_verify_inode(), so any lookup that will find some layer root
 * will fail.
 */
struct inode *ovl_get_trap_inode(struct super_block *sb, struct dentry *dir)
{
	struct inode *key = d_inode(dir);
	struct inode *trap;

	if (!d_is_dir(dir))
		return ERR_PTR(-ENOTDIR);

	trap = iget5_locked(sb, (unsigned long) key, ovl_inode_test,
			    ovl_inode_set, key);
	if (!trap)
		return ERR_PTR(-ENOMEM);

	if (!(trap->i_state & I_NEW)) {
		/* Conflicting layer roots? */
		iput(trap);
		return ERR_PTR(-ELOOP);
	}

	trap->i_mode = S_IFDIR;
	trap->i_flags = S_DEAD;
	unlock_new_inode(trap);

	return trap;
}

/*
 * Does overlay inode need to be hashed by lower inode?
 */
static bool ovl_hash_bylower(struct super_block *sb, struct dentry *upper,
			     struct dentry *lower, bool index)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	/* No, if pure upper */
	if (!lower)
		return false;

	/* Yes, if already indexed */
	if (index)
		return true;

	/* Yes, if won't be copied up */
	if (!ovl_upper_mnt(ofs))
		return true;

	/* No, if lower hardlink is or will be broken on copy up */
	if ((upper || !ovl_indexdir(sb)) &&
	    !d_is_dir(lower) && d_inode(lower)->i_nlink > 1)
		return false;

	/* No, if non-indexed upper with NFS export */
	if (sb->s_export_op && upper)
		return false;

	/* Otherwise, hash by lower inode for fsnotify */
	return true;
}

static struct inode *ovl_iget5(struct super_block *sb, struct inode *newinode,
			       struct inode *key)
{
	return newinode ? inode_insert5(newinode, (unsigned long) key,
					 ovl_inode_test, ovl_inode_set, key) :
			  iget5_locked(sb, (unsigned long) key,
				       ovl_inode_test, ovl_inode_set, key);
}

struct inode *ovl_get_inode(struct super_block *sb,
			    struct ovl_inode_params *oip)
{
	struct ovl_fs *ofs = OVL_FS(sb);
	struct dentry *upperdentry = oip->upperdentry;
	struct ovl_path *lowerpath = oip->lowerpath;
	struct inode *realinode = upperdentry ? d_inode(upperdentry) : NULL;
	struct inode *inode;
	struct dentry *lowerdentry = lowerpath ? lowerpath->dentry : NULL;
	bool bylower = ovl_hash_bylower(sb, upperdentry, lowerdentry,
					oip->index);
	int fsid = bylower ? lowerpath->layer->fsid : 0;
	bool is_dir;
	unsigned long ino = 0;
	int err = oip->newinode ? -EEXIST : -ENOMEM;

	if (!realinode)
		realinode = d_inode(lowerdentry);

	/*
	 * Copy up origin (lower) may exist for non-indexed upper, but we must
	 * not use lower as hash key if this is a broken hardlink.
	 */
	is_dir = S_ISDIR(realinode->i_mode);
	if (upperdentry || bylower) {
		struct inode *key = d_inode(bylower ? lowerdentry :
						      upperdentry);
		unsigned int nlink = is_dir ? 1 : realinode->i_nlink;

		inode = ovl_iget5(sb, oip->newinode, key);
		if (!inode)
			goto out_err;
		if (!(inode->i_state & I_NEW)) {
			/*
			 * Verify that the underlying files stored in the inode
			 * match those in the dentry.
			 */
			if (!ovl_verify_inode(inode, lowerdentry, upperdentry,
					      true)) {
				iput(inode);
				err = -ESTALE;
				goto out_err;
			}

			dput(upperdentry);
			kfree(oip->redirect);
			goto out;
		}

		/* Recalculate nlink for non-dir due to indexing */
		if (!is_dir)
			nlink = ovl_get_nlink(ofs, lowerdentry, upperdentry,
					      nlink);
		set_nlink(inode, nlink);
		ino = key->i_ino;
	} else {
		/* Lower hardlink that will be broken on copy up */
		inode = new_inode(sb);
		if (!inode) {
			err = -ENOMEM;
			goto out_err;
		}
		ino = realinode->i_ino;
		fsid = lowerpath->layer->fsid;
	}
	ovl_fill_inode(inode, realinode->i_mode, realinode->i_rdev);
	ovl_inode_init(inode, oip, ino, fsid);

	if (upperdentry && ovl_is_impuredir(sb, upperdentry))
		ovl_set_flag(OVL_IMPURE, inode);

	if (oip->index)
		ovl_set_flag(OVL_INDEX, inode);

	OVL_I(inode)->redirect = oip->redirect;

	if (bylower)
		ovl_set_flag(OVL_CONST_INO, inode);

	/* Check for non-merge dir that may have whiteouts */
	if (is_dir) {
		if (((upperdentry && lowerdentry) || oip->numlower > 1) ||
		    ovl_check_origin_xattr(ofs, upperdentry ?: lowerdentry)) {
			ovl_set_flag(OVL_WHITEOUTS, inode);
		}
	}

	/* Check for immutable/append-only inode flags in xattr */
	if (upperdentry)
		ovl_check_protattr(inode, upperdentry);

	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);
out:
	return inode;

out_err:
	pr_warn_ratelimited("failed to get inode (%i)\n", err);
	inode = ERR_PTR(err);
	goto out;
}
