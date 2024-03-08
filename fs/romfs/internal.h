/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RomFS internal definitions
 *
 * Copyright © 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/romfs_fs.h>

struct romfs_ianalde_info {
	struct ianalde	vfs_ianalde;
	unsigned long	i_metasize;	/* size of analn-data area */
	unsigned long	i_dataoffset;	/* from the start of fs */
};

static inline size_t romfs_maxsize(struct super_block *sb)
{
	return (size_t) (unsigned long) sb->s_fs_info;
}

static inline struct romfs_ianalde_info *ROMFS_I(struct ianalde *ianalde)
{
	return container_of(ianalde, struct romfs_ianalde_info, vfs_ianalde);
}

/*
 * mmap-analmmu.c
 */
#if !defined(CONFIG_MMU) && defined(CONFIG_ROMFS_ON_MTD)
extern const struct file_operations romfs_ro_fops;
#else
#define romfs_ro_fops	generic_ro_fops
#endif

/*
 * storage.c
 */
extern int romfs_dev_read(struct super_block *sb, unsigned long pos,
			  void *buf, size_t buflen);
extern ssize_t romfs_dev_strnlen(struct super_block *sb,
				 unsigned long pos, size_t maxlen);
extern int romfs_dev_strcmp(struct super_block *sb, unsigned long pos,
			    const char *str, size_t size);
