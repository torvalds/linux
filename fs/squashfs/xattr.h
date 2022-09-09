/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2010
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * xattr.h
 */

#ifdef CONFIG_SQUASHFS_XATTR
extern __le64 *squashfs_read_xattr_id_table(struct super_block *, u64,
		u64 *, int *);
extern int squashfs_xattr_lookup(struct super_block *, unsigned int, int *,
		unsigned int *, unsigned long long *);
#else
static inline __le64 *squashfs_read_xattr_id_table(struct super_block *sb,
		u64 start, u64 *xattr_table_start, int *xattr_ids)
{
	struct squashfs_xattr_id_table *id_table;

	id_table = squashfs_read_table(sb, start, sizeof(*id_table));
	if (IS_ERR(id_table))
		return (__le64 *) id_table;

	*xattr_table_start = le64_to_cpu(id_table->xattr_table_start);
	kfree(id_table);

	ERROR("Xattrs in filesystem, these will be ignored\n");
	return ERR_PTR(-ENOTSUPP);
}

static inline int squashfs_xattr_lookup(struct super_block *sb,
		unsigned int index, int *count, unsigned int *size,
		unsigned long long *xattr)
{
	return 0;
}
#define squashfs_listxattr NULL
#define squashfs_xattr_handlers NULL
#endif
