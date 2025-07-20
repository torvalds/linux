/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_VFS_DEBUG_H
#define LINUX_VFS_DEBUG_H 1

#include <linux/bug.h>

struct inode;

#ifdef CONFIG_DEBUG_VFS
void dump_inode(struct inode *inode, const char *reason);

#define VFS_BUG_ON(cond) BUG_ON(cond)
#define VFS_WARN_ON(cond) (void)WARN_ON(cond)
#define VFS_WARN_ON_ONCE(cond) (void)WARN_ON_ONCE(cond)
#define VFS_WARN_ONCE(cond, format...) (void)WARN_ONCE(cond, format)
#define VFS_WARN(cond, format...) (void)WARN(cond, format)

#define VFS_BUG_ON_INODE(cond, inode)		({			\
	if (unlikely(!!(cond))) {					\
		dump_inode(inode, "VFS_BUG_ON_INODE(" #cond")");\
		BUG_ON(1);						\
	}								\
})

#define VFS_WARN_ON_INODE(cond, inode)		({			\
	int __ret_warn = !!(cond);					\
									\
	if (unlikely(__ret_warn)) {					\
		dump_inode(inode, "VFS_WARN_ON_INODE(" #cond")");\
		WARN_ON(1);						\
	}								\
	unlikely(__ret_warn);						\
})
#else
#define VFS_BUG_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VFS_WARN_ON(cond) BUILD_BUG_ON_INVALID(cond)
#define VFS_WARN_ON_ONCE(cond) BUILD_BUG_ON_INVALID(cond)
#define VFS_WARN_ONCE(cond, format...) BUILD_BUG_ON_INVALID(cond)
#define VFS_WARN(cond, format...) BUILD_BUG_ON_INVALID(cond)

#define VFS_BUG_ON_INODE(cond, inode) VFS_BUG_ON(cond)
#define VFS_WARN_ON_INODE(cond, inode)  BUILD_BUG_ON_INVALID(cond)
#endif /* CONFIG_DEBUG_VFS */

#endif
