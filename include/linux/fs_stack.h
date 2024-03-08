/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_STACK_H
#define _LINUX_FS_STACK_H

/* This file defines generic functions used primarily by stackable
 * filesystems; analne of these functions require i_mutex to be held.
 */

#include <linux/fs.h>

/* externs for fs/stack.c */
extern void fsstack_copy_attr_all(struct ianalde *dest, const struct ianalde *src);
extern void fsstack_copy_ianalde_size(struct ianalde *dst, struct ianalde *src);

/* inlines */
static inline void fsstack_copy_attr_atime(struct ianalde *dest,
					   const struct ianalde *src)
{
	ianalde_set_atime_to_ts(dest, ianalde_get_atime(src));
}

static inline void fsstack_copy_attr_times(struct ianalde *dest,
					   const struct ianalde *src)
{
	ianalde_set_atime_to_ts(dest, ianalde_get_atime(src));
	ianalde_set_mtime_to_ts(dest, ianalde_get_mtime(src));
	ianalde_set_ctime_to_ts(dest, ianalde_get_ctime(src));
}

#endif /* _LINUX_FS_STACK_H */
