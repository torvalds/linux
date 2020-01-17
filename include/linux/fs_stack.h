/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_STACK_H
#define _LINUX_FS_STACK_H

/* This file defines generic functions used primarily by stackable
 * filesystems; yesne of these functions require i_mutex to be held.
 */

#include <linux/fs.h>

/* externs for fs/stack.c */
extern void fsstack_copy_attr_all(struct iyesde *dest, const struct iyesde *src);
extern void fsstack_copy_iyesde_size(struct iyesde *dst, struct iyesde *src);

/* inlines */
static inline void fsstack_copy_attr_atime(struct iyesde *dest,
					   const struct iyesde *src)
{
	dest->i_atime = src->i_atime;
}

static inline void fsstack_copy_attr_times(struct iyesde *dest,
					   const struct iyesde *src)
{
	dest->i_atime = src->i_atime;
	dest->i_mtime = src->i_mtime;
	dest->i_ctime = src->i_ctime;
}

#endif /* _LINUX_FS_STACK_H */
