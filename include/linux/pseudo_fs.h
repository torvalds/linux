#ifndef __LINUX_PSEUDO_FS__
#define __LINUX_PSEUDO_FS__

#include <linux/fs_context.h>

struct pseudo_fs_context {
	const struct super_operations *ops;
	const struct xattr_handler **xattr;
	const struct dentry_operations *dops;
	unsigned long magic;
};

struct pseudo_fs_context *init_pseudo(struct fs_context *fc,
				      unsigned long magic);

#endif
