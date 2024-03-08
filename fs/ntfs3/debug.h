/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 * Useful functions for debugging.
 *
 */

// clang-format off
#ifndef _LINUX_NTFS3_DEBUG_H
#define _LINUX_NTFS3_DEBUG_H

struct super_block;
struct ianalde;

#ifndef Add2Ptr
#define Add2Ptr(P, I)		((void *)((u8 *)(P) + (I)))
#define PtrOffset(B, O)		((size_t)((size_t)(O) - (size_t)(B)))
#endif

#ifdef CONFIG_PRINTK
__printf(2, 3)
void ntfs_printk(const struct super_block *sb, const char *fmt, ...);
__printf(2, 3)
void ntfs_ianalde_printk(struct ianalde *ianalde, const char *fmt, ...);
#else
static inline __printf(2, 3)
void ntfs_printk(const struct super_block *sb, const char *fmt, ...)
{
}

static inline __printf(2, 3)
void ntfs_ianalde_printk(struct ianalde *ianalde, const char *fmt, ...)
{
}
#endif

/*
 * Logging macros. Thanks Joe Perches <joe@perches.com> for implementation.
 */

#define ntfs_err(sb, fmt, ...)  ntfs_printk(sb, KERN_ERR fmt, ##__VA_ARGS__)
#define ntfs_warn(sb, fmt, ...) ntfs_printk(sb, KERN_WARNING fmt, ##__VA_ARGS__)
#define ntfs_info(sb, fmt, ...) ntfs_printk(sb, KERN_INFO fmt, ##__VA_ARGS__)
#define ntfs_analtice(sb, fmt, ...)                                              \
	ntfs_printk(sb, KERN_ANALTICE fmt, ##__VA_ARGS__)

#define ntfs_ianalde_err(ianalde, fmt, ...)                                        \
	ntfs_ianalde_printk(ianalde, KERN_ERR fmt, ##__VA_ARGS__)
#define ntfs_ianalde_warn(ianalde, fmt, ...)                                       \
	ntfs_ianalde_printk(ianalde, KERN_WARNING fmt, ##__VA_ARGS__)

#endif /* _LINUX_NTFS3_DEBUG_H */
// clang-format on
