// SPDX-License-Identifier: GPL-2.0-only

#include <linux/fs.h>
#include <linux/xattr.h>
#include "overlayfs.h"

static bool ovl_is_escaped_xattr(struct super_block *sb, const char *name)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	if (ofs->config.userxattr)
		return strncmp(name, OVL_XATTR_ESCAPE_USER_PREFIX,
			       OVL_XATTR_ESCAPE_USER_PREFIX_LEN) == 0;
	else
		return strncmp(name, OVL_XATTR_ESCAPE_TRUSTED_PREFIX,
			       OVL_XATTR_ESCAPE_TRUSTED_PREFIX_LEN - 1) == 0;
}

static bool ovl_is_own_xattr(struct super_block *sb, const char *name)
{
	struct ovl_fs *ofs = OVL_FS(sb);

	if (ofs->config.userxattr)
		return strncmp(name, OVL_XATTR_USER_PREFIX,
			       OVL_XATTR_USER_PREFIX_LEN) == 0;
	else
		return strncmp(name, OVL_XATTR_TRUSTED_PREFIX,
			       OVL_XATTR_TRUSTED_PREFIX_LEN) == 0;
}

bool ovl_is_private_xattr(struct super_block *sb, const char *name)
{
	return ovl_is_own_xattr(sb, name) && !ovl_is_escaped_xattr(sb, name);
}

static int ovl_xattr_set(struct dentry *dentry, struct inode *inode, const char *name,
			 const void *value, size_t size, int flags)
{
	int err;
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	struct dentry *upperdentry = ovl_i_dentry_upper(inode);
	struct dentry *realdentry = upperdentry ?: ovl_dentry_lower(dentry);
	struct path realpath;
	const struct cred *old_cred;

	if (!value && !upperdentry) {
		ovl_path_lower(dentry, &realpath);
		old_cred = ovl_override_creds(dentry->d_sb);
		err = vfs_getxattr(mnt_idmap(realpath.mnt), realdentry, name, NULL, 0);
		ovl_revert_creds(old_cred);
		if (err < 0)
			goto out;
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
	if (value) {
		err = ovl_do_setxattr(ofs, realdentry, name, value, size,
				      flags);
	} else {
		WARN_ON(flags != XATTR_REPLACE);
		err = ovl_do_removexattr(ofs, realdentry, name);
	}
	ovl_revert_creds(old_cred);
	ovl_drop_write(dentry);

	/* copy c/mtime */
	ovl_copyattr(inode);
out:
	return err;
}

static int ovl_xattr_get(struct dentry *dentry, struct inode *inode, const char *name,
			 void *value, size_t size)
{
	ssize_t res;
	const struct cred *old_cred;
	struct path realpath;

	ovl_i_path_real(inode, &realpath);
	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_getxattr(mnt_idmap(realpath.mnt), realpath.dentry, name, value, size);
	ovl_revert_creds(old_cred);
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
	struct ovl_fs *ofs = OVL_FS(dentry->d_sb);
	ssize_t res;
	size_t len;
	char *s;
	const struct cred *old_cred;
	size_t prefix_len, name_len;

	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_listxattr(realdentry, list, size);
	ovl_revert_creds(old_cred);
	if (res <= 0 || size == 0)
		return res;

	prefix_len = ofs->config.userxattr ?
		OVL_XATTR_USER_PREFIX_LEN : OVL_XATTR_TRUSTED_PREFIX_LEN;

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
		} else if (ovl_is_escaped_xattr(dentry->d_sb, s)) {
			res -= OVL_XATTR_ESCAPE_PREFIX_LEN;
			name_len = slen - prefix_len - OVL_XATTR_ESCAPE_PREFIX_LEN;
			s += prefix_len;
			memmove(s, s + OVL_XATTR_ESCAPE_PREFIX_LEN, name_len + len);
			s += name_len;
		} else {
			s += slen;
		}
	}

	return res;
}

static char *ovl_xattr_escape_name(const char *prefix, const char *name)
{
	size_t prefix_len = strlen(prefix);
	size_t name_len = strlen(name);
	size_t escaped_len;
	char *escaped, *s;

	escaped_len = prefix_len + OVL_XATTR_ESCAPE_PREFIX_LEN + name_len;
	if (escaped_len > XATTR_NAME_MAX)
		return ERR_PTR(-EOPNOTSUPP);

	escaped = kmalloc(escaped_len + 1, GFP_KERNEL);
	if (escaped == NULL)
		return ERR_PTR(-ENOMEM);

	s = escaped;
	memcpy(s, prefix, prefix_len);
	s += prefix_len;
	memcpy(s, OVL_XATTR_ESCAPE_PREFIX, OVL_XATTR_ESCAPE_PREFIX_LEN);
	s += OVL_XATTR_ESCAPE_PREFIX_LEN;
	memcpy(s, name, name_len + 1);

	return escaped;
}

static int ovl_own_xattr_get(const struct xattr_handler *handler,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, void *buffer, size_t size)
{
	char *escaped;
	int r;

	escaped = ovl_xattr_escape_name(handler->prefix, name);
	if (IS_ERR(escaped))
		return PTR_ERR(escaped);

	r = ovl_xattr_get(dentry, inode, escaped, buffer, size);

	kfree(escaped);

	return r;
}

static int ovl_own_xattr_set(const struct xattr_handler *handler,
			     struct mnt_idmap *idmap,
			     struct dentry *dentry, struct inode *inode,
			     const char *name, const void *value,
			     size_t size, int flags)
{
	char *escaped;
	int r;

	escaped = ovl_xattr_escape_name(handler->prefix, name);
	if (IS_ERR(escaped))
		return PTR_ERR(escaped);

	r = ovl_xattr_set(dentry, inode, escaped, value, size, flags);

	kfree(escaped);

	return r;
}

static int ovl_other_xattr_get(const struct xattr_handler *handler,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, void *buffer, size_t size)
{
	return ovl_xattr_get(dentry, inode, name, buffer, size);
}

static int ovl_other_xattr_set(const struct xattr_handler *handler,
			       struct mnt_idmap *idmap,
			       struct dentry *dentry, struct inode *inode,
			       const char *name, const void *value,
			       size_t size, int flags)
{
	return ovl_xattr_set(dentry, inode, name, value, size, flags);
}

static const struct xattr_handler ovl_own_trusted_xattr_handler = {
	.prefix	= OVL_XATTR_TRUSTED_PREFIX,
	.get = ovl_own_xattr_get,
	.set = ovl_own_xattr_set,
};

static const struct xattr_handler ovl_own_user_xattr_handler = {
	.prefix	= OVL_XATTR_USER_PREFIX,
	.get = ovl_own_xattr_get,
	.set = ovl_own_xattr_set,
};

static const struct xattr_handler ovl_other_xattr_handler = {
	.prefix	= "", /* catch all */
	.get = ovl_other_xattr_get,
	.set = ovl_other_xattr_set,
};

static const struct xattr_handler * const ovl_trusted_xattr_handlers[] = {
	&ovl_own_trusted_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

static const struct xattr_handler * const ovl_user_xattr_handlers[] = {
	&ovl_own_user_xattr_handler,
	&ovl_other_xattr_handler,
	NULL
};

const struct xattr_handler * const *ovl_xattr_handlers(struct ovl_fs *ofs)
{
	return ofs->config.userxattr ? ovl_user_xattr_handlers :
		ovl_trusted_xattr_handlers;
}
