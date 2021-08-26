/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 * useful functions for debugging
 */

// clang-format off
#ifndef _LINUX_NTFS3_DEBUG_H
#define _LINUX_NTFS3_DEBUG_H

#ifndef Add2Ptr
#define Add2Ptr(P, I)		((void *)((u8 *)(P) + (I)))
#define PtrOffset(B, O)		((size_t)((size_t)(O) - (size_t)(B)))
#endif

#ifdef CONFIG_PRINTK
__printf(2, 3)
void ntfs_printk(const struct super_block *sb, const char *fmt, ...);
__printf(2, 3)
void ntfs_inode_printk(struct inode *inode, const char *fmt, ...);
#else
static inline __printf(2, 3)
void ntfs_printk(const struct super_block *sb, const char *fmt, ...)
{
}

static inline __printf(2, 3)
void ntfs_inode_printk(struct inode *inode, const char *fmt, ...)
{
}
#endif

/*
 * Logging macros ( thanks Joe Perches <joe@perches.com> for implementation )
 */

#define ntfs_err(sb, fmt, ...)  ntfs_printk(sb, KERN_ERR fmt, ##__VA_ARGS__)
#define ntfs_warn(sb, fmt, ...) ntfs_printk(sb, KERN_WARNING fmt, ##__VA_ARGS__)
#define ntfs_info(sb, fmt, ...) ntfs_printk(sb, KERN_INFO fmt, ##__VA_ARGS__)
#define ntfs_notice(sb, fmt, ...)                                              \
	ntfs_printk(sb, KERN_NOTICE fmt, ##__VA_ARGS__)

#define ntfs_inode_err(inode, fmt, ...)                                        \
	ntfs_inode_printk(inode, KERN_ERR fmt, ##__VA_ARGS__)
#define ntfs_inode_warn(inode, fmt, ...)                                       \
	ntfs_inode_printk(inode, KERN_WARNING fmt, ##__VA_ARGS__)

#define ntfs_malloc(s)		kmalloc(s, GFP_NOFS)
#define ntfs_zalloc(s)		kzalloc(s, GFP_NOFS)
#define ntfs_vmalloc(s)		kvmalloc(s, GFP_KERNEL)
#define ntfs_free(p)		kfree(p)
#define ntfs_vfree(p)		kvfree(p)
#define ntfs_memdup(src, len)	kmemdup(src, len, GFP_NOFS)

#endif /* _LINUX_NTFS3_DEBUG_H */
// clang-format on
