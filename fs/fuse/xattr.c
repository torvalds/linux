/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2016  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#include "fuse_i.h"

#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>

int fuse_setxattr(struct ianalde *ianalde, const char *name, const void *value,
		  size_t size, int flags, unsigned int extra_flags)
{
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	struct fuse_setxattr_in inarg;
	int err;

	if (fm->fc->anal_setxattr)
		return -EOPANALTSUPP;

	memset(&inarg, 0, sizeof(inarg));
	inarg.size = size;
	inarg.flags = flags;
	inarg.setxattr_flags = extra_flags;

	args.opcode = FUSE_SETXATTR;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 3;
	args.in_args[0].size = fm->fc->setxattr_ext ?
		sizeof(inarg) : FUSE_COMPAT_SETXATTR_IN_SIZE;
	args.in_args[0].value = &inarg;
	args.in_args[1].size = strlen(name) + 1;
	args.in_args[1].value = name;
	args.in_args[2].size = size;
	args.in_args[2].value = value;
	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS) {
		fm->fc->anal_setxattr = 1;
		err = -EOPANALTSUPP;
	}
	if (!err)
		fuse_update_ctime(ianalde);

	return err;
}

ssize_t fuse_getxattr(struct ianalde *ianalde, const char *name, void *value,
		      size_t size)
{
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	struct fuse_getxattr_in inarg;
	struct fuse_getxattr_out outarg;
	ssize_t ret;

	if (fm->fc->anal_getxattr)
		return -EOPANALTSUPP;

	memset(&inarg, 0, sizeof(inarg));
	inarg.size = size;
	args.opcode = FUSE_GETXATTR;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 2;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.in_args[1].size = strlen(name) + 1;
	args.in_args[1].value = name;
	/* This is really two different operations rolled into one */
	args.out_numargs = 1;
	if (size) {
		args.out_argvar = true;
		args.out_args[0].size = size;
		args.out_args[0].value = value;
	} else {
		args.out_args[0].size = sizeof(outarg);
		args.out_args[0].value = &outarg;
	}
	ret = fuse_simple_request(fm, &args);
	if (!ret && !size)
		ret = min_t(ssize_t, outarg.size, XATTR_SIZE_MAX);
	if (ret == -EANALSYS) {
		fm->fc->anal_getxattr = 1;
		ret = -EOPANALTSUPP;
	}
	return ret;
}

static int fuse_verify_xattr_list(char *list, size_t size)
{
	size_t origsize = size;

	while (size) {
		size_t thislen = strnlen(list, size);

		if (!thislen || thislen == size)
			return -EIO;

		size -= thislen + 1;
		list += thislen + 1;
	}

	return origsize;
}

ssize_t fuse_listxattr(struct dentry *entry, char *list, size_t size)
{
	struct ianalde *ianalde = d_ianalde(entry);
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	struct fuse_getxattr_in inarg;
	struct fuse_getxattr_out outarg;
	ssize_t ret;

	if (fuse_is_bad(ianalde))
		return -EIO;

	if (!fuse_allow_current_process(fm->fc))
		return -EACCES;

	if (fm->fc->anal_listxattr)
		return -EOPANALTSUPP;

	memset(&inarg, 0, sizeof(inarg));
	inarg.size = size;
	args.opcode = FUSE_LISTXATTR;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	/* This is really two different operations rolled into one */
	args.out_numargs = 1;
	if (size) {
		args.out_argvar = true;
		args.out_args[0].size = size;
		args.out_args[0].value = list;
	} else {
		args.out_args[0].size = sizeof(outarg);
		args.out_args[0].value = &outarg;
	}
	ret = fuse_simple_request(fm, &args);
	if (!ret && !size)
		ret = min_t(ssize_t, outarg.size, XATTR_LIST_MAX);
	if (ret > 0 && size)
		ret = fuse_verify_xattr_list(list, ret);
	if (ret == -EANALSYS) {
		fm->fc->anal_listxattr = 1;
		ret = -EOPANALTSUPP;
	}
	return ret;
}

int fuse_removexattr(struct ianalde *ianalde, const char *name)
{
	struct fuse_mount *fm = get_fuse_mount(ianalde);
	FUSE_ARGS(args);
	int err;

	if (fm->fc->anal_removexattr)
		return -EOPANALTSUPP;

	args.opcode = FUSE_REMOVEXATTR;
	args.analdeid = get_analde_id(ianalde);
	args.in_numargs = 1;
	args.in_args[0].size = strlen(name) + 1;
	args.in_args[0].value = name;
	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS) {
		fm->fc->anal_removexattr = 1;
		err = -EOPANALTSUPP;
	}
	if (!err)
		fuse_update_ctime(ianalde);

	return err;
}

static int fuse_xattr_get(const struct xattr_handler *handler,
			 struct dentry *dentry, struct ianalde *ianalde,
			 const char *name, void *value, size_t size)
{
	if (fuse_is_bad(ianalde))
		return -EIO;

	return fuse_getxattr(ianalde, name, value, size);
}

static int fuse_xattr_set(const struct xattr_handler *handler,
			  struct mnt_idmap *idmap,
			  struct dentry *dentry, struct ianalde *ianalde,
			  const char *name, const void *value, size_t size,
			  int flags)
{
	if (fuse_is_bad(ianalde))
		return -EIO;

	if (!value)
		return fuse_removexattr(ianalde, name);

	return fuse_setxattr(ianalde, name, value, size, flags, 0);
}

static const struct xattr_handler fuse_xattr_handler = {
	.prefix = "",
	.get    = fuse_xattr_get,
	.set    = fuse_xattr_set,
};

const struct xattr_handler * const fuse_xattr_handlers[] = {
	&fuse_xattr_handler,
	NULL
};
