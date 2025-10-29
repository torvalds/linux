#ifndef __LINUX_PSEUDO_FS__
#define __LINUX_PSEUDO_FS__

#include <linux/fs_context.h>

struct pseudo_fs_context {
	const struct super_operations *ops;
	const struct export_operations *eops;
	const struct xattr_handler * const *xattr;
	const struct dentry_operations *dops;
	unsigned long magic;
	unsigned int s_d_flags;
};

struct pseudo_fs_context *init_pseudo(struct fs_context *fc,
				      unsigned long magic);

#endif
